# 开发环境与工具链

## 工程基线

本项目按所有者要求，正式采用与 xdzip 已核实的 Windows 本机构建相同的工具版本。[toolchain.json](../../toolchain.json) 是本项目自己的版本声明，获取、检查、构建和应用 CI 均读取它；不依赖 xdzip 仓库存在，也不从另一个仓库动态继承版本。

| 项目 | 正式基线 |
| --- | --- |
| UI | Qt 6.8.2，Windows `win64_msvc2022_64` x64 Kit；CMake 精确匹配版本 |
| 语言 | C++20，禁止非必要编译器扩展 |
| Windows 编译器 | Visual Studio 2019 16.11 / v142；工具目录 14.29.30133，MSVC 19.29.30153.0，linker 14.29.30153.0，x64 |
| Windows SDK | 10.0.19041.0 |
| 构建工具 | CMake 4.4.4、Ninja Python 分发 1.13.2 |
| 工具 Python | Python 3.12.13；普通仓库检查可使用 Python 3.11+ |
| Qt 获取 | aqtinstall 3.3.0、py7zr 1.1.4 |
| 附加验证 | Windows CI 的 MSVC 2022，明确区别于 v142 正式基线 |
| 设计重建 | 项目 Qt 6.8.2 的离屏 SVG 工具；`-DesignAssets` 构建并导出，可选使用已有 rsvg-convert |

参照来源包括 xdzip 的 Qt 声明、实际工具环境、构建缓存和 SDK 元数据，详见[工程依赖](dependencies.md)与 [ADR-0002](adr/0002-toolchain-baseline.md)。xdzip 的 CI 没有固定 CMake/Ninja/Python 补丁，本项目将本次实测版本独立固定，不能把这一清单称作 xdzip 的完整发行锁。

Qt Kit 名称中的 `msvc2022_64` 是 SDK 分发名称，不等于实际调用的本机编译器。该 SDK 自身由 MSVC 19.39.33520 构建；本项目所选 MSVC 19.29/linker 更旧。Qt 官方 6.8 Windows 支持表列出 MSVC 2022，Microsoft 的普遍二进制兼容保证要求 linker 不早于最新输入工具集。本项目正式选择与 xdzip 相同的实测构建环境，但这一选择不扩大 Qt 官方支持或 Microsoft 的保证；发行前继续进行适用验证，并保留 MSVC 2022 的附加兼容检查。

当前应用配置使用 Core、Gui、Qml、Quick、QuickControls2、Svg，通过 QGuiApplication 启动，不引入 Widgets。开启 `BUILD_TESTING` 时 GUI 测试另使用 Qt Test；产品不链接该模块。Windows 真机承担接管、焦点、安装及兼容性验收；通用构建不能证明这些能力通过。

## 仓库检查

使用 Python 3.11+；Windows 的 `python` 或 `python3` 若指向旧解释器/商店别名，应改用实际安装的 Python 3.11+ 可执行文件，不修改脚本适配 Python 2。

```sh
python3 scripts/check_repository.py
python3 -m unittest discover -s tests/tooling -v
```

可选重建设计资产见[设计目录](../design/README.md)。`./scripts/build-windows.ps1 -Preset app-release -DesignAssets` 在正式构建后编译 `xdstarter_render_svg`，重建深浅设计稿和开发素材并校验；仅运行离屏导出，不打开桌面窗口。普通仓库检查不编译或运行 Qt，素材校验只用 Python 标准库。上述仓库检查命令不包含应用构建。

## 无 Qt 的核心构建

安装 CMake、Ninja 与 C++20 编译器；Windows 在相应 x64 Developer PowerShell 中执行：

```sh
cmake --preset core-debug
cmake --build --preset core-debug
ctest --preset core-debug
```

将 preset 改成 `core-release` 可验证优化构建。核心预设设置 `XDSTARTER_BUILD_APP=OFF`，不会查找 Qt，也不能用来证明 Qt 或 Windows 系统集成通过。

## Windows 应用获取、构建与运行

先安装清单中的 Python 3.12.13、VS 2019 C++ x64/v142 工具集与 Windows SDK 10.0.19041.0。在 PowerShell 中执行以下正式入口，`-Python` 指向所装解释器或使用已正确配置的 `python`：

Python 3.12.13 是[仅源码的安全版本](https://www.python.org/downloads/release/python-31213/)，没有官方 Windows 安装器。本机使用 uv 管理的兼容分发；Windows CI 也通过清单固定的 uv 获取精确版本，以 `uv python find --managed-python 3.12.13` 返回的解释器路径传给 `-Python`。这只调整获取渠道，不降低 Python 版本，也不改变 Qt、MSVC 或 SDK 基线，来源见[依赖记录](dependencies.md)。

```powershell
./scripts/build-windows.ps1 -InstallTools -Python python -Preset app-release -Fresh
```

入口选择 VS 2019 与精确 toolset/SDK；缺失或版本不符直接报错，不自动换成 VS 2022。首次运行在本项目 `build/tooling/` 创建独立 Python 环境，安装清单固定版本，并通过 Qt 官方分发创建本项目 `build/Qt/6.8.2/msvc2022_64/` 缓存。Python 工具的传递依赖由 pip 解析，尚不是逐传递依赖/哈希的完整环境锁。Qt 获取日志保留在 `build/qt-install/`。随后配置、编译、qmllint 和 CTest；任何非零退出均停止。后续构建可省略 `-InstallTools`，调试用 `-Preset app-debug`。成功后实际工具记录在 `build/app-release/toolchain-validation.json`。

已有相同 Qt SDK 可作为外部只读缓存；路径只通过参数或环境变量传入，不写入公开配置。这仅省去下载，依赖版本仍由本项目清单决定：

```powershell
./scripts/build-windows.ps1 -InstallTools -Python python -QtPrefix $env:XDSTARTER_QT_ROOT
# 只检查本项目工具与现有 Qt 缓存，不编译。
./build/tooling/Scripts/python.exe scripts/toolchain.py check --qt-prefix $env:XDSTARTER_QT_ROOT
$env:PATH = "$env:XDSTARTER_QT_ROOT/bin;$env:PATH"
& ./build/app-release/bin/xdstarter.exe
```

使用默认项目 Qt 缓存运行时，将上例 PATH 中的根目录改为 `./build/Qt/6.8.2/msvc2022_64`。应用输出在 `build/<preset>/bin/`；没有部署或安装包，不能把构建目录当作独立发行目录。

`./scripts/build-windows.ps1 -Toolchain MSVC2022 -InstallTools -Python python` 是显式选择的附加兼容验证，CI 使用此入口。它不满足 v142 的精确版本要求，不得将其结果标为基线实测。手工 CMake 应用配置也默认检查 MSVC/SDK 基线；仅附加验证可显式设置 `XDSTARTER_ALLOW_ALTERNATE_MSVC=ON`。Qt 仍精确要求 6.8.2，升级需修改清单并按依赖 PR 验证。

此前 Windows 11 10.0.26300 / Intel Iris Xe / Direct3D 11 普通窗口、三档磁贴交互和缩放检查使用 Qt 6.8.2、MSVC 19.29.30153，但旧辅助工具为 CMake 3.31.10、Ninja 1.13.0、Python 3.11。那一轮证据只属于当时环境，不能代替新清单完整构建结果；最新实际结果以关联 Issues 和新生成的工具记录为准。

当前原型普通启动使用真实应用目录，支持图标、受控启动、固定/取消固定，以及目录与固定目标的离线名称、英文、拼音全拼和首字母搜索；输入框支持 Ctrl+F、方向键和 Enter，查询由专用后台 worker 处理，详见 [ADR-0010](adr/0010-local-application-search.md)。三档磁贴从共享几何获得外框，小磁贴仅显示图标，ToolTip 和辅助功能保留完整名称，长名称在磁贴内省略。缩小窗口时布局不重排，Tab/Shift+Tab 或方向键聚焦会自动双向滚动到目标。主题与布局可保存恢复；编辑模式支持拖拽/跨组移动、Ctrl+方向键移动和三档尺寸切换。普通启动使用当前用户布局目录，`--layout-file` 可隔离验证，查询不写入布局。

`--smoke-test` 和 GUI 样例夹具使用三组共 15 枚固定样例。smoke 完全禁用布局读写，不连接真实目录、桌面动作、托盘、定位服务或 Win 键接管；样例名称可使用同一离线匹配逻辑搜索，但不能启动真实应用，也不代表已安装应用。编辑模式已接入分组新建、重命名、排序与空组删除，磁贴显示名称编辑和原生背景材料仍待后续开发；样例、模拟平台与真实 Windows 行为分别记录。

启用应用与测试的预设当前注册十二套 CTest：`app.qml_smoke`、`core.tile_grid`、`services.prototype`、`services.layout_store`、`services.application_catalog`、`services.desktop_shell`、`services.search`、`platform.applications`、`platform.desktop`、`platform.win-key-capture`、`ui.prototype`、`ui.desktop_window_host`。注册不代表已运行通过；确切版本结果见[测试策略](testing.md)。`--smoke-test` 加载 QML/视觉资源并短暂运行后退出；`ui.prototype` 使用同一产品 QML 和 controller 检查交互与抓帧，本轮增加四个搜索用例，覆盖焦点与输入法、浏览恢复、键盘启动及目录／别名／重启状态。CTest 默认将 GUI 检查放在 offscreen/software 环境，该结果不代替原生 Windows 窗口和系统集成验收。

## Windows 窗口与缩放截图

先完成 `app-release` 构建，再在已设置 `XDSTARTER_QT_ROOT` 的 PowerShell 中运行：

```powershell
# 默认：离屏交互检查，保存深浅主题下三组的截图与像素记录。
./scripts/verify-windows-ui.ps1 -QtPrefix $env:XDSTARTER_QT_ROOT

# 原生 Windows QPA：在当前显示器上检查五档 Qt 有效缩放。
./scripts/verify-windows-ui.ps1 -QtPrefix $env:XDSTARTER_QT_ROOT -NativeScales
```

Debug 构建使用 `-Preset app-debug`。脚本复用已构建的 `xdstarter_ui_tests.exe`，不安装 Qt、不修改 Windows 显示设置；执行结束恢复它临时改变的进程环境变量。默认工件目录为 `build/validation/ui/offscreen/`。`-NativeScales` 清除强制 software 后端，使用 Windows QPA；先探测当前屏幕基础 DPR，再以 `目标 DPR / 基础 DPR` 设置 `QT_SCALE_FACTOR`，核对实际窗口 DPR 达到 1、1.25、1.5、1.75、2。本机探测的基础 DPR 为 1.5，图形后端为 Direct3D 11，不能假设其他机器相同。

原生工件输出到 `build/validation/ui/native-{100,125,150,175,200}/`，汇总为 `native-matrix.json`。每档包含深浅主题 × 三组，共六组抓帧记录；每组保存完整窗口、网格裁剪 PNG 和 JSON。检查从窗口图像扫描磁贴物理边界与间隙，并核对四小磁贴外框等于标准磁贴、两标准磁贴及间隙等于宽磁贴，允许至多 1 物理像素的光栅化误差。记录同时保留实际 DPR、图形 API、屏幕与窗口几何，失败日志用于定位，详细覆盖见[测试策略](testing.md)。

验证脚本仅临时为当前进程和测试子进程设置 `TEMP`/`TMP=build/validation/ui/temp/`，供隔离布局文件夹具使用，并在 finally 中恢复原环境，避免受管桌面默认临时目录不可写；不改变系统设置或产品保存路径。几何抓帧在测试夹具中隐藏 ToolTip，JSON 标记 `tooltipsHiddenForGeometryCapture`，提示交互另行测试。

此矩阵覆盖当前屏幕上的 Qt 有效缩放，不证明更改 Windows 系统缩放后的行为、混合 DPI 跨屏切换、原生系统接管或屏幕阅读器焦点已通过。原生交互验证需要可见 Windows 桌面；其他 Qt 版本、MSVC 2022 应用 CI 与 macOS 构建的结果须按各自环境单独记录，不能由本机矩阵推断。

macOS 可使用对应的 Qt 6.8.2 Kit 和相同 app presets，界面明确标记模拟平台；当前尚未进行 macOS 构建验证。

## 构建输出与个人配置

采用 out-of-source 构建，输出到 `build/`。`CMakeUserPresets.json` 可为个人 Qt 路径设置 `CMAKE_PREFIX_PATH`，该文件已忽略；公开 Presets 不包含开发者机器路径。共享网格在配置时生成到构建目录，修改 JSON 会触发重新配置。

MSVC/Ninja 构建在配置与编译阶段统一 `VSLANG`，并按原始字节探测实际 `/showIncludes` 前缀，防止中文输出编码使头文件依赖遗漏。无法识别前缀时配置直接失败；不要绕过探测后继续依赖增量构建。工具链或语言环境变化后，应重新配置，并验证修改共享头文件会触发相关目标重编译。

正式构建入口将 Windows 构建子进程的 `TEMP`/`TMP` 指向本项目 `build/tmp/`，避免 MSVC v142 经由 CMake 调用时错误解码中文用户临时目录，导致编译器探测报 C1083。该设置只存在于构建子进程，不改变系统或用户环境变量；实际目录写入工具验证记录。

[应用工作流](../../.github/workflows/application-build.yml) 读取本项目工具清单，定义 Ubuntu 核心构建及额外 MSVC 2022 Windows Qt 构建、lint、CTest；它与原有文档检查并行保留。[#12](https://github.com/xiaodaozhi/xdstarter/issues/12) 保存早期 Actions 额度阻塞及恢复记录，不代表当前 PR 的状态。当前 head 的适用检查以关联 PR 为准，各版本的本地、CI 与真实窗口证据统一见[测试策略](testing.md)，不以历史运行结果替代新提交检查。

官方依据：[Qt 6.8 Windows 支持配置](https://doc.qt.io/qt-6.8/windows.html)、[MSVC 二进制兼容性及 linker 限制](https://learn.microsoft.com/en-us/cpp/porting/binary-compat-2015-2017?view=msvc-170)、[CMake 要求](https://doc.qt.io/qt-6/cmake-supported-cmake-versions.html)。
