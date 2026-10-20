"""Verify exported SVG bounds, app placements, gaps, and PNG canvas sizes."""
import json
from pathlib import Path
import struct
import xml.etree.ElementTree as ET

ROOT=Path(__file__).resolve().parents[1]
grid=json.loads((ROOT/'tokens/tile-grid.json').read_text())
ns={'s':'http://www.w3.org/2000/svg'}
groups=[]
for theme in ('dark','light'):
    svg=ET.parse(ROOT/f'mockups/v2/xdstarter-desktop-{theme}-v2.svg').getroot()
    for group,apps in grid['groups'].items():
        node=svg.find(f'.//s:g[@data-group="{group}"]',ns)
        for app in apps:
            el=node.find(f's:g[@data-app="{app["app"]}"]',ns)
            rect=el.find('s:rect',ns)
            span=grid['sizes'][app['size']]
            w=span['columns']*(grid['cell']+grid['gap'])-grid['gap']
            h=span['rows']*(grid['cell']+grid['gap'])-grid['gap']
            assert float(rect.attrib['width'])==w and float(rect.attrib['height'])==h
            assert float(rect.attrib['x'])==app['column']*(grid['cell']+grid['gap'])
            assert float(rect.attrib['y'])==app['row']*(grid['cell']+grid['gap'])
            if app['size']!='wide':assert w==h
            else:assert w==2*h+grid['gap']
    groups.append([ET.tostring(el) for el in svg.findall('.//s:g[@data-group]',ns)])
    png=(ROOT/f'mockups/v2/xdstarter-desktop-{theme}-v2.png').read_bytes()
    assert png[:8]==b'\x89PNG\r\n\x1a\n'
    assert struct.unpack('>II',png[16:24])==(1920,1080)
    print(f'{theme}: all 15 tile bounds and placements correct, 1920x1080 PNG')
assert groups[0]==groups[1], 'Theme tile geometry/content differs'
print('Both themes have identical tile geometry and content.')
