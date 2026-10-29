# ADR-0014：开始按钮点击与自适应任务栏

- 日期：2026-10-23
- 状态：已采纳；实际验收按程序版本记录于测试策略
- 跟踪：[Issue #45](https://github.com/xiaodaozhi/xdstarter/issues/45)、父 [#6](https://github.com/xiaodaozhi/xdstarter/issues/6)、兼容矩阵 [#33](https://github.com/xiaodaozhi/xdstarter/issues/33)
- 窗口行为修订：[Issue #48](https://github.com/xiaodaozhi/xdstarter/issues/48)。旧程序的验收与失败记录保留原版本身份，不能替代新候选验证。
- 补充：ADR-0009 的只读研究；替代 ADR-0013 的“只有排查、未接管”现状以及仅水平居中的布局策略

## 背景与决定

Windows 11 的 StartButton 可以没有独立 HWND，本机研究结果为 `no-native-window`。本方案不要求 XAML 子元素拥有 HWND，也不把任务栏宿主加旧按钮矩形作为点击许可。正常托盘注册成功后启用独立的开始按钮输入服务；托盘与电源菜单提供暂停和重新启用。Win 键服务继续独立且默认关闭，布局 schema 和 Qt Quick 技术栈不变。

查询线程在真实 Explorer 任务栏内查找唯一 `AutomationId=StartButton`、`ControlType=Button`，验证提供方进程、可见性和边界，再对实际指针位置执行 `ElementFromPoint`，通过 `CompareElements` 与最多八层 raw parent 确认该点属于同一按钮。查询前后复核指针、按钮矩形、任务栏身份和几何；UIA 超时设置为 100ms，整个点资格查询超过 200ms 则弃用。COM 与 UIA 均在输入线程之外。

资格快照包含任务栏与实际命中窗口、Explorer PID、显示器身份和物理边界、DPI、按钮物理矩形、已查询的点、时间和失效代次。UIA 的结构／关键属性事件及原生任务栏显示、销毁、重排和位置事件只增加失效代次。低级输入回调以 try-lock 读取快照，不等待查询线程；原生窗口、进程、显示器、DPI、几何和 WindowFromPoint 必须仍匹配。快照年龄最多 200ms，实际点与已查询点每轴差不超过 1 物理像素。缺少资格、移动过快、遮挡、过期、异常或修饰键参与时，新的按下和释放全部走原生路径。

UIA、WinEvent 和原生查询不是一个原子事务，微软也不保证提供方发出所有事件。因此这些检查缩小剩余竞态，不能保证共享 XAML 内部变化绝无漏报；200ms 是年龄上限而非安全证明。过快到达后立即点击可能使用原生菜单，不通过重放或先开再关原生 Start 掩盖。该限制与具体 Windows 版本的实际结果必须保留在验收说明中。

## 点击状态与恢复

专用消息线程维护低级鼠标 Hook 和只取消候选的键盘观察 Hook。回调只做固定原生身份检查、状态转换和消息派发，不执行 COM、磁盘、QML 或窗口显示。开始按钮动作在完整左键释放后排队给 host，完整手势只派发一次。双击作为两次独立点击切换。

一旦接收 down，就拥有相应 up；拖出或每轴移动超过 4 物理像素、超过 1500ms 的长按、键盘／其他鼠标事件、资格失效、暂停和退出都会取消动作，仍消费配对释放。未接管的右键、滚轮、其他任务栏按钮和 Win+X 不消费。合成输入全部透传并取消候选，不为测试放宽此规则，也不合成或重放用户点击。

暂停立即取消事件会话和后续动作，消息线程排空已接收的释放后移除 Hook；正常退出等待资源释放。退出最多等待 2000ms 排空，防止设备断开后永久挂住；超过期限移除 Hook，之后的迟到原生 up 可能没有原生 down，不能把这种异常退出边界写为完整配对保证。输入桌面不可用、远程会话、交换主次鼠标键、托盘丢失和已知 Hook／消息错误保留系统入口；收到任务栏重建通知时也先暂停接管并失效旧定位，托盘重注册成功后不自动重新开启。Hook 被系统静默移除仍无可靠直接查询接口。

## 显示与定位

开始按钮、托盘、IPC 与 Win 键通过同一 host 唤起。先在隐藏阶段完成跨屏 DPR 准备与原生位置设置，再调用 Qt showNormal。随后由 Windows 适配层只对当前 GUI 线程的本进程顶层 HWND 复核隐藏／最小化状态，显式 restore/show 并请求前台。这样不把 Qt 的 `isVisible()` 视为原生窗口已正常显示，也不让启动器的 STARTUPINFO 隐藏／最小化方式覆盖用户此次唤起。日志分别记录原生 visible、minimized 和 foreground；前台请求被系统拒绝不能写为激活成功。不修改全局前台锁策略，不发送伪造键盘输入。

位置根据实际任务栏窗口几何推导四边停靠，水平任务栏按左／中，垂直任务栏按顶／中定位。居中时两侧远离停靠轴的边缘都可调整；贴近任务栏的边不提供缩放。自动隐藏任务栏展开而 rcWork 尚未收缩时，额外扣除与任务栏重叠的边缘，已预留的空间不重复扣除。原生位置失败的 Qt 回退沿用相同方向和居中策略。48 DIP 小格与 8 DIP 间隙不变。

前台请求首次被拒绝时，参照 Qt Windows 的显式激活路径，先以 50ms 有界 WM_NULL 检查当前前台窗口仍响应且身份未变，再临时 AttachThreadInput、提升本窗口并设置焦点，随后立即解除。失败不循环重试、不连接安全桌面，也不注入 Alt／Win 键。测试分别检查窗口可见与真正前台，二者不能互相替代。

## 真机缩放与隐藏任务栏修正

后续顶部靠左验收发现两项失败：底边调整高度时，可见上沿由物理 y=84 变成108；另一次重开查询只有顶部预留工作区、没有可见任务栏，原逻辑退回底部。旧程序的失败与底部居中通过记录保留，不由新程序覆盖。

布局查询可以读取同一显示器上暂时隐藏的真实 Explorer 任务栏几何，优先选择可见任务栏；这类元数据不构成点击许可，Start 的可见性与点资格规则保持原样。任务栏几何缺失时，只在工作区相对屏幕恰好预留单一边缘时推导方向；没有预留或有多个预留边时不猜测。输入服务不使用此回退。

原生 sizing 把完整 HWND（包含24 DIP透明阴影）限制到工作区，导致可见面板的非拖动边偏移。保留 Qt 系统 resize，在 Windows 平台层为本进程 GUI 线程的菜单 HWND 安装可移除的 `SetWindowSubclass` 处理器：在 Qt 派发的 `SC_SIZE` 阶段记录归一化前的外框，`WM_SIZING` 固定未拖动的边并保留最小尺寸，拖动期间的 `WM_WINDOWPOSCHANGING` 保留最近确认的 sizing 矩形，避免进入循环前和松手前的系统归一化再次移动阴影。未进入循环的准备状态最多保留两秒；退出拖动立即清除，窗口销毁或平台对象析构时移除。其他 HWND 和未处于拖动循环的消息照常传递，不处理 Explorer 窗口。

通用矩形约束不含 Win32，覆盖五档缩放、负坐标、四边／四角的80组组合，另验最小尺寸及整数边界。原生回归只向测试自有隐藏 HWND 发送 sizing 单元消息，覆盖八个边／角、三个准备／归一化阶段和三个非活动／其他窗口／移除后的透传场景，共14项；夹具拦截 sizing 命令，不进入系统模态输入循环。Qt6.8.2 的应用级 nativeEventFilter 会跳过 `WM_SIZING`，因此不能用该入口替代本窗口处理器；初版八项原生用例失败也保留为实际失败。

依据：[Qt6.8.2 消息分派源码](https://github.com/qt/qtbase/blob/v6.8.2/src/plugins/platforms/windows/qwindowscontext.cpp)、[Qt 系统 resize 入口](https://github.com/qt/qtbase/blob/v6.8.2/src/plugins/platforms/windows/qwindowswindow.cpp)、[WM_SIZING](https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-sizing)、[WM_WINDOWPOSCHANGING](https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-windowposchanging)、[SetWindowSubclass](https://learn.microsoft.com/en-us/windows/win32/api/commctrl/nf-commctrl-setwindowsubclass)。

## Issue #48：窗口生命周期与边界修订

当前实现将主菜单类型改为 `Qt.Tool | Qt.FramelessWindowHint | Qt.NoDropShadowWindowHint | Qt.WindowStaysOnTopHint`。Windows 层在隐藏阶段设置 `WS_EX_TOOLWINDOW`、清除 `WS_EX_APPWINDOW`，并请求 topmost；保留透明合成样式，以本窗口 region 排除任务栏交集，不向 Explorer 注入代码。目标是在菜单显示与隐藏时均不产生普通任务栏窗口按钮，同时保持正常键盘输入。实际 HWND 样式、层级和 Shell 表现仍须按当前候选验证，不能以 Qt flags 声明替代实际结果。

隐藏原生 STATIC fixture 曾出现 `SetWindowPos` 返回成功而实际 extended style 只有 `0x80`、没有 TOPMOST 的结果；原 style `0x84000000` 未变，窗口始终隐藏。这不是产品真机置顶失败的证据，也不能记作隐藏置顶成功。Microsoft 的 `SetWindowPos` 说明对提升窗口要求前台激活权限。实现分别刷新 frame 与请求 topmost，随后读取实际位；未生效必须返回 `success=false` 与 `menu-topmost-state-denied`。用户唤起在恢复前台后再进行一次有界请求并实测 TOPMOST，不用计时器循环抢焦点。隐藏 fixture 只允许实测成功或上述明确拒绝，实际 Qt 窗口和可见候选仍须强检 TOPMOST。

自有原生文件／设置对话框继续以菜单为 owner。重复唤起只在真正自有前台对话框存在时保留其交互，不将菜单本身的前台身份误当成对话框保护。`hasOwnedForegroundDialog()` 排除根窗口并复核精确 owner 链和前台稳定性；失焦判断继续使用包含菜单本身的 `hasOwnedForegroundWindow()`，归属语义不变。没有永久抢焦点或反复提升窗口的计时器。

host 在隐藏阶段完成几何与 DPR 准备后调用 `stagePresentation()`，停止旧动画并将进度置为 0，然后才显示原生窗口，本轮仅调用一次进入动画。独立 GUI fixture 的自行显示入口与受 host 管理的产品入口明确区分；排队回调核对显示代次。已显示窗口的重复唤起使用 `restorePresentationFocus()`，取消正在进行的退出而不重播进入动画。旧弹层仅剩退出动画时，焦点恢复等待其关闭；真正仍在交互的内部弹层保持其焦点。

开始按钮、右上角收起、Esc 和外部失焦继续进入同一 host 收起转换。退出动画完成后隐藏 HWND；重复收起不重新延长计时器，动画期间重新唤起使旧隐藏失效。首次激活被拒绝时仍保留原有焦点防误收起边界。

旧的“无托盘则最小化并保留任务栏按钮”恢复策略由本次修订替换。托盘失效会暂停两类接管，恢复并保持一个可见、非最小化的 Tool 窗口，提供持续可见的重试与退出入口。外部失焦不隐藏最后的恢复窗口；主动收起或关闭最后窗口请求正常退出。单例 IPC 可再次唤起，但不作为唯一可发现的恢复方式。托盘恢复不会自动重新启用已暂停或失败的输入接管。此前 ADR 与测试中的最小化记录属于当时版本，不修改其事实。

尚未完成可信原生定位的无托盘紧急恢复入口采用更保守的几何：整个透明外窗及阴影都放在工作区四边 8 DIP 以内，最大尺寸亦受此约束，并临时关闭边框拖动。这一恢复状态优先保持原生开始按钮和重试／退出可达；不沿用正常菜单“可见面板 8 DIP、部分阴影跨出工作区”的策略。重试注册成功后重新经过隐藏定位和 region 建立，恢复正常面板间距及方向对应的可拖动边，不永久关闭缩放。

普通原生定位失败但仍可取得 Qt 工作区的降级显示使用同样的完整外窗边界，本轮禁用缩放，下一次成功原生定位才恢复正常边缘。降级状态不继续套用旧显示器／任务栏方向的排除 region；确切的 region 建立失败仍直接取消显示，不走该回退。

可见面板允许矩形由 `desktopMenuWorkArea()` 四边各扣除指定间距产生，当前为 8 DIP。显示准备先夹限旧尺寸，并统一 Qt maximumSize、有效 minimumSize 与原生 `WM_SIZING` 的允许外窗矩形；小工作区的有效最小值不得大于最大值。拖动边不能突破允许矩形，非拖动边保持固定；工作区在可见期间缩小时，原生缩放再次读取当前显示器工作区，较小边界优先。宽度吸附在相同最小／最大范围内选择档位，吸附后复核边界。透明外窗仍计入 24 DIP 阴影留白，面板与外窗边界分别计算。

topmost 带来单独的输入命中问题：24 DIP 阴影留白大于 8 DIP 面板间距，外窗可与任务栏重叠。2026-10-23 正常桌面权限下，只读探针观察隔离布局候选 PID 33672：外窗为 `[-24,460,1542,1092]`，底部任务栏始于物理 y=1528，Start 矩形为 `[0,1528,83,72]`，region 尚未设置。菜单位于任务栏之前，9 个 Start 采样点中有 6 个被菜单命中，UIA 也未识别为 Start；中心点仍属于任务栏。该实际失败保存在本地 `build/validation/issue48-product-probe/start-overlap.json`，不因后续修复覆写。探针没有注入输入，采样几何稳定；指针前后位置不同，不将其描述为桌面没有用户活动。此轮隔离候选的清理不作为从菜单正常退出的验收证据。

因此采用有实际失败依据的原生 region 修复：纯 Qt 几何 helper 将完整外窗的本地物理区域减去与任务栏矩形的交集，仅裁掉覆盖任务栏的阴影，保留可见面板 8 DIP 间距和其余方向／屏幕边的阴影。不将整个工作区作为一刀切裁剪，也不依赖仅对[同线程其他窗口有效的 HTTRANSPARENT](https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-nchittest)。任务栏实际几何缺失时，仅使用显示器相对工作区的唯一预留条带回退，不赋予点击资格。

Windows 在最终定位后对本进程 GUI 线程的菜单 HWND 调用 `SetWindowRgn`。同一自有窗口子类在位置、尺寸、显示和 DPI 的 Qt 默认处理完成后复核并重套，避免 Qt 清空 mask 或尺寸变化留下旧区域；比较已有 region 并使用重入保护，防止该 API 产生的窗口位置消息形成循环。API 成功后将 HRGN 所有权交给系统；失败才由调用方释放。无法建立所需区域时取消本轮显示，不能回退为继续覆盖任务栏的窗口。新源码增加纯几何、自有隐藏 HWND 和 host 失败分支回归，但本段不声称修复已经通过构建或原生点位复测。

局部 region 的交集直接用 `qint64` 端点运算，不对全局坐标使用 QRect 的通用归一化交集：[固定 Qt 6.8.2 实现](https://github.com/qt/qtbase/blob/v6.8.2/src/corelib/tools/qrect.cpp#L936)包含 `left()-1`／`top()-1`，有效矩形处于 INT_MIN 时也可能溢出。边界回归同时覆盖 INT_MIN 与 INT_MAX，只在转换为非负局部范围后构造 QRegion。

`--run --observe-start-overlap PID` 对当前可见候选进行只读 HWND、Z 序、region、Start UIA 矩形及边缘／中心点采样，不移动指针、不发送输入、不安装 Hook，也不赋予点击资格。样式声明、命中采样和真实左／右键验收分别记录，修复后须使用同一模式确认实际命中，而不是以 region 已设置为通过证据。

依据：[Microsoft 任务栏按钮规则](https://learn.microsoft.com/en-us/windows/win32/shell/taskbar)、[SetWindowPos 与 owner 层级](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowpos)、[Qt 6.8.2 Windows 窗口实现](https://github.com/qt/qtbase/blob/v6.8.2/src/plugins/platforms/windows/qwindowswindow.cpp)、[Layered Windows 命中规则](https://learn.microsoft.com/en-us/windows/win32/winmsg/window-features#layered-windows)、[SetWindowRgn 坐标、消息与所有权规则](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowrgn)。源码调查与待验证边界另见[窗口研究](../research/menu-window-followup-48.md)，实际结果见测试策略与 Issue / PR。

## 验证与依据

常规测试使用纯状态机、参数化几何、host fixture 和 QML MouseArea 的可观察行为，不安装全局 Hook。`xdstarter_native_taskbar_probe --run` 显式测量测试进程自己的隐藏 HWND，在实际显示器 DPR 下比较 GetWindowRect 与预期面板边界；合成停靠数据与实际 Explorer 任务栏样本分别标记，不能互相替代。`--show-state` 另外检查显式显示恢复。真实鼠标、系统任务栏设置和边框拖动必须单独记录；首次显示失败的用户反馈保留为失败，不改标为通过。

依据：[低级鼠标 Hook](https://learn.microsoft.com/en-us/windows/win32/winmsg/lowlevelmouseproc)、[UIA 元素与点查询](https://learn.microsoft.com/en-us/windows/win32/winauto/uiauto-obtainingelements)、[UIA 事件限制](https://learn.microsoft.com/en-us/windows/win32/winauto/uiauto-eventsforclients)、[ShowWindow](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-showwindow)、[2026 年任务栏四边位置与对齐](https://blogs.windows.com/windows-insider/2026/05/15/improving-windows-quality-making-taskbar-and-start-more-personal/)。
