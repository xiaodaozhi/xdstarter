from __future__ import annotations

from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
import verify_native_picker_evidence as evidence


class NativePickerEvidenceTests(unittest.TestCase):
    def setUp(self):
        (ROOT / 'build').mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix='picker-evidence-', dir=ROOT / 'build')
        self.root = Path(self.temporary.name)
        self.addCleanup(self.temporary.cleanup)
        for name, functions in evidence.FUNCTIONS.items():
            names = ('initTestCase', *functions, 'cleanupTestCase')
            lines = [f'PASS   : PrototypeWindowTest::{function}()' for function in names]
            if name in {'rapid.log', 'immediate.log', 'shown.log'}:
                mode = name.removesuffix('.log')
                lines += [f'Native picker {stage} close: "{mode}" dark= {dark}'
                          for stage in ('before', 'after') for dark in ('true', 'false')]
                if mode == 'rapid':
                    lines += ['Native picker rapid reopen: oldExited= true nextVisible= true']
                if mode == 'immediate':
                    lines += ['Native picker after-close observation: processExited= true']
                if mode == 'shown':
                    lines += ['Native picker foreground verified: foreground= true',
                              'Native picker owner focus restored: ownerForeground= true']
            elif name == 'selection.log':
                lines += ['Native picker selection driver: finished= true',
                          'Native picker selected file and opened import preview:',
                          'Native picker launch failure visibly reported and retry canceled:']
            else:
                lines += ['Native picker shutdown: fakeWinCaptureStopped= true '
                          'fakeStartCaptureStopped= true childExited= true']
            lines.append(f'Totals: {len(names)} passed, 0 failed, 0 skipped, 0 blacklisted, 50ms')
            (self.root / name).write_text('\n'.join(lines), encoding='utf-8')

    def test_complete_logs_are_accepted(self):
        evidence.verify(self.root)

    def test_critical_and_fatal_fail_even_with_passing_totals(self):
        path = self.root / 'rapid.log'
        original = path.read_text(encoding='utf-8')
        for severity in ('QCRITICAL', 'QFATAL'):
            path.write_text(original + f'\n{severity}: native thread failed\n', encoding='utf-8')
            with self.assertRaisesRegex(ValueError, 'severe Qt message'):
                evidence.verify(self.root)

    def test_missing_log_or_scenario_fails(self):
        (self.root / 'shutdown.log').unlink()
        with self.assertRaisesRegex(ValueError, 'missing native picker evidence'):
            evidence.verify(self.root)

    def test_skipped_or_incomplete_selection_fails(self):
        path = self.root / 'selection.log'
        contents = path.read_text(encoding='utf-8')
        path.write_text(contents.replace('0 skipped', '1 skipped'), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'incomplete native picker totals'):
            evidence.verify(self.root)
        path.write_text(contents.replace('finished= true', 'finished= false'), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'missing native picker observations'):
            evidence.verify(self.root)


if __name__ == '__main__':
    unittest.main()
