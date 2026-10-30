from __future__ import annotations

import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
import benchmark_resident as benchmark
import inspect_resident_sample as inspector


class ResidentSampleTests(unittest.TestCase):
    def setUp(self):
        (ROOT / 'build').mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix='resident-sample-', dir=ROOT / 'build')
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.samples = [{'atNs': i * 1_000_000_000, 'cpu100ns': i * 10_000_000,
                         'workingSetBytes': 100 + i, 'privateBytes': 200 + i,
                         'visibleTopLevelWindows': 0, 'observerNs': 5000}
                        for i in range(benchmark.SAMPLE_COUNT)]
        self.result = {'schemaVersion': 1, 'processId': 42, 'executableSha256': 'a' * 64,
                       'productExit': 0, 'buildInfo': {'qt': '6.8.2', 'revision': 'b' * 40,
                                                       'dirty': False, 'distribution': True},
                       'ci': {'GITHUB_SHA': 'b' * 40, 'GITHUB_RUN_ID': '12', 'GITHUB_RUN_ATTEMPT': '1',
                              'RUNNER_OS': 'Windows', 'RUNNER_ARCH': 'X64'},
                       'diagnostic': {'intervalMs': 250, 'runId': 'run-1', 'writeFailures': 0,
                                      'initialHookInstalled': True, 'initialSessionActive': True,
                                      'observedHookInstalled': True, 'observedSessionActive': True,
                                      'finalHookInstalled': False, 'finalSessionActive': False},
                       'samples': self.samples, 'summary': benchmark.summarize(self.samples),
                       'limitations': ['diagnostics add overhead', 'sampled visibility', 'hosted environment']}
        (self.directory / 'stdout.log').write_text('', encoding='utf-8')
        (self.directory / 'stderr.log').write_text('', encoding='utf-8')
        (self.directory / 'snapshot.json').write_text(json.dumps({
            'processId': 42, 'executableSha256': 'a' * 64, 'runId': 'run-1',
            'hookInstalled': False, 'sessionActive': False, 'diagnosticWriteFailures': 0}), encoding='utf-8')
        self.write_result()

    def write_result(self):
        (self.directory / 'resident-sample.json').write_text(json.dumps(self.result), encoding='utf-8')

    def test_one_core_cpu_and_visibility_are_reported_from_samples(self):
        report = inspector.inspect(self.directory, 'b' * 40)
        self.assertEqual(report['maxCpuPercentOneCore'], 100)
        self.assertTrue(report['allSamplesWithoutVisibleTopLevelWindows'])
        self.samples[5]['visibleTopLevelWindows'] = 1
        self.result['summary'] = benchmark.summarize(self.samples)
        self.write_result()
        self.assertFalse(inspector.inspect(self.directory)['allSamplesWithoutVisibleTopLevelWindows'])

    def test_missing_or_reversed_counters_are_rejected(self):
        with self.assertRaisesRegex(ValueError, 'missing resident samples'):
            benchmark.summarize(self.samples[:-1])
        self.samples[5]['cpu100ns'] = 0
        with self.assertRaisesRegex(ValueError, 'nonmonotonic'):
            benchmark.summarize(self.samples)

    def test_tampered_summary_or_wrong_run_is_rejected(self):
        self.result['summary']['maxObserverNs'] = 0
        self.write_result()
        with self.assertRaisesRegex(ValueError, 'summary'):
            inspector.inspect(self.directory)
        self.result['summary'] = benchmark.summarize(self.samples)
        self.write_result()
        with self.assertRaisesRegex(ValueError, 'CI identity'):
            inspector.inspect(self.directory, 'c' * 40)

    def test_only_hosted_windows_flags_allow_process_launch(self):
        env = {'GITHUB_ACTIONS': 'true', 'RUNNER_ENVIRONMENT': 'github-hosted', 'RUNNER_OS': 'Windows'}
        benchmark.require_hosted(env, 'nt')
        for changed, system in (({**env, 'RUNNER_ENVIRONMENT': 'self-hosted'}, 'nt'), (env, 'posix'), ({}, 'nt')):
            with self.subTest(changed=changed, system=system), self.assertRaises(ValueError):
                benchmark.require_hosted(changed, system)

    def test_severe_message_and_mismatched_cleanup_cannot_be_hidden(self):
        stderr = self.directory / 'stderr.log'
        stderr.write_text('critical: capture failed\n', encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'severe Qt message'):
            inspector.inspect(self.directory)
        stderr.write_text('', encoding='utf-8')
        snapshot = self.directory / 'snapshot.json'
        data = json.loads(snapshot.read_text(encoding='utf-8'))
        data['hookInstalled'] = True
        snapshot.write_text(json.dumps(data), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'final capture snapshot'):
            inspector.inspect(self.directory)


if __name__ == '__main__':
    unittest.main()
