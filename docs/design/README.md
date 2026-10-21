# 视觉设计与资产

当前基准：**v2 精确网格矢量稿，修订 2.1**。本次修订调整开始菜单的左对齐锚点，重绘侧栏快捷图标与圆形头像，并提供开发切图和 Windows 软件图标，跟踪于 [Issue #13](https://github.com/xiaodaozhi/xdstarter/issues/13)。Logo 沿用三枚阶梯状圆角磁贴，磁贴网格不变。原始图像探索仅保留为历史材料。

[Issue #14](https://github.com/xiaodaozhi/xdstarter/issues/14) 承接素材交付，将侧栏、圆形头像和软件图标接入运行时；[#15](https://github.com/xiaodaozhi/xdstarter/issues/15) 增加无边框菜单与外部焦点收起；[#16](https://github.com/xiaodaozhi/xdstarter/issues/16) 将运行时可见面板的左侧/底部工作区边距统一为 8 DIP，并完全自绘圆角、单层边框与阴影，禁用 Windows 原生装饰。本轮 [#22](https://github.com/xiaodaozhi/xdstarter/issues/22) 增加默认关闭、可显式启用及暂停的单独 Win 键原型，开始按钮仍为原生入口。现有 v2 修订 2.1 图片保留原画布锚点，未重生成，不作为新位置与自绘窗口外观的像素验收图。当前程序及实际结果分别记录于[测试策略](../technical/testing.md)；背景材料和其余系统接管继续按 Issues 推进。

本轮入口的 Windows 原生渲染样例见[启用菜单](validation/issue-22/win-key-enable-sample-native.png)与[暂停菜单](validation/issue-22/win-key-pause-sample-native.png)。它们取自测试窗口的完整 RGBA 帧，使用内存样例磁贴和模拟接管状态；不连接真实应用目录，也不证明原生输入接管。生成方式和实际物理按键验收分别见[测试策略](../technical/testing.md)。

## 当前设计稿

| 资产 | 位置 |
| --- | --- |
| 深色界面 | [PNG](mockups/v2/xdstarter-desktop-dark-v2.png) / [SVG](mockups/v2/xdstarter-desktop-dark-v2.svg) |
| 浅色界面 | [PNG](mockups/v2/xdstarter-desktop-light-v2.png) / [SVG](mockups/v2/xdstarter-desktop-light-v2.svg) |
| 尺寸与对齐规范 | [PNG](mockups/v2/xdstarter-tile-grid-v2.png) / [SVG](mockups/v2/xdstarter-tile-grid-v2.svg) |
| Logo 展示板 | [PNG](brand/xdstarter-brand-board.png) / [SVG](brand/xdstarter-brand-board.svg) |
| 独立标志 | [SVG](brand/xdstarter-mark.svg) / [1024 透明 PNG](brand/xdstarter-mark-1024.png) |
| 应用图标 | [SVG](assets/app/xdstarter-app-icon.svg) / [512 PNG](assets/app/xdstarter-app-icon-512.png) |
| 横向字标 | [SVG](brand/xdstarter-wordmark.svg) / [PNG](brand/xdstarter-wordmark.png) |
| 开发素材总览 | [PNG](assets/asset-overview.png) / [SVG](assets/asset-overview.svg) / [使用说明](assets/README.md) |
| 开发素材清单 | [manifest.json](assets/manifest.json)，记录逐项尺寸、缩放、主题、源文件与 SHA-256 |
| Windows 软件图标 | [xdstarter.ico](assets/app/xdstarter.ico)，含 16 至 256 px 的十帧图标 |

`brand/` 保留品牌展示与单色标志。开发集成使用 `assets/` 中的独立 SVG、透明 PNG 与 ICO，完整规格见其[使用说明](assets/README.md)。两张桌面界面图为 1920×1080；网格规范图为 1600×1080。设计稿中的第三方应用图标仅为简化矢量示意，不随开发素材分发；运行时从系统应用目录提取实际图标。

## 对齐、侧栏与头像

桌面设计稿的参考缩放为 150%，以下为修订 2.1 当时的画布约定；画布坐标以 px 表示，界面规格以 DIP 表示。其中开始按钮同左对齐已由 #16 的运行时对称边距规则替代，图标与头像几何继续使用：

| 项目 | 修订 2.1 规格 |
| --- | --- |
| 菜单与开始按钮左对齐（历史画布锚点） | 原图菜单可见边框和任务栏开始按钮命中区共用 `x = 24 px`，对应 16 DIP；不用于 #16 的运行时位置验收 |
| 任务栏 | 顶边 `y = 1008 px`，高 72 px，即 48 DIP；开始按钮命中区为 72×72 px，即 48×48 DIP |
| 左侧快捷入口 | 文档、下载、设置、电源四项；24 DIP 线描图标，统一 1.75 DIP 圆头笔画；命中区为 48×48 DIP |
| 快捷栏宽度 | 64 DIP；不占用右侧磁贴的共享网格宽度 |
| 左上头像 | 32 DIP 圆形头像，150% 对应 48×48 px；人物图形及背景均受圆形遮罩约束，圆外透明 |

头像 SVG 使用 `<mask>`，与项目 Qt 6.8.2 SVG 渲染器配合。Qt 6.8 的[遮罩文档](https://doc.qt.io/qt-6.8/svgextensions.html#masking)列出 `<mask>` 支持，并说明不支持 `<clipPath>`。实际用户照片也应在运行时进行圆形裁切。电源入口的交互仍遵循[交互设计](../product/interaction.md)，打开带文字的操作菜单。

## 运行时接入

`ShortcutRail.qml` 采用 64 DIP 栏宽、48×48 DIP 命中区、32 DIP 圆形头像和 24 DIP 图标，并从 qrc 按深浅主题读取 `assets/ui/{theme}/` 中的独立 SVG。头像当前为占位素材，点击进入账户设置；文档、下载和设置分别打开受控系统入口。每项保留键盘焦点、辅助功能名称和完整提示。电源菜单提供 Win 键唤起启用/暂停和退出 XDStarter，系统关机、重启与注销未启用。Win 键接管每次启动默认关闭，等待启用结果时显示忙碌状态；仅支持环境且托盘可用时允许显式启用，不改变现有快捷栏图标和几何。

软件 ICO 已写入 Windows EXE 资源；应用与窗口 `QIcon` 使用 16–1024 px 的多尺寸 PNG，托盘从自带图像创建原生图标。Qt 资源和 EXE 资源均由构建打包，不依赖程序工作目录。中性磁贴底图继续作为开发参考，实际磁贴根据共享网格与主题原生绘制。

菜单每次用户唤起时，后台通过 `queryMenuAnchor()` 只读查询目标显示器、工作区和任务栏 DPI，不初始化 COM 或等待 UIA 开始按钮识别。可见面板距工作区左侧与底部均为 8 DIP，空间不足时夹限，不再因按钮识别状态改变位置。透明外窗每边 24 DIP 阴影留白不计入面板间距，也不读取 DWM frame 边界参与布局；产品入口初始隐藏，在完成定位后再显示。原 UIA 研究接口仅在显式 probe 中使用。自动隐藏任务栏、Explorer 重启和混合 DPI 跨屏尚未完成实际验证；系统接管与材料继续由 [Issue #6](https://github.com/xiaodaozhi/xdstarter/issues/6) 跟踪。

运行时以 `visual.json` 的 `cornerRadiusDip.menu = 12` 和 `menuChrome` 为外观基准：`shadowInsetDip = 24`、`screenGapDip = 8`、`borderWidthDip = 1`。Canvas 使用共享圆角路径绘制主题实色面板、单层边框与阴影，并沿同一路径裁切侧栏背景，正文保留内边距。Windows 原生边框、圆角与阴影被禁用；此轮不修改 255 项开发素材或共享磁贴网格，透明外窗也不代表启用了背景材料。决策见 [ADR-0007](../technical/adr/0007-custom-menu-chrome.md)。

[Issue #18](https://github.com/xiaodaozhi/xdstarter/issues/18) 将菜单边框改为主题独立的 `themes.{light,dark}.menuBorder`：浅色为 `#B7BEC8` 中性灰，深色为 `#46505E` 灰蓝，均为不透明颜色，切换主题时立即重绘。浅色参考用户提供的 Windows 11 开始菜单细描边观感，不将参考截图当作固定系统色值。继续使用同一路径上的单层 1 DIP 内描边、12 DIP 圆角和现有阴影；此处是主题 token，不新增用户设置项，也不改变历史设计稿与开发素材。

托盘提供显示/隐藏、Win 键唤起启用/暂停/恢复和文字退出，收起后可恢复窗口；不可用时停止接管，并通过最小化保留任务栏恢复入口。托盘提示显示实际 Win 键状态和“开始按钮未接管”，恢复注册不自动重新启用。桌面快捷动作与工作区查询在后台服务执行，失败保留窗口；生命周期和支持边界见 [Windows 集成](../technical/windows-integration.md)，本机结果不扩大为所有 Windows 环境通过。当前菜单使用设计的局部组件与几何约定，整张桌面设计稿仍不作为产品窗口的像素验收截图。

## 尺寸和颜色

[tile-grid.json](tokens/tile-grid.json) 是当前几何数据来源；[视觉 tokens](tokens/visual.json) 保存品牌色和基础视觉值。

| 类型 | 实体尺寸（DIP） | 小格跨度 |
| --- | --- | --- |
| 小正方形 | 48×48 | 1×1 |
| 标准正方形 | 104×104 | 2×2 |
| 宽磁贴 | 216×104 | 4×2 |

所有间隙为 8 DIP。`48 + 8 + 48 = 104`，`104 + 8 + 104 = 216`。当前界面图按 150% 绘制，对应 72×72、156×156、324×156 像素，间隙 12 像素。深浅主题共用同一份布局。

品牌蓝 `#368BFF`、青 `#36D6CB`、深蓝 `#142138`。字标 PNG 保留既有导出字形；包含文字的 SVG 依赖字体替换，几何 Logo 无字体依赖。当前菜单与素材总览在 Windows 上使用 Segoe UI 及系统中文回退字体，不随资源分发字体。运行时使用系统字体并重新验收中文与缩放。

## 开发切图规格

| 素材 | SVG 与 PNG 规格 |
| --- | --- |
| 深浅主题界面图标 | SVG 24×24 viewBox；100%、125%、150%、175%、200%、300% 对应 PNG 24、30、36、42、48、72 px |
| 圆形占位头像 | 深浅主题 SVG；PNG 32、40、48、56、64、96、128、192、256 px，基准显示尺寸 32 DIP |
| 三档中性磁贴底图 | 48×48、104×104、216×104 DIP；每档导出 100%、125%、150%、175%、200% PNG，圆角 6 DIP，无应用图标和文字 |
| 应用图标 | ICO 含 16、20、24、32、40、48、64、96、128、256 px 十个 32 位 RGBA PNG 帧；独立 PNG 另含 512、1024 px |
| 透明品牌标志 | 三枚阶梯状磁贴的 SVG 与对应 PNG，无应用图标外框 |

`assets/` 提供 37 个 SVG、217 个 PNG 和 1 个 ICO，共 255 个素材文件，另含清单与说明。QML 优先使用矢量或根据 tokens 绘制几何；PNG 按实际设备像素比选择。不要将整张设计图作为界面背景，也不要把带示意应用图标的磁贴截图用作运行时磁贴。素材不附带字体，仓库许可证尚未确定，本轮不另行选择授权。

## 可复现源文件

Python 生成与校验脚本仅使用标准库；PNG 通过 [render_svg.py](source/render_svg.py) 调用 Qt 6.8.2 的 `QSvgRenderer` 离屏导出。渲染辅助程序的源代码位于 [renderer/](source/renderer/)，不随素材分发 Qt 库或可执行文件。在已按[开发环境](../technical/development.md)配置的仓库根目录执行：

```powershell
./scripts/build-windows.ps1 -Preset app-release -DesignAssets
./build/tooling/Scripts/python.exe scripts/check_repository.py
```

上述入口构建渲染辅助程序，重新导出本轮菜单图和开发素材，并运行素材专项校验；随后仓库检查包含菜单与开始按钮对齐、头像和磁贴几何等检查。辅助程序已构建时，也可分别执行本轮重建：

```sh
python3 docs/design/source/build_desktop_grid.py
python3 docs/design/source/build_development_assets.py
python3 docs/design/source/verify_menu_design.py
python3 docs/design/source/verify_tile_grid.py
python3 docs/design/source/verify_development_assets.py
python3 scripts/check_repository.py
```

本轮生成器输出到 `mockups/v2/` 和 `assets/`。如需对比其他渲染器导出，可通过 `XDSTARTER_SVG_RENDERER` 显式选择已安装的 `rsvg-convert`。既有品牌展示板及尺寸规范图的生成器 `build_brand.py`、`build_tile_grid.py` 仍直接使用 `rsvg-convert`，需要另行安装后执行；它们不属于 `-DesignAssets` 的本轮重建范围。

文档与素材校验本身不要求安装渲染器；重新生成 PNG 才需要渲染环境。自动检查覆盖菜单/开始按钮实际边框对齐、侧栏点击区与线描规格、磁贴几何、PNG 尺寸和透明通道、头像圆形边界、素材哈希以及 ICO 帧目录和 PNG 负载；字体、图标观感与两张主题稿仍需目视核对。

- [初始生成提示词](source/prompts/image-prompts.md)
- [磁贴修订探索提示词](source/prompts/image-prompts-v2.md)
- [归档说明](archive/README.md)

修改设计时先更新网格或源文件，再导出 PNG/SVG 并目视核对。不要把生成式探索图作为精确尺寸依据。设计变更关联 Issue 和 PR，版本规则见[文档规范](../standards/documentation.md)。
