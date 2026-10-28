"""Windows candidate deployment, audit and packaging shared by local builds and CI."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tarfile
import time
import urllib.request
import uuid
import zipfile

ROOT = Path(__file__).resolve().parents[1]
CONFIG = ROOT / 'packaging/windows'
STYLES = ('FluentWinUI3', 'Fusion', 'Imagine', 'Material', 'Universal', 'Windows')
REMOVED_DIRS = tuple('qml/QtQuick/Controls/' + name for name in STYLES) + (
    'qml/QtQuick/NativeStyle', 'qml/QtQuick/Effects', 'qmltooling')
REMOVED_DLLS = tuple('Qt6QuickControls2' + name + '.dll' for name in (
    'FluentWinUI3StyleImpl', 'Fusion', 'FusionStyleImpl', 'Imagine', 'ImagineStyleImpl',
    'Material', 'MaterialStyleImpl', 'Universal', 'UniversalStyleImpl', 'WindowsStyleImpl')) + (
    'Qt6QuickEffects.dll', 'vc_redist.x64.exe')
GRAPHICS = ('opengl32sw.dll', 'd3dcompiler_47.dll', 'dxcompiler.dll', 'dxil.dll')
SYSTEM_DLLS = set(('kernel32 kernelbase user32 gdi32 gdi32full ole32 oleaut32 advapi32 '
    'ws2_32 shell32 shlwapi comdlg32 comctl32 uxtheme dwmapi dxgi d3d11 d3d12 d3d9 '
    'opengl32 glu32 bcrypt bcryptprimitives crypt32 cryptbase ncrypt netapi32 version '
    'winmm imm32 msvcrt ucrtbase userenv ntdll winhttp wininet wtsapi32 psapi iphlpapi '
    'normaliz rpcrt4 secur32 authz powrprof d2d1 dwrite dhcpcsvc dnsapi msimg32 usp10 '
    'rstrtmgr propsys windowscodecs sechost mswsock pdh wldap32 avrt dbghelp imagehlp '
    'setupapi dxcore cfgmgr32 wintrust winsta hid cabinet dcomp shcore win32u '
    'uiautomationcore oleacc mpr msctf').split())
REQUIRED = ('xdstarter.exe', 'platforms/qwindows.dll',
    'qml/QtQuick/Controls/Basic/qmldir', 'qml/QtQuick/Dialogs/qmldir',
    'Qt6QuickDialogs2.dll', 'Qt6QuickDialogs2QuickImpl.dll', 'Qt6Network.dll',
    'translations/qt_zh_CN.qm', 'qt.conf', *GRAPHICS)


def config(name):
    return json.loads((CONFIG / name).read_text(encoding='utf-8'))


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def file_version(path):
    import ctypes
    from ctypes import wintypes
    api = ctypes.WinDLL('version', use_last_error=True)
    api.GetFileVersionInfoSizeW.argtypes = [wintypes.LPCWSTR, ctypes.POINTER(wintypes.DWORD)]
    api.GetFileVersionInfoSizeW.restype = wintypes.DWORD
    api.GetFileVersionInfoW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p]
    api.VerQueryValueW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR,
                                  ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(wintypes.UINT)]
    unused = wintypes.DWORD()
    size = api.GetFileVersionInfoSizeW(str(path), ctypes.byref(unused))
    if not size:
        raise ValueError('Native version resource missing: ' + path.name)
    data = ctypes.create_string_buffer(size)
    if not api.GetFileVersionInfoW(str(path), 0, size, data):
        raise ValueError('Cannot read native version resource')
    pointer, length = ctypes.c_void_p(), wintypes.UINT()
    if not api.VerQueryValueW(data, '\\', ctypes.byref(pointer), ctypes.byref(length)) or length.value < 52:
        raise ValueError('Native fixed version resource missing')
    fields = ctypes.cast(pointer, ctypes.POINTER(wintypes.DWORD * 13)).contents
    if fields[0] != 0xFEEF04BD:
        raise ValueError('Invalid native fixed version signature')
    return '.'.join(map(str, (fields[2] >> 16, fields[2] & 65535,
                              fields[3] >> 16, fields[3] & 65535)))


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def command(*args, env=None, cwd=ROOT, timeout=180):
    return subprocess.check_output([str(arg) for arg in args], cwd=cwd, env=env,
        text=True, encoding='utf-8', errors='replace', timeout=timeout).strip()


def generated(path):
    """All mutable package locations are owned children of this checkout's build/."""
    path = Path(path).absolute()
    build = (ROOT / 'build').resolve()
    if path.resolve() == build or not path.resolve().is_relative_to(build):
        raise ValueError('Package output/cache must be a dedicated child of build/')
    for parent in (path, *path.parents):
        if parent == ROOT:
            break
        if parent.is_symlink() or (hasattr(parent, 'is_junction') and parent.is_junction()):
            raise ValueError('Package paths must not traverse links or junctions')
    return path


def relative_name(value):
    value = value.replace('\\', '/')
    parts = value.split('/')
    devices = {'con', 'prn', 'aux', 'nul', 'clock$', *(f'com{i}' for i in range(1, 10)),
               *(f'lpt{i}' for i in range(1, 10))}
    if (len(value) > 1024 or not parts or any(not part or part in ('.', '..') or
            part[-1] in '. ' or part.split('.')[0].casefold() in devices for part in parts) or
            any(ord(char) < 32 or char in ':*?"<>|' for char in value)):
        raise ValueError('Unsafe package-relative path')
    return '/'.join(parts)


def files(root):
    """Never follow reparse points during scans or later cleanup."""
    root = Path(root)
    result, seen = [], set()
    for base, directories, names in os.walk(root, followlinks=False):
        for name in [*directories, *names]:
            path = Path(base) / name
            if path.is_symlink() or (hasattr(path, 'is_junction') and path.is_junction()):
                raise ValueError('Package tree contains a link or junction')
            rel = relative_name(path.relative_to(root).as_posix())
            if rel.casefold() in seen:
                raise ValueError('Case-colliding package paths')
            seen.add(rel.casefold())
        result.extend(Path(base) / name for name in names)
    return sorted(result, key=lambda path: path.relative_to(root).as_posix().casefold())


def inventory(root):
    return [{'path': path.relative_to(root).as_posix(), 'bytes': path.stat().st_size,
             'sha256': digest(path)} for path in files(root)]


def verify_inventory(root, entries):
    expected = {}
    for item in entries:
        rel = relative_name(item['path'])
        if rel.casefold() in expected or not re.fullmatch('[0-9a-f]{64}', item['sha256']):
            raise ValueError('Invalid or duplicated payload inventory')
        expected[rel.casefold()] = (rel, item['sha256'], item['bytes'])
    actual = {item['path'].casefold(): (item['path'], item['sha256'], item['bytes'])
              for item in inventory(root)}
    if actual != expected:
        raise ValueError('Payload changed after audit (missing, extra or modified files)')


def source_identity():
    revision = command('git', 'rev-parse', 'HEAD')
    if not re.fullmatch('[0-9a-f]{40}', revision):
        raise ValueError('A Git checkout with a full commit identity is required')
    dirty = bool(command('git', 'status', '--porcelain', '--untracked-files=normal'))
    names = subprocess.check_output(['git', 'ls-files', '-z', '--cached', '--others',
                                    '--exclude-standard'], cwd=ROOT).split(b'\0')
    fingerprint = hashlib.sha256(revision.encode())
    for raw in sorted(set(names)):
        if raw:
            path = ROOT / raw.decode('utf-8')
            fingerprint.update(raw + b'\0')
            fingerprint.update(digest(path).encode() if path.is_file() else b'deleted')
    return {'revision': revision, 'dirty': dirty, 'fingerprint': fingerprint.hexdigest()}


def fetch(record, destination):
    destination = Path(destination)
    if destination.exists():
        if digest(destination) != record['sha256']:
            raise ValueError('Cached supplier input hash mismatch: ' + destination.name)
        return destination
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_name(destination.name + '.partial-' + uuid.uuid4().hex)
    for attempt in range(3):
        try:
            request = urllib.request.Request(record['url'], headers={'User-Agent': 'XDStarter-build'})
            with urllib.request.urlopen(request, timeout=45) as response, temporary.open('wb') as out:
                shutil.copyfileobj(response, out)
            if digest(temporary) != record['sha256']:
                raise ValueError('Supplier input hash mismatch: ' + destination.name)
            temporary.replace(destination)
            return destination
        except (OSError, TimeoutError):
            temporary.unlink(missing_ok=True)
            if attempt == 2:
                raise
            time.sleep(1)
        except Exception:
            temporary.unlink(missing_ok=True)
            raise


def prepare(cache):
    if os.name != 'nt':
        raise ValueError('Windows packaging tools require Windows')
    cache = generated(cache)
    files(cache)
    runtime = config('runtime.json')
    bundle = fetch(runtime, cache / 'vc_redist.x64.exe')
    wix_archive = fetch(runtime['wix'], cache / 'wix.zip')
    wix = cache / 'wix'
    with zipfile.ZipFile(wix_archive) as archive:
        for member in archive.infolist():
            relative_name(member.filename.rstrip('/'))
            if (member.external_attr >> 16) & 0o170000 == 0o120000:
                raise ValueError('Build tool archive contains a link')
        archive.extractall(wix)
    extracted = cache / 'extracted'
    command(wix / 'dark.exe', '-nologo', '-x', extracted, bundle)
    raw = cache / 'raw'
    raw.mkdir(exist_ok=True)
    command('expand.exe', '-F:*', extracted / 'AttachedContainer/packages/vcRuntimeMinimum_amd64/cab1.cab', raw)
    target = cache / 'runtime'
    target.mkdir(exist_ok=True)
    inputs = {name: raw / (name + '_amd64') for name in runtime['files']}
    inputs['Microsoft-VC-runtime-license.rtf'] = extracted / 'UX/license.rtf'
    expected = {**runtime['files'], 'Microsoft-VC-runtime-license.rtf': runtime['license_sha256']}
    for name, path in inputs.items():
        if digest(path) != expected[name]:
            raise ValueError('Extracted Microsoft material hash mismatch')
    for name, path in inputs.items():
        shutil.copyfile(path, target / name)
    installer = config('installer-toolchain.json')['inno_setup']
    setup = fetch(installer, cache / 'compiler-setup.exe')
    compiler = cache / 'compiler'
    if not (compiler / 'ISCC.exe').is_file():
        # This installs the build compiler, never XDStarter. It is an explicit prepare stage.
        subprocess.run([str(setup), '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART',
            '/CURRENTUSER', '/NOICONS', '/DIR=' + str(compiler)], check=True, timeout=180)
    # Inno's official compiler PE resource is 0.0.0.0; the engine reports its
    # real version when compiling. Output=no makes this a non-installing probe.
    probe = subprocess.run([str(compiler / 'ISCC.exe'), '-'], input=(
        '[Setup]\nAppName=XDStarter compiler probe\nAppVersion=0.0.0\n'
        'DefaultDirName={tmp}\\XDStarter-probe\nOutput=no\nUninstallable=no\n'
        'PrivilegesRequired=lowest\n'), text=True, capture_output=True,
        encoding='utf-8', errors='replace', timeout=30)
    if probe.returncode or ('Compiler engine version: Inno Setup ' + installer['version']) not in probe.stdout:
        raise ValueError('Installer compiler version mismatch')
    for material in installer['materials']:
        fetch(material, cache / material['file'])
    for material in config('qt-license-sources.json')['licenses']:
        fetch(material, cache / 'licenses' / (material['id'] + '.txt'))
    for material in config('sources.json')['archives']:
        fetch(material, cache / 'sources' / material['file'])
    write_json(cache / 'prepared.json', {'status': 'passed', 'runtime': runtime['version'],
        'installer': installer['version'], 'runtimeConfigSha256': digest(CONFIG / 'runtime.json'),
        'installerConfigSha256': digest(CONFIG / 'installer-toolchain.json')})


def prune(root):
    entries = files(root)  # Complete reparse-point check before the first deletion.
    remove = []
    for path in entries:
        rel = path.relative_to(root).as_posix()
        if (rel.endswith('.qmltypes') or rel in REMOVED_DLLS or
                any(rel.startswith(directory + '/') for directory in REMOVED_DIRS)):
            remove.append({'path': rel, 'bytes': path.stat().st_size})
    for entry in remove:
        (root / entry['path']).unlink()
    for directory in sorted((p for p in root.rglob('*') if p.is_dir()),
                            key=lambda path: len(path.parts), reverse=True):
        if not any(directory.iterdir()):
            directory.rmdir()
    return remove


def copy_crt(cache, root):
    runtime = config('runtime.json')
    inputs = {**runtime['files'], 'Microsoft-VC-runtime-license.rtf': runtime['license_sha256']}
    for name, expected in inputs.items():
        if digest(cache / 'runtime' / name) != expected:
            raise ValueError('Prepared CRT hash mismatch')
    notice = root / 'share/notices/Microsoft-VC-runtime'
    notice.mkdir(parents=True)
    for name in runtime['files']:
        shutil.copyfile(cache / 'runtime' / name, root / name)
    shutil.copyfile(cache / 'runtime/Microsoft-VC-runtime-license.rtf', notice / 'license.rtf')
    write_json(notice / 'provenance.json', runtime)


def copy_graphics(qt, root):
    sdk = Path(os.environ.get('WindowsSdkDir', ''))
    version = os.environ.get('WindowsSDKVersion', '').strip('\\/')
    if not sdk.is_absolute() or not re.fullmatch(r'\d+\.\d+\.\d+\.\d+', version):
        raise ValueError('Explicit Windows SDK developer environment required')
    license_file = sdk / 'Licenses' / version / 'sdk_license.rtf'
    if not license_file.is_file():
        license_file = sdk / 'license.rtf'
    if not license_file.is_file():
        raise ValueError('Selected SDK license is missing')
    inputs = {name: sdk / 'Redist/D3D/x64' / name for name in GRAPHICS[1:]}
    inputs['opengl32sw.dll'] = qt / 'bin/opengl32sw.dll'
    if digest(inputs['opengl32sw.dll']) != config('sources.json')['mesaBinary']['sha256']:
        raise ValueError('Unreviewed Qt Mesa binary')
    result = []
    for name, path in inputs.items():
        if path.is_symlink() or not path.resolve().is_relative_to(qt if name == 'opengl32sw.dll' else sdk):
            raise ValueError('Graphics supplier input escapes its SDK')
        shutil.copyfile(path, root / name)
        result.append({'path': name, 'sha256': digest(path),
            'supplier': 'Qt Mesa' if name == 'opengl32sw.dll' else 'Windows SDK REDIST',
            'sourceRelative': path.relative_to(qt if name == 'opengl32sw.dll' else sdk).as_posix()})
    notice = root / 'share/notices/Windows-SDK-graphics'
    notice.mkdir(parents=True)
    shutil.copyfile(license_file, notice / 'sdk_license.rtf')
    other = license_file.with_name('sdk_third_party_notices.rtf')
    if other.is_file():
        shutil.copyfile(other, notice / other.name)
    write_json(notice / 'provenance.json', {'sdkVersion': version, 'files': result,
        'scope': 'Supplier provenance, not automatic redistribution eligibility approval.'})


def collect_notices(cache, qt, root):
    notice = root / 'share/notices'
    qt_notice = notice / 'Qt'
    qt_notice.mkdir(parents=True)
    for record in config('qt-license-sources.json')['licenses']:
        source = cache / 'licenses' / (record['id'] + '.txt')
        if digest(source) != record['sha256']:
            raise ValueError('Qt license text hash mismatch')
        shutil.copyfile(source, qt_notice / source.name)
    for module in ('qtbase', 'qtdeclarative', 'qtsvg', 'qttranslations'):
        source = qt / 'sbom' / (module + '-6.8.2.spdx.json')
        document = json.loads(source.read_text(encoding='utf-8'))
        if document.get('spdxVersion') not in ('SPDX-2.2', 'SPDX-2.3'):
            raise ValueError('Unsupported Qt SDK SPDX document')
        shutil.copyfile(source, qt_notice / source.name)
    shutil.copytree(ROOT / 'third_party/unicode', notice / 'Unicode',
                    ignore=shutil.ignore_patterns('*.tsv', '*.inc', '*.zip'))
    native = notice / 'Mesa-LLVM'
    native.mkdir()
    for record in config('sources.json')['archives']:
        archive_path = cache / 'sources' / record['file']
        if digest(archive_path) != record['sha256']:
            raise ValueError('Corresponding source archive hash mismatch')
        if 'notices' not in record:
            continue
        with tarfile.open(archive_path, 'r:*') as archive:
            for wanted in record['notices']:
                (native / (record['component'] + '-' + wanted.replace('/', '_'))).write_bytes(
                    source_notice(archive, wanted))
    installer = config('installer-toolchain.json')['inno_setup']
    for record in installer['materials']:
        source = cache / record['file']
        if digest(source) != record['sha256']:
            raise ValueError('Installer material hash mismatch')
        if record['file'] != 'ChineseSimplified.isl':
            shutil.copyfile(source, notice / source.name)
    (notice / 'Installer-CREDITS.txt').write_text(
        'Inno Setup uses RemObjects Pascal Script (https://www.remobjects.com/ps).\n', encoding='utf-8')
    for name in ('Windows-runtime-NOTICE.txt', 'Qt-replacement.txt'):
        (notice / name).write_bytes(b'\xef\xbb\xbf' + (CONFIG / name).read_bytes())
    write_json(notice / 'sources.json', config('sources.json'))


def source_notice(archive, wanted):
    # Match within the archive's single source root. A basename suffix would
    # confuse LLVM's root license with its separate ARM/support licenses.
    matches = [member for member in archive.getmembers() if member.isfile() and
               member.name.removeprefix('./').partition('/')[2] == wanted]
    if len(matches) != 1 or matches[0].size > 2 * 1024 * 1024:
        raise ValueError('Missing or ambiguous native source notice: ' + wanted)
    with archive.extractfile(matches[0]) as stream:
        return stream.read()


def binary_info(path):
    data = path.read_bytes()
    if len(data) < 64 or data[:2] != b'MZ':
        raise ValueError('Not a PE binary: ' + path.name)
    offset = struct.unpack_from('<I', data, 60)[0]
    if offset + 96 > len(data) or data[offset:offset + 4] != b'PE\0\0':
        raise ValueError('Invalid PE headers: ' + path.name)
    machine = struct.unpack_from('<H', data, offset + 4)[0]
    magic = struct.unpack_from('<H', data, offset + 24)[0]
    if machine != 0x8664 or magic != 0x20b:
        raise ValueError('Package contains a non-x64 binary: ' + path.name)
    return {'machine': 'x64', 'subsystem': struct.unpack_from('<H', data, offset + 92)[0]}


def imports(path):
    output = command('dumpbin.exe', '/nologo', '/dependents', path)
    # dumpbin prints normal and delay-load sections; both use the same DLL lines.
    return sorted(set(name.casefold() for name in re.findall(
        r'^\s+([a-zA-Z0-9_+.-]+\.dll)\s*$', output, re.MULTILINE | re.IGNORECASE)))


def audit(root, identity, import_reader=imports, pe_reader=binary_info):
    entries = inventory(root)
    names = {}
    for entry in entries:
        names.setdefault(Path(entry['path']).name.casefold(), []).append(entry['path'])
    missing = [name for name in (*REQUIRED, *config('runtime.json')['files'])
               if not (root / name).is_file()]
    if missing:
        raise ValueError('Required runtime files missing: ' + ', '.join(missing))
    for entry in entries:
        rel = entry['path']
        if (rel.endswith('.qmltypes') or rel in REMOVED_DLLS or
                any(rel.startswith(directory + '/') for directory in REMOVED_DIRS) or
                Path(rel).name.casefold().startswith(('qt6test', 'qoffscreen', 'xdstarter_ui_tests')) or
                (rel.lower().endswith('.exe') and rel != 'xdstarter.exe')):
            raise ValueError('Unexpected development/unused runtime file: ' + rel)
    binaries = []
    for entry in entries:
        if Path(entry['path']).suffix.lower() not in ('.dll', '.exe'):
            continue
        path = root / entry['path']
        metadata = pe_reader(path)
        dependencies = []
        for name in import_reader(path):
            candidates = names.get(name, [])
            if len(candidates) == 1:
                dependencies.append({'name': name, 'package': candidates[0]})
            elif len(candidates) > 1:
                raise ValueError('Ambiguous DLL dependency: ' + name)
            elif name.startswith(('api-ms-win-', 'ext-ms-win-')) or name.removesuffix('.dll') in SYSTEM_DLLS:
                dependencies.append({'name': name, 'system': True})
            else:
                raise ValueError('Unresolved/non-system dependency: ' + name + ' from ' + entry['path'])
        binaries.append({'path': entry['path'], **metadata, 'imports': dependencies})
    if next(item for item in binaries if item['path'] == 'xdstarter.exe')['subsystem'] != 2:
        raise ValueError('Product executable must use the Windows GUI subsystem')
    for name, expected in config('runtime.json')['files'].items():
        if digest(root / name) != expected:
            raise ValueError('Deployed CRT differs from locked supplier input')
    return {'schemaVersion': 1, 'status': 'passed', 'platform': 'windows', 'mode': 'deployed',
            'identity': identity, 'files': entries, 'binaries': binaries,
            'bytes': sum(entry['bytes'] for entry in entries),
            'limits': ['Native import closure does not establish dynamic plugin or clean-machine coverage.']}


def isolated_environment(root, *, software=False):
    environment = dict(os.environ)
    system = Path(environment['SystemRoot'])
    for key in list(environment):
        if key.upper().startswith(('QT_', 'QML', 'QSG_')) or key.upper() in ('QTDIR', 'QT_PLUGIN_PATH'):
            del environment[key]
    environment.update(PATH=os.pathsep.join(map(str, (system / 'System32', system))),
        QT_PLUGIN_PATH=str(root), QT_QPA_PLATFORM_PLUGIN_PATH=str(root / 'platforms'),
        QML_IMPORT_PATH=str(root / 'qml'), QML2_IMPORT_PATH=str(root / 'qml'),
        QT_QPA_PLATFORM='offscreen' if software else 'windows',
        QT_FORCE_STDERR_LOGGING='1')
    if software:
        environment['QT_QUICK_BACKEND'] = 'software'
    else:
        environment['QSG_RHI_BACKEND'] = 'd3d11'
    return environment


def payload_manifest(entries):
    lines, seen = ['xdstarter-install-files-v1'], set()
    for entry in sorted(entries, key=lambda item: item['path'].casefold()):
        name = relative_name(entry['path'])
        if (name.casefold() in seen or name.split('/')[0].casefold().startswith('unins') or
                name.casefold() == 'install-manifest.txt' or
                not re.fullmatch('[0-9a-f]{64}', entry['sha256'])):
            raise ValueError('Invalid installer manifest entry')
        seen.add(name.casefold())
        lines.append(entry['sha256'] + '\t' + name.replace('/', '\\'))
    text = '\n'.join(lines) + '\n'
    if len(lines) > 16385 or len(text.encode('utf-8')) > 2097152:
        raise ValueError('Installer manifest exceeds supported bounds')
    return text


def attribution(qt, root):
    declarations = {}
    for module in ('qtbase', 'qtdeclarative', 'qtsvg', 'qttranslations'):
        doc = json.loads((qt / 'sbom' / (module + '-6.8.2.spdx.json')).read_text(encoding='utf-8'))
        for item in doc.get('files', []):
            name = item.get('fileName', '').removeprefix('./')
            declarations.setdefault(Path(name).name.casefold(), []).append((module, name, item))
    records, gaps = [], []
    for path in files(root):
        rel = path.relative_to(root).as_posix()
        candidates = declarations.get(path.name.casefold(), [])
        exact = []
        for module, name, declaration in candidates:
            supplier = qt / name
            if supplier.is_file() and digest(supplier) == digest(path):
                exact.append({'module': module, 'sdkRelative': name,
                    'sdkSha256': digest(supplier), 'spdxId': declaration['SPDXID'],
                    'licenseExpression': declaration.get('licenseConcluded', 'NOASSERTION')})
        if exact:
            records.append({'path': rel, 'status': 'sdk-bytes-matched', 'declarations': exact})
        elif candidates or rel.startswith(('qml/', 'translations/')):
            gaps.append({'path': rel, 'status': 'sdk-file-or-repository-scope-unconfirmed'})
    return {'schemaVersion': 1, 'completeness': 'incomplete', 'files': records, 'gaps': gaps,
        'limits': ['SDK declarations may omit QML resources or embedded components.',
                   'Original sources do not establish complete SDK build correspondence.',
                   'Project license decision and public distribution review remain #3/#36.']}


class Workflow:
    def __init__(self, args):
        self.args = args
        self.qt = Path(args.qt_prefix).resolve() if args.qt_prefix else (
            ROOT / 'build/Qt' / config('qt-license-sources.json')['qt_version'] / 'msvc2022_64')
        self.output = generated(args.output_dir)
        self.cache = generated(args.package_cache)
        self.root = self.output / 'runtime'
        self.build = ROOT / 'build' / args.preset

    def preflight(self):
        if os.name != 'nt' or self.args.preset != 'app-release':
            raise ValueError('Windows packaging requires the app-release preset')
        if not shutil.which('dumpbin.exe'):
            raise ValueError('Run packaging from the selected MSVC developer environment')
        receipt = json.loads((self.build / 'toolchain-validation.json').read_text(encoding='utf-8'))
        if receipt.get('source') != source_identity():
            raise ValueError('Source changed or build receipt is stale; build and test again')
        env = dict(os.environ)
        env['PATH'] = str(self.qt / 'bin') + os.pathsep + env.get('PATH', '')
        identity = json.loads(command(self.build / 'bin/xdstarter.exe', '--build-info', env=env))
        if (identity != receipt.get('identity') or
                digest(self.build / 'bin/xdstarter.exe') != receipt.get('executableSha256')):
            raise ValueError('Built executable differs from its successful build receipt')
        if identity['qt'] != config('qt-license-sources.json')['qt_version']:
            raise ValueError('Executable and deployment Qt versions differ')
        if file_version(self.build / 'bin/xdstarter.exe') != identity['version'] + '.' + str(identity['build']):
            raise ValueError('Windows VERSIONINFO differs from the executable identity')
        if not self.args.development and (identity['dirty'] or not identity['distribution']):
            raise ValueError('Candidate packaging requires a clean distribution build; development fixtures require --development')
        runtime_version = tuple(map(int, config('runtime.json')['version'].split('.')))
        linker_version = tuple(map(int, receipt['linkerVersion'].split('.')))
        # Qt's SDK records the compiler used to produce its binary inputs.
        qt_doc = json.loads((self.qt / 'sbom/qtbase-6.8.2.spdx.json').read_text(encoding='utf-8'))
        qt_compilers = [item['versionInfo'] for item in qt_doc['packages'] if item.get('name') == 'MSVC']
        if not qt_compilers:
            raise ValueError('Qt SDK compiler provenance missing')
        for compiler in qt_compilers:
            version = list(map(int, compiler.split('.')))
            version[0] -= 5  # Compiler 19.xx corresponds to the 14.xx redistributable.
            if runtime_version < tuple(version):
                raise ValueError('Pinned CRT predates the Qt SDK compiler; review runtime lock')
        if runtime_version < linker_version:
            raise ValueError('Pinned CRT predates application toolchain; review runtime lock')
        self.output.mkdir(parents=True, exist_ok=True)
        return identity, receipt

    def read_audit(self):
        audit_path = self.output / 'deployment-audit.json'
        record = json.loads(audit_path.read_text(encoding='utf-8'))
        if record.get('status') != 'passed' or record.get('mode') != 'deployed':
            raise ValueError('Successful deployed audit required')
        if not self.args.development and (record['identity']['dirty'] or not record['identity']['distribution']):
            raise ValueError('Development runtime cannot become a distribution candidate')
        verify_inventory(self.root, record['files'])
        return record

    def runtime(self):
        identity, receipt = self.preflight()
        if self.root.exists():
            raise ValueError('Use a new package output directory; staging trees are never overlaid')
        self.root.mkdir()
        shutil.copyfile(self.build / 'bin/xdstarter.exe', self.root / 'xdstarter.exe')
        command(self.qt / 'bin/windeployqt.exe', '--release', '--no-compiler-runtime',
            '--no-system-d3d-compiler', '--no-system-dxc-compiler', '--skip-plugin-types',
            'qmltooling', '--translations', 'zh_CN', '--qmldir', ROOT / 'qml',
            '--qmlimport', self.build / 'qml', self.root / 'xdstarter.exe', timeout=300)
        removed = prune(self.root)
        copy_crt(self.cache, self.root)
        copy_graphics(self.qt, self.root)
        (self.root / 'qt.conf').write_text(
            '[Paths]\nPrefix=.\nPlugins=.\nQmlImports=qml\nTranslations=translations\n', encoding='utf-8')
        collect_notices(self.cache, self.qt, self.root)
        write_json(self.root / 'share/build-info.json', identity)
        write_json(self.root / 'share/deployment.json', {'schemaVersion': 1, 'quickStyle': 'Basic',
            'removed': removed, 'removedBytes': sum(item['bytes'] for item in removed),
            'crt': config('runtime.json')['version'], 'graphics': list(GRAPHICS),
            'toolchain': {key: receipt.get(key) for key in
                          ('compilerVersion', 'linkerVersion', 'compilerToolset', 'windowsSdk')},
            'developmentFixture': self.args.development})
        record = audit(self.root, identity)
        write_json(self.output / 'deployment-audit.json', record)
        write_json(self.output / 'runtime-attribution.json', attribution(self.qt, self.root))
        groups = {}
        for item in record['files']:
            name = item['path']
            category = ('crt' if name in config('runtime.json')['files'] else
                        'graphics' if name in GRAPHICS else
                        'qml' if name.startswith('qml/') else
                        'notices' if name.startswith('share/notices/') else
                        'application' if name == 'xdstarter.exe' else 'qt-and-resources')
            groups[category] = groups.get(category, 0) + item['bytes']
        write_json(self.output / 'size-report.json', {'files': len(record['files']),
            'bytes': record['bytes'], 'groups': groups,
            'removedFiles': len(removed), 'removedBytes': sum(item['bytes'] for item in removed)})

    def verify_runtime(self):
        record = self.read_audit()
        result_path = self.output / 'runtime-verification.json'
        write_json(result_path, {'status': 'running'})
        audit(self.root, record['identity'])
        harness = generated(self.output / ('verify-' + uuid.uuid4().hex))
        runtime = harness / 'runtime'
        shutil.copytree(self.root, runtime)
        logs = self.output / ('verification-' + uuid.uuid4().hex)
        logs.mkdir()
        try:
            # Test-only imports and plugins are injected into a separate owned copy.
            for name in ('xdstarter_ui_tests.exe', 'xdstarter_runtime_support_tests.exe'):
                shutil.copyfile(self.build / 'bin' / name, runtime / name)
            shutil.copyfile(self.qt / 'bin/Qt6Test.dll', runtime / 'Qt6Test.dll')
            shutil.copyfile(self.qt / 'plugins/platforms/qoffscreen.dll', runtime / 'platforms/qoffscreen.dll')
            for software in (True, False):
                env = isolated_environment(runtime, software=software)
                temporary = harness / 'tmp'
                temporary.mkdir(exist_ok=True)
                env.update(TEMP=str(temporary), TMP=str(temporary))
                if json.loads(command(runtime / 'xdstarter.exe', '--build-info', env=env, cwd=harness)) != record['identity']:
                    raise ValueError('Runtime executable and audit identities differ')
                command(runtime / 'xdstarter.exe', '--smoke-test', env=env, cwd=harness, timeout=30)
                selection = ['prototypeWindowLoads', 'layoutImportPreviewAndRestore',
                             'layoutImportFilePickerOwnership', 'layoutImportDialogThemeAndKeyboard']
                command(runtime / 'xdstarter_ui_tests.exe', *selection, '-o',
                    str(logs / ('software.log' if software else 'native-d3d11.log')) + ',txt',
                    env=env, cwd=harness, timeout=180)
            command(runtime / 'xdstarter_runtime_support_tests.exe', runtime / 'translations',
                    env=isolated_environment(runtime), cwd=harness)
            verify_inventory(self.root, record['files'])
            write_json(result_path, {'status': 'passed', 'sdkSearchPaths': False,
                'auditSha256': digest(self.output / 'deployment-audit.json'),
                'executableSha256': digest(self.root / 'xdstarter.exe'),
                'developmentFixture': self.args.development,
                'limits': ['Windows host supplies OS APIs. This is not a clean Windows 11 installation or manual desktop acceptance.']})
        finally:
            generated(harness)
            files(harness)
            shutil.rmtree(harness)

    def prefix(self, identity):
        suffix = '-development' if self.args.development else ''
        return (f'xdstarter-{identity["version"]}-build{identity["build"]}-'
                f'{identity["revision"][:12]}-windows-x64{suffix}')

    def installer(self):
        from windows_installer import compile_installer
        record = self.read_audit()
        verification = json.loads((self.output / 'runtime-verification.json').read_text(encoding='utf-8'))
        if (verification.get('status') != 'passed' or verification.get('auditSha256') !=
                digest(self.output / 'deployment-audit.json')):
            raise ValueError('Verify the same runtime before compiling an installer')
        compile_installer(self.root, record, self.output, self.cache,
                          self.prefix(record['identity']), self.args.development)

    def archives(self):
        record = self.read_audit()
        prefix = self.prefix(record['identity'])
        archive_path = self.output / (prefix + '.zip')
        with zipfile.ZipFile(archive_path, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
            for item in record['files']:
                archive.write(self.root / item['path'], item['path'])
        with (self.output / (prefix + '-source.tar.gz')).open('wb') as stream:
            subprocess.run(['git', 'archive', '--format=tar.gz', '--prefix=xdstarter/',
                record['identity']['revision']], cwd=ROOT, stdout=stream, check=True)
        # Original source archives are copied verbatim into the matching materials bundle.
        with tarfile.open(self.output / (prefix + '-materials.tar.xz'), 'w:xz', preset=1) as archive:
            for item in config('sources.json')['archives']:
                source = self.cache / 'sources' / item['file']
                if digest(source) != item['sha256']:
                    raise ValueError('Source material changed after preparation')
                archive.add(source, arcname='sources/' + item['file'], recursive=False)
            archive.add(CONFIG / 'sources.json', arcname='sources.json', recursive=False)
            archive.add(self.root / 'share/notices', arcname='notices')
        artifacts = [path for path in sorted(self.output.iterdir())
                     if path.is_file() and (path.name.startswith(prefix) or path.suffix == '.json')]
        write_json(self.output / 'package.json', {'status': 'passed', 'identity': record['identity'],
            'developmentFixture': self.args.development, 'signed': False, 'publicReleaseApproved': False,
            'artifacts': [{'file': path.name, 'sha256': digest(path), 'bytes': path.stat().st_size}
                          for path in artifacts if path.name != 'package.json']})
        artifacts = [path for path in self.output.iterdir() if path.is_file() and path.name != 'SHA256SUMS']
        (self.output / 'SHA256SUMS').write_text(''.join(
            f'{digest(path)}  {path.name}\n' for path in sorted(artifacts)), encoding='utf-8')

    def verify_archives(self):
        record = self.read_audit()
        package = json.loads((self.output / 'package.json').read_text(encoding='utf-8'))
        if package.get('identity') != record['identity']:
            raise ValueError('Package and deployed executable identities differ')
        for item in package['artifacts']:
            if relative_name(item['file']) != Path(item['file']).name:
                raise ValueError('Artifact reference must be a local filename')
            path = self.output / item['file']
            if digest(path) != item['sha256'] or path.stat().st_size != item['bytes']:
                raise ValueError('Packaged artifact hash mismatch')
        with zipfile.ZipFile(self.output / (self.prefix(record['identity']) + '.zip')) as archive:
            expected = {item['path']: item['sha256'] for item in record['files']}
            actual = {}
            for member in archive.infolist():
                relative_name(member.filename)
                if member.filename in actual:
                    raise ValueError('Duplicate archive member')
                with archive.open(member) as stream:
                    actual[member.filename] = hashlib.file_digest(stream, 'sha256').hexdigest()
            if actual != expected:
                raise ValueError('Portable archive differs from the audited runtime')


def execute(args):
    from toolchain import build_environment
    workflow = Workflow(args)
    # Qt deployment tools also create temporary files. Use the same owned
    # environment as compilation rather than inheriting a sandbox/user TEMP.
    os.environ.update(build_environment(workflow.qt))
    if args.command == 'prepare-package':
        prepare(workflow.cache)
    elif args.command == 'package':
        if args.kind in ('runtime', 'all'):
            workflow.runtime()
        workflow.verify_runtime()
        if args.kind in ('installer', 'all'):
            workflow.installer()
        workflow.archives()
        workflow.verify_archives()
    else:
        workflow.verify_runtime()
        if args.kind != 'runtime':
            workflow.verify_archives()
    print('Windows ' + args.command + ' completed; evidence under ' + workflow.output.relative_to(ROOT).as_posix())
