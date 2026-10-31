#!/usr/bin/env python3
"""Collect first reveals in fresh synthetic test processes, without clearing OS caches."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import subprocess
import time

from benchmark_ui_reveal import ROOT, finite_time, validate_report
from benchmark_ui_search import benchmark_environment, file_hash


def statistics(values):
    ordered = sorted(values)
    return {'p50Milliseconds': ordered[(len(ordered) * 50 + 99) // 100 - 1],
            'p95Milliseconds': ordered[(len(ordered) * 95 + 99) // 100 - 1],
            'maximumMilliseconds': ordered[-1]}


def inspect(directory, expected_identity=None, sample_count=100, require_children=False, expected_tiles=100):
    directory = Path(directory)
    names = ('summary.json', 'events.jsonl')
    for name in (*names, 'manifest.json'):
        path = directory / name
        if not path.is_file() or path.is_symlink() or path.stat().st_size > 10 * 1024 * 1024:
            raise ValueError('missing or oversized fresh reveal evidence')
    manifest = json.loads((directory / 'manifest.json').read_text(encoding='utf-8'))
    if (type(expected_tiles) is not int or expected_tiles not in (0, 100) or
            not isinstance(manifest, dict) or type(manifest.get('schemaVersion')) is not int or
            manifest['schemaVersion'] not in (1, 2) or
            manifest.get('status') != 'passed' or manifest.get('mode') not in ('offscreen', 'hosted-native') or
            not isinstance(manifest.get('binarySha256'), str) or
            not re.fullmatch(r'[0-9a-f]{64}', manifest['binarySha256'])):
        raise ValueError('invalid fresh reveal manifest')
    version = manifest['schemaVersion']
    if ((version == 1 and expected_tiles != 100) or
            (version == 2 and (type(manifest.get('tiles')) is not int or manifest['tiles'] != expected_tiles))):
        raise ValueError('incorrect fresh reveal fixture')
    if expected_identity is not None and manifest.get('identity') != expected_identity:
        raise ValueError('fresh reveal identity mismatch')
    hashes = {name: hashlib.sha256((directory / name).read_bytes()).hexdigest() for name in names}
    if manifest.get('files') != hashes:
        raise ValueError('fresh reveal checksum mismatch')
    summary = json.loads((directory / 'summary.json').read_text(encoding='utf-8'))
    events = [json.loads(line) for line in (directory / 'events.jsonl').read_text(encoding='utf-8').splitlines()]
    backend = ('offscreen', 'Software') if manifest['mode'] == 'offscreen' else ('windows', 'Direct3D11')
    if (sample_count < 1 or not isinstance(summary, dict) or type(summary.get('schemaVersion')) is not int or
            summary['schemaVersion'] != version or
            (version == 2 and (type(summary.get('tiles')) is not int or summary['tiles'] != expected_tiles)) or
            type(summary.get('sampleCount')) is not int or summary['sampleCount'] != sample_count or
            summary.get('binarySha256') != manifest['binarySha256'] or len(events) != sample_count or
            (summary.get('qpa'), summary.get('graphicsApi')) != backend or
            summary.get('releaseNdebug') is not True or
            any(not isinstance(summary.get(key), str) or not summary[key] for key in
                ('scope', 'boundary', 'qtVersion', 'compiler', 'kernelVersion'))):
        raise ValueError('incomplete fresh reveal samples')
    values = {'firstFocus': [], 'fixtureToFocus': [], 'processLifetime': []}
    for index, event in enumerate(events):
        if (not isinstance(event, dict) or type(event.get('index')) is not int or event['index'] != index or
                type(event.get('exitCode')) is not int or event['exitCode'] != 0):
            raise ValueError('invalid fresh process identity or exit')
        first = event.get('firstReveal')
        if (not isinstance(first, dict) or first.get('phase') != 'first' or
                type(first.get('index')) is not int or first['index'] != 0 or
                first.get('syntheticKeyAccepted') is not True):
            raise ValueError('invalid first reveal event')
        ready, frame = first.get('focusReadyMilliseconds'), first.get('synchronizedFrameMilliseconds')
        fixture, lifetime = first.get('fixtureToFocusMilliseconds'), event.get('processLifetimeMilliseconds')
        if (not all(finite_time(v) for v in (ready, frame, fixture, lifetime)) or
                frame < ready or fixture < ready or lifetime < fixture):
            raise ValueError('invalid fresh reveal timing order')
        digests = event.get('childHashes')
        if (not isinstance(digests, dict) or set(digests) != {'reveal.json', 'events.jsonl', 'process.log'} or
                not all(isinstance(v, str) and re.fullmatch(r'[0-9a-f]{64}', v) for v in digests.values())):
            raise ValueError('missing fresh child checksums')
        if require_children:
            child = directory / 'children' / f'{index:03d}'
            for name, digest in digests.items():
                path = child / name
                if (not path.is_file() or path.is_symlink() or path.stat().st_size > 10 * 1024 * 1024 or
                        file_hash(path) != digest):
                    raise ValueError('fresh child checksum mismatch')
            report = json.loads((child / 'reveal.json').read_text(encoding='utf-8'))
            raw = [json.loads(line) for line in (child / 'events.jsonl').read_text(encoding='utf-8').splitlines()]
            validate_report(report, raw, manifest['binarySha256'], manifest['mode'], sample_count=0, warmups=0,
                            tile_count=expected_tiles)
            if raw != [first]:
                raise ValueError('fresh child event differs from aggregate')
            log = (child / 'process.log').read_text(encoding='utf-8', errors='replace')
            if re.search(r'Q(?:CRITICAL|FATAL)\s*:|^(?:critical|fatal):', log, re.IGNORECASE | re.MULTILINE):
                raise ValueError('fresh child emitted critical or fatal output')
        values['firstFocus'].append(ready)
        values['fixtureToFocus'].append(fixture)
        values['processLifetime'].append(lifetime)
    for name, times in values.items():
        actual = summary.get(name)
        expected = statistics(times)
        if (not isinstance(actual, dict) or set(actual) != set(expected) or
                any(not finite_time(actual.get(key)) or not math.isclose(actual[key], value, abs_tol=1e-6)
                    for key, value in expected.items())):
            raise ValueError('incorrect fresh reveal statistics')
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--hosted-native', action='store_true')
    parser.add_argument('--empty-layout', action='store_true', help='use one empty group and 1000 catalog entries')
    parser.add_argument('--timeout-seconds', type=int, default=30)
    args = parser.parse_args()
    try:
        binary = args.executable.resolve(strict=True)
        output = args.output_dir.resolve()
        build = (ROOT / 'build').resolve()
        if (binary.name.lower() not in ('xdstarter_ui_tests', 'xdstarter_ui_tests.exe') or
                not binary.is_relative_to(build) or not output.is_relative_to(build) or output.exists()):
            raise ValueError('use the test executable and a new output directory under build')
        if not 1 <= args.timeout_seconds <= 120:
            raise ValueError('per-process timeout must be between 1 and 120 seconds')
        mode = 'hosted-native' if args.hosted_native else 'offscreen'
        env = benchmark_environment(os.environ, mode, os.name)
        env.pop('XDSTARTER_UI_REVEAL_EMPTY_LAYOUT', None)
        if args.empty_layout:
            env['XDSTARTER_UI_REVEAL_EMPTY_LAYOUT'] = '1'
        expected_tiles = 0 if args.empty_layout else 100
        env.update(XDSTARTER_UI_REVEAL_FIRST_ONLY='1', QT_FORCE_STDERR_LOGGING='1',
                   QT_MESSAGE_PATTERN='%{type}: %{message}')
        output.mkdir(parents=True, exist_ok=False)
        children = output / 'children'
        children.mkdir()
        digest = file_hash(binary)
        identity = {key: env.get(key, '') for key in ('GITHUB_SHA', 'GITHUB_RUN_ID', 'GITHUB_RUN_ATTEMPT')}
        samples = []
        environment_details = None
        with (output / 'events.jsonl').open('x', encoding='utf-8', newline='\n') as stream:
            for index in range(100):
                child = children / f'{index:03d}'
                child.mkdir()
                report_path, events_path, log_path = (child / name for name in
                                                       ('reveal.json', 'events.jsonl', 'process.log'))
                child_env = {**env, 'XDSTARTER_UI_REVEAL_OUTPUT': str(report_path),
                             'XDSTARTER_UI_REVEAL_EVENTS': str(events_path)}
                started = time.perf_counter_ns()
                with log_path.open('wb') as log:
                    process = subprocess.run([str(binary), '--ui-reveal-benchmark'], env=child_env,
                                             stdout=log, stderr=subprocess.STDOUT, timeout=args.timeout_seconds)
                lifetime = (time.perf_counter_ns() - started) / 1_000_000
                if process.returncode != 0:
                    raise ValueError(f'fresh child {index} exited {process.returncode}')
                text = log_path.read_text(encoding='utf-8', errors='replace')
                if re.search(r'Q(?:CRITICAL|FATAL)\s*:|^(?:critical|fatal):', text, re.IGNORECASE | re.MULTILINE):
                    raise ValueError(f'fresh child {index} emitted critical or fatal output')
                report = json.loads(report_path.read_text(encoding='utf-8'))
                raw = [json.loads(line) for line in events_path.read_text(encoding='utf-8').splitlines()]
                validate_report(report, raw, digest, mode, sample_count=0, warmups=0, tile_count=expected_tiles)
                current_details = {key: report[key] for key in
                                   ('qpa', 'graphicsApi', 'qtVersion', 'compiler', 'kernelVersion', 'releaseNdebug', 'tiles')}
                if environment_details is not None and environment_details != current_details:
                    raise ValueError('fresh child environment changed during sampling')
                environment_details = current_details
                row = {'index': index, 'exitCode': process.returncode, 'processLifetimeMilliseconds': lifetime,
                       'firstReveal': raw[0], 'childHashes': {p.name: file_hash(p)
                                                             for p in (report_path, events_path, log_path)}}
                stream.write(json.dumps(row, ensure_ascii=False) + '\n')
                stream.flush()
                samples.append(row)
        if file_hash(binary) != digest:
            raise ValueError('benchmark executable changed during fresh-process sampling')
        summary = {'schemaVersion': 2, 'sampleCount': len(samples), 'binarySha256': digest, **environment_details,
                   'scope': 'fresh synthetic test processes with a real host and simulated platform',
                   'boundary': 'first reveal after QGuiApplication exists; fixture time includes QML and catalog setup; process lifetime includes teardown; OS caches are not cleared and no physical input or display is measured',
                   'firstFocus': statistics([row['firstReveal']['focusReadyMilliseconds'] for row in samples]),
                   'fixtureToFocus': statistics([row['firstReveal']['fixtureToFocusMilliseconds'] for row in samples]),
                   'processLifetime': statistics([row['processLifetimeMilliseconds'] for row in samples])}
        summary_path = output / 'summary.json'
        summary_path.write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
        manifest = {'schemaVersion': 2, 'status': 'passed', 'mode': mode, 'identity': identity, 'tiles': expected_tiles,
                    'binarySha256': digest, 'files': {name: file_hash(output / name)
                                                     for name in ('summary.json', 'events.jsonl')}}
        (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
        inspect(output, identity, require_children=True, expected_tiles=expected_tiles)
        print(f'Validated 100 fresh synthetic host processes: {output}')
    except (OSError, ValueError, subprocess.SubprocessError, json.JSONDecodeError) as error:
        parser.exit(1, f'Fresh reveal benchmark failed: {error}\n')


if __name__ == '__main__':
    main()
