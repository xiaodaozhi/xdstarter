# ADR-0002：正式采用与 xdzip 一致的构建工具基线

日期：2026-10-20。状态：已接受；根据项目所有者要求，将此前仅参考 Qt 版本、临时使用 SDK 缓存的安排更新为本项目自己的固定工具声明。本决定调整 [ADR-0001](0001-qt-quick.md) 的版本条款。

## 背景

初始工程写入 Qt 6.12 与 MSVC 2022，原型随后参考 xdzip 改用 Qt 6.8.2，并以已有 SDK 路径开展本机实验。项目所有者要求正式对齐 xdzip 所用 Qt 等构建工具的版本，不能只依赖另一个项目的本机安装路径。

只读核对 xdzip 的 78472b345561ebc8b687e419d9168e96d29fed07 版本及实际 Windows 构建环境后，区分出仓库声明、当前机器的工具补丁版本、SDK 自身构建版本与 CI runner 工具。具体来源见[依赖记录](../dependencies.md)。xdzip 未对所有工具提供完整发行锁，本项目不扩大这一证据。

## 决定

- 本项目以 [toolchain.json](../../../toolchain.json) 独立声明 Qt 6.8.2、Python 3.12.13、CMake 4.4.4、Ninja Python 分发 1.13.2、aqtinstall 3.3.0、py7zr 1.1.4。获取、检查、CMake 应用配置和应用 CI 读取同一来源。
- 正式 Windows 本机构建基线采用 xdzip 已核实的 x64 VS 2019 16.11/v142：工具目录 14.29.30133、MSVC 19.29.30153.0、linker 14.29.30153.0、Windows SDK 10.0.19041.0。入口自动选择该环境，缺失或不匹配明确失败，不静默切换到 VS 2022。
- Qt 使用官方 `win64_msvc2022_64` Kit；CMake 精确匹配 6.8.2，Qt CMake 策略要求仍为 6.8。Kit 名称表示分发包，其自身由 MSVC 19.39.33520 构建，不表示产品正在调用 MSVC 2022。
- 默认工具环境和 SDK 缓存为本项目忽略的 `build/tooling/`、`build/Qt/`，提供正式下载与构建入口。允许 `XDSTARTER_QT_ROOT` 或 `-QtPrefix` 指向已有相同 SDK，只改变缓存位置，不改变依赖版本，也不写机器路径到公开配置。
- 保留 MSVC 2022 CI 作为显式附加兼容验证，结果与精确 v142 基线分别记录。使用 `-Toolchain MSVC2022` 或手工 `XDSTARTER_ALLOW_ALTERNATE_MSVC=ON` 才绕过基线编译器/SDK 版本约束；Qt 精确版本不改变。
- 2026-10-21 的首次实际 Windows CI 证明 setup-python 无法获取仅源码安全版 3.12.13。Windows CI 改由清单固定版本的 uv 获取并定位该 CPython，沿用本机已核实的分发渠道和严格版本校验；此补充不改变正式依赖版本，具体 action 与获取工具见依赖记录。
- 保留 Qt Quick / QML + C++20、模块边界、最低 CMake 3.25，以及普通仓库脚本的 Python 3.11+ 兼容范围；不引入 xdzip 独有的 Rust、格式后端或安装器依赖。
- 升级通过依赖 PR 更新清单、许可记录和兼容回归，不永久冻结这些版本。工具安装器仍解析传递依赖，当前不是逐传递依赖与哈希的完整环境锁。

## 支持边界与影响

Qt 6.8 官方 Windows 支持表列出 MSVC 2022。Microsoft 的二进制兼容说明要求 linker 不早于所链接输入中最新的工具集；较旧 MSVC 2019 linker 使用由较新 MSVC 构建的 Qt 库，超出了这一普遍保证。项目根据明确要求正式选择与 xdzip 一致的实测环境，不将其称为 Qt 官方支持组合或免验收的发行保证，并保留 MSVC 2022 的附加验证。

新工具清单必须实际构建后才能记录通过；此前 CMake 3.31.10/Ninja 1.13.0/Python 3.11 的原型证据保留其原有范围。构建、QML 检查、核心/服务/GUI 测试、实际窗口及 DPI 检查分别记录，不用 offscreen 成功代替原生窗口或系统开始菜单接管验收。

## 依据

- [xdzip 固定参照版本](https://github.com/xiaodaozhi/xdzip/tree/78472b345561ebc8b687e419d9168e96d29fed07)：Qt 声明及 Windows 实际工具核对入口。
- [Qt 6.8 Windows 支持配置](https://doc.qt.io/qt-6.8/windows.html)、[Qt 6.8.2 官方归档](https://download.qt.io/archive/qt/6.8/6.8.2/)：SDK 来源与官方支持边界。
- [MSVC 二进制兼容性](https://learn.microsoft.com/en-us/cpp/porting/binary-compat-2015-2017?view=msvc-170)：新旧工具集及 linker 限制。
- [CMake 4.4.4](https://pypi.org/project/cmake/4.4.4/)、[Ninja 1.13.2](https://pypi.org/project/ninja/1.13.2/)、[Python 3.12.13](https://www.python.org/downloads/release/python-31213/)：工具版本来源。
