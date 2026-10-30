#!/usr/bin/env python3
"""Fail closed on incomplete or severe native-picker QtTest evidence."""
from __future__ import annotations

import argparse
from pathlib import Path
import re


FUNCTIONS = {
    'rapid.log': ('layoutImportFilePickerOwnership',),
    'immediate.log': ('layoutImportFilePickerOwnership',),
    'shown.log': ('layoutImportFilePickerOwnership',),
    'selection.log': ('layoutImportNativeSelection', 'layoutImportNativeLaunchFailure'),
    'shutdown.log': ('layoutImportNativeShutdown',),
}


def verify(directory: Path) -> None:
    for name, functions in FUNCTIONS.items():
        path = directory / name
        if not path.is_file():
            raise ValueError(f'missing native picker evidence: {name}')
        text = path.read_text(encoding='utf-8', errors='replace')
        if re.search(r'Q(?:CRITICAL|FATAL)\s*:', text, re.IGNORECASE):
            raise ValueError(f'severe Qt message: {name}')
        passes = re.findall(r'(?m)^PASS\s*:\s*PrototypeWindowTest::([^\r\n]+)', text)
        expected = ['initTestCase()', *(function + '()' for function in functions), 'cleanupTestCase()']
        if passes != expected:
            raise ValueError(f'incomplete native picker functions: {name}')
        totals = re.findall(r'Totals:\s*(\d+) passed,\s*(\d+) failed,\s*(\d+) skipped,\s*(\d+) blacklisted', text)
        if totals != [(str(len(expected)), '0', '0', '0')]:
            raise ValueError(f'incomplete native picker totals: {name}')
        markers: tuple[str, ...]
        if name in {'rapid.log', 'immediate.log', 'shown.log'}:
            mode = name.removesuffix('.log')
            markers = tuple(f'Native picker {stage} close: "{mode}" dark= {dark}'
                            for stage in ('before', 'after') for dark in ('true', 'false'))
            if mode == 'rapid':
                markers += ('Native picker rapid reopen:', 'oldExited= true', 'nextVisible= true')
            if mode == 'immediate':
                markers += ('Native picker after-close observation:', 'processExited= true')
            if mode == 'shown':
                markers += ('Native picker foreground verified:', 'foreground= true',
                            'Native picker owner focus restored:', 'ownerForeground= true')
        elif name == 'selection.log':
            markers = ('Native picker selection driver:', 'finished= true',
                       'Native picker selected file and opened import preview:',
                       'Native picker external focus preserved:', 'otherForeground= true',
                       'Native picker launch failure visibly reported and retry canceled:')
        else:
            markers = ('Native picker shutdown:', 'fakeWinCaptureStopped= true',
                       'fakeStartCaptureStopped= true', 'childExited= true')
        if any(marker not in text for marker in markers):
            raise ValueError(f'missing native picker observations: {name}')


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    try:
        verify(args.directory)
    except (OSError, ValueError) as error:
        print(f'Native picker evidence failed: {error}')
        return 1
    print('Native picker evidence complete; no QCRITICAL or QFATAL messages.')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
