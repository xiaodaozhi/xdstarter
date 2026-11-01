"""Collect and validate same-run, same-commit full CI evidence."""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import sys

import inspect_contrast_frame_trace as frame_trace
import inspect_resident_sample as resident_sample
import benchmark_ui_reveal
import benchmark_ui_reveal_fresh
import verify_shell_matrix

ROOT = Path(__file__).resolve().parents[1]
LOGS = {
    'core': {'ctest.log': ('core.tile_grid', 'core.picker_request_state')},
    'windows': {
        'ctest.log': ('app.d3d11_failure_exit', 'app.qml_smoke', 'app.qt-translations', 'core.tile_grid',
                      'core.picker_request_state',
                      'platform.applications', 'platform.applications.localized', 'platform.contrast-preference',
                      'platform.desktop', 'platform.desktop-width-snap', 'platform.graphics-failure-notice',
                      'platform.installation-guard', 'platform.motion-preference', 'platform.native-menu-resize',
                      'platform.start-button-capture', 'platform.start-button-window-events',
                      'platform.win-key-capture', 'services.application_browser', 'services.application_catalog',
                      'services.desktop_shell', 'services.layout_import', 'services.layout_store',
                      'services.prototype', 'services.search', 'services.session_instance',
                      'ui.accessibility-uia-native', 'ui.desktop_window_host', 'ui.delayed-asset-offscreen', 'ui.delayed-asset-native',
                      'ui.high-contrast-native', 'ui.high-contrast-frame-offscreen',
                      'ui.high-contrast-frame-native', 'ui.layout-import-native',
                      'ui.native-picker-immediate', 'ui.native-picker-rapid', 'ui.native-picker-shown',
                      'ui.native-picker-selection', 'ui.native-picker-shutdown',
                      'ui.prototype', 'ui.scene_graph_failure_host'),
        'smoke.log': ('app.qml_smoke',),
        'shell.log': ('platform.applications', 'platform.applications.localized'),
        'experiments.log': ('experimental.start-bridge', 'experimental.start-bridge-ui-smoke',
                            'experimental.explorer-route-self-test', 'experimental.picker-cancel',
                            'experimental.picker-process'),
    },
}
COPIES = {
    'lifecycle.json': 'build/package/lifecycle-evidence/result.json',
    'install.log': 'build/package/lifecycle-evidence/install.log',
    'install-newer-fixture.log': 'build/package/lifecycle-evidence/install-newer-fixture.log',
    'downgrade-older-fixture.log': 'build/package/lifecycle-evidence/downgrade-older-fixture.log',
    'downgrade-future-schema-runtime.json': 'build/package/lifecycle-evidence/downgrade-future-schema-runtime.json',
    'uninstall.log': 'build/package/lifecycle-evidence/uninstall-keep-data.log',
    'smoke-startup.json': 'build/validation/smoke-startup-ci/result.json',
    'smoke-startup.log': 'build/validation/smoke-startup-ci/smoke.log',
    'search.json': 'build/app-release/search-benchmark-ci.json',
    'ui-search.json': 'build/app-release/ui-search-benchmark-ci.json',
    'shell-workspace-initial.json': 'build/app-release/shell-localization-history/workspace-initial.json',
    'shell-workspace-repeat.json': 'build/app-release/shell-localization-history/workspace-repeat.json',
    'shell-profile-initial.json': 'build/app-release/shell-localization-history/profile-initial.json',
    'shell-profile-repeat.json': 'build/app-release/shell-localization-history/profile-repeat.json',
    'shell-matrix.json': 'build/app-release/shell-localization-history/cross-matrix.json',
    'shell-helper.exe': 'build/app-release/xdstarter_launch_fixture.exe',
    'layout-import.log': 'build/app-release/layout-import-native-evidence/native-d3d11.log',
    'accessibility-uia.log': 'build/app-release/accessibility-uia-native-evidence/test.log',
    'high-contrast.log': 'build/app-release/high-contrast-native-evidence/native-d3d11.log',
    'picker-rapid.log': 'build/app-release/native-picker-evidence/rapid.log',
    'picker-immediate.log': 'build/app-release/native-picker-evidence/immediate.log',
    'picker-shown.log': 'build/app-release/native-picker-evidence/shown.log',
    'picker-selection.log': 'build/app-release/native-picker-evidence/selection.log',
    'picker-shutdown.log': 'build/app-release/native-picker-evidence/shutdown.log',
    'graphics.json': 'build/app-release/graphics-failure-evidence/result.json',
    'graphics-normal.log': 'build/app-release/graphics-failure-evidence/result-normal.log',
    'graphics-hardware-failure.log': 'build/app-release/graphics-failure-evidence/result-hardware-failure.log',
    'graphics-all-failure.log': 'build/app-release/graphics-failure-evidence/result-all-failure.log',
}
FRAME_MODES = {'offscreen': ('offscreen', 1), 'native': ('windows', 4)}
FRAME_FILES = ('frame-trace.json', 'test.log',
               *(f'{scene}-{stage}.png' for scene in frame_trace.SCENES for stage in frame_trace.STAGES))
COPIES.update({f'frame-{mode}/{name}': f'build/app-release/high-contrast-frame-evidence/{mode}/{name}'
               for mode in FRAME_MODES for name in FRAME_FILES})
RESIDENT_FILES = ('resident-sample.json', 'snapshot.json', 'stdout.log', 'stderr.log')
COPIES.update({f'resident-sample/{name}': f'build/validation/resident-sample-ci/{name}'
               for name in RESIDENT_FILES})
REVEAL_FILES = ('manifest.json', 'reveal.json', 'events.jsonl', 'process.log')
COPIES.update({f'ui-reveal/{name}': f'build/validation/ui-reveal-ci/{name}' for name in REVEAL_FILES})
COPIES.update({f'ui-reveal-empty/{name}': f'build/validation/ui-reveal-empty-ci/{name}' for name in REVEAL_FILES})
FRESH_REVEAL_FILES = ('manifest.json', 'summary.json', 'events.jsonl')
COPIES.update({f'ui-reveal-fresh/{name}': f'build/validation/ui-reveal-fresh-ci/{name}'
               for name in FRESH_REVEAL_FILES})
EMPTY_ALLOWED_FILES = {'resident-sample/stdout.log', 'resident-sample/stderr.log'}
LIFECYCLE_CASES = {'junction-parent-refused', 'install', 'modified-obsolete-refused',
                   'upgrade-retire-unchanged', 'upgrade-disable-autostart', 'upgrade-retain-disabled-autostart',
                   'upgrade-enable-autostart', 'running-install-refused', 'running-uninstall-refused',
                   'junction-parent-uninstall-refused', 'install-newer-fixture', 'downgrade-older-fixture',
                   'downgrade-future-schema-runtime', 'uninstall-keep-data', 'reinstall-no-autostart',
                   'uninstall-explicit-data-cleanup'}
QT_LOGS = {
    'accessibility-uia.log': ('nativeAccessibilityDescriptions(search)',
                              'nativeAccessibilityDescriptions(group)',
                              'nativeAccessibilityDescriptions(tile)'),
    'layout-import.log': ('layoutImportDialogThemeAndKeyboard(normal)',
                          'layoutImportDialogThemeAndKeyboard(completion-during-key-click)'),
    'high-contrast.log': ('highContrastTracksSystemPalette()',),
    'picker-rapid.log': ('layoutImportFilePickerOwnership()',),
    'picker-immediate.log': ('layoutImportFilePickerOwnership()',),
    'picker-shown.log': ('layoutImportFilePickerOwnership()',),
    'picker-selection.log': ('layoutImportNativeSelection()', 'layoutImportNativeLaunchFailure()'),
    'picker-shutdown.log': ('layoutImportNativeShutdown()',),
}
PHASES = {'core': {'python-packages', 'configure', 'compile', 'ctest'},
          'windows': {'python-packages', 'configure', 'compile', 'qml-lint', 'ctest'}}
STEPS = {'core': ('build',),
         'windows': ('build', 'picker-audit', 'contrast-trace', 'smoke', 'shell', 'shell-matrix', 'search', 'ui-search', 'ui-reveal', 'ui-reveal-fresh', 'ui-reveal-empty',
                     'experiments', 'resident-sample', 'prepare', 'package')}


def require(ok, message):
    if not ok:
        raise ValueError(message)


def validate_shell_localization_controls(controls):
    require(isinstance(controls, list) and len(controls) == 4, 'missing-shell-localization-controls')
    for index, (kind, control) in enumerate(zip(
            ('literal', 'unicode-module', 'ascii-module', 'system-module'), controls)):
        require(isinstance(control, dict) and type(control.get('case')) is int and
                control['case'] == index and control.get('kind') == kind and
                control.get('configured') is True and control.get('asciiModuleCopied') is True and
                control.get('displayAvailable') is True and
                isinstance(control.get('reference'), str) and bool(control['reference']) and
                isinstance(control.get('displayName'), str) and bool(control['displayName']) and
                isinstance(control.get('indirectName'), str) and
                isinstance(control.get('localizedModule'), str) and
                type(control.get('localizedResourceId')) is int and
                all(isinstance(control.get(key), str) and re.fullmatch(r'[0-9a-f]{1,8}', control[key])
                    for key in ('indirectResultHex', 'localizedResultHex')),
                'incomplete-shell-localization-controls')


def identity(env):
    result = {key: env.get(key, '') for key in
              ('GITHUB_SHA', 'GITHUB_RUN_ID', 'GITHUB_RUN_ATTEMPT', 'RUNNER_OS', 'RUNNER_ARCH')}
    require(bool(re.fullmatch(r'[0-9a-f]{40}', result['GITHUB_SHA'])) and
            re.fullmatch(r'[1-9][0-9]*', result['GITHUB_RUN_ID']) and
            re.fullmatch(r'[1-9][0-9]*', result['GITHUB_RUN_ATTEMPT']) and
            result['RUNNER_OS'] in ('Linux', 'Windows') and result['RUNNER_ARCH'] == 'X64', 'invalid-identity')
    return result


def checksum(path, allow_empty=False):
    require(path.is_file() and not path.is_symlink(), 'missing-raw-evidence')
    size = path.stat().st_size
    require((size > 0 or allow_empty) and size <= 100 * 1024 * 1024, 'empty-or-oversized-evidence')
    with path.open('rb') as stream:
        return {'bytes': size, 'sha256': hashlib.file_digest(stream, 'sha256').hexdigest()}


def collect(role, root=ROOT, env=None):
    env = os.environ if env is None else env
    directory = root / 'build/ci-evidence' / role
    directory.mkdir(parents=True, exist_ok=True)
    report = {'role': role, 'identity': identity(env), 'job_status': env.get('XDSTARTER_JOB_STATUS', ''),
              'steps': {name: env.get('XDSTARTER_STEP_' + name.upper().replace('-', '_'), '')
                        for name in STEPS[role]}, 'files': {}, 'errors': []}
    if role == 'windows':
        for name, source in COPIES.items():
            path = root / source
            if path.is_file() and not path.is_symlink():
                destination = directory / name
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(path, destination)
    for name in ('timings.json', *LOGS[role], *(COPIES if role == 'windows' else ())):
        try:
            report['files'][name] = checksum(directory / name, allow_empty=name in EMPTY_ALLOWED_FILES)
        except (OSError, ValueError) as error:
            report['errors'].append(name + ':' + str(error))
    (directory / 'manifest.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(f"Collected {role} evidence: {len(report['files'])} files, {len(report['errors'])} errors")
    for error in report['errors']:
        print('Evidence collection error: ' + error)
    return report


def validate_role(directory, role, expected):
    report = json.loads((directory / 'manifest.json').read_text(encoding='utf-8'))
    platform = 'Linux' if role == 'core' else 'Windows'
    require(report.get('role') == role and report.get('identity') == {**expected, 'RUNNER_OS': platform, 'RUNNER_ARCH': 'X64'},
            'evidence-identity-mismatch')
    require(report.get('job_status') == 'success' and not report.get('errors'), 'failed-job-or-missing-evidence')
    require(report.get('steps') == {name: 'success' for name in STEPS[role]}, 'missing-or-failed-step')
    required_files = {'timings.json', *LOGS[role], *(COPIES if role == 'windows' else ())}
    require(set(report.get('files', {})) == required_files, 'missing-or-ambiguous-file-set')
    entries = list(directory.rglob('*'))
    expected_directories = {str(parent).replace('\\', '/') for name in required_files
                            for parent in Path(name).parents if str(parent) != '.'}
    require(all(not item.is_symlink() for item in entries) and
            {item.relative_to(directory).as_posix() for item in entries if item.is_dir()} == expected_directories and
            {item.relative_to(directory).as_posix() for item in entries if not item.is_dir()} ==
            required_files | {'manifest.json'},
            'unexpected-or-ambiguous-evidence')
    for name in required_files:
        require(checksum(directory / name, allow_empty=name in EMPTY_ALLOWED_FILES) == report['files'][name],
                'changed-raw-evidence')
    timing = json.loads((directory / 'timings.json').read_text(encoding='utf-8'))
    require(timing.get('identity') == report['identity'], 'timing-identity-mismatch')
    commands = timing.get('commands', [])
    require(commands and all(type(row.get('exit_code')) is int and row['exit_code'] == 0
                             and type(row.get('elapsed_ms')) in (float, int)
                             and 0 <= row['elapsed_ms'] < 86400000 for row in commands), 'failed-timed-command')
    require(PHASES[role] <= {row.get('phase') for row in commands}, 'missing-timed-phase')
    for name, tests in LOGS[role].items():
        text = (directory / name).read_text(encoding='utf-8', errors='replace')
        totals = re.findall(r'100% tests passed(?:, 0 tests failed)? out of ([1-9][0-9]*)', text)
        rows = re.findall(r'(?m)^\s*\d+/\d+\s+Test\s+#\d+:\s+(\S+)\s+\.{2,}\s+(\S+)', text)
        names = [row[0] for row in rows]
        require(len(totals) == 1 and len(rows) == int(totals[0]) and len(set(names)) == len(names) and
                all(row[1] == 'Passed' for row in rows) and set(tests) <= set(names),
                'incomplete-or-failed-test-log')
    if role == 'windows':
        try:
            warm_reveal = benchmark_ui_reveal.inspect(directory / 'ui-reveal', expected)
            fresh_reveal = benchmark_ui_reveal_fresh.inspect(directory / 'ui-reveal-fresh', expected)
            empty_reveal = benchmark_ui_reveal.inspect(directory / 'ui-reveal-empty', expected, expected_tiles=0)
            require(fresh_reveal['schemaVersion'] == 2, 'legacy-fresh-ui-reveal-evidence')
            require(warm_reveal['binarySha256'] == fresh_reveal['binarySha256'] == empty_reveal['binarySha256'],
                    'different-ui-reveal-executables')
        except (OSError, ValueError, KeyError, TypeError) as error:
            raise ValueError('incomplete-ui-reveal-evidence') from error
        smoke = json.loads((directory / 'smoke-startup.json').read_text(encoding='utf-8'))
        smoke_raw = (directory / 'smoke-startup.log').read_bytes()
        timing_values = [smoke.get(name) for name in ('createReturnMilliseconds', 'firstByteMilliseconds',
                         'firstStageMilliseconds', 'finishedMilliseconds')]
        exit_boundaries = [smoke.get(name) for name in ('processExitObservedMilliseconds',
                                                        'pipeEofObservedMilliseconds')]
        stage_names = [stage.get('name') for stage in smoke.get('stages', [])]
        require(smoke.get('status') == 'passed' and smoke.get('exitCode') == 0 and
                smoke.get('readerFinished') is True and smoke.get('outputTruncated') is False and
                isinstance(smoke.get('executableSha256'), str) and
                re.fullmatch(r'[0-9a-f]{64}', smoke['executableSha256']) and
                smoke.get('outputSha256') == hashlib.sha256(smoke_raw).hexdigest() and
                all(type(value) in (int, float) and 0 <= value < 60000 for value in timing_values) and
                timing_values[0] <= timing_values[1] <= timing_values[2] <= timing_values[3] and
                all(type(value) in (int, float) and timing_values[0] <= value <= timing_values[3]
                    for value in exit_boundaries) and
                'before-application' in stage_names and 'event-loop-returned' in stage_names and
                b'Smoke stage before-application' in smoke_raw and b'Smoke stage event-loop-returned' in smoke_raw,
                'incomplete-smoke-startup-evidence')
        try:
            resident_sample.inspect(directory / 'resident-sample', expected['GITHUB_SHA'])
        except (OSError, ValueError, KeyError, TypeError) as error:
            raise ValueError('incomplete-resident-sample') from error
        resident = json.loads((directory / 'resident-sample/resident-sample.json').read_text(encoding='utf-8'))
        require({key: resident['ci'].get(key) for key in expected} == expected, 'resident-identity-mismatch')
        for mode, (qpa, graphics_api) in FRAME_MODES.items():
            try:
                frame_trace.inspect(directory / f'frame-{mode}', qpa, graphics_api, require_hints=True)
            except (OSError, ValueError, KeyError, TypeError) as error:
                raise ValueError('incomplete-frame-trace:' + mode) from error
        for name, functions in QT_LOGS.items():
            text = (directory / name).read_text(encoding='utf-8', errors='replace')
            totals = re.findall(r'Totals:\s+([1-9][0-9]*) passed,\s+0 failed,\s+0 skipped,\s+0 blacklisted', text)
            passes = re.findall(r'(?m)^PASS\s*:\s*PrototypeWindowTest::([^\r\n]+)', text)
            expected_passes = ('initTestCase()', *functions, 'cleanupTestCase()')
            require(len(totals) == 1 and int(totals[0]) == len(expected_passes) and
                    Counter(passes) == Counter(expected_passes) and
                    not re.search(r'\bQ(?:CRITICAL|FATAL)\s*:|FAIL\s*!', text),
                    'failed-or-incomplete-native-qt-log:' + name)
            if name == 'accessibility-uia.log':
                require(text.count('Native UIA scope: non-parent process refused') == 1,
                        'missing-native-uia-scope-refusal')
                for field in ('search', 'group', 'tile'):
                    marker = (f'Native UIA description: {field} matched=1 found=1 '
                              'descriptionHresult=0 variantType=8 controlType=50004')
                    require(text.count(marker) == 1, 'incomplete-native-uia-description:' + field)
            if name in ('picker-rapid.log', 'picker-immediate.log', 'picker-shown.log'):
                mode = name.removeprefix('picker-').removesuffix('.log')
                require(all(f'Native picker before close: "{mode}" dark= {dark}' in text
                            for dark in ('true', 'false')), 'ambiguous-native-picker-log:' + name)
                if mode == 'rapid':
                    require('Native picker rapid reopen:' in text and 'oldExited= true' in text and
                            'nextVisible= true' in text, 'incomplete-picker-reopen')
                elif mode == 'immediate':
                    require('Native picker after-close observation:' in text and 'processExited= true' in text,
                            'incomplete-picker-immediate')
                else:
                    require('Native picker foreground verified:' in text and 'foreground= true' in text and
                            'Native picker owner focus restored:' in text and 'ownerForeground= true' in text,
                            'incomplete-picker-focus')
            if name == 'picker-selection.log':
                require('Native picker selected file and opened import preview:' in text and
                        'Native picker launch failure visibly reported and retry canceled:' in text and
                        'Native picker external focus preserved:' in text and 'otherForeground= true' in text and
                        'finished= true' in text, 'incomplete-picker-selection')
            if name == 'picker-shutdown.log':
                require('Native picker shutdown:' in text and 'fakeWinCaptureStopped= true' in text and
                        'fakeStartCaptureStopped= true' in text and 'childExited= true' in text,
                        'incomplete-picker-shutdown')
        lifecycle = json.loads((directory / 'lifecycle.json').read_text(encoding='utf-8'))
        require(lifecycle.get('status') == 'passed' and lifecycle.get('serverLifecycleFixture') is True and
                lifecycle.get('cleanWindows11Accepted') is False and
                lifecycle.get('sourceIdentity', {}).get('revision') == expected['GITHUB_SHA'] and
                {case.get('case') for case in lifecycle.get('cases', [])} == LIFECYCLE_CASES and
                len(lifecycle['cases']) == len(LIFECYCLE_CASES) and
                all(case.get('passed') is True for case in lifecycle['cases']),
                'incomplete-lifecycle-evidence')
        versions = lifecycle.get('versionedFixtures', {})
        older, newer = versions.get('older', {}), versions.get('newer', {})
        hashes = [versions.get('commonExecutableSha256', ''), older.get('setupSha256', ''),
                  older.get('markerSha256', ''), newer.get('setupSha256', ''), newer.get('markerSha256', '')]
        require(versions.get('synthetic') is True and older.get('version') == '0.0.1' and
                newer.get('version') == '0.0.2' and
                all(isinstance(h, str) and re.fullmatch(r'[0-9a-f]{64}', h) for h in hashes) and
                older['setupSha256'] != newer['setupSha256'] and older['markerSha256'] != newer['markerSha256'],
                'incomplete-downgrade-fixture-evidence')
        graphics = json.loads((directory / 'graphics.json').read_text(encoding='utf-8'))
        require(graphics.get('status') == 'passed' and graphics.get('softwareOpenGLAbsent') is True and
                {'normal', 'hardware-failure', 'all-failure', 'delayed-all-failure', 'hosted-resident'} <=
                set(graphics.get('results', {})), 'incomplete-graphics-evidence')
        for fixture in ('workspace', 'profile'):
            for run in ('initial', 'repeat'):
                shell = json.loads((directory / f'shell-{fixture}-{run}.json').read_text(encoding='utf-8'))
                display = shell.get('shellDisplay', {})
                localized = display.get('strictLocalizedDistinctPaths')
                require(shell.get('passed') is True and shell.get('fixtureCleaned') is True and
                        shell.get('allFixtureDaclsRestored') is True and
                        display.get('displayAssertionPassed') is True and
                        display.get('matchesNativeShell') is True and
                        type(localized) is bool and (fixture == 'workspace' or localized is True) and
                        display.get('mode') == ('system' if fixture == 'workspace' else 'strict-localized'),
                        'incomplete-or-misattributed-shell-evidence')
                validate_shell_localization_controls(shell.get('shellLocalizationControls'))
        matrix = json.loads((directory / 'shell-matrix.json').read_text(encoding='utf-8'))
        verify_shell_matrix.validate(matrix, {'sha': expected['GITHUB_SHA'], 'runId': expected['GITHUB_RUN_ID'],
                                               'attempt': expected['GITHUB_RUN_ATTEMPT'],
                                               'runnerOs': 'Windows', 'runnerArch': 'X64'},
                                     require_parent_cases=True, require_acl_cases=True, require_path_facts=True,
                                     require_junction_cases=True, require_fresh_names=True,
                                     expected_module_sha256=checksum(directory / 'shell-helper.exe')['sha256'])
    return report


def validate(root, env):
    expected = {key: env.get(key, '') for key in ('GITHUB_SHA', 'GITHUB_RUN_ID', 'GITHUB_RUN_ATTEMPT')}
    require(all(env.get('XDSTARTER_NEEDS_' + role.upper()) == 'success' for role in ('core', 'windows')),
            'failed-canceled-or-skipped-platform')
    for role in ('core', 'windows'):
        validate_role(root / role, role, expected)
    if env.get('XDSTARTER_PLAN_RESULT') == 'success':
        print('Shadow classification:', env.get('XDSTARTER_PLAN_CLASSIFICATION', 'unknown'),
              'would_skip:', env.get('XDSTARTER_PLAN_WOULD_SKIP', 'unknown'))
    else:
        print('Classification unavailable; full validation remained enabled.')
    print('Same-run Linux and Windows full-validation evidence verified; no validation was skipped.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=('collect', 'validate'))
    parser.add_argument('--role', choices=('core', 'windows'))
    parser.add_argument('--directory', default='build/ci-gate')
    args = parser.parse_args()
    if args.mode == 'collect':
        require(args.role is not None, 'missing-role')
        collect(args.role)
    else:
        validate(ROOT / args.directory, os.environ)


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, KeyError, TypeError, json.JSONDecodeError) as error:
        print('CI evidence error: ' + str(error), file=sys.stderr)
        sys.exit(1)
