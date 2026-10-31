from __future__ import annotations

import base64
import copy
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
import inspect_contrast_frame_trace as inspector

PNG = base64.b64decode('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+X1ioAAAAASUVORK5CYII=')


class ContrastFrameTraceTests(unittest.TestCase):
    def setUp(self):
        (ROOT / 'build').mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix='frame-trace-', dir=ROOT / 'build')
        self.root = Path(self.temporary.name)
        self.addCleanup(self.temporary.cleanup)
        events, captures = [], []
        hints = {'tileCount': 1, 'tileHintsComplete': True,
                 'tileHints': [{'tileId': 'sample', 'hovered': False, 'visible': False, 'opened': False}]}
        tick = 0
        for scene in inspector.SCENES:
            for phase in ('request-update', 'before-early-grab', 'before-ready-grab',
                          'before-first-grab', 'before-later-grab'):
                tick += 1
                events.append({'ns': tick, 'event': 'gui-state', 'phase': f'{scene}:{phase}',
                               'thread': '42', 'state': copy.deepcopy(hints)})
            for stage in inspector.STAGES:
                tick += 1
                name = f'{scene}-{stage}.png'
                (self.root / name).write_bytes(PNG)
                captures.append({'scene': scene, 'stage': stage, 'ns': tick,
                                 'state': {**copy.deepcopy(hints), 'highContrast': True,
                                           'searchGlyphVisible': True, 'searchGlyphStatus': 1},
                                 'valid': True,
                                 'file': name, 'sha256': hashlib.sha256(PNG).hexdigest(),
                                 'width': 1, 'height': 1, 'dpr': 1})
        comparisons = [{'scene': scene, 'from': before, 'to': after, 'comparable': True, 'changedPixels': 0,
                        'glyphComparable': True, 'glyphChangedPixels': 0}
                       for scene in inspector.SCENES for before, after in
                       (('early', 'ready'), ('ready', 'first'), ('first', 'later'))]
        self.trace = {'schemaVersion': 2, 'observerIsInstrumented': True, 'eventLimitReached': False,
                      'qpa': 'offscreen', 'graphicsApi': 1, 'qtVersion': '6.8.2',
                      'events': events, 'captures': captures, 'pixelComparisons': comparisons}
        self.write_trace()
        (self.root / 'test.log').write_text(
            'PASS : PrototypeWindowTest::initTestCase()\n'
            'PASS : PrototypeWindowTest::highContrastTracksSystemPalette()\n'
            'PASS : PrototypeWindowTest::cleanupTestCase()\n'
            'Totals: 3 passed, 0 failed, 0 skipped, 0 blacklisted, 100ms\n', encoding='utf-8')

    def write_trace(self):
        (self.root / 'frame-trace.json').write_text(json.dumps(self.trace), encoding='utf-8')

    def test_valid_trace_reports_comparison_without_claiming_completion(self):
        report = inspector.inspect(self.root)
        self.assertEqual(report['captures'], 12)
        self.assertEqual(report['comparisons']['contrast-dark']['earlyToReady']['changedPixels'], 0)
        self.assertNotIn('renderingComplete', report)
        with self.assertRaisesRegex(ValueError, 'backend does not match'):
            inspector.inspect(self.root, 'windows', 4)

    def test_hint_changes_are_reported_without_claiming_render_completion(self):
        for capture in self.trace['captures']:
            if capture['scene'] == 'contrast-dark' and capture['stage'] == 'first':
                capture['state']['tileHints'][0].update(hovered=True, visible=True, opened=True)
        self.write_trace()
        report = inspector.inspect(self.root, require_hints=True)
        changes = report['tileHintChanges']['contrast-dark']['readyToFirst']
        self.assertEqual(len(changes), 1)
        self.assertEqual(changes[0]['tileId'], 'sample')
        self.assertFalse(changes[0]['before']['visible'])
        self.assertTrue(changes[0]['after']['visible'])
        self.assertNotIn('renderingComplete', report)

    def test_missing_invalid_or_duplicate_hint_state_is_rejected(self):
        for change in ('missing', 'incomplete', 'empty', 'duplicate', 'wrong-type', 'event-missing'):
            with self.subTest(change=change):
                original = json.loads(json.dumps(self.trace))
                state = self.trace['captures'][0]['state']
                if change == 'missing':
                    state.pop('tileHints')
                elif change == 'incomplete':
                    state['tileHintsComplete'] = False
                elif change == 'empty':
                    state.update(tileHints=[], tileCount=0)
                elif change == 'duplicate':
                    state['tileHints'].append(dict(state['tileHints'][0]))
                    state['tileCount'] = 2
                elif change == 'wrong-type':
                    state['tileHints'][0]['visible'] = 1
                else:
                    self.trace['events'][0]['state'].pop('tileHints')
                self.write_trace()
                with self.assertRaisesRegex(ValueError, 'tile hint state'):
                    inspector.inspect(self.root, require_hints=True)
                self.trace = original
        self.write_trace()

    def test_legacy_trace_can_be_read_but_does_not_satisfy_new_ci(self):
        self.trace['schemaVersion'] = 1
        self.write_trace()
        self.assertNotIn('tileHintChanges', inspector.inspect(self.root))
        with self.assertRaisesRegex(ValueError, 'missing tile hint trace'):
            inspector.inspect(self.root, require_hints=True)

    def test_trace_audit_belongs_only_to_windows_job_after_build(self):
        workflow = json.loads((ROOT / '.github/workflows/application-build.yml').read_text(encoding='utf-8'))
        self.assertFalse(any(step.get('id') == 'contrast_trace' for step in workflow['jobs']['core']['steps']))
        steps = workflow['jobs']['windows']['steps']
        audit = [index for index, step in enumerate(steps) if step.get('id') == 'contrast_trace']
        build = [index for index, step in enumerate(steps) if step.get('id') == 'build']
        self.assertEqual(len(audit), 1)
        self.assertEqual(len(build), 1)
        self.assertGreater(audit[0], build[0])
        self.assertIn('inspect_contrast_frame_trace.py', steps[audit[0]]['run'])

    def test_tampered_image_and_missing_phase_are_rejected(self):
        (self.root / 'contrast-dark-early.png').write_bytes(PNG + b'tamper')
        with self.assertRaisesRegex(ValueError, 'bytes or dimensions'):
            inspector.inspect(self.root)
        (self.root / 'contrast-dark-early.png').write_bytes(PNG)
        self.trace['events'].pop()
        self.write_trace()
        with self.assertRaisesRegex(ValueError, 'missing scene phase'):
            inspector.inspect(self.root)

    def test_event_reordering_and_invalid_capture_are_rejected(self):
        self.trace['events'][1]['ns'] = -1
        self.write_trace()
        with self.assertRaisesRegex(ValueError, 'event order'):
            inspector.inspect(self.root)
        self.trace['events'][1]['ns'] = 2
        self.trace['captures'][0]['valid'] = False
        self.write_trace()
        with self.assertRaisesRegex(ValueError, 'invalid or repeated capture'):
            inspector.inspect(self.root)

    def test_critical_qt_log_is_not_hidden_by_successful_totals(self):
        path = self.root / 'test.log'
        path.write_text(path.read_text(encoding='utf-8') + 'QCRITICAL: frame fixture failure\n', encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'failed or incomplete contrast QtTest log'):
            inspector.inspect(self.root)

    def test_ready_glyph_changes_are_not_hidden_by_other_stable_pixels(self):
        path = self.root / 'contrast-dark-first.png'
        changed = PNG + b'changed'
        path.write_bytes(changed)
        for capture in self.trace['captures']:
            if capture['scene'] == 'contrast-dark' and capture['stage'] == 'first':
                capture['sha256'] = hashlib.sha256(changed).hexdigest()
        for comparison in self.trace['pixelComparisons']:
            if comparison['scene'] == 'contrast-dark' and comparison['from'] == 'ready':
                comparison.update(changedPixels=1, glyphChangedPixels=1,
                                  bounds={'x': 0, 'y': 0, 'width': 1, 'height': 1})
        self.write_trace()
        with self.assertRaisesRegex(ValueError, 'search glyph changed'):
            inspector.inspect(self.root)


if __name__ == '__main__':
    unittest.main()
