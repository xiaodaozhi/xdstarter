# 技术架构

状态：Qt Quick 路线已确认；当前原型已实现拖拽/键盘移动、三档尺寸、布局与主题持久化，以及 Windows 应用目录、系统图标、启动与固定/取消固定。[Issue #14](https://github.com/xiaodaozhi/xdstarter/issues/14) 接入设计素材、桌面动作与托盘；[#15](https://github.com/xiaodaozhi/xdstarter/issues/15) 接入无边框与外部焦点收起；[#16](https://github.com/xiaodaozhi/xdstarter/issues/16) 接入自绘外观和隐藏阶段定位；[#22](https://github.com/xiaodaozhi/xdstarter/issues/22) 接入独立 Win 键接口、显式启用/暂停和托盘守护。[#8](https://github.com/xiaodaozhi/xdstarter/issues/8) 增加离线应用名称、拼音和首字母搜索；本轮 [#9](https://github.com/xiaodaozhi/xdstarter/issues/9) 接入分组新建、重命名、排序和空组删除。各版本的构建、GUI 与物理键盘证据统一见[测试策略](testing.md)，不由代码接入推断 Windows 支持范围。磁贴显示名称编辑与 [#6](https://github.com/xiaodaozhi/xdstarter/issues/6) 中开始按钮、材料及其他系统兼容目标继续推进。

## 模块与依赖方向

| 模块 | 职责 | 边界 |
| --- | --- | --- |
| app | 生命周期、单实例、依赖组装、菜单控制器 | 组装服务；不承载布局算法 |
| ui | QML 页面、控件、主题与输入反馈 | 绑定 C++ 模型；不直接操作注册表或 Shell |
| core | 网格、磁贴/分组与布局校验 | 纯 C++；不依赖 Qt 或 Win32 |
| services | 应用目录、Qt Core 搜索索引、图标缓存、配置存储 | 搜索不依赖 Win32；目录等功能通过接口调用平台模块 |
| platform/windows | Win32/COM、窗口材料、托盘、输入接管、应用启动 | 专用线程与资源清理；向上暴露稳定事件/结果 |
| platform/stub | 通用 UI 开发的假目录、普通窗口唤起 | 明确标注模拟功能，不能代表 Windows 兼容 |

依赖方向为 `ui → app/models → services/core → platform interface`，Windows 实现由 app 注入。布局与搜索无需运行 Explorer；平台模块不依赖 QML 页面类型。

工程目录已建立，当前 targets 为：

| Target | 当前实现 |
| --- | --- |
| `xdstarter_core` | 纯 C++ 三档逻辑矩形、范围与溢出校验；由共享 JSON 生成几何常量 |
| `xdstarter_services` | `PrototypeService` 提供邻接、布局事务、固定/取消固定及分组管理；`LayoutStore` 校验、原子保存及恢复 schema 1/2；`ApplicationCatalog` 后台读取目录、图标、可用性及启动；`DesktopShellService` 在另一专用 worker 执行受控桌面动作与工作区查询；`SearchIndex` / `SearchService` 使用 Qt Core 值类型、独立 worker 与离线字音表完成有界搜索 |
| `xdstarter_platform` | `PlatformIntegration` 提供集成状态；`ApplicationPlatform` 封装应用目录、图标及启动；`DesktopPlatform` 封装受控 Shell 动作、工作区/UIA 查询、面板物理定位与原生装饰抑制；`DesktopTray` 封装 `Shell_NotifyIcon` 与文字菜单；`WinKeyCapture` 封装默认关闭的单独 Win 键接管及纯 C++ 状态机。Windows Hook 仅显式启用后安装，非支持平台返回明确的不支持结果 |
| `xdstarter` | `QGuiApplication`、依赖组装、`MenuController`、缓存图像 provider、`DesktopWindowHost` 与 QML；支持应用与磁贴操作、快捷栏、无边框呈现、延后焦点复核及可恢复的托盘收起/显示生命周期 |
| `xdstarter_core_tests` | 独立核心行为测试，Debug 和 Release 均执行真实检查 |
| `xdstarter_prototype_service_tests` | 样例布局、稳定标识、方向键邻接、移动/尺寸、分组事务与非法操作不变性测试 |
| `xdstarter_layout_store_tests` | schema、引用/数量/边界校验、保存恢复、备份、损坏和未来版本保护 |
| `xdstarter_ui_tests` | 使用 Qt Test 加载与产品相同的 QML、controller 和视觉资源，检查真实窗口交互及截图像素 |
| `xdstarter_application_catalog_tests` | 注入假平台检查目录刷新、worker 线程、图标和启动结果；不把模拟平台当作 Windows 验收 |
| `xdstarter_search_service_tests` | 字面／全拼／首字母、规范化、多音、去重、稳定排序、结果上限、查询及目录代次与退出；另提供显式参考数据集计时入口 |
| `xdstarter_application_platform_tests` | Windows 隔离快捷方式目录、稳定标识、失效目标、原生图标与受控 helper 启动，不修改系统开始菜单 |
| `xdstarter_desktop_shell_service_tests` | 注入假桌面平台检查动作白名单、后台执行、去重、查询代次与线程退出，不打开用户文件夹或系统设置 |
| `xdstarter_desktop_platform_tests` | 桌面动作边界、物理矩形夹限与托盘会话清理；真实 UIA 查询采用显式只读 probe，独立记录环境 |
| `xdstarter_desktop_window_host_tests` | 假托盘与定位平台上的关闭后恢复、迟到定位、外部焦点与内部临时窗口、拖拽延后、无托盘最小化、退出清理及应用关闭策略还原 |

入口将平台实现注入服务与 controller，QML 通过 required property 接收 controller，避免依赖全局 context property。原型显式使用可自定义的 Qt Quick Controls Basic 样式，避免 Windows 默认原生样式干扰自绘磁贴。主窗口采用 `Qt.Window | Qt.FramelessWindowHint`，透明外窗每侧留 24 DIP，自绘主题实色面板、12 DIP 圆角、1 DIP 单层边框与阴影；侧栏背景沿同一圆角路径裁切，正文保留面板内边距。默认面板仍为 980×680 DIP，磁贴共享几何不变；不使用 `Qt.Popup` 自动关闭或鼠标抓取。Windows 平台抑制原生圆角、边框与阴影，返回的是设置结果，不能代替实际视觉验收。Mica/Acrylic、背景模糊材料和完整 Fluent 外观仍待后续研究。`tile-grid.json` 在 CMake 配置时生成常量；`visual.json` 的 `menuChrome` 提供阴影留白、工作区间距与边框宽度，和深浅主题 SVG、应用图标 PNG 一起通过 Qt Resource System 打包，不依赖工作目录。Windows EXE 另嵌入多帧 ICO，`QIcon` 使用 16–1024 px 资源设置应用和窗口图标。应用身份固定为 organizationName `xiaodaozhi`、organizationDomain `github.com/xiaodaozhi`、applicationName `XDStarter`；普通启动通过 QStandardPaths 获取用户布局目录，`--layout-file` 可选择隔离文件；`--smoke-test` 只使用内存样例，不连接真实目录、桌面动作、托盘或定位服务，也不读写布局配置。

磁贴呈现流为 `PrototypeService → MenuController.groups → TileGroup → TileButton`，编辑流为 `QML 输入/预览 → controller → PrototypeService 校验与提交 → groupsChanged`。C++ 提供逻辑矩形与方向键目标；QML 负责输入反馈、滚动、稳定 tileId 的焦点恢复。普通首次启动显示空的“常用应用”分组；smoke 与测试夹具仍可使用三组 15 枚样例。每组宽 552 DIP，至少高 216 DIP，编辑后按内容增长。样例只选择并提示；普通模式的真实磁贴调用启动服务。

分组管理沿用相同编辑通道。controller 要求配置可写且处于编辑模式；PrototypeService 校验组名、稳定 groupId、1–32 组边界与空组删除，再提交新建、重命名或相邻排序。新组追加，排序只改变 groups 数组顺序，重命名只改 group.name，磁贴身份、目标和共享几何不变；重复组名允许存在。既有 schema 2 已承载名称和顺序，继续使用现有格式与模块边界。名称输入、焦点与取消语义见[交互设计](../product/interaction.md)，数据约束见[磁贴布局](tile-layout.md)和[数据与持久化](data-persistence.md)。

启动时 LayoutStore 有界读取并校验 schemaVersion 1/2，PrototypeService 恢复稳定标识、格坐标、尺寸、真实目标及主题；读取旧样例不执行目标，下次用户保存才写 schema 2。编辑或固定操作提交后 controller 用 500 ms 计时器合并完整快照，交给专用 QObject/QThread 后台保存；修订号保证后续编辑不会被旧保存完成状态误标为已落盘。写入使用 QSaveFile、有效备份与版本保护；退出 flush 并等待线程结束，失败向 UI 返回原因。格式、只读和恢复语义见[数据与持久化](data-persistence.md)、[ADR-0004](adr/0004-application-catalog.md)。

## 应用目录、图标与启动

`ApplicationCatalog` 通过独立的 `ApplicationPlatform` 注入真实平台，未向普通窗口状态接口硬加 Shell 职责。Windows 扫描当前用户和公共 `Programs` Known Folder 的 `.lnk`，再通过 AppsFolder 补充有有效 package family/app 标识的打包应用。快捷方式的稳定 id 由规范化绝对路径生成，打包应用由 AUMID 生成；同路径和同 AUMID 去重，不因可执行文件相同而合并不同参数的快捷方式。扫描最多 4096 个应用、512 个目录、12 层及 32768 个目录项，不递归重解析目录；失效快捷方式仍可保留为项目。

目录读取、图标提取、缺失目标检查和启动在同一个专用 worker 上串行执行。每次平台方法负责 COM 初始化与释放，COM 对象不跨线程保存；扫描结果回到 GUI 后才替换 `QAbstractListModel`，刷新失败保留已有列表。当前为启动时或手工刷新，不包含文件系统监视或安装/卸载事件订阅。搜索接收已发布的目录值快照，不自行枚举 Shell。

图标通过 `IShellItemImageFactory` 提取，快捷方式另有 `SHGetFileInfoW` 回退；HBITMAP/HICON 与 DC 均在平台层释放，转换为独立 QImage 返回。`ApplicationCatalog` 用约 8 MiB 的 QCache 按需缓存 96 像素图标，并合并重复请求。QML 的 `image://application-icons/` provider 只读取缓存或返回占位图，随后排队后台加载，不在 provider 或 GUI 线程执行 Shell 提取；加载失败继续显示占位，不删除磁贴。图标按目标更新版本，异步完成不重建整组磁贴，目录与磁贴焦点分别管理。图标缓存可重建，不写布局快照。

列表与磁贴通过稳定目标 id 请求启动，启动前重新检查目标；`.lnk` 直接交给 `ShellExecuteExW`，参数和工作目录由快捷方式自身保留，打包应用使用 `IApplicationActivationManager::ActivateApplication`。不接收任意命令串，也不通过字符串拼接 Shell 参数。成功请求启动后窗口收起，失败保留窗口并显示结构化原因；这是系统已接受启动请求，不代表已证明目标应用完成启动。恢复时目录中缺失的固定目标单独后台检查，位置和尺寸仍保留，允许取消固定。

固定操作从当前目录取受控目标描述，默认以标准尺寸放到所选分组首个空位；同一目标重复固定返回已有磁贴，不新增副本。取消固定移除该磁贴，最后一处引用消失后移除对应描述；不修改原快捷方式或卸载应用。两项均使用相同校验和合并保存流程，schema 2 允许取消最后一枚后保存空布局。

## 离线应用搜索

`MenuController` 在 GUI 线程组装应用目录与固定目标的值快照，按 targetId 合并；目录名称优先作为显示名称，磁贴名称补充别名。固定但目录中缺失的目标仍加入索引，不把失效目标当作已删除。仅名称、别名与 id 进入 `SearchService`，Shell 对象、目标路径与启动参数不进入匹配算法。组名不参与搜索，分组排序不改变名称快照。名称快照没有变化时不重建索引；图标或可用状态更新不触发拼音索引重建，单个图标完成只改变对应 id 的图像 URL。

`SearchIndex` 位于 `services`，使用 `QString` / `QVector` 等 Qt Core 值类型，不属于无 Qt 的 `xdstarter_core`。`SearchService` 将索引构建与查询放入独立 QObject / QThread worker，一次只派发一个任务。GUI 保存最新输入，目录修订与查询代次分别递增，返回时两者均一致才发布结果；过期任务完成后转向最新输入，不为每次键入无限排队。新查询使旧结果立即失效；这不表示强制中断正在执行的查询。退出请求线程中断并等待 worker 清理。

索引对输入和 NFKC / casefold 后的名称再次有界截断，避免截开 UTF-16 代理对；最多接收 8192 条输入记录，每个合并目标最多 16 个名称／别名，每名 256 个 UTF-16 代码单元。查询上限为 128 个 UTF-16 代码单元，规范化后再次限制长度。同一 id 只输出一次，优先级为名称完整、前缀、包含、全拼、首字母；同档按规范化显示名称、id 排序。最多发布前 100 项，同时返回匹配总数。

拼音数据是固定 Unicode Unihan 17.0.0 四字段的离线派生表，`pinyinReadings(char32_t)` 按 Unicode 标量二分查找。查询的 `ü`、`u:`、`v` 等价，拼音去声调；保留名称字面符号匹配，拼音路径可略过空白与标点，未知且不可转写的字符形成断点。逐字读音采用可达 KMP 状态集合覆盖多音路径，不展开读音的笛卡尔积；这没有词级消歧保证。复杂度还受查询长度、别名数及多音分支影响，输入上限不等于所有上限组合都达到响应目标，测量范围见[测试策略](testing.md)。

GUI 将有界结果映射为名称、图标、是否固定及缓存的可用状态；图标 URL 通过每 id 的版本更新，失败继续使用占位。启动入口只接受当前有效结果中的受控 id，再沿 `ApplicationCatalog` 的可用性复核与启动路径处理，不把查询解释为路径、URI 或命令。查询是进程内暂态，不写布局或日志。界面键盘、输入法与浏览位置恢复约定见[交互设计](../product/interaction.md)，依赖来源和完整许可见 [ADR-0010](adr/0010-local-application-search.md)、[Unicode 数据说明](../../third_party/unicode/README.md)。

## 快捷栏与菜单恢复

`ShortcutRail` 为 64 DIP 宽；每项命中区为 48×48 DIP，头像显示为 32 DIP 圆形，其他图标为 24 DIP。组件按主题从 qrc 读取独立 SVG，并提供可见焦点、名称与提示。头像是已交付的圆形占位素材，点击进入账户设置，不读取真实用户照片。磁贴仍使用共享网格原生绘制，素材接入不改变三档几何。

`MenuController.requestDesktopAction` 只接收 `documents`、`downloads`、`settings`、`account`。`DesktopShellService` 使用专用 QThread 串行执行这些请求，平台层再次校验白名单；文档和下载通过 Known Folders 解析，设置与账户分别使用固定的 `ms-settings:`、`ms-settings:yourinfo`。不接受路径、URI 或命令串作为界面输入。同一时刻只接受一个桌面动作，完成后通过信号更新忙碌状态与原因；成功收起窗口，失败保留窗口。电源入口显示文字菜单，提供 Win 键唤起启用/暂停和退出 XDStarter，未接入系统关机、重启或注销。

`DesktopWindowHost` 在 GUI 边界统一管理菜单、托盘、外观、定位与接管状态。托盘注册成功时设置 `quitOnLastWindowClosed(false)`，关闭或收起窗口后进程保留，托盘可再次显示或退出；注册不可用时恢复系统关闭窗口的退出行为，但主动收起、外部焦点收起及启动成功后收起均采用最小化以保留任务栏恢复入口。host 仅在支持环境且托盘可用时允许启用 Win 键唤起，托盘失效立即停止，注册恢复不自动重新启用。退出会失效定位请求和接管通知，停止焦点计时器、Hook 线程与托盘，并执行既有布局 flush 与 worker join。

Host 监听 `focusWindowChanged`、应用激活状态与窗口激活变化；每轮显示只有实际获得根窗口或其 `transientParent` 家族焦点后才启用外部收起。焦点离开后延后 100 ms 复核，内部临时窗口、短时焦点交接和托盘菜单不会误关。初次激活被拒时不自行隐藏。拖拽且左键按下时仅在待处理的外部失焦期间短时复查，释放后正常收起；编辑模式不成为永久禁止收起的条件。

QML 的尺寸、固定、分组、电源、关于及组名编辑弹层显式使用 `Popup.Item`。`prepareForDismissal` 关闭内部弹层、丢弃组名草稿、取消未提交拖拽并暂停迟到的焦点恢复；`prepareForPresentation` 清理暂态并聚焦搜索框，保留当前查询、已提交布局和编辑模式。Host 调用该呈现契约，不依赖具体 QML 控件类型。QML 的主动收起信号交给 host 执行恢复策略，standalone smoke 仍可自行关闭。

普通窗口桌面集成的历史阶段边界见 [ADR-0005](adr/0005-desktop-shell-prototype.md)；焦点与可恢复收起见 [ADR-0006](adr/0006-menu-popup.md)，自绘外观、统一边距和隐藏阶段定位见 [ADR-0007](adr/0007-custom-menu-chrome.md)，单独 Win 键的显式启用与失败策略见 [ADR-0008](adr/0008-win-key-capture.md)。

Windows 托盘直接使用 `Shell_NotifyIcon`，不引入 Widgets。隐藏的原生宿主接收通知，WndProc 只排队事件；Qt 事件循环中显示带文字的显示/隐藏与退出菜单。停止或重建托盘会失效旧会话回调，原生菜单返回后也检查代次。`TaskbarCreated` 重注册路径已写入代码，Explorer 重启后的实际恢复仍需单独验证。

`WinKeyCapture` 由 app 注入 host，通过 `setEnabled(bool)`、`stop()` 管理会话，以 `isSupported()`、`isActive()` 和 `statusChanged(active, message, errorCode)` 报告能力与实际状态。构造和进程启动均不安装 Hook，开关不进入布局 schema；QML 与托盘分别显示启用/暂停/失败和开始按钮未接管。Windows 专用消息线程维护键盘及鼠标观察 Hook，纯 C++ 状态机区分左右 Win 独按与组合。真实 down 和组合事件放行，独按 up 的固定三条输入派发成功后才消费物理 up、投递 toggle；窗口/焦点操作均在 GUI。会话代次阻止暂停、失败或退出前的旧事件重新开窗。本地 Windows 11 与输入桌面检查只作为原型启用 gate，不等于完整支持矩阵，具体限制见 ADR-0008。

每次用户唤起窗口，host 请求 `DesktopShellService` worker 调用 `DesktopPlatform.queryMenuAnchor()`，Windows 仅读取指针所在显示器名称、物理矩形、工作区和任务栏 DPI，不初始化 COM 或调用 UIA。可见面板统一距工作区左侧和底部 8 DIP，不依据保留兼容用途的 `trusted` 字段改变位置。产品入口将 QML 初始 `visible` 设为 `false`，smoke 与独立 GUI 夹具仍自行显示。查询按代次合并；待显示窗口保持隐藏，当前结果返回后配置原生装饰抑制并迁移至目标屏幕，等待 Qt DPR 与原生物理尺寸连续一致，最终定位再显示。准备最长 1500 ms，新请求、取消、关闭、退出及同步重入使旧准备失效；超时保留托盘重试或最小化恢复入口。GUI 的 `queryWindowMetrics()` 只观测本进程 GUI 线程的顶层窗口尺寸，Win32 仍封装在平台适配器中。物理定位按稳定后的 Qt 渲染 DPR 换算并扣除透明阴影留白，不使用任务栏 DPI 或 DWM frame 边界替代；面板矩形与透明外窗边界分别记录。此次 [#28](https://github.com/xiaodaozhi/xdstarter/issues/28) 的实际跨屏验证与未覆盖组合见[测试策略](testing.md)，自动隐藏任务栏、Explorer 重启及完整混合 DPI 矩阵仍需后续验证。

服务公开的 `requestStartButtonAnchor()`、`startButtonAnchorReady` 名称暂保留兼容，但 Windows 产品路径执行的是上述快速工作区查询，不等待 UIA 提供方超时。原 `queryStartButtonAnchor()` 和 `--probe-anchor` 仅保留为显式系统集成研究，`--probe-menu-anchor` 则输出产品工作区查询 JSON；这些 probe 不进入常规 CTest 或普通唤起。

CMake 的 `xdstarter_add_ui` 为产品与 GUI 测试编译同一组 QML、controller 和视觉资源；Qt Test 仅用于测试目标。工具版本以本项目 [toolchain.json](../../toolchain.json) 为准，Windows v142/SDK19041 为正式基线，MSVC 2022 另作附加兼容验证。

## 数据流

当前数据流如下；开始按钮接管仍为后续目标：

1. 启动：读取偏好与布局 → 预创建菜单窗口 → 后台加载应用目录与图标 → 更新模型。
2. 菜单唤起：托盘事件或显式启用后的有效 Win 独按通知 → DesktopWindowHost 清理暂态、保持隐藏并标记待显示 → DesktopShellService 快速读取显示器/工作区/任务栏 DPI → 当前有效结果抑制原生装饰、隐藏迁移并等待 DPR／尺寸稳定 → 最终定位、显示与请求焦点；取消或过期结果不重开窗口，外部焦点持续离开后可恢复地收起。
3. 搜索：目录与固定目标值快照／已提交查询 → SearchService 独立 worker 构建或查询 SearchIndex → 复核目录修订与查询代次 → controller 映射缓存状态 → QML 稳定排序结果列表。
4. 编辑与固定：磁贴/分组的指针与键盘操作或选择应用 → controller 复核可写状态与编辑条件 → PrototypeService 校验 → 提交内存布局 → 更新模型 → 后台合并保存；显示名称编辑仍待后续开发。
5. 启动目标：UI 发出 targetId → ApplicationCatalog worker 复核可用性 → ApplicationPlatform 启动 → 成功收起、失败保留菜单。
6. 桌面快捷动作：快捷栏 → MenuController → DesktopShellService worker → DesktopPlatform 受控动作 → 成功收起或失败反馈；host 保证无托盘时仍可恢复。

## 核心接口方向

- `ApplicationCatalog`：已实现异步目录快照、图标缓存与启动结果；返回稳定 appId、名称、目标和图标键；自动增量扫描待开发。
- `LayoutEngine`：以格坐标计算矩形、校验占用、生成落点；不读取屏幕物理坐标。
- `SearchIndex` / `SearchService`：Qt Core 的离线名称和拼音匹配；输入值快照与查询，后台返回稳定目标 id、名称、排序等级与匹配总数，双版本校验阻止旧查询或旧目录覆盖当前状态。
- `LayoutStore`：已实现 schema 1/2 的读取、严格校验、原子保存、备份恢复与未来版本保护；支持真实目标和空布局，导入 UI 待后续扩展。
- `ApplicationPlatform`：已实现同步、可替换的 Windows 目录/图标/可用性/启动接口，由 ApplicationCatalog worker 调用。
- `DesktopShellService` / `DesktopPlatform`：已接入桌面动作、无 COM/UIA 的产品工作区查询、面板物理定位及原生装饰抑制接口；工作区查询与 Shell 在 worker，短时本进程窗口定位与设置在 GUI 边界；原 UIA 开始按钮接口仅保留显式研究用途。
- `DesktopTray` / `DesktopWindowHost`：已接入原生托盘、菜单恢复/退出、异步定位代次与外部焦点延后复核。
- `WinKeyCapture`：独立的默认关闭平台接口；Windows 专用消息线程接管单独 Win，stub 返回不支持；不依赖 QML，状态机不依赖 Win32/Qt。实际 Shell 行为需物理键盘验收。
- `PlatformIntegration`：报告当前平台集成范围与显式启用边界；材料和开始按钮接管仍待 #6 扩展。

具体 C++ 方法签名随各模块 Issue 落地；对持久化格式、错误语义或跨模块职责的变更应更新本设计。

[Issue #24](https://github.com/xiaodaozhi/xdstarter/issues/24) 的 `DesktopPlatform::queryStartButtonHitTarget()` 是显式只读研究接口，返回原生窗口身份、物理几何与五点命中的值快照；通用分类器不依赖 Win32。非支持平台返回明确诊断。它不接入 `DesktopShellService` 的产品定位、不持久化 HWND、不安装输入 Hook；`IndependentCandidate` 只表示本次采样满足研究条件，不能用于启用接管。后续缓存与完整点击手势的准入条件见 [ADR-0009](adr/0009-start-button-hit-probe.md)。

## 线程与性能

GUI 线程更新布局 QVariantList 和应用 QAbstractListModel，并管理窗口与原生托盘；保存快照在独立后台线程写盘，应用目录/图标/可用性/启动使用另一串行 worker，桌面动作与产品工作区查询再使用独立的 DesktopShellService worker。搜索另有专用 worker，GUI 只组装值快照与映射前 100 条结果，不构建拼音索引或执行多音匹配。启用 Win 键时另有专用原生消息线程，回调仅更新最小状态、固定有界输入派发与投递通知；每 500 ms 的输入桌面检查在消息循环中执行，暂停时移除 Hook，退出等待线程结束，不将其计时响应当作 Hook 存活证明。Windows 产品工作区查询不执行 COM/UIA，显式 UIA 研究 probe 独立于普通唤起。启动布局读取有界进行。队列信号或受控任务返回值不直接修改场景图对象；退出停止新请求并等待 worker 结束。Shell/COM 查询不进入 GUI 或原生消息回调。

图标缓存限制为约 8 MiB，刷新目录时重新加载；异步返回校验当前目标描述和版本，防止旧结果覆盖新目标。单个应用图标加载完成只更新对应模型行和固定磁贴的图片版本，避免全部磁贴重新请求图片；取消固定后清理不再引用的目标与图片版本。

菜单保持常驻，隐藏时停止不必要的动画与轮询。按需加载图标并限制缓存，避免每次唤起重新扫描。启动延迟、CPU、内存与 GPU 使用需要通过[测试策略](testing.md)测量。


## 菜单视觉状态（v2 修订 2.2）

[#29](https://github.com/xiaodaozhi/xdstarter/issues/29) 继续复用产品与 GUI 测试的相同 QML、controller 和视觉资源。完整布局读取 visual.json 的 layout、font、themes 与 tilePalette；tile-grid.json 仍是磁贴实体几何来源。浏览仅裁去未使用的组尾放置行，编辑恢复完整区域，服务快照和持久化 schema 不变。

controller 将操作、保存、桌面动作与目录扫描的结构化成功／失败结果映射为只读 operationError、persistenceError、desktopActionError、catalogError，并随原状态 NOTIFY 一起更新。目录部分警告也保留可见提示。QML 使用这些标志保留重要反馈，将普通成功反馈放入有界计时的覆盖层，避免常驻状态栏挤占正文；不基于本地化字符串猜测严重程度。更多菜单仍为 Popup.Item，沿用内部焦点和取消规则。独立视觉 probe 仅使用内存布局、匿名目录与图标，不调用真实 Shell、托盘或输入接管，其抓帧不能代替真实产品验收。
