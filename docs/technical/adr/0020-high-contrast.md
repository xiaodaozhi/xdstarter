# ADR-0020：Windows 高对比度系统色

日期：2026-10-29。状态：阶段实现。关联 [#83](https://github.com/xiaodaozhi/xdstarter/issues/83) 和 [#34](https://github.com/xiaodaozhi/xdstarter/issues/34)。

## 决定

- Windows 平台只读 `SPI_GETHIGHCONTRAST` 和 `GetSysColor` 的窗口／文字、选中／选中文字、禁用文字；不设置系统偏好或广播消息。启动和窗口显示时读取，收到 `WM_SETTINGCHANGE`、`WM_SYSCOLORCHANGE` 或 `WM_THEMECHANGED` 后合并并排队重读。数据不完整或读取失败时保留上一份有效状态；启动时尚无有效状态则沿用普通主题，后续通知及显示时重试。
- 高对比度启用时在呈现层优先使用系统色；原有跟随系统／浅色／深色选择仍可修改且不因高对比度切换而重写，关闭高对比度后恢复该选择。此状态不进入布局 schema、不产生保存修订。平台接口由 controller 注入 QML，领域层与 QML 不调用 Win32。
- 面板及文字使用系统窗口／窗口文字配对，输入选区使用系统选中／选中文字配对。普通控件的悬停、按下、键盘焦点与选中通过边框表达，避免将窗口文字置于系统选中背景上。菜单、提示、对话框及禁用状态使用相应系统色；错误和不可用状态保留文字或符号。阴影和模态半透明遮罩不用于高对比度呈现。
- 磁贴使用系统窗口色及文字色，保留原尺寸、可访问全名和提示；应用目录、搜索结果及磁贴中的多色图像以名称首个 Unicode 码点的文本替代，快捷栏使用短文字及完整辅助名称。系统色由用户选择，不以固定黑白或某一色值推断高对比度。

## 验证边界

原生测试注入读取结果，只向本进程隐藏窗口发送通知；GUI 测试注入两组自定义色，检查切换、恢复、焦点、菜单、提示、搜索、拖放、保存和几何。离屏图与属性断言不能代替真实 Windows 11 高对比主题切换、屏幕阅读器、DPI/显示矩阵或所有应用图标／名称的视觉验收；这些仍在 #34 跟踪。读取初次失败时无法判断真实系统偏好，也不宣称已覆盖所有状态。

依据：[Microsoft 高对比参数](https://learn.microsoft.com/en-us/windows/win32/winauto/high-contrast-parameter)、[高对比模式](https://learn.microsoft.com/windows/win32/w8cookbook/high-contrast-mode)、[测试策略](../testing.md)。
