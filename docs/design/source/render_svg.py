"""Render editable SVG sources with the project's exact Qt SVG implementation.

The helper runs without a desktop window. No fonts, Qt libraries or executable
are copied into the asset delivery. A locally installed rsvg-convert can also
be selected explicitly for historical export comparisons.
"""
import os
from pathlib import Path
import shutil
import subprocess
import sys

REPOSITORY = Path(__file__).resolve().parents[3]


def render_svg(source, destination, width=None, height=None):
    source, destination = Path(source), Path(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    requested = os.environ.get('XDSTARTER_SVG_RENDERER', '')
    renderer_name = 'xdstarter_render_svg.exe' if os.name == 'nt' else 'xdstarter_render_svg'
    program = Path(requested) if requested else REPOSITORY / 'build/app-release/bin' / renderer_name
    environment = os.environ.copy()
    if (width is None) != (height is None):
        raise ValueError('Pass both width and height')
    if program.is_file() and not program.name.startswith('rsvg-convert'):
        command = [str(program), str(source), str(destination)]
        if width is not None or height is not None:
            command += [str(width), str(height)]
        qt = Path(environment.get('XDSTARTER_QT_ROOT', REPOSITORY / 'build/Qt/6.8.2/msvc2022_64'))
        environment['PATH'] = str(qt / 'bin') + os.pathsep + environment.get('PATH', '')
        environment['QT_QPA_PLATFORM'] = 'offscreen'
        # Font loading uses the host's installed fonts; no font files are bundled.
        if environment.get('WINDIR'):
            environment['QT_QPA_FONTDIR'] = str(Path(environment['WINDIR']) / 'Fonts')
    else:
        rsvg = shutil.which(requested or 'rsvg-convert')
        if not rsvg:
            raise RuntimeError('Build xdstarter_render_svg first: cmake --build build/app-release '
                               '--target xdstarter_render_svg; or set XDSTARTER_SVG_RENDERER to rsvg-convert')
        command = [rsvg, str(source), '-o', str(destination)]
        if width is not None:
            command += ['-w', str(width), '-h', str(height)]
    subprocess.run(command, env=environment, check=True, capture_output=True, text=True, encoding='utf-8')


if __name__ == '__main__':
    if len(sys.argv) not in (3, 5):
        raise SystemExit('Usage: render_svg.py input.svg output.png [width height]')
    render_svg(*sys.argv[1:3], *(int(value) for value in sys.argv[3:]))
