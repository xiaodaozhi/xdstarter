"""Validate export dimensions, hashes, transparency, circular avatars and ICO frames.

The verifier only uses the standard library and never launches a renderer or UI.
"""
from pathlib import Path
import hashlib
import json
import math
import struct
import xml.etree.ElementTree as ET
import zlib

from build_development_assets import (
    APP_SIZES, AVATAR_SIZES, ICO_SIZES, OUT, REPOSITORY, TILE_SCALES, UI_SCALES,
)
from ui_symbols import GLYPHS


def require(condition, message):
    if not condition:
        raise ValueError(message)


def read_png(data):
    """Decode the renderer's non-interlaced, eight-bit RGBA PNG, checking CRCs."""
    require(data[:8] == b'\x89PNG\r\n\x1a\n', 'Invalid PNG signature')
    position, compressed, header, ended = 8, bytearray(), None, False
    while position < len(data):
        require(position + 12 <= len(data), 'Truncated PNG chunk')
        length = struct.unpack_from('>I', data, position)[0]
        end = position + 12 + length
        require(end <= len(data), 'Truncated PNG payload')
        tag = data[position + 4:position + 8]
        payload = data[position + 8:position + 8 + length]
        crc = struct.unpack_from('>I', data, position + 8 + length)[0]
        require(zlib.crc32(tag + payload) & 0xFFFFFFFF == crc, 'PNG CRC mismatch')
        if tag == b'IHDR':
            require(header is None and length == 13, 'Invalid PNG IHDR')
            header = struct.unpack('>IIBBBBB', payload)
        elif tag == b'IDAT':
            compressed.extend(payload)
        elif tag == b'IEND':
            require(length == 0 and end == len(data), 'Invalid PNG IEND')
            ended = True
        position = end
    require(header is not None and ended, 'Incomplete PNG')
    width, height, depth, kind, compression, filtering, interlace = header
    require(depth == 8 and kind == 6 and compression == filtering == interlace == 0,
            'Expected a non-interlaced eight-bit RGBA PNG')
    stride = width * 4
    raw = zlib.decompress(compressed)
    require(len(raw) == height * (stride + 1), 'PNG decompressed length mismatch')
    previous = bytearray(stride)
    rows = []
    for y in range(height):
        start = y * (stride + 1)
        mode, row = raw[start], bytearray(raw[start + 1:start + stride + 1])
        require(mode in range(5), 'Unknown PNG filter')
        for index in range(stride):
            left = row[index - 4] if index >= 4 else 0
            above = previous[index]
            upper_left = previous[index - 4] if index >= 4 else 0
            if mode == 1:
                predictor = left
            elif mode == 2:
                predictor = above
            elif mode == 3:
                predictor = (left + above) // 2
            elif mode == 4:
                estimate = left + above - upper_left
                distances = (abs(estimate - left), abs(estimate - above), abs(estimate - upper_left))
                predictor = (left, above, upper_left)[distances.index(min(distances))]
            else:
                predictor = 0
            row[index] = (row[index] + predictor) & 255
        rows.append(row)
        previous = row
    return width, height, rows


def verify_ico(path, expected_sizes):
    data = path.read_bytes()
    require(len(data) >= 6, 'ICO header missing')
    reserved, kind, count = struct.unpack_from('<HHH', data)
    require((reserved, kind, count) == (0, 1, len(expected_sizes)), 'ICO directory does not match manifest')
    offset = 6 + count * 16
    for index, size in enumerate(expected_sizes):
        width, height, colors, reserved, planes, bits, length, payload_offset = struct.unpack_from('<BBBBHHII', data, 6 + index * 16)
        require((width or 256, height or 256) == (size, size), f'ICO frame {index}: dimensions mismatch')
        require((colors, reserved, planes, bits) == (0, 0, 1, 32), f'ICO frame {size}: invalid directory flags')
        require(payload_offset == offset and length > 0 and offset + length <= len(data), f'ICO frame {size}: invalid payload range')
        payload = data[offset:offset + length]
        require(payload == (path.parent / f'xdstarter-app-icon-{size}.png').read_bytes(), f'ICO frame {size}: payload differs from PNG')
        require(read_png(payload)[:2] == (size, size), f'ICO frame {size}: invalid PNG dimensions')
        offset += length
    require(offset == len(data), 'Unexpected trailing data in ICO')


def verify():
    manifest = json.loads((OUT / 'manifest.json').read_text(encoding='utf-8'))
    require(manifest['schemaVersion'] == 1 and manifest['complete'], 'Development export is incomplete')
    require(manifest['baseDip'] == {'uiGlyph': 24, 'avatar': 32}, 'Incorrect DIP baselines')
    require(manifest['applicationIconPixels'] == list(APP_SIZES), 'Application PNG size coverage mismatch')
    require(manifest['avatarPixels'] == list(AVATAR_SIZES), 'Avatar size coverage mismatch')
    require(manifest['icoFrames'] == list(ICO_SIZES), 'ICO frame coverage mismatch')
    for source in manifest['generatorSources']:
        content = (REPOSITORY / source['path']).read_bytes().replace(b'\r\n', b'\n')
        require(hashlib.sha256(content).hexdigest() == source['sha256'], f'Stale generator source: {source["path"]}')
    assets = manifest['assets']
    paths = [asset['path'] for asset in assets]
    require(len(paths) == len(set(paths)), 'Duplicate manifest path')
    manifest_paths = set(paths)
    for variant in ('dark', 'light'):
        for name in GLYPHS:
            require(f'ui/{variant}/{name}.svg' in manifest_paths, f'Missing SVG: {variant}/{name}')
            for scale in UI_SCALES:
                require(f'ui/{variant}/{name}-{round(24 * scale)}.png' in manifest_paths, 'Missing UI DPI scale')
        for size in AVATAR_SIZES:
            require(f'ui/{variant}/avatar-{size}.png' in manifest_paths, 'Missing avatar scale')
    png_count, svg_count, avatar_count = 0, 0, 0
    tile_grid = json.loads((REPOSITORY / 'docs/design/tokens/tile-grid.json').read_text(encoding='utf-8'))
    for variant in ('dark', 'light'):
        for name, size in tile_grid['sizes'].items():
            width = size['columns'] * tile_grid['cell'] + (size['columns'] - 1) * tile_grid['gap']
            height = size['rows'] * tile_grid['cell'] + (size['rows'] - 1) * tile_grid['gap']
            for scale in TILE_SCALES:
                expected = f'tiles/{variant}/{name}-{round(width * scale)}x{round(height * scale)}.png'
                require(expected in manifest_paths, f'Missing tile scale: {expected}')
    for asset in assets:
        path = OUT / asset['path']
        require(path.resolve().is_relative_to(OUT.resolve()) and path.is_file(), f'Invalid asset path: {asset["path"]}')
        data = path.read_bytes()
        require(hashlib.sha256(data).hexdigest() == asset['sha256'], f'Asset hash mismatch: {asset["path"]}')
        if asset['format'] == 'svg':
            root = ET.fromstring(data)
            require(root.tag == '{http://www.w3.org/2000/svg}svg', f'Invalid SVG root: {path.name}')
            require(not any(node.tag.rsplit('}', 1)[-1] in ('image', 'script', 'foreignObject') for node in root.iter()),
                    f'SVG must remain original vectors: {path.name}')
            if path.name == 'avatar.svg':
                masks = root.findall('.//{http://www.w3.org/2000/svg}mask')
                require(len(masks) == 1, 'Avatar must have exactly one circular mask')
                circle = masks[0].find('{http://www.w3.org/2000/svg}circle')
                require(circle is not None and [circle.get(key) for key in ('cx', 'cy', 'r')] == ['32', '32', '32'],
                        'Avatar circle mask differs from source geometry')
            svg_count += 1
        elif asset['format'] == 'png':
            width, height, rows = read_png(data)
            require([width, height] == asset['pixels'], f'PNG dimensions mismatch: {path.name}')
            if asset['variant'] != 'overview':
                alpha = [row[index] for row in rows for index in range(3, len(row), 4)]
                require(min(alpha) == 0 and max(alpha) == 255, f'Expected transparent and opaque pixels: {path.name}')
                require(all(rows[y][x * 4 + 3] == 0 for x, y in ((0, 0), (width - 1, 0), (0, height - 1), (width - 1, height - 1))),
                        f'Expected transparent corners: {path.name}')
            if path.name.startswith('avatar-'):
                radius = width / 2
                require(width == height, 'Avatar must be square')
                for y, row in enumerate(rows):
                    for x in range(width):
                        if math.hypot(x + .5 - radius, y + .5 - radius) > radius + 1:
                            require(row[x * 4 + 3] == 0, f'Avatar has pixels outside circular boundary: {path.name}')
                avatar_count += 1
            if asset['dip'] is not None and asset['scale'] is not None:
                require([round(edge * asset['scale']) for edge in asset['dip']] == [width, height],
                        f'DIP/scale metadata mismatch: {path.name}')
            png_count += 1
        elif asset['format'] == 'ico':
            verify_ico(path, ICO_SIZES)
        else:
            raise ValueError(f'Unsupported asset type: {path.name}')
    require(avatar_count == 2 * len(AVATAR_SIZES), 'Circular avatar coverage mismatch')
    require('asset-overview.png' in manifest_paths and 'asset-overview.svg' in manifest_paths, 'Overview missing')
    require('app/xdstarter.ico' in manifest_paths, 'Windows ICO missing')
    print(f'PASS: {len(assets)} manifest entries; {svg_count} SVG, {png_count} RGBA PNG, {avatar_count} circular avatars, {len(ICO_SIZES)} ICO frames.')
    print('Dimensions, DIP scales, source/asset SHA-256, transparent corners and exact ICO payloads match.')


if __name__ == '__main__':
    verify()
