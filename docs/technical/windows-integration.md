# Windows 系统集成

状态：[Issue #14](https://github.com/xiaodaozhi/xdstarter/issues/14) 已接入桌面快捷动作、原生托盘与只读开始按钮定位；[#15](https://github.com/xiaodaozhi/xdstarter/issues/15) 接入无边框菜单与外部焦点收起；[#16](https://github.com/xiaodaozhi/xdstarter/issues/16) 修复横向锚点交替和显示后定位跳变，并改用完全自绘外观。本轮 [#22](https://github.com/xiaodaozhi/xdstarter/issues/22) 接入可显式启用、暂停的单独 Win 键原型，机制与恢复取舍见 [ADR-0008](adr/0008-win-key-capture.md)。构建和真机结果按版本写入[测试策略](testing.md)，实现状态不扩大支持矩阵。开始按钮接管、背景材料及其他系统组合继续由父 [#6](https://github.com/xiaodaozhi/xdstarter/issues/6) 跟踪。对应 PRD-001、PRD-010、PRD-011。

## 实现边界

独立普通用户桌面程序，保留 Explorer，不向其注入代码，不替换系统文件。使用 `QGuiApplication` 和 Qt Quick，托盘已直接封装 `Shell_NotifyIcon`，没有引入 Widgets。应用目录和桌面快捷动作分别通过可替换平台接口执行。

当前使用 `Qt.Window | Qt.FramelessWindowHint` 的 Qt Quick 菜单窗口，透明外窗容纳自绘阴影与圆角，正文面板仍为主题实色，外部焦点收起由 host 管理，不使用 `Qt.Popup` 的原生自动关闭与输入抓取。每次启动默认没有键盘/鼠标 Hook；只有用户显式启用单独 Win 键唤起后才安装。系统开始按钮保持原生行为，普通托盘显示和快捷栏动作不启用接管。

## 本轮常驻、对齐与开始按钮核查（Issue #41）

正常首次启动隐藏菜单，仅注册托盘；失败才显示恢复窗口。同用户／会话单例通过 Qt 本机 IPC 通知原 host 唤起，二次启动不并行读写布局。当前原生开始按钮没有点击接管实现；本机 Windows 10.0.26300 的正常权限只读探针确认 UIA 元素没有独立 HWND，不能将可见矩形作为吞点击依据。根因、原始／沙箱证据区分和后续边界见 [ADR-0013](adr/0013-resident-editable-menu.md)。

`queryMenuAnchor` 在原有无 UIA 路径中增加实际任务栏矩形和只读对齐偏好，纯函数推导四边停靠、水平居中和相应 resize 边缘；保留混合 DPI 隐藏迁移门槛。底部左对齐仍左／底 8 DIP；顶部改为左／顶，左侧改为邻接工作区左上，右侧镜像，水平居中改为工作区中心。未知／缺少偏好回退左对齐。QML 使用公开 Qt 系统 resize，领域模型不依赖 Win32，系统显示设置未被修改。本轮策略自动测试不替代各任务栏布局的物理拖动验收。

## 唤起与焦点

当前入口是产品窗口、原生托盘，以及用户主动启用的单独 Win 键。`DesktopWindowHost` 统一处理显示/隐藏、最小化恢复、退出、原生装饰抑制和定位：托盘可用时设置 `quitOnLastWindowClosed(false)`，窗口关闭或收起后保持常驻；托盘不可用时系统关闭窗口仍按默认策略退出，但主动收起和外部焦点收起采用最小化，以免进程隐藏后不可恢复。托盘文字菜单提供“显示/隐藏 XDStarter”、Win 键唤起启用/暂停/恢复和“退出 XDStarter”，并显示实际状态与开始按钮未接管说明。

每次用户唤起窗口都重新读取指针所在显示器名称、工作区与任务栏 DPI。`DesktopShellService` 专用 worker 调用 Windows 的 `queryMenuAnchor()`，不初始化 COM、不调用 UIA，也不等待开始按钮提供方；重复请求合并。产品入口加载 QML 时初始 `visible = false`，收到当前有效结果后先配置原生装饰抑制和定位，再显示并请求激活；smoke 与独立 GUI 夹具仍自行显示。关闭、主动收起或取消待显示请求会使旧查询失效，迟到结果不能重新打开窗口，也不对已显示窗口作可见的异步位置修正。启动应用或桌面动作成功后收起窗口；托盘不可用时改为最小化，保留任务栏恢复入口；失败保留窗口并说明原因。

host 监听 Qt 的窗口焦点、窗口激活与应用状态，在持续外部失焦 100 ms 后复核。根窗口及沿 `transientParent` 可追溯到根的内部窗口均属于本菜单；只有本轮显示实际获得过内部焦点后才允许失焦收起，初次激活被系统拒绝时不隐藏。内部尺寸、固定、电源和关于菜单固定为 `Popup.Item`，没有额外的原生顶层焦点交接；托盘原生文字菜单期间暂停复核，返回后再判断。

[#10](https://github.com/xiaodaozhi/xdstarter/issues/10) 按用户要求优先使用 Windows 系统原生打开文件对话框，公开父窗口指向菜单并使用窗口模态。2026-10-22 的首轮候选 `b238d506` 在该交接中被 host 误判为外部焦点而收起，不能把 Qt transient 链检查视为原生模态支持。`1709a7a` 在 Windows 平台模块增加 `hasOwnedForegroundWindow`：菜单须为本进程、当前 GUI 线程中可见的顶层窗口，原生前台窗口及至多 16 层 `GW_OWNER` 链须始终属于本进程并到达菜单，返回成功前再复核前台未变；允许原生对话框位于另一线程。同进程但没有精确 owner 关系的窗口、失效句柄及真正外部应用不豁免收起。host 只有在原生 owned 阶段每 100 ms 复核，以发现没有新 Qt 事件的外切；Qt 焦点返回、隐藏与退出遵循现有清理，不扩展永久轮询。补修已独立审查、正式构建通过，但第二轮固定产品 `d632d626…` 在真实托盘恢复后点击导入入口仍发生 `external-focus` 收起，未形成可操作的模态选择器，未选文件且主布局未变。后续有界诊断观察到，原生线程的前台窗口初始化时尚不可见，查询因此在 owner 链验证之前返回 false；`74a2c4d` 只去掉前台可见性前置条件，菜单可见性与全部精确归属检查保持，临时诊断已撤去。最终固定产品通过本机原生选择／取消、导入与备份往返、明确启动及独立重启；独立重启后，用户亲自点击外部应用，确认菜单和系统选择器均收起，与 `external-focus` 日志一致，随后真实托盘退出并取得第二次退出码 0。旧失败和诊断不改标为最终通过，也不扩大支持矩阵，见 [ADR-0012](adr/0012-layout-import-transaction.md) 和[测试策略](testing.md)。

[#28](https://github.com/xiaodaozhi/xdstarter/issues/28) 补充隐藏窗口跨屏准备：`setScreen()` 后还须在隐藏阶段实际迁移窗口，等待 Qt 窗口与目标屏幕 DPR、逻辑尺寸和本进程原生物理尺寸连续一致，再最终定位显示。准备最长 1500 ms，旧请求、取消、关闭、退出及同步重入不会使旧窗口重新显示；超时保留托盘重试或最小化入口。不能以旧 DPR 定位后再在可见阶段修正，也不使用任务栏 DPI 替代 Qt 渲染缩放。机制与独立测量边界见 [ADR-0007](adr/0007-custom-menu-chrome.md#隐藏窗口跨-dpi-屏幕的准备阶段)。

拖拽且左键仍按下时暂缓外部失焦收起，仅在该待处理状态下短时复查，松手或取消后正常判断。编辑模式不永久阻止外部收起，内部弹层打开也不妨碍真正切换到外部应用后关闭。收起前调用 QML 清理契约，关闭内部弹层、取消未提交预览、暂停迟到的焦点恢复；重新显示时恢复可操作焦点。已提交布局与当前编辑模式保留，定位和焦点计时器不会让旧一轮状态重新显示窗口。

Win 键状态跟踪与开始按钮识别独立于 QML。`WinKeyCapture` 平台模块报告 toggle 和实际启用/失败状态，由主线程统一处理窗口与焦点；输入回调不承载上述窗口状态逻辑。

### 单独 Win 键原型与恢复

每次启动接管关闭，启用状态不持久化。用户从窗口的“电源与退出”或托盘文字菜单启用；host 要求支持环境且托盘可用，托盘失效时停止接管，重新注册不自动恢复。暂停、失败和退出使旧会话的排队 toggle 无效并释放 Hook；退出还等待消息线程结束，新进程仍从关闭状态开始。

Windows 在专用消息线程安装低级键盘 Hook，以及仅维护按钮按住状态、取消 Win+鼠标独按候选的鼠标观察 Hook。鼠标事件全部放行，不采集坐标；已按住鼠标再按 Win 也不建立独按候选。键盘状态机放行真实左右 Win-down 和组合事件，只在没有其他按键或鼠标按钮/滚轮参与的首次 Win-up 识别独按；重复和两侧 Win 重叠不产生额外 toggle。当前事件不能用回调内 `GetAsyncKeyState` 判断，因为异步键态尚未更新。

独按释放在回调内执行固定三条 `SendInput`：`0xFF down`、`0xFF up`、相应 Win 的 extended key-up；成功插入全部事件才消费真实 Win-up，并投递到线程消息后排队交给 Qt 主线程。自有标记避免重入，外部合成输入取消独按。派发不完整时放行物理 up、尽力补 dummy up，并停用报告错误；toggle 通知失败时本次唤起可能丢失，但不重新合成 Win-down。此有界平台输入派发之外，回调不执行窗口操作、COM、磁盘、QML 或 GUI 等待。没有先打开再关闭原生 Start 的路径，也不重放 Win+E/R/L/数字等组合。

当前启用 gate 限于本地 Windows 11；输入桌面启用前及每 500 ms 检查，不可访问或不再是本线程的 `Default` 桌面时停止，需要用户手动重新启用。检查存在时间间隔，不能据此宣称覆盖所有短暂安全桌面切换。`0xFF` 参考 PowerToys 的未文档化技巧，`SendInput` 受 UIPI 限制；Hook 超时可能被系统静默移除，没有可靠直接查询接口，消息线程仍活着不代表 Hook 仍在。已知安装、消息循环、派发和桌面检查错误会停用，不宣称完全检测 Hook 丢失。

左右 Win、Win+E/R/L/数字、按住/重复、其他程序快捷键、暂停、退出和失败恢复必须分别真机记录；模拟平台、成功安装 Hook 或状态机测试不构成系统输入验收通过。开始按钮左键接管、右键系统菜单继续单独测试，当前只读定位与单独 Win 原型不完成父 Issue #6。方法依据、限制与验证层次见 [ADR-0008](adr/0008-win-key-capture.md)。

## 开始按钮与兼容策略

产品的 `queryMenuAnchor()` 只读取目标显示器/工作区与同屏真实任务栏 DPI，任务栏通过可见的 `Shell_TrayWnd` / `Shell_SecondaryTrayWnd` 及 Explorer 进程校验识别。可见面板左侧与底部统一留 8 DIP，不以开始按钮位置或兼容保留的 `trusted` 字段决定几何。工作区不可用时明确报告定位不可用，保留可达的窗口位置。服务公开名称 `requestStartButtonAnchor()`、`startButtonAnchorReady` 暂保留兼容，Windows 产品路径实际调用的是快速工作区查询。

原 `queryStartButtonAnchor()` 保留为显式只读研究入口：在真实任务栏内通过 UI Automation 查询唯一的 `AutomationId = StartButton` 且 `ControlType = Button`，再检查提供方进程、可见性、尺寸、包含关系与物理边界；标识缺失、多候选、隐藏或越界均返回不可信结果，不按本地化 Name 或固定坐标猜测。该研究查询独立初始化与释放 COM，可用时通过 `IUIAutomation2` 设置 1500 ms 连接/事务超时；对象不跨线程共享。产品唤起不执行此查询，避免隐藏阶段预定位引入提供方超时。所有原生矩形均在物理像素上下文读取并先作范围校验。

显式 `xdstarter_desktop_platform_tests.exe --probe-menu-anchor` 输出工作区查询 JSON；`--probe-anchor` 输出 UIA 开始按钮研究 JSON。二者均需单独记录环境，常规 CTest 不执行桌面 probe，且成功输出不等于系统输入接管或完整 Windows 兼容通过。

### 开始按钮原生命中的只读研究

[#24](https://github.com/xiaodaozhi/xdstarter/issues/24) 在可替换平台接口增加只读命中诊断，由 `xdstarter_desktop_platform_tests.exe --probe-start-hit` 显式调用。它复用唯一 StartButton、Button、Explorer 及物理边界校验，进一步读取 UIA 元素的原生 HWND、所属任务栏、进程、可见性、窗口类与几何，并在按钮中心及内缩四角采样实时 `WindowFromPoint` 身份。结果区分没有 HWND、共享宿主、几何不符、遮挡/命中不一致和独立命中候选；真实结果、版本与 hash 由[测试策略](testing.md)记录，此处不预判本机结果。

此 probe 不装 Hook，不调用控件动作，不消费或合成输入，不移动窗口或修改任务栏/系统配置，不接入产品 UI，也不建立持续缓存或订阅。普通启动、常规 CTest 及产品菜单快速工作区定位不运行该研究查询。分步读取与五点采样不是原子事务，单次独立候选不等于可持续点击接管；原生开始按钮行为不因该研究改变。

微软不保证 `AutomationId` 跨版本稳定，`NativeWindowHandle` 也可为 0；UIA 矩形与未离屏状态不保证没有遮挡。UIA 属性变化事件并不完备，`WindowFromPoint` 只能提供该次点位的原生窗口身份。因此共享宿主、宿主 HWND 加旧按钮矩形、仅 TTL 或仅事件订阅都不能推定命中安全。依据与保守取舍见 [ADR-0009](adr/0009-start-button-hit-probe.md)。[UIA 属性标识](https://learn.microsoft.com/en-us/windows/win32/winauto/uiauto-automation-element-propids)、[UIA 属性与事件](https://learn.microsoft.com/en-us/windows/win32/winauto/uiauto-propertiesoverview)、[WindowFromPoint](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-windowfrompoint)

实际接管需另行设计回调内有界身份复核和缓存失效策略，覆盖 Explorer/任务栏重建、显示/DPI/位置/可见性变化与查询失败；缺失、过期、歧义或不支持时，本次完整点击保留原生路径。还须实测原生 down/up 行为，并证明长按、双击、拖出、暂停、失败及退出期间的完整按下/释放配对，不能以先吞 down、失效后放行 up 或先开再关原生 Start 代替恢复。真实物理左键、右键系统菜单、Win+X、其他任务栏项目、与 Win 键接管并用及异常后的原生入口分别验收，未验证的系统组合继续留在父 [#6](https://github.com/xiaodaozhi/xdstarter/issues/6)。

任务栏居中/左对齐、自动隐藏、副屏、混合 DPI 和语言差异必须按实际环境验证。本轮 #28 已完成本机 96/144 DPI 双屏原生受控往返测量；真实托盘用户协助记录与独立进程恢复结果见[测试策略](testing.md)。自动隐藏、Explorer 重启、物理系统缩放切换、拔插及完整混合 DPI 组合仍未完成验证。将来启用接管前仍须探测支持条件，不以关闭原生菜单后重开自定义窗口的闪烁方案作为验收通过。

对不支持的系统组合保留原生行为；界面区分 Win 键未启用、实际启用、暂停和失败，并明确开始按钮未接管。安全桌面、锁屏、UAC 等系统隔离场景不作为接管目标，不以普通桌面 Hook 的结果扩大支持范围。

## 窗口与材料

平台只对当前进程、当前 GUI 线程的顶层窗口作短时原生定位和装饰抑制。定位读取透明外窗实际物理尺寸，减去两侧阴影留白后得到可见面板尺寸；不再读取 `DWMWA_EXTENDED_FRAME_BOUNDS` 参与布局。间距与留白统一按选定 Qt 窗口的实际渲染 DPR 从 DIP 转换成物理像素，任务栏 DPI 仅用于诊断，可见面板距工作区左侧和底部均为 8 DIP，并在空间不足时夹限工作区；阴影外窗可以越出工作区，不把阴影算入面板间距。`DesktopPlacementResult.physicalRect` 返回可见面板矩形，不能当作外窗边界。极值运算先校验范围，GUI 边界执行 `SetWindowPos`；Shell 与产品工作区查询在 worker 中，UIA 仅用于显式研究 probe。

QML 去除系统标题栏并设置 `Qt.NoDropShadowWindowHint`，透明顶层窗口每侧留 24 DIP；内容面板默认仍为 980×680 DIP、最小 480×480 DIP。Canvas 使用同一圆角路径绘制主题实色表面、1 DIP 单层边框和阴影，并沿该路径裁切侧栏背景，正文保留面板内边距。`visual.json` 的 `menuChrome` 统一提供阴影留白、屏幕间距与边框宽度；磁贴仍使用原有 48/8 DIP 网格。透明角与阴影区域可请求收起，不作为正文按钮命中区。

`DesktopPlatform.configureMenuWindow` 允许 Qt alpha 表面，设置 `DWMWA_WINDOW_CORNER_PREFERENCE = DWMWCP_DONOTROUND`、禁用非客户区渲染、清空扩展 frame，并请求 `DWMWA_COLOR_NONE` 原生边框颜色；不添加系统标题栏、可调整边框、region 或原生 resize 命中区。`DesktopAppearanceResult.nativeDecorationsDisabled` 报告抑制请求结果，失败返回结构化原因。自绘边框、圆角与阴影只由 Qt Quick 产生，实际是否只有一层外观仍须用当前程序的真机截图和操作验收，API 返回不能替代视觉证据。

背景 Mica/Acrylic、模糊材料仍待后续研究；显示／收起动画见 ADR-0013；外窗为阴影透明不等于启用背景材料。[ADR-0007](adr/0007-custom-menu-chrome.md) 替代 [ADR-0006](adr/0006-menu-popup.md) 的原生外观与旧位置选择，保留原焦点、拖拽和恢复规则。

位置以目标显示器工作区和任务栏为依据，每次唤起重新查询，不持久化屏幕像素坐标。跨屏定位仍需覆盖 Qt 的 DPI 尺寸变化、任务栏自动隐藏及分辨率变化，单屏结果不能替代混合 DPI 验证。

## 应用与恢复

已有应用目录读取当前用户及公共开始菜单，并补充打包应用。`ApplicationPlatform` 使用系统 API 处理快捷方式/AUMID；后台执行与受控启动边界见[架构](architecture.md)。管理员启动仍待专门交互，不使主进程长期提升权限。

快捷栏动作由 `DesktopShellService` 的另一专用线程串行处理。服务与 Windows 平台层均只接受 `documents`、`downloads`、`settings`、`account`：文档与下载使用系统 Known Folders，设置与账户仅使用固定 URI `ms-settings:`、`ms-settings:yourinfo`。界面不能提交任意路径、URI 或 Shell 命令。失败返回结构化原因，重复点击不排队执行多个动作。头像使用圆形占位素材；电源文字菜单提供默认关闭的 Win 键唤起启用/暂停，以及退出 XDStarter，系统关机、重启与注销未启用。

托盘使用隐藏的本进程顶层宿主接收 Shell 通知。原生 WndProc 只排队事件，文字菜单与应用状态变化由 Qt 事件循环处理；停止时移除图标、结束原生菜单并失效会话代次，旧通知不影响新的托盘会话。退出清理资源，软件图标从自带 QImage 创建，不读取机器本地图标路径。

`TaskbarCreated` 后重新注册托盘图标的代码已接入；注册失败会通知 host 恢复可达的窗口/任务栏入口，并停止 Win 键接管。实际重启 Explorer 的恢复测试尚未执行；注册恢复后也需用户重新启用接管，不自动恢复已失败或暂停的会话。开机启动项管理和显式打开系统开始菜单的独立动作仍待后续开发；暂停或退出接管后，系统开始按钮与原生 Win 键路径继续可用，实际恢复须按当前版本验收。

## 必须产出的验证记录

原型 Issue 与[测试策略](testing.md)记录 Windows 版本/构建、架构、任务栏设置、DPI、Qt 版本、渲染后端、程序版本、通过/失败用例与恢复步骤。当前弹层需单独验证无标题栏、单层圆角/边框/阴影、左侧与底部可见面板间距相同、可信/失败重复唤起位置一致、点击外部与应用切换收起、内部菜单/Esc、拖拽越界释放、编辑状态保留、托盘恢复及新进程布局恢复。返回的面板定位矩形、透明外窗实际边界、外观 API 返回与实际截图分别记录；若托盘点击需要用户协助，也要注明。真实快捷动作需要有意执行，常规自动测试仅注入假平台，不自动打开用户文件夹或系统设置。

只有记录实际通过的组合进入支持矩阵。只读 UIA probe、模拟平台、强制缩放 GUI 回归、普通窗口历史结果或一次本机 DWM 请求，均不能单独证明完整弹层外观、系统接管、混合 DPI、自动隐藏任务栏或 Explorer 重启兼容。高对比度、远程会话和系统效果关闭等环境需另行验证。

参考：[低级键盘 Hook](https://learn.microsoft.com/en-us/windows/win32/winmsg/lowlevelkeyboardproc)、[SendInput](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput)、[PowerToys 键盘事件机制](https://github.com/microsoft/PowerToys/blob/main/doc/devdocs/modules/keyboardmanager/keyboardmanager.md)、[DWM 窗口属性](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/nf-dwmapi-dwmsetwindowattribute)。
