from __future__ import annotations

import copy
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
import benchmark_ui_reveal as benchmark
import benchmark_ui_reveal_fresh as fresh


def example():
    def event(phase, index, ready, frame):
        return {'phase': phase, 'index': index, 'focusReadyMilliseconds': ready,
                'synchronizedFrameMilliseconds': frame, 'syntheticKeyAccepted': True}
    events = [event('first', 0, 1, 2)] + [event('warmup', i, 1, 2) for i in range(5)] + [
        event('warm', 0, 3, 7), event('warm', 1, 1, 5), event('warm', 2, 2, 6)]
    events[0]['fixtureToFocusMilliseconds'] = 4
    report = {'schemaVersion': 1, 'entries': 1000, 'tiles': 100, 'warmups': 5, 'measuredReveals': 3,
              'releaseNdebug': True, 'steadyClock': True, 'binarySha256': 'a' * 64,
              'qpa': 'offscreen', 'graphicsApi': 'Software', 'scope': 'synthetic',
              'boundary': 'not physical display', 'qtVersion': '6.8.2', 'compiler': 'synthetic',
              'kernelVersion': 'synthetic', 'capturedAtUtc': '2026-10-31T12:00:00Z',
              'firstReveal': copy.deepcopy(events[0]), 'samples': copy.deepcopy(events[6:]),
              'focusReady': {'p50Milliseconds': 2, 'p95Milliseconds': 3, 'maximumMilliseconds': 3},
              'synchronizedFrame': {'p50Milliseconds': 6, 'p95Milliseconds': 7, 'maximumMilliseconds': 7}}
    return report, events


def example_100():
    report, events = example()
    events = events[:6] + [{'phase': 'warm', 'index': i, 'focusReadyMilliseconds': i + 1,
                           'synchronizedFrameMilliseconds': i + 2, 'syntheticKeyAccepted': True}
                          for i in range(100)]
    report.update(measuredReveals=100, samples=copy.deepcopy(events[6:]),
                  focusReady={'p50Milliseconds': 50, 'p95Milliseconds': 95, 'maximumMilliseconds': 100},
                  synchronizedFrame={'p50Milliseconds': 51, 'p95Milliseconds': 96, 'maximumMilliseconds': 101})
    return report, events


def fresh_example(count=100):
    rows = []
    for i in range(count):
        first = {'phase': 'first', 'index': 0, 'focusReadyMilliseconds': i + 1,
                 'synchronizedFrameMilliseconds': i + 2, 'fixtureToFocusMilliseconds': i + 3,
                 'syntheticKeyAccepted': True}
        rows.append({'index': i, 'exitCode': 0, 'processLifetimeMilliseconds': i + 4,
                     'firstReveal': first, 'childHashes': {name: 'd' * 64 for name in
                                                         ('reveal.json', 'events.jsonl', 'process.log')}})
    summary = {'schemaVersion': 1, 'sampleCount': count, 'binarySha256': 'a' * 64,
               'scope': 'synthetic', 'boundary': 'not OS cold', 'qpa': 'offscreen', 'graphicsApi': 'Software',
               'qtVersion': '6.8.2', 'compiler': 'synthetic', 'kernelVersion': 'synthetic',
               'releaseNdebug': True,
               'firstFocus': fresh.statistics([r['firstReveal']['focusReadyMilliseconds'] for r in rows]),
               'fixtureToFocus': fresh.statistics([r['firstReveal']['fixtureToFocusMilliseconds'] for r in rows]),
               'processLifetime': fresh.statistics([r['processLifetimeMilliseconds'] for r in rows])}
    return summary, rows


class RevealBenchmarkTests(unittest.TestCase):
    def test_valid_samples_and_statistics(self):
        report, events = example()
        benchmark.validate_report(report, events, 'a' * 64, 'offscreen', sample_count=3)

    def test_empty_layout_is_explicit_and_cannot_pass_as_standard(self):
        report, events = example()
        report['tiles'] = 0
        benchmark.validate_report(report, events, 'a' * 64, 'offscreen', sample_count=3, tile_count=0)
        with self.assertRaisesRegex(ValueError, 'incorrect tiles'):
            benchmark.validate_report(report, events, 'a' * 64, 'offscreen', sample_count=3)

    def test_first_only_requires_no_warm_statistics(self):
        report, events = example()
        report.update(warmups=0, measuredReveals=0, samples=[], focusReady=None, synchronizedFrame=None)
        benchmark.validate_report(report, events[:1], 'a' * 64, 'offscreen', sample_count=0, warmups=0)
        report['focusReady'] = {'p50Milliseconds': 1}
        with self.assertRaisesRegex(ValueError, 'first-only'):
            benchmark.validate_report(report, events[:1], 'a' * 64, 'offscreen', sample_count=0, warmups=0)

    def test_invalid_identity_backend_events_or_statistics_are_rejected(self):
        for kind in ('schema-bool', 'hash', 'backend', 'missing', 'reordered', 'failed-key', 'negative',
                     'frame-before-ready', 'tampered-event', 'percentile', 'bool-time', 'timestamp'):
            with self.subTest(kind=kind):
                report, events = example()
                if kind == 'schema-bool':
                    report['schemaVersion'] = True
                elif kind == 'hash':
                    report['binarySha256'] = 'b' * 64
                elif kind == 'backend':
                    report['graphicsApi'] = 'Direct3D11'
                elif kind == 'missing':
                    events.pop()
                elif kind == 'reordered':
                    events[6], events[7] = events[7], events[6]
                elif kind == 'failed-key':
                    events[6]['syntheticKeyAccepted'] = False
                elif kind == 'negative':
                    events[6]['focusReadyMilliseconds'] = -1
                elif kind == 'frame-before-ready':
                    events[6]['synchronizedFrameMilliseconds'] = 1
                elif kind == 'tampered-event':
                    events[6]['focusReadyMilliseconds'] = 2.5
                elif kind == 'percentile':
                    report['focusReady']['p95Milliseconds'] = 2
                elif kind == 'bool-time':
                    events[6]['focusReadyMilliseconds'] = True
                else:
                    report['capturedAtUtc'] = '2026-10-31T12:00:00'
                with self.assertRaises(ValueError):
                    benchmark.validate_report(report, events, 'a' * 64, 'offscreen', sample_count=3)

    def test_evidence_checks_identity_bytes_and_raw_event_consistency(self):
        build = Path(__file__).resolve().parents[2] / 'build'
        build.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='reveal-audit-', dir=build) as temporary:
            root = Path(temporary)
            report, events = example_100()
            (root / 'reveal.json').write_text(json.dumps(report), encoding='utf-8')
            (root / 'events.jsonl').write_text(''.join(json.dumps(e) + '\n' for e in events), encoding='utf-8')
            (root / 'process.log').write_text('info: synthetic run completed\n', encoding='utf-8')
            names = ('reveal.json', 'events.jsonl', 'process.log')
            identity = {'GITHUB_SHA': 'c' * 40, 'GITHUB_RUN_ID': '9', 'GITHUB_RUN_ATTEMPT': '1'}
            manifest = {'schemaVersion': 1, 'status': 'passed', 'mode': 'offscreen', 'identity': identity,
                        'binarySha256': 'a' * 64,
                        'files': {name: hashlib.sha256((root / name).read_bytes()).hexdigest() for name in names}}
            path = root / 'manifest.json'
            path.write_text(json.dumps(manifest), encoding='utf-8')
            benchmark.inspect(root, identity)
            with self.assertRaisesRegex(ValueError, 'identity mismatch'):
                benchmark.inspect(root, {**identity, 'GITHUB_RUN_ATTEMPT': '2'})
            events[6]['focusReadyMilliseconds'] = 0.5
            (root / 'events.jsonl').write_text(''.join(json.dumps(e) + '\n' for e in events), encoding='utf-8')
            with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
                benchmark.inspect(root, identity)
            manifest['files']['events.jsonl'] = hashlib.sha256((root / 'events.jsonl').read_bytes()).hexdigest()
            path.write_text(json.dumps(manifest), encoding='utf-8')
            with self.assertRaisesRegex(ValueError, 'does not match'):
                benchmark.inspect(root, identity)

    def test_fresh_process_evidence_checks_samples_and_child_hashes(self):
        build = Path(__file__).resolve().parents[2] / 'build'
        build.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='reveal-fresh-audit-', dir=build) as temporary:
            root = Path(temporary)
            summary, rows = fresh_example(3)
            (root / 'summary.json').write_text(json.dumps(summary), encoding='utf-8')
            (root / 'events.jsonl').write_text(''.join(json.dumps(row) + '\n' for row in rows), encoding='utf-8')
            identity = {'GITHUB_SHA': 'c' * 40, 'GITHUB_RUN_ID': '9', 'GITHUB_RUN_ATTEMPT': '1'}
            manifest = {'schemaVersion': 1, 'status': 'passed', 'mode': 'offscreen', 'identity': identity,
                        'binarySha256': 'a' * 64,
                        'files': {name: hashlib.sha256((root / name).read_bytes()).hexdigest()
                                  for name in ('summary.json', 'events.jsonl')}}
            path = root / 'manifest.json'
            path.write_text(json.dumps(manifest), encoding='utf-8')
            fresh.inspect(root, identity, sample_count=3)
            with self.assertRaisesRegex(ValueError, 'fixture'):
                fresh.inspect(root, identity, sample_count=3, expected_tiles=0)
            summary.update(schemaVersion=2, tiles=0)
            (root / 'summary.json').write_text(json.dumps(summary), encoding='utf-8')
            manifest.update(schemaVersion=2, tiles=0)
            manifest['files']['summary.json'] = hashlib.sha256((root / 'summary.json').read_bytes()).hexdigest()
            path.write_text(json.dumps(manifest), encoding='utf-8')
            fresh.inspect(root, identity, sample_count=3, expected_tiles=0)
            with self.assertRaisesRegex(ValueError, 'fixture'):
                fresh.inspect(root, identity, sample_count=3)
            with self.assertRaisesRegex(ValueError, 'child checksum'):
                fresh.inspect(root, identity, sample_count=3, require_children=True, expected_tiles=0)
            rows[0]['processLifetimeMilliseconds'] = 0
            (root / 'events.jsonl').write_text(''.join(json.dumps(row) + '\n' for row in rows), encoding='utf-8')
            manifest['files']['events.jsonl'] = hashlib.sha256((root / 'events.jsonl').read_bytes()).hexdigest()
            path.write_text(json.dumps(manifest), encoding='utf-8')
            with self.assertRaisesRegex(ValueError, 'timing order'):
                fresh.inspect(root, identity, sample_count=3, expected_tiles=0)


if __name__ == '__main__':
    unittest.main()
