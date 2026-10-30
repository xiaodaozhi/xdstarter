"""Validate the completeness of the hosted Shell cross-location observations."""
from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


def require(condition, message):
    if not condition:
        raise ValueError(message)


def is_hash(value):
    return isinstance(value, str) and re.fullmatch(r'[0-9a-f]{64}', value) is not None


def validate(report, expected_identity=None):
    require(isinstance(report, dict) and report.get('schemaVersion') == 1 and
            report.get('status') == 'passed' and report.get('cleanupComplete') is True and
            report.get('errors') == [] and type(report.get('comInitializationResult')) is int and
            report['comInitializationResult'] >= 0, 'incomplete-shell-matrix')
    identity = report.get('identity')
    require(isinstance(identity, dict) and isinstance(identity.get('sha'), str) and
            re.fullmatch(r'[0-9a-f]{40}', identity['sha']) is not None and
            all(isinstance(identity.get(key), str) and re.fullmatch(r'[1-9][0-9]*', identity[key])
                for key in ('runId', 'attempt')) and identity.get('runnerOs') == 'Windows' and
            identity.get('runnerArch') == 'X64', 'invalid-shell-matrix-identity')
    if expected_identity is not None:
        require(identity == expected_identity, 'shell-matrix-identity-mismatch')
    require(isinstance(report.get('osVersion'), str) and report['osVersion'], 'missing-os-version')
    cases = report.get('cases')
    require(isinstance(cases, list) and len(cases) == 4 and all(isinstance(c, dict) for c in cases),
            'incomplete-shell-matrix-cases')
    observed = set()
    modules = set()
    locations = {}
    for case in cases:
        folder, module = case.get('folderLocation'), case.get('moduleLocation')
        pair = (folder, module)
        require(folder in ('workspace', 'profile') and module in ('workspace', 'profile') and
                pair not in observed, 'invalid-shell-matrix-pair')
        observed.add(pair)
        for label, location in (('folder', folder), ('module', module)):
            drive, filesystem = case.get(label + 'Drive'), case.get(label + 'FileSystem')
            require(isinstance(drive, str) and drive and isinstance(filesystem, str) and filesystem,
                    'missing-shell-matrix-volume')
            require(location not in locations or locations[location] == (drive, filesystem),
                    'inconsistent-shell-matrix-volume')
            locations[location] = (drive, filesystem)
        require(is_hash(case.get('moduleSha256')) and is_hash(case.get('folderAclSha256')),
                'missing-shell-matrix-hash')
        modules.add(case['moduleSha256'])
        require(type(case.get('folderAttributes')) is int and not case['folderAttributes'] & 1024,
                'invalid-shell-matrix-folder')
        for label in ('ini', 'module'):
            facts = case.get(label)
            require(isinstance(facts, dict) and is_hash(facts.get('aclSha256')) and
                    type(facts.get('aclProtected')) is bool and type(facts.get('zonePresent')) is bool and
                    type(facts.get('attributes')) is int and not facts['attributes'] & 1024,
                    'missing-shell-matrix-file-facts')
        require(case['ini']['attributes'] & 6 == 6, 'missing-desktop-ini-attributes')
        require(type(case.get('setLocalizedName')) is int and case['setLocalizedName'] >= 0 and
                type(case.get('indirectResult')) is int and isinstance(case.get('indirectName'), str) and
                (case['indirectResult'] < 0 or bool(case['indirectName'])) and
                case.get('shellDisplayAvailable') is True and
                isinstance(case.get('shellDisplayName'), str) and bool(case['shellDisplayName']) and
                case.get('physicalName') == f'matrix-{folder}-{module}',
                'incomplete-shell-matrix-observation')
    require(len(modules) == 1, 'different-shell-matrix-modules')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('report', type=Path)
    args = parser.parse_args()
    validate(json.loads(args.report.read_text(encoding='utf-8')))
    print('Four hosted Shell cross-location observations and cleanup verified.')


if __name__ == '__main__':
    main()
