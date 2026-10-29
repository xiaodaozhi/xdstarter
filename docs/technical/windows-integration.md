# Windows 系统集成

状态：[Issue #14](https://github.com/xiaodaozhi/xdstarter/issues/14) 已接入桌面快捷动作、原生托盘与只读开始按钮定位；[#15](https://github.com/xiaodaozhi/xdstarter/issues/15) 接入无边框菜单与外部焦点收起；[#16](https://github.com/xiaodaozhi/xdstarter/issues/16) 修复横向锚点交替和显示后定位跳变，并改用完全自绘外观。本轮 [#22](https://github.com/xiaodaozhi/xdstarter/issues/22) 接入可显式启用、暂停的单独 Win 键原型，机制与恢复取舍见 [ADR-0008](adr/0008-win-key-capture.md)。构建和真机结果按版本写入[测试策略](testing.md)，实现状态不扩大支持矩阵。开始按钮新增实现见 ADR-0014；背景材料及其他系统组合继续由父 [#6](https://github.com/xiaodaozhi/xdstarter/issues/6) 跟踪。对应 PRD-001、PRD-010、PRD-011。

## 实现边界

独立普通用户桌面程序，保留 Explorer，产品不向其注入代码，不替换系统文件。使用 `QGuiApplication` 和 Qt Quick，托盘已直接封装 `Shell_NotifyIcon`，没有引入 Widgets。应用目录和桌面快捷动作分别通过可替换平台接口执行。用户授权的独立Explorer路由研究探针默认不构建，只观察指定任务栏线程消息且全部放行，期限/身份/卸载边界见[ADR-0016](adr/0016-start-capture-experiments.md)，不代表产品已采用进程内接管。

当前 [Issue #48](https://github.com/xiaodaozhi/xdstarter/issues/48) 使用 `Qt.Tool | Qt.FramelessWindowHint | Qt.NoDropShadowWindowHint | Qt.WindowStaysOnTopHint` 的 Qt Quick 菜单窗口，透明外窗容纳自绘阴影与圆角，正文面板仍为主题实色。Windows 层在隐藏阶段配置 TOOLWINDOW、清除 APPWINDOW 并请求 topmost，目标是排除普通任务栏窗口按钮；实际原生状态与 Shell 行为以当前候选验证为准。外部焦点收起由 host 管理，不使用 `Qt.Popup` 的原生自动关闭与输入抓取。托盘注册成功后自动启用开始按钮点击服务的消息／查询线程和 Hook；它只消费已取得点资格的完整左键。Win 键 Hook 继续由用户显式启用，两个服务可独立暂停。快捷栏动作不启用接管。

隐藏阶段置顶以读取实际 WS_EX_TOPMOST 位为准；API 返回成功而位未生效时返回 `menu-topmost-state-denied`，不宣称配置成功。Microsoft 对窗口提升有前台权限条件，隐藏后台 STATIC fixture 出现过该诊断；产品 Qt 窗口是否置顶仍由实际候选单独验证。用户显式唤起恢复前台后再进行一次置顶请求和原生位复核，不循环抢焦点。无托盘紧急恢复窗口在未取得原生定位时，将整个透明外窗限制在工作区四边 8 DIP 内并临时禁用边框拖动；注册恢复后重新走普通隐藏定位，恢复正常可见面板间距、输入 region 及缩放边。

## 当前开始按钮与自适应任务栏（Issue #45）

实现见 [ADR-0014](adr/0014-start-click-and-adaptive-taskbar.md)。查询线程以实时 UIA 点身份和完整原生身份／显示器／DPI／代次获取短时资格，输入线程不执行 UIA。托盘丢失或输入桌面失效暂停接管；恢复后需手动重新开启。开始按钮右键、Win+X 与合成输入透传。普通菜单快速定位继续不调用 UIA。真实鼠标首次验收发现窗口显示异常，按失败记录并增加原生 presentation 复核；验证结果按当前程序记录，不能仅以信号已到达视为成功。

任务栏四边位置根据实际窗口几何识别，水平与垂直方向均支持居中；按停靠方向开放 resize 边缘。定位查询允许读取暂时隐藏的真实 Explorer 任务栏；几何缺失时，仅从单一预留工作区边推导方向，此回退不赋予输入资格。自动隐藏任务栏已展开时扣除重叠区域，原生失败的 Qt 回退使用同一策略。

Qt 系统 resize 保留原生输入循环；Windows 平台仅在本进程 GUI 线程的菜单 HWND 安装可移除的消息子类处理，在 sizing 命令、矩形调整和系统归一化阶段保持非拖动边固定，避免透明阴影被夹回工作区后面板跳位。其他窗口与非拖动期间消息照常传递。常规原生消息回归与用户在真实任务栏设置下的边框拖动分别记录，范围与失败历史见[测试策略](testing.md)及 ADR-0014。

## 历史：常驻、对齐与开始按钮核查（Issue #41）

正常首次启动隐藏菜单，仅注册托盘；失败才显示恢复窗口。同用户／会话单例通过 Qt 本机 IPC 通知原 host 唤起，二次启动不并行读写布局。当前原生开始按钮没有点击接管实现；本机 Windows 10.0.26300 的正常权限只读探针确认 UIA 元素没有独立 HWND，不能将可见矩形作为吞点击依据。根因、原始／沙箱证据区分和后续边界见 [ADR-0013](adr/0013-resident-editable-menu.md)。

`queryMenuAnchor` 在原有无 UIA 路径中增加实际任务栏矩形和只读对齐偏好，纯函数推导四边停靠、水平居中和相应 resize 边缘；保留混合 DPI 隐藏迁移门槛。底部左对齐仍左／底 8 DIP；顶部改为左／顶，左侧改为邻接工作区左上，右侧镜像，水平居中改为工作区中心。未知／缺少偏好回退左对齐。QML 使用公开 Qt 系统 resize，领域模型不依赖 Win32，系统显示设置未被修改。本轮策略自动测试不替代各任务栏布局的物理拖动验收。

## 唤起与焦点

当前入口是 Qt 工具菜单、原生托盘、取得实时点资格的开始按钮及显式启用的单独 Win 键。Tool／Topmost 菜单保留正常键盘输入，自有原生对话框通过 owner 关系保持在菜单之上。`DesktopWindowHost` 统一收起动画完成后的 `hide()`；重复收起不延长动画，重新唤起使旧隐藏失效。托盘失效暂停接管，显示持续可见的重试／退出恢复区；最小化的 Tool 窗口也恢复为正常可见窗口，外部失焦不隐藏最后入口，用户主动关闭则请求正常退出。此前无托盘时 `showMinimized()` 保留普通任务栏恢复入口的策略已被替换，历史程序证据保持不变。

每次隐藏菜单唤起均后台重读目标显示器、工作区与任务栏 DPI，在隐藏阶段迁移、夹限、设置位置并以 `stagePresentation()` 将进度归零后才 `showNormal()`，随后本轮启动一次动画。准备、显示与清理受同一代次约束；已显示时再次唤起只恢复焦点，真正自有前台对话框保持其焦点。`hasOwnedForegroundDialog()` 排除根窗口，避免菜单自己位于前台时跳过退出动画的恢复；失焦复核继续沿用包含根窗口的 `hasOwnedForegroundWindow()`。旧弹层仍在退出时，按本轮代次等待其关闭后恢复搜索焦点。旧查询及旧动画回调不能重新显示窗口。启动或桌面动作成功后收起；托盘失效则维持明确恢复入口，不最小化。失败原因使用必要错误反馈。

host 监听 Qt 的窗口焦点、窗口激活与应用状态，在持续外部失焦 100 ms 后复核。根窗口及沿 `transientParent` 可追溯到根的内部窗口均属于本菜单；只有本轮显示实际获得过内部焦点后才允许失焦收起，初次激活被系统拒绝时不隐藏。内部尺寸、固定、电源和关于菜单固定为 `Popup.Item`，没有额外的原生顶层焦点交接；托盘原生文字菜单期间暂停复核，返回后再判断。

[#10](https://github.com/xiaodaozhi/xdstarter/issues/10) 按用户要求优先使用 Windows 系统原生打开文件对话框，公开父窗口指向菜单并使用窗口模态。2026-10-22 的首轮候选 `b238d506` 在该交接中被 host 误判为外部焦点而收起，不能把 Qt transient 链检查视为原生模态支持。`1709a7a` 在 Windows 平台模块增加 `hasOwnedForegroundWindow`：菜单须为本进程、当前 GUI 线程中可见的顶层窗口，原生前台窗口及至多 16 层 `GW_OWNER` 链须始终属于本进程并到达菜单，返回成功前再复核前台未变；允许原生对话框位于另一线程。同进程但没有精确 owner 关系的窗口、失效句柄及真正外部应用不豁免收起。host 只有在原生 owned 阶段每 100 ms 复核，以发现没有新 Qt 事件的外切；Qt 焦点返回、隐藏与退出遵循现有清理，不扩展永久轮询。补修已独立审查、正式构建通过，但第二轮固定产品 `d632d626…` 在真实托盘恢复后点击导入入口仍发生 `external-focus` 收起，未形成可操作的模态选择器，未选文件且主布局未变。后续有界诊断观察到，原生线程的前台窗口初始化时尚不可见，查询因此在 owner 链验证之前返回 false；`74a2c4d` 只去掉前台可见性前置条件，菜单可见性与全部精确归属检查保持，临时诊断已撤去。最终固定产品通过本机原生选择／取消、导入与备份往返、明确启动及独立重启；独立重启后，用户亲自点击外部应用，确认菜单和系统选择器均收起，与 `external-focus` 日志一致，随后真实托盘退出并取得第二次退出码 0。旧失败和诊断不改标为最终通过，也不扩大支持矩阵，见 [ADR-0012](adr/0012-layout-import-transaction.md) 和[测试策略](testing.md)。

[#28](https://github.com/xiaodaozhi/xdstarter/issues/28) 补充隐藏窗口跨屏准备：`setScreen()` 后还须在隐藏阶段实际迁移窗口，等待 Qt 窗口与目标屏幕 DPR、逻辑尺寸和本进程原生物理尺寸连续一致，再最终定位显示。准备最长 1500 ms，旧请求、取消、关闭、退出及同步重入不会使旧窗口重新显示。超时有托盘时保留重试；当时无托盘时采用最小化恢复，#48 已替换为可见的重试／退出窗口。不能以旧 DPR 定位后再在可见阶段修正，也不使用任务栏 DPI 替代 Qt 渲染缩放。机制与独立测量边界见 [ADR-0007](adr/0007-custom-menu-chrome.md#隐藏窗口跨-dpi-屏幕的准备阶段)。

拖拽且左键仍按下时暂缓外部失焦收起，仅在该待处理状态下短时复查，松手或取消后正常判断。编辑模式不永久阻止外部收起，内部弹层打开也不妨碍真正切换到外部应用后关闭。收起前调用 QML 清理契约，关闭内部弹层、取消未提交预览、暂停迟到的焦点恢复；重新显示时恢复可操作焦点。已提交布局与当前编辑模式保留，定位和焦点计时器不会让旧一轮状态重新显示窗口。

Win 键状态跟踪与开始按钮识别独立于 QML。`WinKeyCapture` 平台模块报告 toggle 和实际启用/失败状态，由主线程统一处理窗口与焦点；输入回调不承载上述窗口状态逻辑。

### 单独 Win 键原型与恢复

每次启动接管关闭，启用状态不持久化。用户从窗口的“电源与退出”或托盘文字菜单启用；host 要求支持环境且托盘可用，托盘失效时停止接管，重新注册不自动恢复。暂停、失败和退出使旧会话的排队 toggle 无效并释放 Hook；退出还等待消息线程结束，新进程仍从关闭状态开始。

Windows 在专用消息线程安装低级键盘 Hook，以及仅维护按钮按住状态、取消 Win+鼠标独按候选的鼠标观察 Hook。鼠标事件全部放行，不采集坐标；已按住鼠标再按 Win 也不建立独按候选。键盘状态机放行真实左右 Win-down 和组合事件，只在没有其他按键或鼠标按钮/滚轮参与的首次 Win-up 识别独按；重复和两侧 Win 重叠不产生额外 toggle。当前事件不能用回调内 `GetAsyncKeyState` 判断，因为异步键态尚未更新。

独按释放在回调内执行固定三条 `SendInput`：`0xFF down`、`0xFF up`、相应 Win 的 extended key-up；成功插入全部事件才消费真实 Win-up，并投递到线程消息后排队交给 Qt 主线程。自有标记避免重入，外部合成输入取消独按。派发不完整时放行物理 up、尽力补 dummy up，并停用报告错误；toggle 通知失败时本次唤起可能丢失，但不重新合成 Win-down。此有界平台输入派发之外，回调不执行窗口操作、COM、磁盘、QML 或 GUI 等待。没有先打开再关闭原生 Start 的路径，也不重放 Win+E/R/L/数字等组合。

当前启用 gate 限于本地 Windows 11；输入桌面启用前及每 500 ms 检查，不可访问或不再是本线程的 `Default` 桌面时停止，需要用户手动重新启用。检查存在时间间隔，不能据此宣称覆盖所有短暂安全桌面切换。`0xFF` 参考 PowerToys 的未文档化技巧，`SendInput` 受 UIPI 限制；Hook 超时可能被系统静默移除，没有可靠直接查询接口，消息线程仍活着不代表 Hook 仍在。已知安装、消息循环、派发和桌面检查错误会停用，不宣称完全检测 Hook 丢失。

左右 Win、Win+E/R/L/数字、按住/重复、其他程序快捷键、暂停、退出和失败恢复必须分别真机记录；模拟平台、成功安装 Hook 或状态机测试不构成系统输入验收通过。开始按钮左键接管、右键系统菜单继续单独测试，当前只读定位与单独 Win 原型不完成父 Issue #6。方法依据、限制与验证层次见 [ADR-0008](adr/0008-win-key-capture.md)。

## 开始按钮与兼容策略

产品的 `queryMenuAnchor()` 只读取目标显示器／工作区与同屏真实任务栏 DPI，通过 `Shell_TrayWnd` / `Shell_SecondaryTrayWnd` 及 Explorer 进程校验识别任务栏；几何查询可读取暂时隐藏的真实任务栏，输入点资格仍要求可见。可见面板按任务栏方向与对齐停靠，四周均预留 8 DIP，不以开始按钮位置或兼容保留的 `trusted` 字段决定几何。工作区不可用时明确报告定位不可用，保留可达的窗口位置。服务公开名称 `requestStartButtonAnchor()`、`startButtonAnchorReady` 暂保留兼容，Windows 产品路径实际调用的是快速工作区查询。

原 `queryStartButtonAnchor()` 保留为显式只读研究入口：在真实任务栏内通过 UI Automation 查询唯一的 `AutomationId = StartButton` 且 `ControlType = Button`，再检查提供方进程、可见性、尺寸、包含关系与物理边界；标识缺失、多候选、隐藏或越界均返回不可信结果，不按本地化 Name 或固定坐标猜测。该研究查询独立初始化与释放 COM，可用时通过 `IUIAutomation2` 设置 1500 ms 连接/事务超时；对象不跨线程共享。产品唤起不执行此查询，避免隐藏阶段预定位引入提供方超时。所有原生矩形均在物理像素上下文读取并先作范围校验。

显式 `xdstarter_desktop_platform_tests.exe --probe-menu-anchor` 输出工作区查询 JSON；`--probe-anchor` 输出 UIA 开始按钮研究 JSON。二者均需单独记录环境，常规 CTest 不执行桌面 probe，且成功输出不等于系统输入接管或完整 Windows 兼容通过。

### 开始按钮原生命中的只读研究

[#24](https://github.com/xiaodaozhi/xdstarter/issues/24) 在可替换平台接口增加只读命中诊断，由 `xdstarter_desktop_platform_tests.exe --probe-start-hit` 显式调用。它复用唯一 StartButton、Button、Explorer 及物理边界校验，进一步读取 UIA 元素的原生 HWND、所属任务栏、进程、可见性、窗口类与几何，并在按钮中心及内缩四角采样实时 `WindowFromPoint` 身份。结果区分没有 HWND、共享宿主、几何不符、遮挡/命中不一致和独立命中候选；真实结果、版本与 hash 由[测试策略](testing.md)记录，此处不预判本机结果。

此 probe 不装 Hook，不调用控件动作，不消费或合成输入，不移动窗口或修改任务栏/系统配置，不接入产品 UI，也不建立持续缓存或订阅。普通启动、常规 CTest 及产品菜单快速工作区定位不运行该研究查询。分步读取与五点采样不是原子事务，单次独立候选不等于可持续点击接管；原生开始按钮行为不因该研究改变。

微软不保证 `AutomationId` 跨版本稳定，`NativeWindowHandle` 也可为 0；UIA 矩形与未离屏状态不保证没有遮挡。UIA 属性变化事件并不完备，`WindowFromPoint` 只能提供该次点位的原生窗口身份。因此共享宿主、宿主 HWND 加旧按钮矩形、仅 TTL 或仅事件订阅都不能推定命中安全。依据与保守取舍见 [ADR-0009](adr/0009-start-button-hit-probe.md)。[UIA 属性标识](https://learn.microsoft.com/en-us/windows/win32/winauto/uiauto-automation-element-propids)、[UIA 属性与事件](https://learn.microsoft.com/en-us/windows/win32/winauto/uiauto-propertiesoverview)、[WindowFromPoint](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-windowfrompoint)

实际接管需另行设计回调内有界身份复核和缓存失效策略，覆盖 Explorer/任务栏重建、显示/DPI/位置/可见性变化与查询失败；缺失、过期、歧义或不支持时，本次完整点击保留原生路径。还须实测原生 down/up 行为，并证明长按、双击、拖出、暂停、失败及退出期间的完整按下/释放配对，不能以先吞 down、失效后放行 up 或先开再关原生 Start 代替恢复。真实物理左键、右键系统菜单、Win+X、其他任务栏项目、与 Win 键接管并用及异常后的原生入口分别验收，未验证的系统组合继续留在父 [#6](https://github.com/xiaodaozhi/xdstarter/issues/6)。

任务栏居中/左对齐、自动隐藏、副屏、混合 DPI 和语言差异必须按实际环境验证。本轮 #28 已完成本机 96/144 DPI 双屏原生受控往返测量；真实托盘用户协助记录与独立进程恢复结果见[测试策略](testing.md)。自动隐藏、Explorer 重启、物理系统缩放切换、拔插及完整混合 DPI 组合仍未完成验证。将来启用接管前仍须探测支持条件，不以关闭原生菜单后重开自定义窗口的闪烁方案作为验收通过。

对不支持的系统组合保留原生行为；界面区分 Win 键未启用、实际启用、暂停和失败，并独立显示开始按钮服务状态。安全桌面、锁屏、UAC 等系统隔离场景不作为接管目标，不以普通桌面 Hook 的结果扩大支持范围。

## 窗口与材料

平台只对当前进程、当前 GUI 线程的顶层窗口作短时原生定位和装饰抑制。定位读取透明外窗实际物理尺寸，减去两侧阴影留白后得到可见面板尺寸；不再读取 `DWMWA_EXTENDED_FRAME_BOUNDS` 参与布局。间距与留白统一按选定 Qt 窗口的实际渲染 DPR 从 DIP 转换成物理像素，任务栏 DPI 仅用于诊断。`desktopMenuPanelBounds()` 从扣除任务栏的工作区四边各预留 8 DIP，显示准备夹限旧尺寸，原生 resize 与 Qt 最大尺寸共享该边界；有效最小尺寸不超过工作区上限，宽度吸附也不能越界。可见期间工作区缩小时，下一轮原生 sizing 再次读取工作区，较小边界优先。阴影外窗可以越出面板允许矩形，不把阴影算入面板间距。`DesktopPlacementResult.physicalRect` 返回可见面板矩形，不能当作外窗边界。极值运算先校验范围，GUI 边界执行 `SetWindowPos`；Shell 与产品工作区查询在 worker 中，UIA 仅用于显式研究 probe。

QML 去除系统标题栏并设置 `Qt.NoDropShadowWindowHint`，透明顶层窗口每侧留 24 DIP；内容面板默认仍为 980×680 DIP，期望最小 480×480 DIP，空间不足时以可用工作区降低有效最小值。Canvas 使用同一圆角路径绘制主题实色表面、1 DIP 单层边框和阴影，并沿该路径裁切侧栏背景，正文保留面板内边距。`visual.json` 的 `menuChrome` 统一提供阴影留白、屏幕间距与边框宽度；磁贴仍使用原有 48/8 DIP 网格。透明角与阴影区域可请求收起，不作为正文按钮命中区。

Tool／Topmost 与透明阴影的组合已有实际失败证据：正常桌面权限下，隔离候选的 9 个 Start 点位中有 6 个被阴影外窗命中，不能由可见面板的 8 DIP 间距推定 Start 上沿安全。当前源码只从菜单自身 window region 扣除实际任务栏交集，保留面板与其余方向阴影；位置、尺寸、显示和 Qt DPI 处理后重套，失败则取消本轮显示。任务栏几何缺失时，仅使用唯一预留条带回退。细节和失败身份见 [ADR-0014](adr/0014-start-click-and-adaptive-taskbar.md)。

只读 `--observe-start-overlap PID` 记录当前候选的窗口层级、样式、region 和 Start 中心／边缘的 Win32、UIA 点身份。它不移动鼠标、不调用控件动作，也不安装 Hook；诊断结果不赋予点击资格。修复后的实际命中必须重新采样，不以 region 存在或纯几何通过替代；当前版本的实际行为与剩余边界单独写入测试策略。

`DesktopPlatform.configureMenuWindow` 允许 Qt alpha 表面，设置 `DWMWA_WINDOW_CORNER_PREFERENCE = DWMWCP_DONOTROUND`、禁用非客户区渲染、清空扩展 frame，并请求 `DWMWA_COLOR_NONE` 原生边框颜色；不添加系统标题栏、可调整边框或原生 resize 命中区。任务栏排除 region 在最终定位后独立计算，不用于重画面板圆角。`DesktopAppearanceResult.nativeDecorationsDisabled` 报告抑制请求结果，失败返回结构化原因。自绘边框、圆角与阴影只由 Qt Quick 产生，实际是否只有一层外观仍须用当前程序的真机截图和操作验收，API 返回不能替代视觉证据。

背景 Mica/Acrylic、模糊材料仍待后续研究；显示／收起动画见 ADR-0013；外窗为阴影透明不等于启用背景材料。[ADR-0007](adr/0007-custom-menu-chrome.md) 替代 [ADR-0006](adr/0006-menu-popup.md) 的原生外观与旧位置选择，保留原焦点、拖拽和恢复规则。

位置以目标显示器工作区和任务栏为依据，每次唤起重新查询，不持久化屏幕像素坐标。跨屏定位仍需覆盖 Qt 的 DPI 尺寸变化、任务栏自动隐藏及分辨率变化，单屏结果不能替代混合 DPI 验证。

## 应用与恢复

已有应用目录读取当前用户及公共开始菜单，并补充打包应用。`ApplicationPlatform` 使用系统 API 处理快捷方式/AUMID；后台执行与受控启动边界见[架构](architecture.md)。管理员启动仍待专门交互，不使主进程长期提升权限。

快捷栏动作由 `DesktopShellService` 的另一专用线程串行处理。服务与 Windows 平台层均只接受 `documents`、`downloads`、`settings`、`account`：文档与下载使用系统 Known Folders，设置与账户仅使用固定 URI `ms-settings:`、`ms-settings:yourinfo`。界面不能提交任意路径、URI 或 Shell 命令。失败返回结构化原因，重复点击不排队执行多个动作。头像使用圆形占位素材；电源文字菜单提供默认关闭的 Win 键唤起启用/暂停，以及退出 XDStarter，系统关机、重启与注销未启用。

托盘使用隐藏的本进程顶层宿主接收 Shell 通知。原生 WndProc 只排队事件，文字菜单与应用状态变化由 Qt 事件循环处理；停止时移除图标、结束原生菜单并失效会话代次，旧通知不影响新的托盘会话。退出清理资源，软件图标从自带 QImage 创建，不读取机器本地图标路径。

Qt Quick 窗口首次显示前连接场景图错误处理。硬件与 WARP 均无法初始化时，应用将原因写入标准错误并以非零状态退出；常驻模式先停止两类输入接管和托盘，再启动独立辅助进程向当前 Windows 会话发送中文错误提示，不等待用户操作或提示关闭，随后继续布局保存与退出。辅助进程不初始化 Qt Quick 或输入接管；系统会话提示由 Windows 持有并在 60 秒后自动关闭，发送失败时回退到独立进程内的原生提示，辅助模式启动后设置 30 秒看门狗。回退期间该进程仍持有程序映像，安装或卸载可能暂时遇到文件占用。用户无需安装通知快捷方式；无法创建进程、系统桌面不可用或两种系统提示均失败时无法保证可见性，相关失败写入标准错误。重复错误仅尝试一次；冒烟模式不显示提示，等待场景图初始化，未在限定时间内完成则失败。此处理覆盖 Qt 报告的场景图初始化错误，不承诺检测所有运行中设备丢失或其他无响应情况。

收到 `TaskbarCreated` 后，原生回调只排队通知；Qt 线程先将托盘标为不可用、暂停两类输入接管并失效旧定位请求，再尝试重新注册图标。注册成功后接管保持暂停，需用户显式恢复；注册失败时显示可见的重试／退出窗口，不使用最小化任务栏入口。恢复窗口的显示延迟一个事件循环，以免同轮注册成功时无故打开菜单；如果恢复未完成或失败，仍显示安全入口。自有隐藏窗口与注入托盘 API 的自动回归覆盖消息派发、成功／失败和退出重入，未重启真正的 Explorer 或安装真实 Hook；实际系统恢复、睡眠唤醒仍待 #32 验收。开机启动项管理和显式打开系统开始菜单的独立动作仍待后续开发；暂停或退出接管后，系统开始按钮与原生 Win 键路径继续可用，实际恢复须按当前版本验收。

## 必须产出的验证记录

原型 Issue 与[测试策略](testing.md)记录 Windows 版本/构建、架构、任务栏设置、DPI、Qt 版本、渲染后端、程序版本、通过/失败用例与恢复步骤。当前弹层需单独验证无标题栏、单层圆角/边框/阴影、左侧与底部可见面板间距相同、可信/失败重复唤起位置一致、点击外部与应用切换收起、内部菜单/Esc、拖拽越界释放、编辑状态保留、托盘恢复及新进程布局恢复。返回的面板定位矩形、透明外窗实际边界、外观 API 返回与实际截图分别记录；若托盘点击需要用户协助，也要注明。真实快捷动作需要有意执行，常规自动测试仅注入假平台，不自动打开用户文件夹或系统设置。

只有记录实际通过的组合进入支持矩阵。只读 UIA probe、模拟平台、强制缩放 GUI 回归、普通窗口历史结果或一次本机 DWM 请求，均不能单独证明完整弹层外观、系统接管、混合 DPI、自动隐藏任务栏或 Explorer 重启兼容。高对比度、远程会话和系统效果关闭等环境需另行验证。

参考：[低级键盘 Hook](https://learn.microsoft.com/en-us/windows/win32/winmsg/lowlevelkeyboardproc)、[SendInput](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput)、[PowerToys 键盘事件机制](https://github.com/microsoft/PowerToys/blob/main/doc/devdocs/modules/keyboardmanager/keyboardmanager.md)、[DWM 窗口属性](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/nf-dwmapi-dwmsetwindowattribute)。
