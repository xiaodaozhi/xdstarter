"""Exercise a Server-compatible installer fixture only on disposable hosted CI.

The product installer still requires Windows 11. This fixture changes only its
minimum OS check and is never shipped or treated as clean Windows 11 evidence.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import uuid

from windows_package import ROOT, Workflow, digest, generated, write_json
from windows_installer import compile_installer


def main():
    if (os.name != 'nt' or os.environ.get('GITHUB_ACTIONS') != 'true' or
            os.environ.get('RUNNER_ENVIRONMENT') != 'github-hosted'):
        raise ValueError('Installer execution requires a disposable GitHub-hosted Windows runner')
    import winreg
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', default='build/package')
    parser.add_argument('--package-cache', default='build/package-tools')
    parser.add_argument('--qt-prefix')
    args = parser.parse_args()
    args.preset, args.development = 'app-release', False
    workflow = Workflow(args)
    audit = workflow.read_audit()
    evidence = generated(workflow.output / 'lifecycle-evidence')
    evidence.mkdir(exist_ok=True)
    work = generated(ROOT / 'build' / ('installer-lifecycle-' + uuid.uuid4().hex))
    work.mkdir()
    fixture = compile_installer(workflow.root, audit, work, workflow.cache,
                                'server-lifecycle-fixture', qa=True)
    installed = work / 'application/installed'
    uninstall_key = r'Software\Microsoft\Windows\CurrentVersion\Uninstall\{C5771F18-616D-49D2-9DBF-5E09C5F8D853}_is1'
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, uninstall_key):
            raise ValueError('Existing installation detected; refusing to touch it')
    except FileNotFoundError:
        pass
    data = Path(os.environ['LOCALAPPDATA']) / 'xiaodaozhi/XDStarter'
    if (data / 'layout.json').exists():
        raise ValueError('Existing default layout detected; refusing to overwrite it')
    data.mkdir(parents=True, exist_ok=True)
    layout = data / 'layout.json'
    original = b'{"schemaVersion":999,"futureData":"preserve verbatim"}\n'
    layout.write_bytes(original)
    custom = work / 'custom-layout.json'
    custom.write_bytes(original)
    run_key = r'Software\Microsoft\Windows\CurrentVersion\Run'
    outcomes = []
    guard_executable = workflow.build / 'bin/xdstarter_installation_guard_tests.exe'
    guard_environment = dict(os.environ)
    guard_environment['PATH'] = str(workflow.qt / 'bin') + os.pathsep + guard_environment.get('PATH', '')

    def wait_for_uninstaller():
        # Inno's original uninstaller can exit before its temporary engine.
        # Observe the shared gate, rather than retrying Setup or sleeping blindly.
        started = time.monotonic()
        while True:
            probe = subprocess.run([str(guard_executable), 'install'], input=b'\n',
                capture_output=True, timeout=5, env=guard_environment)
            if probe.returncode == 0 and probe.stdout.strip() == b'ready':
                return round((time.monotonic() - started) * 1000)
            if probe.returncode != 2:
                raise ValueError('Uninstaller completion probe failed: ' + str(probe.returncode))
            if time.monotonic() - started >= 15:
                raise ValueError('Uninstaller did not release the installation gate')
            time.sleep(0.05)

    def invoke(executable, label, *extra, success=True):
        result = subprocess.run([str(executable), '/VERYSILENT', '/SUPPRESSMSGBOXES',
            '/NORESTART', '/SP-', '/LOG=' + str(evidence / (label + '.log')), *map(str, extra)],
            capture_output=True, timeout=120)
        if (result.returncode == 0) != success:
            raise ValueError(label + ' unexpected exit code ' + str(result.returncode))
        outcome = {'case': label, 'exitCode': result.returncode, 'passed': True}
        if success and executable.name.casefold().startswith('unins'):
            outcome['installationGateWaitMs'] = wait_for_uninstaller()
        outcomes.append(outcome)

    def installed_payload_matches():
        for item in audit['files']:
            if digest(installed / item['path']) != item['sha256']:
                raise ValueError('Installed payload differs from audit: ' + item['path'])
        if layout.read_bytes() != original or custom.read_bytes() != original:
            raise ValueError('Installer modified default or custom user data')

    def startup_value():
        try:
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, run_key) as key:
                return winreg.QueryValueEx(key, 'XDStarter')[0]
        except FileNotFoundError:
            return None

    def create_junction(link, target):
        generated(link)
        generated(target)
        subprocess.run(['cmd.exe', '/c', 'mklink', '/J', str(link), str(target)],
            check=True, capture_output=True, timeout=15)

    success = False
    try:
        target = generated(work / 'unrelated-directory')
        target.mkdir()
        link = generated(work / 'linked-parent')
        create_junction(link, target)
        try:
            invoke(fixture, 'junction-parent-refused', '/DIR=' + str(link / 'installed'), success=False)
            if any(target.iterdir()) or startup_value() is not None:
                raise ValueError('Installer crossed the junction-parent boundary')
        finally:
            # Remove only the owned junction, never its target tree.
            link.rmdir()
        invoke(fixture, 'install', '/DIR=' + str(installed), '/TASKS=autostart')
        installed_payload_matches()
        expected_startup = '"' + str(installed / 'xdstarter.exe') + '"'
        if startup_value() != expected_startup:
            raise ValueError('Current-user autostart entry mismatch')
        unknown = installed / 'user-note.txt'
        unknown.write_bytes(b'unknown file must be preserved')
        obsolete = installed / 'opengl32sw.dll'
        obsolete.write_bytes(b'old owned software OpenGL fixture')
        old_hash = digest(obsolete)
        manifest = installed / 'install-manifest.txt'
        with manifest.open('a', encoding='utf-8') as stream:
            stream.write(old_hash + '\topengl32sw.dll\n')
        obsolete.write_bytes(b'user modification')
        invoke(fixture, 'modified-obsolete-refused', '/DIR=' + str(installed), success=False)
        if obsolete.read_bytes() != b'user modification':
            raise ValueError('Modified obsolete file was deleted')
        obsolete.write_bytes(b'old owned software OpenGL fixture')
        invoke(fixture, 'upgrade-retire-unchanged', '/DIR=' + str(installed))
        installed_payload_matches()
        if obsolete.exists() or not unknown.is_file() or startup_value() != expected_startup:
            raise ValueError('Upgrade ownership cleanup or previous-task retention failed')
        invoke(fixture, 'upgrade-disable-autostart', '/DIR=' + str(installed), '/TASKS=')
        if startup_value() is not None:
            raise ValueError('Upgrade did not honor disabled autostart selection')
        invoke(fixture, 'upgrade-retain-disabled-autostart', '/DIR=' + str(installed))
        if startup_value() is not None:
            raise ValueError('Upgrade did not retain disabled autostart selection')
        invoke(fixture, 'upgrade-enable-autostart', '/DIR=' + str(installed), '/TASKS=autostart')
        installed_payload_matches()
        if startup_value() != expected_startup:
            raise ValueError('Upgrade did not honor enabled autostart selection')

        guard = subprocess.Popen([str(guard_executable), 'runtime'], stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=guard_environment)
        try:
            if guard.stdout.readline().strip() != b'ready':
                raise ValueError('Native runtime guard fixture did not start')
            invoke(fixture, 'running-install-refused', '/DIR=' + str(installed), success=False)
            invoke(installed / 'unins000.exe', 'running-uninstall-refused', success=False)
        finally:
            guard.communicate(b'\n', timeout=15)
        installed_payload_matches()
        # The existing future-schema read-only path must survive normal timed exit.
        subprocess.run([str(installed / 'xdstarter.exe'), '--layout-file', str(custom),
            '--start-capture-diagnostics', str(evidence / 'future-schema-runtime.json'),
            '--start-capture-duration', '1'], check=True, timeout=30)
        if custom.read_bytes() != original:
            raise ValueError('Older schema reader modified future user data')
        container = generated(installed.parent)
        relocated = generated(work / 'relocated-application')
        container.rename(relocated)
        linked = False
        try:
            create_junction(container, relocated)
            linked = True
            invoke(installed / 'unins000.exe', 'junction-parent-uninstall-refused', success=False)
            installed_payload_matches()
        finally:
            if linked:
                container.rmdir()
            generated(relocated).rename(generated(container))
        invoke(installed / 'unins000.exe', 'uninstall-keep-data')
        if layout.read_bytes() != original or not unknown.is_file() or startup_value() is not None:
            raise ValueError('Uninstall did not preserve data/unknown files or remove startup entry')
        invoke(fixture, 'reinstall-no-autostart', '/DIR=' + str(installed), '/TASKS=')
        (data / 'layout.json.bak').write_bytes(original)
        (data / 'cache').mkdir(exist_ok=True)
        (data / 'cache/owned-cache').write_bytes(b'cache')
        invoke(installed / 'unins000.exe', 'uninstall-explicit-data-cleanup', '/REMOVEUSERDATA=1')
        if layout.exists() or (data / 'layout.json.bak').exists() or (data / 'cache').exists():
            raise ValueError('Explicit default data cleanup failed')
        if not unknown.is_file() or custom.read_bytes() != original or startup_value() is not None:
            raise ValueError('Explicit cleanup crossed its ownership boundary')
        success = True
    finally:
        if not success and (installed / 'unins000.exe').is_file():
            subprocess.run([str(installed / 'unins000.exe'), '/VERYSILENT',
                '/SUPPRESSMSGBOXES', '/NORESTART'], timeout=60)
        write_json(evidence / 'result.json', {'status': 'passed' if success else 'failed',
            'cases': outcomes, 'sourceIdentity': audit['identity'], 'fixtureSha256': digest(fixture),
            'candidateInstallerSha256': json.loads((workflow.output / 'installer.json').read_text())['sha256'],
            'serverLifecycleFixture': True, 'cleanWindows11Accepted': False,
            'limits': ['Fixture lowers only the OS version gate for hosted Server engineering tests.',
                       'The exact Windows 11 candidate still needs #36 clean-machine/manual acceptance.']})
    if not success:
        raise ValueError('Installer lifecycle incomplete')
    print('Hosted Windows Server fixture lifecycle passed; Windows 11 acceptance remains separate')


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print('Installer lifecycle error: ' + str(error), file=sys.stderr)
        sys.exit(1)
