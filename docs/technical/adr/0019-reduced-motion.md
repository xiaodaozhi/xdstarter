# ADR-0019：遵循 Windows 动画偏好

日期：2026-10-29。状态：已接受（阶段实现）。关联 [#34](https://github.com/xiaodaozhi/xdstarter/issues/34)。

## 决定

- Windows 平台以 `SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION)` 只读用户的客户端区域动画偏好；启动和窗口再次显示时读取，并在本进程收到 `WM_SETTINGCHANGE` 后合并通知、排队重读。显示时重读也覆盖首次显示前未创建 Qt 原生窗口、可能错过通知的情况。不设置系统参数、不广播系统消息。读取失败时回退为减少动画，后续通知或显示可重新尝试。
- 平台对象将只读状态注入 controller 和 QML，不进入布局 schema 或产生保存修订；不在 QML 和领域层直接使用 Win32。非 Windows 测试适配器可提供进程内状态。
- 禁用时窗口呈现/收起、目录切换、菜单、磁贴重排/出现及按压透视、字母索引、反馈与拖放预览不启动新的过渡；磁贴按下仍以静态颜色表达。进行中的磁贴和视图动效收敛到目标状态，已开始的菜单进入/退出过渡允许完成原有有界时长，后续菜单不再使用过渡。
- 目标状态更新不改变共享 48 DIP 小格与 8 DIP 间隙、布局身份或恢复入口。切换后继续支持菜单、目录、键盘、错误提示、拖放和安全退出。

## 验证边界

平台测试通过可替换读取函数和发送给本进程隐藏 Qt 窗口的单条 `WM_SETTINGCHANGE` 验证读取、去重、失败回退与通知路径；不广播消息或修改系统设置。离屏 GUI 测试覆盖初始和运行中切换、过渡目标状态、按压、目录导航、菜单、几何与不写盘。模拟不能证明真实系统设置切换及所有 Windows 配置，也不代替高对比度、屏幕阅读器或实际辅助功能验收。

依据：[Microsoft SystemParametersInfo](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-systemparametersinfow)、[WM_SETTINGCHANGE](https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-settingchange)、[测试策略](../testing.md)。
