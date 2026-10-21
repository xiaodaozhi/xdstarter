# ADR-0008：可暂停的单独 Win 键唤起原型

- 日期：2026-10-21
- 状态：已采用的局部原型方案；机制推导与实现不替代真机验收
- 关联：[Issue #22](https://github.com/xiaodaozhi/xdstarter/issues/22)、[系统集成研究 #6](https://github.com/xiaodaozhi/xdstarter/issues/6)
- 延续：[ADR-0007](0007-custom-menu-chrome.md) 的自绘窗口与隐藏阶段定位，以及 [ADR-0006](0006-menu-popup.md) 的焦点、内部菜单和可恢复收起规则

## 背景

托盘和窗口内入口已可唤起菜单，但仍缺少产品所需的单独 Win 键入口。此次先实现由用户显式启用的局部原型，不接管开始按钮，不向 Explorer 注入代码，也不替换系统文件。继续使用 Qt Quick / QML、Qt 6.8.2 与现有工具链。

必须区分单独 Win 与 Win 组合键。吞掉 Win-down 后等待第二个键，需要重放组合输入，增加 Win+L、提权前台和第三方快捷键的风险；只放行 down、永久吞掉 up，则不能保证修饰键状态正常释放。先让原生 Start 打开再关闭也会闪烁，不作为可接受实现。

## 决定

1. `WinKeyCapture` 为独立的可替换平台接口，提供 `setEnabled(bool)`、`stop()`、`isActive()`、`isSupported()`，通过 `toggleRequested` 和 `statusChanged(active, message, errorCode)` 报告实际状态。QML、通用布局和领域模型不直接依赖 Win32。构造对象和每次启动均不安装 Hook；启用状态不写布局或偏好文件，用户从“电源与退出”或托盘文字菜单启用、暂停或恢复。
2. `DesktopWindowHost` 仅在支持环境且托盘可用时允许启用。托盘失效立即停止接管，恢复注册不自动重启接管；退出停止接管、失效已排队通知，并等待消息线程释放资源。暂停和失败也使旧一轮 toggle 失效。开始按钮、Ctrl+Esc 和未接管输入继续交给系统；此处描述事件路径，不代替实际系统行为验证。
3. Windows 在专用消息线程安装 `WH_KEYBOARD_LL`，并安装观察鼠标按钮、滚轮的 `WH_MOUSE_LL`，后者仅维护按钮是否按住与取消 Win+鼠标独按候选，从不消费鼠标输入或保存坐标。回调只维护有界的按键状态、执行下述固定输入派发、投递线程消息；不做 COM、磁盘、QML、窗口定位、激活或等待 GUI。窗口切换在 Qt 主线程复用既有 host 流程。
4. 真实左右 Win-down 及组合键事件直接放行。只有按下时没有其他键或鼠标按钮按住、期间没有其他按键或鼠标按钮/滚轮参与的首次 Win-up 可成为独按请求；重复 down 不重复触发，两侧 Win 重叠取消独按。安装时在回调外读取一次已按下键与鼠标按钮，避免把启用前已按住的输入误判为新手势。自有合成输入按标记透传以防递归；外部合成输入不能触发自有 toggle，并取消可能冲突的候选。当前事件的判断来自状态机，不在回调内用 `GetAsyncKeyState` 查询它尚未更新的异步状态。
5. 独按释放通过一次 `SendInput` 固定派发三条事件：`0xFF down`、`0xFF up`、对应左右 Win 的 extended key-up。所有事件带自有标记；只有返回完整的三条成功数量，才消费物理 Win-up 并投递 toggle。这样无需重放 Win+E/R/L/数字等组合，也不主动打开再关闭原生 Start。输入派发未完整成功时，真实 Win-up 原样放行，尽力补充 dummy key-up，停止接管并报告失败；不能把部分插入模拟测试写成真实系统的完整恢复证明。输入已成功但 toggle 消息投递失败时，Win 已有匹配释放，本次唤起可能丢失，接管停止，后续由系统处理。
6. 不将这三条输入先排入异步 GUI 或其他 worker：若先吞物理 up 再注入，失败时无法保证匹配释放，期间也可能夹入新的物理输入。因此固定、有界的输入派发留在平台 Hook 回调，窗口与服务工作仍排队。这是对“输入回调只派发事件”的具体化，不能扩展为在回调中执行应用逻辑。
7. 当前仅允许本地 Windows 11 桌面原型。启用前及专用线程每 500 ms 检查当前输入桌面是否为本线程所在的 `Default` 桌面；检查失败、切换至其他输入桌面、Hook 安装、输入派发或消息循环失败时停止接管，需要用户手动重新启用。远程会话和非支持系统返回不支持。500 ms 检查存在观察间隔，不承诺检测所有短暂安全桌面切换；高对比度、锁屏、UAC、提权前台、远程环境及其他构建不由本机结果推定支持。

## 依据与限制

微软说明低级键盘 Hook 在安装线程执行，该线程需要消息循环；Hook 调用先于异步键态更新。超时后系统可能静默移除 Hook，且没有直接查询其是否已被移除的方法。线程或定时器仍响应不构成 Hook 存活证明；本原型只处理已知的安装、消息线程、输入桌面与派发失败，不宣称完全检测静默卸载。[LowLevelKeyboardProc](https://learn.microsoft.com/en-us/windows/win32/winmsg/lowlevelkeyboardproc)

初始键态只读取 `GetAsyncKeyState` 的高位；鼠标结果表示物理按钮，需结合 `SM_SWAPBUTTON` 与后续逻辑按钮消息对齐，通用修饰键别名不能重复留作永久按下状态。此快照不替代后续按键、鼠标按钮事件的配对维护。[GetAsyncKeyState](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getasynckeystate)

`SendInput` 报告成功插入的事件数量，受 UIPI 限制，只能向相同或更低完整性级别注入；错误码不能可靠识别是否由 UIPI 导致。普通用户程序不通过提权或放宽系统隔离来规避此限制，失败时停止接管。[SendInput](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput)

`0xFF` 是 PowerToys 用于避免 Win/Alt 单独释放动作的未文档化办法，不是 Windows 对开始菜单接管的正式保证。此原型参考该机制但独立实现，不复制第三方代码；其真实表现与其他 Hook 的组合仍需验证。[PowerToys Keyboard Manager 机制说明](https://github.com/microsoft/PowerToys/blob/main/doc/devdocs/modules/keyboardmanager/keyboardmanager.md)

## 验证与范围

自动检查可覆盖纯状态机中的左右 Win、重复、重叠、修饰键、组合键与外部合成输入，固定派发的完整/失败数量，以及 host 的托盘守护、退出、状态展示与旧通知取消；这些检查不证明原生 Shell 已接受输入、没有 Start 闪烁或 Win+L 已实际锁屏。

当前程序的物理 Win 键、左右组合、重复唤起、暂停恢复、内部菜单焦点、退出后原生入口与异常恢复，须记录确切 Windows 构建、Qt/RHI、程序版本和实际步骤，统一写入[测试策略](../testing.md)。需要人操作的键盘验收由用户协助，不以工具合成输入冒充物理按键结果。此文档确立机制与失败策略，不提前声明这些用例通过。

本轮通过不关闭父 Issue #6：开始按钮左键、右键系统菜单、任务栏居中/左对齐与自动隐藏、多屏混合 DPI、Explorer 重启、睡眠恢复及背景材料均按各自证据继续推进。启用开关不改变布局 schema，不增加开机自启，也不持久化系统接管权限。
