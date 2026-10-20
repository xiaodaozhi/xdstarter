# ADR-0001：采用 Qt Quick / QML + C++

日期：2026-10-20。状态：已接受，依据项目所有者明确选择。

## 背景

XDStarter 需要支持多跨度磁贴、拖拽与动画、深浅主题、Windows 11 菜单浮层，以及原生 Win 键与开始按钮集成。已有精确 SVG 设计与网格数据，没有历史应用代码。

## 决定

- UI 使用 Qt Quick / QML，业务与平台适配使用 C++20，构建采用 CMake。
- 初始 Qt 基线为 6.12.0，Windows 首选 MSVC 2022 x64。
- 普通控件可采用 Qt Quick Controls 的 FluentWinUI3 风格，磁贴由自定义组件绘制。
- 通用布局/模型不依赖 Win32，Windows 系统集成单独封装。
- 首版支持目标仍为 Windows 11 x64；跨平台开发便利不等同于跨平台产品承诺。

## 取舍

Qt Quick 适合自定义图形、状态与动画，也允许在 macOS 开发通用界面。Qt 提供 FluentWinUI3 样式，但它是 Qt 自身绘制的外观，部分控件存在回退，需要自行验证视觉一致性。

与 WinUI 3 相比，Windows 原生材料、托盘和系统接管需要更多平台适配，发行时还需管理 Qt 动态库与许可。系统接管本身对两种 UI 方案都是独立工程问题，不能由 UI 框架自动解决。

## 影响与后续验证

M1 先完成窗口、网格与 Windows 集成原型，确认材料/焦点/输入兼容后再扩展完整功能。性能使用实际 Release 原型衡量。未来变更本决定需新 ADR，并说明迁移成本及对现有 Issues 的影响。

依据：[Qt Quick](https://doc.qt.io/qt-6/qtquick-index.html)、[FluentWinUI3 样式](https://doc.qt.io/qt-6/qtquickcontrols-fluentwinui3.html)、[Qt 6.12 发布](https://www.qt.io/blog/qt-6.12-released)。
