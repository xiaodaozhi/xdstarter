# 测试目录

仓库工具的无网络单元测试验证 GitHub 初始化、Windows 打包与依赖审计、故障边界、验收资料和 EXE 图标等行为。实际数量与结果以本次运行输出为准：

```sh
python3 -m unittest discover -s tests/tooling -v
```

`core/` 为无 Qt 的 C++20 几何行为测试。`core-debug` 与 `core-release` 验证相同可观察行为，测试不使用会在 Release 中消失的 assert。Windows Qt 应用开启 `BUILD_TESTING` 时注册 23 套 CTest；注册数量不表示本轮已经全部通过，实际结果按程序版本记录：

| 目录/入口 | CTest | 范围 |
| --- | --- | --- |
| `core/` | `core.tile_grid` | 网格几何、边界、非法尺寸与溢出 |
| `platform/win_key_capture_tests.cpp` | `platform.win-key-capture` | 独按／组合键、派发失败、暂停和事件代次 |
| `platform/start_button_capture_tests.cpp` | `platform.start-button-capture` | 点资格与过期、显示器／DPI／代次变化、完整 down/up、拖出／长按／双击、暂停与合成输入透传 |
| `services/layout_import_tests.cpp` | `services.layout_import` | 严格预览、取消、替换事务、修订与失败恢复 |
| `services/search_service_tests.cpp` | `services.search` | 离线名称／拼音／别名匹配与索引代次 |
| `services/prototype_service_tests.cpp` | `services.prototype` | 稳定标识、邻接、预览不变性、移动／跨组／三档尺寸及确定性挤开、满格源位复用／空间不足原子拒绝、180 轮不变量与恢复、固定/取消固定及非法操作拒绝 |
| `services/layout_store_tests.cpp` | `services.layout_store` | 严格 schema、原子保存、备份/损坏恢复、未来版本和失败路径 |
| `services/session_instance_tests.cpp` | `services.session_instance` | 独立进程 activate 握手、重复启动、会话隔离和主实例释放恢复；本机命名管道需要正常桌面权限 |
| `services/application_browser_tests.cpp` | `services.application_browser` | 中文拼音／数字／拉丁／未知分类、稳定排序、文件夹层级／返回及刷新移除恢复 |
| `services/application_catalog_tests.cpp` | `services.application_catalog` | worker 调用、目录刷新、图标更新/缓存预算、启动复核与失效目标保留 |
| `platform/` | `platform.applications`、`platform.applications.localized`（后者仅 hosted Windows） | 隔离快捷方式、原生图标、失效目标及受控启动；工作区按 Shell 显示名验证，用户配置临时目录严格核对中文与路径身份 |
| `platform/desktop_platform_tests.cpp` | `platform.desktop` | 桌面动作白名单、可信/失败面板矩形一致、工作区夹限/极值/阴影留白转换、原生装饰抑制与窗口所有权边界及托盘旧会话事件失效；只读 UIA probe 显式执行 |
| `platform/desktop_width_snap_tests.cpp` | `platform.desktop-width-snap` | 五档缩放、负坐标、左右拖动、最近档位、最大边界及非法输入的宽度吸附 |
| `platform/native_taskbar_probe.cpp` | `platform.native-menu-resize`（仅 Windows） | 自有隐藏 HWND 的 36 项原生消息／窗口策略回归：原 24 项边角、非活动、阶段、最大边界与窗口策略，另加四边各 3 项 region／重新定位与尺寸／Qt mask 清空重套；不发送全局输入 |
| `services/desktop_shell_service_tests.cpp` | `services.desktop_shell` | 注入平台上的后台动作、成功/失败、重复请求拦截、查询合并/失效与析构等待 |
| 产品 `--smoke-test` | `app.qml_smoke` | 从资源加载 QML/token 后退出，不读写用户布局 |
| `graphics/` | `app.d3d11_failure_exit`（仅 Windows） | 隔离副本中的设备创建故障、自动 WARP、无设备时有界退出；仅 hosted runner 常驻用例验证已启用的接管关闭及提示进程启动 |
| `platform/graphics_failure_notice_tests.cpp` | `platform.graphics-failure-notice`（仅 Windows） | 系统提示失败回退及独立进程看门狗 |
| `ui/scene_graph_failure_host_fixture.cpp` | `ui.scene_graph_failure_host` | 图形错误时先停止假托盘及两类假接管，再尝试提示；重复、冒烟及启动前失败 |
| `ui/` | `ui.prototype` | 真实 Qt Quick 窗口的交互、编辑、保存恢复、只读与像素截图 |
| `ui/tst_desktop_window_host.cpp` | `ui.desktop_window_host` | 假托盘与定位平台上的隐藏阶段定位、待显示取消、过时查询拒绝、关闭后恢复、外部焦点收起、内部临时窗口/快速返回保留、拖拽延后、无托盘/托盘丢失恢复及退出清理 |

产品与 GUI 测试共用 `xdstarter_add_ui` 编译的 QML、controller 与视觉资源；Qt6::Test 只进入测试目标。GUI 测试使用独立临时配置，覆盖鼠标组内/跨组拖拽、冲突与 Esc 取消、三档尺寸菜单、Ctrl+方向键移动、重建窗口恢复布局/主题、失败重试和未来版本只读，另检查两主题下尺寸菜单高亮/按下文字与勾选的对比度，并保留原有名称、导航、主题、最小窗口与磁贴像素几何回归。无边框窗口与共享圆角 token、可键盘收起按钮、`Popup.Item` 内部菜单、呈现清理契约和外部释放取消拖拽预览继续回归；本轮改为透明外窗、自绘边框/阴影与侧栏背景圆角裁切，正文尺寸及共享磁贴几何不变。清理不得改变已提交布局或退出编辑模式。

```powershell
# 使用toolchain.json正式工具和默认项目Qt缓存，包含构建、lint与十八套CTest。
./scripts/build-windows.ps1 -InstallTools -Python python -Preset app-release -Fresh
$env:XDSTARTER_QT_ROOT = "$PWD/build/Qt/6.8.2/msvc2022_64"
./scripts/verify-windows-ui.ps1 -QtPrefix $env:XDSTARTER_QT_ROOT
./scripts/verify-windows-ui.ps1 -QtPrefix $env:XDSTARTER_QT_ROOT -NativeScales
```

`-Python` 需指向声明的 Python 3.12.13。CTest 默认 GUI 使用 offscreen/software；`-NativeScales` 使用 Windows QPA，探测基础 DPR 后校准五档 Qt 有效缩放。像素扫描仍以实际占用的磁贴区域为准，不把新增两行空白拖放区算成磁贴高度。几何截图夹具临时隐藏 ToolTip（JSON 记录 `tooltipsHiddenForGeometryCapture`），取样避开字形抗锯齿，不改变产品提示或像素容差；ToolTip 交互独立检查。工件写入忽略的 `build/validation/ui/`，不提交到 Git。

实际窗口与重启恢复需另用产品 `--layout-file` 隔离配置验证；原生结果以[测试策略](../docs/technical/testing.md)和 Issues 的本轮记录为准。自动检查不替代真实系统缩放切换、混合 DPI、屏幕阅读器或开始菜单接管验收。工具来源与支持边界见[开发环境](../docs/technical/development.md)。

Windows 产品唤起调用 `queryMenuAnchor()`，仅后台读取显示器、工作区和任务栏 DPI，不执行 COM/UIA。产品入口将 QML 初始 `visible` 设为 `false`，定位后再显示；smoke 与独立 GUI 夹具仍自行显示。服务的 `requestStartButtonAnchor()`、`startButtonAnchorReady` 名称暂保留兼容。平台测试程序的 `--probe-menu-anchor` 显式输出这一工作区查询 JSON；`--probe-anchor` 继续用于独立的 UIA `queryStartButtonAnchor()` 研究，两者不进入常规 CTest，也不把输出成功当作托盘点击或系统接管验收。

无边框菜单的 host 测试通过真实 Qt 窗口与注入托盘/定位平台触发内部与外部焦点，但不操作用户托盘。常规平台测试在隐藏的自建原生窗口上验证原生装饰抑制与窗口所有权边界；API 接受结果不等于目视确认仅有一层边框、圆角或阴影。产品实际验收需分别测量透明外窗和可见面板，检查左侧/底部间距、重复唤起不跳变、点击桌面/外部应用、内部菜单、Esc、拖拽越界与编辑状态保留，随后验证托盘恢复、正常退出及新进程配置恢复。

目录 GUI 使用注入平台验证实际图标像素、固定/取消固定、启动失败保留窗口、成功收起、失效目标占位和空布局恢复。平台测试只创建 build 内的快捷方式并启动自身测试 helper，不更改用户开始菜单。真实 AppsFolder 与已知系统应用的启动由产品窗口单独验收。

快捷栏 GUI 验证 qrc 资源、64/48/32/24 DIP 几何、深浅主题圆形头像、键盘与提示、受控桌面动作结果、电源文字退出及关于状态。常规桌面服务测试不打开真实用户文档、下载或系统设置；原生托盘右键菜单与 Explorer 重启必须另行实际验证，不能由 fake 平台替代。

实际构建的 EXE 图标可以单独只读审计；命令不会加载或运行产品：

```powershell
./build/tooling/Scripts/python.exe scripts/check_windows_icon.py --executable ./build/app-release/bin/xdstarter.exe
```

[Issue #16](https://github.com/xiaodaozhi/xdstarter/issues/16) 正式 Release、QML lint 与 CTest 10/10 通过（13.64 秒，`build/custom-chrome-check.log`），GUI 为 32/32、host 为 17/17。默认 offscreen/software 的 RGB backing store 不保留 alpha，外观用例通过 Canvas `toDataURL("image/png")` 检查原始 RGBA 绘制结果，侧栏透明与控件圆角内几何另查；该回退不能当作 Windows 完整窗口截图。

2026-10-21，同一 GUI hash 的 `--native-chrome-probe` 在真实 Windows QPA/Direct3D 11、DPR 1.5 下通过十二轮，首次可见与稳定后矩形相同、左/底均为 8 DIP；深浅主题直接通过完整 `grabWindow()` 检查 RGBA，不使用离屏回退。此 probe 采用模拟托盘信号，没有注册或点击真实系统托盘；[深色](../docs/technical/validation/issue-16/dark-native-chrome.png)、[浅色](../docs/technical/validation/issue-16/light-native-chrome.png)验证图均为无用户数据的内存样例，不是产品真实目录。五档 Windows QPA 各 32/32，30 份 JSON 与 60 张窗口/网格 PNG 的像素检查通过。

实际产品已验证拖拽、内部尺寸菜单/Esc、主题保存、新进程恢复相同 IDs/targets/groups/preferences 和外部失焦收起。用户亲自点击通知区图标恢复并完成两轮重复收起/唤起，目视确认等距、稳定、编辑保留及单层圆角/阴影，最终回复“均正常，已从菜单退出”；这是用户协助真实托盘验收，与 probe 的模拟信号分别记录。两进程均从电源菜单正常退出，代码均为 0，本轮本机验收完成。精确程序 hash、工件与支持边界见[测试策略](../docs/technical/testing.md)当前章节。

真机验收结束时的 CI 付款/额度阻塞属于历史状态；用户随后恢复额度，[PR #17](https://github.com/xiaodaozhi/xdstarter/pull/17) 的仓库与 Linux 核心检查已成功。首轮 Windows 检查因 `setup-python` 无法获取精确 Python 3.12.13 Windows 二进制失败，该仅源码安全版本的获取路径现由 PR #17 修复为固定版本 uv managed Python，正式依赖基线不变。获取步骤修复本身不是构建通过证据，最终远程结果以 PR #17 和 [#12](https://github.com/xiaodaozhi/xdstarter/issues/12) 的实际运行记录为准，本机通过不替代 PR 检查。

历史 [#15](https://github.com/xiaodaozhi/xdstarter/issues/15) 正式 Release、QML lint 与 CTest 为 10/10，GUI 为 30/30、host 为 14/14，五档 Windows QPA 各 30/30；实际焦点、用户协助托盘恢复、受控中断与新进程读取结果分别记录，不将中断当作正常退出。[#14](https://github.com/xiaodaozhi/xdstarter/issues/14) 普通窗口的 10/10 与五档各 26/26 同样保留历史身份，不沿用为本轮通过结论。

## 功能测试优先级

参考 xdzip，日常变更优先使用纯算法、服务 fixture、QtTest 和本机 IPC 子进程；没有必要时不使用 computer use。截图由 QtTest 抓帧并直接查看本地图片，设计导出与运行时截图分别标记。系统显示／任务栏配置、Explorer 重启及物理输入法等无法由单元测试证明的内容保留明确验证边界；不为完成普通回归反复操作用户桌面。

### Issue #48 当前测试范围

2026-10-23最终本机附加v142补丁兼容构建：18/18 CTest、GUI66/66、host50/50、IPC6/6与工具13/13通过，QML lint无诊断；实际编译器19.29.30159.0，不标为精确正式基线19.29.30153.0通过。同一产品EXE只读Start九点Win32／UIA全通过，旧#45物理矩阵不沿用为新程序亲测。完整散列与边界见[本轮证据](../docs/technical/validation/issue-48/interaction-and-layout.json)。

本轮新增或调整的检查围绕可观察行为展开：Tool／Topmost 的原生样式与自有对话框边界；隐藏阶段先将进入进度归零、每轮只播放一次、快速取消退出和迟到回调；带退出动画的统一隐藏、重复收起不延长、托盘丢失时恢复最小化窗口及重试／退出；工作区四边间距、有效最小／最大尺寸与有界吸附。纯 region 矩阵另有 40 组四边／五档 DPR／负坐标组合，验证只扣任务栏交集、面板和其余阴影保留，以及极值和无交集边界。Host 的假平台分别模拟前台就是菜单根窗口和真正 owned dialog，不能把二者合为同一保护分支；region 应用失败必须取消显示，不能走未排除任务栏的 Qt 回退。

隐藏后台 STATIC fixture 不通过激活或抢前台来取得置顶权限；断言保持原 styles、TOOLWINDOW=true、APPWINDOW=false，并要求实际 TOPMOST 或明确 `success=false / menu-topmost-state-denied`，不接受其他错误代替实测。实际 Qt 原生 fixture 和可见产品候选仍强检 TOPMOST。Host 另验无托盘紧急恢复时整个透明外窗及最大尺寸都在工作区 8 DIP 内、边框拖动关闭，托盘重试成功后重新定位并恢复方向对应缩放边。纯 region 极值覆盖 INT_MIN 与 INT_MAX，避免 QRect 通用交集的归一化中间值溢出。

普通定位错误的 Qt 降级显示另验完整外窗在工作区内、该轮禁用缩放，后续原生定位成功恢复正常 region 和缩放边；不能把 `menu-input-region-failed` 混入可见降级分支。

QML 用例另覆盖新右键／下拉菜单及尺寸子菜单、统一图标位、hover 无强调色闪动、关于卡片、必要错误反馈与关闭、紧凑无阴影 tooltip、目录返回与空状态、目录拖动固定，以及动态列数、重排和持久化。目录扫描和固定使用隔离数据，常规测试不修改真实开始菜单，不启用真实全局 Hook。回归范围不代表尚未运行或仍失败的用例已经通过；当前完整构建、GUI 与原生结果见[测试策略](../docs/technical/testing.md)和 [Issue #48](https://github.com/xiaodaozhi/xdstarter/issues/48)。

Topmost 的透明外窗可能跨入任务栏，必须单独检查 Start 上沿实际命中。以下命令只读观察一个已经显示的指定测试候选，不会启动、显示、移动或关闭产品，不移动鼠标，也不合成输入或安装 Hook：

```powershell
$candidateProcessId = 12345 # 替换为当前已显示候选的实际进程号。
$env:PATH = "$PWD/build/Qt/6.8.2/msvc2022_64/bin;$env:PATH"
./build/issue48-release/xdstarter_native_taskbar_probe.exe --run --observe-start-overlap $candidateProcessId
```

输出包括菜单／任务栏 HWND、物理边界、style / extendedStyle、region、相对 Z 序，以及唯一 StartButton 的 UIA 矩形；采样按钮中心、四角和靠菜单一侧 1／4／8／16 DIP，分别记录 `WindowFromPoint` 与 UIA 是否仍属于 Start。COM worker 对 UIA 连接和事务设 100 ms 限制，采样后复核几何稳定。`measurementCompleted` 仅表示测量完成，`menuHitCount > 0` 仍是需要处理的命中结果；正常退出码不等于 Start 点击通过。结果不授予输入资格，且不替代真实左键、右键与 Win+X 验收。单次观察不扩大 Windows 支持矩阵。

Issue #41 的 `interactionRefinements` 与 `catalogAlphabetAndFolderOverlay` 覆盖默认编辑、中文字体、圆形清除、Tab／点击焦点门控、600 ms tooltip、移除旧入口、hover 图钉、分类全视口覆盖和文件夹导航不启动应用。`XDSTARTER_INTERACTION_CAPTURE_DIR` 可把深浅更多菜单抓帧保存到调用者指定的本地目录；它不读取真实目录或注册托盘。

Issue #41 的历史正式本机结果为 15/15 CTest、GUI 57/57、host 38/38，字体字形与目录刷新后的视觉尺寸均有回归；详见[历史证据](../docs/technical/validation/issue-41/menu-experience.json)。

Issue #45 后续真机修复的正式结果为 17/17 CTest（94.05秒）、GUI 63/63、host 41/41。任务栏参数测试覆盖四边、双轴居中、五档缩放、负坐标和三种尺寸，共240组合；新增80组边／角缩放组合及最小尺寸、溢出与任务栏缺失回退。Windows 常规 CTest 的 `platform.native-menu-resize` 显式调用 `--run --resize-filter-test`，覆盖14项本窗口消息处理，夹具不进入系统模态输入循环。`xdstarter_native_taskbar_probe --run` 仅移动测试自己的隐藏 HWND；`--run --launch-show-states` 验证隐藏／最小化子进程的实际可见、最小化与前台状态；`--run --observe-product PID` 只读观察指定测试产品窗口。后两者与隐藏矩阵不能混为完整物理输入验收。

`scripts/verify-taskbar-integration.ps1` 默认只读，输出原生几何和开始按钮识别 JSON；显式 `-AlignmentMatrix` 才短暂修改 TaskbarAl，并在 finally 中恢复、复核原值和类型。它不重启 Explorer、不改 StuckRects 二进制、不猜测任务栏方向设置。真实系统四边切换、边框拖动与其他 Windows 构建的状态见[测试策略](../docs/technical/testing.md)，未运行的组合继续由 GitHub Issue 跟踪。
