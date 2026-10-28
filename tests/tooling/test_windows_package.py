"""Behavioral regressions for candidate payload boundaries and dependency failures."""
import importlib.util
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
            for missing in ('Qt6Network.dll', 'vcruntime140.dll', 'libssl-3-x64.dll'):
                with self.subTest(missing=missing), self.assertRaisesRegex(ValueError, 'Unresolved'):
                    package.audit(self.directory, {}, lambda _: [missing.casefold()], self.pe)

    def test_audit_refuses_test_only_payload(self):
        with self.fixture():
            self.write('Qt6Test.dll')
            with self.assertRaisesRegex(ValueError, 'development/unused'):
                package.audit(self.directory, {}, lambda _: [], self.pe)

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

    def test_existing_toolchain_cli_remains_available(self):
        result = subprocess.run([sys.executable, str(ROOT / 'scripts/toolchain.py'), '--help'],
                                capture_output=True, text=True, check=True)
        for command in ('install', 'check', 'build', 'prepare-package', 'package', 'verify'):
            self.assertIn(command, result.stdout)


if __name__ == '__main__':
    unittest.main()
