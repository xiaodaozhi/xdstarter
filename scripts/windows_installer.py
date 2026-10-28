"""Compile a per-user installer from the exact verified payload; never install it."""
import json
from pathlib import Path
import re
import subprocess

from windows_package import CONFIG, config, digest, payload_manifest, verify_inventory, write_json


def render(root, audit, output, cache, prefix, *, qa=False):
    if (audit.get('status') != 'passed' or audit.get('platform') != 'windows' or
            audit.get('mode') != 'deployed'):
        raise ValueError('Installer requires a successful Windows deployed audit')
    verify_inventory(root, audit['files'])
    identity = audit['identity']
    if not re.fullmatch('[0-9a-f]{40}', identity['revision']):
        raise ValueError('Installer requires a full source revision')
    runtime = config('runtime.json')
    agreement = root / 'share/notices/Microsoft-VC-runtime/license.rtf'
    if digest(agreement) != runtime['license_sha256']:
        raise ValueError('Installer requires the original Microsoft runtime agreement')
    notice = root / 'share/notices/Windows-runtime-NOTICE.txt'
    if notice.read_bytes() != b'\xef\xbb\xbf' + (CONFIG / 'Windows-runtime-NOTICE.txt').read_bytes():
        raise ValueError('Installer runtime notice is incomplete')
    for material in config('installer-toolchain.json')['inno_setup']['materials']:
        source = cache / material['file'] if material['file'] == 'ChineseSimplified.isl' else (
            root / 'share/notices' / material['file'])
        if digest(source) != material['sha256']:
            raise ValueError('Installer engine material hash mismatch')
    if qa:
        import os
        if os.environ.get('GITHUB_ACTIONS') != 'true' or os.environ.get('RUNNER_ENVIRONMENT') != 'github-hosted':
            raise ValueError('Server lifecycle fixture is restricted to disposable hosted CI')
    substitutions = {'VERSION': identity['version'], 'BUILD': str(identity['build']),
        'PAYLOAD': str(root.resolve()), 'OUTPUT': str(output.resolve()), 'PREFIX': prefix,
        'ICON': str((CONFIG.parents[1] / 'docs/design/assets/app/xdstarter.ico').resolve()),
        'CHINESE': str((cache / 'ChineseSimplified.isl').resolve()),
        'MANIFEST': str((output / (prefix + '.files')).resolve()),
        'MINVERSION': '10.0.17763' if qa else '10.0.22000'}
    text = (CONFIG / 'installer.iss.in').read_text(encoding='utf-8')
    for name, value in substitutions.items():
        if any(char in value for char in ('"', '\n', '\r')):
            raise ValueError('Unsafe installer substitution')
        text = text.replace('@' + name + '@', value)
    if re.search(r'@[A-Z]+@', text):
        raise ValueError('Unresolved installer template field')
    return text


def compile_installer(root, audit, output, cache, prefix, development=False, *, qa=False):
    if not development and (audit['identity']['dirty'] or not audit['identity']['distribution']):
        raise ValueError('Development payload cannot become a distribution installer')
    text = render(root, audit, output, cache, prefix, qa=qa)
    manifest = output / (prefix + '.files')
    manifest.write_bytes(payload_manifest(audit['files']).encode('utf-8'))
    source = output / (prefix + '.iss')
    source.write_text(text, encoding='utf-8-sig')
    subprocess.run([str(cache / 'compiler/ISCC.exe'), str(source)], check=True, timeout=300,
                   stdout=subprocess.DEVNULL)
    installer = output / (prefix + '-setup.exe')
    write_json(output / ('installer-qa.json' if qa else 'installer.json'), {
        'status': 'passed', 'identity': audit['identity'], 'file': installer.name,
        'sha256': digest(installer), 'bytes': installer.stat().st_size,
        'executableSha256': digest(root / 'xdstarter.exe'),
        'manifestSha256': digest(manifest), 'compiler': 'Inno Setup 6.7.3',
        'scope': 'current-user', 'signed': False, 'developmentFixture': development,
        'serverLifecycleFixture': qa,
        'limits': ['Compiler success does not establish clean Windows 11 installation acceptance.']})
    # Absolute build paths are needed only while ISCC runs, never shipped as evidence.
    source.unlink()
    manifest.unlink()
    return installer
