"""Behavioral regressions for candidate payload boundaries and dependency failures."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
import windows_package as package
import verify_windows_installer as lifecycle


class WindowsPackageTests(unittest.TestCase):
    def setUp(self):
        (ROOT / 'build').mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix='package-tests-', dir=ROOT / 'build')
        self.directory = Path(self.temporary.name)
        self.addCleanup(self.temporary.cleanup)

    def write(self, relative, data=b'owned'):
        path = self.directory / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        return path

    def test_output_root_cannot_be_workspace_build_root_or_escape(self):
        self.assertEqual(package.generated(self.directory), self.directory)
        for path in (ROOT, ROOT / 'build', ROOT / 'build/../../outside', ROOT / 'packaging'):
            with self.subTest(path=path), self.assertRaises(ValueError):
                package.generated(path)

    def test_manifest_rejects_windows_aliases_traversal_and_collisions(self):
        for name in ('../outside', '/outside', 'C:/outside', 'a//b', 'AUX.txt', 'com1/data',
                     'dir/end.', 'file:stream', 'unins000.exe', 'install-manifest.txt'):
            with self.subTest(name=name), self.assertRaises(ValueError):
                package.payload_manifest([{'path': name, 'sha256': 'a' * 64}])
        with self.assertRaises(ValueError):
            package.payload_manifest([{'path': name, 'sha256': 'a' * 64}
                                      for name in ('File.dll', 'file.dll')])

    def test_inventory_refuses_changed_missing_and_unexpected_files(self):
        source = self.write('file.txt')
        record = package.inventory(self.directory)
        package.verify_inventory(self.directory, record)
        source.write_bytes(b'changed')
        with self.assertRaises(ValueError):
            package.verify_inventory(self.directory, record)
        source.unlink()
        with self.assertRaises(ValueError):
            package.verify_inventory(self.directory, record)
        source.write_bytes(b'owned')
        self.write('unexpected.dll')
        with self.assertRaises(ValueError):
            package.verify_inventory(self.directory, record)

    def test_versioned_lifecycle_fixtures_are_distinct_and_preserve_source(self):
        root = self.directory / 'candidate'
        root.mkdir()
        (root / 'xdstarter.exe').write_bytes(b'unchanged application')
        audit = {'identity': {'version': '1.2.3', 'build': 9}, 'files': package.inventory(root)}
        original = json.loads(json.dumps(audit))
        work = self.directory / 'work'
        work.mkdir()
        seen = []

        def compile_fixture(payload, record, output, cache, prefix, *, qa):
            self.assertTrue(qa)
            package.verify_inventory(payload, record['files'])
            seen.append((prefix, record['identity']['version'],
                         {row['path'] for row in record['files']}, package.digest(payload / 'xdstarter.exe')))
            result = output / (prefix + '-setup.exe')
            result.write_bytes(prefix.encode('ascii'))
            return result

        with patch.object(lifecycle, 'compile_installer', side_effect=compile_fixture):
            records = lifecycle.versioned_fixtures(root, audit, work, self.directory)
        self.assertEqual(audit, original)
        package.verify_inventory(root, original['files'])
        self.assertEqual([row[:2] for row in seen], [('older', '0.0.1'), ('newer', '0.0.2')])
        self.assertEqual(seen[0][3], seen[1][3])
        self.assertIn('share/ci-installer-version.txt', seen[0][2])
        self.assertNotIn('share/ci-installer-newer-only.txt', seen[0][2])
        self.assertIn('share/ci-installer-newer-only.txt', seen[1][2])
        self.assertNotEqual(records['older']['markerSha256'], records['newer']['markerSha256'])
        self.assertNotEqual(records['older']['setupSha256'], records['newer']['setupSha256'])

    def test_basic_dialogs_and_graphics_survive_style_pruning(self):
        retained = ['qml/QtQuick/Controls/Basic/qmldir', 'qml/QtQuick/Dialogs/qmldir',
                    'Qt6QuickDialogs2QuickImpl.dll', *package.GRAPHICS]
        removed = ['qml/QtQuick/Controls/Material/qmldir', 'Qt6QuickControls2Fusion.dll',
                   'qml/QtQuick/plugins.qmltypes', 'qmltooling/debug.dll']
        for name in retained + removed:
            self.write(name)
        self.assertEqual({item['path'] for item in package.prune(self.directory)}, set(removed))
        self.assertTrue(all((self.directory / name).is_file() for name in retained))
        self.assertEqual(package.prune(self.directory), [])

    def fixture(self):
        for name in ('xdstarter.exe', 'Qt6Core.dll', 'msvcp140.dll', 'plugins/plugin.dll'):
            self.write(name)
        runtime = {'files': {'msvcp140.dll': package.digest(self.directory / 'msvcp140.dll')}}
        return patch.multiple(package, REQUIRED=('xdstarter.exe', 'msvcp140.dll'),
                              config=lambda _: runtime)

    @staticmethod
    def pe(_):
        return {'machine': 'x64', 'subsystem': 2}

    def test_audit_recurses_into_plugins_and_requires_app_local_crt(self):
        with self.fixture():
            dependencies = lambda path: ['kernel32.dll', 'msvcp140.dll']
            record = package.audit(self.directory, {}, dependencies, self.pe)
            self.assertEqual(len(record['binaries']), 4)
            broken_plugin = lambda path: ['MissingQt.dll'] if path.name == 'plugin.dll' else dependencies(path)
            with self.assertRaisesRegex(ValueError, 'MissingQt.dll|missingqt.dll'):
                package.audit(self.directory, {}, broken_plugin, self.pe)
            (self.directory / 'msvcp140.dll').unlink()
            with self.assertRaisesRegex(ValueError, 'Required runtime'):
                package.audit(self.directory, {}, dependencies, self.pe)

    def test_audit_does_not_accept_sdk_or_system32_as_fallback_for_third_party(self):
        with self.fixture():
            for missing in ('Qt6Network.dll', 'vcruntime140.dll', 'libssl-3-x64.dll', 'opengl32sw.dll'):
                with self.subTest(missing=missing), self.assertRaisesRegex(ValueError, 'Unresolved'):
                    package.audit(self.directory, {}, lambda _: [missing.casefold()], self.pe)

    def test_audit_refuses_test_only_payload(self):
        with self.fixture():
            self.write('Qt6Test.dll')
            with self.assertRaisesRegex(ValueError, 'development/unused'):
                package.audit(self.directory, {}, lambda _: [], self.pe)

    def test_audit_refuses_excluded_graphics_dlls_at_any_depth(self):
        with self.fixture():
            for name in ('opengl32sw.dll', 'plugins/OPENGL32SW.DLL',
                         'd3d11.dll', 'plugins/XDSTARTER_D3D11_FAILURE_PROXY.DLL'):
                file = self.write(name)
                with self.subTest(name=name), self.assertRaisesRegex(ValueError, 'development/unused'):
                    package.audit(self.directory, {}, lambda _: [], self.pe)
                file.unlink()

    def test_warp_evidence_requires_the_selected_microsoft_software_adapter(self):
        start = 'qt.scenegraph.general: Creating QRhi with backend D3D11 for window 1\n'
        gpu = ("qt.rhi.general: Adapter 0: 'GPU's adapter' "
               "(vendor 0x8086 device 0x9A49 flags 0x0)\n")
        basic = ("qt.rhi.general: Adapter 1: 'Microsoft Basic Render Driver' "
                 "(vendor 0x1414 device 0x8C flags 0x2)\n")
        chosen = 'qt.rhi.general:   using this adapter\n'
        created = 'qt.scenegraph.general: Created QRhi 0x1 for window 1\n'
        hardware = start + gpu + 'qt.scenegraph.general: unrelated render-thread message\n' + chosen + basic + created
        self.assertEqual(package.d3d11_evidence(hardware)['selectedAdapters'][0]['name'], "GPU's adapter")
        with self.assertRaisesRegex(ValueError, 'WARP'):
            package.d3d11_evidence(hardware, warp=True)
        selected_warp = start + gpu + basic + chosen + created
        evidence = package.d3d11_evidence(selected_warp, warp=True)
        self.assertEqual(evidence['selectedAdapters'][0]['flags'] & 0x2, 0x2)
        with self.assertRaisesRegex(ValueError, 'WARP'):
            package.d3d11_evidence(selected_warp.replace('vendor 0x1414', 'vendor 0x1234'), warp=True)
        with self.assertRaisesRegex(ValueError, 'scene graph'):
            package.d3d11_evidence(start + basic + chosen +
                'qt.scenegraph.general: Creating QRhi with backend OpenGL for window 1\n' + created, warp=True)
        with self.assertRaisesRegex(ValueError, 'scene graph'):
            package.d3d11_evidence('no renderer selected', warp=True)

    def test_normal_and_delay_load_imports_are_both_audited(self):
        output = ('    Image has the following dependencies:\n        Qt6Core.DLL\n'
                  '    Image has the following delay load dependencies:\n        VCRUNTIME140.dll\n')
        with patch.object(package, 'command', return_value=output):
            self.assertEqual(package.imports(Path('owned.exe')), ['qt6core.dll', 'vcruntime140.dll'])

    def test_supplier_cache_tampering_fails_without_downloading_or_running_tools(self):
        source = self.write('cached.exe', b'bad')
        with patch.object(package.urllib.request, 'urlopen') as network:
            with self.assertRaisesRegex(ValueError, 'Cached supplier'):
                package.fetch({'sha256': 'a' * 64, 'url': 'https://example.invalid'}, source)
            network.assert_not_called()

    def test_zip_bytes_must_match_the_original_audited_payload(self):
        root = self.directory / 'runtime'
        root.mkdir()
        (root / 'owned.txt').write_bytes(b'original')
        identity = {'version': '0.1.0', 'build': 0, 'revision': 'a' * 40,
                    'dirty': True, 'distribution': False}
        record = {'status': 'passed', 'mode': 'deployed', 'identity': identity,
                  'files': package.inventory(root)}
        package.write_json(self.directory / 'deployment-audit.json', record)
        package.write_json(self.directory / 'package.json', {'identity': identity, 'artifacts': []})
        args = SimpleNamespace(qt_prefix=None, output_dir=self.directory,
                               package_cache=self.directory / 'cache', preset='app-release',
                               development=True)
        workflow = package.Workflow(args)
        archive = self.directory / (workflow.prefix(identity) + '.zip')
        with zipfile.ZipFile(archive, 'w') as output:
            output.writestr('owned.txt', b'original')
        workflow.verify_archives()
        with zipfile.ZipFile(archive, 'w') as output:
            output.writestr('owned.txt', b'tampered')
        with self.assertRaisesRegex(ValueError, 'Portable archive'):
            workflow.verify_archives()

    def test_production_runtime_reuse_requires_the_same_current_clean_build(self):
        root = self.directory / 'runtime'
        root.mkdir()
        (root / 'owned.txt').write_bytes(b'original')
        identity = {'version': '0.1.0', 'build': 1, 'revision': 'a' * 40,
                    'dirty': False, 'distribution': True}
        record = {'status': 'passed', 'mode': 'deployed', 'identity': identity,
                  'files': package.inventory(root)}
        package.write_json(self.directory / 'deployment-audit.json', record)
        workflow = package.Workflow(SimpleNamespace(qt_prefix=None, output_dir=self.directory,
            package_cache=self.directory / 'cache', preset='app-release', development=False))
        other_identity = {**identity, 'revision': 'b' * 40}
        with patch.object(workflow, 'preflight', return_value=(other_identity, {})):
            with self.assertRaisesRegex(ValueError, 'identities differ'):
                workflow.read_audit()
        with patch.object(workflow, 'preflight', side_effect=ValueError('Source changed')):
            with self.assertRaisesRegex(ValueError, 'Source changed'):
                workflow.read_audit()
        with patch.object(workflow, 'preflight', return_value=(identity, {})):
            self.assertEqual(workflow.read_audit(), record)

    def test_existing_toolchain_cli_remains_available(self):
        result = subprocess.run([sys.executable, str(ROOT / 'scripts/toolchain.py'), '--help'],
                                capture_output=True, text=True, check=True)
        for command in ('install', 'check', 'build', 'prepare-package', 'package', 'verify'):
            self.assertIn(command, result.stdout)

    def test_isolated_runtime_handles_windows_environment_case_and_clears_sdk_paths(self):
        for key in ('SystemRoot', 'SYSTEMROOT'):
            environment = {key: str(self.directory), 'PATH': 'old-sdk/bin',
                           'QT_PLUGIN_PATH': 'old-sdk/plugins', 'QML_IMPORT_PATH': 'old-sdk/qml',
                           'QSG_RHI_BACKEND': 'vulkan'}
            with self.subTest(key=key), patch.dict(package.os.environ, environment, clear=True):
                result = package.isolated_environment(self.directory / 'runtime')
                self.assertNotIn('old-sdk', result['PATH'])
                self.assertEqual(result['QSG_RHI_BACKEND'], 'd3d11')
                self.assertEqual(result['QML_IMPORT_PATH'], str(self.directory / 'runtime/qml'))
                warp = package.isolated_environment(self.directory / 'runtime', warp=True)
                self.assertEqual(warp['QSG_RHI_PREFER_SOFTWARE_RENDERER'], '1')
                software = package.isolated_environment(self.directory / 'runtime', software=True)
                self.assertEqual(software['QT_QPA_FONTDIR'], str(self.directory / 'Fonts'))
                with self.assertRaises(ValueError):
                    package.isolated_environment(self.directory / 'runtime', software=True, warp=True)


if __name__ == '__main__':
    unittest.main()
