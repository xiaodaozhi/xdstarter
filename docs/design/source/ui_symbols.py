"""Shared, original vector symbols for mockups and development exports.

All interface glyphs use a 24 by 24 viewBox. Callers that embed glyph_paths
must provide the same rounded 1.75-unit stroke used by glyph_svg.
"""
from html import escape

GLYPHS = (
    'documents', 'downloads', 'settings', 'power', 'search',
    'chevron-down', 'more', 'add', 'close', 'pin', 'unpin', 'apps',
)
STROKE_WIDTH = 1.75
THEME_COLORS = {'dark': '#DCE6F4', 'light': '#33465F'}
BLUE, CYAN, INK = '#368BFF', '#36D6CB', '#142138'

_PATHS = {
    'documents': (
        '<path d="M8 3.5h6.5l4 4V19a1.5 1.5 0 0 1-1.5 1.5H8A1.5 1.5 0 0 1 6.5 19V5A1.5 1.5 0 0 1 8 3.5Z"/>'
        '<path d="M14.5 3.5V8h4M9.5 11.5h6M9.5 15h6"/>'
        '<path d="M3.5 7v12a4 4 0 0 0 4 4"/>'
    ),
    'downloads': (
        '<path d="M12 3.5v11m-4-4 4 4 4-4M4.5 15.5V19A1.5 1.5 0 0 0 6 20.5h12a1.5 1.5 0 0 0 1.5-1.5v-3.5"/>'
    ),
    'settings': (
        '<path d="m9.5 3.5-.6 2-1.7 1-2-.5-2.5 4.3 1.5 1.5v2l-1.5 1.5 2.5 4.3 2-.5 1.7 1 .6 2h5l.6-2 1.7-1 2 .5 2.5-4.3-1.5-1.5v-2l1.5-1.5L18.8 6l-2 .5-1.7-1-.6-2Z"/>'
        '<circle cx="12" cy="12.7" r="3.25"/>'
    ),
    'power': '<path d="M12 2.75v8.5M7 5.75a8 8 0 1 0 10 0"/>',
    'search': '<circle cx="10.5" cy="10.5" r="6.25"/><path d="m15 15 5 5"/>',
    'chevron-down': '<path d="m6.5 9.25 5.5 5.5 5.5-5.5"/>',
    'more': '<circle cx="5" cy="12" r=".8"/><circle cx="12" cy="12" r=".8"/><circle cx="19" cy="12" r=".8"/>',
    'add': '<path d="M12 5v14M5 12h14"/>',
    'close': '<path d="m6 6 12 12M18 6 6 18"/>',
    'pin': '<path d="m9 3.5 7.5 0-1 6 3 3v1.5h-13v-1.5l3-3-1-6ZM12 14v7"/>',
    'unpin': '<path d="M16.5 3.5h-8l1 4M16.5 3.5l-1 6 3 3v1.5h-6M8 10l-2.5 2.5V14h3M12 17v4M3 3l18 18"/>',
    'apps': (
        '<rect x="4" y="4" width="6" height="6" rx="1.25"/>'
        '<rect x="14" y="4" width="6" height="6" rx="1.25"/>'
        '<rect x="4" y="14" width="6" height="6" rx="1.25"/>'
        '<rect x="14" y="14" width="6" height="6" rx="1.25"/>'
    ),
}


def glyph_paths(name):
    """Return geometry only, for embedding in the shared 24-unit coordinate space."""
    return _PATHS[name]


def glyph_svg(name, color=THEME_COLORS['dark'], size=24):
    """Return a transparent, font-independent standalone interface icon."""
    return (
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{size}" height="{size}" viewBox="0 0 24 24">'
        f'<g fill="none" stroke="{escape(color, quote=True)}" stroke-width="{STROKE_WIDTH}" '
        f'stroke-linecap="round" stroke-linejoin="round">{glyph_paths(name)}</g></svg>'
    )


def avatar_body(variant='dark', clip_id='avatar-circle'):
    """Return circular avatar geometry in a 64-unit coordinate space.

    A circular alpha mask includes the background, head and shoulders. Qt 6.8
    supports masks; its SVG renderer does not support the SVG clipPath element.
    No rounded-square background or font glyph is used.
    """
    colors = {
        'dark': ('#244C80', '#C5E6FF', '#7CB7F3', '#83BFF2'),
        'light': ('#D8E9FF', '#3B6A9C', '#6C9ED5', '#AACCEF'),
    }
    background, head, shoulders, ring = colors[variant]
    clip_id = escape(clip_id, quote=True)
    return (
        f'<defs><mask id="{clip_id}" maskUnits="userSpaceOnUse" x="0" y="0" width="64" height="64">'
        '<circle cx="32" cy="32" r="32" fill="#FFFFFF"/></mask></defs>'
        f'<g mask="url(#{clip_id})">'
        f'<circle cx="32" cy="32" r="32" fill="{background}"/>'
        f'<circle cx="32" cy="24" r="10" fill="{head}"/>'
        f'<path d="M10 64v-5c0-13 9-21 22-21s22 8 22 21v5Z" fill="{shoulders}"/>'
        '</g>'
        f'<circle cx="32" cy="32" r="31.25" fill="none" stroke="{ring}" stroke-width="1.5"/>'
    )


def avatar_svg(size=64, variant='dark'):
    return (
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{size}" height="{size}" viewBox="0 0 64 64">'
        f'{avatar_body(variant)}</svg>'
    )


def brand_mark(color=None, optical_size=None):
    """The approved three ascending rounded tiles, in the original 256-unit grid."""
    del optical_size  # Brand geometry stays canonical; tiny app icons use a separate optical fit.
    return ''.join(
        f'<rect x="{x}" y="{y}" width="{side}" height="{side}" rx="{radius}" fill="{color or fill}"/>'
        for x, y, side, radius, fill in (
            (16, 132, 108, 20, BLUE), (140, 76, 76, 14, CYAN), (184, 12, 48, 10, BLUE),
        )
    )


def brand_mark_svg(size=256):
    return (
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{size}" height="{size}" viewBox="0 0 256 256">'
        f'{brand_mark()}</svg>'
    )


def app_icon_svg(size=256, optical=False):
    """Approved rounded-square app icon; 16/20 px fit to the physical pixel grid."""
    if optical and size in (16, 20):
        if size == 16:
            tiles = ((3, 9, 5, 1, BLUE), (9, 6, 4, 1, CYAN), (12, 2, 2, .5, BLUE))
            radius = 3.5
        else:
            tiles = ((3, 11, 7, 1.25, BLUE), (11, 7, 5, 1, CYAN), (14, 3, 3, .75, BLUE))
            radius = 4.25
        body = f'<rect width="{size}" height="{size}" rx="{radius}" fill="{INK}"/>'
        body += ''.join(
            f'<rect x="{x}" y="{y}" width="{side}" height="{side}" rx="{corner}" fill="{fill}"/>'
            for x, y, side, corner, fill in tiles
        )
        viewbox = size
    else:
        body = f'<rect width="256" height="256" rx="56" fill="{INK}"/>'
        body += f'<g transform="translate(31 25) scale({194 / 256})">{brand_mark()}</g>'
        viewbox = 256
    return (
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{size}" height="{size}" viewBox="0 0 {viewbox} {viewbox}">'
        f'{body}</svg>'
    )
