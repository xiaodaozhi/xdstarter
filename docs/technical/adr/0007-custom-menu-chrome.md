# ADR-0007：自绘菜单外观与统一工作区边距

- 日期：2026-10-20
- 状态：已采用；实际验证范围见本轮测试记录
- 关联：[Issue #16](https://github.com/xiaodaozhi/xdstarter/issues/16)、[菜单弹层 #15](https://github.com/xiaodaozhi/xdstarter/issues/15)、[系统集成研究 #6](https://github.com/xiaodaozhi/xdstarter/issues/6)
- 替代：[ADR-0006](0006-menu-popup.md) 的原生 DWM 外观选择，以及此前可信开始按钮与回退位置分别定位的规则；保留其焦点与可恢复收起约定

## 背景

用户在实际托盘唤起中观察到窗口贴屏与留边交替出现，并发现两层圆角。原定位将可信 `StartButton.left` 与查询失败时的 `workArea.left + gap` 分别作为横向锚点；在同一显示器上，UIA 信任状态变化即可改变位置。先显示再等待后台查询，也使定位变化直接可见。原型同时绘制 QML 圆角边框并请求 DWM 圆角、非客户区与 frame，形成两套外观来源。

本轮用户明确要求可见面板的左边距与底部任务栏间距相同，并完全自行绘制边框、圆角和阴影。继续使用 Qt Quick / QML、Qt 6.8.2 和正式工具链，不改变磁贴网格、持久化格式或系统输入接管范围。

## 决定

1. 主窗口保持 `Qt.Window | Qt.FramelessWindowHint | Qt.NoDropShadowWindowHint`，顶层颜色透明。QML 通过同一套圆角路径绘制主题实色面板、单层边框与阴影，并沿同一路径裁切侧栏背景，避免矩形背景覆盖圆角；正文保留面板内边距。面板圆角继续来自 `cornerRadiusDip.menu = 12`；`visual.json` 新增 `menuChrome`，统一阴影留白 24 DIP、屏幕间距 8 DIP、边框宽 1 DIP。
2. 阴影由 QML Canvas 绘制，不依赖 Windows 原生阴影。透明外窗每边增加 24 DIP，默认内容面板仍为 980×680 DIP，最小内容区仍为 480×480 DIP。外窗矩形、阴影留白与可见面板矩形分别计算；阴影不算作屏幕边距，不缩减磁贴共享几何。透明圆角和阴影区域不作为可操作正文，点击可请求收起。
3. Windows 平台校验当前进程、当前 GUI 线程的顶层 HWND 后，设置 DWM `DONOTROUND`、禁用非客户区渲染、清空扩展 frame，并请求无原生边框颜色。允许 Qt 的透明表面，不再拒绝带 alpha 的窗口。该设置仅抑制系统装饰，不添加原生 resize 命中区、不安装输入 Hook；结构化结果报告是否成功禁用，API 接受结果与实际视觉仍分别验收。
4. 可见面板统一定位为目标工作区左侧和底部均留 8 DIP，产品唤起不再识别或等待开始按钮。Windows 的 `queryMenuAnchor()` 仅读取指针所在显示器名称、物理矩形、工作区和任务栏 DPI，不初始化 COM、不调用 UIA；原 `queryStartButtonAnchor()` 仅保留为显式系统集成研究入口。产品不以结果中保留兼容用途的 `trusted` 字段决定位置。使用选定 Qt 窗口的实际渲染 DPR 将间距和阴影留白统一转换成物理像素，任务栏 DPI 仅用于诊断；可见面板夹限工作区，空间不足时以可达性优先。透明外窗可为阴影越出工作区，返回的 `DesktopPlacementResult.physicalRect` 专指可见面板，不读取 DWM extended-frame 边界参与布局。
5. 产品入口加载 QML 时将初始 `visible` 设为 `false`，smoke 与独立 GUI 夹具仍自行显示。`DesktopWindowHost` 在隐藏阶段请求 `DesktopShellService` worker 调用 `queryMenuAnchor()`，收到仍有效的结果后先配置原生装饰抑制、完成定位，再显示与请求激活；避免为预定位等待旧 UIA 配置的 1500 ms 连接／事务超时，也不将异步定位作为已显示窗口的可见修正。连续切换、主动收起或关闭使待显示请求失效，迟到结果不能重开窗口。服务的 `requestStartButtonAnchor()`、`startButtonAnchorReady` 名称暂保留兼容，产品实现已是工作区查询。Shell 动作仍在 worker，GUI 仅执行短时本进程窗口操作。

## 影响与验证边界

### 隐藏窗口跨 DPI 屏幕的准备阶段

2026-10-21，分组管理验收发现 [Issue #28](https://github.com/xiaodaozhi/xdstarter/issues/28)：隐藏的 96 DPI 窗口首次从 144 DPI 屏幕托盘恢复时，`setScreen()` 后仍读取到旧的窗口 DPR／物理尺寸，显示后才转换，导致底部越过任务栏；第二次唤回又正常。它不在此前同屏十二轮 probe 的验证范围内。

Qt 的 [QWindow::setScreen 文档](https://doc.qt.io/qt-6.8/qwindow.html#setScreen)说明，同一虚拟桌面内设置屏幕不会自动移动已有窗口。唤起现在先在隐藏阶段选择并预移至目标屏幕，使原生窗口完成 DPI 转换；之后跨事件循环采样，要求实际窗口 DPR 与目标 QScreen DPR 一致，且本进程 GUI 线程顶层窗口的物理尺寸等于 Qt 逻辑尺寸乘实际 DPR，连续两次稳定才执行最终定位和显示。Windows 短时观测经平台接口返回普通尺寸数据，QML 和领域模型不依赖 HWND 或 Win32。

屏幕选择也不能比较 `QScreen::name()` 与 GDI 设备名称：本机 Qt 返回面板型号，GDI 返回 `DISPLAY1` 一类名称。`DesktopPlatform::screenForAnchor()` 在 GUI 线程经 Windows 适配器使用 Qt 6.8.2 的公开 `QWindowsScreen::handle()`，精确核对原生显示器、设备名及物理矩形，要求唯一匹配后返回 QScreen；不按名称前缀、面板型号或持久化的句柄猜测。已知原生目标不存在时不回用旧屏幕，worker 不访问 QScreen。

准备阶段每次唤起独立，最多等待 1500 ms；取消、重复请求、关闭和退出使旧准备失效，同步状态发布中的重入也复核当前请求。超时不显示错误几何：有托盘时保持隐藏供重试，无托盘时保留最小化恢复入口。任务栏 DPI 仍只供诊断，不代替 Qt 实际渲染 DPR；已显示窗口不执行补偿跳动。

显式 `--native-screen-transition-probe` 使用真实混合 DPI 显示器及模拟托盘信号，在首次可见和 1 秒后分别独立测量原生物理外窗矩形、目标屏幕及 DPR，检查 980×680 DIP 正文、24 DIP 阴影留白和左／底 8 DIP。两端矩形相等不表示逐帧监测了中间全部位置；真实通知区域点击仍另行验收。该 probe 不改变系统缩放、移动指针或读取用户布局，缺少适用的不同 DPI 双屏时报告条件不足，不能算通过。

[ADR-0006](0006-menu-popup.md) 的 100 ms 外部焦点复核、内部 `Popup.Item`、拖拽按下期间延后、编辑状态保留、托盘恢复和无托盘最小化规则继续有效。此次透明只服务于外侧阴影和圆角，面板仍为可读的主题实色，不等于启用 Mica/Acrylic 或背景模糊材料。

v2 修订 2.1 设计稿的菜单与开始按钮同左侧坐标属于此前设计约定；不将其固定画布位置用于本轮运行时边距验收。既有图标、头像、磁贴与 ICO 素材不变；新的窗口边距与自绘效果以 tokens、代码和对应版本的实际截图为准。

自动检查覆盖可信/回退结果几何一致、透明外窗与面板坐标转换、边距/DPI/极值、待显示请求取消、过期结果、原生装饰抑制边界、侧栏圆角裁切和阴影像素。真实托盘连续唤起、实际单层圆角阴影、外部收起、内部菜单、拖拽和重启布局恢复另行记录于[测试策略](../testing.md)，不沿用 #15 的程序 hash 或用户确认结论。混合 DPI、自动隐藏任务栏、Explorer 重启、远程会话与高对比度等组合仍需单独验证。
