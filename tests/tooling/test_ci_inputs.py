from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
import ci_package_inputs
import ci_timing


class CIInputTests(unittest.TestCase):
    def setUp(self):
        (ROOT / 'build').mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix='ci-input-tests-', dir=ROOT / 'build')
        self.root = Path(self.temporary.name)
        self.addCleanup(self.temporary.cleanup)

    def test_only_fixed_sources_are_planned_and_source_changes_change_key(self):
        destination = self.root / 'packaging/windows'
        destination.mkdir(parents=True)
        for name in ('runtime.json', 'installer-toolchain.json', 'qt-license-sources.json', 'sources.json'):
            shutil.copyfile(ROOT / 'packaging/windows' / name, destination / name)
        rows = ci_package_inputs.inputs(self.root)
        self.assertGreater(len(rows), 10)
        self.assertTrue(all(row['path'].startswith('build/package-tools/') for row in rows))
        changed = [dict(row) for row in rows]
        changed[0]['url'] += '?different-source'
        self.assertNotEqual(ci_package_inputs.key(rows), ci_package_inputs.key(changed))
        runtime = destination / 'runtime.json'
        data = json.loads(runtime.read_text(encoding='utf-8'))
        data['url'] = 'https://example.invalid/untrusted.exe'
        runtime.write_text(json.dumps(data), encoding='utf-8')
        with self.assertRaises(ValueError):
            ci_package_inputs.inputs(self.root)

    def test_restored_bytes_and_unexpected_entries_are_checked(self):
        path = self.root / 'build/package-tools/example.zip'
        path.parent.mkdir(parents=True)
        path.write_bytes(b'expected')
        rows = [{'path': 'build/package-tools/example.zip', 'url': 'https://github.com/example',
                 'sha256': hashlib.sha256(b'expected').hexdigest()}]
        ci_package_inputs.verify(rows, self.root, restored=True)
        path.write_bytes(b'tampered')
        with self.assertRaisesRegex(ValueError, 'hash mismatch'):
            ci_package_inputs.verify(rows, self.root, restored=True)
        path.write_bytes(b'expected')
        (path.parent / 'extra.exe').write_bytes(b'unexpected')
        with self.assertRaisesRegex(ValueError, 'Unexpected'):
            ci_package_inputs.verify(rows, self.root, restored=True)

    def test_timing_records_source_run_attempt_and_actual_exit_code(self):
        env = {'XDSTARTER_CI_EVIDENCE': str(self.root), 'GITHUB_SHA': 'a' * 40,
               'GITHUB_RUN_ID': '17', 'GITHUB_RUN_ATTEMPT': '1',
               'RUNNER_OS': 'Windows', 'RUNNER_ARCH': 'X64'}
        with patch.dict(os.environ, env, clear=True):
            ci_timing.record('ctest', time.monotonic_ns(), 1)
            data = json.loads((self.root / 'timings.json').read_text(encoding='utf-8'))
            self.assertEqual(data['identity']['GITHUB_SHA'], 'a' * 40)
            self.assertEqual(data['commands'][0]['exit_code'], 1)
            os.environ['GITHUB_RUN_ATTEMPT'] = '2'
            with self.assertRaisesRegex(ValueError, 'identity changed'):
                ci_timing.record('ctest', time.monotonic_ns(), 0)


if __name__ == '__main__':
    unittest.main()
