import copy
from pathlib import Path
import sys
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
                          'module': {**facts, 'attributes': 32}, 'ini': dict(facts),
                          'setLocalizedName': 0, 'indirectResult': 0, 'indirectName': '本地化工具',
                          'shellDisplayAvailable': True, 'shellDisplayName': '本地化工具'})
    return {'schemaVersion': 1, 'status': 'passed', 'cleanupComplete': True, 'errors': [],
            'comInitializationResult': 0, 'osVersion': 'synthetic Windows',
            'identity': {'sha': 'd' * 40, 'runId': '123', 'attempt': '1',
                         'runnerOs': 'Windows', 'runnerArch': 'X64'}, 'cases': cases}


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


if __name__ == '__main__':
    unittest.main()
