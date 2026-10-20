"""Rebuild the vector identity and PNG exports with rsvg-convert (no dependencies)."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'brand'
OUT.mkdir(parents=True, exist_ok=True)

BLUE, CYAN, INK = '#368BFF', '#36D6CB', '#142138'

def mark(color=None):
    return ''.join(
        f'<rect x="{x}" y="{y}" width="{s}" height="{s}" rx="{r}" fill="{color or c}"/>'
        for x,y,s,r,c in [(16,132,108,20,BLUE),(140,76,76,14,CYAN),(184,12,48,10,BLUE)]
    )

def svg(body, w=256, h=256):
    return f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}">{body}</svg>'

def export(name, body, w=256, h=256, sizes=()):
    path = OUT / f'{name}.svg'
    path.write_text(svg(body,w,h))
    subprocess.run(['rsvg-convert',str(path),'-o',str(OUT/f'{name}.png')],check=True)
    for size in sizes:
        subprocess.run(['rsvg-convert','-w',str(size),'-h',str(size),str(path),'-o',str(OUT/f'{name}-{size}.png')],check=True)

def text(x,y,value,size=18,fill=INK,weight=400,extra=''):
    return f'<text x="{x}" y="{y}" font-family="Avenir Next, Arial, Hiragino Sans GB, sans-serif" font-size="{size}" font-weight="{weight}" fill="{fill}" {extra}>{value}</text>'

def rect(x,y,w,h,fill,rx=24,stroke='none'):
    return f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" fill="{fill}" stroke="{stroke}"/>'

def icon_at(x,y,s,color=None):
    return f'<g transform="translate({x} {y}) scale({s/256})">{mark(color)}</g>'

export('xdstarter-mark', mark(), sizes=(16,24,32,48,256,512,1024))
export('xdstarter-mark-mono-dark', mark(INK))
export('xdstarter-mark-mono-light', mark('#FFFFFF'))
export('xdstarter-app-icon', rect(0,0,256,256,INK,56)+icon_at(31,25,194), sizes=(16,24,32,48,256,512))
export('xdstarter-wordmark', icon_at(0,0,112)+text(138,76,'XDStarter',66,INK,600),600,112)

b = rect(0,0,1920,1080,'#F3F5F8',0)
b += text(80,80,'XDStarter',36,INK,600)
b += text(80,120,'展开的磁贴 · Windows 开始菜单新体验',20,'#64748B')
b += text(1840,82,'VISUAL IDENTITY / 01',15,'#64748B',500,'text-anchor="end" letter-spacing="2"')

# Primary dark lockup.
b += rect(80,166,1080,478,INK,28)
b += text(120,214,'PRIMARY IDENTITY',13,'#8CA0BE',600,'letter-spacing="2"')
b += icon_at(154,275,256)
b += text(456,424,'XDStarter',86,'#F4F7FB',600)
b += text(460,470,'每一次启动，都按你的习惯。',23,'#AABBD3')
b += text(120,610,'三枚磁贴 · 向上展开 · 自由组合',16,'#8CA0BE')

# App-icon presentation.
b += rect(1184,166,656,478,'#E8EDF4',28)
b += text(1224,214,'APPLICATION ICON',13,'#64748B',600,'letter-spacing="2"')
b += rect(1384,264,256,256,INK,56)
b += icon_at(1415,289,194)
b += text(1512,584,'清晰轮廓，适配任务栏与桌面',18,'#64748B',400,'text-anchor="middle"')

# Light/monochrome variants.
b += rect(80,668,1080,290,'#FFFFFF',28)
b += text(120,711,'LIGHT + MONOCHROME',13,'#64748B',600,'letter-spacing="2"')
b += icon_at(118,755,116)
b += text(258,827,'XDStarter',48,INK,600)
b += '<path d="M650 750V906" stroke="#E5EAF1"/>'
b += icon_at(702,762,110,INK)
b += rect(868,746,220,168,INK,20)
b += icon_at(924,774,110,'#FFFFFF')

# Actual pixel-sized marks (not enlarged).
b += rect(1184,668,656,290,'#FFFFFF',28)
b += text(1224,711,'SMALL-SIZE CHECK',13,'#64748B',600,'letter-spacing="2"')
for x,size in [(1250,16),(1370,24),(1490,32),(1620,48)]:
    b += icon_at(x+(48-size)/2,766+(48-size)/2,size)
    b += text(x+24,851,f'{size} px',16,'#64748B',400,'text-anchor="middle"')
b += text(1224,909,'透明标志 / 深浅单色 / 独立应用图标',17,'#64748B')

b += rect(80,994,22,22,BLUE,6)+text(114,1011,'#368BFF',15,'#64748B')
b += rect(260,994,22,22,CYAN,6)+text(294,1011,'#36D6CB',15,'#64748B')
b += rect(440,994,22,22,INK,6)+text(474,1011,'#142138',15,'#64748B')
b += text(1840,1011,'XDStarter / Brand concept / 2026',15,'#64748B',400,'text-anchor="end"')
export('xdstarter-brand-board',b,1920,1080)
