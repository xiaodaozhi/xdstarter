<!-- docs/technical/research/menu-window-followup-48.md -->

# 菜单窗口行为与边界约束研究

日期：2026-10-23。对应 [Issue #48](https://github.com/xiaodaozhi/xdstarter/issues/48) 第 1、2、3、7、16 项。

源码核查基线为本轮修复前的`a9abf252a6668c431928b2bbad5cafd1801bef55`；下文“现状”描述该版本的问题，新实现与验证结果另见测试策略和Issue。

本文记录源码核查、平台依据和待实现方案，不代表修复已完成，也不代表新版本已通过测试。高保真交互设计先由用户验收，再落实产品代码；实现进展与实际验证结果由 Issue / PR 跟踪。此前 [Issue #45 的验收证据](../validation/issue-45/desktop-acceptance.json) 保持原有程序版本与结论，不能作为本轮修复的通过记录。

## 已核实的现状

### 外部失焦与主动收起

[`DesktopWindowHost::checkFocus()`](../../../src/app/desktop_window_host.cpp) 在持续外部失焦后调用 `dismiss("external-focus")`。开始按钮切换和右上角收起按钮也进入同一 `dismiss()`，没有发现外部失焦独有的“只隐藏 QML 内容、不隐藏窗口”分支。

收起时先清理内部弹层和拖拽状态，再播放最多 300 ms 的退出动画。计时器完成后，有托盘恢复入口就调用 `QWindow::hide()`；托盘不可用则调用 `showMinimized()`。后者是 [ADR-0006](../adr/0006-menu-popup.md) 保留普通任务栏恢复入口的旧设计。程序目前的退出动画为 120 ms，期间可见内容逐渐消失，但原生窗口尚未隐藏。

因此，用户报告的长期残留不能直接归因为两种入口采用了不同收起函数。可能涉及窗口实际仍最小化、托盘恢复状态变化、动画收尾状态，或 Shell 的窗口按钮更新；这些分支尚未取得本轮运行时复现证据。验证必须同时记录收起原因、托盘恢复状态、Qt visibility、`IsWindowVisible()`、`IsIconic()` 和动画完成时点，避免把透明内容消失当作 HWND 已隐藏。

### 普通窗口按钮与顶层顺序

[`Main.qml`](../../../qml/Main.qml) 当前声明 `Qt.Window | Qt.FramelessWindowHint | Qt.NoDropShadowWindowHint`。[`WindowsDesktopPlatform::configureMenuWindow()`](../../../src/platform/windows/windows_desktop_platform.cpp) 处理 DWM 装饰和菜单自有 HWND 的缩放子类，但没有设置 `WS_EX_TOOLWINDOW`，也没有为菜单建立隐藏 owner。

这是菜单显示时出现普通任务栏窗口按钮的明确实现原因。Microsoft 说明，无 owner 的顶层窗口通常获得任务栏按钮；`WS_EX_TOOLWINDOW` 可排除该按钮。如果需要改变已经可见窗口的任务栏样式，应先隐藏窗口，再变更样式并重新显示，不能直接在显示期间修改并假定旧按钮会消失。

当前 QML 没有 `Qt.WindowStaysOnTopHint`，原生呈现使用 `SetWindowPos(..., HWND_TOP, ...)`；`raise()` 和 `HWND_TOP` 只调整当时的顺序，没有建立持续 topmost 属性。这解释了当前实现为什么不能满足菜单保持在普通应用窗口上方的要求，但本轮尚未复现某个具体窗口将菜单遮挡的全过程。

### 呈现动画有多个触发入口

`DesktopWindowHost::finishRevealPreparation()` 完成隐藏阶段定位后，依次执行 `showNormal()`、raise、激活与原生显示复核，最后才调用 QML 的 `prepareForPresentation()`。该方法调用 `revealMotion.restart()`，动画从 0 开始。

与此同时，`Main.qml` 的 `onVisibleChanged` 在变为可见时通过 `Qt.callLater()` 再次调用 `prepareForPresentation()`，`Component.onCompleted` 也调用同一方法。隐藏期间没有把 `revealProgress` 固定为 0。因而源码中存在以下确定的时序缺陷：

1. 窗口显示时可能沿用已经为 1 的进度，先提交完整面板。
2. host 随后将动画从 0 重启，面板瞬间变透明后进入动画。
3. 排队的可见性回调还可能再次重启动画；该回调仅检查 `visible`，没有本轮显示代次校验。

这与用户描述的完整显示、瞬间消失、再动画出现相符，仍需修复后的帧级回归验证。已显示窗口的重复唤起也调用同一准备方法，既会重播动画，也会关闭内部弹层；文件对话框位于前台时，不应通过重复准备主菜单把它关闭或覆盖。

### 工作区最大尺寸没有统一约束

[`desktopPopupPhysicalRect()`](../../../src/platform/desktop_platform.cpp) 已使用扣除展开任务栏重叠的 `desktopMenuWorkArea()`，但最大面板宽高只夹到完整工作区尺寸，没有扣除左右、上下各一份指定边距。后续位置夹限可能把应有的边距压为 0。

同一文件中的 `desktopResizePhysicalRect()` 只约束最小尺寸和非拖动边，没有最大工作区矩形。Windows 缩放子类在 `WM_SIZING` 中调用它，但未加入四边工作区上限。QML 声明了最小尺寸，尚未声明随目标工作区变化的最大尺寸。隐藏跨屏准备又会恢复用户原有的逻辑尺寸，因此旧尺寸大于新工作区时也需要统一夹限。

## 待实现的窗口策略

### 正常托盘常驻状态

菜单应在首次创建原生表面前就采用没有任务栏按钮的窗口类型。候选为 `Qt.Tool | Qt.FramelessWindowHint | Qt.NoDropShadowWindowHint | Qt.WindowStaysOnTopHint`。Qt 6.8.2 的 Windows 平台实现会把 Tool 类型转换成 `WS_EX_TOOLWINDOW`；实际 HWND 应检查 TOOLWINDOW 已设置、APPWINDOW 未设置、TOPMOST 已设置。若 Qt 或原生路径需要补充样式，也只能配置本进程、当前 GUI 线程的菜单根 HWND，并在隐藏阶段执行。

不采用 `Qt.Popup` 的全局输入抓取，也不采用 `WindowDoesNotAcceptFocus` 或 `WS_EX_NOACTIVATE` 来隐藏任务栏按钮；这些标志会改变正常文本输入和焦点行为，不能代替窗口类型选择。

原生显式呈现需要与 Qt topmost 声明一致。只在真实唤起时请求前台激活，不通过永久定时器反复抢焦点或抬升窗口。自有文件、设置等原生对话框保留正确 owner，Qt 窗口保留 `transientParent`；Windows 会将 owned window 放在 owner 上方，并随 topmost owner 维持相应层级。显示中的菜单再次收到唤起请求时，应先确认是否已有自有前台对话框，保持该对话框的交互，而不是重新准备主菜单。

“始终置顶”的实现目标是 Windows 正常桌面上的 topmost 菜单窗口及其自有对话框顺序；不以抢占安全桌面、改动其他进程窗口或持续压过后来出现的系统 topmost 窗口作为验收手段。

### 统一收起和呈现契约

外部失焦、开始按钮切换、右上角收起和 Esc 的最终收起继续进入同一 host 状态转换。动画只是该转换的一段，不允许 QML 单独淡出后保留原生可见状态。重复失焦事件不能无限延后退出计时器；动画期间重新唤起必须使旧的隐藏回调失效。启动成功后的收起入口也应遵循同一可观察契约。

呈现准备拆为三个职责，具体方法名在实现中确定：

- 隐藏阶段清理旧弹层、取消旧动画、将进度置为 0，并标记新的显示代次。
- 原生位置和 DPI 稳定后显示窗口，本轮只启动一次进入动画，再恢复可操作焦点。
- 已显示窗口的重复唤起只处理必要的激活与焦点，不重播动画，也不无条件关闭自有对话框。

实际产品由 host 管理上述时序。没有 host 的 smoke / GUI fixture 可自行呈现，但必须通过显式契约区分，不能让同一窗口同时走两套启动入口。所有延后回调核对显示代次；隐藏、取消、退出和下一轮显示使旧代次失效。

### 托盘失效时的恢复取舍

旧设计依靠最小化普通窗口保留任务栏入口。改为 Tool 窗口后直接沿用此做法，可能产生后台存活但没有可见入口的状态；这不满足可恢复性。

本轮用户要求菜单始终不出现普通任务栏按钮，因此优先方案是：正常菜单维持同一种 Tool 类型；托盘首次注册失败或运行中丢失时，停止系统输入接管并展示明确的恢复状态，提供重试托盘和退出入口。恢复入口未建立时，不把最后一个可操作窗口静默隐藏；关闭恢复窗口应明确结束程序并执行正常保存。已有单例 IPC 再次启动唤起继续保留，但不能作为用户唯一可发现的恢复方式。该恢复界面的文案与关闭语义应随高保真设计一并确认。

另一种方案是托盘失效时临时切回普通窗口，以保留任务栏恢复。这会产生窗口类型切换、HWND 生命周期和重注册风险，而且与本轮“始终不显示窗口按钮”的要求冲突，不能作为默认隐式例外。若未来另设明确的独立应急窗口，应单独定义范围并更新 ADR，不能宣称普通菜单已经无按钮而悄悄保留旧最小化行为。

## 待实现的几何约束

可见面板的允许矩形统一从 `desktopMenuWorkArea(anchor)` 向四边扣除 `screenGapDip` 换算后的物理边距获得。面板宽高、位置和拖动结果都以这个矩形为上限；阴影仍按 [ADR-0007](../adr/0007-custom-menu-chrome.md) 计入透明外窗，透明留白可以超出面板允许矩形，不能拿整个外窗边缘代替可见面板边缘。

显示准备阶段夹限原有尺寸，并让隐藏窗口的 Qt 尺寸和原生尺寸重新通过稳定检查后再显示。原生 `WM_SIZING` 同时保持非拖动边、限制拖动边不越过允许矩形，并遵循有效最小尺寸。Qt maximumSize 与原生约束使用同一来源，避免一个接受、另一个随后修正而出现松手跳位。

小屏幕、远程桌面尺寸骤减或高缩放下，产品期望最小尺寸可能大于可用空间。此时有效最小值必须不大于有效最大值，内部内容通过现有滚动与响应布局保持可达，不能为了维持最小尺寸越过任务栏或四边间距。工作区、DPI 和任务栏方向变化需要重新计算约束，不把旧显示器的最大值用于新显示器。

宽度吸附由 Issue #48 的磁贴区布局研究确定；吸附结果仍必须落在同一几何上限内。不能先允许超界吸附，再依靠显示后的二次移动修正。

## 预期验证

本轮以适用的单元、QML 集成和自有 HWND 夹具为主。检查通过只能证明各自覆盖的行为，实际 Shell 按钮和用户桌面行为若另行验收，必须绑定本轮产品版本并单独记录。

| 层级 | 预期检查 |
| --- | --- |
| Host 状态转换 | 外部失焦、开始按钮、右上角收起在有退出动画时最终均隐藏；重复失焦不延长隐藏；动画中唤回不被旧计时器隐藏；隐藏后不最小化复活 |
| Host 恢复边界 | 托盘启动失败、运行中丢失、重试成功、退出、单例再次唤起；最后恢复入口不可用时没有不可达后台状态；旧输入接管会话不再派发 |
| QML 与 host 呈现 | 可见前进度为 0；本轮只启动一次；已显示时再唤起不重播；快速收起重开不受旧 `callLater` 影响；自有对话框不被重复准备关闭 |
| 自有原生 HWND | 显示前及多次显示隐藏后的 TOOLWINDOW / APPWINDOW / TOPMOST 状态；受控 owner 对话框位于菜单上方；其他进程、线程或子窗口不能被菜单配置接口修改 |
| 纯几何 | 四边任务栏和两种对齐、五档 DPR、负坐标显示器、超大请求、单像素边界、工作区骤减、自动隐藏任务栏展开、整数溢出；结果四边都满足指定间距 |
| 原生缩放夹具 | 全部边与角、最小及最大尺寸、松手归一化、准备阶段与退出阶段消息、无关窗口和卸载子类后的消息；非拖动边稳定且面板不越界 |

实际动画可通过产品 QML 的进度、可见性转换和渲染帧信号验证，不能仅断言某个动画对象存在。仅检查窗口扩展样式也不能宣称实际任务栏按钮已经验收；若补充 Shell 观察，应保持只读，不通过改动 Explorer、合成系统开始按钮输入或重启用户桌面来扩大测试。

## 官方依据

- [Microsoft：The Taskbar，Managing Taskbar Buttons](https://learn.microsoft.com/en-us/windows/win32/shell/taskbar)：任务栏按钮、TOOLWINDOW 与改变可见窗口样式的隐藏顺序。
- [Microsoft：Window Features，Owned Windows 与 Z-Order](https://learn.microsoft.com/en-us/windows/win32/winmsg/window-features)：owner 关系、owned window 位于 owner 上方和 topmost 层级。
- [Microsoft：SetWindowPos](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowpos)：HWND_TOPMOST、前台激活和 owner / owned 的 topmost 传播。
- [Qt 6.8.2 Windows 窗口实现](https://github.com/qt/qtbase/blob/v6.8.2/src/plugins/platforms/windows/qwindowswindow.cpp)：Tool 类型到 WS_EX_TOOLWINDOW 的转换，以及项目固定 Qt 版本的窗口行为依据。
- [Microsoft：WM_SIZING](https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-sizing)：调整中的可修改矩形与边缘标识。
- [Microsoft：WM_WINDOWPOSCHANGING](https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-windowposchanging)：原生缩放归一化阶段的矩形约束。现有菜单自有 HWND 子类机制继续沿用 [ADR-0014](../adr/0014-start-click-and-adaptive-taskbar.md)，不扩展到 Explorer。
