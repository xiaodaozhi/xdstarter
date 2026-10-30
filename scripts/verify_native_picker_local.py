#!/usr/bin/env python3
"""Run bounded native picker tests in explicitly opted-in local Windows processes."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys

from verify_native_picker_evidence import verify
from windows_package import generated


def digest(path: Path) -> str:
    with path.open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run', action='store_true', help='explicitly permit brief local fixture windows')
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    if not args.run or os.name != 'nt':
        parser.error('local Windows execution requires --run')
    if os.environ.get('GITHUB_ACTIONS') == 'true' and os.environ.get('RUNNER_ENVIRONMENT') == 'github-hosted':
        parser.error('use the hosted fixture in GitHub Actions')
    exe = args.executable.resolve()
    application = exe.parent / 'xdstarter.exe'
    driver = exe.parent / 'xdstarter_native_picker_driver.exe'
    if exe.name.casefold() != 'xdstarter_ui_tests.exe' or not all(p.is_file() for p in (exe, application, driver)):
        parser.error('test executable, application helper and picker driver must share the build bin directory')
    try:
        output = generated(args.output_dir)
    except ValueError as error:
        parser.error(str(error))
    if output.exists():
        parser.error('output must be a new dedicated directory under build/')
    output.mkdir(parents=True)
    temporary = output / 'tmp'
    temporary.mkdir()
    hashes = {path.name: digest(path) for path in (exe, application, driver)}
    base = dict(os.environ)
    for key in ('XDSTARTER_NATIVE_PICKER_DIAGNOSTIC', 'XDSTARTER_NATIVE_PICKER_SELECTION',
                'XDSTARTER_NATIVE_PICKER_SHUTDOWN', 'XDSTARTER_NATIVE_PICKER_LOCAL'):
        base.pop(key, None)
    base.update(XDSTARTER_NATIVE_PICKER_LOCAL='1', QT_QPA_PLATFORM='windows', QSG_RHI_BACKEND='d3d11',
                QT_FORCE_STDERR_LOGGING='1', QSG_INFO='1', TEMP=str(temporary), TMP=str(temporary))
    base.pop('QT_QUICK_BACKEND', None)
    cases = [('rapid', ['layoutImportFilePickerOwnership'], {'XDSTARTER_NATIVE_PICKER_DIAGNOSTIC': 'rapid'}),
             ('immediate', ['layoutImportFilePickerOwnership'], {'XDSTARTER_NATIVE_PICKER_DIAGNOSTIC': 'immediate'}),
             ('shown', ['layoutImportFilePickerOwnership'], {'XDSTARTER_NATIVE_PICKER_DIAGNOSTIC': 'shown'}),
             ('selection', ['layoutImportNativeSelection', 'layoutImportNativeLaunchFailure'],
              {'XDSTARTER_NATIVE_PICKER_SELECTION': '1'}),
             ('shutdown', ['layoutImportNativeShutdown'], {'XDSTARTER_NATIVE_PICKER_SHUTDOWN': '1'})]
    outcomes = []
    result = {'status': 'running', 'scope': 'local synthetic owned windows and child processes',
              'windows': platform.platform(), 'binarySha256': hashes, 'cases': outcomes,
              'limits': ['Automated UIA is not physical keyboard, screen-reader or full desktop acceptance.',
                         'Finite polling cannot prove that no transient window appeared.']}
    def write_result():
        (output / 'result.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    errors = []
    for name, functions, settings in cases:
        try:
            log = output / (name + '.log')
            stdio = output / (name + '-stdio.log')
            with stdio.open('wb') as stream:
                process = subprocess.Popen([str(exe), *functions, '-o', str(log) + ',txt'],
                                           stdout=stream, stderr=subprocess.STDOUT,
                                           cwd=exe.parent, env={**base, **settings})
                try:
                    code = process.wait(timeout=75)
                except subprocess.TimeoutExpired:
                    # The PID is held by this Popen and is still running; target
                    # only this fixture and its descendants, never other windows.
                    cleanup = 'unavailable'
                    try:
                        taskkill = Path(os.environ.get('SystemRoot', r'C:\Windows')) / 'System32/taskkill.exe'
                        killed = subprocess.run([str(taskkill), '/PID', str(process.pid), '/T', '/F'],
                                                capture_output=True, timeout=15, check=False)
                        cleanup = str(killed.returncode)
                    finally:
                        if process.poll() is None:
                            process.kill()
                        process.wait(timeout=15)
                    raise ValueError(f'{name} timed out; owned process tree cleanup exit {cleanup}')
            outcomes.append({'case': name, 'exitCode': code})
            if code != 0:
                errors.append(f'{name} exited {code}')
                continue
            captured = stdio.read_bytes().upper()
            if b'QCRITICAL' in captured or b'QFATAL' in captured:
                errors.append(f'{name} emitted a severe Qt message outside its test log')
        except (OSError, ValueError, subprocess.SubprocessError) as error:
            outcomes.append({'case': name, 'error': str(error)})
            errors.append(str(error))
    try:
        verify(output)
    except (OSError, ValueError) as error:
        errors.append(str(error))
    try:
        if 'Native picker local path guard: rejected= true dialogStillVisible= true' not in (
                output / 'selection.log').read_text(encoding='utf-8', errors='replace'):
            raise ValueError('local file-boundary rejection was not observed')
        if any(digest(path) != hashes[path.name] for path in (exe, application, driver)):
            raise ValueError('fixture binary changed during execution')
    except (OSError, ValueError) as error:
        errors.append(str(error))
    result['status'] = 'failed' if errors else 'passed'
    result['errors'] = errors
    write_result()
    if errors:
        print('Local native picker fixture failed: ' + '; '.join(errors), file=sys.stderr)
        return 1
    print(f'Local native picker fixture passed: {output}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
