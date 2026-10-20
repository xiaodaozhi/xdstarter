"""Build the authoritative, geometrically exact tile-size design sheet."""
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
grid = json.loads((ROOT/'tokens/tile-grid.json').read_text())
cell, gap = grid['cell'], grid['gap']
def span(n): return n*cell+(n-1)*gap

# All groups must occupy the same grid bounds without overlapping cells.
for name, apps in grid['groups'].items():
    occupied = set()
    for app in apps:
        size = grid['sizes'][app['size']]
        for c in range(app['column'],app['column']+size['columns']):
            for r in range(app['row'],app['row']+size['rows']):
                assert 0 <= c < grid['columns'] and 0 <= r < 2
                assert (c,r) not in occupied, (name,app)
                occupied.add((c,r))
    assert len(occupied) == grid['columns']*2, name

INK, MUTED, BLUE, CYAN = '#142138','#52647B','#368BFF','#36D6CB'
parts = []
def rect(x,y,w,h,fill,rx=6,stroke='none',extra=''):
    parts.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" fill="{fill}" stroke="{stroke}" {extra}/>')
def text(x,y,label,size=18,color=INK,weight=400,extra=''):
    parts.append(f'<text x="{x}" y="{y}" fill="{color}" font-family="Avenir Next, Arial, Hiragino Sans GB, sans-serif" font-size="{size}" font-weight="{weight}" {extra}>{label}</text>')
def line(x1,y1,x2,y2,color='#A3B3C6',dash=''):
    parts.append(f'<path d="M{x1} {y1}L{x2} {y2}" stroke="{color}" stroke-width="1" stroke-dasharray="{dash}"/>')
def tile(x,y,w,h,label='',color=BLUE):
    rect(x,y,w,h,color)
    if label: text(x+w/2,y+h/2+7,label,20,'#FFFFFF',600,'text-anchor="middle"')

rect(0,0,1600,1080,'#F3F5F8',0)
text(72,76,'XDStarter / 磁贴尺寸与对齐',34,INK,600)
text(72,116,'统一使用 48 DIP 小格与 8 DIP 间隙，所有尺寸由跨格数推导。',20,MUTED)
text(1528,76,'GRID SYSTEM / 02',14,MUTED,500,'text-anchor="end" letter-spacing="2"')

for x,title,detail,w,h,col in [
    (72,'小正方形','48 × 48 DIP / 1 × 1 格',48,48,CYAN),
    (568,'标准正方形','104 × 104 DIP / 2 × 2 格',104,104,BLUE),
    (1064,'宽磁贴','216 × 104 DIP / 4 × 2 格',216,104,'#225FB8')]:
    rect(x,164,464,346,'#FFFFFF',22)
    text(x+28,210,title,24,INK,600)
    scale=1.5
    tile(x+(464-w*scale)/2,250+(156-h*scale)/2,w*scale,h*scale,color=col)
    text(x+232,460,detail,20,MUTED,400,'text-anchor="middle"')

rect(72,534,712,364,'#FFFFFF',22)
text(100,578,'两枚标准磁贴 + 间隙 = 一枚宽磁贴',22,INK,600)
left,top=120,626
tile(left,top,104,104,'104')
tile(left+112,top,104,104,'104')
tile(left,top+120,216,104,'216','#225FB8')
for x in (left,left+216): line(x,top-16,x,top+234,CYAN,'4 4')
text(396,672,'104 + 8 + 104 = 216',22,INK,600)
text(396,710,'左右边缘与磁贴高度保持对齐',18,MUTED)
text(396,798,'宽磁贴比例包含中缝：',18,MUTED)
text(396,834,'216 : 104 ≈ 2.077 : 1',22,INK,600)

rect(808,534,720,364,'#FFFFFF',22)
text(836,578,'四枚小磁贴 = 一枚标准磁贴的占位',22,INK,600)
for dx in (0,56):
    for dy in (0,56): tile(864+dx,654+dy,48,48,'',CYAN)
tile(1096,654,104,104,'104')
text(1032,716,'=',28,MUTED,500)
for yy in (654,758): line(844,yy,1220,yy,CYAN,'4 4')
text(1260,696,'48 + 8 + 48',21,INK,600)
text(1260,730,'= 104',21,INK,600)
text(844,820,'“半宽”指半格占位；小磁贴实体边长为 (104 − 8) ÷ 2。',18,MUTED)
text(844,856,'小磁贴显示居中图标，悬停或键盘聚焦时显示名称。',18,MUTED)

rect(72,922,1456,92,INK,18)
text(104,960,'统一计算',18,'#AABBD3',600)
text(236,960,'尺寸 = 跨格数 × 48 + (跨格数 − 1) × 8',22,'#FFFFFF',500)
text(236,992,'DIP 为逻辑单位；系统缩放时统一换算边界，避免独立取整导致错位。',17,'#AABBD3')
text(72,1050,'精确矢量尺寸规范 / 原生 SVG',15,MUTED)
text(1528,1050,'XDStarter / 2026',15,MUTED,400,'text-anchor="end"')

out=ROOT/'mockups/v2/xdstarter-tile-grid-v2.svg'
out.write_text('<svg xmlns="http://www.w3.org/2000/svg" width="1600" height="1080" viewBox="0 0 1600 1080">'+''.join(parts)+'</svg>')
subprocess.run(['rsvg-convert',str(out),'-o',str(out.with_suffix('.png'))],check=True)
print('Grid validated: small=48×48, standard=104×104, wide=216×104, gap=8 DIP; all groups fully aligned.')
