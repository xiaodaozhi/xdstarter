# ADR-0006：可恢复的无边框菜单与延后焦点复核

- 日期：2026-10-20
- 状态：部分被 [ADR-0007](0007-custom-menu-chrome.md) 替代；本文件保留 #15 当时的原生外观决定与验证边界，焦点和可恢复收起约定继续有效
- 关联：[Issue #15](https://github.com/xiaodaozhi/xdstarter/issues/15)、[系统集成研究 #6](https://github.com/xiaodaozhi/xdstarter/issues/6)、[桌面恢复 #14](https://github.com/xiaodaozhi/xdstarter/issues/14)

## 背景

[ADR-0005](0005-desktop-shell-prototype.md) 的普通窗口已接入应用目录、磁贴编辑、桌面快捷动作、托盘恢复与可信开始按钮定位。下一阶段需去除系统标题栏、呈现圆角阴影，并在用户点击外部或切换应用后收起；内部子菜单、拖拽和编辑不能因短时焦点变化而丢失操作。

现有正式环境保持 Qt 6.8.2、v142 与 Windows SDK 10.0.19041.0。窗口外观需要原生合成器参与，但不能为效果修改系统输入行为、引入 Qt Widgets 或提前接管 Win 键和开始按钮。

## 决定

1. 保持 Qt Quick / QML，主窗口使用 `Qt.Window | Qt.FramelessWindowHint`。不采用 `Qt.Popup` 的自动关闭与输入抓取。QML 使用共享菜单圆角 token 绘制主题纯色表面，顶层窗口保持不透明；透明合成和 Mica/Acrylic 不属于本轮实现。
2. Windows 平台仅为本进程当前线程的顶层窗口设置公开 DWM 圆角偏好、非客户区渲染策略与一物理像素 frame 请求。保持 Qt 样式不变，不添加标题栏、resize 边框或命中区，不使用窗口 region 或分层 alpha。结果分别报告圆角与阴影请求是否被接受；请求接受不是跨系统的视觉保证，失败时保留不透明可操作窗口。
3. 由 `DesktopWindowHost` 管理焦点和可恢复收起。每轮显示实际取得本窗口或其 `transientParent` 家族焦点后才启用外部收起；延后 100 ms 复核外部焦点，避免短时交接误关。初次激活被系统拒绝时不隐藏；重新显示或隐藏会停止旧检查，并使旧定位结果失效。
4. 尺寸、固定、电源与关于菜单固定为 `Popup.Item`。拖拽且左键仍按下时延后收起，释放后再复核；托盘原生文字菜单期间暂缓检查。编辑模式保留，但不成为永久阻止收起的条件。真正外部失焦会关闭内部菜单并取消未提交预览，不撤销已提交布局。
5. QML 提供 `prepareForDismissal`、`prepareForPresentation` 与主动收起信号的呈现契约。Host 调用契约清理暂态、避免迟到焦点恢复，并在显示时恢复可操作焦点；通用 host 不依赖具体 QML 控件类。托盘可用时隐藏，无托盘时最小化保留任务栏入口；明确退出仍走既有保存与资源清理流程。

## 影响与验证边界

本轮不改变磁贴几何、布局 schema 或后台 Shell/UIA 线程边界，不安装输入 Hook，不改变原生开始菜单与系统组合键行为。Qt、编译器及 SDK 版本保持正式基线。

自动测试分别检查 QML 无边框/不透明/内部弹层与清理契约、host 的内部/外部焦点和拖拽延后，以及平台 HWND 所有权与 DWM 请求边界。通过注入或 API 返回的测试不能代替 Windows 合成器外观和真实桌面操作。实际窗口、托盘恢复和新进程布局恢复按程序 hash 单独记录于[测试策略](../testing.md)，不沿用上一轮普通窗口证据。

混合 DPI、自动隐藏任务栏、Explorer 重启、远程会话、高对比度及系统效果关闭等组合需另外验证；窗口材料与系统输入接管继续由 #6 跟踪。实现边界见 [Windows 集成](../windows-integration.md)，用户行为见[交互设计](../../product/interaction.md)。
