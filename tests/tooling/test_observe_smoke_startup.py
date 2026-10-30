"""Watchdog evidence for independently owned, controlled child processes."""
import hashlib
import json
import os
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from observe_smoke_startup import MAX_OUTPUT, observe


class SmokeObserverTests(unittest.TestCase):
    def test_saved_local_log_matches_its_raw_digest(self):
        evidence = ROOT / 'docs/technical/validation/issue-86'
        record = json.loads((evidence / 'smoke-startup-local.json').read_text(encoding='utf-8'))
        raw = (evidence / 'smoke-startup-local.log').read_bytes()
        self.assertEqual(hashlib.sha256(raw).hexdigest(), record['outputSha256'])

    def run_child(self, source, timeout=2):
        return observe([sys.executable, '-u', '-c', source], timeout=timeout,
                       cwd=ROOT, env=dict(os.environ))

    def test_success_records_real_stage_and_creation_boundaries(self):
        result, raw = self.run_child("print('Smoke stage before-application 0 ms'); "
                                     "print('Smoke stage event-loop-returned 7 ms')")
        self.assertEqual(result['status'], 'passed')
        self.assertEqual(result['exitCode'], 0)
        self.assertTrue(result['readerFinished'])
        self.assertLessEqual(result['createReturnMilliseconds'], result['firstStageMilliseconds'])
        self.assertLessEqual(result['firstByteMilliseconds'], result['firstStageMilliseconds'])
        self.assertEqual([row['name'] for row in result['stages']], ['before-application', 'event-loop-returned'])
        self.assertIn(b'event-loop-returned', raw)

    def test_stall_before_first_log_is_bounded_and_preserved(self):
        result, raw = self.run_child('import time; time.sleep(30)', timeout=0.6)
        self.assertEqual(result['status'], 'timeout-before-first-stage')
        self.assertIsNone(result['firstStageMilliseconds'])
        self.assertIsNone(result['firstByteMilliseconds'])
        self.assertNotEqual(result['exitCode'], 0)
        self.assertLess(result['finishedMilliseconds'], 5000)
        self.assertEqual(raw, b'')

    def test_stall_after_first_log_retains_partial_output(self):
        result, raw = self.run_child("import time; print('Smoke stage before-application 0 ms'); time.sleep(30)",
                                     timeout=1.5)
        self.assertEqual(result['status'], 'timeout-after-first-stage')
        self.assertIsNotNone(result['firstStageMilliseconds'])
        self.assertIn(b'Smoke stage before-application', raw)
        self.assertLess(result['finishedMilliseconds'], 5000)

    def test_failure_and_excessive_output_cannot_pass(self):
        result, _ = self.run_child('import sys; sys.exit(9)')
        self.assertEqual(result['status'], 'child-failed')
        result, raw = self.run_child(f'import sys; sys.stdout.buffer.write(b"x" * {MAX_OUTPUT + 65536}); sys.stdout.flush()')
        self.assertEqual(result['status'], 'output-limit')
        self.assertEqual(len(raw), MAX_OUTPUT)
        self.assertTrue(result['outputTruncated'])


if __name__ == '__main__':
    unittest.main()
