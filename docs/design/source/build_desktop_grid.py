"""Native SVG mockups: both themes share the exact tile-grid.json geometry."""
import json
from pathlib import Path
from html import escape
from render_svg import render_svg
from ui_symbols import STROKE_WIDTH, THEME_COLORS, avatar_body, brand_mark, glyph_paths

ROOT = Path(__file__).resolve().parents[1]
GRID = json.loads((ROOT/'tokens/tile-grid.json').read_text())
COLORS = {'Word':'#174DA3','Excel':'#14613D','PowerPoint':'#973F20','VS Code':'#123A68',
          'Edge':'#105779','WeChat':'#145F43','Photos':'#263D94','Steam':'#153B67',
          'Spotify':'#145636','VLC':'#914711','Terminal':'#35475C',
          'Calculator':'#175E85','Clock':'#35475C','Notepad':'#145D80','Settings':'#35475C'}
LABELS = {'WeChat':'微信','Photos':'照片'}

# The 1920x1080 reference canvas represents 150% UI scaling.
# Menu border and Start button hit area share a 16 DIP / 24 px left anchor.
SCALE = 1.5
START_X = 24
TASKBAR_TOP = 1008


def ui_glyph(name, x, y, size=36, color='#FFFFFF'):
    return (f'<g data-ui-symbol="{name}" transform="translate({x} {y}) scale({size / 24})" '
            f'fill="none" stroke="{color}" stroke-width="{STROKE_WIDTH}" '
            f'stroke-linecap="round" stroke-linejoin="round">{glyph_paths(name)}</g>')

def r(x,y,w,h,fill,rx=6,extra=''):
    return f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" fill="{fill}" {extra}/>'
def t(x,y,s,size=20,fill='#FFFFFF',weight=400,extra=''):
    return f'<text x="{x}" y="{y}" font-size="{size}" fill="{fill}" font-weight="{weight}" {extra}>{escape(s)}</text>'
def path(d,fill='none',stroke='none',sw=2,extra=''):
    return f'<path d="{d}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}" stroke-linecap="round" stroke-linejoin="round" {extra}/>'
def circle(x,y,rad,fill,extra=''):
    return f'<circle cx="{x}" cy="{y}" r="{rad}" fill="{fill}" {extra}/>'

def icon(name,x,y,size,color='#FFFFFF'):
    p=''
    if name in ('Word','Excel','PowerPoint'):
        base={'Word':'#287AE6','Excel':'#1FB774','PowerPoint':'#F58054'}[name]
        p=r(17,5,43,51,base,5)+r(17,31,43,25,'#000000',0,'opacity=".12"')
        p+=r(3,16,34,34,COLORS[name],4)+t(20,41,{'Word':'W','Excel':'X','PowerPoint':'P'}[name],27,'#FFFFFF',600,'text-anchor="middle"')
    elif name=='VS Code':
        p=path('M47 4L62 10V54L47 60 18 39 7 48 1 43 14 32 1 21 7 16 18 25Z','#27B9F4')
        p+=path('M47 16L27 32 47 48Z',COLORS[name])
    elif name=='Edge':
        p=circle(32,32,29,'#13BFAD')+path('M4 33C4 6 50 3 60 26C47 14 20 20 19 38C17 52 42 61 59 41C52 70 7 69 4 43C2 33 12 25 32 26C8 28 17 49 39 43C18 45 17 24 4 33Z','#1485EA')
        p+=path('M32 7C52 4 66 22 60 37C58 20 42 20 28 28C24 22 24 14 32 7Z','#43E8B7')
    elif name=='WeChat':
        p='<ellipse cx="25" cy="25" rx="23" ry="19" fill="#82E5AD"/>'+path('M12 39L9 49 22 42Z','#82E5AD')
        p+='<ellipse cx="44" cy="39" rx="19" ry="16" fill="white"/>'+path('M49 52L57 60 54 49Z','#FFFFFF')
        p+=''.join(circle(a,b,2.2,'#1D744D') for a,b in [(17,21),(32,21),(38,36),(51,36)])
    elif name=='Photos':
        p=r(4,7,56,50,'#5AA2FF',8)+circle(45,21,6,'#FFFFFF')+path('M4 49L24 28 40 46 49 37 60 50V57H4Z','#2454C8')
    elif name=='Spotify':
        p=circle(32,32,30,'#1EEA73')
        for yy,dd in [(23,0),(33,4),(43,8)]:
            p+=path(f'M{14+dd/2} {yy}Q32 {yy-7} {51-dd/2} {yy+3}','none','#103C2A',5-dd/5)
    elif name=='Steam':
        p=circle(32,32,31,'#092B50')+path('M1 37L24 47 43 25','none','#FFFFFF',10)
        p+=circle(45,22,14,'#FFFFFF')+circle(45,22,9,'#092B50')+circle(45,22,6,'#FFFFFF')+circle(24,47,9,'#FFFFFF')+circle(24,47,5,'#092B50')
    elif name=='VLC':
        p=path('M27 4H37L55 58H9Z','#FF9E1B')+path('M23 19H41L44 28H20Z','#FFFFFF')+path('M16 40H48L51 49H13Z','#FFFFFF')+r(6,55,52,7,'#FFB22C',3)
    elif name=='Terminal':
        p=r(2,9,60,46,'#121C29',5)+r(2,9,60,10,'#8FA2B4',4)
        p+=path('M13 29L23 37 13 45M31 45H45','none','#FFFFFF',4)
    elif name=='Calculator':
        p=r(12,4,40,56,'none',4,f'stroke="{color}" stroke-width="3"')+r(18,11,28,12,color,1)
        for xx in (18,29,40):
            for yy in (30,41,52): p+=r(xx,yy,6,6,color,1)
    elif name=='Download':
        p=path('M32 7V43M18 30L32 44 46 30M11 50V57H53V50','none',color,3)
    elif name=='Clock':
        p=circle(32,32,26,'none',f'stroke="{color}" stroke-width="3"')+path('M32 13V33L45 41','none',color,3)
    elif name=='Notepad':
        p=r(12,7,40,50,'none',4,f'stroke="{color}" stroke-width="3"')
        for yy in (20,30,40): p+=path(f'M21 {yy}H43','none',color,3)
        for xx in (22,42):p+=path(f'M{xx} 3V13','none',color,3)
    elif name=='Settings':
        p=circle(32,32,18,'none',f'stroke="{color}" stroke-width="7"')+circle(32,32,6,color)
        for a in range(0,360,45):p+=path('M32 5V12','none',color,7,f'transform="rotate({a} 32 32)"')
    elif name=='Folder':
        p=path('M4 16H25L30 22H60V54H4Z','#F8C955')+path('M4 29H60L55 58H4Z','#F8AF36')
    elif name=='Chrome':
        p=circle(32,32,29,'#E34D42')+path('M32 32L57 18C72 40 48 69 29 61Z','#F5CA42')+path('M32 32L29 61C3 63-8 23 12 11Z','#37A666')+circle(32,32,14,'#F1F7FC')+circle(32,32,10,'#3C8AE0')
    elif name=='Figma':
        p=r(16,3,17,20,'#F66D56',9)+r(31,3,17,20,'#FF9A8B',9)+r(16,22,17,20,'#AA69F5',9)+circle(40,32,10,'#27BEF6')+r(16,41,17,20,'#30CE9F',9)
    elif name=='Discord':
        p=circle(32,32,30,'#6374EC')+path('M15 23Q32 15 49 23L54 43 44 47 40 42H24L20 47 10 43Z','#FFFFFF')+circle(25,32,3,'#6374EC')+circle(40,32,3,'#6374EC')
    elif name=='Blender':
        p=path('M5 15L28 24 13 35H3L24 18 13 9','none','#F58B24',7)+circle(38,34,20,'#F58B24')+circle(38,34,13,'#FFFFFF')+circle(38,34,8,'#258AC2')
    else: p=circle(32,32,26,'#368BFF')+t(32,42,name[:1],28,'#FFFFFF',600,'text-anchor="middle"')
    return f'<g transform="translate({x} {y}) scale({size/64})">{p}</g>'

def build(theme):
    light=theme=='light'
    fg='#172338' if light else '#F4F7FB'
    muted='#596A82' if light else '#ACBBD0'
    border='#CDDAEB' if light else '#46617F'
    p=f'''<defs>
    <linearGradient id="desktop" x2="1" y2="1"><stop stop-color="{'#D4E8FF' if light else '#11284C'}"/><stop offset="1" stop-color="{'#91B9EF' if light else '#050E20'}"/></linearGradient>
    <linearGradient id="fold" x2=".6" y2="1"><stop stop-color="{'#EDF6FF' if light else '#275BBB'}"/><stop offset="1" stop-color="{'#78A5E2' if light else '#0A244C'}"/></linearGradient>
    <linearGradient id="panel" x2="1" y2="1"><stop stop-color="{'#F5FAFF' if light else '#21334A'}"/><stop offset="1" stop-color="{'#E3EFFC' if light else '#111F32'}"/></linearGradient>
    <linearGradient id="tile" x2="0" y2="1"><stop stop-color="white" stop-opacity=".05"/><stop offset="1" stop-color="black" stop-opacity=".06"/></linearGradient>
    <filter id="shadow" x="-10%" y="-10%" width="125%" height="130%"><feGaussianBlur in="SourceAlpha" stdDeviation="15" result="blur"/><feOffset in="blur" dy="12" result="offset"/><feFlood flood-color="#061224" flood-opacity="{'0.12' if light else '0.35'}" result="color"/><feComposite in="color" in2="offset" operator="in" result="shade"/><feMerge><feMergeNode in="shade"/><feMergeNode in="SourceGraphic"/></feMerge></filter>
    </defs>'''
    p+=r(0,0,1920,1080,'url(#desktop)',0)
    for d in ['M780-100C1610 40 1300 370 2040 440L2040 780C1310 570 1470 130 780-100Z',
              'M1260-100C2050 130 1240 450 1940 600L1940 850C1230 650 1900 280 1260-100Z',
              'M890 1180C1440 540 1680 450 2070 640L2070 850C1620 660 1480 860 1270 1180Z']:
        p+=path(d,'url(#fold)', '#BBD6FA' if light else '#23559A',1)
    p+=f'<g id="menu-window" data-left-px="{START_X}" data-left-dip="{START_X / SCALE:g}" transform="translate({START_X - 44} 0)">'
    p+=r(44,88,1362,904,'url(#panel)',20,f'data-role="menu-border" stroke="{border}" filter="url(#shadow)"')
    p+=path('M140 88V992M516 164V992','none',border,1,'opacity=".35"')
    p+='<g data-role="user-avatar" data-size-dip="32" transform="translate(68 120) scale(.75)">'
    p+='<title>用户账户：圆形默认头像</title>'+avatar_body(theme, 'menu-avatar-circle')+'</g>'
    p+=r(176,110,1200,54,'#FFFFFF' if light else '#263B55',16,f'stroke="{border}" fill-opacity=".5"')
    p+=circle(205,135,9,'none',f'stroke="{muted}" stroke-width="2"')+path('M212 142L219 149','none',muted,2)
    p+=t(236,146,'搜索应用或快捷方式',21,muted)
    p+=t(176,219,'所有应用',24,fg,600)+t(399,217,'A–Z  ⌄',18,muted)
    rows=[('B','Blender',300),('C','Chrome',392),('D','Discord',484),('E','Edge',576),('F','Figma',668),('M','Microsoft Excel',793),('','Microsoft Word',852),('V','VS Code',947)]
    for letter,label,yy in rows:
        if letter:p+=t(178,yy-37,letter,18,muted)
        key={'Microsoft Excel':'Excel','Microsoft Word':'Word'}.get(label,label)
        p+=icon(key,178,yy-22,42)+t(244,yy+8,label,21,fg)
    for name, label, center_y in [('documents','文档',736),('downloads','下载',808),
                                  ('settings','设置',880),('power','电源选项',952)]:
        p+=f'<g data-shortcut="{name}" data-hit-size-dip="48" role="button" aria-label="{label}"><title>{label}</title>'
        p+=r(56,center_y-36,72,72,'#D7E7FA' if light else '#294462',12,
             f'data-role="shortcut-hit-area" fill-opacity="{0.5 if name == "documents" else 0}"')
        p+=ui_glyph(name,74,center_y-18,36,fg if name == 'documents' else THEME_COLORS[theme])+'</g>'

    for group,title,top in [('work','工作',238),('daily','日常',492),('entertainment','娱乐',740)]:
        p+=t(546,top-22,title,24,fg,600)+t(1355,top-24,'···',25,muted)
        # Geometry is in DIP, rendered at 150% without per-tile rounding.
        p+=f'<g data-group="{group}" transform="translate(546 {top}) scale(1.5)">'
        for app in GRID['groups'][group]:
            size=GRID['sizes'][app['size']]
            x=app['column']*(GRID['cell']+GRID['gap'])
            y=app['row']*(GRID['cell']+GRID['gap'])
            w=size['columns']*GRID['cell']+(size['columns']-1)*GRID['gap']
            h=size['rows']*GRID['cell']+(size['rows']-1)*GRID['gap']
            name=app['app']
            p+=f'<g data-app="{name}" data-size="{app["size"]}">'
            p+=r(x,y,w,h,COLORS[name],6,'stroke="#FFFFFF" stroke-opacity=".12" stroke-width=".7"')+r(x,y,w,h,'url(#tile)')
            if app['size']=='small':p+=icon(name,x+9,y+9,30)
            else:
                p+=icon(name,x+(w-50)/2,y+15,50)
                p+=t(x+10,y+h-12,LABELS.get(name,name),14,'#FFFFFF',400)
            p+='</g>'
        p+='</g>'
    p+=r(1237,935,138,44,'none',22,f'stroke="{border}"')+t(1254,964,'＋ 添加应用',18,fg)
    p+='</g>'
    p+=r(0,TASKBAR_TOP,1920,72,'#E4F0FC' if light else '#112440',0,'fill-opacity=".95"')
    p+=f'<g id="start-button" data-left-px="{START_X}" data-left-dip="16" data-hit-size-dip="48"><title>XDStarter 开始按钮</title>'
    p+=r(START_X,TASKBAR_TOP,72,72,'#C2D9F5' if light else '#284769',10,
         'data-role="start-hit-area" fill-opacity=".65"')
    p+=f'<g transform="translate(42 1026) scale({36/256})">{brand_mark()}</g></g>'
    p+=ui_glyph('search',114,1026,36,fg)
    p+=icon('Folder',170,1022,44)+icon('Edge',238,1022,44)+icon('VS Code',306,1022,44)
    p+=path('M1688 1048L1696 1040 1704 1048M1725 1043Q1737 1032 1749 1043M1730 1048Q1737 1042 1744 1048','none',fg,2)
    p+=circle(1737,1054,2,fg)+path('M1765 1043H1771L1779 1036V1058L1771 1051H1765ZM1786 1040Q1796 1048 1786 1056','none',fg,2)
    p+=t(1885,1044,'09:41',17,fg,400,'text-anchor="end"')+t(1885,1067,'2026/10/20',16,fg,400,'text-anchor="end"')
    out=ROOT/f'mockups/v2/xdstarter-desktop-{theme}-v2.svg'
    out.write_text('<svg xmlns="http://www.w3.org/2000/svg" width="1920" height="1080" viewBox="0 0 1920 1080" data-design-revision="2.1" data-reference-scale="1.5"><g font-family="Segoe UI">'+p+'</g></svg>', encoding='utf-8')
    render_svg(out,out.with_suffix('.png'))
    print(f'{theme}: revision 2.1, aligned Start anchor, circular avatar, exact shared grid')

for theme in ('dark','light'):build(theme)
