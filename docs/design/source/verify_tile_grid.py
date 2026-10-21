# docs/design/source/verify_tile_grid.py

"""Verify current SVG tile geometry, grid placement, palette and PNG canvases."""
import json
from pathlib import Path
import re
import struct
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
GRID = json.loads((ROOT / 'tokens/tile-grid.json').read_text(encoding='utf-8'))
VISUAL = json.loads((ROOT / 'tokens/visual.json').read_text(encoding='utf-8'))
NS = {'s': 'http://www.w3.org/2000/svg'}


def verify():
    groups = []
    layout = VISUAL['layout']
    stride = GRID['cell'] + GRID['gap']
    grid_width = GRID['columns'] * stride - GRID['gap']
    expected_left = layout['railWidth'] + layout['contentMargin'] + layout['catalogWidth'] + layout['contentSpacing']
    expected_first_top = layout['contentMargin'] + layout['searchHeight'] + layout['contentSpacing'] + layout['headerHeight'] + GRID['gap']
    assert (GRID['cell'], GRID['gap'], GRID['columns'], grid_width) == (48, 8, 10, 552)
    for theme in ('dark', 'light'):
        svg = ET.parse(ROOT / f'mockups/v2/xdstarter-desktop-{theme}-v2.svg').getroot()
        expected_top = expected_first_top
        count = 0
        for group, apps in GRID['groups'].items():
            node = svg.find(f'.//s:g[@data-group="{group}"]', NS)
            position = re.fullmatch(r'translate\(([-\d.]+) ([-\d.]+)\)', node.get('transform'))
            assert position is not None and [float(value) for value in position.groups()] == [expected_left, expected_top]
            occupied_height = 0
            for app in apps:
                count += 1
                el = node.find(f's:g[@data-app="{app["app"]}"]', NS)
                rect = el.find('s:rect', NS)
                span = GRID['sizes'][app['size']]
                width = span['columns'] * stride - GRID['gap']
                height = span['rows'] * stride - GRID['gap']
                x, y = app['column'] * stride, app['row'] * stride
                assert float(rect.get('width')) == width and float(rect.get('height')) == height
                assert float(rect.get('x')) == x and float(rect.get('y')) == y
                assert x + width <= grid_width
                assert expected_left + x + width <= VISUAL['defaultMenuSizeDip']['width'] - layout['contentMargin']
                assert expected_top + y + height <= VISUAL['defaultMenuSizeDip']['height'] - layout['contentMargin']
                assert rect.get('data-base-color') in VISUAL['tilePalette']
                if app['size'] != 'wide':
                    assert width == height
                else:
                    assert width == 2 * height + GRID['gap']
                occupied_height = max(occupied_height, y + height)
            expected_top += occupied_height + layout['groupSpacing'] + layout['headerHeight'] + GRID['gap']
        groups.append([ET.tostring(element) for element in svg.findall('.//s:g[@data-group]', NS)])
        png = (ROOT / f'mockups/v2/xdstarter-desktop-{theme}-v2.png').read_bytes()
        assert png[:8] == b'\x89PNG\r\n\x1a\n'
        assert struct.unpack('>II', png[16:24]) == (VISUAL['referenceCanvas']['width'], VISUAL['referenceCanvas']['height'])
        print(f'{theme}: all {count} tile bounds, 10-column group positions and palette correct; 1920x1200 PNG')
    assert groups[0] == groups[1], 'Theme tile geometry/content differs'
    print('Both themes have identical tile geometry and content.')


if __name__ == '__main__':
    verify()
