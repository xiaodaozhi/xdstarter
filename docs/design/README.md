# 视觉设计与资产

[Issue #48](https://github.com/xiaodaozhi/xdstarter/issues/48) 的 **2.4 交互表面稿已于2026-10-23由用户验收**。它修订菜单、尺寸子菜单、关于、浮动反馈与tooltip，完整桌面磁贴几何沿用下文基准；运行时检查另记于测试策略。

| 已验收交互稿 | 浅色 | 深色 |
| --- | --- | --- |
| 右键、下拉与尺寸子菜单 | [PNG](mockups/v2/proposals/issue-48/menus-light.png) / [SVG](mockups/v2/proposals/issue-48/menus-light.svg) | [PNG](mockups/v2/proposals/issue-48/menus-dark.png) / [SVG](mockups/v2/proposals/issue-48/menus-dark.svg) |
| 关于、必要错误提示、tooltip与目录空态 | [PNG](mockups/v2/proposals/issue-48/overlays-light.png) / [SVG](mockups/v2/proposals/issue-48/overlays-light.svg) | [PNG](mockups/v2/proposals/issue-48/overlays-dark.png) / [SVG](mockups/v2/proposals/issue-48/overlays-dark.svg) |

表面使用独立的`interactionSurface` tokens：菜单8 DIP圆角、36 DIP行高、统一38 DIP文本起点；勾选与图标均为16 DIP矢量图形。菜单、浮动提示和对话框使用柔和分层投影，tooltip仅4 DIP圆角且无投影。鼠标悬停直接进入中性底色，避免强调色中间态。正常打开应用、固定、刷新和保存不弹浮动提示；错误反馈可关闭。新稿及内置文案不使用中点作分隔符，用户和系统提供的名称不被清洗。

源码为[source/build_surface_proposal_48.py](source/build_surface_proposal_48.py)，[proposal.json](mockups/v2/proposals/issue-48/proposal.json)记录设计验收身份；PNG由现有Qt SVG工具离屏导出，字体仍为系统Microsoft YaHei UI，不分发字体。当前实现采用schema4引入的动态列，数据与吸附取舍见[ADR-0015](../technical/adr/0015-responsive-tile-columns.md)。

运行时样例已通过QtTest与目视核对：[浅色菜单](validation/issue-48/light-menus.png)／[深色菜单](validation/issue-48/dark-menus.png)、[浅色关于](validation/issue-48/light-about.png)／[深色关于](validation/issue-48/dark-about.png)、[浅色必要错误提示](validation/issue-48/light-failure.png)／[深色必要错误提示](validation/issue-48/dark-failure.png)。这些是内存样例的离屏抓帧，主窗口之外的RGB背景不代表Windows透明合成；菜单位置与样例编辑状态可不同，字体、图标位、hover、圆角、模态层、阴影及关闭对齐分别验证。精确程序和检查身份见[测试策略](../technical/testing.md)。

当前基准：**v2 精确网格矢量稿，修订 2.3**，由 [Issue #41](https://github.com/xiaodaozhi/xdstarter/issues/41) 更新，沿用 [Issue #29](https://github.com/xiaodaozhi/xdstarter/issues/29) 的完整菜单布局。本修订统一此前的窗口外观与完整正文布局：顶部以搜索为主入口，右侧提供更多和收起图标；正文并列应用目录与磁贴分组，正常浏览不常驻品牌大标题、调试提示或保存按钮。源文件、tokens 和当前桌面导出同时维护。

设计导出说明目标布局；实际 QML 抓帧、有效缩放、产品交互和重启的结果统一记录在[测试策略](../technical/testing.md)，不能以 SVG/PNG 导出代替运行时验收。Mica/Acrylic 与完整系统接管仍由 [#6](https://github.com/xiaodaozhi/xdstarter/issues/6) 跟踪，本轮采用实色面板。

## 当前设计稿

| 资产 | 位置 |
| --- | --- |
| 深色界面，修订 2.3 | [PNG](mockups/v2/xdstarter-desktop-dark-v2.png) / [SVG](mockups/v2/xdstarter-desktop-dark-v2.svg) |
| 浅色界面，修订 2.3 | [PNG](mockups/v2/xdstarter-desktop-light-v2.png) / [SVG](mockups/v2/xdstarter-desktop-light-v2.svg) |
| 浅色交互状态 | [PNG](mockups/v2/xdstarter-interactions-light-v2.png) / [SVG](mockups/v2/xdstarter-interactions-light-v2.svg) |
| 深色交互状态 | [PNG](mockups/v2/xdstarter-interactions-dark-v2.png) / [SVG](mockups/v2/xdstarter-interactions-dark-v2.svg) |
| 尺寸与对齐规范 | [PNG](mockups/v2/xdstarter-tile-grid-v2.png) / [SVG](mockups/v2/xdstarter-tile-grid-v2.svg) |
| Logo 展示板 | [PNG](brand/xdstarter-brand-board.png) / [SVG](brand/xdstarter-brand-board.svg) |
| 独立标志 | [SVG](brand/xdstarter-mark.svg) / [1024 透明 PNG](brand/xdstarter-mark-1024.png) |
| 应用图标 | [SVG](assets/app/xdstarter-app-icon.svg) / [512 PNG](assets/app/xdstarter-app-icon-512.png) |
| 横向字标 | [SVG](brand/xdstarter-wordmark.svg) / [PNG](brand/xdstarter-wordmark.png) |
| 开发素材总览 | [PNG](assets/asset-overview.png) / [SVG](assets/asset-overview.svg) / [使用说明](assets/README.md) |
| 开发素材清单 | [manifest.json](assets/manifest.json)，逐项尺寸、缩放、主题、源文件与 SHA-256 |
| Windows 软件图标 | [xdstarter.ico](assets/app/xdstarter.ico)，16 至 256 px 十帧 |

桌面图为 **1920×1200 px、150% 参考缩放**，网格规范图仍为 1600×1080 px。默认可见面板为 980×680 DIP，即 1470×1020 px；参考任务栏高 48 DIP，即 72 px。面板左侧与任务栏上方各留 8 DIP：画布中的可见外缘为 `x = 12 px`、`y = 96 px`，底边 `y = 1116 px`，任务栏顶边 `y = 1128 px`。任务栏开始按钮仍可位于 `x = 24 px`，它不再决定菜单锚点。

修订 2.2 已冻结至[历史桌面稿](archive/v2-revision-2.2/README.md)。修订 2.1 的两套桌面 SVG/PNG 与生成器已冻结至[历史归档](archive/v2-revision-2.1/README.md)，不用于当前位置、完整布局或窗口外观验收。更早的生成式探索见[归档说明](archive/README.md)。Logo、网格规范图及 283 项开发素材继续沿用既有版本，原始素材继续沿用；本轮新增刷新与地球分类图标。

设计中的第三方应用图标是简化矢量示意，不作为可分发应用图标，也不要求实际系统图标与示意逐像素相同。运行时从系统应用目录提取图标；当前 SVG 与 QML 均优先使用 Microsoft YaHei UI／微软雅黑，控件与磁贴文本保持同一字族，仓库不分发字体。

## 版式与共享几何

[visual.json](tokens/visual.json) 保存完整菜单的视觉合约；[tile-grid.json](tokens/tile-grid.json) 是磁贴实体几何的唯一数据源。两张主题图共用同一布局，没有单独缩放或挪动某枚磁贴。

| 项目 | DIP 合约 |
| --- | --- |
| 默认可见面板 | 980×680；12 圆角、1 单层边框；外窗每侧 24 阴影留白 |
| 工作区边距 | 底部左对齐时左／底均为 8；其他停靠策略见 ADR-0013，空间不足时按工作区夹限 |
| 快捷栏 | 宽 64；48×48 命中区、24 线描图标、32 圆形头像 |
| 正文 | 距快捷栏及面板外缘 24；主要列间距 24 |
| 顶部 | 搜索高 40；两个 36×36 图标入口，间隔 8；正文与搜索相隔 24 |
| 应用目录 | 宽 240；标题高 32，右侧提供 28×28 刷新入口；目录行高 44、分类行高 44，32×32 分类按钮与应用图标对齐 |
| 磁贴分组 | 标题高 32、标题至网格 8、组间 24；默认网格 10 列、总宽 552 |
| 小磁贴 | 48×48，1×1 小格 |
| 标准磁贴 | 104×104，2×2 小格 |
| 宽磁贴 | 216×104，4×2 小格 |

基础小格 48 DIP、间隙 8 DIP，因此 `48 + 8 + 48 = 104`，`104 + 8 + 104 = 216`。150% 对应 72×72、156×156、324×156 px，间隙 12 px。默认版式中，正文起点为面板内 `(88, 88) DIP`，磁贴列从 `x = 352 DIP` 开始；三个示例组的网格顶边依次为 `128/296/464 DIP`。浏览模式按实际占用高度排组，空组保留可见占位；默认保留上下文操作，仅拖动期间展开落点区域，不改动磁贴坐标与尺寸。窄窗通过目录/磁贴切换及滚动容纳现有数据，不压缩磁贴。

字体基准为正文与磁贴文字 14 DIP、说明 12 DIP、分类字母 22 DIP、分组和目录标题 16 DIP。目录图标 32 DIP，位于 44 DIP 目录行的 `(6, 6) DIP`，文字起点距行左侧 46 DIP；小磁贴图标 28 DIP，标准与宽磁贴图标 36 DIP，后两者图标顶距 14 DIP。磁贴文字布局框距左右和底边 10 DIP；SVG 另以磁贴底边上方 14 DIP 定位 14 DIP 字号的基线，基线不是文字框底边。长名称省略并提供完整提示。快捷栏图标维持 1.75 DIP 圆头线条；圆形头像的背景、人物都受圆形遮罩约束，圆外透明。

## 主题、表面与控件状态

品牌蓝 `#368BFF`、青 `#36D6CB`、深蓝 `#142138` 保留用于品牌和强调。正文使用当前实色 `opaqueFallback`，快捷栏及高层表面使用 `surface`；搜索、控件边框、分隔线、焦点和交互状态从同一主题表取值。

| Token | 浅色 | 深色 |
| --- | --- | --- |
| `opaqueFallback` | `#EFF5FC` | `#182334` |
| `surface` | `#F8FAFE` | `#223148` |
| `searchBackground` | `#FFFFFF` | `#23334A` |
| `focusAccent` | `#2164D7` | `#36D6CB` |
| `focusControl` | `#2164D7` | `#93BDFF` |
| `focusTileInner` | `#FFFFFF` | `#FFFFFF` |
| `foreground` | `#172338` | `#F4F7FB` |
| `secondary` | `#596A82` | `#ACBBD0` |
| `errorForeground` | `#B42318` | `#FF9696` |
| `menuBorder` | `#B7BEC8` | `#46505E` |
| `controlBorder` | `#D1DAE6` | `#3B4B61` |
| `separator` | `#D4DEEB` | `#324258` |
| `hover` | `#E2EAF5` | `#2B3E57` |
| `pressed` | `#D5E1F0` | `#344B68` |

[#80](https://github.com/xiaodaozhi/xdstarter/issues/80) 的键盘焦点使用主题焦点色：分组和快捷栏采用 `focusAccent`，普通控件采用 `focusControl`；磁贴焦点的外圈对比面板，白色内圈对比磁贴渐变，均在原尺寸内绘制。非焦点的品牌、选中及表面色不变。修订 2.3 的交互状态图保留当时配色，当前焦点样式以运行时 tokens 和测试记录为准。

Windows 高对比度是独立的系统色呈现，不以这两套固定色值代替用户配色。磁贴和应用图标以名称首字呈现、快捷栏改用短文字；焦点、悬停、按下及选中用边框区分。[深底模拟样例](validation/issue-83/contrast-dark.png)、[浅底焦点与提示](validation/issue-83/contrast-light-focus.png)和[浅底菜单](validation/issue-83/contrast-light-menu.png)来自注入颜色的离屏 QML 测试，不是实际 Windows 高对比主题或物理屏幕验收。规范和失败回退见 [ADR-0020](../technical/adr/0020-high-contrast.md)。

菜单、对话框、按钮和列表沿用这些表面及状态色；鼠标悬停、按下、键盘焦点和禁用状态应可区分。重要错误和只读原因保留文字，并使用主题独立的 `errorForeground` 确保浅色/深色底上的可读性；全局 `status.error = #ED7777` 继续用于失效标记填充等图形，不能直接作为浅色背景上的错误文字。正常成功状态不占用常驻底栏。主体 Canvas 先绘制边框色的 12 DIP 圆角外轮廓，再向内缩进 1 DIP 绘制面板和侧栏，内圆角为 11 DIP；外缘只覆盖一次边框色，避免同一抗锯齿边缘的面板填色和描边叠加使曲线变淡。侧栏限制在内轮廓中，边框外侧没有第二层圆角遮罩。Windows 原生边框、圆角、阴影均禁用。透明外窗只用于承载阴影，不表示已启用模糊背景材料。决策见 [ADR-0007](../technical/adr/0007-custom-menu-chrome.md)，本次光栅化修正由 [#31](https://github.com/xiaodaozhi/xdstarter/issues/31) 跟踪；v2 修订 2.3 的几何与颜色不变，运行时结果与设计导出分别验收。

磁贴统一使用 `tilePalette` 中八种深色底与白字：`#245CA6`、`#206747`、`#9B482A`、`#244E79`、`#1E617B`、`#3B4D63`、`#404F9A`、`#8B5020`。底色由稳定身份映射：优先 `targetId`，否则磁贴 `id`，最多读取前 256 个 UTF-16 代码单元；初值 5381，每步 `hash = ((hash * 33) ^ codeUnit) >>> 0`，最终对调色板长度取余。设计示例以固定 app 标识代替运行时目标身份。分组、排序、重启和主题切换不会导致同一目标随意变色；底色不承诺品牌取色。微弱纵向层次为基础色顶部 1.05 倍提亮、底部 1.04 倍压暗。

## 功能状态的视觉基准

以下是本轮必须覆盖的布局状态，具体抓帧和通过情况见[测试策略](../technical/testing.md)。设计桌面图只展示默认浏览，不能代表这些交互已通过。

| 状态 | 视觉与交互要求 |
| --- | --- |
| 默认浏览 | 顶部搜索和紧凑入口；目录与分组并列；无常驻品牌大标题或调试栏 |
| 默认编辑 | 无模式开关及说明工具栏；新建分组位于更多菜单，保留拖动／尺寸／跨组／取消操作 |
| 搜索 | 正文切为结果；保留图标、名称、可用性及匹配状态；清空后恢复浏览位置 |
| 空布局或空目录 | 在所属区域说明并提供可发现的固定、刷新或编辑入口，不伪造应用内容 |
| 加载或刷新 | 区域内显示忙碌反馈；已有目录快照仍可浏览；不以整窗遮罩替代后台操作 |
| 失效、错误或只读 | 保留明确原因与禁用状态；可恢复/取消固定等入口仍可发现 |
| 更多与内部弹层 | 主题、保存、关于移入“更多”，刷新保留在目录标题旁；菜单/组名弹层与主体同主题，键盘可操作，模态遮罩限于圆角面板 |
| 窄窗与缩放 | 原有磁贴实体几何保持；目录切换及滚动可达；文字、焦点与阴影不被截断 |

实际验收分别覆盖深浅主题与 100/125/150/175/200% Qt 有效缩放。通过进程缩放抓帧不等同更改系统显示设置，也不自动证明热插拔、远程桌面、所有混合 DPI 迁移或完整 Windows 兼容矩阵通过。

## 运行时与系统入口

`ShortcutRail.qml` 从 qrc 读取 `assets/ui/{theme}/` 独立 SVG，保留可见焦点、辅助功能名称及提示。头像为占位，点击进入账户设置；文档、下载、设置打开受控系统入口，电源图标先打开带文字的菜单。软件 ICO 写入 Windows EXE 资源；窗口与托盘图标从自带多尺寸图像创建，不依赖工作目录。

唤起时后台只读查询目标显示器、工作区与任务栏 DPI，在隐藏阶段完成实际跨屏迁移、逻辑尺寸恢复和稳定定位后显示；不等待 UIA 开始按钮识别，不读取 DWM 外框参与布局。当前窗口可见面板的对称间距与首次混合 DPI 恢复已有独立原生测量和产品验收记录；具体版本和边界见[测试策略](../technical/testing.md)。自动隐藏任务栏、Explorer 重启、远程会话及完整材料/接管兼容仍由 [#5](https://github.com/xiaodaozhi/xdstarter/issues/5) / [#6](https://github.com/xiaodaozhi/xdstarter/issues/6) 跟踪。

托盘提供显示/隐藏、Win 键唤起启用/暂停/恢复和文字退出。Win 键原型每次启动默认关闭，托盘失效时停止接管并保留任务栏恢复方式；系统开始按钮仍为原生入口。历史 [Issue #22](https://github.com/xiaodaozhi/xdstarter/issues/22) 的[启用菜单](validation/issue-22/win-key-enable-sample-native.png)与[暂停菜单](validation/issue-22/win-key-pause-sample-native.png)是当时的独立样例，不作为修订 2.3 的整体视觉基准。

## 开发切图与重建

| 素材 | SVG 与 PNG 规格 |
| --- | --- |
| 深浅主题界面图标 | 24×24 viewBox；100/125/150/175/200/300% 对应 24/30/36/42/48/72 px |
| 圆形头像 | 深浅 SVG；PNG 32/40/48/56/64/96/128/192/256 px，基准 32 DIP |
| 中性磁贴底图 | 48×48、104×104、216×104 DIP，五档缩放；圆角 6 DIP，无应用图标文字 |
| 软件图标 | ICO 内 16/20/24/32/40/48/64/96/128/256 px RGBA PNG；独立 PNG 另有 512/1024 px |
| 透明标志 | 规范三阶磁贴 SVG 与 PNG，无应用图标外框 |

`assets/` 共 41 SVG、241 PNG、1 ICO，即 283 个素材文件，另含清单与说明。修订 2.3 增加刷新和地球分类矢量及各档缩放，已更新素材清单；中性磁贴底图是开发参考，运行时根据 tokens 绘制。不要把完整设计图或带示意图标的磁贴截图贴作界面。许可证尚未确定，本轮不另行选择授权。

生成与校验脚本仅使用 Python 标准库；PNG 通过 [render_svg.py](source/render_svg.py) 调用 Qt 6.8.2 的 `QSvgRenderer` 离屏导出。辅助程序已存在时，仅重建当前桌面稿：

```powershell
./build/tooling/Scripts/python.exe docs/design/source/build_desktop_grid.py
./build/tooling/Scripts/python.exe docs/design/source/verify_menu_design.py
./build/tooling/Scripts/python.exe docs/design/source/verify_tile_grid.py
./build/tooling/Scripts/python.exe scripts/check_repository.py
```

缺少辅助程序时，按[开发环境](../technical/development.md)准备固定工具链；`./scripts/build-windows.ps1 -Preset app-release -DesignAssets` 会构建辅助程序并重建桌面稿、交互表面稿及开发素材全套。不要仅为更新桌面稿而重导出全部素材。生成器在 `source/`，当前输出在 `mockups/v2/`；可用 `XDSTARTER_SVG_RENDERER` 显式选择已安装的 `rsvg-convert` 做渲染器比较。既有品牌板和网格规范图的 `build_brand.py`、`build_tile_grid.py` 使用 `rsvg-convert`，不属于本轮桌面重建。

仓库检查覆盖当前画布、对称边距、单层边框、工具入口、圆形头像、共享磁贴几何、PNG 尺寸、素材透明度与哈希和 ICO 负载。字体、图标、主题观感及运行时状态仍须目视与实际交互核对。源文件改动后重新生成 SVG/PNG 并核对，版本规范见[文档规范](../standards/documentation.md)；历史探索提示词见[初始提示词](source/prompts/image-prompts.md)与[磁贴修订探索](source/prompts/image-prompts-v2.md)。

## 修订 2.3 交互表面

菜单使用 10 DIP 圆角表面、6 DIP 内距、36 DIP 菜单行、5 DIP hover 圆角及主题色 pressed 反馈；禁用行降低透明度，不显示额外 hint。浮动反馈用 12 DIP 圆角、16 DIP 内距、主题细边框和状态色侧标，不挤占列表或网格布局。搜索清除按钮 32×32、16 DIP 圆角。磁贴常态边缘从自身背景色提亮，避免统一边框与底色不协调；键盘焦点独立门控。提示等待 600 ms，菜单和磁贴动画使用 tokens 中的有界时长。

交互状态图由 `source/build_interaction_states.py` 直接读取运行时 tokens、共享字体和矢量图标导出，覆盖深浅菜单默认／hover／pressed／Tab 焦点、禁用项、反馈、提示、分类和文件夹。重建：`python3 docs/design/source/build_interaction_states.py`。这些是设计稿，运行时抓帧与自动断言单独记录。

字体采用系统安装的 Microsoft YaHei UI，QML 控件与普通 Text 共享实际解析后的应用字体。Windows 离屏字体扫描遗漏 TTC 字体集合时，工具注册系统已有的微软雅黑常规／粗体集合；不拷贝或分发字体。自动检查验证实际 QFontInfo／QRawFont 的字体族和中英文字形，避免缺失别名静默回退到 Agency FB。Qt SVG 稿使用明确的单一字体族。

分类选择按钮中的字母、# 和地球图标均居中；运行时按字形实际边界排布，SVG 采用按钮中心的文字锚点，不再以相同左偏移排放宽度不同的字母。分类选择与列表中的分类入口共用图形组件。

## 磁贴按压与内容对齐

[Issue #50](https://github.com/xiaodaozhi/xdstarter/issues/50) 在既有外观上补充按点透视反馈，[Issue #58](https://github.com/xiaodaozhi/xdstarter/issues/58) 修正其平面姿态和恢复中重按。共享`motion.tilePress`使用90 ms按下、180 ms复原和180 DIP视距。按点方向先归一化，再用一次合成轴旋转求姿态；48×48、104×104和216×104 DIP磁贴分别按该方向在真实平面上的跨度限制最终倾角，最多10度且整个平面最大后退不超过16 DIP，不能分别限制两轴后叠加。边缘和角落按压围绕对侧边或角，按下侧后退，对侧接近原位；方形对角按压的两枚相邻角可以等深。中心收缩至0.985并下沉2 DIP，随远离中心衰减；恢复途中重按从当前姿态连续插值。

三种磁贴在正常显示尺寸下，常用边缘和角落按点应产生可感知的轮廓收退与边长差，图标和文字随平面同步透视。自动检查覆盖方向、对称性和常用按点的最低可见位移与边长差；运行时还需对照同尺寸的按下前、按住与恢复画面，检查动画中的实际反馈。

带透视分母的矩阵统一变换背景、图标和文字，保留固定的输入几何与拖动偏移。参考微软[指针点击动画指南](https://download.microsoft.com/download/2/4/A/24A81A29-77CF-4AA5-967E-64E42554F21B/UWP%20app%20design%20guidelines%20v1509.pdf)中的按下缩放／倾斜及松开恢复原则；投影使用Qt的[matrix4x4](https://doc.qt.io/qt-6.8/qml-matrix4x4.html)。具体参数是本项目取值，不作为Windows私有动画参数。聊天中生成的透视说明图不作为视觉参考或验收基准；既有v2稿与tokens保持原有身份，本次运行时证据见[测试策略](../technical/testing.md)。

磁贴内容视口、分组操作、右上关闭按钮及窄模式全部应用共用内容右边界；滚动条置于现有24 DIP外侧留白。磁贴仍按48/8几何排布，任意非吸附宽度可剩下不足一列的自然空白；吸附宽度下整列右缘对齐，见[布局说明](../technical/tile-layout.md)。关于框的“项目主页”文本与正文左边界对齐，不采用默认按钮的水平缩进。
