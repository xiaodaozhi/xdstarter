# ADR-0014：开始按钮点击与自适应任务栏

- 日期：2026-10-23
- 状态：已采纳；实际验收按程序版本记录于测试策略
- 跟踪：[Issue #45](https://github.com/xiaodaozhi/xdstarter/issues/45)、父 [#6](https://github.com/xiaodaozhi/xdstarter/issues/6)、兼容矩阵 [#33](https://github.com/xiaodaozhi/xdstarter/issues/33)
- 补充：ADR-0009 的只读研究；替代 ADR-0013 的“只有排查、未接管”现状以及仅水平居中的布局策略

## 背景与决定

Windows 11 的 StartButton 可以没有独立 HWND，本机研究结果为 `no-native-window`。本方案不要求 XAML 子元素拥有 HWND，也不把任务栏宿主加旧按钮矩形作为点击许可。正常托盘注册成功后启用独立的开始按钮输入服务；托盘与电源菜单提供暂停和重新启用。Win 键服务继续独立且默认关闭，布局 schema 和 Qt Quick 技术栈不变。

查询线程在真实 Explorer 任务栏内查找唯一 `AutomationId=StartButton`、`ControlType=Button`，验证提供方进程、可见性和边界，再对实际指针位置执行 `ElementFromPoint`，通过 `CompareElements` 与最多八层 raw parent 确认该点属于同一按钮。查询前后复核指针、按钮矩形、任务栏身份和几何；UIA 超时设置为 100ms，整个点资格查询超过 200ms 则弃用。COM 与 UIA 均在输入线程之外。

资格快照包含任务栏与实际命中窗口、Explorer PID、显示器身份和物理边界、DPI、按钮物理矩形、已查询的点、时间和失效代次。UIA 的结构／关键属性事件及原生任务栏显示、销毁、重排和位置事件只增加失效代次。低级输入回调以 try-lock 读取快照，不等待查询线程；原生窗口、进程、显示器、DPI、几何和 WindowFromPoint 必须仍匹配。快照年龄最多 200ms，实际点与已查询点每轴差不超过 1 物理像素。缺少资格、移动过快、遮挡、过期、异常或修饰键参与时，新的按下和释放全部走原生路径。

UIA、WinEvent 和原生查询不是一个原子事务，微软也不保证提供方发出所有事件。因此这些检查缩小剩余竞态，不能保证共享 XAML 内部变化绝无漏报；200ms 是年龄上限而非安全证明。过快到达后立即点击可能使用原生菜单，不通过重放或先开再关原生 Start 掩盖。该限制与具体 Windows 版本的实际结果必须保留在验收说明中。

## 点击状态与恢复

专用消息线程维护低级鼠标 Hook 和只取消候选的键盘观察 Hook。回调只做固定原生身份检查、状态转换和消息派发，不执行 COM、磁盘、QML 或窗口显示。开始按钮动作在完整左键释放后排队给 host，完整手势只派发一次。双击作为两次独立点击切换。

一旦接收 down，就拥有相应 up；拖出或每轴移动超过 4 物理像素、超过 1500ms 的长按、键盘／其他鼠标事件、资格失效、暂停和退出都会取消动作，仍消费配对释放。未接管的右键、滚轮、其他任务栏按钮和 Win+X 不消费。合成输入全部透传并取消候选，不为测试放宽此规则，也不合成或重放用户点击。

暂停立即取消事件会话和后续动作，消息线程排空已接收的释放后移除 Hook；正常退出等待资源释放。退出最多等待 2000ms 排空，防止设备断开后永久挂住；超过期限移除 Hook，之后的迟到原生 up 可能没有原生 down，不能把这种异常退出边界写为完整配对保证。输入桌面不可用、远程会话、交换主次鼠标键、托盘丢失和已知 Hook／消息错误保留系统入口；托盘恢复不自动重新开启本轮失败或暂停的接管。Hook 被系统静默移除仍无可靠直接查询接口。

## 显示与定位

开始按钮、托盘、IPC 与 Win 键通过同一 host 唤起。先在隐藏阶段完成跨屏 DPR 准备与原生位置设置，再调用 Qt showNormal。随后由 Windows 适配层只对当前 GUI 线程的本进程顶层 HWND 复核隐藏／最小化状态，显式 restore/show 并请求前台。这样不把 Qt 的 `isVisible()` 视为原生窗口已正常显示，也不让启动器的 STARTUPINFO 隐藏／最小化方式覆盖用户此次唤起。日志分别记录原生 visible、minimized 和 foreground；前台请求被系统拒绝不能写为激活成功。不修改全局前台锁策略，不发送伪造键盘输入。

位置根据实际任务栏窗口几何推导四边停靠，水平任务栏按左／中，垂直任务栏按顶／中定位。居中时两侧远离停靠轴的边缘都可调整；贴近任务栏的边不提供缩放。自动隐藏任务栏展开而 rcWork 尚未收缩时，额外扣除与任务栏重叠的边缘，已预留的空间不重复扣除。原生位置失败的 Qt 回退沿用相同方向和居中策略。48 DIP 小格与 8 DIP 间隙不变。

前台请求首次被拒绝时，参照 Qt Windows 的显式激活路径，先以 50ms 有界 WM_NULL 检查当前前台窗口仍响应且身份未变，再临时 AttachThreadInput、提升本窗口并设置焦点，随后立即解除。失败不循环重试、不连接安全桌面，也不注入 Alt／Win 键。测试分别检查窗口可见与真正前台，二者不能互相替代。

## 验证与依据

常规测试使用纯状态机、参数化几何、host fixture 和 QML MouseArea 的可观察行为，不安装全局 Hook。`xdstarter_native_taskbar_probe --run` 显式测量测试进程自己的隐藏 HWND，在实际显示器 DPR 下比较 GetWindowRect 与预期面板边界；合成停靠数据与实际 Explorer 任务栏样本分别标记，不能互相替代。`--show-state` 另外检查显式显示恢复。真实鼠标、系统任务栏设置和边框拖动必须单独记录；首次显示失败的用户反馈保留为失败，不改标为通过。

依据：[低级鼠标 Hook](https://learn.microsoft.com/en-us/windows/win32/winmsg/lowlevelmouseproc)、[UIA 元素与点查询](https://learn.microsoft.com/en-us/windows/win32/winauto/uiauto-obtainingelements)、[UIA 事件限制](https://learn.microsoft.com/en-us/windows/win32/winauto/uiauto-eventsforclients)、[ShowWindow](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-showwindow)、[2026 年任务栏四边位置与对齐](https://blogs.windows.com/windows-insider/2026/05/15/improving-windows-quality-making-taskbar-and-start-more-personal/)。
