#!/usr/bin/env python3
"""Measure a synthetic host reveal without registering system input hooks."""
from __future__ import annotations

import argparse
from datetime import datetime
import hashlib
import json
import math
import os
from pathlib import Path
import re
import subprocess

from benchmark_ui_search import benchmark_environment, file_hash

ROOT = Path(__file__).resolve().parents[1]
WARMUPS = 5


def finite_time(value):
    return not isinstance(value, bool) and isinstance(value, (int, float)) and math.isfinite(value) and value >= 0


def validate_report(report, events, binary_hash, mode, sample_count=100, warmups=WARMUPS, tile_count=100):
    if (not isinstance(report, dict) or type(report.get('schemaVersion')) is not int or
            report['schemaVersion'] != 1):
        raise ValueError('invalid reveal schema')
    if type(tile_count) is not int or tile_count not in (0, 100):
        raise ValueError('invalid reveal fixture')
    expected = {'entries': 1000, 'tiles': tile_count, 'warmups': warmups, 'measuredReveals': sample_count,
                'releaseNdebug': True, 'steadyClock': True, 'binarySha256': binary_hash}
    for key, value in expected.items():
        if type(report.get(key)) is not type(value) or report.get(key) != value:
            raise ValueError(f'incorrect {key}')
    expected_backend = ('offscreen', 'Software') if mode == 'offscreen' else ('windows', 'Direct3D11')
    if (report.get('qpa'), report.get('graphicsApi')) != expected_backend:
        raise ValueError('incorrect reveal backend')
    for key in ('scope', 'boundary', 'qtVersion', 'compiler', 'kernelVersion'):
        if not isinstance(report.get(key), str) or not report[key]:
            raise ValueError(f'missing {key}')
    try:
        timestamp = datetime.fromisoformat(report['capturedAtUtc'].replace('Z', '+00:00'))
    except (KeyError, AttributeError, ValueError, TypeError) as error:
        raise ValueError('invalid capture timestamp') from error
    if timestamp.tzinfo is None:
        raise ValueError('capture timestamp lacks timezone')
    if not isinstance(events, list) or len(events) != 1 + warmups + sample_count:
        raise ValueError('incomplete reveal event stream')
    expected_ids = [('first', 0)] + [('warmup', i) for i in range(warmups)] + [
        ('warm', i) for i in range(sample_count)]
    for event, (phase, index) in zip(events, expected_ids):
        if (not isinstance(event, dict) or event.get('phase') != phase or
                type(event.get('index')) is not int or event['index'] != index or
                event.get('syntheticKeyAccepted') is not True):
            raise ValueError('invalid or failed reveal event')
        ready, frame = event.get('focusReadyMilliseconds'), event.get('synchronizedFrameMilliseconds')
        if not finite_time(ready) or not finite_time(frame) or frame < ready:
            raise ValueError('invalid reveal timing order')
    first_fixture = events[0].get('fixtureToFocusMilliseconds')
    if not finite_time(first_fixture) or first_fixture < events[0]['focusReadyMilliseconds']:
        raise ValueError('invalid first fixture initialization time')
    if report.get('firstReveal') != events[0] or report.get('samples') != events[1 + warmups:]:
        raise ValueError('report does not match raw reveal events')
    if sample_count == 0:
        if report.get('focusReady') is not None or report.get('synchronizedFrame') is not None:
            raise ValueError('first-only result must not contain warm statistics')
        return
    for name, field in (('focusReady', 'focusReadyMilliseconds'),
                        ('synchronizedFrame', 'synchronizedFrameMilliseconds')):
        values = sorted(event[field] for event in events[1 + warmups:])
        summary = report.get(name)
        if not isinstance(summary, dict):
            raise ValueError('missing reveal statistics')
        for percent, key in ((50, 'p50Milliseconds'), (95, 'p95Milliseconds')):
            value = values[max(1, (len(values) * percent + 99) // 100) - 1]
            if not finite_time(summary.get(key)) or not math.isclose(summary[key], value, abs_tol=1e-6):
                raise ValueError('incorrect reveal percentile')
        if not finite_time(summary.get('maximumMilliseconds')) or not math.isclose(
                summary['maximumMilliseconds'], values[-1], abs_tol=1e-6):
            raise ValueError('incorrect reveal maximum')


def inspect(directory, expected_identity=None, expected_tiles=100):
    directory = Path(directory)
    names = ('reveal.json', 'events.jsonl', 'process.log')
    for name in (*names, 'manifest.json'):
        path = directory / name
        if not path.is_file() or path.is_symlink() or path.stat().st_size > 10 * 1024 * 1024:
            raise ValueError('missing or oversized reveal evidence')
    manifest = json.loads((directory / 'manifest.json').read_text(encoding='utf-8'))
    if (not isinstance(manifest, dict) or type(manifest.get('schemaVersion')) is not int or
            manifest['schemaVersion'] != 1 or
            manifest.get('status') != 'passed' or manifest.get('mode') not in ('offscreen', 'hosted-native') or
            not isinstance(manifest.get('binarySha256'), str) or
            not re.fullmatch(r'[0-9a-f]{64}', manifest['binarySha256'])):
        raise ValueError('invalid reveal manifest')
    if expected_identity is not None and manifest.get('identity') != expected_identity:
        raise ValueError('reveal identity mismatch')
    hashes = {name: hashlib.sha256((directory / name).read_bytes()).hexdigest() for name in names}
    if manifest.get('files') != hashes:
        raise ValueError('reveal evidence checksum mismatch')
    log = (directory / 'process.log').read_text(encoding='utf-8', errors='replace')
    if re.search(r'Q(?:CRITICAL|FATAL)\s*:|^(?:critical|fatal):', log, re.IGNORECASE | re.MULTILINE):
        raise ValueError('benchmark emitted a critical or fatal message')
    report = json.loads((directory / 'reveal.json').read_text(encoding='utf-8'))
    events = [json.loads(line) for line in (directory / 'events.jsonl').read_text(encoding='utf-8').splitlines()]
    validate_report(report, events, manifest['binarySha256'], manifest['mode'], tile_count=expected_tiles)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--hosted-native', action='store_true')
    parser.add_argument('--empty-layout', action='store_true', help='use one empty group and 1000 catalog entries')
    parser.add_argument('--timeout-seconds', type=int, default=240)
    args = parser.parse_args()
    try:
        binary = args.executable.resolve(strict=True)
        output = args.output_dir.resolve()
        build = (ROOT / 'build').resolve()
        if (binary.name.lower() not in ('xdstarter_ui_tests', 'xdstarter_ui_tests.exe') or
                not binary.is_relative_to(build) or not output.is_relative_to(build) or output.exists()):
            raise ValueError('use the test executable and a new output directory under build')
        if not 1 <= args.timeout_seconds <= 900:
            raise ValueError('timeout must be between 1 and 900 seconds')
        mode = 'hosted-native' if args.hosted_native else 'offscreen'
        env = benchmark_environment(os.environ, mode, os.name)
        env.pop('XDSTARTER_UI_REVEAL_FIRST_ONLY', None)
        env.pop('XDSTARTER_UI_REVEAL_EMPTY_LAYOUT', None)
        if args.empty_layout:
            env['XDSTARTER_UI_REVEAL_EMPTY_LAYOUT'] = '1'
        expected_tiles = 0 if args.empty_layout else 100
        output.mkdir(parents=True, exist_ok=False)
        report_path, events_path = output / 'reveal.json', output / 'events.jsonl'
        env.update(XDSTARTER_UI_REVEAL_OUTPUT=str(report_path), XDSTARTER_UI_REVEAL_EVENTS=str(events_path),
                   XDSTARTER_UI_REVEAL_SAMPLES='100', QT_FORCE_STDERR_LOGGING='1',
                   QT_MESSAGE_PATTERN='%{type}: %{message}')
        digest = file_hash(binary)
        with (output / 'process.log').open('wb') as log:
            process = subprocess.run([str(binary), '--ui-reveal-benchmark'], env=env,
                                     stdout=log, stderr=subprocess.STDOUT, timeout=args.timeout_seconds)
        if process.returncode != 0:
            raise ValueError(f'benchmark exited {process.returncode}')
        log = (output / 'process.log').read_text(encoding='utf-8', errors='replace')
        if re.search(r'Q(?:CRITICAL|FATAL)\s*:|^(?:critical|fatal):', log, re.IGNORECASE | re.MULTILINE):
            raise ValueError('benchmark emitted a critical or fatal message')
        report = json.loads(report_path.read_text(encoding='utf-8'))
        events = [json.loads(line) for line in events_path.read_text(encoding='utf-8').splitlines()]
        if file_hash(binary) != digest:
            raise ValueError('benchmark executable changed during sampling')
        validate_report(report, events, digest, mode, tile_count=expected_tiles)
        identity = {key: env.get(key, '') for key in ('GITHUB_SHA', 'GITHUB_RUN_ID', 'GITHUB_RUN_ATTEMPT')}
        manifest = {'schemaVersion': 1, 'status': 'passed', 'mode': mode, 'identity': identity,
                    'binarySha256': digest,
                    'files': {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                              for p in (report_path, events_path, output / 'process.log')}}
        (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
        inspect(output, identity, expected_tiles=expected_tiles)
        print(f'Validated first reveal and 100 warm synthetic host reveals: {output}')
    except (OSError, ValueError, subprocess.SubprocessError, json.JSONDecodeError) as error:
        parser.exit(1, f'Reveal benchmark failed: {error}\n')


if __name__ == '__main__':
    main()
