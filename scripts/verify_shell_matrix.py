"""Validate the completeness of the hosted Shell cross-location observations."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path


def require(condition, message):
    if not condition:
        raise ValueError(message)


def is_hash(value):
    return isinstance(value, str) and re.fullmatch(r'[0-9a-f]{64}', value) is not None


def validate_path_facts(facts):
    require(isinstance(facts, dict), 'missing-shell-path-facts')
    zone, volume = facts.get('urlZone'), facts.get('volume')
    require(isinstance(zone, dict) and isinstance(volume, dict), 'missing-shell-path-facts')
    require({'managerHResult', 'managerAvailable', 'mapHResult', 'zone', 'errorType', 'errorHResult'} <= zone.keys() and
            {'mountPathAvailable', 'mountPathError', 'mountPathSha256', 'volumeNameAvailable',
             'volumeNameError', 'volumeNameSha256'} <= volume.keys(), 'missing-shell-path-facts')
    for key in ('managerHResult', 'mapHResult', 'errorHResult'):
        require(zone.get(key) is None or type(zone[key]) is int and -(2 ** 31) <= zone[key] < 2 ** 31,
                'invalid-shell-url-zone')
    require(type(zone.get('managerAvailable')) is bool and
            (zone.get('zone') is None or type(zone['zone']) is int and 0 <= zone['zone'] < 2 ** 32) and
            (zone.get('errorType') is None or isinstance(zone['errorType'], str) and
             re.fullmatch(r'[A-Za-z0-9_.]+', zone['errorType']) is not None),
            'invalid-shell-url-zone')
    if zone.get('errorType') is not None:
        require(type(zone.get('errorHResult')) is int, 'invalid-shell-url-zone')
    else:
        require(zone.get('errorHResult') is None and type(zone.get('managerHResult')) is int,
                'invalid-shell-url-zone')
        if zone['managerHResult'] >= 0 and zone['managerAvailable']:
            require(type(zone.get('mapHResult')) is int and
                    ((zone['mapHResult'] >= 0 and type(zone.get('zone')) is int) or
                     (zone['mapHResult'] < 0 and zone.get('zone') is None)), 'invalid-shell-url-zone')
        else:
            require(zone.get('mapHResult') is None and zone.get('zone') is None, 'invalid-shell-url-zone')
    for prefix in ('mountPath', 'volumeName'):
        available, error, digest = (volume.get(prefix + suffix) for suffix in ('Available', 'Error', 'Sha256'))
        require(type(available) is bool and type(error) is int and error >= 0 and
                ((available and error == 0 and is_hash(digest)) or (not available and digest is None)),
                'invalid-shell-volume-identity')
    require(not volume['volumeNameAvailable'] or volume['mountPathAvailable'], 'invalid-shell-volume-identity')


def validate_observation(case, require_path_facts=False):
    require(is_hash(case.get('moduleSha256')) and is_hash(case.get('folderAclSha256')),
            'missing-shell-matrix-hash')
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
            isinstance(case.get('shellDisplayName'), str) and bool(case['shellDisplayName']),
            'incomplete-shell-matrix-observation')
    if require_path_facts:
        for key in ('folderPathFacts', 'iniPathFacts'):
            validate_path_facts(case.get(key))


def validate(report, expected_identity=None, require_parent_cases=False, require_acl_cases=False,
             expected_module_sha256=None, require_path_facts=False):
    require(isinstance(report, dict) and type(report.get('schemaVersion')) is int and
            report['schemaVersion'] in (1, 2, 3, 4) and
            report.get('status') == 'passed' and report.get('cleanupComplete') is True and
            report.get('errors') == [] and type(report.get('comInitializationResult')) is int and
            report['comInitializationResult'] >= 0, 'incomplete-shell-matrix')
    require(not require_parent_cases or report['schemaVersion'] >= 2, 'missing-parent-matrix')
    require(not require_acl_cases or report['schemaVersion'] >= 3, 'missing-acl-matrix')
    require(not require_path_facts or report['schemaVersion'] >= 4, 'missing-shell-path-facts')
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
    module_paths = {}
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
        validate_observation(case, report['schemaVersion'] >= 4)
        modules.add(case['moduleSha256'])
        if report['schemaVersion'] >= 2:
            path_hash = case.get('modulePathSha256')
            require(is_hash(path_hash) and (module not in module_paths or module_paths[module] == path_hash),
                    'inconsistent-shell-matrix-module-path')
            module_paths[module] = path_hash
        require(case.get('physicalName') == f'matrix-{folder}-{module}',
                'incomplete-shell-matrix-observation')
    require(len(modules) == 1, 'different-shell-matrix-modules')
    if expected_module_sha256 is not None:
        require(is_hash(expected_module_sha256) and modules == {expected_module_sha256},
                'shell-matrix-helper-mismatch')
    if report['schemaVersion'] >= 2:
        validate_parent_cases(report, locations, modules, module_paths['workspace'])
    if report['schemaVersion'] >= 3:
        validate_acl_cases(report, locations, modules, module_paths['workspace'])
    return report


def validate_acl_cases(report, locations, modules, workspace_module_path):
    cases = report.get('aclCases')
    require(isinstance(cases, list) and len(cases) == 4 and all(isinstance(c, dict) for c in cases),
            'incomplete-acl-matrix')
    pairs = set()
    templates = {}
    for case in cases:
        target, template = case.get('targetLocation'), case.get('templateLocation')
        require(target in ('workspace', 'profile') and template in ('workspace', 'profile') and
                (target, template) not in pairs, 'invalid-acl-matrix-pair')
        pairs.add((target, template))
        validate_observation(case, report['schemaVersion'] >= 4)
        require(case.get('physicalName') == f'acl-{target}-{template}' and
                (case.get('folderDrive'), case.get('folderFileSystem')) == locations[target] and
                (case.get('moduleDrive'), case.get('moduleFileSystem')) == locations['workspace'] and
                case.get('moduleSha256') in modules and case.get('modulePathSha256') == workspace_module_path,
                'invalid-acl-matrix-location')
        keys = ('folderAccessSha256', 'iniAccessSha256', 'folderTemplateSha256', 'iniTemplateSha256')
        require(all(is_hash(case.get(key)) for key in keys) and
                case['folderAccessSha256'] == case['folderTemplateSha256'] and
                case['iniAccessSha256'] == case['iniTemplateSha256'] and
                case.get('folderAccessProtected') is True and case['ini']['aclProtected'] is True,
                'invalid-acl-matrix-descriptor')
        descriptors = (case['folderTemplateSha256'], case['iniTemplateSha256'])
        require(template not in templates or templates[template] == descriptors,
                'inconsistent-acl-matrix-template')
        templates[template] = descriptors


def validate_parent_cases(report, locations, modules, workspace_module_path):
    cases = report.get('parentCases')
    names = {'workspace', 'runner-temp', 'profile', 'profile-local'}
    require(isinstance(cases, list) and len(cases) == 4 and all(isinstance(c, dict) for c in cases),
            'incomplete-parent-matrix')
    by_name = {}
    parents = {}
    for case in cases:
        name = case.get('parentName')
        require(isinstance(name, str) and name in names and name not in by_name, 'invalid-parent-matrix-name')
        by_name[name] = case
        validate_observation(case, report['schemaVersion'] >= 4)
        require(case.get('moduleSha256') in modules and case.get('modulePathSha256') == workspace_module_path and
                case.get('physicalName') == f'parent-{name}',
                'invalid-parent-matrix-module')
        for key in ('parentPathSha256', 'parentAclSha256', 'rootAclSha256'):
            require(is_hash(case.get(key)), 'missing-parent-matrix-hash')
        require(type(case.get('parentAttributes')) is int and not case['parentAttributes'] & 1024,
                'invalid-parent-matrix-directory')
        for label in ('folder', 'module'):
            require(isinstance(case.get(label + 'Drive'), str) and case[label + 'Drive'] and
                    isinstance(case.get(label + 'FileSystem'), str) and case[label + 'FileSystem'],
                    'missing-parent-matrix-volume')
        require((case['moduleDrive'], case['moduleFileSystem']) == locations['workspace'],
                'invalid-parent-matrix-module-location')
        if name in ('workspace', 'profile'):
            require((case['folderDrive'], case['folderFileSystem']) == locations[name],
                    'inconsistent-parent-matrix-volume')
        parent = (case['folderDrive'].casefold(), case['folderFileSystem'],
                  case['parentAttributes'], case['parentAclSha256'])
        identity = case['parentPathSha256']
        require(identity not in parents or parents[identity] == parent, 'inconsistent-parent-matrix-parent')
        parents[identity] = parent
    comparisons = report.get('parentComparisons')
    pairs = {('workspace', 'runner-temp'), ('profile', 'profile-local')}
    require(isinstance(comparisons, list) and len(comparisons) == 2 and
            all(isinstance(c, dict) for c in comparisons), 'incomplete-parent-comparisons')
    seen = set()
    for comparison in comparisons:
        pair = (comparison.get('left'), comparison.get('right'))
        require(all(isinstance(name, str) for name in pair) and pair in pairs and pair not in seen,
                'invalid-parent-comparison')
        seen.add(pair)
        left, right = (by_name[name] for name in pair)
        expected = {
            'sameVolumeRoot': left['folderDrive'].casefold() == right['folderDrive'].casefold(),
            'distinctParentPaths': left['parentPathSha256'] != right['parentPathSha256'],
            'sameParentAcl': left['parentAclSha256'] == right['parentAclSha256'],
            'resourceResolved': all(c['indirectResult'] >= 0 and c['indirectName'] == '本地化工具'
                                    for c in (left, right)),
        }
        for key, value in expected.items():
            require(type(comparison.get(key)) is bool and comparison[key] == value,
                    'invalid-parent-comparison-facts')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('report', type=Path)
    parser.add_argument('--helper', type=Path, help='verify the preserved resource module against the report')
    parser.add_argument('--allow-legacy', action='store_true',
                        help='read a schema 1, 2 or 3 report without the latest observations')
    args = parser.parse_args()
    helper_sha256 = None
    if args.helper is not None:
        require(args.helper.is_file() and not args.helper.is_symlink(), 'missing-shell-matrix-helper')
        with args.helper.open('rb') as helper:
            helper_sha256 = hashlib.file_digest(helper, 'sha256').hexdigest()
    report = validate(json.loads(args.report.read_text(encoding='utf-8')),
                      require_parent_cases=not args.allow_legacy,
                      require_acl_cases=not args.allow_legacy, expected_module_sha256=helper_sha256,
                      require_path_facts=not args.allow_legacy)
    if report['schemaVersion'] == 1:
        print('Legacy Shell cross-location observations verified; parent observations unavailable.')
    elif report['schemaVersion'] == 2:
        print('Legacy Shell cross-location and parent observations verified; access observations unavailable.')
    elif report['schemaVersion'] == 3:
        print('Legacy Shell cross-location, parent and access observations verified; path facts unavailable.')
    else:
        print('Shell cross-location, parent, access and path observations and cleanup verified.')


if __name__ == '__main__':
    main()
