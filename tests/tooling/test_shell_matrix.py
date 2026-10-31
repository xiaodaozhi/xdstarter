import copy
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
import verify_shell_matrix


def sample_report():
    facts = {'attributes': 6, 'aclSha256': 'b' * 64, 'aclProtected': False, 'zonePresent': False}
    path_facts = {'urlZone': {'managerHResult': 0, 'managerAvailable': True, 'mapHResult': 0,
                              'zone': 0, 'errorType': None, 'errorHResult': None},
                  'volume': {'mountPathAvailable': True, 'mountPathError': 0, 'mountPathSha256': 'a' * 64,
                             'volumeNameAvailable': True, 'volumeNameError': 0, 'volumeNameSha256': 'b' * 64}}
    cases = []
    for folder in ('workspace', 'profile'):
        for module in ('workspace', 'profile'):
            cases.append({'folderLocation': folder, 'moduleLocation': module,
                          'folderDrive': 'C:\\', 'moduleDrive': 'C:\\',
                          'folderFileSystem': 'NTFS', 'moduleFileSystem': 'NTFS',
                          'physicalName': f'matrix-{folder}-{module}', 'folderAttributes': 17,
                          'folderAclSha256': 'c' * 64, 'moduleSha256': 'a' * 64,
                          'modulePathSha256': ('1' if module == 'workspace' else '2') * 64,
                          'module': {**facts, 'attributes': 32}, 'ini': dict(facts),
                          'folderPathFacts': copy.deepcopy(path_facts), 'iniPathFacts': copy.deepcopy(path_facts),
                          'setLocalizedName': 0, 'indirectResult': 0, 'indirectName': '本地化工具',
                          'shellDisplayAvailable': True, 'shellDisplayName': '本地化工具'})
    parents = []
    for index, name in enumerate(('workspace', 'runner-temp', 'profile', 'profile-local')):
        case = copy.deepcopy(cases[0])
        case.pop('folderLocation')
        case.pop('moduleLocation')
        case.update(parentName=name, physicalName=f'parent-{name}', parentAttributes=16,
                    parentPathSha256=str(index + 3) * 64,
                    parentAclSha256=('d' if index < 3 else 'e') * 64, rootAclSha256='f' * 64)
        parents.append(case)
    comparisons = [
        {'left': 'workspace', 'right': 'runner-temp', 'sameVolumeRoot': True,
         'distinctParentPaths': True, 'sameParentAcl': True, 'resourceResolved': True},
        {'left': 'profile', 'right': 'profile-local', 'sameVolumeRoot': True,
         'distinctParentPaths': True, 'sameParentAcl': False, 'resourceResolved': True},
    ]
    acl_cases = []
    for target in ('workspace', 'profile'):
        for template in ('workspace', 'profile'):
            case = copy.deepcopy(cases[0])
            case.pop('folderLocation')
            case.pop('moduleLocation')
            folder_access = ('6' if template == 'workspace' else '7') * 64
            ini_access = ('8' if template == 'workspace' else '9') * 64
            case.update(targetLocation=target, templateLocation=template,
                        physicalName=f'acl-{target}-{template}', folderAccessProtected=True,
                        folderAccessSha256=folder_access, folderTemplateSha256=folder_access,
                        iniAccessSha256=ini_access, iniTemplateSha256=ini_access)
            case['ini']['aclProtected'] = True
            acl_cases.append(case)
    return {'schemaVersion': 4, 'status': 'passed', 'cleanupComplete': True, 'errors': [],
            'comInitializationResult': 0, 'osVersion': 'synthetic Windows',
            'identity': {'sha': 'd' * 40, 'runId': '123', 'attempt': '1',
                         'runnerOs': 'Windows', 'runnerArch': 'X64'}, 'cases': cases,
            'parentCases': parents, 'parentComparisons': comparisons, 'aclCases': acl_cases}


class ShellMatrixTests(unittest.TestCase):
    @unittest.skipUnless(sys.platform == 'win32' and shutil.which('pwsh'), 'requires Windows PowerShell 7')
    def test_read_only_path_facts_on_owned_file(self):
        root = Path(verify_shell_matrix.__file__).resolve().parents[1]
        build = root / 'build'
        build.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='path-facts-', dir=build) as directory:
            fixture = Path(directory) / '本地化.ini'
            fixture.write_bytes(b'owned fixture')
            env = os.environ.copy()
            env['XDSTARTER_TEST_PATH_FACTS_SCRIPT'] = str(root / 'scripts/shell_path_facts.ps1')
            env['XDSTARTER_TEST_PATH_FACTS_FILE'] = str(fixture)
            env['TEMP'] = env['TMP'] = str(build)
            command = r'''
                $ErrorActionPreference = 'Stop'
                . $env:XDSTARTER_TEST_PATH_FACTS_SCRIPT
                Get-XDStarterPathFacts $env:XDSTARTER_TEST_PATH_FACTS_FILE | ConvertTo-Json -Depth 5 -Compress
            '''
            result = subprocess.run([shutil.which('pwsh'), '-NoProfile', '-NonInteractive', '-Command', command],
                                    capture_output=True, text=True, encoding='utf-8', env=env, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)
            verify_shell_matrix.validate_path_facts(json.loads(result.stdout))
            self.assertEqual(fixture.read_bytes(), b'owned fixture')

    def test_path_facts_reject_missing_or_inconsistent_values_but_preserve_api_failures(self):
        for change in ('missing', 'success-without-zone', 'failure-with-zone', 'bad-hash', 'boolean-hr'):
            with self.subTest(change=change):
                report = sample_report()
                facts = report['cases'][0]['iniPathFacts']
                if change == 'missing':
                    report['cases'][0].pop('iniPathFacts')
                elif change == 'success-without-zone':
                    facts['urlZone']['zone'] = None
                elif change == 'failure-with-zone':
                    facts['urlZone']['mapHResult'] = -1
                elif change == 'bad-hash':
                    facts['volume']['volumeNameSha256'] = 'not-a-hash'
                else:
                    facts['urlZone']['mapHResult'] = True
                with self.assertRaises(ValueError):
                    verify_shell_matrix.validate(report, require_path_facts=True)
        report = sample_report()
        facts = report['cases'][0]['iniPathFacts']
        facts['urlZone'].update(managerHResult=-1, managerAvailable=False, mapHResult=None, zone=None)
        facts['volume'].update(volumeNameAvailable=False, volumeNameError=5, volumeNameSha256=None)
        verify_shell_matrix.validate(report, require_path_facts=True)
        facts['volume']['volumeNameError'] = -2147483648
        verify_shell_matrix.validate(report, require_path_facts=True)
        facts['urlZone'].update(managerHResult=None, errorType='System.InvalidOperationException', errorHResult=-1)
        verify_shell_matrix.validate(report, require_path_facts=True)

    @unittest.skipUnless(sys.platform == 'win32' and shutil.which('pwsh'), 'requires Windows PowerShell 7')
    def test_restoration_allows_only_auto_inherited_control_flag(self):
        script = r'''
            . $env:XDSTARTER_TEST_ACCESS_DESCRIPTOR_SCRIPT
            $cases = @(
                @{a='D:(A;;FA;;;SY)'; b='D:AI(A;;FA;;;SY)'; expected=$true},
                @{a='D:AI(A;;FA;;;SY)'; b='D:(A;;FA;;;SY)'; expected=$false},
                @{a='D:(A;;FA;;;SY)'; b='D:P(A;;FA;;;SY)'; expected=$false},
                @{a='D:(A;;FA;;;SY)'; b='D:(A;;FR;;;SY)'; expected=$false},
                @{a='D:(A;;FA;;;SY)'; b='D:(D;;FA;;;SY)'; expected=$false},
                @{a='D:(A;;FA;;;SY)'; b='D:(A;ID;FA;;;SY)'; expected=$false},
                @{a='D:(A;;FA;;;SY)(A;;FR;;;BA)'; b='D:(A;;FR;;;BA)(A;;FA;;;SY)'; expected=$false}
            )
            foreach ($case in $cases) {
                if ((Test-RestoredAccess $case.a $case.b) -ne $case.expected) { exit 1 }
            }
        '''
        env = os.environ.copy()
        env['XDSTARTER_TEST_ACCESS_DESCRIPTOR_SCRIPT'] = str(
            Path(verify_shell_matrix.__file__).parent / 'windows_access_descriptor.ps1')
        result = subprocess.run([shutil.which('pwsh'), '-NoProfile', '-NonInteractive', '-Command', script],
                                capture_output=True, text=True, encoding='utf-8', env=env)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_accepts_distinct_combinations_and_records_shell_fallbacks(self):
        report = sample_report()
        report['cases'][0].update(indirectResult=-1, indirectName='', shellDisplayName='matrix-workspace-workspace')
        report['cases'][1]['module']['zonePresent'] = True
        verify_shell_matrix.validate(report, report['identity'])

    def test_preserved_helper_must_match_the_matrix(self):
        report = sample_report()
        verify_shell_matrix.validate(report, expected_module_sha256='a' * 64)
        with self.assertRaisesRegex(ValueError, 'shell-matrix-helper-mismatch'):
            verify_shell_matrix.validate(report, expected_module_sha256='b' * 64)

        with tempfile.TemporaryDirectory() as directory:
            helper = Path(directory) / 'helper.exe'
            helper.write_bytes(b'synthetic helper for matrix verification')
            digest = hashlib.sha256(helper.read_bytes()).hexdigest()
            for group in ('cases', 'parentCases', 'aclCases'):
                for case in report[group]:
                    case['moduleSha256'] = digest
            matrix = Path(directory) / 'matrix.json'
            matrix.write_text(json.dumps(report), encoding='utf-8')
            script = Path(verify_shell_matrix.__file__)
            command = [sys.executable, str(script), str(matrix), '--helper', str(helper)]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            helper.write_bytes(b'different helper')
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('shell-matrix-helper-mismatch', result.stderr)

    def test_rejects_missing_duplicate_or_mismatched_cases(self):
        for change in ('missing', 'duplicate', 'different-module', 'wrong-volume'):
            with self.subTest(change=change):
                report = sample_report()
                if change == 'missing':
                    report['cases'].pop()
                elif change == 'duplicate':
                    report['cases'][1] = copy.deepcopy(report['cases'][0])
                elif change == 'different-module':
                    report['cases'][1]['moduleSha256'] = 'e' * 64
                else:
                    report['cases'][1]['folderDrive'] = 'D:\\'
                with self.assertRaises(ValueError):
                    verify_shell_matrix.validate(report)

    def test_rejects_missing_facts_failed_cleanup_and_wrong_identity(self):
        for change in ('acl', 'zone', 'ini-flags', 'cleanup', 'reparse', 'identity'):
            with self.subTest(change=change):
                report = sample_report()
                expected = dict(report['identity'])
                if change == 'acl':
                    report['cases'][0]['module']['aclSha256'] = ''
                elif change == 'zone':
                    report['cases'][0]['ini'].pop('zonePresent')
                elif change == 'ini-flags':
                    report['cases'][0]['ini']['attributes'] = 2
                elif change == 'cleanup':
                    report['cleanupComplete'] = False
                elif change == 'reparse':
                    report['cases'][0]['folderAttributes'] |= 1024
                else:
                    report['identity']['attempt'] = '2'
                with self.assertRaises(ValueError):
                    verify_shell_matrix.validate(report, expected)

    def test_rejects_missing_or_inconsistent_parent_observations(self):
        for change in ('missing', 'duplicate', 'unknown-name', 'path-hash', 'parent-reparse',
                       'wrong-volume', 'module-path', 'missing-comparison', 'comparison-lie',
                       'resource-lie', 'old-cross-module-path'):
            with self.subTest(change=change):
                report = sample_report()
                if change == 'missing':
                    report['parentCases'].pop()
                elif change == 'duplicate':
                    report['parentCases'][1] = copy.deepcopy(report['parentCases'][0])
                elif change == 'unknown-name':
                    report['parentCases'][0]['parentName'] = 'unknown'
                elif change == 'path-hash':
                    report['parentCases'][0]['parentPathSha256'] = ''
                elif change == 'parent-reparse':
                    report['parentCases'][0]['parentAttributes'] |= 1024
                elif change == 'wrong-volume':
                    report['parentCases'][0]['folderDrive'] = 'D:\\'
                elif change == 'module-path':
                    report['parentCases'][1]['modulePathSha256'] = '9' * 64
                elif change == 'missing-comparison':
                    report['parentComparisons'].pop()
                elif change == 'comparison-lie':
                    report['parentComparisons'][0]['distinctParentPaths'] = False
                elif change == 'resource-lie':
                    report['parentCases'][1].update(indirectResult=-1, indirectName='')
                else:
                    report['cases'][2]['modulePathSha256'] = '9' * 64
                with self.assertRaises(ValueError):
                    verify_shell_matrix.validate(report, require_parent_cases=True)

    def test_comparison_marks_same_parent_or_different_volume_without_rejecting_observations(self):
        report = sample_report()
        report['parentCases'][1]['parentPathSha256'] = report['parentCases'][0]['parentPathSha256']
        report['parentComparisons'][0]['distinctParentPaths'] = False
        verify_shell_matrix.validate(report, require_parent_cases=True)

        report = sample_report()
        report['parentCases'][1]['folderDrive'] = 'D:\\'
        report['parentComparisons'][0]['sameVolumeRoot'] = False
        report['parentCases'][1].update(indirectResult=-1, indirectName='',
                                        shellDisplayName='parent-runner-temp')
        report['parentComparisons'][0]['resourceResolved'] = False
        verify_shell_matrix.validate(report, require_parent_cases=True)

    def test_rejects_incomplete_or_unapplied_access_contrasts(self):
        for change in ('missing', 'duplicate', 'wrong-target', 'wrong-module', 'missing-hash',
                       'folder-mismatch', 'file-mismatch', 'unprotected-folder', 'unprotected-file',
                       'inconsistent-template'):
            with self.subTest(change=change):
                report = sample_report()
                cases = report['aclCases']
                if change == 'missing':
                    cases.pop()
                elif change == 'duplicate':
                    cases[1] = copy.deepcopy(cases[0])
                elif change == 'wrong-target':
                    cases[0]['targetLocation'] = 'other'
                elif change == 'wrong-module':
                    cases[0]['modulePathSha256'] = 'f' * 64
                elif change == 'missing-hash':
                    cases[0].pop('folderAccessSha256')
                elif change == 'folder-mismatch':
                    cases[0]['folderAccessSha256'] = 'f' * 64
                elif change == 'file-mismatch':
                    cases[0]['iniAccessSha256'] = 'f' * 64
                elif change == 'unprotected-folder':
                    cases[0]['folderAccessProtected'] = False
                elif change == 'unprotected-file':
                    cases[0]['ini']['aclProtected'] = False
                else:
                    cases[2].update(folderAccessSha256='f' * 64, folderTemplateSha256='f' * 64)
                with self.assertRaises(ValueError):
                    verify_shell_matrix.validate(report, require_acl_cases=True)

    def test_equal_access_templates_are_observations_not_a_forced_difference(self):
        report = sample_report()
        for case in report['aclCases']:
            case.update(folderAccessSha256='6' * 64, folderTemplateSha256='6' * 64,
                        iniAccessSha256='8' * 64, iniTemplateSha256='8' * 64)
        verify_shell_matrix.validate(report, require_acl_cases=True)

    def test_legacy_reports_remain_readable_but_not_sufficient_for_new_ci(self):
        report = sample_report()
        report['schemaVersion'] = 3
        verify_shell_matrix.validate(report, require_acl_cases=True)
        with self.assertRaisesRegex(ValueError, 'missing-shell-path-facts'):
            verify_shell_matrix.validate(report, require_path_facts=True)
        report['schemaVersion'] = 2
        report.pop('aclCases')
        verify_shell_matrix.validate(report, require_parent_cases=True)
        with self.assertRaisesRegex(ValueError, 'missing-acl-matrix'):
            verify_shell_matrix.validate(report, require_acl_cases=True)
        report['schemaVersion'] = 1
        report.pop('parentCases')
        report.pop('parentComparisons')
        verify_shell_matrix.validate(report)
        with self.assertRaisesRegex(ValueError, 'missing-parent-matrix'):
            verify_shell_matrix.validate(report, require_parent_cases=True)

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'legacy.json'
            path.write_text(json.dumps(report), encoding='utf-8')
            script = Path(verify_shell_matrix.__file__)
            default = subprocess.run([sys.executable, str(script), str(path)], capture_output=True, text=True)
            legacy = subprocess.run([sys.executable, str(script), str(path), '--allow-legacy'],
                                    capture_output=True, text=True)
            self.assertNotEqual(default.returncode, 0)
            self.assertEqual(legacy.returncode, 0, legacy.stderr)
            self.assertIn('parent observations unavailable', legacy.stdout)


if __name__ == '__main__':
    unittest.main()
