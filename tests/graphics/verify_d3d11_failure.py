"""Run D3D11 fault cases in disposable app-local copies, never in system paths."""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from windows_package import d3d11_evidence, isolated_environment  # noqa: E402


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require_hosted_resident(environment):
    if (environment.get('GITHUB_ACTIONS') != 'true' or
            environment.get('RUNNER_ENVIRONMENT') != 'github-hosted'):
        raise ValueError('Resident verification is restricted to disposable hosted runners')


def verify(args):
    if args.hosted_resident:
        require_hosted_resident(os.environ)
    if os.name != 'nt':
        raise ValueError('D3D11 fault verification requires Windows')
    if bool(args.app) == bool(args.runtime):
        raise ValueError('Choose an executable or a packaged runtime')
    if bool(args.qt_bin) != bool(args.app):
        raise ValueError('An SDK bin path is required only for a build-tree executable')
    proxy = args.proxy.resolve(strict=True)
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    logs = output.parent
    source = (args.runtime / 'xdstarter.exe' if args.runtime else args.app).resolve(strict=True)
    # Error mode affects only this verifier and its children; no global Windows
    # settings or host graphics drivers are modified.
    ctypes.windll.kernel32.SetErrorMode(0x0001 | 0x0002 | 0x8000)
    results = {}
    with tempfile.TemporaryDirectory(prefix='d3d11-fault-', dir=output.parent) as temp:
        owned = Path(temp).resolve()
        if not owned.is_relative_to(output.parent):
            raise ValueError('Temporary fixture escaped its output directory')
        runtime = owned / 'runtime'
        if args.runtime:
            shutil.copytree(args.runtime, runtime)
        else:
            runtime.mkdir()
            shutil.copyfile(source, runtime / 'xdstarter.exe')
        if any(path.name.casefold() in ('opengl32sw.dll', 'd3d11.dll')
               for path in runtime.rglob('*') if path.is_file()):
            raise ValueError('Test runtime contains software OpenGL or an app-local D3D11 DLL')
        temporary = owned / 'tmp'
        local = owned / 'local'
        roaming = owned / 'roaming'
        for directory in (temporary, local, roaming):
            directory.mkdir()
        env = isolated_environment(runtime)
        env.pop('QSG_RHI_BACKEND', None)
        env.update(QSG_INFO='1', TEMP=str(temporary), TMP=str(temporary),
                   LOCALAPPDATA=str(local), APPDATA=str(roaming))
        system = Path(next(value for key, value in env.items() if key.casefold() == 'systemroot'))
        if any((directory / 'opengl32sw.dll').exists()
               for directory in (system / 'System32', system / 'System', system, owned)):
            raise ValueError('Verification requires no software OpenGL DLL in system search paths')
        if args.qt_bin:
            qt_bin = args.qt_bin.resolve(strict=True)
            qt = qt_bin.parent
            env.update(PATH=os.pathsep.join((str(qt_bin), env['PATH'])),
                       QT_PLUGIN_PATH=str(qt / 'plugins'),
                       QT_QPA_PLATFORM_PLUGIN_PATH=str(qt / 'plugins/platforms'),
                       QML_IMPORT_PATH=str(qt / 'qml'), QML2_IMPORT_PATH=str(qt / 'qml'))
        assert 'QSG_RHI_PREFER_SOFTWARE_RENDERER' not in env
        assert 'QSG_RHI_BACKEND' not in env and 'QT_QUICK_BACKEND' not in env

        def run(name, mode=None):
            child_env = dict(env)
            child_env.pop('XDSTARTER_D3D11_FAULT', None)
            if mode:
                child_env['XDSTARTER_D3D11_FAULT'] = mode
            started = time.monotonic()
            try:
                process = subprocess.run([str(runtime / 'xdstarter.exe'), '--smoke-test'],
                    cwd=owned, env=child_env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                    text=True, encoding='utf-8', errors='replace', timeout=30)
            except subprocess.TimeoutExpired as error:
                partial = error.output or b''
                if isinstance(partial, str):
                    partial = partial.encode('utf-8', errors='replace')
                (logs / (output.stem + '-' + name + '.log')).write_bytes(partial)
                raise ValueError(name + ' did not exit within 30 seconds') from error
            log = logs / (output.stem + '-' + name + '.log')
            log.write_text(process.stdout, encoding='utf-8')
            results[name] = {'exitCode': process.returncode,
                             'elapsedSeconds': round(time.monotonic() - started, 3),
                             'log': log.relative_to(output.parent).as_posix()}
            print(name, 'exit', process.returncode, 'seconds', results[name]['elapsedSeconds'], flush=True)
            if 'Creating QRhi with backend D3D11' not in process.stdout or \
                    'Prefer software device: 0' not in process.stdout:
                raise ValueError(name + ' did not use default D3D11 preferences')
            return process.returncode, process.stdout

        code, normal = run('normal')
        if code or '[d3d11-test-proxy]' in normal:
            raise ValueError('Unmodified baseline did not start normally')
        initial = d3d11_evidence(normal)['selectedAdapters']
        results['normal']['selectedAdapters'] = initial
        shutil.copyfile(proxy, runtime / 'd3d11.dll')
        code, control = run('proxy-control', 'none')
        expected_system = ('real-module=' + str(system / 'System32/d3d11.dll')).casefold()
        if (code or '[d3d11-test-proxy] mode=none' not in control or
                'action=injected-failure' in control or expected_system not in control.casefold() or
                'system-result=0x00000000' not in control or
                d3d11_evidence(control)['selectedAdapters'] != initial):
            raise ValueError('Transparent proxy control failed')
        code, fallback = run('hardware-failure', 'hardware')
        if code:
            raise ValueError('Hardware failure did not recover')
        if 'action=injected-failure' in fallback:
            selected = d3d11_evidence(fallback)['selectedAdapters']
            marker = 'Failed to create a D3D device with default settings'
            if (marker not in fallback or 'action=forward' not in fallback or
                    not (fallback.index('action=injected-failure') < fallback.index(marker) <
                         fallback.index('action=forward') < fallback.index('Created QRhi')) or
                    expected_system not in fallback.casefold() or
                    'system-result=0x00000000' not in fallback or not selected or
                    selected[-1]['vendor'] != 0x1414 or selected[-1]['device'] != 0x8c or
                    not selected[-1]['flags'] & 2):
                raise ValueError('Automatic WARP retry was not observed')
            results['hardware-failure']['selectedAdapters'] = selected
        else:
            # A host whose default adapter is already software has no hardware
            # attempt to fail. Never report that as hardware-fallback evidence.
            selected = d3d11_evidence(fallback)['selectedAdapters']
            if not selected or not selected[0]['flags'] & 2:
                raise ValueError('Hardware injection did not reach the selected adapter')
            results['hardware-failure']['skipped'] = 'Default adapter is already software'
        for name, mode in (('all-failure', 'all'), ('delayed-all-failure', 'delayed-all')):
            code, failure = run(name, mode)
            if (not code or failure.count('action=injected-failure') < 2 or
                    'Failed to create a D3D device with default settings' not in failure or
                    'Graphics initialization failed:' not in failure or 'Created QRhi' in failure):
                raise ValueError(name + ' did not produce a bounded handled graphics failure')

        if args.hosted_resident:
            # Normal mode may enable an input hook. It is never run on the local
            # workstation by this verifier; the hosted runner is disposable.
            resident_env = {**env, 'XDSTARTER_D3D11_FAULT': 'all'}
            diagnostics = owned / 'resident-diagnostics.json'
            layout = owned / 'resident-layout.json'
            resident_log = logs / (output.stem + '-resident.log')
            activation_log = logs / (output.stem + '-resident-activation.log')
            started = time.monotonic()
            with resident_log.open('wb') as stream:
                resident = subprocess.Popen([str(runtime / 'xdstarter.exe'), '--layout-file', str(layout),
                    '--start-capture-diagnostics', str(diagnostics), '--start-capture-duration', '60'],
                    cwd=owned, env=resident_env,
                    stdout=stream, stderr=subprocess.STDOUT)
                try:
                    deadline = time.monotonic() + 30
                    ready = False
                    initial = None
                    while resident.poll() is None and time.monotonic() < deadline:
                        try:
                            snapshot = json.loads(diagnostics.read_text(encoding='utf-8'))
                            ready = snapshot.get('processId') == resident.pid
                            if ready:
                                initial = snapshot
                        except (OSError, ValueError):
                            pass
                        if ready:
                            break
                        time.sleep(0.1)
                    if resident.poll() is None:
                        if not ready:
                            raise ValueError('Resident process did not publish its readiness snapshot')
                        activation = subprocess.run([str(runtime / 'xdstarter.exe'), '--layout-file', str(layout),
                            '--start-capture-diagnostics', str(owned / 'activation-diagnostics.json'),
                            '--start-capture-duration', '25'], cwd=owned, env=resident_env,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=20)
                        activation_log.write_bytes(activation.stdout)
                        if activation.returncode:
                            raise ValueError('Resident activation failed')
                    try:
                        resident.wait(timeout=30)
                    except subprocess.TimeoutExpired as error:
                        raise ValueError('Resident graphics failure did not exit within 30 seconds') from error
                finally:
                    if resident.poll() is None:
                        resident.kill()
                        resident.wait(timeout=10)
            output_text = resident_log.read_text(encoding='utf-8', errors='replace')
            if (resident.returncode == 0 or 'Graphics initialization failed:' not in output_text or
                    output_text.count('action=injected-failure') < 2):
                raise ValueError('Resident process did not handle the injected graphics failure')
            final = json.loads(diagnostics.read_text(encoding='utf-8'))
            if (final.get('processId') != resident.pid or final.get('requested') is not False or
                    final.get('sessionActive') is not False or final.get('hookInstalled') is not False):
                raise ValueError('Resident process did not publish stopped input capture')
            saved_diagnostics = logs / (output.stem + '-resident-diagnostics.json')
            shutil.copyfile(diagnostics, saved_diagnostics)
            results['hosted-resident'] = {'exitCode': resident.returncode,
                'elapsedSeconds': round(time.monotonic() - started, 3),
                'log': resident_log.relative_to(output.parent).as_posix(),
                'diagnostics': saved_diagnostics.relative_to(output.parent).as_posix(),
                'captureActiveBeforeFailure': initial.get('sessionActive') if initial else None,
                'hookInstalledBeforeFailure': initial.get('hookInstalled') if initial else None,
                'captureRequestedAfterShutdown': False, 'captureActiveAfterShutdown': False,
                'hookInstalledAfterShutdown': False}
            print('hosted-resident exit', resident.returncode,
                  'seconds', results['hosted-resident']['elapsedSeconds'], flush=True)

    result = {'status': 'passed', 'executableSha256': digest(source),
              'testProxySha256': digest(proxy), 'sdkSearchPaths': bool(args.qt_bin),
              'softwareOpenGLAbsent': True, 'forcedBackendOrWarp': False,
              'results': results,
              'limits': ['Simulated D3D11CreateDevice failure; no real driver or running-device loss.',
                         'The proxy exists only in a disposable application copy.']}
    output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument('--app', type=Path)
    source.add_argument('--runtime', type=Path)
    parser.add_argument('--qt-bin', type=Path)
    parser.add_argument('--proxy', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--hosted-resident', action='store_true',
                        help='Exercise resident mode only on a disposable hosted runner')
    args = parser.parse_args()
    try:
        verify(args)
    except (OSError, ValueError) as error:
        print('D3D11 fault verification failed:', error, file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
