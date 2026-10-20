# XDStarter v2: tile-grid revision

Tool: built-in image_gen.imagegen. The authoritative size sheet is native SVG rendered by rsvg-convert.

## Tile reflow

Use case: ui-mockup, precise layout edit.
Edit the supplied dark XDStarter Windows desktop mockup to correct its TILE GEOMETRY. Preserve the wallpaper, glass menu, search, rail, all-app list, taskbar, colors, Chinese and typography. Reflow ONLY the right tile region.
CRITICAL REQUIREMENT: all ordinary tiles must be geometrically exact SQUARES; wide tiles must have the width of TWO standard squares PLUS the gap and the same height as ONE standard square. Add half-grid SMALL SQUARE tiles. Do not create vertically stretched tiles or arbitrary-width rectangles.
Logical sizes in DIP: small=48x48, standard=104x104, wide=216x104, gap=8. Medium=2*small+gap, wide=2*medium+gap. The screenshot shows these at 150%: SMALL=72x72 pixels, STANDARD=156x156, WIDE=324x156, GAP=12. Small tiles arranged 2x2 occupy exactly the same outer bounds as a standard tile, including interior gaps.
If rendering at 1920x1080, use the existing tile area's left x=546 and right x=1374. All rows are 828px wide and use the same 12px gaps:
工作 group, tile top y=238: Word standard at x546; Excel standard at x714; PowerPoint standard at x882; VS Code WIDE at x1050.
日常 group, tile top y=492: Edge WIDE at x546; 微信 standard at x882; 照片 standard at x1050; a 2x2 quartet of small tiles at x1218,y492 / x1302,y492 / x1218,y576 / x1302,y576. The quartet has centered Calculator, Clock, Notepad, Settings icons, NO visible text labels.
娱乐 group, tile top y=740: Steam WIDE at x546; Spotify standard at x882; VLC standard at x1050; Terminal standard at x1218.
The rows' outer right edges must align exactly. Keep all three Chinese group headings 工作 日常 娱乐 above their rows; ellipses align at right. Keep the original app names, real brand icons, white labels near bottom left of medium and wide tiles, centered icons in tiny tiles. White text contrast must remain high, using rich dark brand backgrounds.
Maintain 6px tile corners and deliberate grid alignment, no overlap, no distorted icons. The entire screenshot composition must remain the same, 16:9, requested export 1920x1080. No dimension annotations in this screenshot.

## Square geometry correction

undefined
