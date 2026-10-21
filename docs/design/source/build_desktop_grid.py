# docs/design/source/build_desktop_grid.py

"""Native SVG mockups: both themes share the exact tile-grid.json geometry."""
import json
from pathlib import Path
from html import escape
from render_svg import render_svg
from ui_symbols import STROKE_WIDTH, THEME_COLORS, avatar_body, brand_mark, glyph_paths

ROOT = Path(__file__).resolve().parents[1]
GRID = json.loads((ROOT/'tokens/tile-grid.json').read_text())
VISUAL = json.loads((ROOT/'tokens/visual.json').read_text())
COLORS = {'Word':'#174DA3','Excel':'#14613D','PowerPoint':'#973F20','VS Code':'#123A68',
          'Edge':'#105779','WeChat':'#145F43','Photos':'#263D94','Steam':'#153B67',
          'Spotify':'#145636','VLC':'#914711','Terminal':'#35475C',
          'Calculator':'#175E85','Clock':'#35475C','Notepad':'#145D80','Settings':'#35475C'}
LABELS = {'WeChat':'微信','Photos':'照片'}

# Desktop context uses physical pixels; the complete menu is authored in DIP.
# Menu left and bottom gap are independent of the illustrated Start button.
SCALE = VISUAL['referenceCanvas']['scale']
CANVAS_WIDTH = VISUAL['referenceCanvas']['width']
CANVAS_HEIGHT = VISUAL['referenceCanvas']['height']
START_X = 24
TASKBAR_HEIGHT = VISUAL['referenceCanvas']['taskbarHeightDip'] * SCALE
TASKBAR_TOP = CANVAS_HEIGHT - TASKBAR_HEIGHT
MENU_LEFT = VISUAL['menuChrome']['screenGapDip'] * SCALE
MENU_WIDTH = VISUAL['defaultMenuSizeDip']['width']
MENU_HEIGHT = VISUAL['defaultMenuSizeDip']['height']
MENU_TOP = TASKBAR_TOP - VISUAL['menuChrome']['screenGapDip'] * SCALE - MENU_HEIGHT * SCALE


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

def tile_color(identity):
    """Match the bounded unsigned UTF-16 identity hash used by QML."""
    value = 5381
    encoded = identity.encode('utf-16-le')[:512]
    for offset in range(0, len(encoded), 2):
        value = ((value * 33) ^ int.from_bytes(encoded[offset:offset + 2], 'little')) & 0xffffffff
    return VISUAL['tilePalette'][value % len(VISUAL['tilePalette'])]


def shade(color, factor):
    """The approved palette stays below clipping when made 1.05 times lighter."""
    return '#' + ''.join(f'{min(255, int(int(color[offset:offset + 2], 16) * factor + .5)):02X}'
                         for offset in (1, 3, 5))


def build(theme):
    light = theme == 'light'
    colors = VISUAL['themes'][theme]
    fg, muted = colors['foreground'], colors['secondary']
    layout = VISUAL['layout']
    margin, rail = layout['contentMargin'], layout['railWidth']
    content_left = rail + margin
    content_right = MENU_WIDTH - margin
    body_top = margin + layout['searchHeight'] + layout['contentSpacing']
    tiles_left = content_left + layout['catalogWidth'] + layout['contentSpacing']
    radius = VISUAL['cornerRadiusDip']['menu']
    p = f'''<defs>
    <linearGradient id="desktop" x2="1" y2="1"><stop stop-color="{'#D4E8FF' if light else '#11284C'}"/><stop offset="1" stop-color="{'#91B9EF' if light else '#050E20'}"/></linearGradient>
    <linearGradient id="fold" x2=".6" y2="1"><stop stop-color="{'#EDF6FF' if light else '#275BBB'}"/><stop offset="1" stop-color="{'#78A5E2' if light else '#0A244C'}"/></linearGradient>
    </defs>'''
    p += r(0, 0, CANVAS_WIDTH, CANVAS_HEIGHT, 'url(#desktop)', 0)
    for d in ['M780-100C1610 40 1300 370 2040 440L2040 780C1310 570 1470 130 780-100Z',
              'M1260-100C2050 130 1240 450 1940 600L1940 850C1230 650 1900 280 1260-100Z',
              'M890 1300C1440 660 1680 570 2070 760L2070 970C1620 780 1480 980 1270 1300Z']:
        p += path(d, 'url(#fold)', '#BBD6FA' if light else '#23559A', 1)
    # Bounded concentric shadow, no platform frame, blur material or SVG filter dependency.
    for inset in range(24, 0, -1):
        p += r(MENU_LEFT - inset * SCALE, MENU_TOP - inset * SCALE,
               (MENU_WIDTH + inset * 2) * SCALE, (MENU_HEIGHT + inset * 2) * SCALE,
               '#071321', (radius + inset) * SCALE,
               f'data-role="menu-shadow" fill-opacity="{0.004 if light else 0.007}"')
    p += (f'<g id="menu-window" data-left-px="{MENU_LEFT:g}" data-left-dip="8" '
          f'data-bottom-gap-dip="8" transform="translate({MENU_LEFT:g} {MENU_TOP:g}) scale({SCALE})">')
    p += r(0, 0, MENU_WIDTH, MENU_HEIGHT, colors['opaqueFallback'], radius,
           'data-role="menu-surface"')
    # A single inset outline remains the only visible border; the rail follows the same corner path.
    p += path(f'M{radius} 0H{rail}V{MENU_HEIGHT}H{radius}Q0 {MENU_HEIGHT} 0 {MENU_HEIGHT-radius}V{radius}Q0 0 {radius} 0Z',
              colors['surface'])
    p += path(f'M{rail} 16V{MENU_HEIGHT-16}', 'none', colors['separator'], .7)
    p += r(.5, .5, MENU_WIDTH - 1, MENU_HEIGHT - 1, 'none', radius - .5,
           f'data-role="menu-border" stroke="{colors["menuBorder"]}" stroke-width="1"')
    p += '<g data-role="user-avatar" data-size-dip="32" transform="translate(16 24) scale(.5)">'
    p += '<title>用户账户：圆形默认头像</title>' + avatar_body(theme, 'menu-avatar-circle') + '</g>'
    for index, (name, label) in enumerate([('documents', '文档'), ('downloads', '下载'),
                                            ('settings', '设置'), ('power', '电源与退出')]):
        top = MENU_HEIGHT - 16 - (4 - index) * 56
        p += f'<g data-shortcut="{name}" data-hit-size-dip="48" role="button" aria-label="{label}"><title>{label}</title>'
        p += r(8, top, 48, 48, colors['hover'], 8,
               'data-role="shortcut-hit-area" fill-opacity="0"')
        p += ui_glyph(name, 20, top + 12, 24, THEME_COLORS[theme]) + '</g>'
    search_width = content_right - content_left - 3 * layout['controlSize'] - 3 * GRID['gap']
    p += r(content_left, margin, search_width, layout['searchHeight'], colors['searchBackground'], 8,
           f'data-role="search-field" stroke="{colors["controlBorder"]}" stroke-width="1"')
    p += ui_glyph('search', content_left + 12, margin + 10, 20, muted)
    p += t(content_left + 40, margin + 26, '搜索应用、拼音或首字母', VISUAL['font']['body'], muted)
    for index, (name, label) in enumerate([('apps', '编辑布局'), ('more', '更多操作'), ('close', '收起菜单')]):
        x = content_left + search_width + GRID['gap'] + index * (layout['controlSize'] + GRID['gap'])
        p += f'<g data-toolbar="{name}" role="button" aria-label="{label}"><title>{label}</title>'
        p += r(x, margin + 2, layout['controlSize'], layout['controlSize'], colors['hover'], 8,
               'data-role="toolbar-hit-area" fill-opacity="0"')
        p += ui_glyph(name, x + 6, margin + 8, 24, fg) + '</g>'
    p += t(content_left, body_top + 22, '所有应用', VISUAL['font']['heading'], fg, 600)
    refresh_left = content_left + layout['catalogWidth'] - 28
    p += t(refresh_left - 8, body_top + 21, 'A–Z', 12, muted, 400, 'text-anchor="end"')
    p += '<g data-catalog-action="refresh" role="button" aria-label="刷新应用目录"><title>刷新应用目录</title>'
    p += r(refresh_left, body_top + 2, 28, 28, colors['hover'], 6,
           'data-role="catalog-refresh-hit-area" fill-opacity="0"')
    p += t(refresh_left + 14, body_top + 23, '↻', 20, fg, 400, 'text-anchor="middle"') + '</g>'
    rows = [('B', 'Blender', 'Blender'), ('C', 'Chrome', 'Chrome'), ('D', 'Discord', 'Discord'),
            ('E', 'Edge', 'Microsoft Edge'), ('F', 'Figma', 'Figma'),
            ('M', 'Excel', 'Microsoft Excel'), ('', 'Word', 'Microsoft Word'), ('V', 'VS Code', 'VS Code')]
    row_top = body_top + layout['headerHeight'] + GRID['gap']
    for letter, key, label in rows:
        if letter:
            p += t(content_left + 6, row_top + 13, letter, VISUAL['font']['section'], muted, 600)
            row_top += layout['catalogSectionHeight']
        p += f'<g data-catalog-app="{key}" data-row-height-dip="{layout["catalogRowHeight"]}">'
        p += r(content_left, row_top, layout['catalogWidth'], layout['catalogRowHeight'], colors['hover'], 6,
               'fill-opacity="0"')
        p += icon(key, content_left + 6, row_top + 6, 32)
        p += t(content_left + 46, row_top + 27, label, VISUAL['font']['body'], fg)
        p += '</g>'
        row_top += layout['catalogRowHeight']
    group_top = body_top
    for group, title in [('work', '工作'), ('daily', '日常'), ('entertainment', '娱乐')]:
        p += t(tiles_left, group_top + 22, title, VISUAL['font']['heading'], fg, 600)
        grid_top = group_top + layout['headerHeight'] + GRID['gap']
        p += f'<g data-group="{group}" transform="translate({tiles_left} {grid_top})">'
        occupied_height = 0
        for index, app in enumerate(GRID['groups'][group]):
            span = GRID['sizes'][app['size']]
            x = app['column'] * (GRID['cell'] + GRID['gap'])
            y = app['row'] * (GRID['cell'] + GRID['gap'])
            w = span['columns'] * (GRID['cell'] + GRID['gap']) - GRID['gap']
            h = span['rows'] * (GRID['cell'] + GRID['gap']) - GRID['gap']
            occupied_height = max(occupied_height, y + h)
            name = app['app']
            p += f'<g data-app="{name}" data-size="{app["size"]}" data-palette-identity="{name}">'
            base = tile_color(name)
            gradient = f'tile-{group}-{index}'
            p += (f'<defs><linearGradient id="{gradient}" x1="0" y1="0" x2="0" y2="1">'
                  f'<stop stop-color="{shade(base, 1.05)}"/>'
                  f'<stop offset="1" stop-color="{shade(base, 1 / 1.04)}"/></linearGradient></defs>')
            p += r(x, y, w, h, f'url(#{gradient})', VISUAL['cornerRadiusDip']['tile'],
                   f'data-base-color="{base}" stroke="#FFFFFF" stroke-opacity=".12" stroke-width=".7"')
            if app['size'] == 'small':
                p += icon(name, x + 10, y + 10, 28)
            else:
                p += icon(name, x + (w - 36) / 2, y + 14, 36)
                # QML uses a 10 DIP bounding-box bottom inset; SVG positions the font baseline separately.
                p += t(x + 10, y + h - 14, LABELS.get(name, name), VISUAL['tileLabelFontDip'], '#FFFFFF')
            p += '</g>'
        p += '</g>'
        group_top = grid_top + occupied_height + layout['groupSpacing']
    p += '</g>'
    p += r(0, TASKBAR_TOP, CANVAS_WIDTH, TASKBAR_HEIGHT, '#E4F0FC' if light else '#112440', 0)
    p += f'<g id="start-button" data-left-px="{START_X}" data-hit-size-dip="48"><title>原生开始按钮示意；位置不作为菜单锚点</title>'
    p += r(START_X, TASKBAR_TOP, 48 * SCALE, 48 * SCALE, '#C2D9F5' if light else '#284769', 10,
           'data-role="start-hit-area" fill-opacity=".65"')
    p += f'<g transform="translate({START_X + 18} {TASKBAR_TOP + 18}) scale({36/256})">{brand_mark()}</g></g>'
    p += ui_glyph('search', 114, TASKBAR_TOP + 18, 36, fg)
    p += icon('Folder', 170, TASKBAR_TOP + 14, 44)
    p += icon('Edge', 238, TASKBAR_TOP + 14, 44) + icon('VS Code', 306, TASKBAR_TOP + 14, 44)
    p += path(f'M1688 {TASKBAR_TOP+40}L1696 {TASKBAR_TOP+32} 1704 {TASKBAR_TOP+40}', 'none', fg, 2)
    p += t(1885, TASKBAR_TOP + 34, '09:41', 17, fg, 400, 'text-anchor="end"')
    p += t(1885, TASKBAR_TOP + 57, '2026/10/21', 16, fg, 400, 'text-anchor="end"')
    out = ROOT / f'mockups/v2/xdstarter-desktop-{theme}-v2.svg'
    out.write_text(f'<svg xmlns="http://www.w3.org/2000/svg" width="{CANVAS_WIDTH}" height="{CANVAS_HEIGHT}" '
                   f'viewBox="0 0 {CANVAS_WIDTH} {CANVAS_HEIGHT}" data-design-revision="{VISUAL["designRevision"]}" '
                   f'data-reference-scale="{SCALE}"><g font-family="Segoe UI Variable">' + p + '</g></svg>', encoding='utf-8')
    render_svg(out, out.with_suffix('.png'))
    print(f'{theme}: revision 2.2; 980x680 DIP panel, symmetric 8 DIP gap; shared 10-column grid')


if __name__ == '__main__':
    for variant in ('dark', 'light'):
        build(variant)
