import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
import verify_shell_matrix


def sample_report():
    facts = {'attributes': 6, 'aclSha256': 'b' * 64, 'aclProtected': False, 'zonePresent': False}
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
    return {'schemaVersion': 2, 'status': 'passed', 'cleanupComplete': True, 'errors': [],
            'comInitializationResult': 0, 'osVersion': 'synthetic Windows',
            'identity': {'sha': 'd' * 40, 'runId': '123', 'attempt': '1',
                         'runnerOs': 'Windows', 'runnerArch': 'X64'}, 'cases': cases,
            'parentCases': parents, 'parentComparisons': comparisons}


class ShellMatrixTests(unittest.TestCase):
    def test_accepts_distinct_combinations_and_records_shell_fallbacks(self):
        report = sample_report()
        report['cases'][0].update(indirectResult=-1, indirectName='', shellDisplayName='matrix-workspace-workspace')
        report['cases'][1]['module']['zonePresent'] = True
        verify_shell_matrix.validate(report, report['identity'])

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

    def test_legacy_reports_remain_readable_but_not_sufficient_for_new_ci(self):
        report = sample_report()
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
