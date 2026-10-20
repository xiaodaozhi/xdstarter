"""Exercise actual bootstrap code against a fake GitHub API; no network calls."""
from contextlib import redirect_stdout
from copy import deepcopy
import io
import json
import os
from pathlib import Path
import runpy
import sys
import unittest
from unittest.mock import patch
from urllib.parse import urlsplit

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / 'scripts/github/bootstrap.py'
CONFIG = json.loads((ROOT / '.github/project-bootstrap.json').read_text(encoding='utf-8'))

class FakeGitHub:
    def __init__(self):
        self.labels = []
        self.milestones = []
        self.issues = []
        self.writes = 0

    def urlopen(self, request, timeout):
        self_endpoint = urlsplit(request.full_url).path.rsplit('/', 1)[-1]
        collection = {'labels': self.labels, 'milestones': self.milestones, 'issues': self.issues}[self_endpoint]
        if request.method == 'GET':
            response = collection
        else:
            assert request.method == 'POST'
            self.writes += 1
            response = json.loads(request.data)
            response.update(number=len(collection) + 1,
                            html_url=f'https://github.com/{CONFIG["repository"]}/{self_endpoint}/{len(collection)+1}')
            collection.append(response)
        return io.BytesIO(json.dumps(response).encode('utf-8'))

class BootstrapTests(unittest.TestCase):
    def run_script(self, api, apply=True, repo=None):
        env = {'GH_TOKEN': 'test-only-placeholder'}
        if repo is not None:
            env['GITHUB_REPOSITORY'] = repo
        with patch.dict(os.environ, env, clear=True), patch.object(sys, 'argv', [str(SCRIPT)] + (['--apply'] if apply else [])), patch('urllib.request.urlopen', api.urlopen), redirect_stdout(io.StringIO()):
            return runpy.run_path(str(SCRIPT), run_name='__main__')

    def test_creates_everything_and_repeated_apply_is_read_only(self):
        api = FakeGitHub()
        self.run_script(api)
        self.assertEqual(len(api.labels), len(CONFIG['labels']))
        self.assertEqual(len(api.milestones), len(CONFIG['milestones']))
        self.assertEqual(len(api.issues), len(CONFIG['seedIssues']))
        first_count = api.writes
        self.run_script(api)
        self.assertEqual(api.writes, first_count)

    def test_existing_issue_progress_and_body_are_preserved(self):
        api = FakeGitHub()
        self.run_script(api)
        api.issues[0].update(state='closed', assignees=['maintainer'], labels=['custom'], body=api.issues[0]['body'] + '\nNew verification evidence.')
        snapshot = deepcopy(api.issues)
        self.run_script(api)
        self.assertEqual(api.issues, snapshot)

    def test_dry_run_makes_no_requests(self):
        api = FakeGitHub()
        with self.assertRaises(SystemExit) as raised:
            self.run_script(api, apply=False)
        self.assertEqual(raised.exception.code, 0)
        self.assertEqual(api.writes, 0)
        self.assertEqual(api.issues, [])

    def test_refuses_cross_repository_writes(self):
        api = FakeGitHub()
        with self.assertRaises(SystemExit):
            self.run_script(api, repo='someone/else')
        self.assertEqual(api.writes, 0)

if __name__ == '__main__':
    unittest.main()
