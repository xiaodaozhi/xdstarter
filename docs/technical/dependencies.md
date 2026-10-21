# 工程依赖记录

[toolchain.json](../../toolchain.json) 是 XDStarter 自己的正式构建版本声明，[获取与检查脚本](../../scripts/toolchain.py)、[Windows 入口](../../scripts/build-windows.ps1)、CMake 和应用 CI 读取同一清单。默认缓存属于本项目，外部 Qt SDK 仅通过参数复用下载缓存；构建不要求 xdzip 仓库存在。

## 与 xdzip 的对齐依据

2026-10-20 只读核对 [xdzip 的 78472b3 提交](https://github.com/xiaodaozhi/xdzip/tree/78472b345561ebc8b687e419d9168e96d29fed07)：

- `docs/engineering/toolchain.md`、`scripts/ci-desktop.ps1`：Qt 固定 6.8.2，aqtinstall 3.3.0，Windows 获取 `win64_msvc2022_64` Kit。
- 本机 `build/tooling/pyvenv.cfg`、Python 包元数据及工具可执行文件：Python 3.12.13、CMake 4.4.4、Ninja Python 分发 1.13.2、py7zr 1.1.4。
- 本机隔离源码 `build/source-78472b3/build/windows/` 的 CMakeCache 与 CMakeCXXCompiler 记录：MSVC 19.29.30153.0、linker 14.29.30153.0，v142 目录 14.29.30133，Windows SDK 10.0.19041.0，x64。
- Qt SDK `mkspecs/qconfig.pri`：Qt 6.8.2、x86_64，SDK 自身构建编译器 MSVC 19.39.33520；分发 Kit 名不代表产品实际编译器。

xdzip 根 CMake 最低要求 3.24，其 CI 的 CMake/Ninja/Python 依赖 runner，并没有固定上述全部补丁版本。XDStarter 保留已有 CMake 3.25 最低语法要求及 C++20，将这次核实的本机工具版本正式独立固定，不引入 xdzip 的 Rust、压缩库或安装器工具。这是两项目所需共同工具的对齐，不是复制 xdzip 完整依赖。

## 固定版本、来源与用途

| 依赖 | 正式版本与来源 | 用途与许可记录 |
| --- | --- | --- |
| Qt | [6.8.2 官方归档](https://download.qt.io/archive/qt/6.8/6.8.2/) | Core、Gui、Qml、Quick、QuickControls2、Svg；测试另用 Test，产品不链接 Test。采用动态库方向，存在开源/商业许可路径，实际模块与部署材料按[官方许可](https://doc.qt.io/qt-6.8/licensing.html)审查 |
| MSVC / linker | VS 2019 16.11、v142 14.29.30133；编译器 19.29.30153.0、linker 14.29.30153.0；Microsoft Visual Studio Installer | Windows x64 构建工具，不捆绑 IDE/工具到应用；VC 运行时的发行许可及部署仍需单独核对 |
| Windows SDK | Microsoft Windows SDK 10.0.19041.0 | Windows 头文件、资源编译器及工具；仅构建期使用，不将整个 SDK 复制入产品 |
| Python | [3.12.13](https://www.python.org/downloads/release/python-31213/) | 构建工具环境；PSF 许可。可使用兼容分发提供的该解释器，项目入口核对实际版本；普通仓库脚本仍支持 Python 3.11+ |
| uv（Windows CI 获取工具） | [0.13.4](https://github.com/astral-sh/uv/releases/tag/0.13.4)，由 `ciAcquisition.uvVersion` 声明 | 仅用于获取并定位精确的 CPython 3.12.13，不进入产品；Python 来自 Astral 的 [python-build-standalone](https://github.com/astral-sh/python-build-standalone)。uv 与解释器的许可材料见各自官方仓库 |
| CMake | [4.4.4 Python 分发](https://pypi.org/project/cmake/4.4.4/) | 配置与构建；CMake 为 BSD 3-Clause，Python 分发封装为 Apache 2.0，随附第三方许可保留 |
| Ninja | [1.13.2 Python 分发](https://pypi.org/project/ninja/1.13.2/) | 构建后端，Apache 2.0；本机可执行文件版本带 `.git.kitware.jobserver-pipe-1` 后缀，记录完整输出，不把后缀当作另一个 PyPI 包版本 |
| aqtinstall | [3.3.0](https://pypi.org/project/aqtinstall/3.3.0/) | 获取 Qt，MIT；不改变 Qt 组件许可，不进入产品运行时 |
| py7zr | [1.1.4](https://pypi.org/project/py7zr/1.1.4/) | Qt 下载包解压，LGPL-2.1-or-later；仅开发工具 |

Python 包通过 PyPI 精确版本取得，项目工具保存在忽略的 `build/tooling/`；默认 Qt 缓存在 `build/Qt/`。版本检查同时核对包元数据、CMake/Ninja 可执行文件及 qmake 返回的 Qt 版本；本地结果保存在 `build/<preset>/toolchain-validation.json`。脚本不把所有传递依赖逐一固定，也未增加完整下载哈希锁，因此不能称作完整离线/位级可复现环境。首次获取需要网络，已安装相同工具和 SDK 时无需重复下载。

应用 CI 固定 actions/checkout、actions/setup-python 与 Windows 使用的 astral-sh/setup-uv 提交；Python 补丁与工具版本由清单读取。Python 3.12.13 为仅源码安全版本，官方不提供该版 Windows 安装器；Windows CI 通过固定版本 uv 获取与本机相同渠道的 CPython，显式定位解释器后传入现有构建入口，严格版本检查保持不变。Linux 核心任务继续使用 setup-python。`windows-2022` runner 的 MSVC 2022/SDK 随镜像变化，仅作为显式附加兼容验证，不冒充上述 v142/19041 精确基线；实际日志记录其选择结果。Ubuntu 核心检查使用 runner 的 C++20 编译器，不提供 Qt/Windows 验收结论。Actions 恢复和检查证据见 [#12](https://github.com/xiaodaozhi/xdstarter/issues/12) 与 [PR #17](https://github.com/xiaodaozhi/xdstarter/pull/17)。

## 支持与升级边界

Qt 官方 6.8 Windows 支持配置列出 MSVC 2022；本项目按所有者要求正式采用与 xdzip 相同的 MSVC 2019 实测环境，同时保留其超出上游普遍兼容保证的边界，见 [ADR-0002](adr/0002-toolchain-baseline.md)。项目自己的正式基线并不改变 Qt/Microsoft 的支持政策。此前使用旧 CMake/Ninja 的结果不能证明这份新工具清单已经完成构建，最新证据以 Issues 和实际运行记录为准。

固定版本不是永久冻结：补丁、安全更新或工具链变更通过依赖 PR 更新清单、文档及适用回归；Qt 升级至少覆盖布局、焦点、输入与部署。当前没有第三方二进制入库或应用安装包，不将工具缓存捆绑入产品。

应用、文档及原创资产的许可仍由 [#3](https://github.com/xiaodaozhi/xdstarter/issues/3) 决定；本表不替代[发行许可审查](../governance/licensing.md)，也不为项目添加授权。尚未引入拼音库、运行时应用图标集合或字体包。
