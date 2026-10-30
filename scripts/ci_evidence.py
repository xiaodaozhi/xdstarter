"""Collect and validate same-run, same-commit full CI evidence."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import sys

ROOT = Path(__file__).resolve().parents[1]
LOGS = {
    'core': {'ctest.log': ('core.tile_grid',)},
    'windows': {
        'ctest.log': ('app.d3d11_failure_exit', 'app.qml_smoke', 'app.qt-translations', 'core.tile_grid',
                      'platform.applications', 'platform.applications.localized', 'platform.contrast-preference',
                      'platform.desktop', 'platform.desktop-width-snap', 'platform.graphics-failure-notice',
                      'platform.installation-guard', 'platform.motion-preference', 'platform.native-menu-resize',
                      'platform.start-button-capture', 'platform.start-button-window-events',
                      'platform.win-key-capture', 'services.application_browser', 'services.application_catalog',
                      'services.desktop_shell', 'services.layout_import', 'services.layout_store',
                      'services.prototype', 'services.search', 'services.session_instance',
                      'ui.desktop_window_host', 'ui.high-contrast-native', 'ui.layout-import-native',
                      'ui.native-picker-immediate', 'ui.native-picker-rapid', 'ui.native-picker-shown',
                      'ui.prototype', 'ui.scene_graph_failure_host'),
        'smoke.log': ('app.qml_smoke',),
        'shell.log': ('platform.applications', 'platform.applications.localized'),
        'experiments.log': ('experimental.start-bridge', 'experimental.start-bridge-ui-smoke',
                            'experimental.explorer-route-self-test', 'experimental.picker-cancel'),
    },
}
COPIES = {
    'lifecycle.json': 'build/package/lifecycle-evidence/result.json',
    'install.log': 'build/package/lifecycle-evidence/install.log',
    'uninstall.log': 'build/package/lifecycle-evidence/uninstall-keep-data.log',
    'search.json': 'build/app-release/search-benchmark-ci.json',
    'ui-search.json': 'build/app-release/ui-search-benchmark-ci.json',
    'layout-import.log': 'build/app-release/layout-import-native-evidence/native-d3d11.log',
    'high-contrast.log': 'build/app-release/high-contrast-native-evidence/native-d3d11.log',
    'picker-rapid.log': 'build/app-release/native-picker-evidence/rapid.log',
    'picker-immediate.log': 'build/app-release/native-picker-evidence/immediate.log',
    'picker-shown.log': 'build/app-release/native-picker-evidence/shown.log',
    'graphics.json': 'build/app-release/graphics-failure-evidence/result.json',
    'graphics-normal.log': 'build/app-release/graphics-failure-evidence/result-normal.log',
    'graphics-hardware-failure.log': 'build/app-release/graphics-failure-evidence/result-hardware-failure.log',
    'graphics-all-failure.log': 'build/app-release/graphics-failure-evidence/result-all-failure.log',
}
LIFECYCLE_CASES = {'junction-parent-refused', 'install', 'modified-obsolete-refused',
                   'upgrade-retire-unchanged', 'upgrade-disable-autostart', 'upgrade-retain-disabled-autostart',
                   'upgrade-enable-autostart', 'running-install-refused', 'running-uninstall-refused',
                   'junction-parent-uninstall-refused', 'uninstall-keep-data', 'reinstall-no-autostart',
                   'uninstall-explicit-data-cleanup'}
QT_LOGS = ('layout-import.log', 'high-contrast.log', 'picker-rapid.log',
           'picker-immediate.log', 'picker-shown.log')
PHASES = {'core': {'python-packages', 'configure', 'compile', 'ctest'},
          'windows': {'python-packages', 'configure', 'compile', 'qml-lint', 'ctest'}}
STEPS = {'core': ('build',),
         'windows': ('build', 'smoke', 'shell', 'search', 'ui-search', 'experiments', 'prepare', 'package')}


def require(ok, message):
    if not ok:
        raise ValueError(message)


def identity(env):
    result = {key: env.get(key, '') for key in
              ('GITHUB_SHA', 'GITHUB_RUN_ID', 'GITHUB_RUN_ATTEMPT', 'RUNNER_OS', 'RUNNER_ARCH')}
    require(bool(re.fullmatch(r'[0-9a-f]{40}', result['GITHUB_SHA'])) and
            re.fullmatch(r'[1-9][0-9]*', result['GITHUB_RUN_ID']) and
            re.fullmatch(r'[1-9][0-9]*', result['GITHUB_RUN_ATTEMPT']) and
            result['RUNNER_OS'] in ('Linux', 'Windows') and result['RUNNER_ARCH'] == 'X64', 'invalid-identity')
    return result


def checksum(path):
    require(path.is_file() and not path.is_symlink(), 'missing-raw-evidence')
    size = path.stat().st_size
    require(0 < size <= 100 * 1024 * 1024, 'empty-or-oversized-evidence')
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
                shutil.copyfile(path, directory / name)
    for name in ('timings.json', *LOGS[role], *(COPIES if role == 'windows' else ())):
        try:
            report['files'][name] = checksum(directory / name)
        except (OSError, ValueError) as error:
            report['errors'].append(name + ':' + str(error))
    (directory / 'manifest.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report))
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
    require({item.name for item in directory.iterdir()} == required_files | {'manifest.json'},
            'unexpected-or-ambiguous-evidence')
    for name in required_files:
        require(checksum(directory / name) == report['files'][name], 'changed-raw-evidence')
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
        for name in QT_LOGS:
            text = (directory / name).read_text(encoding='utf-8', errors='replace')
            totals = re.findall(r'Totals:\s+([1-9][0-9]*) passed,\s+0 failed,\s+0 skipped,\s+0 blacklisted', text)
            require(len(totals) == 1 and not re.search(r'\bQ(?:CRITICAL|FATAL)\s*:|FAIL\s*!', text),
                    'failed-or-incomplete-native-qt-log:' + name)
        lifecycle = json.loads((directory / 'lifecycle.json').read_text(encoding='utf-8'))
        require(lifecycle.get('status') == 'passed' and lifecycle.get('serverLifecycleFixture') is True and
                lifecycle.get('cleanWindows11Accepted') is False and
                lifecycle.get('sourceIdentity', {}).get('revision') == expected['GITHUB_SHA'] and
                {case.get('case') for case in lifecycle.get('cases', [])} == LIFECYCLE_CASES and
                len(lifecycle['cases']) == len(LIFECYCLE_CASES) and
                all(case.get('passed') is True for case in lifecycle['cases']),
                'incomplete-lifecycle-evidence')
        graphics = json.loads((directory / 'graphics.json').read_text(encoding='utf-8'))
        require(graphics.get('status') == 'passed' and graphics.get('softwareOpenGLAbsent') is True and
                {'normal', 'hardware-failure', 'all-failure', 'delayed-all-failure', 'hosted-resident'} <=
                set(graphics.get('results', {})), 'incomplete-graphics-evidence')
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
