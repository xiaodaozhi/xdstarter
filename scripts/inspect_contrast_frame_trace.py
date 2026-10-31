#!/usr/bin/env python3
"""Validate and summarize opt-in contrast-frame research evidence."""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import struct


SCENES = ('contrast-dark', 'contrast-light-focus', 'contrast-light-menu')
STAGES = ('early', 'ready', 'first', 'later')
PNG_SIGNATURE = b'\x89PNG\r\n\x1a\n'


def tile_hints(state: dict) -> dict:
    hints, count = state.get('tileHints'), state.get('tileCount')
    if (state.get('tileHintsComplete') is not True or type(count) is not int or count < 0 or
            not isinstance(hints, list) or len(hints) != count):
        raise ValueError('missing tile hint state')
    result = {}
    for hint in hints:
        if not isinstance(hint, dict):
            raise ValueError('invalid tile hint state')
        tile_id = hint.get('tileId')
        if (not isinstance(tile_id, str) or not tile_id or tile_id in result or
                any(type(hint.get(key)) is not bool for key in ('hovered', 'visible', 'opened'))):
            raise ValueError('invalid tile hint state')
        result[tile_id] = {key: hint[key] for key in ('hovered', 'visible', 'opened')}
    return result


def inspect(directory: Path, expected_qpa: str | None = None, expected_graphics_api: int | None = None,
            require_hints: bool = False) -> dict:
    log = (directory / 'test.log').read_text(encoding='utf-8', errors='replace')
    passes = re.findall(r'(?m)^PASS\s*:\s*PrototypeWindowTest::([^\r\n]+)', log)
    totals = re.findall(r'Totals:\s*(\d+) passed,\s*(\d+) failed,\s*(\d+) skipped,\s*(\d+) blacklisted', log)
    if (passes != ['initTestCase()', 'highContrastTracksSystemPalette()', 'cleanupTestCase()'] or
            totals != [('3', '0', '0', '0')] or re.search(r'Q(?:CRITICAL|FATAL)\s*:|FAIL\s*!', log, re.IGNORECASE)):
        raise ValueError('failed or incomplete contrast QtTest log')
    trace = json.loads((directory / 'frame-trace.json').read_text(encoding='utf-8'))
    version = trace.get('schemaVersion')
    if (type(version) is not int or version not in (1, 2) or trace.get('observerIsInstrumented') is not True or
            trace.get('eventLimitReached') is not False or not trace.get('qpa') or
            not isinstance(trace.get('graphicsApi'), int) or not trace.get('qtVersion')):
        raise ValueError('invalid or truncated trace metadata')
    if require_hints and version != 2:
        raise ValueError('missing tile hint trace')
    if ((expected_qpa is not None and trace['qpa'] != expected_qpa) or
            (expected_graphics_api is not None and trace['graphicsApi'] != expected_graphics_api)):
        raise ValueError('trace backend does not match the requested scenario')
    events = trace.get('events')
    if not isinstance(events, list) or not events:
        raise ValueError('missing trace events')
    last = -1
    phases = set()
    counts = Counter()
    for event in events:
        instant = event.get('ns')
        if (not isinstance(instant, (int, float)) or isinstance(instant, bool) or instant < last or
                not isinstance(event.get('event'), str) or not isinstance(event.get('phase'), str) or
                not re.fullmatch(r'[0-9]+', str(event.get('thread', '')))):
            raise ValueError('invalid event order or identity')
        last = instant
        counts[event['event']] += 1
        if event['event'] == 'gui-state':
            if not isinstance(event.get('state'), dict):
                raise ValueError('missing GUI state')
            if version == 2:
                tile_hints(event['state'])
            phases.add(event['phase'])
    required_phases = {f'{scene}:{suffix}' for scene in SCENES for suffix in
                       ('request-update', 'before-early-grab', 'before-ready-grab',
                        'before-first-grab', 'before-later-grab')}
    if not required_phases <= phases:
        raise ValueError('missing scene phase')
    captures = trace.get('captures')
    if not isinstance(captures, list) or len(captures) != len(SCENES) * len(STAGES):
        raise ValueError('missing capture records')
    hashes = {}
    dimensions = {}
    hint_snapshots = {}
    last_capture = -1
    for capture in captures:
        scene, stage = capture.get('scene'), capture.get('stage')
        instant = capture.get('ns')
        key = (scene, stage)
        if (scene not in SCENES or stage not in STAGES or key in hashes or
                capture.get('valid') is not True or not isinstance(capture.get('state'), dict) or
                not isinstance(instant, (float, int)) or isinstance(instant, bool) or instant < last_capture):
            raise ValueError('invalid or repeated capture')
        state = capture['state']
        if version == 2:
            hint_snapshots[key] = tile_hints(state)
        if (state.get('highContrast') is not True or state.get('searchGlyphVisible') is not True or
                type(state.get('searchGlyphStatus')) is not int or state['searchGlyphStatus'] not in (0, 1, 2, 3) or
                (stage in ('ready', 'first', 'later') and state['searchGlyphStatus'] != 1)):
            raise ValueError('missing or unready search glyph state')
        last_capture = instant
        expected_name = f'{scene}-{stage}.png'
        path = directory / expected_name
        if capture.get('file') != expected_name or not path.is_file() or path.is_symlink():
            raise ValueError('missing capture file')
        data = path.read_bytes()
        digest = hashlib.sha256(data).hexdigest()
        if (capture.get('sha256') != digest or len(data) < 24 or data[:8] != PNG_SIGNATURE or
                data[12:16] != b'IHDR' or struct.unpack('>II', data[16:24]) !=
                (capture.get('width'), capture.get('height')) or capture.get('dpr', 0) <= 0):
            raise ValueError('capture bytes or dimensions differ')
        hashes[key] = digest
        dimensions[key] = (capture['width'], capture['height'])
    comparisons = trace.get('pixelComparisons')
    if not isinstance(comparisons, list) or len(comparisons) != len(SCENES) * 3:
        raise ValueError('missing pixel comparisons')
    observed = {}
    for comparison in comparisons:
        scene, before, after = comparison.get('scene'), comparison.get('from'), comparison.get('to')
        key = (scene, before, after)
        if (scene not in SCENES or (before, after) not in {('early', 'ready'), ('ready', 'first'), ('first', 'later')} or
                key in observed or comparison.get('comparable') is not True):
            raise ValueError('invalid pixel comparison')
        changed = comparison.get('changedPixels')
        glyph_changed = comparison.get('glyphChangedPixels')
        width, height = dimensions[(scene, before)]
        if (dimensions[(scene, after)] != (width, height) or type(changed) is not int or
                changed < 0 or changed > width * height or
                (hashes[(scene, before)] == hashes[(scene, after)] and changed)):
            raise ValueError('invalid changed pixel count')
        if (comparison.get('glyphComparable') is not True or type(glyph_changed) is not int or
                glyph_changed < 0 or glyph_changed > changed):
            raise ValueError('invalid glyph pixel comparison')
        if (before, after) == ('ready', 'first') and glyph_changed:
            raise ValueError('search glyph changed after its ready capture')
        if changed:
            bounds = comparison.get('bounds')
            if (not isinstance(bounds, dict) or any(type(bounds.get(k)) is not int for k in ('x', 'y', 'width', 'height')) or
                    bounds['x'] < 0 or bounds['y'] < 0 or bounds['width'] <= 0 or bounds['height'] <= 0 or
                    bounds['x'] + bounds['width'] > width or bounds['y'] + bounds['height'] > height):
                raise ValueError('invalid changed pixel bounds')
        observed[key] = comparison
    result = {'qpa': trace['qpa'], 'graphicsApi': trace['graphicsApi'], 'qtVersion': trace['qtVersion'],
              'observerIsInstrumented': True, 'eventCounts': dict(sorted(counts.items())),
              'captures': len(captures),
              'comparisons': {scene: {'earlyToReady': observed[(scene, 'early', 'ready')],
                                      'readyToFirst': observed[(scene, 'ready', 'first')],
                                      'firstToLater': observed[(scene, 'first', 'later')]}
                              for scene in SCENES}}
    if version == 2:
        changes = {}
        for scene in SCENES:
            changes[scene] = {}
            for label, before, after in (('earlyToReady', 'early', 'ready'),
                                         ('readyToFirst', 'ready', 'first'),
                                         ('firstToLater', 'first', 'later')):
                old, new = hint_snapshots[(scene, before)], hint_snapshots[(scene, after)]
                changes[scene][label] = [{'tileId': tile_id, 'before': old.get(tile_id), 'after': new.get(tile_id)}
                                         for tile_id in sorted(old.keys() | new.keys())
                                         if old.get(tile_id) != new.get(tile_id)]
        result['tileHintChanges'] = changes
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--qpa')
    parser.add_argument('--graphics-api', type=int)
    parser.add_argument('--allow-legacy', action='store_true', help='read a trace without tile hint states')
    args = parser.parse_args()
    try:
        result = inspect(args.directory, args.qpa, args.graphics_api, require_hints=not args.allow_legacy)
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f'Contrast frame trace incomplete: {error}')
        return 1
    print(json.dumps(result, ensure_ascii=False, sort_keys=True))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
