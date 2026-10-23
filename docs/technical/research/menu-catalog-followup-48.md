<!-- docs/technical/research/menu-catalog-followup-48.md -->

# 应用目录合并、本地化与交互研究

研究日期：2026-10-23。本文记录 [Issue #48](https://github.com/xiaodaozhi/xdstarter/issues/48) 的只读排查与待实现方案，不表示产品已经支持新的目录合并、固定或提示策略。实施进展和验收结果统一记录于 Issue／PR。

本研究延续 [ADR-0004](../adr/0004-application-catalog.md) 的快捷方式身份、原文件启动与有界后台扫描约束，以及 [ADR-0013](../adr/0013-resident-editable-menu.md) 的层级目录呈现。方案落地时需同步修改相关决定与[交互规格](../../product/interaction.md)，不能把本文的建议当作已接受的架构变更。

## 当前实现与根因

| 范围 | 实现依据 | 只读结论 |
| --- | --- | --- |
| 两个开始菜单来源 | [WindowsApplicationPlatform](../../../src/platform/windows/windows_application_platform.cpp) 的 `scanApplications()` | 依次枚举 `FOLDERID_Programs` 与 `FOLDERID_CommonPrograms`，分别把文件夹及子项加入快照。 |
| 目录身份与去重 | [application_platform.cpp](../../../src/platform/application_platform.cpp) 的 `applicationTargetId()`、`normalizedApplications()` | 文件夹与快捷方式均按规范化绝对路径生成 id；目前只按 id 去重。两个不同根中的同名文件夹不会合并。 |
| 目录显示名称 | [windows_application_platform.cpp](../../../src/platform/windows/windows_application_platform.cpp) 的 `scanShortcutDirectory()`、`displayName()` | 快捷方式使用 Shell 正常显示名，文件夹直接使用 `WIN32_FIND_DATAW.cFileName`。系统文件夹的英文物理名因此直接进入界面。 |
| 层级导航 | [ApplicationBrowserModel](../../../src/services/application_catalog.cpp) | 当前层通过 `parentFolderId` 过滤，展开栈保存文件夹 id。合并目录必须同步重映射子项父身份，否则子项会丢失或散落在另一份目录中。 |
| 固定入口 | [Main.qml](../../../qml/Main.qml)、[PrototypeService](../../../src/services/prototype_service.cpp)、[MenuController](../../../src/app/menu_controller.cpp) | 每项右侧有独立图钉，右键列出分组。服务的空 `groupId` 默认选择首组；取消固定接口以 `tileId` 为入口。 |
| A–Z 标签 | [Main.qml](../../../qml/Main.qml) 的应用区域标题行 | 纯文本 `Label`，无交互。真正分类入口是列表内的分类按钮。 |
| 文件夹提示滞留 | [Main.qml](../../../qml/Main.qml) 的应用 delegate | 右键先以鼠标原因取得焦点，文件夹不打开固定菜单；提示条件为 `hovered || activeFocus` 且无超时，移开后焦点仍可使原提示常驻。 |
| 返回箭头与空目录 | [Main.qml](../../../qml/Main.qml) 的 `folderBackButton`、`emptyCatalogHint` | 返回使用文本字符 `‹`，受字体字形位置影响；空态填满整个 ListView，与其文件夹 header 占用相同区域。 |

2026-10-23 在本机对两个 Programs 根进行只读文件系统检查，均找到 `Accessibility` 与 `Accessories`。两个来源各自的 `desktop.ini` 为这两个名称提供相同的资源引用：

| 物理目录名 | 双方的 `LocalizedResourceName` |
| --- | --- |
| `Accessibility` | `@%SystemRoot%\system32\shell32.dll,-21840` |
| `Accessories` | `@%SystemRoot%\system32\shell32.dll,-21761` |

这证明本机所报重复目录确有两个实际来源，结合当前按绝对路径生成目录身份的实现，可解释同时显示两项。该检查没有修改系统目录、属性、资源或运行实例，也未枚举或公开用户的完整应用清单。资源引用存在不等于已验证产品能显示相应本地化名称。

## 同类软件与系统 API 依据

Open-Shell 官方 `ProgramsTree.cpp` 的 `AddFirstFolder()`／`AddSecondFolder()` 分别接入两来源，以大写父相对解析名称的 hash 匹配同名项，失败才回退显示名；合并项保留 `pItemInfo2`。`GetFolderItems()` 展开时继续枚举两来源，根层使用 Programs／CommonPrograms。此处是源码事实，不表示 XDStarter 已采用相同策略。[官方源码](https://github.com/Open-Shell/Open-Shell-Menu/blob/master/Src/StartMenu/StartMenuDLL/ProgramsTree.cpp#L643-L918)

微软把 `SIGDN_NORMALDISPLAY` 定义为通常适合界面的显示名，`SIGDN_PARENTRELATIVEPARSING` 则用于解析，不适合直接展示。[SIGDN 文档](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/ne-shobjidl_core-sigdn) `IShellItem::GetDisplayName()` 通过指定 SIGDN 取得名称，返回的内存由调用方释放；项目现有 `displayName()` 已使用 RAII 处理。[GetDisplayName 文档](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ishellitem-getdisplayname)

若 Shell 正常显示名在目标 Windows 环境仍未返回所需名称，可另行验证 `SHGetLocalizedName()` 的资源信息和 `SHLoadIndirectString()` 的间接资源解析。前者不直接返回最终文案，后者支持以 `@` 开头的系统资源引用；它们只是候选补充，不是本轮已确认必须引入的依赖或已实现的回退路径。[SHGetLocalizedName](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shgetlocalizedname)、[SHLoadIndirectString](https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-shloadindirectstring)

这些外部资料于研究日期核对。Open-Shell 链接指向可变的 master，若后续实现依赖具体行为，应在 PR 中记录复核时的上游提交；本项目只参考行为，不复制其代码或引入其运行组件。

## 待实现的目录与身份方案

### 合并文件夹，保留实际目标

建议在 Windows 平台枚举阶段建立同一快照内的来源表，以“相对于 Programs 根的规范化路径”匹配文件夹，匹配时忽略 Windows 路径大小写。用户来源先枚举，公共来源补充；同一逻辑文件夹只展示一项，其子项集合来自两侧目录。嵌套目录采用相同规则，不能只合并根层。

相对路径用于内部合并，不使用本地化显示名作为键。两个不同真实相对路径即使具有相同显示名称，也不能据此合并；不同路径的同名叶目录也不能跨父级合并。重解析目录仍按原约定跳过，不为合并进行额外递归或跟随链接。

在不改变现有目录身份校验的实现选择中，可保留首个可用实际文件夹的 id、路径与图标来源，把另一来源目录 id 映射到代表 id，再统一重映射其子项 `parentFolderId`。来源信息仅存在于目录快照，不写入布局。代表来源消失时可在下次扫描重建代表；已展开目录若失效，由现有 `reconcile()` 回到有效层级，不使用过期路径继续操作。若另行采用与来源无关的虚拟目录 id，必须同时调整 `isValidCatalogEntry()` 与导航约束，并补充 ADR，不能让虚拟 id 绕过路径校验。

合并先限定为目录层级的联合呈现。真实快捷方式继续按规范化完整路径保留身份；AUMID 继续使用其既有身份。不能按最终可执行文件、显示名或图标合并快捷方式，以免丢失参数、工作目录与用户命名。若后续要求同名文件项去重，应单独明确完整目标等价和优先级规则，并验证对已固定磁贴的影响；本研究不推断两个同名快捷方式必然等价。

两个来源部分不可读时，联合视图保留可读内容和现有结构化警告；全部失败不能变成成功空目录。扫描上限仍约束实际读取工作量，不能因为合并减少可见数量而无限枚举。

### 本地化与稳定身份分离

建议文件夹也通过现有 `shellItem()` 和 `displayName()` 获取 `SIGDN_NORMALDISPLAY`，Shell 失败或返回空名时回退物理文件名。合并时若一个来源只能回退为物理名、另一来源成功取得不同显示名，可优先选择成功取得的显示名，同时保持来源身份与路径不变。具体成功／回退信息应在平台内保留，不能靠硬编码英文目录名推断。

分类和排序使用最终显示名；目录身份、快捷方式身份、启动目标和已固定引用不因系统语言或展示名称改变。不要写死“Accessibility → 辅助功能”等翻译表，也不要修改系统 `desktop.ini`、目录属性或目录名称。目录本身继续不是可启动或可固定的应用目标。

## 待实现的应用列表交互

固定入口按 Issue #48 改为整行右键与拖动；删除逐项图钉及其预留空间，也删除无交互的 A–Z 标记。右键文件项显示“固定为磁贴”或“取消固定磁贴”，默认固定至当前顺序最后一个分组，无额外分组选择。显式拖到某组时可将落点作为目标，但拖动预览和提交必须共用规划，不能在拖动开始就永久创建磁贴。

取消固定应以应用目标身份找到相关磁贴，而非依赖当前目录行索引。恢复布局可能让多个磁贴引用同一目标，需明确操作处理这些引用的一致语义，保留其他目标及其磁贴，并继续遵守只读和导入提交期保护。目录项不能因打开菜单或拖动误变成可固定目标。

提示条件应区分真正 hover 与 Tab 导航焦点，鼠标右键产生的焦点不单独维持提示。内部菜单、分类面板、拖拽、目录切换和窗口隐藏均应抑制并清理旧提示，避免多个 delegate 各自保留一份提示。目录类型文案应明确为文件夹，软件内文案按 Issue #48 不使用间隔号作为分隔符。

文件夹标题／返回行建议放在列表内容区之外，空态只填充其下方视口。返回箭头采用固定 viewBox 的 SVG 置于按钮中心，避免字体字形基线影响；仍保留现有目录切换动画、键盘返回和辅助名称。

## 有效测试与验证边界

| 层次 | 建议覆盖 | 能证明的边界 |
| --- | --- | --- |
| Windows 隔离扫描 | 两个受控根中的同相对路径目录、嵌套目录、大小写、两侧互补子项、同显示名不同路径、来源移除和部分不可读 | 真实枚举和平台合并语义，不操作用户开始菜单或启动用户应用。 |
| 目标身份回归 | 同 exe 不同参数／工作目录的快捷方式、相同 id 的重复输入、AUMID、已固定旧目标 | 合并不破坏 ADR-0004 的路径与启动语义，不能仅检查可见数量减少。 |
| 本地化隔离样例 | 系统 Shell 支持的本地化目录配置、普通目录、缺失资源、Shell 失败回退、名称改变后身份保持 | 实际 Shell 显示名与身份分离。资源引用存在或伪平台返回中文不能替代真实 API 验证。 |
| 服务／controller | 默认最后组、排序后的最后组、满组原子拒绝、重复固定、同目标多磁贴取消固定、只读与导入提交期 | 布局修改和持久化语义，与界面菜单项是否显示分开验证。 |
| QML 交互 | 图钉不存在、整行可用、两种右键文案、拖动阈值、有效落点、外部落点、Esc、已固定目标及满组 | 真实 Qt 事件至模型的行为；仅调用 JavaScript 辅助函数不能替代拖动事件覆盖。 |
| 提示生命周期 | 右键文件夹后移开、移至第二项、Tab／鼠标焦点分别测试、弹层／切目录／刷新／隐藏清理 | 原提示不常驻，同时至多展示预期提示，不以固定等待时间代替状态断言。 |
| 文件夹布局 | 空目录、长标题、最小窗口高度、100%／150% DPI 下标题／返回／空态不重叠和图标居中 | 几何断言配合必要离屏截图；不把伪平台、离屏截图当作真实 Windows 目录兼容性通过。 |

优先扩展现有 [平台测试](../../../tests/platform/application_platform_tests.cpp)、[目录服务测试](../../../tests/services/application_catalog_tests.cpp)、[布局服务测试](../../../tests/services/prototype_service_tests.cpp) 和 [Qt 界面测试](../../../tests/ui/tst_prototype_window.cpp)。不为纯标签删除编写镜像实现的测试；已有交互测试应删除旧图钉依赖并验证新的完整入口。仅在单元／隔离测试无法覆盖的必要边界使用实际桌面验收，且不能把本次只读研究记为上述方案已构建或已通过测试。
