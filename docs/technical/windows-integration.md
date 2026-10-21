# Windows 系统集成

状态：[Issue #14](https://github.com/xiaodaozhi/xdstarter/issues/14) 已接入桌面快捷动作、原生托盘与只读开始按钮定位；[#15](https://github.com/xiaodaozhi/xdstarter/issues/15) 接入无边框菜单与外部焦点收起。本轮 [#16](https://github.com/xiaodaozhi/xdstarter/issues/16) 修复横向锚点交替和显示后定位跳变，改用完全自绘圆角、边框和阴影；Windows 平台禁用原生装饰。构建和真机结果按版本写入[测试策略](testing.md)，历史原生外观与本轮结果分别记录，不由实现状态扩大支持矩阵。Win 键/开始按钮接管、背景材料等研究仍未完成。对应 PRD-001、PRD-010、PRD-011。

## 实现边界

独立普通用户桌面程序，保留 Explorer，不向其注入代码，不替换系统文件。使用 `QGuiApplication` 和 Qt Quick，托盘已直接封装 `Shell_NotifyIcon`，没有引入 Widgets。应用目录和桌面快捷动作分别通过可替换平台接口执行。

当前使用 `Qt.Window | Qt.FramelessWindowHint` 的 Qt Quick 菜单窗口，透明外窗容纳自绘阴影与圆角，正文面板仍为主题实色，外部焦点收起由 host 管理。没有安装键盘/鼠标 Hook，没有拦截 Win 键、系统开始按钮或开始菜单，也不使用 `Qt.Popup` 的原生自动关闭与输入抓取。托盘及快捷栏不会改变原生开始菜单行为。

## 唤起与焦点

当前入口是产品窗口与原生托盘。`DesktopWindowHost` 统一处理显示/隐藏、最小化恢复、退出、原生装饰抑制和定位：托盘可用时设置 `quitOnLastWindowClosed(false)`，窗口关闭或收起后保持常驻；托盘不可用时系统关闭窗口仍按默认策略退出，但主动收起和外部焦点收起采用最小化，以免进程隐藏后不可恢复。托盘文字菜单提供“显示/隐藏 XDStarter”和“退出 XDStarter”。

每次用户唤起窗口都重新读取指针所在显示器名称、工作区与任务栏 DPI。`DesktopShellService` 专用 worker 调用 Windows 的 `queryMenuAnchor()`，不初始化 COM、不调用 UIA，也不等待开始按钮提供方；重复请求合并。产品入口加载 QML 时初始 `visible = false`，收到当前有效结果后先配置原生装饰抑制和定位，再显示并请求激活；smoke 与独立 GUI 夹具仍自行显示。关闭、主动收起或取消待显示请求会使旧查询失效，迟到结果不能重新打开窗口，也不对已显示窗口作可见的异步位置修正。启动应用或桌面动作成功后收起窗口；托盘不可用时改为最小化，保留任务栏恢复入口；失败保留窗口并说明原因。

host 监听 Qt 的窗口焦点、窗口激活与应用状态，在持续外部失焦 100 ms 后复核。根窗口及沿 `transientParent` 可追溯到根的内部窗口均属于本菜单；只有本轮显示实际获得过内部焦点后才允许失焦收起，初次激活被系统拒绝时不隐藏。内部尺寸、固定、电源和关于菜单固定为 `Popup.Item`，没有额外的原生顶层焦点交接；托盘原生文字菜单期间暂停复核，返回后再判断。

拖拽且左键仍按下时暂缓外部失焦收起，仅在该待处理状态下短时复查，松手或取消后正常判断。编辑模式不永久阻止外部收起，内部弹层打开也不妨碍真正切换到外部应用后关闭。收起前调用 QML 清理契约，关闭内部弹层、取消未提交预览、暂停迟到的焦点恢复；重新显示时恢复可操作焦点。已提交布局与当前编辑模式保留，定位和焦点计时器不会让旧一轮状态重新显示窗口。

后续系统接管中，Win 键状态跟踪与开始按钮识别继续独立于 QML。平台模块只发出 toggle 请求，由主线程统一处理窗口与焦点；输入回调不承载上述窗口状态逻辑。

单独按下并释放 Win 键切换菜单仍是目标行为；Win+E/R/L/数字、左右 Win 键、按住/重复按键、其他程序快捷键都必须保留并验证。开始按钮左键接管、右键系统菜单分别测试；当前只读定位不构成这些接管用例通过。

如使用低级输入 Hook，放在带消息循环的专用线程；回调只更新最小状态和派发事件，不执行 COM 查询、磁盘操作或 QML 调用。超时、Hook 丢失、Explorer 重启后应能检测异常并停止接管。

## 开始按钮与兼容策略

产品的 `queryMenuAnchor()` 只读取目标显示器/工作区与同屏真实任务栏 DPI，任务栏通过可见的 `Shell_TrayWnd` / `Shell_SecondaryTrayWnd` 及 Explorer 进程校验识别。可见面板左侧与底部统一留 8 DIP，不以开始按钮位置或兼容保留的 `trusted` 字段决定几何。工作区不可用时明确报告定位不可用，保留可达的窗口位置。服务公开名称 `requestStartButtonAnchor()`、`startButtonAnchorReady` 暂保留兼容，Windows 产品路径实际调用的是快速工作区查询。

原 `queryStartButtonAnchor()` 保留为显式只读研究入口：在真实任务栏内通过 UI Automation 查询唯一的 `AutomationId = StartButton` 且 `ControlType = Button`，再检查提供方进程、可见性、尺寸、包含关系与物理边界；标识缺失、多候选、隐藏或越界均返回不可信结果，不按本地化 Name 或固定坐标猜测。该研究查询独立初始化与释放 COM，可用时通过 `IUIAutomation2` 设置 1500 ms 连接/事务超时；对象不跨线程共享。产品唤起不执行此查询，避免隐藏阶段预定位引入提供方超时。所有原生矩形均在物理像素上下文读取并先作范围校验。

显式 `xdstarter_desktop_platform_tests.exe --probe-menu-anchor` 输出工作区查询 JSON；`--probe-anchor` 输出 UIA 开始按钮研究 JSON。二者均需单独记录环境，常规 CTest 不执行桌面 probe，且成功输出不等于系统输入接管或完整 Windows 兼容通过。

任务栏居中/左对齐、自动隐藏、副屏、混合 DPI 和语言差异必须按实际环境验证。自动隐藏、Explorer 重启与混合 DPI 跨屏本轮尚未完成实际验证。将来启用接管前仍须探测支持条件，不以关闭原生菜单后重开自定义窗口的闪烁方案作为验收通过。

对不支持的系统组合保留原生行为；当前集成状态明确说明尚未接管。安全桌面、锁屏、UAC 等系统隔离场景不接管。

## 窗口与材料

平台只对当前进程、当前 GUI 线程的顶层窗口作短时原生定位和装饰抑制。定位读取透明外窗实际物理尺寸，减去两侧阴影留白后得到可见面板尺寸；不再读取 `DWMWA_EXTENDED_FRAME_BOUNDS` 参与布局。间距与留白统一按选定 Qt 窗口的实际渲染 DPR 从 DIP 转换成物理像素，任务栏 DPI 仅用于诊断，可见面板距工作区左侧和底部均为 8 DIP，并在空间不足时夹限工作区；阴影外窗可以越出工作区，不把阴影算入面板间距。`DesktopPlacementResult.physicalRect` 返回可见面板矩形，不能当作外窗边界。极值运算先校验范围，GUI 边界执行 `SetWindowPos`；Shell 与产品工作区查询在 worker 中，UIA 仅用于显式研究 probe。

QML 去除系统标题栏并设置 `Qt.NoDropShadowWindowHint`，透明顶层窗口每侧留 24 DIP；内容面板默认仍为 980×680 DIP、最小 480×480 DIP。Canvas 使用同一圆角路径绘制主题实色表面、1 DIP 单层边框和阴影，并沿该路径裁切侧栏背景，正文保留面板内边距。`visual.json` 的 `menuChrome` 统一提供阴影留白、屏幕间距与边框宽度；磁贴仍使用原有 48/8 DIP 网格。透明角与阴影区域可请求收起，不作为正文按钮命中区。

`DesktopPlatform.configureMenuWindow` 允许 Qt alpha 表面，设置 `DWMWA_WINDOW_CORNER_PREFERENCE = DWMWCP_DONOTROUND`、禁用非客户区渲染、清空扩展 frame，并请求 `DWMWA_COLOR_NONE` 原生边框颜色；不添加系统标题栏、可调整边框、region 或原生 resize 命中区。`DesktopAppearanceResult.nativeDecorationsDisabled` 报告抑制请求结果，失败返回结构化原因。自绘边框、圆角与阴影只由 Qt Quick 产生，实际是否只有一层外观仍须用当前程序的真机截图和操作验收，API 返回不能替代视觉证据。

背景 Mica/Acrylic、模糊材料与显示动画仍待后续研究；外窗为阴影透明不等于启用背景材料。[ADR-0007](adr/0007-custom-menu-chrome.md) 替代 [ADR-0006](adr/0006-menu-popup.md) 的原生外观与旧位置选择，保留原焦点、拖拽和恢复规则。

位置以目标显示器工作区和任务栏为依据，每次唤起重新查询，不持久化屏幕像素坐标。跨屏定位仍需覆盖 Qt 的 DPI 尺寸变化、任务栏自动隐藏及分辨率变化，单屏结果不能替代混合 DPI 验证。

## 应用与恢复

已有应用目录读取当前用户及公共开始菜单，并补充打包应用。`ApplicationPlatform` 使用系统 API 处理快捷方式/AUMID；后台执行与受控启动边界见[架构](architecture.md)。管理员启动仍待专门交互，不使主进程长期提升权限。

快捷栏动作由 `DesktopShellService` 的另一专用线程串行处理。服务与 Windows 平台层均只接受 `documents`、`downloads`、`settings`、`account`：文档与下载使用系统 Known Folders，设置与账户仅使用固定 URI `ms-settings:`、`ms-settings:yourinfo`。界面不能提交任意路径、URI 或 Shell 命令。失败返回结构化原因，重复点击不排队执行多个动作。头像使用圆形占位素材；电源文字菜单只提供退出 XDStarter，系统关机、重启与注销未启用。

托盘使用隐藏的本进程顶层宿主接收 Shell 通知。原生 WndProc 只排队事件，文字菜单与应用状态变化由 Qt 事件循环处理；停止时移除图标、结束原生菜单并失效会话代次，旧通知不影响新的托盘会话。退出清理资源，软件图标从自带 QImage 创建，不读取机器本地图标路径。

`TaskbarCreated` 后重新注册托盘图标的代码已接入；注册失败会通知 host 恢复可达的窗口/任务栏入口。实际重启 Explorer 的恢复测试尚未执行。暂停接管、恢复原生菜单和开机启动项管理仍属后续接管工作；将来发生 Hook 失败时不应自动恢复已判定不安全的接管状态。

## 必须产出的验证记录

原型 Issue 与[测试策略](testing.md)记录 Windows 版本/构建、架构、任务栏设置、DPI、Qt 版本、渲染后端、程序版本、通过/失败用例与恢复步骤。当前弹层需单独验证无标题栏、单层圆角/边框/阴影、左侧与底部可见面板间距相同、可信/失败重复唤起位置一致、点击外部与应用切换收起、内部菜单/Esc、拖拽越界释放、编辑状态保留、托盘恢复及新进程布局恢复。返回的面板定位矩形、透明外窗实际边界、外观 API 返回与实际截图分别记录；若托盘点击需要用户协助，也要注明。真实快捷动作需要有意执行，常规自动测试仅注入假平台，不自动打开用户文件夹或系统设置。

只有记录实际通过的组合进入支持矩阵。只读 UIA probe、模拟平台、强制缩放 GUI 回归、普通窗口历史结果或一次本机 DWM 请求，均不能单独证明完整弹层外观、系统接管、混合 DPI、自动隐藏任务栏或 Explorer 重启兼容。高对比度、远程会话和系统效果关闭等环境需另行验证。

参考：[低级键盘 Hook](https://learn.microsoft.com/en-us/windows/win32/winmsg/lowlevelkeyboardproc)、[DWM 窗口属性](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/nf-dwmapi-dwmsetwindowattribute)。
