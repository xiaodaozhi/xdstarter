"""Acquire/check XDStarter's declared tools and run reproducible CMake presets."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import platform
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = json.loads((ROOT / 'toolchain.json').read_text(encoding='utf-8'))
TOOLS = ROOT / MANIFEST['cache']['tools']
WINDOWS = os.name == 'nt'
BIN = TOOLS / ('Scripts' if WINDOWS else 'bin')
PYTHON = BIN / ('python.exe' if WINDOWS else 'python')


def run(command, **kwargs):
    print('+', subprocess.list2cmdline([str(part) for part in command]), flush=True)
    return subprocess.run([str(part) for part in command], check=True, cwd=ROOT, **kwargs)


def capture(command, **kwargs):
    return subprocess.check_output([str(part) for part in command], cwd=ROOT,
                                   text=True, encoding='utf-8', errors='replace', **kwargs).strip()


def require_python():
    expected = MANIFEST['python']['version']
    if platform.python_version() != expected:
        raise ValueError(f'The declared tool environment requires Python {expected}; '
                         f'current interpreter is {platform.python_version()}: {sys.executable}. '
                         'Pass the correct interpreter to build-windows.ps1 -Python.')


def tool(name):
    candidate = BIN / (name + ('.exe' if WINDOWS else ''))
    if not candidate.is_file():
        raise ValueError(f'Missing project tool {candidate}; run toolchain.py install first.')
    return candidate


def qt_prefix(argument):
    configured = argument or os.environ.get('XDSTARTER_QT_ROOT')
    if configured:
        return Path(configured).resolve()
    qt = MANIFEST['qt']
    return ROOT / MANIFEST['cache']['qt'] / qt['version'] / qt['directory']


def check_qt(prefix):
    qmake = prefix / 'bin' / ('qmake.exe' if WINDOWS else 'qmake')
    if not qmake.is_file():
        raise ValueError(f'Qt SDK not found at {prefix}; install it or pass --qt-prefix.')
    actual = capture([qmake, '-query', 'QT_VERSION'])
    if actual != MANIFEST['qt']['version']:
        raise ValueError(f'Expected Qt {MANIFEST["qt"]["version"]}, found {actual}.')
    if WINDOWS:
        configuration = (prefix / 'mkspecs/qconfig.pri').read_text(encoding='utf-8')
        if not re.search(r'^QT_ARCH\s*=\s*x86_64\s*$', configuration, re.MULTILINE):
            raise ValueError('The declared Windows Qt SDK must be x86_64.')
        if not (prefix / 'mkspecs/win32-msvc/qmake.conf').is_file():
            raise ValueError('The declared Windows Qt SDK must use the MSVC kit.')
    return actual


def install(args):
    require_python()
    if not PYTHON.is_file():
        run([sys.executable, '-m', 'venv', TOOLS])
    # Reject a stale venv before mutating its packages.
    actual_python = capture([PYTHON, '-c', 'import platform; print(platform.python_version())'])
    if actual_python != MANIFEST['python']['version']:
        raise ValueError(f'{TOOLS} uses Python {actual_python}; choose a new/clean local cache '
                         'before installing the declared baseline.')
    requirements = [f'{name}=={version}' for name, version in MANIFEST['packages'].items()]
    run([PYTHON, '-m', 'pip', 'install', '--disable-pip-version-check', *requirements])
    if args.core_only:
        return
    prefix = qt_prefix(args.qt_prefix)
    if not prefix.exists() and not (args.qt_prefix or os.environ.get('XDSTARTER_QT_ROOT')):
        if not WINDOWS:
            raise ValueError('Automatic Qt acquisition currently targets Windows; pass an existing Qt prefix.')
        qt = MANIFEST['qt']
        work = ROOT / 'build/qt-install'
        work.mkdir(parents=True, exist_ok=True)
        command = [PYTHON, '-m', 'aqt', 'install-qt', qt['host'], qt['target'],
                   qt['version'], qt['architecture'], '-O', ROOT / MANIFEST['cache']['qt']]
        print('+', subprocess.list2cmdline([str(part) for part in command]), flush=True)
        subprocess.run([str(part) for part in command], cwd=work, check=True)
    check_qt(prefix)


def check(args):
    if not PYTHON.is_file():
        raise ValueError('Project tool environment is missing; run toolchain.py install first.')
    actual_python = capture([PYTHON, '-c', 'import platform; print(platform.python_version())'])
    if actual_python != MANIFEST['python']['version']:
        raise ValueError(f'Expected project Python {MANIFEST["python"]["version"]}, found {actual_python}.')
    versions = json.loads(capture([PYTHON, '-c',
        'import importlib.metadata as m,json; print(json.dumps({n:m.version(n) for n in '
        + repr(tuple(MANIFEST['packages'])) + '}))']))
    if versions != MANIFEST['packages']:
        raise ValueError(f'Project tool versions differ from toolchain.json: {versions}')
    # The executable version is also checked: package metadata alone cannot prove PATH selection.
    cmake_version = capture([tool('cmake'), '--version']).splitlines()[0].split()[-1]
    ninja_version = capture([tool('ninja'), '--version'])
    if cmake_version != versions['cmake']:
        raise ValueError(f'CMake executable version mismatch: {cmake_version}')
    if not re.fullmatch(re.escape(versions['ninja']) + r'(?:\..*)?', ninja_version):
        raise ValueError(f'Ninja executable version mismatch: {ninja_version}')
    report = {'python': actual_python, 'packages': versions,
              'executables': {'cmake': cmake_version, 'ninja': ninja_version}}
    if not args.core_only:
        report['qt'] = check_qt(qt_prefix(args.qt_prefix))
    return report


def build_environment(prefix=None):
    environment = dict(os.environ)
    environment['PATH'] = str(BIN) + os.pathsep + environment.get('PATH', '')
    if prefix is not None:
        environment['PATH'] = str(prefix / 'bin') + os.pathsep + environment['PATH']
    if WINDOWS:
        # MSVC v142 can misdecode a non-ASCII user TEMP inherited through CMake.
        # Keep temporary compiler files in the project; never change system/user settings.
        temporary = ROOT / 'build/tmp'
        temporary.mkdir(parents=True, exist_ok=True)
        environment['TEMP'] = str(temporary)
        environment['TMP'] = str(temporary)
    return environment


def environment_value(environment, name):
    if WINDOWS:
        return next((value for key, value in environment.items() if key.casefold() == name.casefold()), None)
    return environment.get(name)


def build(args):
    report = check(args)
    prefix = qt_prefix(args.qt_prefix)
    environment = build_environment(None if args.core_only else prefix)
    configure = [tool('cmake'), '--preset', args.preset]
    if args.fresh:
        configure.append('--fresh')
    configure += ['-DCMAKE_MAKE_PROGRAM=' + tool('ninja').as_posix()]
    if not args.core_only:
        configure.append('-DCMAKE_PREFIX_PATH=' + prefix.as_posix())
        configure.append('-DXDSTARTER_ALLOW_ALTERNATE_MSVC=' + ('ON' if args.alternate_msvc else 'OFF'))
    run(configure, env=environment)
    if WINDOWS and not args.core_only:
        compiler_files = list((ROOT / 'build' / args.preset / 'CMakeFiles').glob('*/CMakeCXXCompiler.cmake'))
        if not compiler_files:
            raise ValueError('CMake did not record the selected compiler.')
        compiler = max(compiler_files, key=lambda path: path.stat().st_mtime).read_text(encoding='utf-8')
        match = re.search(r'set\(CMAKE_CXX_COMPILER_VERSION "([^"]+)"\)', compiler)
        if not match:
            raise ValueError('Cannot determine the actual CMake compiler version.')
        report['compilerVersion'] = match[1]
        linker = re.search(r'set\(CMAKE_CXX_COMPILER_LINKER_VERSION "([^"]+)"\)', compiler)
        if not linker:
            raise ValueError('Cannot determine the actual CMake linker version.')
        report['linkerVersion'] = linker[1]
        if not args.alternate_msvc and (match[1] != MANIFEST['windows']['compilerVersion'] or
                                      linker[1] != MANIFEST['windows']['linkerVersion']):
            raise ValueError('The selected compiler/linker differ from the declared Windows baseline.')
    run([tool('cmake'), '--build', '--preset', args.preset, '--parallel', args.parallel], env=environment)
    if not args.core_only:
        run([tool('cmake'), '--build', 'build/' + args.preset, '--target', 'xdstarter_qmllint'], env=environment)
    run([tool('ctest'), '--preset', args.preset], env=environment)
    report.update({'preset': args.preset, 'alternateMsvc': args.alternate_msvc,
                   'compilerToolset': environment_value(environment, 'VCToolsVersion'),
                   'windowsSdk': environment_value(environment, 'WindowsSDKVersion'),
                   'temporaryDirectory': environment.get('TEMP') if WINDOWS else None,
                   'qtPrefix': str(prefix) if not args.core_only else None})
    output = ROOT / 'build' / args.preset / 'toolchain-validation.json'
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(f'Build, applicable lint and CTest completed; evidence: {output}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('install', 'check', 'build'))
    parser.add_argument('--qt-prefix', help='Existing SDK cache; the project owns its version declaration.')
    parser.add_argument('--core-only', action='store_true', help='Skip Qt acquisition/discovery.')
    parser.add_argument('--preset', choices=('app-debug', 'app-release', 'core-debug', 'core-release'), default='app-release')
    parser.add_argument('--fresh', action='store_true')
    parser.add_argument('--alternate-msvc', action='store_true', help='Additional compatibility validation, not the declared Windows baseline.')
    parser.add_argument('--parallel', type=int, default=3)
    args = parser.parse_args()
    if args.command == 'build':
        args.core_only = args.preset.startswith('core-')
    if args.command == 'install':
        install(args)
    elif args.command == 'build':
        build(args)
    else:
        print(json.dumps(check(args), indent=2))


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f'Toolchain error: {error}', file=sys.stderr)
        sys.exit(1)
