# 开发素材

本目录提供与当前设计源共享的原创界面图标、圆形占位头像、中性磁贴底图和软件品牌图标。[Issue #14](https://github.com/xiaodaozhi/xdstarter/issues/14) 已将快捷栏 SVG、圆形头像及软件图标接入运行时代码；其余素材按用途供后续组件使用，完整素材清单不等于全部已呈现在界面。第三方应用的示意图标不在本目录；实际应用图标由运行时从系统读取。构建和实际窗口结果统一见[测试策略](../../technical/testing.md)。

[素材总览 PNG](asset-overview.png) / [SVG](asset-overview.svg) 可用于浏览；[manifest.json](manifest.json) 是逐项尺寸、缩放、主题、源文件、SHA-256 和用途清单。总览中的大尺寸应用图标经过缩小展示，请按清单使用原始文件。

| 类型 | 路径与规格 | 使用说明 |
| --- | --- | --- |
| 界面图标 | `ui/dark/`、`ui/light/`，SVG 24×24 viewBox；PNG 24、30、36、42、48、72 px | 基准 24 DIP，对应 100%、125%、150%、175%、200%、300%；统一 1.75 单位圆角线描 |
| 圆形占位头像 | `ui/{theme}/avatar.svg`；PNG 32、40、48、56、64、96、128、192、256 px | 设计显示基准 32 DIP；SVG 源画布 64×64，圆形 mask 保证四角透明；真实用户照片也应圆形裁切 |
| 中性磁贴底图 | `tiles/dark/`、`tiles/light/` | 48×48、104×104、216×104 DIP；每档包含 100%、125%、150%、175%、200% PNG，圆角 6 DIP，无应用图标和文字 |
| 软件图标 | [xdstarter.ico](app/xdstarter.ico) | 16、20、24、32、40、48、64、96、128、256 px 十个 32 位 RGBA PNG 帧；16/20 px 采用像素网格光学校正 |
| 软件图标 PNG | `app/xdstarter-app-icon-{size}.png` | ICO 各尺寸，另含 512、1024 px；按物理像素选择，不绑定单一 DIP |
| 透明品牌标志 | `app/xdstarter-mark.svg` 与同尺寸 PNG | 沿用已确认的三枚阶梯状圆角磁贴，无应用图标外框 |

QML 应优先用 SVG 或原生几何绘制，按 DIP 布局并由设备像素比选择像素资源。磁贴底图用于参考或需要位图的场景；优先根据 [tile-grid.json](../tokens/tile-grid.json) 原生绘制，保持三档共用几何、动态主题和缩放。不要将整张界面设计图裁切后嵌入产品。

## 当前资源映射

| 用途 | 构建与运行时入口 |
| --- | --- |
| 快捷栏图标及头像 | `xdstarter_add_ui` 将深浅主题 SVG 打包到 `qrc:/xdstarter/ui/{theme}/`；`ShortcutRail.qml` 使用 64 DIP 栏宽、48×48 DIP 命中区、32 DIP 头像与 24 DIP 图标 |
| 品牌标志 | `qrc:/xdstarter/app/xdstarter-mark.svg`，供窗口标题区域使用 |
| 应用与窗口图标 | `qrc:/xdstarter/app/xdstarter-app-icon-{size}.png`，16–1024 px 的十二档 PNG 加入 `QIcon` 并设置应用及窗口图标 |
| Windows EXE 图标 | `src/app/windows_resources.rc.in` 在构建时引用 `app/xdstarter.ico`，将十帧 ICO 编入 EXE |
| 原生托盘图标 | 从自带 32 px PNG 创建受管理的 HICON，由 `Shell_NotifyIcon` 使用并在停止时释放 |
| 中性磁贴底图及其他 PNG | 保留为参考与可选切图；当前磁贴按共享 tokens 原生绘制，侧栏优先使用 SVG |

产品与 GUI 测试使用同一资源打包函数，不依赖启动工作目录；新增素材需显式加入构建资源列表。圆形头像目前是占位图，未加载真实用户照片。深浅主题切换只更换对应 SVG，不改变点击区和图标尺寸。

素材接入和运行窗口效果分别验收。[Issue #15](https://github.com/xiaodaozhi/xdstarter/issues/15) 接入无边框与外部焦点收起；本轮 [#16](https://github.com/xiaodaozhi/xdstarter/issues/16) 改由 Canvas 自绘 12 DIP 圆角、单层边框和阴影，正文保持主题实色，透明外窗每边留 24 DIP，Windows 原生装饰被禁用。`visual.json` 的 `menuChrome` 同时定义 8 DIP 工作区边距和 1 DIP 边框；左侧与底部按可见面板测量，阴影留白不改变边距。唤起仅读取显示器、工作区与任务栏 DPI，不等待 UIA 开始按钮识别；隐藏阶段完成定位后再显示，不再使用 DWM frame 补偿。此轮没有重生成或修改 255 项开发素材，v2 修订 2.1 整图保留的历史开始按钮锚点也不用于新位置验收；背景材料与系统输入接管仍待后续实现。实际结果见[测试策略](../../technical/testing.md)。

所有 UI、头像、磁贴和品牌 PNG 均保留透明通道；只有素材总览包含实色背景。字体不随素材分发。仓库许可证尚未确定，本目录不另行指定许可证。

在仓库根目录重新导出并检查：

```sh
python3 docs/design/source/build_development_assets.py
python3 docs/design/source/verify_development_assets.py
python3 scripts/check_repository.py
```

矢量和 ICO 容器生成仅依赖 Python 标准库；PNG 通过项目的 [render_svg.py](../source/render_svg.py) 使用 Qt 6.8.2 的 SVG 渲染器离屏导出。校验不需要 Qt，逐项验证尺寸、透明像素、圆形边界、源与导出哈希以及 ICO 帧目录与 PNG 负载。

## Issue #41 更新

开发素材增加同一 24 DIP 矢量系统的刷新与地球分类图标及六档 RGBA 导出，完整清单为 283 项。总览按当前图标数均匀排布，不裁掉新增图标。此前“255 项未改变”仅属于 #16 的历史状态；当前设计与运行时采用 v2.3、后台首启及自适应任务栏策略，见[设计说明](../README.md)和 [ADR-0013](../../technical/adr/0013-resident-editable-menu.md)。

生成器源文件散列按仓库 `.gitattributes` 的 LF 字节计算，避免 Windows 编辑器的 CRLF 与 Git 提交字节不同；SVG／PNG／ICO 素材仍检查精确原始字节。
