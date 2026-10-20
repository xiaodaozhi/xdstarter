# 开发环境与工具链

## 计划基线

| 项目 | 基线 |
| --- | --- |
| UI | Qt 6.12.0，Qt Quick / QML / Qt Quick Controls |
| 语言 | C++20，禁止非必要编译器扩展 |
| Windows 工具链 | MSVC 2022 x64，匹配的 Qt MSVC Kit，Windows SDK |
| 构建 | CMake 3.25+、Ninja；实际版本在工程初始化 PR 固定 |
| 当前辅助脚本 | Python 3.11+，仅标准库 |
| 设计重建 | 可选安装 librsvg 的 rsvg-convert |

Qt 6.12.0 是当前选定基线，升级补丁版本通过依赖 PR 与兼容回归完成。LTS 维护获取方式取决于 Qt 许可渠道，不将全部后续 LTS 补丁可公开获取作为项目假设。

首版模块拟使用 Core、Gui、Qml、Quick、QuickControls2、Svg；测试使用 Test 与 QuickTest。界面通过 QGuiApplication 启动，Windows 托盘采用平台适配，当前不要求 Widgets。

## Windows

安装匹配 MSVC 2022 的 Qt Kit 与 C++ 工具链。Windows 真机承担接管、原生材料、焦点、安装及兼容性验收。macOS 可编辑文档、运行通用检查、开发通用 UI/核心逻辑，不能据此声明 Windows 集成通过。

## 当前可运行内容

仓库尚未包含 Qt 应用的 CMakeLists.txt 或启动程序。现在只运行：

```sh
python3 scripts/check_repository.py
```

可选重建设计资产见[设计目录](../design/README.md)。应用工程创建后由对应 PR 添加真实的配置、构建、测试与运行命令，并扩展 CI。

## 后续工程约定

采用 out-of-source 构建，输出到 `build/`。`CMakeUserPresets.json` 存放个人 Qt 路径并忽略，公开 Presets 不能写入开发者绝对路径。初始工程需提供 Windows 构建和通用核心测试，禁止空 CI 伪装应用构建已通过。

官方依据：[Qt 6.12 发布](https://www.qt.io/blog/qt-6.12-released)、[Windows 支持配置](https://doc.qt.io/qt-6.12/windows.html)、[CMake 要求](https://doc.qt.io/qt-6/cmake-supported-cmake-versions.html)。
