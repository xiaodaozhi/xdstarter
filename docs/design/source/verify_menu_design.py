"""Check actual menu/button alignment and sidebar geometry in both SVG exports."""
from pathlib import Path
import re
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
NS = {'s': 'http://www.w3.org/2000/svg'}


def verify():
    for theme in ('dark', 'light'):
        svg = ET.parse(ROOT / f'mockups/v2/xdstarter-desktop-{theme}-v2.svg').getroot()
        assert svg.get('data-design-revision') == '2.1'
        scale = float(svg.get('data-reference-scale'))
        menu = svg.find('.//s:g[@id="menu-window"]', NS)
        button = svg.find('.//s:g[@id="start-button"]', NS)
        frame = menu.find('s:rect[@data-role="menu-border"]', NS)
        hit = button.find('s:rect[@data-role="start-hit-area"]', NS)
        translation = re.fullmatch(r'translate\(([-\d.]+) 0\)', menu.get('transform'))
        actual_menu_left = float(frame.get('x')) + float(translation[1])
        assert actual_menu_left == float(hit.get('x')) == 24, 'Visible menu and Start hit area are misaligned'
        assert float(hit.get('width')) == float(hit.get('height')) == 48 * scale
        assert float(hit.get('y')) + float(hit.get('height')) == 1080
        avatar = menu.find('s:g[@data-role="user-avatar"]', NS)
        assert float(avatar.get('data-size-dip')) == 32
        mask = avatar.find('.//s:mask', NS)
        circle = mask.find('s:circle', NS)
        assert [float(circle.get(key)) for key in ('cx', 'cy', 'r')] == [32, 32, 32]
        assert avatar.find(f'.//s:g[@mask="url(#{mask.get("id")})"]', NS) is not None
        entries = menu.findall('s:g[@data-shortcut]', NS)
        assert [entry.get('data-shortcut') for entry in entries] == ['documents', 'downloads', 'settings', 'power']
        for entry in entries:
            area = entry.find('s:rect[@data-role="shortcut-hit-area"]', NS)
            glyph = entry.find('s:g[@data-ui-symbol]', NS)
            assert float(area.get('width')) == float(area.get('height')) == 48 * scale
            assert glyph.get('fill') == 'none' and float(glyph.get('stroke-width')) == 1.75
            assert glyph.get('stroke-linecap') == glyph.get('stroke-linejoin') == 'round'
            assert glyph.get('transform').endswith('scale(1.5)')
        print(f'{theme}: menu/Start left=24px; 48 DIP hit areas, 24 DIP glyphs and circular 32 DIP avatar')


if __name__ == '__main__':
    verify()
