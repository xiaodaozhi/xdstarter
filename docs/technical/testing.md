# 测试策略与验收

## 当前自动检查

`python3 scripts/check_repository.py` 检查本地文档/图片链接、JSON/SVG/PNG 基本有效性、当前设计尺寸、Logo 透明通道及 SVG 网格几何。该检查不运行 Qt，也不代表 Windows 原型通过。

GitHub Actions 在 push/PR 上运行相同检查。设计重建后应目视查看 PNG，核对字体、应用图标、长名称、间隙及辅助功能信息的表达。

## 当前：统一边距与自绘外观（Issue #16）

2026-10-20，[#16](https://github.com/xiaodaozhi/xdstarter/issues/16) 修复托盘唤起时可信/失败锚点切换与显示后异步移动，改用透明外窗、QML 自绘单层圆角/边框/阴影，并禁用 Windows 原生装饰。可见面板距工作区左侧和底部统一为 8 DIP；外窗每侧 24 DIP 阴影留白不算作面板边距。Windows 产品入口初始隐藏，worker 的 `queryMenuAnchor()` 仅读取显示器、工作区和任务栏 DPI，不执行 COM/UIA，定位后再显示；旧 UIA 研究入口保留显式 probe，不能将产品兼容保留的 `trusted` 字段解释为唤起需要识别按钮。范围与取舍见 [ADR-0007](adr/0007-custom-menu-chrome.md)。继续使用本项目 Qt 6.8.2 与正式 v142/SDK19041 基线，不改变布局 schema 或磁贴几何。

正式 Release、QML lint 与 10/10 CTest 通过（13.64 秒），日志为本地 `build/custom-chrome-check.log`；GUI 为 32/32，host 为 17/17。自动检查覆盖可信/不可信兼容字段下几何一致、DPI/极值、面板与透明外窗坐标、隐藏阶段定位与待显示取消、过期查询不重开、原生装饰抑制、侧栏背景裁切及阴影/透明角像素。程序身份为：

| 程序 | SHA-256 |
| --- | --- |
| 产品 `xdstarter.exe` | `97054e7dc0d61aceed524c425a565b72313506b2d571defa6730c9d38d0b85f3` |
| GUI `xdstarter_ui_tests.exe` | `156aebb50586b94a7c17d94ad4addaf62b2db7d9e1062652fd6a35c4f15d34dc` |
| Host `xdstarter_desktop_window_host_tests.exe` | `2757cecf5ba8257ca9e0b08a7093584f9bb0291e1e720a90ba2b81908fe1a71f` |

本轮 CTest 默认 GUI 为 offscreen/software。该后端的 `QQuickWindow.grabWindow()` 会返回不含 alpha 的 RGB backing store，因此外观测试通过 Canvas `toDataURL("image/png")` 读取实际绘制的原始 RGBA PNG，未合成透明通道；侧栏透明背景及控件不越过圆角的几何另行检查。这验证 Canvas 绘制结果，不是完整 Windows 合成窗口截图，也不证明 DWM 实际外观。Windows 原生路径要求直接检查完整 `grabWindow()`，不能沿用离屏回退作为其通过证据。

GUI 测试程序已实现显式 `--native-chrome-probe`：真实 Windows QPA/RHI 与工作区查询、模拟托盘 `toggleRequested`，检查十二轮显示/隐藏的位置及深浅主题完整截图；不注册或点击真实系统托盘，不读取用户布局或真实应用目录。本轮尚未运行该 probe，也未运行新版 Windows 缩放矩阵、实际托盘重复唤起或目视外观验收。内部菜单、拖拽、编辑、外部收起及新进程布局恢复仍需用本轮程序作实际回归。下方 #15/#14 的 hash、用户确认与待验收事项保持历史身份，不作为本轮通过结论。

## 历史：原生外观菜单弹层验证（Issue #15）

2026-10-20，[#15](https://github.com/xiaodaozhi/xdstarter/issues/15) 承接 #14 的桌面恢复基础，实现无边框菜单、Windows 圆角/阴影请求、外部焦点收起，以及内部菜单、拖拽和编辑的清理与恢复。该任务只覆盖 [#6](https://github.com/xiaodaozhi/xdstarter/issues/6) 的窗口与焦点部分，不安装输入 Hook，也不接管 Win 键、开始按钮或原生开始菜单。

### 构建与自动检查

正式入口继续使用项目自有 Qt 6.8.2、MSVC 19.29.30153.0/v142、Windows SDK 10.0.19041.0 与固定 Python/CMake/Ninja 工具，Release、QML lint 和 10/10 CTest 通过（13.06 秒），日志为本地 `build/popup-build-verified.log`。GUI 为 30/30，host 为 14/14；精确程序身份分别记录为：

| 程序 | SHA-256 |
| --- | --- |
| 产品 `xdstarter.exe` | `79555e72b5991e3c0cde1ef47d790451fe076d12ca6a3e6bdc0dad425b2c7a11` |
| GUI `xdstarter_ui_tests.exe` | `7d66601675b0068ddb24f403a4af10905ddb860793ce70925560fdf08dce6f12` |
| Host `xdstarter_desktop_window_host_tests.exe` | `0a665a74b785395957959d21da783201a5c2d5fa3f8a47f43e01e211b745e3aa` |

新增检查覆盖无边框且不透明的窗口、共享菜单圆角 token、键盘/鼠标主动收起、`Popup.Item` 内部菜单、呈现清理契约、越界释放取消未提交拖拽，以及 host 的外部焦点收起、内部临时窗口与快速返回保留、拖拽按下期间延后和无托盘最小化。测试使用隔离配置和注入平台；Qt 默认 offscreen/software 结果不当作 Windows 合成器外观或原生托盘通过证据。

`scripts/check_windows_icon.py` 对上述产品 PE 作只读审计，命名组 `IDI_XDSTARTER`、语言 1033，十帧 16/20/24/32/40/48/64/96/128/256 px 的元数据与 PNG 负载逐字节匹配源码 ICO，日志为 `build/popup-icon-audit.log`。仓库检查通过 432 文件和 255 项开发素材。

Windows QPA / Direct3D 11 的 100/125/150/175/200% Qt 有效缩放复核全部通过，每档 GUI 为 30/30，耗时分别为 54.514/33.412/29.427/32.469/33.482 秒。基础 DPR 为 1.5，实际 DPR 约为 1、1.25、1.5、1.75、2；30 份 JSON 与 60 张窗口/网格 PNG 的像素检查通过。本地 `build/validation/ui/native-matrix.json` 记录上述 GUI hash，日志为 `build/popup-native-matrix.log`；该矩阵不修改 Windows 显示设置，也不代替真实系统缩放切换或混合 DPI 验证。

### 实际窗口：隔离权限下的降级路径

实际环境为 Windows `10.0.26300.0`、Qt 6.8.2、Windows QPA / Direct3D 11、Intel Iris Xe、144 DPI。两轮产品均使用上表相同 hash 与 `build/validation/popup-native/layout.json` 隔离文件，不修改日常用户布局。

第一轮在执行环境的沙箱限制内启动；托盘注册与可信 UIA 识别不可用，窗口使用工作区内的明确位置回退。日志记录 `trustedAnchor: false`，初始截图元数据为起点 `(8, 500)`、逻辑尺寸 980×680，不能将这一轮表述为可信开始按钮对齐。产品外观请求被 DWM 接受，但该返回本身不证明圆角和阴影目视通过。

在实际菜单进入编辑后，将 Epic Games Launcher 小磁贴从 column 0 拖到 column 1、row 0，磁盘隔离布局反映同一 tileId 与 targetId 的新列坐标。打开内部右键尺寸菜单并按 Esc，主窗口保持显示与编辑状态。激活本次自建的空白 `manualFocusWindow` 后，产品日志记录 `external-focus`、`trayRecovery: false`，按降级策略最小化到任务栏；恢复后仍处于编辑模式。随后经 QML 文字退出入口正常结束，`first-exit.json` 记录代码 0。这一轮不作为真实托盘恢复验收。

### 实际窗口：正常桌面权限与用户协助托盘恢复

第二轮以正常桌面权限启动同一产品，实际应用图标、原生托盘与 UIA 可用。可信识别 StartButton 为 `(0, 1528, 83, 72)`，工作区为 `(0, 0, 2400, 1528)`；定位日志记录可见窗口请求矩形 `(0, 496, 1470, 1020)`，底部间隔为 12 物理像素，即 8 DIP。独立截图元数据记录实际屏幕起点 `(0, 496)`、逻辑尺寸 980×680，与按钮左侧 `x = 0` 一致；截图逻辑尺寸不作为独立测得的完整物理边界。

实际窗口从浅色切换为深色并进入编辑，随后点击本次自建空白窗口；产品消失，日志记录 `external-focus`、`trayRecovery: true`。自动化工具无法直接选择隐藏的产品窗口或托盘图标，因此由用户亲自点击 XDStarter 托盘恢复。用户回复“恢复正常，圆角和阴影可见”；本次圆角/阴影目视结果来自用户确认，和日志中的 DWM 请求接受状态分别记录。该操作不作为自动化点击托盘或原生托盘右键菜单验收。

托盘恢复后取得的实际窗口截图 `tray-restored-editing.jpg` 显示浅色主题、三枚真实目标图标、column 1/2/3 的磁贴、编辑状态与保存成功提示。用户在目视测试期间已从深色切回浅色，最终隔离配置实际保存为 `preferences.theme = light`，不按较早深色截图推断重启主题。

### 受控中断与新进程恢复

正常桌面进程在多次托盘恢复与原生矩阵焦点交接后，自动化工具无法选择隐藏的产品窗口。为验证已有自动保存快照的恢复，先核对本次进程的精确 PID 41376、EXE 路径和 `--layout-file` 参数，保存 `layout-before-restart.json`，然后仅对该隔离产品进程执行受控中断。`controlled-interruption.json` 记录目标与原因，`desktop-exit.json` 为 -1；这次不是正常退出，不作为 QML 文字退出或退出 flush 通过证据。第一轮沙箱进程的 QML 文字退出代码 0 单独保留。

随后以正常桌面权限启动同一产品 hash 的新进程，UI Automation 树显示三个真实目标名称和“已恢复保存的布局与主题”。`layout-after-restart.json` 与中断前快照逐对象一致：schemaVersion 2、相同 targets/groups/tileId/targetId，三枚磁贴仍为 column 1/2/3、row 0、small/small/standard，主题为 light。该证据支持新进程读取已保存布局，不将编辑模式作为持久化偏好。

新进程的窗口激活被系统拒绝，自动化工具两次返回 `failed to activate captured window`；所得画面是桌面背景，未作为产品窗口证据保存。按工具要求停止进一步 UI 输入，已转由用户目视确认新进程位置与主题并从 QML 电源菜单退出。该轮用户目视与正常退出结果仍待取得，当前不声明新进程窗口截图或正常退出通过。

工件位于本地 `build/validation/popup-native/`：各轮 `*-process.json`、`first-exit.json`、`desktop-exit.json`、`controlled-interruption.json`、`initial-capture-bounds.json`、`desktop-capture-bounds.json`、`light-initial.jpg`、`internal-size-menu.jpg`、`dark-edited.jpg`、`tray-restored-editing.jpg`、`sandbox-fallback-about.jpg`、`layout-before.json`、`layout-before-restart.json`、`layout-after-restart.json`、`layout.json` 及各轮日志。执行权限导致的托盘/UIA 降级与正常桌面结果分开记录，不据此归因为所有用户桌面环境的失败。这些工件位于忽略的构建目录，不提交到 Git。

混合 DPI 跨屏、真实 Windows 系统缩放切换、自动隐藏任务栏、Explorer 重启、原生托盘右键菜单、远程会话、高对比度及系统效果关闭、屏幕阅读器、Win 键/开始按钮接管和 Mica/Acrylic 材料本轮仍未验收。当前本机结果不扩大为所有 Windows 版本或完整系统接管支持。

## 历史：普通窗口桌面集成验证（Issue #14）

2026-10-20，[#14](https://github.com/xiaodaozhi/xdstarter/issues/14) 承接 #13 的设计素材，完成普通窗口快捷栏、圆形头像、软件图标、托盘恢复和只读开始按钮定位的本轮验证。以下记录对应产品 SHA-256 `154d7cff464dbf09473401462c9fe319b03ce7d03a36f2f41d572d0747f11182`；下方应用目录与布局记录保留原版本边界。

正式入口使用项目自有 Qt 6.8.2、MSVC 19.29.30153.0/v142、Windows SDK 10.0.19041.0 与固定 Python/CMake/Ninja 工具完成 Release、QML lint 和 10/10 CTest（11.09 秒）。新增 `platform.desktop`、`services.desktop_shell`、`ui.desktop_window_host`；GUI 26/26 通过。日志为本地 `build/runtime-shell-build.log`。后台动作与 host 测试注入假平台，覆盖白名单、成功/失败、singleflight、查询代次、关闭后旧结果失效、无托盘恢复、退出清理及应用关闭策略，不据此宣称实际 Explorer 重启已通过。

Windows QPA / Direct3D 11 的 100/125/150/175/200% Qt 有效缩放均为 GUI 26/26；基础 DPR 1.5，实际 DPR 约为 1、1.25、1.5、1.75、2，30 份 JSON 与 60 张窗口/网格 PNG 的像素检查通过。GUI 程序 SHA-256 为 `4bff72632e7fcb60153079bdd28fd6a7c99fafa29d0594069992484c45db5802`，与产品 hash 分别记录。当时工件为本地 `build/validation/ui/native-matrix.json`、`native-{100,125,150,175,200}/` 和 `build/runtime-shell-native-matrix.log`；共享的矩阵路径可被后续版本覆盖，#14 历史身份以本节 hash 与对应历史日志为准。该矩阵未修改 Windows 显示设置。

只读 `scripts/check_windows_icon.py` 对实际产品 PE 的资源目录进行审计：命名组 `IDI_XDSTARTER`、语言 1033，含 16、20、24、32、40、48、64、96、128、256 px 十帧 32 位 RGBA PNG，目录元数据、CRC 与每帧负载逐字节匹配源码 ICO；脚本不加载或执行 EXE。日志为 `build/validation/runtime-shell-native/exe-icon-audit.log`。工具单测 13/13 通过，包含原有 4 项 GitHub 初始化边界及新增 9 项 PE/ICO 正反向检查。

### 普通产品窗口与新进程恢复

环境为 Windows `10.0.26300.0`、Qt 6.8.2、Windows QPA / Direct3D 11、144 DPI。显示器物理矩形为 `(0, 0, 2400, 1600)`，工作区为 `(0, 0, 2400, 1528)`。只读 UIA probe 可信识别开始按钮 `(0, 1528, 83, 72)`；原生定位记录的可见窗口请求矩形为 `(0, 449, 1474, 1067)`，截图元数据独立记录实际屏幕起点 `(0, 449)`，与按钮左边 `x = 0` 相符。定位记录中的底部间隔为 12 px，即本机 8 DIP；没有把截图的 983×711 逻辑尺寸当作独立测得的完整物理边界。进程日志记录 `trustedAnchor: true` 与定位成功。

使用隔离布局启动后，实际窗口恢复三枚真实磁贴与深色主题，快捷栏、圆形头像和软件图标可见。点击“文档”打开新的 Explorer Documents 窗口，XDStarter 收起；只关闭该次新开的文档窗口，未读写其中的用户文件。原生托盘未出现在自动化工具可操作窗口列表，用户亲自点击后回复“已恢复可继续测试”；随后实际观察到同一个产品窗口 ID 恢复，日志再次记录同一可信定位矩形。该证据是用户协助的托盘点击恢复，不是自动化点击或原生托盘右键菜单验收。

恢复后切换浅色主题，查看“电源与退出”文字菜单，确认仅启用退出 XDStarter，Esc 关闭菜单并保留主窗口；“关于”显示当前托盘与定位状态。通过 QML 电源菜单退出，进程代码 0。新进程读取同一隔离文件，恢复浅色主题和三枚真实磁贴，targets/groups 与原隔离布局逐对象一致，再经同一明确退出入口正常退出，代码 0。自建产品窗口与新开的文档窗口均已关闭，桌面已交还用户；未修改日常用户布局。

工件位于本地 `build/validation/runtime-shell-native/`：`anchor-probe.json`、`capture-bounds.json`、`dark-restored.jpg`、`light-restored.jpg`、`about-integration.jpg`、`light-restarted.jpg`、`first-process.json`、`restarted-process.json`、`layout.json`，以及各轮 `stdout`/`stderr` 中的定位记录；汇总为 `result.json`，记录两进程相同产品 hash、三次定位、实际截图起点及两次正常退出。这些工件位于忽略的构建目录，不提交到 Git。

本轮未实际操作原生托盘右键文字菜单，下载、设置与账户入口也未专门通过真实平台打开；这些路径的当前证据来自代码边界和注入测试。混合 DPI 跨屏、真实 Windows 系统缩放切换、自动隐藏任务栏、Explorer 重启、远程 CI、屏幕阅读器、Win 键/开始按钮接管、DWM 材料、无边框弹层及外部点击关闭均未验收。当前是带系统边框的普通窗口；本机可信定位与用户协助托盘恢复不扩大为所有 Windows 版本或系统接管支持。

## 工程原型检查

正式工具版本由 [toolchain.json](../../toolchain.json) 声明，获取、版本检查和构建命令见[开发环境](development.md)。本项目拥有自己的 `build/tooling/` 和 `build/Qt/` 缓存，测试不要求 xdzip 仓库存在。

```powershell
./scripts/build-windows.ps1 -InstallTools -Python python -Preset app-release -Fresh
```

入口检查固定工具、选择 v142/SDK19041、配置、编译、运行 `xdstarter_qmllint` 和 CTest，失败即停止。`core-debug`/`core-release` 可不依赖 Qt，仍使用 C++20；`app-debug`/`app-release` 开启以下十套 CTest。本表说明目标覆盖，当前与历史变更的实际结果分节记录；GUI 默认 offscreen/software，不将离屏结果等同于 Windows 原生窗口。

| CTest | 覆盖 |
| --- | --- |
| `core.tile_grid` | 纯 C++ 三档几何、共享边界、负坐标、非法尺寸、横向越界与整数溢出；Release 下仍执行真实检查 |
| `services.prototype` | 稳定标识、三档矩形、方向邻接、预览不变性、组内/跨组移动、尺寸切换、固定/重复固定/取消固定、碰撞及边界拒绝 |
| `services.layout_store` | schema 1/2、真实目标与空布局校验、兼容保存及旧字节备份、引用/数量/坐标校验、损坏保留、未来版本只读、写入失败及有界读取 |
| `services.application_catalog` | 注入平台检查后台线程、目录结果、刷新失败保留列表、图标/缓存、重复请求和启动结果；不执行真实用户应用 |
| `platform.applications` | Windows 隔离 `.lnk` 扫描与去重、中文/特殊字符、目标失效、AUMID 校验、原生图标及受控 helper 启动参数/工作目录 |
| `platform.desktop` | 桌面动作白名单、面板工作区边距/极值与阴影留白转换、DWM 原生装饰抑制及 HWND 所有权边界、托盘旧会话事件失效；常规测试不打开真实用户目录，UIA probe 显式执行 |
| `services.desktop_shell` | 假平台上的后台动作、成功/失败、重复请求拦截、定位查询合并/失效和线程析构等待 |
| `app.qml_smoke` | 产品入口加载 QML 与视觉 token 后退出，加载失败或 QML warnings 使测试失败；不读写布局文件 |
| `ui.prototype` | 同一产品 QML、controller 与视觉资源上的鼠标/键盘、主题、编辑、保存恢复、只读和真实窗口像素检查 |
| `ui.desktop_window_host` | 假托盘/定位平台上的隐藏阶段定位、待显示取消/过期查询、关闭后恢复、内部/外部焦点、拖拽延后、无托盘与托盘丢失恢复、shutdown 和关闭策略还原 |

GUI 用例保留小磁贴 ToolTip/QAccessible 名称、长名称省略、480×480 双向滚动、方向导航和主题几何回归，覆盖鼠标组内/跨组拖拽、非法落点与 Esc 取消、右键/Shift+F10 三档尺寸、冲突拒绝、Ctrl+方向键编辑、重建窗口恢复布局与主题、保存失败重试、未来配置只读，以及深浅主题下尺寸菜单的高亮/按下文字与勾选对比度（至少 4.5）。目录固定/取消固定、启动成功/失败生命周期及失效目标恢复继续保留；桌面集成新增主题资源、圆形头像和快捷栏几何/键盘提示、受控动作结果、电源文字退出及关于状态检查。GUI 使用注入平台，真实 Shell 启动另在平台和产品窗口验证。测试使用隔离文件，不改日常布局；QML warnings 使测试失败。辅助功能名称检查不代表屏幕阅读器焦点已经验收。

[应用 CI](../../.github/workflows/application-build.yml) 读取同一工具清单，Ubuntu job 构建核心；Windows 2022 job 明确作为附加 MSVC 2022 兼容验证，不冒充 v142 精确基线。工作流尚未推送运行，Actions 额度问题仍由 [#12](https://github.com/xiaodaozhi/xdstarter/issues/12) 跟踪。

## 历史：真实应用目录与固定操作验证

2026-10-20，真实应用目录与固定操作：已使用本项目 Qt 6.8.2、v142/SDK19041 和正式 CMake/Ninja 工具完成平台及 fixture 目标的局部 Release 构建，`platform.applications` 通过（2.25 秒）。测试只在 `build/app-release/platform-fixture-*` 创建快捷方式，枚举重复根时仍保留三枚不同快捷方式；实际验证中文/空格/`&` 路径、不同参数身份、失效目标保留及错误分类、删除后不可用、未安装 AUMID 和尾换行拒绝、64 像素原生 Shell 图标。

同一用例通过 `ShellExecuteExW` 启动测试构建的受控 helper，核对中文参数、含 `&` 的单个字面参数及快捷方式工作目录，结果写到隔离 JSON 后清理；没有启动未知用户软件或修改系统开始菜单。日志为本地 `build/platform-validation.log`。该结果不等于 AppsFolder 全目录或产品 GUI 已验收。

### 前一轮构建：按键激活修复后的回归

原生 125% 用例发现固定/取消固定菜单收到 Return 后未触发，现为相应 MenuItem 显式处理 Return/Enter。修复后的产品 SHA-256 为 `9826fbcad29f5c79ea61b02978246963ee76bba7fc9b5cb58a9abaeca661ae9f`，正式 Release、QML lint 和七套 CTest 已通过（7/7，10.72 秒）；GUI 23/23 通过，未出现 QML 或 Qt Pixmap 线程警告。GUI 回归包含目录/磁贴焦点隔离、按目标更新图标版本及菜单键盘操作。工具清单仍为本项目 Qt 6.8.2、Python 3.12.13、CMake 4.4.4、Ninja 1.13.2、MSVC 19.29.30153.0/v142 和 Windows SDK 10.0.19041.0，不把这些结果表述为所有编译诊断均为零。

同轮 GUI 测试 SHA-256 为 `163f1f813eae32f26d791f0753fe387eb7b8db270fd7e8b5db0de89c3cb93d04`，Windows QPA / Direct3D 11 五档 Qt 有效缩放全部通过，每档 23/23。基础 DPR 1.5，实测 DPR 约为 1、1.25、1.5、1.75、2；30 份 JSON 和 60 张窗口/网格 PNG 的像素检查通过。当时的 `build/validation/ui/native-matrix.json` 记录该轮 GUI 程序校验值；这个路径可被后续矩阵更新，历史版本以本节和该轮回归汇总为准，不将测试可执行文件的 hash 当作产品 hash。

该轮回归汇总为本地 `build/validation/application-final-regression.json`。修复后普通产品窗口已打开，但手动复核在输入操作前中止，随后仅清理已确认路径的隔离产品进程；这不作为正常退出或新产品普通窗口交互通过记录。下面普通窗口的完整手工证据属于修复前的 `89edf…` 构建，不能用新测试结果或新 hash 覆写其历史身份。

### 修复前构建：普通产品窗口手工验证

普通产品窗口已实际读取 281 个目录项，覆盖用户/公共 `.lnk` 与 packaged 应用及原生图标；以新进程打开同一隔离文件，恢复两枚桌面快捷方式小磁贴（48×48 DIP）及画图 packaged 标准磁贴（104×104 DIP），位置为 column 0/2/3、row 0，主题 dark。恢复前后布局字节相同，没有在读取阶段自动写回。截图为本地 `build/validation/catalog-native/restored-three-tiles.jpg`，进程清单为 `restored-process.json`；该轮产品 SHA-256 为 `89edf1a293ad7d7e1acb118d2b285997131e3b9fe8b58e15c0dcfbe2846ffc08`。

随后点击真实画图磁贴，通过 AUMID 打开“无标题 - 画图”空白窗口，产品收起，未编辑或保存用户文件。隔离失效快捷方式用例保存为 column 2/row 2/wide、light；实际窗口保留宽磁贴及红色失效标记，启动失败显示“快捷方式已移动或删除”，窗口保持可见。通过真实鼠标菜单取消最后一枚后，主 JSON 的 targets 与 tiles 均为空，`.bak` 保留前一枚有效磁贴；退出码 0。新进程恢复空布局及 light 主题，实际窗口与磁盘一致，再次退出码 0。

修复前的产品验收汇总为本地 `build/validation/catalog-native/result.json`，截图为 `paint-launched.jpg`、`unavailable-preserved.jpg`、`last-tile-unpinned.jpg`、`empty-restored.jpg`；该组手工验收的产品 SHA-256 均为上述 `89edf…`，不代表该轮 `9826…` 产品已重新完成手动复核。正常三枚磁贴的布局另行保留，失效用例没有删除系统开始菜单中的真实快捷方式，日常用户配置未修改。GUI 中缓存版本用例通过 controller 清理引用，鼠标菜单取消固定由另两个 GUI 用例与该组真实窗口单独覆盖。

该轮 `9826…` 构建的普通窗口手动复核未完成；本轮桌面集成窗口另按上方 `154d…` 记录，不追溯补记旧版本结果。真实权限拒绝未全面覆盖，真实系统缩放切换、混合 DPI 跨屏和远程 CI 仍未验收。进度与补充证据由 [#7](https://github.com/xiaodaozhi/xdstarter/issues/7)、[#9](https://github.com/xiaodaozhi/xdstarter/issues/9)、[#10](https://github.com/xiaodaozhi/xdstarter/issues/10) 跟踪。

## 上一轮工具链与布局验证

2026-10-20，应用目录变更前：正式入口已使用本项目 Qt 6.8.2 SDK 与自有工具环境完成 Release 构建、QML lint 和五套 CTest，5/5 通过，GUI 19 项通过。实际工具为 Python 3.12.13、CMake 4.4.4、Ninja Python 分发 1.13.2，MSVC 19.29.30153.0、linker 14.29.30153.0、v142 14.29.30133、Windows SDK 10.0.19041.0；完整版本记录在本地 `build/app-release/toolchain-validation.json`。生成的 QML C++ 编译经过 Qt 第三方头时存在 C4702 诊断，QML lint 本身通过，不将结果表述为“零编译警告”。

构建首次暴露中文用户临时目录被 MSVC v142 错误解码的问题，正式入口改为只对子进程设置项目 `build/tmp/`；重新配置已能识别编译器并完成 ABI 检查。上一轮五档原生矩阵已完成：Windows QPA / Direct3D 11，基础 DPR 1.5；100/125/150/175/200% 各运行完整 GUI 检查，均为 19 项通过、0 失败，实际窗口 DPR 分别约为 1、1.25、1.5、1.75、2。每档深浅主题 × 三组共六组记录，合计 30 份 JSON、60 张 PNG，像素扫描全部通过。当时的 `native-matrix.json` 记录了 GUI 测试校验值，手工窗口记录也核对了同轮产品校验值；当前构建的独立回归结果见上方记录。进度与证据见 [#4](https://github.com/xiaodaozhi/xdstarter/issues/4)、[#5](https://github.com/xiaodaozhi/xdstarter/issues/5)、[#9](https://github.com/xiaodaozhi/xdstarter/issues/9)、[#10](https://github.com/xiaodaozhi/xdstarter/issues/10)。

上一轮已使用产品普通窗口完成可见 Windows QPA / Direct3D 11 验证：通过实际鼠标拖拽将 A 磁贴从工作组第 0 列/第 0 行移至第 0 列/第 2 行；右键菜单依次由标准切换为小、标准、宽，左上锚点不变；尝试扩宽相邻 B 磁贴时因占用冲突而拒绝。自动保存与显式“保存布局”均成功，JSON 和 `.bak` 存在；Esc 先退出编辑，再关闭窗口。

随后以新进程打开同一隔离配置，界面提示已恢复，A 的工作组/column 0/row 2/wide 与 light 主题均和 JSON 一致。三次产品原生进程全部正常退出，代码 0。工件为 `build/validation/editing-native/light-wide-saved.jpg`、`restored-light-wide.jpg` 和 `result.json`。实际窗口还发现 Basic 尺寸菜单高亮白底白字，现已固定文字/选中配色并增加两主题、三尺寸项的可读性回归；实测高亮文字清晰可见。未修改日常用户配置。

前一轮静态原型已在 Windows 11 10.0.26300、Intel Iris Xe / Direct3D 11、Qt 6.8.2 和同版 MSVC 实测选择、主题、焦点滚动、五档 Qt 有效缩放及 Esc 退出；历史工件位于 `build/validation/prototype-native/`、`layout-native/`、`ui/`。当时辅助工具为 CMake 3.31.10、Ninja 1.13.0、Python 3.11，只保留为历史证据，不代表新清单和新编辑功能验收。混合 DPI 跨屏、真实 Windows 系统缩放切换、系统开始菜单接管和屏幕阅读器焦点仍未验收。

## 隔离配置的实际窗口验证

```powershell
$env:PATH = "$PWD/build/Qt/6.8.2/msvc2022_64/bin;$env:PATH"
& ./build/app-release/bin/xdstarter.exe --layout-file "$PWD/build/validation/manual/layout.json"
```

普通首次运行应显示空“常用应用”和真实应用列表，等待扫描完成，核对用户/公共开始菜单与打包应用名称及实际系统图标。选择不会改变用户文件的已知应用验证列表与磁贴启动；成功收起窗口，失败保留窗口并显示原因。不要为验收批量启动未知应用或卸载用户软件。

从新的隔离文件选择应用并固定到指定组，验证标准尺寸与首个空位、重复固定不新增；进入编辑模式验证鼠标拖拽/跨组、合法与冲突预览、Esc 取消、三档尺寸与键盘编辑。切换主题后等待保存成功或点击保存，关闭窗口再以同一参数启动，核对真实 targetId/启动描述、磁贴标识、分组、格坐标、尺寸和主题；对照磁盘 JSON 与实际窗口，不能只观察状态文字。随后取消固定，验证目录项目仍在、快捷方式未删除，取消最后一枚后重启应恢复空布局。

失效目标使用 build 内测试快捷方式：固定并保存后删除 fixture 快捷方式，再重启检查磁贴位置保留、不可用原因及取消固定；不删除真实开始菜单条目。schema 1 升级单独核对旧样例不启动、读取不写回、下次保存 schema 2 且 `.bak` 保留旧字节。损坏/较新版本/无权限目标使用独立测试文件，保留原输入与备份，不在真实日常配置上制造错误。

快捷栏实际验证选择文档等已知入口，检查新系统窗口打开、产品收起及托盘恢复；不批量打开用户目录或设置。托盘文字菜单与 QML 电源菜单分别验收，不将其中一个通过记作另一个通过。测试无托盘恢复和 Explorer 重注册时，分别注明假平台结果与真实系统操作。

`--smoke-test` 不连接真实目录、桌面动作、托盘或定位服务且无布局 IO 的行为单独验证；正常退出应等待待保存快照落盘，失败应有状态或日志并保留前一有效文件。正常窗口运行不等于已接管 Win 键、开始按钮或 Explorer。

## Windows 运行时截图与有效缩放

完成应用和测试构建后运行。正式默认缓存可直接赋给 `XDSTARTER_QT_ROOT`，或传入其他相同版本 SDK：

```powershell
$env:XDSTARTER_QT_ROOT = "$PWD/build/Qt/6.8.2/msvc2022_64"
./scripts/verify-windows-ui.ps1 -QtPrefix $env:XDSTARTER_QT_ROOT
./scripts/verify-windows-ui.ps1 -QtPrefix $env:XDSTARTER_QT_ROOT -NativeScales
```

第一条在 offscreen/software 下运行交互与截图检查；第二条在可见 Windows 桌面使用 Windows QPA 和默认图形后端。脚本先以 `QT_SCALE_FACTOR=1` 探测当前屏幕基础 DPR，再按 `目标 DPR / 基础 DPR` 校准 100/125/150/175/200% 的 Qt 有效缩放，并核对实际窗口 DPR。此前本机探测基础 DPR 为 1.5、后端 Direct3D 11；脚本不假定其他显示器具有同样数值。

原生输出目录为 `build/validation/ui/native-{100,125,150,175,200}/`，`native-matrix.json` 汇总基础 DPR、实际 DPR、图形 API 和被测程序校验值。每档保存六组深浅主题 × 三组的记录，每组包括完整窗口 PNG、网格裁剪 PNG 和 JSON；默认离屏输出到 `build/validation/ui/offscreen/`。测试对真实 `QQuickWindow` 抓帧并扫描物理像素，检查小/标准/宽磁贴的宽高、8 DIP 间隙、四小磁贴含中缝与标准外框相等、两标准磁贴含间隙与宽磁贴相等，允许光栅化边界最多 1 物理像素误差，同时确认主题切换不改变逻辑几何。

验证脚本将当前进程及其测试子进程的 `TEMP`/`TMP` 临时设为 `build/validation/ui/temp/`，避免受管桌面默认临时目录不可写使 QTemporaryDir 夹具失败；脚本通过 finally 恢复原环境，不修改系统/用户设置或产品保存逻辑。构建入口另使用 `build/tmp/`，两者用途分开记录。

几何抓帧只在测试夹具中把 ToolTip opacity 设为 0，避免浮层遮挡磁贴扫描；JSON 明确记录 `tooltipsHiddenForGeometryCapture: true`。像素取样避开图标字形抗锯齿区域，未修改产品 ToolTip 行为或放宽 1 物理像素容差。ToolTip 可见性与完整文本由独立交互用例验证，不能用这些几何抓帧判定提示浮层表现。

所有截图、JSON 和日志属于本地或 CI 工件，不提交到 Git。失败时保留实际 QPA、图形 API、窗口/屏幕几何、DPR 与像素扫描记录，避免仅凭 JSON 参数或设计稿认定布局通过。

此矩阵不修改 Windows 显示设置，不能替代系统设置中真实切换缩放、混合 DPI 跨屏拖动、拔插屏幕、原生开始菜单接管或屏幕阅读器焦点验收。其他 Qt 版本、MSVC 2022 应用 CI 与 macOS 构建也仍需各自验证。

## 应用测试分层

| 层级 | 场景 |
| --- | --- |
| 核心单测 | 网格矩形、占用冲突、边界、搜索排序、配置校验与恢复 |
| QML/集成 | 模型更新、输入焦点、键盘导航、拖拽预览、缩放截图 |
| Windows 真机 | Win 键/按钮接管、系统组合键、托盘、应用启动、原生材料、异常退出 |
| 发行回归 | 干净机器安装、非管理员启动、升级/降级、卸载和配置保留 |

可逆的纯文案修改以文档与视觉检查为主；新增算法、状态转换和数据恢复应有针对性测试，避免只复述实现的测试。

## 首轮矩阵

- Windows 11 x64：记录精确版本；当前菜单原型本机证据来自 10.0.26300.0，执行环境沙箱降级与正常桌面权限结果分别记录。仍需覆盖用户实际使用的稳定构建与另一稳定构建，不能由一次本机 DWM 请求或普通窗口历史结果扩大兼容范围。
- 缩放：当前提供 100/125/150/175/200% 的 Qt 有效缩放检查；真实系统缩放切换、双屏混合 DPI、屏幕拔插与主屏切换仍需真机验证。
- 任务栏：居中/左对齐、自动隐藏、副屏；Explorer 重启、睡眠恢复与会话切换。
- 输入：左右 Win、连续/长按、Win+E/R/L/数字、开始按钮左右键、中文输入法。
- 视觉与可达性：深浅、高对比度、关闭透明、减少动画、屏幕阅读器与纯键盘。
- 数据：卸载应用、快捷方式失效、无权限目标、写盘失败、配置损坏、较新配置版本。

## 性能目标与方法

参考机记录 CPU、内存、GPU、磁盘、Windows 构建、Qt/编译器和 Release 构建参数。以 100 枚磁贴、1000 个应用目录项作为初始数据集，另外覆盖空布局和较大目录。

常驻后从唤起输入到首帧可交互 P95 ≤200 ms；索引就绪后查询至结果更新 P95 ≤100 ms。至少采集 100 次并区分冷/热启动。记录工作集、私有内存、隐藏时 CPU 和动画时帧耗时，暂不凭框架名称承诺内存数值。

原生材料在虚拟机/远程桌面下的表现单独记录，不能替代本地真机结论。失败用例进入 Bug Issue，附版本、复现、预期/实际和日志截图。
