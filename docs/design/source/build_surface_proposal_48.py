# docs/design/source/build_surface_proposal_48.py

"""Editable Issue 48 proposal boards; never modifies runtime tokens or QML."""
from html import escape
from pathlib import Path
import json
import math

from render_svg import render_svg
from ui_symbols import app_icon_svg, glyph_paths

DESIGN = Path(__file__).resolve().parents[1]
OUT = DESIGN / 'mockups/v2/proposals/issue-48'
BASE = json.loads((DESIGN / 'tokens/visual.json').read_text(encoding='utf-8'))
PROPOSAL = {
    'issue': 48,
    'revision': '2.4-candidate-1',
    'status': 'approved-by-user-2026-10-23',
    'appliedToRuntime': True,
    'runtimeVerification': 'recorded separately in Issue 48 and testing documentation',
    'fontFamily': BASE['fontFamily'],
    'menu': {'radiusDip': 8, 'paddingDip': 6, 'rowHeightDip': 36,
             'rowRadiusDip': 4, 'iconSizeDip': 16, 'textInsetDip': 38,
             'pointerHover': 'quiet neutral surface, no accent flash',
             'checkmark': 'rounded vector path in the shared leading icon slot'},
    'tooltip': {'radiusDip': 4, 'shadow': False, 'delayMs': 600,
                'paddingHorizontalDip': 10, 'paddingVerticalDip': 7},
    'feedback': {'radiusDip': 8, 'shadow': 'soft ambient and contact layers',
                 'showFor': ['actionable failure requiring user attention'],
                 'silentFor': ['application launch', 'pin or unpin',
                               'successful refresh', 'successful save']},
    'dialog': {'radiusDip': 12, 'paddingDip': 24,
               'shadow': 'soft ambient and contact layers'},
    'motion': {'hoverMs': 0, 'pressMs': 0, 'menuEnterMs': 120,
               'menuExitMs': 90, 'oneEnterPerReveal': True},
    'copy': {'generatedMiddleDotSeparators': False,
             'tileSizes': ['小磁贴 (1×1)', '中磁贴 (2×2)', '宽磁贴 (4×2)']},
}

EXTRA_ICONS = BASE['interactionSurface']['glyphs']


class Board:
    def __init__(self, theme, title):
        self.theme = theme
        self.c = dict(BASE['themes'][theme])
        self.c.update({
            'board': '#EEF2F7' if theme == 'light' else '#111925',
            'card': '#FFFFFF' if theme == 'light' else '#253247',
            'hover': '#EEF2F6' if theme == 'light' else '#334359',
            'pressed': '#E2E8EF' if theme == 'light' else '#3D506B',
            'primary': '#2164D7' if theme == 'light' else '#93BDFF',
            'onPrimary': '#FFFFFF' if theme == 'light' else '#102341',
            'disabled': '#97A2B0' if theme == 'light' else '#728196',
        })
        self.parts = []
        self.rect(0, 0, 1440, 1120, self.c['board'], 0)
        self.app_icon(48, 36, 36)
        self.text(98, 63, 'XDStarter', 26, weight=600)
        self.text(1392, 57, '2.4 设计 / 已验收', 13,
                  self.c['secondary'], anchor='end')
        self.text(48, 114, title, 24, weight=600)
        self.text(48, 142, '微软雅黑 UI / 共享字体 / 可编辑 SVG / 150% 导出 / 运行时结果单独验证',
                  13, self.c['secondary'])

    def rect(self, x, y, w, h, color, radius=8, stroke='none', opacity=1):
        self.parts.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" '
                          f'rx="{radius}" fill="{color}" stroke="{stroke}" opacity="{opacity}"/>')

    def text(self, x, y, value, size=14, color=None, weight=400, anchor='start'):
        self.parts.append(f'<text x="{x}" y="{y}" font-size="{size}" '
                          f'font-weight="{weight}" fill="{color or self.c["foreground"]}" '
                          f'text-anchor="{anchor}">{escape(value)}</text>')

    def icon(self, name, x, y, size=16, color=None):
        paths = EXTRA_ICONS[name] if name in EXTRA_ICONS else glyph_paths(name)
        self.parts.append(f'<g transform="translate({x} {y}) scale({size/24})" '
                          f'fill="none" stroke="{color or self.c["foreground"]}" stroke-width="1.8" '
                          f'stroke-linecap="round" stroke-linejoin="round">{paths}</g>')

    def app_icon(self, x, y, size):
        source = app_icon_svg().split('>', 1)[1].rsplit('</svg>', 1)[0]
        self.parts.append(f'<g transform="translate({x} {y}) scale({size/256})">{source}</g>')

    def shadow(self, x, y, w, h, radius=8, elevation=1):
        # Many low-opacity contours approximate a continuous blur in Qt SVG,
        # without unsupported SVG filters or a hard offset duplicate surface.
        for spread in range(32 * elevation, 0, -1):
            opacity = (0.012 if self.theme == 'light' else 0.022) * math.exp(-0.5 * (spread / (8 * elevation))**2)
            self.rect(x-spread, y+4*elevation-spread, w+2*spread, h+2*spread,
                      '#07111E', radius+spread, opacity=round(opacity, 5))

    def panel(self, x, y, w, h, radius=8, elevation=1):
        self.shadow(x, y, w, h, radius, elevation)
        self.rect(x, y, w, h, self.c['card'], radius, self.c['controlBorder'])

    def rule(self, x, y, w):
        self.rect(x, y, w, 1, self.c['separator'], 0)

    def menu(self, x, y, w, rows, state=None):
        h = 12 + sum(13 if row is None else 36 for row in rows)
        self.panel(x, y, w, h)
        top = y + 6
        for index, row in enumerate(rows):
            if row is None:
                self.rule(x+12, top+6, w-24)
                top += 13
                continue
            label, icon, option = row
            option = option or ''
            disabled = 'disabled' in option
            row_state = state.get(index) if state else None
            if row_state in ('hover', 'pressed', 'focus'):
                self.rect(x+6, top, w-12, 36,
                          self.c['pressed'] if row_state == 'pressed' else self.c['hover'], 4,
                          self.c['primary'] if row_state == 'focus' else 'none')
            color = self.c['disabled'] if disabled else self.c['foreground']
            if icon:
                self.icon(icon, x+14, top+10, color=color)
            self.text(x+38, top+23, label, 14, color)
            if 'submenu' in option:
                self.icon('chevron-right', x+w-27, top+11, 14, color)
            top += 36

    def write(self, name):
        target = OUT / f'{name}-{self.theme}.svg'
        body = ('<svg xmlns="http://www.w3.org/2000/svg" width="2160" height="1680" '
                'viewBox="0 0 1440 1120"><title>XDStarter Issue 48 待验收设计</title>'
                f'<g font-family="{BASE["fontFamily"]}">')
        target.write_text(body + ''.join(self.parts) + '</g></svg>\n', encoding='utf-8')
        render_svg(target, target.with_suffix('.png'))
        print(target.relative_to(DESIGN))


def menus(theme):
    b = Board(theme, '右键菜单与下拉菜单')
    b.text(48, 194, '01 / 磁贴右键与尺寸子菜单', 16, weight=600)
    b.menu(48, 218, 252, [
        ('打开', 'open', ''), ('磁贴大小', 'tile-size', 'submenu'),
        ('修改显示名称', 'edit', ''), ('恢复原名称', None, 'disabled'),
        None, ('取消固定磁贴', 'unpin', '')], {1: 'hover'})
    selected = 'check'
    b.menu(308, 260, 232, [
        ('小磁贴 (1×1)', None, ''), ('中磁贴 (2×2)', selected, ''),
        ('宽磁贴 (4×2)', None, '')], {2: 'hover'})
    b.text(48, 478, '图标、勾选与空位共享同一列，所有文本对齐。', 13, b.c['secondary'])
    b.text(48, 502, '中磁贴为当前尺寸；宽磁贴展示鼠标悬停。', 13, b.c['secondary'])

    b.text(632, 194, '02 / 更多菜单与勾选项', 16, weight=600)
    b.menu(632, 218, 256, [
        ('外观主题', 'theme', 'submenu'), ('新建分组', 'add', ''), None,
        ('保存布局…', 'save', ''), ('导入布局…', 'import', ''), None,
        ('关于 XDStarter', 'info', '')], {0: 'hover'})
    b.menu(896, 224, 204, [
        ('浅色', 'check' if theme == 'light' else None, ''),
        ('深色', 'check' if theme == 'dark' else None, '')])
    b.text(1140, 248, '柔和投影', 14, weight=600)
    b.text(1140, 277, '不再使用偏移的实色矩形', 12, b.c['secondary'])
    b.text(1140, 299, '子菜单与主菜单保持间隔', 12, b.c['secondary'])
    b.text(1140, 340, '统一状态', 14, weight=600)
    b.text(1140, 369, '悬停只进入低对比度底色', 12, b.c['secondary'])
    b.text(1140, 391, '不经过蓝色或强高亮中间态', 12, b.c['secondary'])
    b.text(1140, 433, '禁用项', 14, weight=600)
    b.text(1140, 462, '降低文字及图标对比度', 12, b.c['secondary'])
    b.text(1140, 484, '不附加 hint 或 tooltip', 12, b.c['secondary'])

    b.text(48, 570, '03 / 所有应用右键', 16, weight=600)
    b.text(48, 598, '未固定', 12, b.c['secondary'])
    b.menu(48, 614, 236, [('打开', 'open', ''), ('固定为磁贴', 'pin', '')], {1: 'hover'})
    b.text(324, 598, '已固定', 12, b.c['secondary'])
    b.menu(324, 614, 236, [('打开', 'open', ''), ('取消固定磁贴', 'unpin', '')], {1: 'hover'})
    b.text(48, 740, '直接固定到最后一个分组，不再要求选择分组。', 13, b.c['secondary'])
    b.text(48, 764, '列表右侧不放图钉按钮，拖拽与右键提供固定入口。', 13, b.c['secondary'])

    b.text(632, 570, '04 / 提示与对齐', 16, weight=600)
    b.rect(632, 614, 330, 42, b.c['card'], 4, b.c['controlBorder'])
    b.text(642, 640, 'Windows 附件', 13)
    b.text(632, 692, '提示圆角 4 DIP，无阴影，等待 600 ms。', 13, b.c['secondary'])
    b.text(632, 720, '菜单打开、拖动、离开或隐藏窗口时取消。', 13, b.c['secondary'])
    b.text(632, 748, '右键后移走指针，不保留旧提示。', 13, b.c['secondary'])

    b.text(48, 838, '05 / 单项交互状态', 16, weight=600)
    for x, label, state in [(48, '默认', None), (396, '悬停', 'hover'),
                             (744, '按下', 'pressed'), (1092, 'Tab 焦点', 'focus')]:
        b.text(x, 872, label, 12, b.c['secondary'])
        b.menu(x, 890, 292, [('保存布局…', 'save', '')], {0: state} if state else None)
    b.rule(48, 1004, 1344)
    b.text(48, 1040, '鼠标路径：默认 → 柔和悬停 → 轻压暗 → 悬停；键盘焦点仅在 Tab 导航时出现。', 13, b.c['secondary'])
    b.text(48, 1070, '菜单圆角 8 DIP，行高 36 DIP，16 DIP 矢量图标；尺寸与勾选均不依赖字体符号。', 13, b.c['secondary'])
    b.write('menus')


def overlays(theme):
    b = Board(theme, '关于对话框、浮动反馈与轻量提示')
    b.text(48, 194, '01 / 关于对话框', 16, weight=600)
    # A quiet sample menu behind the dialog makes the elevation readable.
    b.rect(48, 218, 736, 526, b.c['opaqueFallback'], 12, b.c['controlBorder'])
    b.rect(72, 242, 688, 40, b.c['searchBackground'], 6, b.c['controlBorder'])
    b.icon('search', 85, 253, 18, b.c['secondary'])
    b.text(114, 268, '搜索应用', 14, b.c['secondary'])
    b.text(76, 324, '所有应用', 16, weight=600)
    b.text(332, 324, '常用应用', 16, weight=600)
    for index, label in enumerate(['Windows 附件', '计算器', '记事本', '设置']):
        y = 348 + index*52
        b.icon('folder' if index == 0 else 'apps', 80, y+8, 24, b.c['secondary'])
        b.text(118, y+27, label)
    for x, y, name, color in [(332, 348, '终端', '#3B4D63'), (444, 348, '照片', '#404F9A'),
                                (332, 460, '设置', '#244E79'), (444, 460, '记事本', '#206747')]:
        b.rect(x, y, 104, 104, color, 6)
        b.text(x+10, y+88, name, 14, '#FFFFFF')
    b.rect(48, 218, 736, 526, '#07111E', 12, opacity=0.10 if theme == 'light' else 0.20)
    b.panel(192, 282, 448, 388, 12, 2)
    b.app_icon(216, 308, 52)
    b.text(288, 334, 'XDStarter', 24, weight=600)
    b.text(288, 359, 'Windows 磁贴开始菜单', 13, b.c['secondary'])
    b.icon('close', 600, 302, 16, b.c['secondary'])
    b.rule(216, 388, 400)
    b.text(216, 423, '版本', 13, b.c['secondary'])
    b.text(616, 423, '0.1.0（开发版）', 14, anchor='end')
    b.text(216, 465, '通过拖动和右键管理常用应用。', 14)
    b.text(216, 491, '默认在后台运行，可从托盘管理。', 14)
    b.rect(216, 516, 400, 46, b.c['hover'], 6)
    b.icon('check', 229, 531, 16, b.c['primary'])
    b.text(256, 545, '开始按钮接管', 13)
    b.text(602, 545, '已启用', 13, b.c['secondary'], anchor='end')
    b.rule(216, 586, 400)
    b.text(216, 630, '项目主页', 14, b.c['primary'])
    b.rect(528, 604, 88, 36, b.c['primary'], 6)
    b.text(572, 627, '关闭', 14, b.c['onPrimary'], 600, 'middle')
    b.text(48, 782, '更清晰的信息层级，统一字体，周围保留柔和投影。状态为界面示例。', 13, b.c['secondary'])

    b.text(848, 194, '02 / 文件夹与空目录', 16, weight=600)
    b.rect(848, 218, 544, 242, b.c['card'], 8, b.c['controlBorder'])
    b.rect(860, 230, 32, 32, b.c['hover'], 4)
    b.icon('chevron-left', 868, 238, 16)
    b.text(904, 252, 'Windows 附件', 16, weight=600)
    b.rule(864, 274, 512)
    b.icon('folder', 1100, 306, 40, b.c['disabled'])
    b.text(1120, 376, '此文件夹为空', 14, anchor='middle')
    b.text(1120, 402, '可通过左上角返回其他应用', 13, b.c['secondary'], anchor='middle')
    b.text(848, 494, '返回使用矢量箭头居中，空态位于标题下方。', 13, b.c['secondary'])
    b.text(848, 552, '03 / Tooltip', 16, weight=600)
    b.rect(848, 576, 326, 42, b.c['card'], 4, b.c['controlBorder'])
    b.text(858, 602, '完整应用名称', 13)
    b.text(848, 656, '小圆角、无投影，不抢视觉焦点。', 13, b.c['secondary'])
    b.text(848, 684, '600 ms 延迟，移开和打开菜单立即取消。', 13, b.c['secondary'])
    b.text(848, 712, '文案用换行或逗号组织，不使用中点分隔。', 13, b.c['secondary'])

    b.text(48, 852, '04 / 仅保留必要的浮动反馈', 16, weight=600)
    b.panel(48, 886, 736, 78, 8)
    b.icon('error', 68, 907, 22, b.c['errorForeground'])
    b.text(104, 916, '无法保存布局', 14, weight=600)
    b.text(104, 942, '现有布局仍保留，请稍后重试。', 13, b.c['secondary'])
    b.icon('close', 752, 901, 16, b.c['secondary'])
    b.text(48, 1007, '只在失败或必须干预时出现，带轻柔投影，不挤占目录与磁贴布局。', 13, b.c['secondary'])
    b.text(848, 852, '以下操作保持安静', 16, weight=600)
    for i, label in enumerate(['打开应用', '固定与取消固定磁贴', '刷新成功', '保存成功']):
        b.icon('check', 848, 879+i*32, 16, b.c['secondary'])
        b.text(876, 892+i*32, label, 14, b.c['secondary'])
    b.rule(48, 1040, 1344)
    b.text(48, 1074, '关于对话框圆角 12 DIP，提示栏圆角 8 DIP；两者有投影，tooltip 不使用投影。', 13, b.c['secondary'])
    b.write('overlays')


if __name__ == '__main__':
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / 'proposal.json').write_text(json.dumps(PROPOSAL, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    for variant in ('light', 'dark'):
        menus(variant)
        overlays(variant)
