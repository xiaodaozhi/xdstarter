# ADR-0009：开始按钮原生命中的只读准入研究

- 日期：2026-10-21
- 状态：采用只读研究方案；实际环境结论由验证记录给出
- 关联：[Issue #24](https://github.com/xiaodaozhi/xdstarter/issues/24)、[系统集成研究 #6](https://github.com/xiaodaozhi/xdstarter/issues/6)、PRD-010
- 延续：[ADR-0008](0008-win-key-capture.md) 的单独 Win 键边界，以及 [ADR-0007](0007-custom-menu-chrome.md) 的快速工作区定位

## 背景

单独 Win 键原型不接管系统开始按钮。已有 `queryStartButtonAnchor()` 能在当前任务栏内识别唯一的 UIA `StartButton`，但 UIA 元素可能没有独立 HWND，或只对应覆盖多个任务栏项目的共享窗口。某次可信矩形不能证明之后的物理点击仍落在同一个按钮上。

任务栏排列、显示器、DPI、可见性和 Explorer 生命周期均可能改变按钮位置。若先缓存矩形、再在鼠标回调中按矩形消费点击，刷新间隔内可能拦截其他任务栏项目；反过来在回调内执行 UIA/COM 查询，则引入跨进程等待。因此先检验原生命中条件，并将消费输入的机制留给后续独立决策。

## 决定

1. 在可替换的 `DesktopPlatform` 边界增加只读命中诊断，由测试程序的显式 `--probe-start-hit` 调用。普通产品启动、菜单唤起和常规 CTest 不运行真实 UIA probe；产品的 `queryMenuAnchor()` 继续只读快速工作区信息，不引入 UIA 或按钮识别等待。
2. 复用现有唯一 `AutomationId = StartButton`、`ControlType = Button`、真实 Explorer 任务栏、提供方进程、可见性和物理边界校验；继续拒绝缺失、多候选、隐藏和越界结果，不按本地化名称、固定坐标或第一个子元素猜测。
3. 读取候选 UIA 元素的原生 HWND，并诊断其存在性、进程、所属任务栏、可见性、窗口类与几何。读取按钮中心及内缩四角的 `WindowFromPoint` 结果，比较实时窗口身份。输出区分没有 HWND、共享宿主、几何不符、遮挡或命中不一致，以及满足本轮保守条件的独立命中候选；不能只因 HWND 非零或进程同为 Explorer 就判定独立按钮。
4. probe 只读取系统状态并输出结果；不安装输入 Hook，不调用控件动作，不消费、合成或重放输入，不移动窗口，不修改任务栏或系统配置，不连接 QML 的开关或持久化偏好。COM 与原生对象在调用范围内释放，不建立持续命中缓存或事件订阅。
5. 每份输出是分步取得的单次快照，并非跨 UIA、窗口查询和五点采样的原子事务。即使全部采样一致，也只允许记录该次候选，不能据此开启点击接管或扩大 Windows 支持范围。诊断失败与不支持是有效研究结果，保留原生开始按钮行为；非 Windows 实现返回明确不支持。

## 官方依据与解释

微软说明 `AutomationId` 不是必需属性，仅要求在兄弟元素中区分，且不保证跨应用版本或构建稳定；UIA 元素粒度也可细于 HWND。因此 `StartButton` 是本轮需要验证的识别条件，不能作为系统承诺的永久标识。[Using UI Automation for Automated Testing](https://learn.microsoft.com/en-us/windows/win32/winauto/uiauto-usefortesting)

`NativeWindowHandle` 在元素没有原生窗口时为 0；`BoundingRectangle` 可包含不可点击或被遮挡的点，`IsOffscreen` 也不表示没有其他窗口遮挡。由此，本项目要求分别记录 UIA 语义身份、原生窗口身份与五点采样，不能把它们互相替代。[Automation Element Property Identifiers](https://learn.microsoft.com/en-us/windows/win32/winauto/uiauto-automation-element-propids)

`WindowFromPoint` 返回当次指定点的窗口，不返回隐藏或禁用窗口；它不提供“这个窗口只代表开始按钮”的语义保证。五点一致、类名和矩形相符只能形成研究候选，不能证明窗口生命周期中始终独占该区域。[WindowFromPoint](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-windowfrompoint)

微软明确不能假设提供方发出所有事件，并非每次属性变化都有通知。本项目据此认为，仅订阅 UIA 事件、仅设置矩形 TTL，或组合两者，都不足以证明缓存命中时仍是开始按钮；共享宿主尤其不能由宿主 HWND 与旧矩形推定安全。[Subscribing to UI Automation Events](https://learn.microsoft.com/en-us/windows/win32/winauto/uiauto-eventsforclients)

## 后续接管必须另行满足的门槛

接管方案需在独立 Issue 中说明命中时可在有界回调内复核的身份与条件，以及缓存缺失、过期、歧义、任务栏移位和共享宿主时的完整原生放行路径。若使用缓存，需绑定任务栏 HWND、进程身份、显示器、DPI、物理几何和会话代次，并处理 Explorer 重建、窗口销毁、位置/显示/DPI/可见性变化和查询失败；失效通知不完备时必须说明剩余竞态与准入限制。TTL 只能限制年龄，不能消除期间位置变化造成的误命中。

还需先实测原生开始按钮在物理 down/up 的哪个阶段动作，再设计完整手势状态机。该状态机必须覆盖配对释放、长按、重复/双击、拖出后释放、其他按钮、修饰键、暂停、失败、退出及失效发生在按住期间的情况；不能先吞 down、失效后又放行 up，也不能假定吞 up 足以阻止原生菜单。右键系统菜单、Win+X 和未接管输入应保留；不得使用先打开再关闭原生 Start 或延后重放点击来掩盖命中失败。

后续必须分别验收真实物理点击、无原生闪现、单次切换、非开始按钮不受影响、暂停/退出/异常后的原生恢复，以及与 Win 键原型共同启用的行为。任务栏左/居中、自动隐藏、混合 DPI、Explorer 重启和睡眠恢复按实际版本记录，未验证组合不承诺支持。UIA/COM 与窗口业务不进入输入回调，通用布局及 QML 不依赖 Win32。

## 验证与影响

native-free 测试只覆盖接口默认行为、替换实现与诊断边界。真实 probe 需显式运行，记录程序 hash、确切 Windows 构建、Qt/RHI、任务栏与显示环境、采样结果和限制，统一见[测试策略](../testing.md)。未运行前不将任何候选条件记为本机通过；只读研究即使取得独立候选，也不构成实际点击交互或系统恢复验收。

本决定不改变菜单位置、磁贴网格、布局 schema、Win 键开关或产品输入行为，不完成父 Issue #6。若真实结果没有独立 HWND 或显示共享宿主，记录这一限制后继续保留原生点击，另定机制。
