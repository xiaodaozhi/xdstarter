"""Dependency-free repository/document/design checks; no Qt build is performed."""
from pathlib import Path
import ast
import json
import re
import runpy
import struct
import subprocess
import sys
from urllib.parse import unquote, urlsplit
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
EXCLUDED = {'.git', 'artifacts', 'build', '.venv', '__pycache__', '.codex', '.agents'}
FILES = [p for p in ROOT.rglob('*') if p.is_file() and not EXCLUDED.intersection(p.relative_to(ROOT).parts)]
errors = []

def require(ok, message):
    if not ok:
        errors.append(message)

def local_link(source, target):
    target = target.strip().split(' "', 1)[0].strip('<>')
    parsed = urlsplit(target)
    if parsed.scheme or parsed.netloc or not parsed.path:
        return
    candidate = (ROOT / parsed.path.lstrip('/')) if parsed.path.startswith('/') else source.parent / unquote(parsed.path)
    resolved = candidate.resolve()
    require(resolved.is_relative_to(ROOT), f'{source.relative_to(ROOT)}: link escapes repository: {target}')
    require(resolved.exists(), f'{source.relative_to(ROOT)}: missing local target: {target}')

for file in FILES:
    suffix = file.suffix.lower()
    try:
        if suffix == '.md':
            text = file.read_text(encoding='utf-8')
            text = re.sub(r'```.*?```', '', text, flags=re.S)
            for target in re.findall(r'\[[^\]]*\]\(([^)]+)\)', text):
                local_link(file, target)
            for target in re.findall(r'<img\b[^>]*\bsrc=[\"\']([^\"\']+)', text):
                local_link(file, target)
            require(not re.search(r'/(?:Users|home)/[^/\s]+/', text), f'{file.relative_to(ROOT)}: machine-local path in documentation')
        elif suffix in ('.json', '.yml', '.yaml'):
            # GitHub YAML files intentionally use JSON syntax (a YAML subset).
            json.loads(file.read_text(encoding='utf-8'))
        elif suffix == '.svg':
            ET.parse(file)
        elif suffix == '.png':
            header = file.read_bytes()[:33]
            require(header[:8] == b'\x89PNG\r\n\x1a\n', f'{file}: invalid PNG')
            require(header[12:16] == b'IHDR', f'{file}: missing PNG IHDR')
        elif suffix == '.py':
            ast.parse(file.read_text(encoding='utf-8'), filename=str(file))
    except (ValueError, OSError, ET.ParseError, SyntaxError) as error:
        errors.append(f'{file.relative_to(ROOT)}: {error}')

def png_size(path):
    return struct.unpack('>II', path.read_bytes()[16:24])

design = ROOT / 'docs/design'
for name in ('dark', 'light'):
    require(png_size(design / f'mockups/v2/xdstarter-desktop-{name}-v2.png') == (1920, 1200), f'{name}: incorrect canvas')
require(png_size(design / 'mockups/v2/xdstarter-tile-grid-v2.png') == (1600, 1080), 'grid: incorrect canvas')
require(png_size(design / 'brand/xdstarter-brand-board.png') == (1920, 1080), 'brand: incorrect canvas')

decoder = runpy.run_path(str(design / 'source/verify_assets.py'))['read_png']
for size in (16, 24, 32, 48, 256, 512, 1024):
    path = design / f'brand/xdstarter-mark-{size}.png'
    require(png_size(path) == (size, size), f'{path.name}: incorrect dimensions')
    require(path.read_bytes()[25] == 6, f'{path.name}: missing RGBA channel')
# Inspect actual transparency in a small rendered export, not just the SVG definition.
w, h, channels, rows = decoder(design / 'brand/xdstarter-mark-48.png')
alpha = [row[i] for row in rows for i in range(3, len(row), 4)]
require(channels == 4 and min(alpha) == 0 and max(alpha) == 255, 'logo: expected transparent and opaque pixels')

config = json.loads((ROOT / '.github/project-bootstrap.json').read_text(encoding='utf-8'))
label_names = {label['name'] for label in config['labels']}
milestones = {milestone['title'] for milestone in config['milestones']}
keys = [issue['key'] for issue in config['seedIssues']]
require(len(keys) == len(set(keys)), 'bootstrap: duplicate seed keys')
for issue in config['seedIssues']:
    require(set(issue['labels']) <= label_names, f'{issue["key"]}: undefined label')
    require(issue['milestone'] in milestones, f'{issue["key"]}: undefined milestone')
for file in (ROOT / '.github/ISSUE_TEMPLATE').glob('*.yml'):
    form = json.loads(file.read_text(encoding='utf-8'))
    if file.name == 'config.yml':
        continue
    require(bool(form.get('name') and form.get('description') and form.get('body')), f'{file.name}: missing form fields')
    require(set(form.get('labels', [])) <= label_names, f'{file.name}: undefined labels')
    ids = [field['id'] for field in form['body'] if 'id' in field]
    require(len(ids) == len(set(ids)), f'{file.name}: duplicate field id')

if errors:
    print('\n'.join(errors), file=sys.stderr)
    sys.exit(1)
subprocess.run([sys.executable, str(design / 'source/verify_tile_grid.py')], check=True)
subprocess.run([sys.executable, str(design / 'source/verify_menu_design.py')], check=True)
subprocess.run([sys.executable, str(design / 'source/verify_development_assets.py')], check=True)
subprocess.run([sys.executable, str(ROOT / 'scripts/check_pinyin_data.py')], check=True)
print(f'PASS: {len(FILES)} repository files; local links, formats, current canvases, logo alpha, forms and shared tile geometry.')
print('Qt application builds and Windows integration tests are not part of this check.')
