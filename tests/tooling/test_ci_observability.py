from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
sys.path.insert(0, str(ROOT / 'tests/tooling'))
import ci_classify
import ci_evidence
import ci_package_inputs
import benchmark_resident
from test_shell_matrix import sample_report as sample_shell_matrix


class TemporaryRepository(unittest.TestCase):
    def setUp(self):
        (ROOT / 'build').mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix='ci-tests-', dir=ROOT / 'build')
        self.root = Path(self.temporary.name)
        self.addCleanup(self.temporary.cleanup)


class ClassificationTests(TemporaryRepository):
    def git(self, *args):
        return subprocess.check_output(['git', *args], cwd=self.root, stderr=subprocess.DEVNULL,
                                       text=True).strip()

    def commit(self, message):
        self.git('add', '.')
        self.git('-c', 'user.name=CI Test', '-c', 'user.email=ci@example.invalid', 'commit', '-m', message)
        return self.git('rev-parse', 'HEAD')

    def setUp(self):
        super().setUp()
        self.git('init', '-b', 'main')
        (self.root / 'docs').mkdir()
        (self.root / 'docs/README.md').write_text('navigation\n', encoding='utf-8')
        self.base = self.commit('base')
        (self.root / 'docs/README.md').write_text('navigation updated\n', encoding='utf-8')
        self.head = self.commit('documentation')
        self.env = {'GITHUB_SHA': self.head, 'GITHUB_RUN_ID': '17', 'GITHUB_RUN_ATTEMPT': '1',
                    'GITHUB_EVENT_NAME': 'push', 'GITHUB_REF': 'refs/heads/main'}

    def test_navigation_only_is_observed_but_never_skips(self):
        result = ci_classify.classify(self.root, self.env, {'before': self.base, 'after': self.head})
        self.assertEqual(result['classification'], 'plain-docs')
        self.assertTrue(result['would_skip'])
        self.assertTrue(result['run_heavy'])

    def test_unknown_contracts_and_missing_baseline_run_full(self):
        (self.root / 'screen.qml').write_text('Item {}\n', encoding='utf-8')
        revision = self.commit('qml')
        self.env['GITHUB_SHA'] = revision
        for before in (self.head, '0' * 40):
            result = ci_classify.classify(self.root, self.env, {'before': before, 'after': revision})
            self.assertEqual(result['classification'], 'full')
            self.assertFalse(result['would_skip'])
            self.assertTrue(result['run_heavy'])
        self.env['GITHUB_EVENT_NAME'] = 'workflow_dispatch'
        self.assertEqual(ci_classify.classify(self.root, self.env, {})['classification'], 'full')

    def test_visual_interface_and_release_inputs_are_never_plain_docs(self):
        for index, name in enumerate(('resources/fonts/sample.ttf', 'src/sample.qml',
                                      'docs/design/tokens/tile-grid.json', 'include/api.h',
                                      'packaging/windows/runtime.json', 'docs/technical/release.md')):
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('fixture\n', encoding='utf-8')
            before = self.git('rev-parse', 'HEAD')
            revision = self.commit('contract ' + str(index))
            env = {**self.env, 'GITHUB_SHA': revision}
            result = ci_classify.classify(self.root, env, {'before': before, 'after': revision})
            with self.subTest(path=name):
                self.assertEqual(result['classification'], 'full')
                self.assertTrue(result['run_heavy'])

    def test_pr_merge_must_match_both_parents(self):
        self.git('checkout', '-b', 'feature', self.base)
        (self.root / 'docs/README.md').write_text('feature navigation\n', encoding='utf-8')
        head = self.commit('feature')
        self.git('checkout', 'main')
        self.git('reset', '--hard', self.base)
        self.git('-c', 'user.name=CI Test', '-c', 'user.email=ci@example.invalid',
                 'merge', '--no-ff', '-m', 'merge', 'feature')
        revision = self.git('rev-parse', 'HEAD')
        env = {**self.env, 'GITHUB_SHA': revision, 'GITHUB_EVENT_NAME': 'pull_request',
               'GITHUB_REPOSITORY': 'owner/repo'}
        payload = {'pull_request': {'base': {'sha': self.base},
                   'head': {'sha': head, 'repo': {'full_name': 'owner/repo'}}}}
        self.assertEqual(ci_classify.classify(self.root, env, payload)['classification'], 'plain-docs')
        payload['pull_request']['head']['sha'] = self.base
        self.assertEqual(ci_classify.classify(self.root, env, payload)['classification'], 'full')


class CacheTests(TemporaryRepository):
    def test_only_fixed_inputs_are_planned_and_source_changes_change_key(self):
        destination = self.root / 'packaging/windows'
        destination.mkdir(parents=True)
        for name in ('runtime.json', 'installer-toolchain.json', 'qt-license-sources.json', 'sources.json'):
            shutil.copyfile(ROOT / 'packaging/windows' / name, destination / name)
        rows = ci_package_inputs.inputs(self.root)
        self.assertGreater(len(rows), 10)
        self.assertTrue(all(row['path'].startswith('build/package-tools/') for row in rows))
        altered = [dict(row) for row in rows]
        altered[0]['url'] += '?different-source'
        self.assertNotEqual(ci_package_inputs.key(rows), ci_package_inputs.key(altered))
        runtime = destination / 'runtime.json'
        data = json.loads(runtime.read_text(encoding='utf-8'))
        data['url'] = 'https://example.invalid/untrusted.exe'
        runtime.write_text(json.dumps(data), encoding='utf-8')
        with self.assertRaises(ValueError):
            ci_package_inputs.inputs(self.root)

    def test_restored_bytes_and_unexpected_entries_are_checked(self):
        path = self.root / 'build/package-tools/example.zip'
        path.parent.mkdir(parents=True)
        path.write_bytes(b'expected')
        rows = [{'path': 'build/package-tools/example.zip', 'url': 'https://github.com/example',
                 'sha256': hashlib.sha256(b'expected').hexdigest()}]
        ci_package_inputs.verify(rows, self.root, restored=True)
        path.write_bytes(b'tampered')
        with self.assertRaisesRegex(ValueError, 'hash mismatch'):
            ci_package_inputs.verify(rows, self.root, restored=True)
        path.write_bytes(b'expected')
        (path.parent / 'extra.exe').write_bytes(b'unexpected')
        with self.assertRaisesRegex(ValueError, 'Unexpected'):
            ci_package_inputs.verify(rows, self.root, restored=True)


class EvidenceTests(TemporaryRepository):
    def setUp(self):
        super().setUp()
        self.env = {'GITHUB_SHA': 'a' * 40, 'GITHUB_RUN_ID': '17', 'GITHUB_RUN_ATTEMPT': '1',
                    'XDSTARTER_NEEDS_CORE': 'success', 'XDSTARTER_NEEDS_WINDOWS': 'success'}
        for role, platform in (('core', 'Linux'), ('windows', 'Windows')):
            directory = self.root / 'build/ci-evidence' / role
            directory.mkdir(parents=True)
            identity = {key: self.env[key] for key in ('GITHUB_SHA', 'GITHUB_RUN_ID', 'GITHUB_RUN_ATTEMPT')}
            identity.update(RUNNER_OS=platform, RUNNER_ARCH='X64')
            timings = {'identity': identity, 'commands': [{'phase': phase, 'exit_code': 0, 'elapsed_ms': 1}
                       for phase in ci_evidence.PHASES[role]]}
            (directory / 'timings.json').write_text(json.dumps(timings), encoding='utf-8')
            for name, tests in ci_evidence.LOGS[role].items():
                count = len(tests)
                rows = [f'{i}/{count} Test #{i}: {test} .... Passed 0.01 sec'
                        for i, test in enumerate(tests, 1)]
                (directory / name).write_text('\n'.join(rows) + f'\n100% tests passed out of {count}\n',
                                               encoding='utf-8')
            if role == 'windows':
                for source in ci_evidence.COPIES.values():
                    path = self.root / source
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_text('{}', encoding='utf-8')
                archived = ROOT / 'docs/technical/validation/issue-85/offscreen'
                for mode, (qpa, graphics_api) in ci_evidence.FRAME_MODES.items():
                    for name in ci_evidence.FRAME_FILES:
                        shutil.copyfile(archived / name, self.root / ci_evidence.COPIES[f'frame-{mode}/{name}'])
                    path = self.root / ci_evidence.COPIES[f'frame-{mode}/frame-trace.json']
                    trace = json.loads(path.read_text(encoding='utf-8'))
                    # Synthetic native identity tests validation only; this is
                    # not a claim that the archived offscreen sample is D3D11.
                    trace.update(qpa=qpa, graphicsApi=graphics_api)
                    path.write_text(json.dumps(trace), encoding='utf-8')
                resident = {name: self.root / ci_evidence.COPIES[f'resident-sample/{name}']
                            for name in ci_evidence.RESIDENT_FILES}
                samples = [{'atNs': i * 1_000_000_000, 'cpu100ns': i * 10_000_000,
                            'workingSetBytes': 100 + i, 'privateBytes': 200 + i,
                            'visibleTopLevelWindows': 0, 'observerNs': 5000}
                           for i in range(benchmark_resident.SAMPLE_COUNT)]
                resident_result = {'schemaVersion': 1, 'processId': 42, 'executableSha256': 'd' * 64,
                                   'productExit': 0, 'buildInfo': {'qt': '6.8.2',
                                       'revision': self.env['GITHUB_SHA'], 'dirty': False, 'distribution': True},
                                   'ci': {**{key: self.env[key] for key in
                                             ('GITHUB_SHA', 'GITHUB_RUN_ID', 'GITHUB_RUN_ATTEMPT')},
                                          'RUNNER_OS': 'Windows', 'RUNNER_ARCH': 'X64'},
                                   'diagnostic': {'intervalMs': 250, 'runId': 'run-1', 'writeFailures': 0,
                                       'initialHookInstalled': True, 'initialSessionActive': True,
                                       'observedHookInstalled': True, 'observedSessionActive': True,
                                       'finalHookInstalled': False, 'finalSessionActive': False},
                                   'samples': samples, 'summary': benchmark_resident.summarize(samples),
                                   'limitations': ['diagnostic overhead', 'sampled visibility', 'hosted platform']}
                resident['resident-sample.json'].write_text(json.dumps(resident_result), encoding='utf-8')
                resident['snapshot.json'].write_text(json.dumps({'processId': 42, 'executableSha256': 'd' * 64,
                    'runId': 'run-1', 'hookInstalled': False, 'sessionActive': False,
                    'diagnosticWriteFailures': 0}), encoding='utf-8')
                resident['stdout.log'].write_text('', encoding='utf-8')
                resident['stderr.log'].write_text('', encoding='utf-8')
                for name, functions in ci_evidence.QT_LOGS.items():
                    cases = ('initTestCase()', *functions, 'cleanupTestCase()')
                    rows = [f'PASS : PrototypeWindowTest::{case}' for case in cases]
                    if name in ('picker-rapid.log', 'picker-immediate.log', 'picker-shown.log'):
                        mode = name.removeprefix('picker-').removesuffix('.log')
                        rows += [f'Native picker before close: "{mode}" dark= {dark}' for dark in ('true', 'false')]
                        if mode == 'rapid':
                            rows += ['Native picker rapid reopen: oldExited= true nextVisible= true']
                        elif mode == 'immediate':
                            rows += ['Native picker after-close observation: processExited= true']
                        else:
                            rows += ['Native picker foreground verified: foreground= true',
                                     'Native picker owner focus restored: ownerForeground= true']
                    if name == 'picker-selection.log':
                        rows += ['Native picker selected file and opened import preview:',
                                 'Native picker launch failure visibly reported and retry canceled:',
                                 'Native picker external focus preserved: otherForeground= true',
                                 'finished= true']
                    if name == 'picker-shutdown.log':
                        rows += ['Native picker shutdown: fakeWinCaptureStopped= true '
                                 'fakeStartCaptureStopped= true childExited= true']
                    rows.append(f'Totals: {len(cases)} passed, 0 failed, 0 skipped, 0 blacklisted, 10ms')
                    (self.root / ci_evidence.COPIES[name]).write_text('\n'.join(rows) + '\n', encoding='utf-8')
                lifecycle = {'status': 'passed', 'serverLifecycleFixture': True, 'cleanWindows11Accepted': False,
                             'sourceIdentity': {'revision': self.env['GITHUB_SHA']},
                             'versionedFixtures': {'synthetic': True, 'commonExecutableSha256': 'a' * 64,
                                 'older': {'version': '0.0.1', 'setupSha256': 'b' * 64, 'markerSha256': 'c' * 64},
                                 'newer': {'version': '0.0.2', 'setupSha256': 'd' * 64, 'markerSha256': 'e' * 64}},
                             'cases': [{'case': case, 'passed': True} for case in ci_evidence.LIFECYCLE_CASES]}
                (self.root / ci_evidence.COPIES['lifecycle.json']).write_text(json.dumps(lifecycle), encoding='utf-8')
                smoke_raw = b'Smoke stage before-application 0 ms\nSmoke stage event-loop-returned 7 ms\n'
                (self.root / ci_evidence.COPIES['smoke-startup.log']).write_bytes(smoke_raw)
                smoke = {'status': 'passed', 'exitCode': 0, 'readerFinished': True, 'outputTruncated': False,
                         'executableSha256': 'a' * 64, 'outputSha256': hashlib.sha256(smoke_raw).hexdigest(),
                         'createReturnMilliseconds': 1, 'firstByteMilliseconds': 2, 'firstStageMilliseconds': 2,
                         'finishedMilliseconds': 7, 'stages': [{'name': 'before-application'},
                                                               {'name': 'event-loop-returned'}]}
                (self.root / ci_evidence.COPIES['smoke-startup.json']).write_text(json.dumps(smoke), encoding='utf-8')
                graphics = {'status': 'passed', 'softwareOpenGLAbsent': True,
                            'results': {name: {} for name in ('normal', 'hardware-failure', 'all-failure',
                                        'delayed-all-failure', 'hosted-resident')}}
                (self.root / ci_evidence.COPIES['graphics.json']).write_text(json.dumps(graphics), encoding='utf-8')
                for fixture in ('workspace', 'profile'):
                    for run in ('initial', 'repeat'):
                        shell = {'passed': True, 'fixtureCleaned': True, 'allFixtureDaclsRestored': True,
                                 'shellDisplay': {'displayAssertionPassed': True, 'matchesNativeShell': True,
                                                  'strictLocalizedDistinctPaths': fixture == 'profile',
                                                  'mode': 'system' if fixture == 'workspace' else 'strict-localized'}}
                        path = self.root / ci_evidence.COPIES[f'shell-{fixture}-{run}.json']
                        path.write_text(json.dumps(shell), encoding='utf-8')
                matrix = sample_shell_matrix()
                matrix['identity'].update(sha=self.env['GITHUB_SHA'], runId=self.env['GITHUB_RUN_ID'],
                                          attempt=self.env['GITHUB_RUN_ATTEMPT'])
                path = self.root / ci_evidence.COPIES['shell-matrix.json']
                path.write_text(json.dumps(matrix), encoding='utf-8')
            env = {**self.env, 'RUNNER_OS': platform, 'RUNNER_ARCH': 'X64', 'XDSTARTER_JOB_STATUS': 'success'}
            env.update({'XDSTARTER_STEP_' + name.upper().replace('-', '_'): 'success'
                        for name in ci_evidence.STEPS[role]})
            ci_evidence.collect(role, self.root, env)

    def test_complete_same_run_and_platform_evidence_passes(self):
        ci_evidence.validate(self.root / 'build/ci-evidence', self.env)

    def test_failed_canceled_missing_and_cross_run_evidence_never_pass(self):
        directory = self.root / 'build/ci-evidence'
        for changed in ({'XDSTARTER_NEEDS_WINDOWS': 'failure'}, {'XDSTARTER_NEEDS_CORE': 'cancelled'},
                        {'XDSTARTER_NEEDS_CORE': 'skipped'}, {'GITHUB_SHA': 'b' * 40},
                        {'GITHUB_RUN_ID': '18'}, {'GITHUB_RUN_ATTEMPT': '2'}):
            with self.subTest(changed=changed), self.assertRaises(ValueError):
                ci_evidence.validate(directory, {**self.env, **changed})
        (directory / 'windows/shell.log').unlink()
        with self.assertRaises(ValueError):
            ci_evidence.validate(directory, self.env)

    def test_tampered_raw_log_does_not_inherit_green_verdict(self):
        (self.root / 'build/ci-evidence/windows/experiments.log').write_text('failed', encoding='utf-8')
        with self.assertRaises(ValueError):
            ci_evidence.validate(self.root / 'build/ci-evidence', self.env)

    def test_smoke_timeout_or_changed_output_cannot_be_green(self):
        directory = self.root / 'build/ci-evidence/windows'
        path = directory / 'smoke-startup.json'
        original = json.loads(path.read_text(encoding='utf-8'))
        manifest = directory / 'manifest.json'
        report = json.loads(manifest.read_text(encoding='utf-8'))
        for mutation in ('timeout', 'output-hash', 'missing-stage', 'impossible-time'):
            data = json.loads(json.dumps(original))
            if mutation == 'timeout':
                data['status'] = 'timeout-before-first-stage'
            elif mutation == 'output-hash':
                data['outputSha256'] = 'b' * 64
            elif mutation == 'missing-stage':
                data['stages'] = []
            else:
                data['firstStageMilliseconds'] = 0
            path.write_text(json.dumps(data), encoding='utf-8')
            report['files']['smoke-startup.json'] = ci_evidence.checksum(path)
            manifest.write_text(json.dumps(report), encoding='utf-8')
            with self.subTest(mutation=mutation), self.assertRaisesRegex(ValueError, 'smoke-startup'):
                ci_evidence.validate(self.root / 'build/ci-evidence', self.env)

    def test_downgrade_requires_distinct_synthetic_versions_and_cases(self):
        directory = self.root / 'build/ci-evidence/windows'
        path = directory / 'lifecycle.json'
        original = json.loads(path.read_text(encoding='utf-8'))
        manifest = directory / 'manifest.json'
        report = json.loads(manifest.read_text(encoding='utf-8'))
        for mutation in ('same-setup', 'same-marker', 'not-synthetic', 'missing-runtime'):
            data = json.loads(json.dumps(original))
            versions = data['versionedFixtures']
            if mutation == 'same-setup':
                versions['older']['setupSha256'] = versions['newer']['setupSha256']
            elif mutation == 'same-marker':
                versions['older']['markerSha256'] = versions['newer']['markerSha256']
            elif mutation == 'not-synthetic':
                versions['synthetic'] = False
            else:
                data['cases'] = [row for row in data['cases'] if row['case'] != 'downgrade-future-schema-runtime']
            path.write_text(json.dumps(data), encoding='utf-8')
            report['files']['lifecycle.json'] = ci_evidence.checksum(path)
            manifest.write_text(json.dumps(report), encoding='utf-8')
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                ci_evidence.validate(self.root / 'build/ci-evidence', self.env)

    def test_failed_case_cannot_be_hidden_by_a_passing_total(self):
        directory = self.root / 'build/ci-evidence/windows'
        path = directory / 'experiments.log'
        path.write_text(path.read_text(encoding='utf-8').replace('Passed', '***Failed', 1), encoding='utf-8')
        manifest = directory / 'manifest.json'
        report = json.loads(manifest.read_text(encoding='utf-8'))
        report['files']['experiments.log'] = ci_evidence.checksum(path)
        manifest.write_text(json.dumps(report), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'failed-test-log'):
            ci_evidence.validate(self.root / 'build/ci-evidence', self.env)

    def test_missing_phase_and_timed_failure_are_not_green(self):
        directory = self.root / 'build/ci-evidence/core'
        path = directory / 'timings.json'
        original = json.loads(path.read_text(encoding='utf-8'))
        manifest = directory / 'manifest.json'
        report = json.loads(manifest.read_text(encoding='utf-8'))
        for mutation in ('missing', 'failed'):
            data = json.loads(json.dumps(original))
            if mutation == 'missing':
                data['commands'] = [row for row in data['commands'] if row['phase'] != 'ctest']
            else:
                data['commands'][0]['exit_code'] = 1
            path.write_text(json.dumps(data), encoding='utf-8')
            report['files']['timings.json'] = ci_evidence.checksum(path)
            manifest.write_text(json.dumps(report), encoding='utf-8')
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                ci_evidence.validate(self.root / 'build/ci-evidence', self.env)

    def test_native_qcritical_is_not_hidden_by_passing_qttest(self):
        directory = self.root / 'build/ci-evidence/windows'
        path = directory / 'picker-rapid.log'
        text = path.read_text(encoding='utf-8')
        path.write_text('QCRITICAL: cleanupThread Thread failed to finish.\n' + text, encoding='utf-8')
        manifest = directory / 'manifest.json'
        report = json.loads(manifest.read_text(encoding='utf-8'))
        report['files']['picker-rapid.log'] = ci_evidence.checksum(path)
        manifest.write_text(json.dumps(report), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'failed-or-incomplete-native-qt-log'):
            ci_evidence.validate(self.root / 'build/ci-evidence', self.env)

    def test_picker_scenarios_cannot_be_replaced_by_passing_totals(self):
        directory = self.root / 'build/ci-evidence/windows'
        manifest = directory / 'manifest.json'
        report = json.loads(manifest.read_text(encoding='utf-8'))
        scenarios = (('picker-rapid.log', 'nextVisible= true', 'incomplete-picker-reopen'),
                     ('picker-selection.log', 'otherForeground= true', 'incomplete-picker-selection'),
                     ('picker-shutdown.log', 'childExited= true', 'incomplete-picker-shutdown'))
        for name, marker, error in scenarios:
            path = directory / name
            original = path.read_text(encoding='utf-8')
            path.write_text(original.replace(marker, 'missing'), encoding='utf-8')
            report['files'][name] = ci_evidence.checksum(path)
            manifest.write_text(json.dumps(report), encoding='utf-8')
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, error):
                ci_evidence.validate(self.root / 'build/ci-evidence', self.env)
            path.write_text(original, encoding='utf-8')
            report['files'][name] = ci_evidence.checksum(path)
            manifest.write_text(json.dumps(report), encoding='utf-8')

    def test_native_picker_log_cannot_be_misattributed_to_another_mode(self):
        directory = self.root / 'build/ci-evidence/windows'
        path = directory / 'picker-shown.log'
        path.write_bytes((directory / 'picker-rapid.log').read_bytes())
        manifest = directory / 'manifest.json'
        report = json.loads(manifest.read_text(encoding='utf-8'))
        report['files']['picker-shown.log'] = ci_evidence.checksum(path)
        manifest.write_text(json.dumps(report), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'ambiguous-native-picker-log'):
            ci_evidence.validate(self.root / 'build/ci-evidence', self.env)

    def test_shell_fixture_evidence_cannot_be_misattributed(self):
        directory = self.root / 'build/ci-evidence/windows'
        path = directory / 'shell-profile-repeat.json'
        path.write_bytes((directory / 'shell-workspace-repeat.json').read_bytes())
        manifest = directory / 'manifest.json'
        report = json.loads(manifest.read_text(encoding='utf-8'))
        report['files']['shell-profile-repeat.json'] = ci_evidence.checksum(path)
        manifest.write_text(json.dumps(report), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'misattributed-shell-evidence'):
            ci_evidence.validate(self.root / 'build/ci-evidence', self.env)

    def test_shell_matrix_from_another_attempt_is_not_same_run(self):
        directory = self.root / 'build/ci-evidence/windows'
        path = directory / 'shell-matrix.json'
        matrix = json.loads(path.read_text(encoding='utf-8'))
        matrix['identity']['attempt'] = '2'
        path.write_text(json.dumps(matrix), encoding='utf-8')
        manifest = directory / 'manifest.json'
        report = json.loads(manifest.read_text(encoding='utf-8'))
        report['files']['shell-matrix.json'] = ci_evidence.checksum(path)
        manifest.write_text(json.dumps(report), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'shell-matrix-identity-mismatch'):
            ci_evidence.validate(self.root / 'build/ci-evidence', self.env)

    def test_current_ci_rejects_legacy_matrix_without_parent_observations(self):
        directory = self.root / 'build/ci-evidence/windows'
        path = directory / 'shell-matrix.json'
        matrix = json.loads(path.read_text(encoding='utf-8'))
        matrix['schemaVersion'] = 1
        matrix.pop('parentCases')
        matrix.pop('parentComparisons')
        path.write_text(json.dumps(matrix), encoding='utf-8')
        manifest = directory / 'manifest.json'
        report = json.loads(manifest.read_text(encoding='utf-8'))
        report['files']['shell-matrix.json'] = ci_evidence.checksum(path)
        manifest.write_text(json.dumps(report), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'missing-parent-matrix'):
            ci_evidence.validate(self.root / 'build/ci-evidence', self.env)

    def test_current_ci_rejects_legacy_matrix_without_access_contrasts(self):
        directory = self.root / 'build/ci-evidence/windows'
        path = directory / 'shell-matrix.json'
        matrix = json.loads(path.read_text(encoding='utf-8'))
        matrix['schemaVersion'] = 2
        matrix.pop('aclCases')
        path.write_text(json.dumps(matrix), encoding='utf-8')
        manifest = directory / 'manifest.json'
        report = json.loads(manifest.read_text(encoding='utf-8'))
        report['files']['shell-matrix.json'] = ci_evidence.checksum(path)
        manifest.write_text(json.dumps(report), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'missing-acl-matrix'):
            ci_evidence.validate(self.root / 'build/ci-evidence', self.env)

    def test_shell_modes_reject_missing_or_failed_evidence(self):
        directory = self.root / 'build/ci-evidence/windows'
        manifest = directory / 'manifest.json'
        report = json.loads(manifest.read_text(encoding='utf-8'))
        cases = (
            ('shell-workspace-initial.json', 'matchesNativeShell', False),
            ('shell-workspace-repeat.json', 'displayAssertionPassed', False),
            ('shell-workspace-initial.json', 'strictLocalizedDistinctPaths', None),
            ('shell-workspace-repeat.json', 'mode', 'strict-localized'),
            ('shell-workspace-initial.json', 'passed', False),
            ('shell-profile-initial.json', 'strictLocalizedDistinctPaths', False),
            ('shell-profile-repeat.json', 'matchesNativeShell', False),
            ('shell-profile-initial.json', 'mode', 'system'),
            ('shell-profile-repeat.json', 'fixtureCleaned', False),
            ('shell-workspace-repeat.json', 'allFixtureDaclsRestored', False),
        )
        for name, key, value in cases:
            path = directory / name
            original = path.read_text(encoding='utf-8')
            evidence = json.loads(original)
            target = evidence if key in ('passed', 'fixtureCleaned', 'allFixtureDaclsRestored') else evidence['shellDisplay']
            if value is None:
                del target[key]
            else:
                target[key] = value
            path.write_text(json.dumps(evidence), encoding='utf-8')
            report['files'][name] = ci_evidence.checksum(path)
            manifest.write_text(json.dumps(report), encoding='utf-8')
            with self.subTest(name=name, key=key), self.assertRaisesRegex(ValueError, 'misattributed-shell-evidence'):
                ci_evidence.validate(self.root / 'build/ci-evidence', self.env)
            path.write_text(original, encoding='utf-8')
            report['files'][name] = ci_evidence.checksum(path)
            manifest.write_text(json.dumps(report), encoding='utf-8')

    def test_frame_backend_cannot_be_misattributed_even_with_updated_checksum(self):
        directory = self.root / 'build/ci-evidence/windows'
        path = directory / 'frame-native/frame-trace.json'
        trace = json.loads(path.read_text(encoding='utf-8'))
        trace.update(qpa='offscreen', graphicsApi=1)
        path.write_text(json.dumps(trace), encoding='utf-8')
        manifest = directory / 'manifest.json'
        report = json.loads(manifest.read_text(encoding='utf-8'))
        report['files']['frame-native/frame-trace.json'] = ci_evidence.checksum(path)
        manifest.write_text(json.dumps(report), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'incomplete-frame-trace:native'):
            ci_evidence.validate(self.root / 'build/ci-evidence', self.env)

    def test_frame_critical_log_cannot_hide_behind_successful_totals(self):
        directory = self.root / 'build/ci-evidence/windows'
        path = directory / 'frame-offscreen/test.log'
        path.write_text('QCRITICAL: failed render\n' + path.read_text(encoding='utf-8'), encoding='utf-8')
        manifest = directory / 'manifest.json'
        report = json.loads(manifest.read_text(encoding='utf-8'))
        report['files']['frame-offscreen/test.log'] = ci_evidence.checksum(path)
        manifest.write_text(json.dumps(report), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'incomplete-frame-trace:offscreen'):
            ci_evidence.validate(self.root / 'build/ci-evidence', self.env)

    def test_missing_frame_image_or_audit_step_is_not_green(self):
        directory = self.root / 'build/ci-evidence/windows'
        image = directory / 'frame-native/contrast-dark-ready.png'
        image.unlink()
        with self.assertRaises(ValueError):
            ci_evidence.validate(self.root / 'build/ci-evidence', self.env)
        shutil.copyfile(ROOT / 'docs/technical/validation/issue-85/offscreen/contrast-dark-ready.png', image)
        manifest = directory / 'manifest.json'
        report = json.loads(manifest.read_text(encoding='utf-8'))
        report['steps']['contrast-trace'] = 'skipped'
        manifest.write_text(json.dumps(report), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'missing-or-failed-step'):
            ci_evidence.validate(self.root / 'build/ci-evidence', self.env)

    def test_resident_sample_identity_and_severe_logs_are_rechecked(self):
        directory = self.root / 'build/ci-evidence/windows'
        manifest = directory / 'manifest.json'
        report = json.loads(manifest.read_text(encoding='utf-8'))
        path = directory / 'resident-sample/resident-sample.json'
        original = path.read_text(encoding='utf-8')
        data = json.loads(original)
        data['buildInfo']['revision'] = 'c' * 40
        path.write_text(json.dumps(data), encoding='utf-8')
        report['files']['resident-sample/resident-sample.json'] = ci_evidence.checksum(path)
        manifest.write_text(json.dumps(report), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'incomplete-resident-sample'):
            ci_evidence.validate(self.root / 'build/ci-evidence', self.env)
        path.write_text(original, encoding='utf-8')
        report['files']['resident-sample/resident-sample.json'] = ci_evidence.checksum(path)
        stderr = directory / 'resident-sample/stderr.log'
        stderr.write_text('critical: failed\n', encoding='utf-8')
        report['files']['resident-sample/stderr.log'] = ci_evidence.checksum(stderr)
        manifest.write_text(json.dumps(report), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'incomplete-resident-sample'):
            ci_evidence.validate(self.root / 'build/ci-evidence', self.env)

    def test_resident_sample_from_another_attempt_is_not_same_run(self):
        directory = self.root / 'build/ci-evidence/windows'
        path = directory / 'resident-sample/resident-sample.json'
        data = json.loads(path.read_text(encoding='utf-8'))
        data['ci']['GITHUB_RUN_ATTEMPT'] = '2'
        path.write_text(json.dumps(data), encoding='utf-8')
        manifest = directory / 'manifest.json'
        report = json.loads(manifest.read_text(encoding='utf-8'))
        report['files']['resident-sample/resident-sample.json'] = ci_evidence.checksum(path)
        manifest.write_text(json.dumps(report), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'resident-identity-mismatch'):
            ci_evidence.validate(self.root / 'build/ci-evidence', self.env)


if __name__ == '__main__':
    unittest.main()
