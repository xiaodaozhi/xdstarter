"""Classify complete Git changes for observation only; never skip CI work."""
from __future__ import annotations

import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SHA = re.compile(r'[0-9a-f]{40}')


def git(root, *args):
    return subprocess.run(['git', *args], cwd=root, capture_output=True, check=True,
                          timeout=30).stdout


def classify(root=ROOT, env=None, payload=None):
    env = os.environ if env is None else env
    revision = env.get('GITHUB_SHA', '')
    result = {'revision': revision, 'run_id': env.get('GITHUB_RUN_ID', ''),
              'run_attempt': env.get('GITHUB_RUN_ATTEMPT', ''), 'classification': 'full',
              'reason': 'classification-failed', 'would_skip': False, 'run_heavy': True}
    try:
        if not SHA.fullmatch(revision) or git(root, 'rev-parse', 'HEAD').decode().strip() != revision:
            raise ValueError('checkout-mismatch')
        if payload is None:
            payload = json.loads(Path(env['GITHUB_EVENT_PATH']).read_text(encoding='utf-8'))
        event = env.get('GITHUB_EVENT_NAME')
        if event == 'push' and env.get('GITHUB_REF') == 'refs/heads/main':
            base = payload['before']
            if payload['after'] != revision:
                raise ValueError('push-mismatch')
        elif event == 'pull_request':
            pr = payload['pull_request']
            if pr['head']['repo']['full_name'] != env.get('GITHUB_REPOSITORY'):
                raise ValueError('fork-pr')
            base, head = pr['base']['sha'], pr['head']['sha']
            if not SHA.fullmatch(head) or git(root, 'rev-list', '--parents', '-n', '1', revision).decode().split() != [revision, base, head]:
                raise ValueError('unverified-pr-merge')
        else:
            raise ValueError('manual-tag-or-unknown-event')
        if not SHA.fullmatch(base) or base == '0' * 40:
            raise ValueError('missing-base')
        git(root, 'merge-base', '--is-ancestor', base, revision)
        raw = git(root, 'diff', '--raw', '-z', '--no-renames', '--no-abbrev', base, revision, '--')
        if len(raw) > 1048576:
            raise ValueError('large-diff')
        parts = raw.split(b'\0')
        if parts[-1] or len(parts) != 3:
            raise ValueError('multiple-or-incomplete-changes')
        header = parts[0].decode('ascii').split()
        name = parts[1].decode('utf-8')
        if (name != 'docs/README.md' or len(header) != 5 or header[:2] != [':100644', '100644']
                or header[4] != 'M' or any(not SHA.fullmatch(blob) for blob in header[2:4])):
            raise ValueError('runtime-contract-or-unknown-path')
        for blob in header[2:4]:
            if int(git(root, 'cat-file', '-s', blob)) > 131072:
                raise ValueError('large-document')
            data = git(root, 'cat-file', 'blob', blob)
            if b'\0' in data:
                raise ValueError('binary-document')
            data.decode('utf-8')
        result.update(classification='plain-docs', reason='navigation-document-only',
                      would_skip=True, base=base)
    except (OSError, ValueError, KeyError, TypeError, UnicodeError, subprocess.SubprocessError) as error:
        reason = str(error) if type(error) is ValueError else ''
        result['reason'] = reason if re.fullmatch(r'[a-z][a-z0-9-]{1,80}', reason) else 'classification-read-failed'
    return result


def main():
    result = classify()
    path = ROOT / 'build/ci-evidence/plan.json'
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    if output := os.environ.get('GITHUB_OUTPUT'):
        with Path(output).open('a', encoding='utf-8') as stream:
            stream.write('classification=' + result['classification'] + '\n')
            stream.write('would_skip=' + str(result['would_skip']).lower() + '\n')
            stream.write('run_heavy=true\n')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
