# docs/design/source/verify_menu_design.py

"""Check the current token-derived panel, toolbar and sidebar geometry."""
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
NS = {'s': 'http://www.w3.org/2000/svg'}
VISUAL = json.loads((ROOT / 'tokens/visual.json').read_text(encoding='utf-8'))


def verify():
    canvas = VISUAL['referenceCanvas']
    layout = VISUAL['layout']
    scale = canvas['scale']
    size = VISUAL['defaultMenuSizeDip']
    gap = VISUAL['menuChrome']['screenGapDip']
    taskbar_top = canvas['height'] - canvas['taskbarHeightDip'] * scale
    for theme in ('dark', 'light'):
        svg = ET.parse(ROOT / f'mockups/v2/xdstarter-desktop-{theme}-v2.svg').getroot()
        assert svg.get('data-design-revision') == VISUAL['designRevision'] == '2.2'
        assert float(svg.get('data-reference-scale')) == scale
        assert [float(svg.get(k)) for k in ('width', 'height')] == [canvas['width'], canvas['height']]
        menu = svg.find('.//s:g[@id="menu-window"]', NS)
        frame = menu.find('s:rect[@data-role="menu-border"]', NS)
        translation = re.fullmatch(r'translate\(([-\d.]+) ([-\d.]+)\) scale\(([-\d.]+)\)', menu.get('transform'))
        assert translation is not None
        left, top, actual_scale = (float(value) for value in translation.groups())
        assert actual_scale == scale
        assert left == gap * scale
        assert top == taskbar_top - (size['height'] + gap) * scale
        assert float(frame.get('x')) == float(frame.get('y')) == .5
        assert float(frame.get('width')) + 1 == size['width']
        assert float(frame.get('height')) + 1 == size['height']
        assert float(frame.get('rx')) + .5 == VISUAL['cornerRadiusDip']['menu']
        assert float(frame.get('stroke-width')) == VISUAL['menuChrome']['borderWidthDip'] == 1
        assert frame.get('stroke') == VISUAL['themes'][theme]['menuBorder']
        assert len(menu.findall('s:rect[@data-role="menu-border"]', NS)) == 1
        assert not svg.findall('.//s:filter', NS), 'Current design uses bounded explicit shadow geometry'
        shadow = svg.findall('.//s:rect[@data-role="menu-shadow"]', NS)
        assert len(shadow) == VISUAL['menuChrome']['shadowInsetDip'] == 24
        hit = svg.find('.//s:g[@id="start-button"]/s:rect[@data-role="start-hit-area"]', NS)
        assert float(hit.get('width')) == float(hit.get('height')) == 48 * scale
        assert float(hit.get('y')) == taskbar_top
        assert float(hit.get('x')) != left, 'Illustrated Start button is not the menu anchor'
        avatar = menu.find('s:g[@data-role="user-avatar"]', NS)
        assert float(avatar.get('data-size-dip')) == 32
        assert avatar.get('transform') == 'translate(16 24) scale(.5)'
        mask = avatar.find('.//s:mask', NS)
        circle = mask.find('s:circle', NS)
        assert [float(circle.get(key)) for key in ('cx', 'cy', 'r')] == [32, 32, 32]
        assert avatar.find(f'.//s:g[@mask="url(#{mask.get("id")})"]', NS) is not None
        entries = menu.findall('s:g[@data-shortcut]', NS)
        assert [entry.get('data-shortcut') for entry in entries] == ['documents', 'downloads', 'settings', 'power']
        for entry in entries:
            area = entry.find('s:rect[@data-role="shortcut-hit-area"]', NS)
            glyph = entry.find('s:g[@data-ui-symbol]', NS)
            assert float(area.get('width')) == float(area.get('height')) == 48
            assert glyph.get('fill') == 'none' and float(glyph.get('stroke-width')) == 1.75
            assert glyph.get('stroke-linecap') == glyph.get('stroke-linejoin') == 'round'
            assert glyph.get('transform').endswith('scale(1.0)')
        search = menu.find('s:rect[@data-role="search-field"]', NS)
        assert float(search.get('x')) == layout['railWidth'] + layout['contentMargin']
        assert float(search.get('y')) == layout['contentMargin']
        assert float(search.get('height')) == layout['searchHeight']
        assert search.get('fill') == VISUAL['themes'][theme]['searchBackground']
        toolbar = menu.findall('s:g[@data-toolbar]', NS)
        assert [entry.get('data-toolbar') for entry in toolbar] == ['apps', 'more', 'close']
        for entry in toolbar:
            area = entry.find('s:rect[@data-role="toolbar-hit-area"]', NS)
            assert float(area.get('width')) == float(area.get('height')) == layout['controlSize']
        assert all(float(row.get('data-row-height-dip')) == layout['catalogRowHeight']
                   for row in menu.findall('s:g[@data-catalog-app]', NS))
        print(f'{theme}: revision 2.2, 980x680 DIP panel; left/bottom 8 DIP; one border, circular avatar and compact toolbar')


if __name__ == '__main__':
    verify()
