"""Build original transparent UI exports and the multi-frame Windows icon.

Vector geometry and ICO packaging use Python's standard library. Rasterization
is delegated to render_svg.py so the same renderer is used by design mockups.
Run from any directory; no output contains a machine-specific path.
"""
from pathlib import Path
import argparse
import hashlib
import json
import struct

from ui_symbols import (
    GLYPHS, THEME_COLORS, app_icon_svg, avatar_body, avatar_svg,
    brand_mark_svg, glyph_paths, glyph_svg,
)

DESIGN = Path(__file__).resolve().parents[1]
REPOSITORY = DESIGN.parents[1]
OUT = DESIGN / 'assets'
UI_SCALES = (1, 1.25, 1.5, 1.75, 2, 3)
TILE_SCALES = (1, 1.25, 1.5, 1.75, 2)
AVATAR_SIZES = (32, 40, 48, 56, 64, 96, 128, 192, 256)
APP_SIZES = (16, 20, 24, 32, 40, 48, 64, 96, 128, 256, 512, 1024)
ICO_SIZES = (16, 20, 24, 32, 40, 48, 64, 96, 128, 256)
TILE_COLORS = {'dark': '#263B55', 'light': '#E4EDF7'}


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def svg_document(body, width, height):
    return (
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
        f'viewBox="0 0 {width} {height}">{body}</svg>'
    )


def tile_svg(width, height, variant):
    """Neutral fallback surface only: application icons and labels are dynamic."""
    color = TILE_COLORS[variant]
    border = '#FFFFFF' if variant == 'dark' else '#8EA5C2'
    return svg_document(
        f'<rect x=".5" y=".5" width="{width - 1}" height="{height - 1}" rx="5.5" '
        f'fill="{color}" stroke="{border}" stroke-opacity=".18" stroke-width="1"/>',
        width, height,
    )


def write_ico(destination, frames):
    """Write standard ICONDIR/ICONDIRENTRY records containing RGBA PNG frames."""
    offset = 6 + 16 * len(frames)
    directory = bytearray(struct.pack('<HHH', 0, 1, len(frames)))
    payloads = []
    for size, png in frames:
        data = png.read_bytes()
        if data[:8] != b'\x89PNG\r\n\x1a\n' or struct.unpack('>II', data[16:24]) != (size, size):
            raise ValueError(f'Invalid ICO source frame: {png.name}')
        edge = 0 if size == 256 else size
        directory.extend(struct.pack('<BBBBHHII', edge, edge, 0, 0, 1, 32, len(data), offset))
        payloads.append(data)
        offset += len(data)
    destination.write_bytes(bytes(directory) + b''.join(payloads))


def overview_svg(grid):
    """A contact sheet; original-size files remain the authoritative exports."""
    body = '<rect width="1600" height="1580" fill="#EFF3F8"/>'

    def text(x, y, value, size=18, color='#33465F'):
        return f'<text x="{x}" y="{y}" font-family="Segoe UI" font-size="{size}" fill="{color}">{value}</text>'

    body += text(48, 56, 'XDStarter / DEVELOPMENT ASSETS', 28)
    body += text(48, 88, 'Original vectors + transparent PNGs. No third-party application artwork.', 17)
    for row, variant in enumerate(('dark', 'light')):
        y = 120 + row * 176
        card = '#182A40' if variant == 'dark' else '#FFFFFF'
        label = '#DCE6F4' if variant == 'dark' else '#33465F'
        body += f'<rect x="32" y="{y}" width="1536" height="156" rx="14" fill="{card}"/>'
        body += text(52, y + 32, f'{variant.upper()} / 24 DIP', 17, label)
        for index, name in enumerate(GLYPHS):
            x = 52 + index * (1496 / len(GLYPHS))
            body += (
                f'<g transform="translate({x + 29} {y + 51}) scale(2)" fill="none" '
                f'stroke="{THEME_COLORS[variant]}" stroke-width="1.75" stroke-linecap="round" '
                f'stroke-linejoin="round">{glyph_paths(name)}</g>'
            )
            body += text(x + 53, y + 130, name, 13, label).replace('font-size="13"', 'font-size="13" text-anchor="middle"')

    body += text(48, 510, 'CIRCULAR AVATAR / 32 DIP base / transparent outside the circle', 20)
    x = 48
    for index, size in enumerate(AVATAR_SIZES):
        body += f'<g transform="translate({x} {535 + (256 - size) / 2}) scale({size / 64})">{avatar_body("dark", f"sheet-avatar-{index}")}</g>'
        body += text(x + size / 2, 825, f'{size}px', 15).replace('font-size="15"', 'font-size="15" text-anchor="middle"')
        x += size + 30

    body += text(48, 892, 'WINDOWS APPLICATION ICON / 16 &amp; 20 px optical fits / ICO frames through 256 px', 20)
    x = 48
    for size in APP_SIZES:
        shown = min(size, 96)
        source = app_icon_svg(size, optical=True)
        content = source.split('>', 1)[1].rsplit('</svg>', 1)[0]
        viewbox = size if size in (16, 20) else 256
        body += f'<g transform="translate({x} {922 + (96 - shown) / 2}) scale({shown / viewbox})">{content}</g>'
        body += text(x + shown / 2, 1049, f'{size}px', 14).replace('font-size="14"', 'font-size="14" text-anchor="middle"')
        x += max(shown, 44) + 37
    body += text(48, 1091, 'Frames larger than 96 px are reduced on this overview; supplied files keep their stated dimensions.', 16)

    body += text(48, 1160, 'NEUTRAL TILE SURFACES / 48 x 48, 104 x 104, 216 x 104 DIP / radius 6 DIP', 20)
    for row, variant in enumerate(('dark', 'light')):
        y = 1192 + row * 165
        body += text(48, y + 20, variant.upper(), 16)
        x = 150
        for name, size in grid['sizes'].items():
            width = size['columns'] * grid['cell'] + (size['columns'] - 1) * grid['gap']
            height = size['rows'] * grid['cell'] + (size['rows'] - 1) * grid['gap']
            content = tile_svg(width, height, variant).split('>', 1)[1].rsplit('</svg>', 1)[0]
            body += f'<g transform="translate({x} {y})">{content}</g>'
            body += text(x, y + 132, f'{name}: {width} x {height}', 15)
            x += width + 110
    body += text(48, 1552, 'PNG scales: UI 100/125/150/175/200/300%; tiles 100/125/150/175/200%. QML should draw tile surfaces natively.', 16)
    return svg_document(body, 1600, 1580)


def build(svg_only=False):
    grid = json.loads((DESIGN / 'tokens/tile-grid.json').read_text(encoding='utf-8'))
    render = None
    if not svg_only:
        from render_svg import render_svg
        render = render_svg
    entries = []

    def record(path, *, dip, pixels, scale, variant, source, purpose, **extra):
        entries.append({
            'path': path.relative_to(OUT).as_posix(), 'format': path.suffix[1:],
            'dip': dip, 'pixels': pixels, 'scale': scale, 'variant': variant,
            'source': source, 'sha256': sha256(path), 'purpose': purpose, **extra,
        })

    def vector(path, content, *, dip, pixels, variant, source, purpose, **extra):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content + '\n', encoding='utf-8', newline='\n')
        record(path, dip=dip, pixels=pixels, scale=None, variant=variant, source=source, purpose=purpose, **extra)

    def raster(source, destination, width, height, *, dip, scale, variant, purpose, **extra):
        if render is None:
            return
        render(source, destination, width=width, height=height)
        record(destination, dip=dip, pixels=[width, height], scale=scale, variant=variant,
               source=source.relative_to(REPOSITORY).as_posix(), purpose=purpose, **extra)

    for variant in THEME_COLORS:
        for name in GLYPHS:
            path = OUT / 'ui' / variant / f'{name}.svg'
            vector(path, glyph_svg(name, THEME_COLORS[variant]), dip=[24, 24], pixels=[24, 24],
                   variant=variant, source='docs/design/source/ui_symbols.py:glyph_svg', purpose='界面操作图标')
            for scale in UI_SCALES:
                size = round(24 * scale)
                raster(path, path.with_name(f'{name}-{size}.png'), size, size, dip=[24, 24], scale=scale,
                       variant=variant, purpose='24 DIP 操作图标的对应像素导出')
        path = OUT / 'ui' / variant / 'avatar.svg'
        vector(path, avatar_svg(64, variant), dip=[32, 32], pixels=[64, 64], variant=variant,
               source='docs/design/source/ui_symbols.py:avatar_svg', purpose='32 DIP 圆形占位头像；真实头像同样圆形裁切')
        for size in AVATAR_SIZES:
            raster(path, path.with_name(f'avatar-{size}.png'), size, size, dip=[32, 32], scale=size / 32,
                   variant=variant, purpose='圆形占位头像的对应像素导出')

    for variant in TILE_COLORS:
        for name, size in grid['sizes'].items():
            width = size['columns'] * grid['cell'] + (size['columns'] - 1) * grid['gap']
            height = size['rows'] * grid['cell'] + (size['rows'] - 1) * grid['gap']
            path = OUT / 'tiles' / variant / f'{name}.svg'
            vector(path, tile_svg(width, height, variant), dip=[width, height], pixels=[width, height],
                   variant=variant, source='docs/design/source/build_development_assets.py:tile_svg + docs/design/tokens/tile-grid.json',
                   purpose='无图标、无文字的磁贴底图；QML 推荐按 token 原生绘制', cornerRadiusDip=6)
            for scale in TILE_SCALES:
                width_px, height_px = round(width * scale), round(height * scale)
                raster(path, path.with_name(f'{name}-{width_px}x{height_px}.png'), width_px, height_px,
                       dip=[width, height], scale=scale, variant=variant,
                       purpose='中性磁贴底图位图参考；动态内容由运行时绘制', cornerRadiusDip=6)

    app = OUT / 'app'
    icon_source = app / 'xdstarter-app-icon.svg'
    vector(icon_source, app_icon_svg(), dip=None, pixels=[256, 256], variant='brand',
           source='docs/design/source/ui_symbols.py:app_icon_svg', purpose='Windows 应用图标矢量母版')
    mark_source = app / 'xdstarter-mark.svg'
    vector(mark_source, brand_mark_svg(), dip=None, pixels=[256, 256], variant='brand',
           source='docs/design/source/ui_symbols.py:brand_mark_svg', purpose='透明三阶梯品牌标志')
    for size in APP_SIZES:
        source = icon_source
        if size in (16, 20):
            source = app / f'xdstarter-app-icon-{size}.svg'
            vector(source, app_icon_svg(size, optical=True), dip=None, pixels=[size, size], variant='brand',
                   source='docs/design/source/ui_symbols.py:app_icon_svg', purpose='小尺寸应用图标光学校正源', opticalFit=True)
        raster(source, app / f'xdstarter-app-icon-{size}.png', size, size, dip=None, scale=None,
               variant='brand', purpose='Windows 应用图标像素尺寸', opticalFit=size in (16, 20))
        raster(mark_source, app / f'xdstarter-mark-{size}.png', size, size, dip=None, scale=None,
               variant='brand', purpose='透明品牌标志像素尺寸')
    if not svg_only:
        ico = app / 'xdstarter.ico'
        write_ico(ico, [(size, app / f'xdstarter-app-icon-{size}.png') for size in ICO_SIZES])
        record(ico, dip=None, pixels=[[size, size] for size in ICO_SIZES], scale=None, variant='brand',
               source='docs/design/assets/app/xdstarter-app-icon-{size}.png',
               purpose='Windows 多帧应用图标；每帧为 32 位 RGBA PNG', frames=list(ICO_SIZES))

    sheet = OUT / 'asset-overview.svg'
    vector(sheet, overview_svg(grid), dip=None, pixels=[1600, 1580], variant='overview',
           source='docs/design/source/build_development_assets.py:overview_svg', purpose='开发素材总览；大尺寸图标在总览中缩小显示')
    raster(sheet, sheet.with_suffix('.png'), 1600, 1580, dip=None, scale=None,
           variant='overview', purpose='开发素材总览预览；不用于界面切图')

    sources = (
        'build_development_assets.py', 'ui_symbols.py', 'render_svg.py',
        'renderer/main.cpp', 'renderer/CMakeLists.txt',
    )
    manifest = {
        'schemaVersion': 1, 'complete': not svg_only,
        'description': '原创界面图标、圆形占位头像、中性磁贴底图与三阶梯品牌应用图标。',
        'baseDip': {'uiGlyph': 24, 'avatar': 32},
        'scales': {'uiGlyph': list(UI_SCALES), 'tiles': list(TILE_SCALES)},
        'avatarPixels': list(AVATAR_SIZES), 'applicationIconPixels': list(APP_SIZES),
        'icoFrames': list(ICO_SIZES),
        'rasterization': {
            'baseline': 'Qt 6.8.2 QSvgRenderer', 'mode': 'offscreen',
            'entryPoint': 'docs/design/source/render_svg.py',
        },
        'notes': [
            'SVG 矢量可缩放；pixels 记录源画布，不表示 SVG 固定物理分辨率。',
            'Windows 应用图标按物理像素选择帧；dip/scale 为 null，不绑定单一界面 DIP。',
            '所有 UI、头像、品牌与磁贴 PNG 的画布外围透明；素材总览含实色背景。',
            '界面 glyph 统一 24 单位 viewBox、1.75 单位圆头圆角描边。',
            '不分发字体或第三方示意应用图标；运行时从系统应用目录加载真实应用图标。',
            '磁贴底图为参考/位图降级素材；QML 应优先按共享 token 原生绘制，保持动态主题与缩放。',
        ],
        'generatorSources': [
            {'path': (Path('docs/design/source') / name).as_posix(), 'sha256': sha256(DESIGN / 'source' / name)}
            for name in sources
        ] + [{'path': 'docs/design/tokens/tile-grid.json', 'sha256': sha256(DESIGN / 'tokens/tile-grid.json')}],
        'assets': entries,
    }
    (OUT / 'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8', newline='\n')
    print(f'Built {len(entries)} development assets ({"SVG only" if svg_only else "SVG + PNG + ICO"}).')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--svg-only', action='store_true', help='Export vectors without a rasterizer; manifest is marked incomplete.')
    build(parser.parse_args().svg_only)
