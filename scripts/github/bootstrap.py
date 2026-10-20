"""Idempotent GitHub labels, milestones and initial Issues. Dry run by default."""
import argparse
import json
import os
from pathlib import Path
import sys
import urllib.error
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
config = json.loads((ROOT / '.github/project-bootstrap.json').read_text(encoding='utf-8'))
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--apply', action='store_true', help='Write to the configured GitHub repository')
args = parser.parse_args()

if not args.apply:
    print(f'DRY RUN: {config["repository"]}: {len(config["labels"])} labels, {len(config["milestones"])} milestones, {len(config["seedIssues"])} initial Issues.')
    print('Existing Issues, their progress, bodies, assignees and state will not be overwritten.')
    sys.exit(0)

token = os.environ.get('GH_TOKEN') or os.environ.get('GITHUB_TOKEN')
if not token:
    sys.exit('GH_TOKEN or GITHUB_TOKEN is required for --apply.')
repo = os.environ.get('GITHUB_REPOSITORY', config['repository'])
if repo != config['repository']:
    sys.exit('Configured repository differs from GITHUB_REPOSITORY; refusing cross-repository writes.')

def api(method, endpoint, payload=None):
    data = json.dumps(payload).encode('utf-8') if payload is not None else None
    request = urllib.request.Request(
        f'https://api.github.com/repos/{repo}/{endpoint}', data=data, method=method,
        headers={'Authorization': f'Bearer {token}', 'Accept': 'application/vnd.github+json',
                 'X-GitHub-Api-Version': '2022-11-28', 'Content-Type': 'application/json',
                 'User-Agent': 'xdstarter-project-bootstrap'})
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            return json.load(response)
    except urllib.error.HTTPError as error:
        try:
            message = json.load(error).get('message', error.reason)
        except (ValueError, OSError):
            message = error.reason
        raise RuntimeError(f'GitHub {method} {endpoint}: HTTP {error.code}: {message}') from None

def pages(endpoint):
    output, page = [], 1
    separator = '&' if '?' in endpoint else '?'
    while True:
        batch = api('GET', f'{endpoint}{separator}per_page=100&page={page}')
        output.extend(batch)
        if len(batch) < 100:
            return output
        page += 1

try:
    labels = {item['name'] for item in pages('labels')}
    for label in config['labels']:
        if label['name'] not in labels:
            api('POST', 'labels', label)
            print(f'Created label: {label["name"]}')
    milestones = {item['title']: item for item in pages('milestones?state=all')}
    for milestone in config['milestones']:
        if milestone['title'] not in milestones:
            created = api('POST', 'milestones', milestone)
            milestones[created['title']] = created
            print(f'Created milestone: {created["html_url"]}')
    issues = [item for item in pages('issues?state=all') if 'pull_request' not in item]
    for seed in config['seedIssues']:
        marker = f'<!-- xdstarter-bootstrap:{seed["key"]} -->'
        existing = next((item for item in issues if marker in (item.get('body') or '')), None)
        if existing:
            print(f'Kept existing Issue: {existing["html_url"]}')
            continue
        created = api('POST', 'issues', {
            'title': seed['title'], 'body': marker + '\n\n' + seed['body'],
            'labels': seed['labels'], 'milestone': milestones[seed['milestone']]['number']})
        issues.append(created)
        print(f'Created Issue: {created["html_url"]}')
except (RuntimeError, urllib.error.URLError, OSError) as error:
    sys.exit(str(error))
