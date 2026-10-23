# ADR-0004：真实应用目录、启动目标与布局 schema 2

日期：2026-10-20。状态：已接受，实施与验收由 [#7](https://github.com/xiaodaozhi/xdstarter/issues/7)、[#9](https://github.com/xiaodaozhi/xdstarter/issues/9)、[#10](https://github.com/xiaodaozhi/xdstarter/issues/10) 跟踪。

本 ADR 记录应用目标接入时的 schema 2 决定；当前显示名称覆盖及 schema 3 读取／写入规则由 [ADR-0011](0011-tile-display-names.md) 延续，原目标类型、启动语义及权限分类继续适用。

## 背景

普通 Qt Quick 窗口已有三档磁贴、编辑与 schema 1 样例快照，需要接入真实 Windows 应用并保存用户固定状态。直接用可执行文件路径代替快捷方式会丢失参数与工作目录；让 QML 请求图标时执行 Shell IO 会阻塞界面。新目标必须与样例明确区分，并保留旧数据和未来版本保护。

本 ADR 更新 [ADR-0003](0003-layout-snapshot.md) 中的当前目标类型、schema 版本及首次默认样例决定；保留它的 QSaveFile、备份、损坏保留、后台合并保存和只读保护。

## 决定

- 新建可替换的 `ApplicationPlatform`，提供同步的目录快照、图标、可用性与启动结果；与普通窗口 `PlatformIntegration` 分开。上层 ApplicationCatalog 在专用 QThread worker 调用，Win32/COM 头和句柄不进入布局及 QML。
- Windows 枚举当前用户/公共 Programs Known Folder 中的 `.lnk`，通过 AppsFolder 补充打包应用。扫描限制应用、目录、层级与目录项数量，不递归重解析目录。快捷方式 id 来自规范化绝对路径，打包应用来自 AUMID；不按同一可执行文件或名称笼统合并快捷方式，从而保留不同参数和用户命名。下述 [#53](https://github.com/xiaodaozhi/xdstarter/issues/53) 的跨根等价展示例外不改变目标 id 或启动协议。
- 图标按需在 worker 提取，平台以 RAII 释放 HBITMAP/HICON/DC 和 COM 引用，返回 QImage。GUI/provider 只读约 8 MiB 内存缓存或返回占位，再排队请求；不新增图标下载、永久缓存或 Widgets 依赖。
- 启动前重新校验目标。`.lnk` 原文件交给 ShellExecuteExW，不另拼参数或工作目录；后台线程调用使用 SEE_MASK_NOASYNC，错误通过结构结果回到 UI。打包应用只接受支持的 `packageFamily!application` AUMID，使用 IApplicationActivationManager::ActivateApplication。每个平台方法在调用线程内初始化及释放 COM，接口不保存跨线程 COM 对象。
- 用户选择后才启动。保存/恢复和目录扫描不执行目标；系统接受启动请求后收起窗口，失败保留窗口并显示原因。非 Windows 实现明确返回不支持，不伪装为真实 Windows 目录。
- 正常首次运行使用空“常用应用”分组；smoke 和独立测试保留三组样例。固定真实目录项时以标准尺寸选择目标分组首个空位，相同目标重复固定返回已有磁贴。取消固定不删除系统快捷方式或卸载应用。
- 当前写入 schemaVersion 2，支持 shortcut、packaged、prototype 目标及没有磁贴的布局，至少保留一个组。布局仅保存目标描述、稳定引用、整数格坐标、尺寸与主题；不保存实际图像或任意命令串。路径、AUMID、未知字段、数量和引用均校验。
- schema 1 继续严格读取，保留样例标识、位置、尺寸和主题，不推断为真实应用。读取不写回；下次用户保存才写 schema 2，按原保存协议将旧主文件原始字节保留到 `.bak`。较新版本或超限文件继续只读，旧程序不能忽略 schema 2 字段后写回 schema 1。
- 应用卸载、快捷方式移除或图标失败与布局损坏分别处理；恢复的目标描述和位置仍保留，后台检查可用性并允许取消固定。扫描当前通过启动时/手工刷新触发，不包含自动目录监视、重新绑定目标或导入 UI。

### 权限与部分目录失败（Issue #7 补充）

2026-10-21 的隔离 Windows 验收补充以下错误边界：

- Windows 目录枚举用有界 `FindFirstFileW`／`FindNextFileW` 读取实际失败，保留原来的应用、目录、层级和目录项上限，以及跳过重解析目录的约定。根目录全部不可读时 `success=false`，ApplicationCatalog 保留原列表；一部分根或子目录不可读时保留本轮可读结果并附 warnings，界面持续显示目录错误反馈。重复的不可读根不能把失败改写为成功空目录。
- 缺失快捷方式、缺失启动文件、无效快捷方式及明确权限拒绝分别返回 `shortcut-missing`、`target-missing`、`shortcut-invalid`、`access-denied`。已存在的文件不因访问拒绝被谎报为移动或删除。其他文件系统错误保留不可访问原因；应用激活及 Shell 的一般失败仍保留对应分类，不能将 `E_FAIL` 一概解释为权限拒绝。
- 本机 Shell 在受控可执行文件被 DACL 禁止执行时会返回一般 `E_FAIL`。因此对解析后存在的普通 `.exe`／`.com` 文件增加短时 `FILE_EXECUTE` 检查，只有明确的访问拒绝或权限不足才提前拒绝；重解析文件／应用别名、文档关联及其他打开错误仍保留原有 Shell 路径。启动继续将原 `.lnk` 直接交给 Shell，不复制参数或工作目录。预检不授予权限，也不替代实际启动结果；外部文件或 ACL 仍可能在预检后变化。

真实 DACL 仅施加于 build 内新建的隔离文件和子目录，先验证原生错误，再检查结构化结果；恢复原 DACL 后才清理。未安装的匿名 AUMID 验证与卸载后缺少注册项等价的不可用状态，不实际卸载用户应用。平台实测与服务假平台回归分别记录，详见[测试策略](../testing.md#应用目录异常验收issue-7)。

### 跨 Programs 根的等价展示（Issue #53 补充）

2026-10-23 的 Bitwarden 目录诊断确认，用户和公共 Programs 根中可以同时存在同相对路径、同显示名、指向同一程序的快捷方式；两份普通桌面 AUMID 元数据可以不同。AppsFolder 中不含 `!` 的桌面 AUMID 仍不进入打包应用激活路径。

- 展示合并只适用于不同 Programs 根、规范化相对 `.lnk` 路径相同、Shell 显示名称完全相同的两项；不跨相对路径、用户命名或文件夹位置猜测同一应用。生产扫描先用户根、再公共根，保留用户根的原路径和原 id。
- 只读解析必须证明两者都是存在的直接普通 `.exe`／`.com` 文件，原路径与文件系统 IDList 指向相同目标；比较规范化目标和工作目录、完整参数、RunAs、ShowCommand 和 HotKey。空工作目录保持为空，不替换为程序目录。参数通过 `IPropertyStore` 的 `PKEY_Link_Arguments` 读取，避免固定长度的 `GetArguments` 静默截断。
- 图标、描述、跟踪数据和普通桌面 AUMID 是展示／身份元数据，不因这些差异阻止已证明的普通文件启动等价。包含 `!` 的 AUMID、环境目标、安装器、兼容性 shim、控制台设置、未知标志／扩展块、重解析目标、路径不一致、缺失／非程序目标、读取失败或数据不完整时保留两项。单份原始链接读取最多 256 KiB，格式检查只为安全地拒绝无法证明的等价，不代替 Shell 启动解析器，也不调用 `Resolve`。
- 4096 个目录／应用候选的预算在合并前计数，不能用被隐藏的重复项扩大扫描量；保留 512 目录、12 层和 32768 目录项上限。快照额外返回仅本轮有效的 `equivalentTargetIds`（隐藏 id → 展示代表 id），供服务聚合现有固定状态和搜索展示；刷新重新建立证明，不将等价关系永久写入布局。原文件不移动、不删除、不另存，不迁移任何已固定磁贴或布局目标；已保存的公共快捷方式 id 仍可沿原路径独立检查和启动。优先来源消失后的下一次刷新可展示原公共来源及其原 id。

平台回归以 build 内隔离快捷方式覆盖等价、AUMID／图标／描述差异、参数与长参数、工作目录、RunAs、显示方式、热键、不同目标和相对位置、嵌套路径、未知扩展、损坏／缺失目标、来源消失及合并前预算。显式诊断入口 `xdstarter_application_platform_tests --catalog-probe [名称片段]` 只扫描真实目录并向 stdout 输出 JSON，在所有 fixture／受控启动测试之前分派；不启动被扫描应用、不改真实快捷方式、不创建 GUI。执行结果和当前 head 的验证由 Issue／PR 证据记录，不由用例存在推断通过。

此范围依据微软的 [Shell Link flags](https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/ne-shlobj_core-shell_link_data_flags)、[Shell Link 文件格式的 LinkFlags](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-shllink/ae350202-3ba9-4790-9e9e-98935f4ee5af) 和 [GetArguments 的完整参数读取建议](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ishelllinkw-getarguments)；是否合并是本产品在这些 API 语义上做出的保守决定，不是 Windows 的全局应用身份协议。

## 取舍与影响

Shell 接口保持 Windows 的快捷方式语义，避免复制目标参数时的转义错误。按路径识别意味着快捷方式移动后形成新的目录身份，旧固定磁贴会保留并提示目标失效；当前通过取消后重新固定处理，不做可能错误的自动匹配。AppsFolder 只接入可由打包应用激活接口处理的 AUMID，桌面应用仍来自开始菜单快捷方式。

单个 worker 简化平台生命周期与结果顺序，避免无限并发提取图标；单次 Shell 调用仍可能受外部组件或磁盘影响，当前不宣称启动/扫描达到性能目标。缓存可重建，失败占位不阻碍布局恢复；布局权威数据和应用目录保持分离。

## 验证与依据

应用目标接入阶段的历史验证包括：平台隔离快捷方式及受控启动测试、中文/特殊字符路径、不同参数、不存在目标与 AUMID、原生图标、启动参数和工作目录；当时正式 Release、QML lint、七套 CTest 与 23 项 GUI 检查通过。该阶段普通窗口验证了 281 项真实目录、系统图标、真实磁贴恢复、画图 AUMID 启动后收起窗口、失效占位及原因、取消最后一枚与空布局/light 新进程恢复。后续 #14 桌面集成的十套 CTest、26 项 GUI 与新进程证据，和上述旧阶段的版本/手动复核中止边界分别保留在[测试策略](../testing.md)，没有用接口完成或假平台测试代替实际窗口验收。

API 依据为微软文档：[GetImage 后台提取与位图释放](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ishellitemimagefactory-getimage)、[SHELLEXECUTEINFO 与无消息循环线程](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/ns-shellapi-shellexecuteinfow)、[应用激活接口](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nn-shobjidl_core-iapplicationactivationmanager)。
