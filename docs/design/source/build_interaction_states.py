# docs/design/source/build_interaction_states.py

"""Export editable interaction boards using the runtime tokens and vector icons."""
import json
from html import escape
from pathlib import Path
from render_svg import render_svg
from ui_symbols import glyph_paths

DESIGN = Path(__file__).resolve().parents[1]
TOKENS = json.loads((DESIGN / 'tokens/visual.json').read_text(encoding='utf-8'))


def build(theme):
    colors = TOKENS['themes'][theme]
    parts = []

    def rect(x, y, width, height, color, radius=10, stroke='none'):
        parts.append(f'<rect x="{x}" y="{y}" width="{width}" height="{height}" rx="{radius}" fill="{color}" stroke="{stroke}"/>')

    def text(x, y, value, size=14, color=None, weight=400, anchor='start'):
        parts.append(f'<text x="{x}" y="{y}" text-anchor="{anchor}" fill="{color or colors["foreground"]}" font-size="{size}" font-weight="{weight}">{escape(value)}</text>')

    def icon(name, x, y, color=None, size=20):
        parts.append(f'<g transform="translate({x} {y}) scale({size/24})" fill="none" stroke="{color or colors["foreground"]}" stroke-width="1.75" stroke-linecap="round" stroke-linejoin="round">{glyph_paths(name)}</g>')

    rect(0, 0, 1440, 1040, colors['opaqueFallback'], 0)
    text(48, 64, 'XDStarter / 交互表面', 30, weight=600)
    text(48, 96, 'v2.3 · 共享运行时 tokens · 100% DIP · 默认支持编辑布局', color=colors['secondary'])
    text(48, 154, '右键与下拉菜单', 18, weight=600)
    for col, state in enumerate(['默认', '悬停', '按下', 'Tab 焦点']):
        x = 48 + col * 292
        text(x, 188, state, 12, colors['secondary'])
        # A restrained elevation separates menus from their backdrop.
        for spread, opacity in [(7, '.035'), (3, '.06')]:
            parts.append(f'<rect x="{x-spread}" y="{204-spread}" width="{264+spread*2}" height="{282+spread*2}" rx="{10+spread}" fill="#071321" opacity="{opacity}"/>')
        rect(x, 204, 264, 282, colors['surface'], stroke=colors['controlBorder'])
        if col:
            rect(x+6, 210, 252, 36, colors['pressed'] if col == 2 else colors['hover'], 5, TOKENS['brand']['blue'] if col == 3 else 'none')
        for row, label in enumerate(['修改显示名称', '恢复原名称', '小磁贴 · 1×1', '标准磁贴 · 2×2', '宽磁贴 · 4×2', '取消固定']):
            y = 234 + row * 42
            text(x+18, y, label, color=colors['secondary'] if row == 1 else None)
            if row == 3:
                text(x+236, y, '✓')
        rect(x+12, 291, 240, 1, colors['separator'], 0)
    text(48, 538, '浮动反馈与延迟提示', 18, weight=600)
    for index, (label, color) in enumerate([('磁贴已固定到“常用应用”', TOKENS['brand']['blue']), ('保存失败，当前布局仍保留。请重试。', colors['errorForeground'])]):
        x = 48 + index * 436
        rect(x, 558, 408, 72, colors['surface'], 12, colors['menuBorder'])
        rect(x+1, 570, 3, 48, color, 1.5)
        text(x+20, 600, label)
    rect(944, 566, 264, 48, colors['surface'], 8, colors['controlBorder'])
    text(960, 596, '完整名称 · 标准磁贴 2×2')
    text(944, 650, '悬停 600 ms 后显示；离开取消', 12, colors['secondary'])
    text(48, 706, '所有应用 / 分类选择', 18, weight=600)
    for index, letter in enumerate(['#'] + list('ABCDEFGHIJKLMNOPQRSTUVWXYZ') + ['🌐']):
        x, y = 48 + index % 7 * 52, 730 + index // 7 * 52
        rect(x, y, 44, 44, colors['hover'] if letter == 'W' else colors['surface'], 7)
        if letter == '🌐':
            icon('globe', x+10, y+10, size=24)
        else:
            text(x+22, y+29, letter, 20, colors['foreground'] if letter in '#BGHWZ' else colors['secondary'], anchor='middle')
    text(468, 706, '文件夹 / 覆盖列表区域展开', 18, weight=600)
    rect(468, 730, 300, 260, colors['surface'], 10, colors['controlBorder'])
    text(484, 762, '‹  Windows 工具', 16, weight=600)
    for index, label in enumerate(['画图', '记事本', '终端', '文件资源管理器']):
        y = 780 + index * 46
        rect(478, y, 280, 44, colors['hover'] if index == 1 else colors['surface'], 6)
        rect(484, y+6, 32, 32, TOKENS['tilePalette'][index], 6)
        text(524, y+28, label)
        if index == 1:
            icon('pin', 724, y+12)
    text(832, 706, '动作与动效', 18, weight=600)
    for index, label in enumerate(['菜单：180 ms 弹出 / 120 ms 收起', '右键与下拉：缩放 + 淡入淡出', '磁贴：200 ms 位移与尺寸过渡', '固定：轻缩放进入 / 取消固定：缩小淡出', '文件夹：100 ms 退场 / 170 ms 进入', '焦点：仅 Tab 导航显示，点击与 Esc 清除']):
        text(832, 754+index*40, label, 14, colors['secondary'])
    output = DESIGN / f'mockups/v2/xdstarter-interactions-{theme}-v2.svg'
    output.write_text('<svg xmlns="http://www.w3.org/2000/svg" width="1440" height="1040" viewBox="0 0 1440 1040"><g font-family="Microsoft YaHei UI">' + ''.join(parts) + '</g></svg>', encoding='utf-8')
    render_svg(output, output.with_suffix('.png'))


if __name__ == '__main__':
    for variant in ('light', 'dark'):
        build(variant)
