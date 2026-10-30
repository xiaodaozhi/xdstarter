#!/usr/bin/env python3
"""Check bounded resident observations without treating them as acceptance."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re

from benchmark_resident import summarize


def inspect(directory, expected_sha=None):
    result = json.loads((directory / 'resident-sample.json').read_text(encoding='utf-8'))
    if (result.get('schemaVersion') != 1 or result.get('productExit') != 0 or
            type(result.get('processId')) is not int or result['processId'] <= 0 or
            not re.fullmatch(r'[0-9a-f]{64}', result.get('executableSha256', ''))):
        raise ValueError('invalid product identity or exit')
    ci = result.get('ci', {})
    if (not re.fullmatch(r'[0-9a-f]{40}', ci.get('GITHUB_SHA', '')) or
            not re.fullmatch(r'[1-9][0-9]*', ci.get('GITHUB_RUN_ID', '')) or
            not re.fullmatch(r'[1-9][0-9]*', ci.get('GITHUB_RUN_ATTEMPT', '')) or
            ci.get('RUNNER_OS') != 'Windows' or ci.get('RUNNER_ARCH') != 'X64' or
            (expected_sha is not None and ci['GITHUB_SHA'] != expected_sha)):
        raise ValueError('invalid CI identity')
    info, diagnostic = result.get('buildInfo', {}), result.get('diagnostic', {})
    if (not info.get('qt') or info.get('revision') != ci['GITHUB_SHA'] or
            info.get('dirty') is not False or info.get('distribution') is not True or
            diagnostic.get('intervalMs') != 250 or
            not diagnostic.get('runId') or diagnostic.get('finalHookInstalled') is not False or
            diagnostic.get('finalSessionActive') is not False or
            type(diagnostic.get('writeFailures')) is not int or diagnostic['writeFailures'] < 0 or
            any(type(diagnostic.get(key)) is not bool for key in
                ('initialHookInstalled', 'initialSessionActive', 'observedHookInstalled', 'observedSessionActive'))):
        raise ValueError('incomplete build or capture diagnostics')
    summary = summarize(result.get('samples'))
    if result.get('summary') != summary or len(result.get('limitations', [])) < 3:
        raise ValueError('summary or observation limitations differ')
    if not (directory / 'stdout.log').is_file() or not (directory / 'stderr.log').is_file():
        raise ValueError('missing product logs')
    stderr = (directory / 'stderr.log').read_text(encoding='utf-8', errors='replace')
    if re.search(r'(?mi)^\s*(?:critical|fatal)\s*:', stderr):
        raise ValueError('severe Qt message during resident observation')
    snapshot = json.loads((directory / 'snapshot.json').read_text(encoding='utf-8'))
    if (snapshot.get('processId') != result['processId'] or
            snapshot.get('executableSha256') != result['executableSha256'] or
            snapshot.get('runId') != diagnostic['runId'] or snapshot.get('hookInstalled') is not False or
            snapshot.get('sessionActive') is not False or
            snapshot.get('diagnosticWriteFailures') != diagnostic['writeFailures']):
        raise ValueError('final capture snapshot does not match the sample')
    return {'processId': result['processId'], 'executableSha256': result['executableSha256'],
            'qt': info['qt'], 'samples': len(result['samples']),
            'allSamplesWithoutVisibleTopLevelWindows': summary['allSamplesWithoutVisibleTopLevelWindows'],
            'maxObserverNs': summary['maxObserverNs'],
            'maxCpuPercentOneCore': max(row['cpuPercentOneCore'] for row in summary['intervals']),
            'maxWorkingSetBytes': max(row['workingSetBytes'] for row in result['samples']),
            'maxPrivateBytes': max(row['privateBytes'] for row in result['samples']),
            'diagnosticWriteFailures': diagnostic['writeFailures']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--expected-sha')
    args = parser.parse_args()
    try:
        print(json.dumps(inspect(args.directory, args.expected_sha), sort_keys=True))
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f'Resident sample incomplete: {error}')
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
