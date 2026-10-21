# 测试策略与验收

## 当前自动检查

`python3 scripts/check_repository.py` 检查本地文档/图片链接、JSON/SVG/PNG 基本有效性、当前设计尺寸、Logo 透明通道及 SVG 网格几何。该检查不运行 Qt，也不代表 Windows 原型通过。

GitHub Actions 在 push/PR 上运行相同检查。设计重建后应目视查看 PNG，核对字体、应用图标、长名称、间隙及辅助功能信息的表达。

## 当前开发验证：磁贴显示名称与搜索别名（Issue #9）

2026-10-21，[#9](https://github.com/xiaodaozhi/xdstarter/issues/9) 剩余的磁贴显示名称编辑已接入右键／Shift+F10 菜单、内部命名弹层、controller、布局与搜索。当前写入 schema 3，严格兼容读取 schema 1/2；旧文件只读取时原字节不变，首次保存保留旧版原字节备份。每枚磁贴可有独立覆盖，原 target 名称与启动描述不变。契约见 [ADR-0011](adr/0011-tile-display-names.md)；此处只记录本分支已执行的检查，不沿用下方旧版本的原生窗口、托盘及产品重启结论。

### 正式构建与覆盖范围

正式入口 `./scripts/build-windows.ps1 -Preset app-release` 使用 Qt **6.8.2**、MSVC **19.29.30153.0／v142**、Windows SDK **10.0.19041.0**。合入 PR #38 前的日志 `build/issue9-tile-names-delivery-final.log` 完成 Release、QML lint 与 **12/12 CTest（27.80 秒）**；GUI **47/47**，host **34/34**。QML lint 没有诊断；没有将 Qt SDK 既有编译警告写为整个构建零警告。文档／素材检查通过，255 项开发素材未重建；`tests/tooling` **13/13** 通过。

| 合入 PR #38 前的显示名称程序 | SHA-256 |
| --- | --- |
| 产品 `xdstarter.exe` | `2353d99abf657662b4a82bccd9f7980f3b3e461326031b9f490bf6d5abdef464` |
| GUI `xdstarter_ui_tests.exe` | `c7eb92cdf7d492d2bf42ed8c6d3cbf222340c17ea11619c4a72118100d22ce1a` |
| host `xdstarter_desktop_window_host_tests.exe` | `3933c64f29cccf46e437874d72d0ba872adf4442c15da6363f89b17615a12a69` |

合入 main `2303aef`（应用权限／目录错误 PR #38）后，`build/issue9-tile-names-main-sync.log` 再次完成 **12/12 CTest（30.39 秒）**。独立只读审查未发现已确认 P1/P2，并建议补足命名弹层的通用收起保护及返回焦点断言；随后只增加测试：`internalPopupsGuardAndCleanup` 包含 `tileNameDialog`，提交、恢复原名、Esc 和取消按钮结束后，实际磁贴必须重新取得 active focus。追加后的 `build/issue9-tile-names-focus-guard.log` 完成 Release、QML lint 与 **12/12 CTest（30.57 秒）**，GUI **47/47**、host **34/34**。仓库 **610 文件／255 素材** 与 tooling **13/13** 通过。产品代码未因追加焦点断言改变，最终程序如下；普通离屏 host 的 `raise()` 不支持及有意超时场景警告不作为真实托盘验收。

| 合入 PR #38 并补足焦点断言后的程序 | SHA-256 |
| --- | --- |
| 产品 `xdstarter.exe` | `269c800746293a35e75e8afd0ed79eca99b0cd702daa17bd1da8290e531363ec` |
| GUI `xdstarter_ui_tests.exe` | `d0e025ff89f7c2515e801e8c632edb5c2d095f54b63fbdd22c456f2ca9c4ae13` |
| host `xdstarter_desktop_window_host_tests.exe` | `ccca0159bc9982b9995609d87510bb7a13326d7f20e403fd46cb90bd11f54b1a` |

服务与保存检查覆盖 trim、空白／129 字符／控制字符／换行／行段分隔符／无效 UTF-16 拒绝、128 代码单元 emoji 边界、未知 id、只读与失败不变性；名称修改和恢复保留目标数组、稳定标识、坐标及尺寸。schema 1/2 的新字段拒绝、旧主配置原字节备份、schema 3 严格字段和重建恢复均有检查。搜索保留每目标 16 个普通名称的原预算，另以整个索引共享的 512 条预算覆盖所有有效磁贴名称；首项、末项、全局超限及恢复后移除旧别名有明确断言。

新增三条 GUI 用例覆盖键盘菜单／弹层、PlainText 标签与完整辅助名称、可见 128 限制、预编辑 Enter／Esc、取消／收起丢弃草稿、只读禁用、同目标两枚磁贴的独立名称，以及原名／覆盖的名称、拼音与首字母搜索。目录名称变化仍优先用于搜索结果，已保存原名与其余有效覆盖可继续查找。真实保存文件后重新创建 QML engine、controller 与服务，逐对象比较完整已保存快照；这属于同一测试进程内重建，不是产品独立进程重启。普通 CTest 的 GUI 为 `offscreen/software` 受控夹具；本分支没有另行运行原生桌面窗口、托盘或真实应用独立进程验收。

### 开发中的失败与复测

- `build/issue9-tile-names-initial.log` 在受限上下文的空编译器探针出现 C1902，尚未编译产品；改在正式 MSVC 构建上下文配置后通过，没有改变工具链版本。
- `build/issue9-tile-names-formal.log` 为 **10/12 CTest，GUI 46/47**：新服务用例误用不存在的样例 tileId；新 GUI 恢复用例比较未包含 controller 默认主题的 service 快照。修正为实际样例 id 与完整已保存 JSON。该轮已有图标加载路径的 QtQuickPixmap 警告保留记录，未计作新增 QML 引擎错误。
- `build/issue9-tile-names-regression.log` 为 **11/12，GUI 46/47**：新 GUI 在重建后首次呈现回调完成前打开菜单，被既有的呈现清理关闭。用例改为等待真实 `frameSwapped` 后发送键盘；菜单可见、恢复入口及布局断言保持。随后 `build/issue9-tile-names-final.log` 已达 12/12；最后仅修改用户错误文案，形成上表程序。
- `build/issue9-tile-names-delivery.log` 为 **11/12**：未修改的 `platform.applications` 夹具中，Shell 返回成功、标记文件存在，但随后的文件打开失败，连带参数与工作目录断言失败。单次原因未确定，没有据此修改平台实现或放宽严格断言。带明确 Qt DLL 路径的独立复测 `build/issue9-platform-launch-with-qt.log` 为 **1/1（1.67 秒）**；同一程序随后完成上面的正式 **12/12**。另一次手动 CTest 未设置 Qt DLL 路径，30 秒超时，保留为 `build/issue9-platform-launch-recheck.log`，属于启动器环境错误。

本轮新功能的原生窗口交互、真实托盘与独立产品进程恢复需要在最终集成程序上协调验收；上述离屏及受控服务证据不宣称这些范围已经通过。下方 #29 及更早章节保留各自二进制身份与历史边界。

## 应用目录异常验收（Issue #7）

2026-10-21，本轮在从 `61c9376` 建立的独立工作树补充 [#7](https://github.com/xiaodaozhi/xdstarter/issues/7) 的权限与不可用目标验收，并修正实际发现的分类问题。仅使用 build 内新建的快捷方式、复制的受控 helper 和新目录，不改真实开始菜单、不卸载用户应用、不改日常布局或系统权限。普通产品窗口没有在本轮操作；此前真实目录、画图启动及失败窗口证据继续保留原版身份，不改标为当前程序。

正式 `app-release` 使用 Qt **6.8.2**、MSVC **19.29.30153.0／v142**、链接器 **14.29.30153.0** 与 SDK **10.0.19041.0**。最终日志 `build/issue7-build-delivery.log` 的 Release、QML lint、**12/12 CTest（31.83 秒）** 通过；其中原生平台 2.48 秒，后台目录服务 1.73 秒，既有离屏 GUI 44/44 和 host 34/34 通过。离屏 GUI 没有读取真实目录，也不等于当前产品的真机交互验收。

| 最终程序 | SHA-256 |
| --- | --- |
| 产品 | `7eb32ad1a33c50ad075a47100f047bb40fb882fdfaf4182ea7906fa085404f95` |
| 应用平台测试 | `20e93c8137ed8570337738aa76e94301d6b4e4439e3ce51b9b3721130f87f337` |
| 后台目录服务测试 | `3fbb02b994ecb620b57501dcddad1af395b74eadf35e3dab9eebe163747a2528` |
| 受控启动 helper | `4751d8f0f22f4fc5c752b46d93ec40a97e982e71c4932538736002009e7b4c16` |

### 真实 Windows API 与隔离 DACL

同一最终平台测试在 Windows kernel `10.0.26300` 下独立运行，退出 **0**；[公开结果](validation/issue-7/catalog-errors.json)包含程序／源码散列、结构化错误和恢复结果，不含机器路径或真实应用清单。完整本地日志为 `build/validation/issue7/native-platform.log`，原始 JSON 位于 `build/validation/issue7/final/`。

| 受控场景 | 实际断言与结果 |
| --- | --- |
| 中文、空格及 `&` 的快捷方式 | 仍由原 `.lnk` 启动构建的 helper，中文参数、含 `&` 的单个字面参数和工作目录逐项一致；不同参数的快捷方式保留独立身份，原生 Shell 图标可取。 |
| 缺失文件与未注册包 | 缺失目标 `target-missing`，删除 fixture `.lnk` 后为 `shortcut-missing`；匿名、语法有效但未安装的 AUMID 返回 `application-missing`，启动失败。这验证卸载后注册项缺失的等价状态，没有实际卸载任何应用。 |
| `.lnk` 读拒绝 | 只对新建快捷方式增加当前用户的 `FILE_READ_DATA` deny；原生打开返回 `ERROR_ACCESS_DENIED = 5`，可用性及启动均返回 `access-denied`；恢复原 DACL 后可用性恢复。 |
| 普通 `.exe` 执行拒绝 | 仅限制复制的受控 helper；元数据仍可读，原生 `FILE_EXECUTE` 打开返回 5，明确执行权限预检及启动均返回 `access-denied`，helper 没有产生启动标记。 |
| 关联文档保持原语义 | 新建 `.txt` 被禁止 `FILE_EXECUTE` 后，其 `.lnk` 可用性仍通过，没有打开其关联程序。重解析文件／应用别名不加入普通可执行文件预检。 |
| 根目录及子目录读拒绝 | 原生 `FILE_LIST_DIRECTORY` 打开返回 5。可读根下有拒绝子目录时保留 1 个有效项目与 warnings；拒绝根与可读根混合时仍成功并保留有效结果；全部拒绝且根重复时 `success=false`，不会伪装成成功空目录。恢复 DACL 后受限子目录正常扫描。 |

ACL 安全门先用文件句柄取得实际路径，再确认它是当前绝对 build 工作目录中新建 fixture 的非重解析子项；不会通过 Qt 的 `.lnk` 目标解析给外部程序修改权限。原 DACL 由 RAII 保存并恢复，最终 `allFixtureDaclsRestored=true`、`fixtureCleaned=true`。权限修改或原生拒绝不能建立时测试失败，不跳过、不降级为模拟通过；恢复失败时保留隔离目录并报告失败。

服务的独立回归 **12/12，1.743 秒**，见本地 `build/validation/issue7/catalog-service.txt`。这部分明确使用注入平台：全失败刷新保留已有目录；部分成功保留可读项并显示错误标志；`shortcut-missing`／`access-denied`／`application-missing` 三类固定目标均保留描述和布局，启动失败且不进入底层 launch，仍可取消固定，平台调用不在 GUI 线程执行。它与上面的真实 DACL 证据互补，不能把假平台结果称为真实系统权限验收。

### 失败记录与边界

首轮 `build/issue7-build-initial.log` 为 **11/12 CTest（30.10 秒）**。ACL 安全门最初调用 `QFileInfo::canonicalFilePath()`，Windows 会解析 `.lnk` 的目的程序，因超出 fixture 路径被安全门拒绝；随后改用实际文件句柄核对快捷方式本体。初始原始结果 `build/validation/issue7/initial/catalog-errors.json` 的 `passed=false` 保留。

第二轮 `build/issue7-build-diagnostics.log` 仍只有执行拒绝分类失败：原生执行打开返回 5，但本机 Shell 将同一拒绝折为 `E_FAIL`／“未指定的错误”。没有把一般 `E_FAIL` 一概解释成权限拒绝，而是增加仅针对普通 `.exe`／`.com` 文件的明确执行权限预检；关联文档、重解析文件／应用别名及其他打开错误继续交给原 Shell 路径。中间的 `build/issue7-build-final.log` 为 12/12（27.76 秒）；补充文档关联保护后再次正式构建，最终结果以 delivery 日志及上表散列为准。

权限预检不是授权或安全边界，外部 ACL 仍可能在检查之后变化，最终启动结果继续由 Shell／应用激活接口返回。本轮没有新增原生产品窗口的权限提示交互、真实安装／卸载监听、重绑定目标、完整 Windows 版本矩阵、远程桌面或发行验证；原有真实窗口与本轮平台／服务结果分别记录。

## 历史：高保真界面落地与视觉验收（Issue #29）

2026-10-21，[#29](https://github.com/xiaodaozhi/xdstarter/issues/29) 将 [v2 修订 2.2](../design/README.md) 统一为可运行界面：顶部搜索与编辑／更多／收起入口，240 DIP 应用目录与分组磁贴正文，主题控件、菜单、弹层及重要反馈。正常浏览不常驻品牌大标题和调试底栏；共享磁贴保持 48 DIP 小格、8 DIP 间隙、48／104／216×104 DIP 三档几何，布局 schemaVersion 2 不变。当前设计 SVG/PNG 仅是布局基准，下面区分构建、离屏状态、原生像素与真实产品证据。

### 正式构建与自动检查

正式入口 `./scripts/build-windows.ps1 -Preset app-release` 使用 Qt **6.8.2**、MSVC **19.29.30153.0／v142**、Windows SDK **10.0.19041.0**。视觉及真机验收前的 `build/issue29-build-rounded-overlay.log` 为 **12/12 CTest（38.63 秒）**。后续仅修正跨组滚动用例的几何前提，最终 `build/issue29-build-ci-scroll-precondition.log` 再次完成 Release、QML lint 和 **12/12 CTest（30.49 秒）**，GUI **44/44**、host **34/34**。产品及 host 散列没有变化。此次 QML lint 没有诊断：`Qt.inputMethod.reset` 使用局部类型抑制，搜索／输入法运行回归仍保留；Qt SDK 头文件的既有 MSVC C4702 警告没有消除，不把它写为整个构建零警告。

| 程序及证据版本 | SHA-256 |
| --- | --- |
| 产品 `xdstarter.exe` | `34f18e80c19b5d81356ad71dec76c6fc7870c32c34e26d86ca2cf46d9131cb23` |
| 原生视觉证据 GUI `xdstarter_ui_tests.exe` | `35c4a04c00e72703ef986cb06d66c2fd112e244d4e7bfc477b082c4e21b184ff` |
| 滚动用例修正后最终 GUI | `d36940ee0efa19f77c272c84eda1d96cb95d8357276676c00f68d87aba772085` |
| host `xdstarter_desktop_window_host_tests.exe` | `d5cbb03068876e11dccbe65df382256ab85c1571e4c14934dad3ac4629999785` |

本轮新增三条 GUI 用例，覆盖完整搜索工具栏及更多菜单入口、深浅主题与浏览／编辑切换、共享网格和图标／文字对比；结构化错误反馈在定时器结束后仍可见，失败后重试可清零；磁贴名含 `<b>literal</b>` 等标记时，标签与有界提示仍按 PlainText 原样显示。原有目录、搜索、拖放、分组、只读保护、持久化和焦点回归继续执行。布局改变只调整浏览容器的占用高度，编辑使用 service 的完整拖放空间，磁贴坐标和尺寸前后相同。

包含公开验收工件的仓库文档／素材检查通过，255 项开发素材及其清单未重建；`tests/tooling` **13/13** 通过（本地 `build/issue29-tooling-tests.log`）。独立只读审查发现 TileButton 标签及提示的 AutoText 遗漏，已显式改为 PlainText、提示宽度上限 380 DIP，并补充字面文本回归；其余主题、错误状态、内部菜单、分组高度与滚动路径未留下已确认 P1/P2。

### 视觉证据 GUI 的离屏状态抓帧

显式 `--visual-acceptance-probe` 在 `offscreen/software` 下取得 **22/22** 张状态图并通过状态断言，`errors` 与 `qmlWarnings` 均为空，DPR 为 1。深浅主题各含浏览、更多菜单、搜索有／无结果、编辑、组名弹层、分组菜单、失效、窄面板、空布局／目录及加载 11 状态；运行方法见[开发环境](development.md)。本地工件为 `build/validation/issue29/offscreen/` 中的 PNG 和 `visual-acceptance-probe.json`；公开[离屏摘要](../design/validation/issue-29/offscreen-summary.json)保留 22 帧断言与元数据，删除未提交 PNG 的截图字段，并明确图片仅保存在本地 build 目录。

夹具复用产品 QML、controller、网格与后台目录接口，只使用匿名内存目标、合成图标和隔离快照；不读取用户布局、枚举已安装应用、启动应用、注册托盘或发送全局输入。抓帧前后检查状态并等待新帧，记录逻辑尺寸、实际 DPR、物理图片尺寸、关键区域和磁贴矩形。离屏原始 capture 没有 alpha；转换 RGBA 仅用于保存统一 PNG 格式，不能据此宣称透明角、原生圆角窗口或 DWM 合成通过。

### 原生矩阵与产品验收的版本边界

早期版本曾以 GUI `e71627b56b0c715c83108f77dc76dd7e086f6e2d64e7bceda3bc078ec005effc`、对应当时产品 `b449…` 执行五档完整 GUI，每档 44/44；其结果属于图标等待及圆角蒙层修正之前的程序，历史矩阵保留为 `native-matrix-before-icon-wait.json`。另有 GUI `824edb6336f2501267f06047e0f23bb2f824c2d5972b6c762c231e156db3ac9f` 的 `renderedGroups` 像素专项，保留为 `native-matrix-before-rounded-overlay.json`。两者都不作为上表最终 GUI 的五档完整回归证据。

原生视觉证据 GUI `35c4a04c…` 的五档像素专项 **全部通过**，结果见[原生矩阵](../design/validation/issue-29/native-matrix.json)。此次仅运行 `renderedGroups`，不是每档重新执行完整 44 项 GUI。每档初始化／抓帧／清理共 **3/3**；深浅主题 × 三组得到 6 份 group JSON，每份包含完整窗口与网格裁剪两张 PNG，共 12 PNG，像素扫描均通过。完整本地工件位于 `build/validation/issue29/final-native-pixels/`，公开矩阵保留程序 hash、实际 DPR、图形 API 与结果。

| Qt 有效缩放 | 实际 DPR | 图形后端 | QTest | 六组像素扫描 |
| --- | --- | --- | --- | --- |
| 100% | 1 | Direct3D 11 | 3/3 | 通过 |
| 125% | 1.25 | Direct3D 11 | 3/3 | 通过 |
| 150% | 1.5 | Direct3D 11 | 3/3 | 通过 |
| 175% | 1.75 | Direct3D 11 | 3/3 | 通过 |
| 200% | 2 | Direct3D 11 | 3/3 | 通过 |

本次用 `QT_SCREEN_SCALE_FACTORS` 将两块已连接显示器同时设为目标 DPR，`QT_SCALE_FACTOR=1`，逐图核对实际 DPR；只修改测试进程环境，没有修改系统显示设置。它验证 Windows QPA 下的有效缩放、光栅边界及共享几何，不验证真实混合 DPR 跨屏迁移或系统缩放变更。真实托盘、应用目录和独立产品重启也不在匿名像素夹具范围内。

### 同一最终 GUI 的原生 22 状态

同一 `35c4a04c…` GUI 在 Windows QPA／Direct3D 11、Qt 6.8.2、kernel `10.0.26300` 下运行显式视觉探针，**22/22 通过**；所有帧实际 DPR 为 1，`originalCaptureHasAlphaChannel` 与四角透明检查均为真，`errors`、`qmlWarnings` 均为空。公开[完整原生元数据](../design/validation/issue-29/visual-acceptance-probe.json)包含状态与新帧断言、图标就绪后的完整窗口帧、像素／逻辑尺寸、区域与磁贴几何，以及最终产品和 GUI hash；[源文件摘要](../design/validation/issue-29/source-digests.json)与证据一同保留。

| 状态 | 深色原生 PNG | 浅色原生 PNG |
| --- | --- | --- |
| 默认浏览 | [深色](../design/validation/issue-29/dark-browse.png) | [浅色](../design/validation/issue-29/light-browse.png) |
| 更多菜单 | [深色](../design/validation/issue-29/dark-more-menu.png) | [浅色](../design/validation/issue-29/light-more-menu.png) |
| 搜索结果 | [深色](../design/validation/issue-29/dark-search-results.png) | [浅色](../design/validation/issue-29/light-search-results.png) |
| 搜索无结果 | [深色](../design/validation/issue-29/dark-search-empty.png) | [浅色](../design/validation/issue-29/light-search-empty.png) |
| 编辑布局 | [深色](../design/validation/issue-29/dark-edit.png) | [浅色](../design/validation/issue-29/light-edit.png) |
| 组名弹层 | [深色](../design/validation/issue-29/dark-group-name-dialog.png) | [浅色](../design/validation/issue-29/light-group-name-dialog.png) |
| 分组菜单 | [深色](../design/validation/issue-29/dark-group-menu.png) | [浅色](../design/validation/issue-29/light-group-menu.png) |
| 失效目标 | [深色](../design/validation/issue-29/dark-unavailable.png) | [浅色](../design/validation/issue-29/light-unavailable.png) |
| 窄面板 | [深色](../design/validation/issue-29/dark-narrow.png) | [浅色](../design/validation/issue-29/light-narrow.png) |
| 空目录／布局 | [深色](../design/validation/issue-29/dark-empty.png) | [浅色](../design/validation/issue-29/light-empty.png) |
| 受控加载 | [深色](../design/validation/issue-29/dark-loading.png) | [浅色](../design/validation/issue-29/light-loading.png) |

已目视核对完整图标、工具栏、分组、菜单、深浅主题与组名弹层；修正后的 Dialog 蒙层只覆盖圆角可见面板，外窗透明角保持。搜索空状态的重复文案已去除，探针在输入查询前通过 Ctrl+F 聚焦搜索，避免残留的更多菜单焦点影响状态。抓帧和输入都局限于匿名测试窗口，不含用户桌面背景、真实应用列表或目标路径；这一原生状态证据没有替代真实产品窗口交互、托盘恢复或重启布局验收，也不扩大为材料或完整系统兼容通过。

### 真实产品交互、托盘与独立进程恢复

同一最终产品 `34f18e80…`、源码提交 `57ed29e`，在 Windows kernel `10.0.26300`／Qt 6.8.2／Direct3D 11 下使用独立验收布局运行两个进程，没有修改日常用户配置。公开[产品验收摘要](../design/validation/issue-29/product-acceptance.json)记录程序身份、进程阶段、布局断言和用户确认，不包含 PID、机器路径、真实目标描述或私人桌面截图。

首进程从浅色主题开始，实际完成 `huatu` 查找唯一画图结果、更多菜单切换深色、清空搜索恢复浏览、进入编辑、新建分组弹层后按 Esc 丢弃草稿。内部菜单和弹层操作没有误收起主窗口；将 Epic 磁贴从原分组跨组拖到学习分组的 `(column 0, row 0)`，再从 standard 切为 wide，实际保存。首个受限启动上下文中，部分打包应用图标显示为通用占位，`trayRecovery: false`；备用激活路径可以恢复窗口并保留编辑状态。这一环境差异保留记录，不将它写为该启动上下文的图标／托盘通过。

第二进程在普通桌面启动上下文运行同一二进制和同一布局，实际恢复深色主题、非编辑状态和空搜索；学习分组位于首组，Epic 为 wide，原另外三枚磁贴仍在原分组。真实系统图标，包括画图等打包应用图标，显示正常，日志记录 `trayRecovery: true`。用户最后亲自点击通知区域图标并正常退出，确认“**恢复正常，查询保留，已从菜单退出**”；此次确认覆盖真实托盘恢复及当前进程查询保留，不是受控托盘适配器或合成输入结果。

| 独立产品进程 | 启动时间（Asia/Shanghai） | 正常菜单退出时间 | 退出代码 |
| --- | --- | --- | --- |
| 首进程 | 2026-10-21 17:58:15 | 18:02:08 | 0 |
| 重启进程 | 2026-10-21 18:02:14 | 18:08:16 | 0 |

保留进程句柄的启动器独立记录两次退出。逐对象断言 `first-after == restart-before == restart-after`；目标描述、稳定目标／磁贴／分组 id、组名与顺序全部保留，schemaVersion 仍为 2。相对验收起始快照，仅主题改为 dark，以及同一 Epic 从原组 `(0,0)` standard 移至学习组 `(0,0)` wide；其余三枚磁贴的目标、位置与尺寸不变。查询只在进程内收起／唤回时保留，重启初始为空，没有写入布局文件。完整布局、真实目录、进程日志及桌面背景截图仅存本地 `build/validation/issue29/product/`。

上述首受限与普通桌面两个启动上下文的差别不证明所有启动方式均兼容。本轮没有重新验收 Win 键或开始按钮接管、Explorer 重启、远程桌面、物理系统缩放切换、显示器热插拔、屏幕阅读器及 Mica／Acrylic；前轮结果继续保留各自程序身份，父 #5／#6 与相关后续任务保持各自范围。

### 失败与修正记录

PR 初次 head `72d13d4` 的[附加 MSVC 2022 检查](https://github.com/xiaodaozhi/xdstarter/actions/runs/39583500992/job/119166601916) 为 11/12 CTest、GUI 43 通过／1 失败：跨组用例断言 `contentY > 0`，却没有先建立并等待目标必须离开视口的几何前提。新布局的浏览／编辑组高不同，原用例依赖默认尺寸及重排时机，CI 没有记录当时的实际目标矩形，不能进一步认定生产 Timer 有缺陷。修正仅在该用例缩到最小窗高，等待编辑网格为 216 DIP 并明确目标位于安全区之外；随后继续使用生产边缘滚动 Timer 和原来的非零滚动、单次跨组提交、坐标、15 枚磁贴及焦点断言。最终正式 CTest 及独立重复结果见[修正摘要](../design/validation/issue-29/ci-scroll-regression.json)。这项变更没有修改产品、QML 或显式视觉探针，原生图片保留实际抓帧 GUI `35c4a04c…` 和源码 `57ed29e` 的身份，不重标为新的 GUI hash；相同产品 `34f18e80…` 的真机证据仍保持准确。

- 初轮 `build/issue29-build-initial.log` 为 **11/12 CTest，GUI 40 通过／3 失败**：旧测试仍要求历史高亮色、260 DIP 目录及常驻品牌图标。核对修订 2.2 后调整这三条视觉期待，保留其可读性、入口及实际几何断言，没有把失败记为通过。
- 离屏探针初次缺少系统字体目录，保留 `offscreen-initial-fonts.json` 和启动日志。随后用空 TextInput 的 `boundingRect()` 衡量布局框，得到的是文本／光标 ink 边界而假失败；改用 `size()` 测布局外框，保留 `offscreen-ink-bounds-failed.json`。
- 原生首次新帧不保证异步图标已经 `Image.Ready`。探针增加显式图标就绪等待，再等待新渲染帧；先前 JSON 保留为 `offscreen-before-icon-wait.json`，不以带占位或迟到图标的帧充当最终状态图。
- 严格原生 alpha 检查发现深浅主题组名 Dialog 的默认模态蒙层覆盖整张透明外窗，**两张状态帧失败**，记录于 `native-dialog-alpha-failed.json` 和 `build/issue29-visual-native-final.log`。修复 `Overlay.modal`，蒙层仅绘制在同一圆角面板内；没有降低原生原始 alpha／四角透明断言，随后重新正式构建为上表最终程序。
- 最终五档专项的初版全局缩放因子按单屏基础 DPR 校准，测试窗口换屏后触发实际 DPR 不匹配；失败启动器／日志保留为 `build/issue29-native-pixels-dpr-location-failed.log`。改为当前测试进程的双屏同目标 DPR 覆盖，仍逐图检查实际 DPR；早期启动器参数失败另存 `build/issue29-native-pixels-wrapper-failed.log`。

本轮采用实色面板，当前 SVG 与 QML 字体为 Segoe UI Variable、中文使用系统回退。字体回退、真实应用图标与设计示意不同，不能要求匿名夹具和第三方示意图逐像素一致。系统缩放切换、热插拔、远程桌面、其他 Windows 构建、屏幕阅读器及完整 Mica／Acrylic 或接管矩阵不由本机状态图证明；相关父 #5／#6 和 Beta 范围继续按 Issues 跟踪。

## 历史：分组管理（Issue #9）

2026-10-21，[#9](https://github.com/xiaodaozhi/xdstarter/issues/9) 本轮交付编辑模式的新建、重命名、相邻排序及空组删除，保留至少一组。磁贴显示名称编辑仍在该 Issue 跟踪，本轮使用 `Refs #9`，不关闭整个 Issue；布局格式仍为 schemaVersion 2。

服务检查覆盖名称 trim、128 个 UTF-16 代码单元及 emoji 边界、控制字符或换行、重复名称、1/32 组边界、未知 id、非空组与最后一组保护、只读和失败不变性；检查排序仅改变组数组，保留磁贴目标、标识、格坐标与尺寸。已有 schema 1/2 读取规则保持兼容，新增保存／恢复检查组名、稳定 id 和顺序。

四条 GUI 用例覆盖创建／重命名／排序／删除、长名称省略与 PlainText、Shift+F10／Enter、IME 合成预编辑、取消／Esc／收起丢弃草稿、固定到新组、跨组鼠标拖拽、同文件重建恢复，以及分组变化不重建搜索索引、不抢用户已移到主题／目录／搜索控件的焦点。独立只读审查发现的后台刷新抢回旧分组焦点问题已修复并加入回归。

分组初版（追加 #28 定位修复前）的正式入口 `./scripts/build-windows.ps1 -Preset app-release` 使用 Qt 6.8.2、MSVC 19.29.30153.0/v142、SDK 10.0.19041.0，最终 Release、适用 QML lint 与 **12/12 CTest** 通过（18.96 秒），GUI **41/41**。最终日志为本地 `build/issue9-build-delivery.log`。保留一条既有 `Qt.inputMethod.reset` 的 lint 类型警告，运行行为由搜索及 IME 用例检查。

首轮日志保留 `build/issue9-build-initial.log`：编译及 lint 完成，但服务断言使用 Latin-1 包装中文名称，另有排序后焦点恢复失败，CTest 为 10/12、GUI 40/41；不把这次结果记为通过。修正中文断言，并根据 Qt focus 日志仅为待完成操作放行控件销毁后落入的布局焦点容器，第二轮 `build/issue9-build-final.log` 为 12/12、GUI 41/41（20.05 秒）。随后补查后台可用性刷新路径，在旧委托销毁前捕获实际聚焦的稳定 groupId，新增可用性 `true → false → true` 的严格回归，再冻结代码完成上述最终构建。

| 分组初版程序（定位修复前） | SHA-256 |
| --- | --- |
| 产品 `xdstarter.exe` | `e28629df2c58345e2ee8ed595d2ac40da43a9c7ba548328dee8767ac6917a866` |
| GUI `xdstarter_ui_tests.exe` | `77e5b88931522fe7978d47bd29adcfc442a7881223a64bec41aef562a0afd7b9` |

### Windows 原生窗口专项

同一分组初版 GUI 程序使用 Windows QPA / Direct3D 11 / Intel Iris Xe 执行四个新增用例，**6/6** 通过（含初始化与清理，5.437 秒）。Qt 测试事件直接送入测试窗口，没有发送全局键盘鼠标输入。覆盖菜单和命名弹层、IME 合成事件、跨组拖拽、删除保护、布局保存／重建及后台焦点回归；重建是同一测试进程内重新创建 QML engine 和服务，不是产品独立进程重启。

直接 `QQuickWindow.grabWindow()` 导出的[浅色](../design/validation/issue-9/groups-light.png)与[深色](../design/validation/issue-9/groups-dark.png)为 1028×728、DPR 1，已目视核对两组标题、空组提示、管理入口、编辑按钮和主题。[元数据](../design/validation/issue-9/groups-render.json)标注受控目录、程序 hash、结果与恢复边界。图中下方磁贴受正常滚动视口裁切，截图本身不作为完整磁贴几何通过证据；共享几何由既有像素检查及分组操作前后的布局断言覆盖。首轮本地启动器误将 `,txt` 作为 QTest 函数名，未执行功能用例；修正参数分组后执行同一程序，失败启动日志保留在 `build/validation/issue9-native/wrapper-initial.log`。

分组初版产品使用隔离布局读取281项真实目录，实际完成新建“工具 Tools”、固定Access及跨组拖到原组；原三枚磁贴与目标逐对象保持不变。命名弹层失焦后草稿未提交，窗口收起。用户随后发现首次跨屏托盘恢复时菜单底部越过任务栏，建立 #28；本次观察推翻该场景的通过预期，不能以先前受控窗口或旧CI代替实际验收。用户从托盘菜单退出初版，独立进程句柄记录PID43276于2026-10-21 15:50:53（Asia/Shanghai）退出代码0，证据仅保留本地build/validation/issue9-product/。修复版后续完成改名、排序及同文件独立进程恢复，证据见下方 #28 追加记录。

## 历史：隐藏窗口跨混合 DPI 唤回（Issue #28）

2026-10-21，分组实际验收发现 [#28](https://github.com/xiaodaozhi/xdstarter/issues/28)：96 DPI 隐藏窗口首次从 144 DPI 屏幕托盘唤回时，定位先于原生窗口迁移与 Qt DPR 更新。修复使用公开 Qt Windows screen 的 HMONITOR 映射目标屏幕；隐藏迁移后等待目标 DPR、保存的逻辑尺寸意图及实际原生物理尺寸一致，两次稳定采样后才定位和显示。总等待上限 1500 ms，取消、关闭、重入及迟到回调有回归；可见窗口在收起前同步保存最新尺寸，避免遗漏尚未送达的 resize 通知。机制见 [ADR-0007](adr/0007-custom-menu-chrome.md)。

最终源码 `550f5d4` 使用正式入口 `./scripts/build-windows.ps1 -Preset app-release`，Qt 6.8.2、MSVC 19.29.30153.0/v142、SDK 10.0.19041.0。Release、适用 QML lint 与 **12/12 CTest** 通过（38.45 秒）；GUI **41/41**，host **34/34**。最终日志为本地 `build/issue28-build-delivery-final.log`。既有搜索路径 `Qt.inputMethod.reset` 的 lint 类型警告保留，不将它写为新增错误或零警告构建。

| 修复后最终程序 | SHA-256 |
| --- | --- |
| 产品 `xdstarter.exe` | `079c5202f3ff01a53187354c0133b71124dafc674ba2acce505f0a030da28066` |
| GUI `xdstarter_ui_tests.exe` | `e03c5ad960bbce417aafc29397c1af4df69ad189c2ce61a5c432bfcba021fdfa` |
| host `xdstarter_desktop_window_host_tests.exe` | `5505b43baa4d7586d29bb35706e08e2e99ea5c6f8701c585b36825ed634d82cf` |

### 原生双屏独立测量

同一最终 GUI 的显式 `--native-screen-transition-probe` 在 Windows QPA / Direct3D 11、Intel Iris Xe、kernel `10.0.26300` 下实际运行，**六轮全部通过**，`errors` 与 `qmlWarnings` 为空。该程序不属于普通 CTest，需要两块真实不同 DPI 且能容纳默认面板的显示器；前提不满足报告不可用，映射错误明确失败。结果见[原生测量 JSON](../design/validation/issue-28/native-screen-transition-probe.json)。

| 实际目标屏幕 | 工作区（物理像素） | 默认面板（物理像素） | 外窗（物理像素） | 左侧／底部间距 |
| --- | --- | --- | --- | --- |
| DISPLAY1，144 DPI／Qt DPR 1.5 | `(0,0,2400,1528)` | `(12,496,1470,1020)` | `(-24,460,1542,1092)` | 各 12 px，即 8 DIP |
| DISPLAY2，96 DPI／Qt DPR 1 | `(252,-1080,1920,1032)` | `(260,-736,980,680)` | `(236,-760,1028,728)` | 各 8 px，即 8 DIP |

从低 DPI 隐藏窗口开始，高低屏交替三次往返；每轮独立调用 `GetWindowRect`、`GetDpiForWindow` 和 `MonitorFromWindow`，不是读取定位函数的输出。同步 `visibleChanged` 样本与 1000 ms 后样本的外窗／面板几何一致，均保持 1028×728 DIP 外窗、980×680 DIP 面板和每侧 24 DIP 阴影留白。六轮首样本均处于 Qt 已可见、原生不可见且尚未 exposed 的时刻；稳定样本额外要求原生可见、Qt 可见及 exposed 全为真。因此这项检查证明两个采样端点的几何与稳定后的真实可见性，不能单独声称连续逐帧没有跳动。

该 probe 使用真实显示器和渲染后端，但托盘信号与目标 anchor 由受控适配器提供，不注册／点击真实托盘，不发送全局输入，不连接真实目录或读写用户布局。真实产品托盘和分组验收另行记录，不把两者混为一项证据。

### 修复后真实产品与独立进程恢复

使用上述最终产品 hash 和独立验收布局文件，读取 281 项真实应用目录，没有修改日常用户配置。首进程实际将稳定 groupId 的“工具 Tools”改名为“学习与资料 Learning”，完成上移、下移、再上移；新增“验收临时空组”，输入另一名称后按 Esc，原名保留且未保存草稿。内部菜单和命名弹层操作期间窗口保持可用。用户协助真实托盘恢复，反馈“位置和大小正常”；恢复后工具实际观察到“完成编辑”状态。保存快照逐对象核对原四枚磁贴、目标及主题未变。

首进程于 16:38:43（Asia/Shanghai）通过用户菜单操作退出，代码 0。第二进程于 16:39:27 启动同一文件，用户实际删除临时空组并确认退出；16:43:30 记录退出代码 0。第三进程于 16:43:44 再次启动，工具恢复到前台，实际观察到浅色主题、“学习与资料 Learning”在前、“常用应用”在后、四枚原磁贴存在、搜索框为空且为非编辑状态；临时空组没有恢复。通过电源菜单退出，16:45:10 记录代码 0。三个独立进程使用同一最终产品 hash；最终重启前后 schemaVersion、targets、groups、preferences 深度相等，分组改名／排序／删除之外的目标、tileId、坐标和尺寸与验收起始快照相同。最后的四枚磁贴为 standard／small／wide／standard，仍沿用共享几何。

第二次启动曾进入既有最小化恢复路径，日志 `trayRecovery: false`；工具激活时两次被用户输入检测中止，随后由用户完成操作。第三次工具成功激活最终产品并核对实际内容。记录这一恢复边界，不据此声称每次托盘注册成功或 Explorer 重启兼容。完整布局和日志仅保留本地 `build/validation/issue28-product/`，公开[脱敏验收摘要](../design/validation/issue-28/product-acceptance.json)只含版本、阶段时间和结果，不上传用户桌面背景、真实目标路径或完整目录。

### 失败与修正记录

首轮原生探测因 Qt 屏幕名为面板型号而非 GDI 设备名，错误报告未满足双屏前提；修正为独立 HMONITOR 映射。随后六轮探测低 DPI 方向三轮通过，高 DPI 方向三轮失败：位置已正确但 Qt 逻辑尺寸缩为 685×485、物理外窗仍为 1028×728。保留迁移前逻辑尺寸意图后，没有放宽默认 980×680 DIP 面板断言。下一轮正式检查为 11/12：立即 resize 后收起尚未捕获新尺寸；增加收起前同步捕获及严格回归后完成最终 12/12 构建和六轮原生通过。失败 JSON 与日志分别保留在本地 `initial-unmatched-screen-probe.json`、`mapped-but-shrunk-probe.json` 及 `build/issue28-build-delivery.log`，不覆盖成最终通过结果。

独立只读审查核对屏幕身份、GUI 线程边界、尺寸意图、取消／重入、迟到回调、1500 ms 总时限及测试断言，最终未留下 P1/P2。该结果不覆盖物理系统缩放切换、显示器拔插、远程桌面、其他 Windows 构建或完整混合 DPI 矩阵；父 #5、#6 继续保持开放。

## 历史：离线应用搜索（Issue #8）

2026-10-21，[#8](https://github.com/xiaodaozhi/xdstarter/issues/8) 接入名称／英文、拼音全拼和首字母搜索，机制、容量与数据许可见 [ADR-0010](adr/0010-local-application-search.md)。自动检查、原生样例抓帧及真实目录／窗口交互分别记录如下。

正式入口 `./scripts/build-windows.ps1 -Preset app-release` 使用 Qt 6.8.2、MSVC 19.29.30153.0/v142、SDK 10.0.19041.0，最终 Release、QML lint 与 **12/12 CTest** 通过（17.87 秒），GUI **37/37**，包含四个新增搜索用例。最终日志为本地 `build/issue8-build-final.log`。首轮编译及 lint 通过，GUI 两项唯一结果断言失败：单字母查询还会命中另一名称／别名的拼音；修正为完整唯一名称后，保留原本结果数量与旧目标拒绝启动的断言并完整重跑。首轮日志保留 `build/issue8-build-initial.log`。

服务检查覆盖稳定排序、别名去重、多音、大小写／NFKC、`ü/u:/v`、符号和未知字断点、结果截断与总数、最新查询／目录胜出，以及后台线程退出。GUI 检查覆盖合成 IME 预编辑和提交、Ctrl+F、上下键／Enter、Esc 分层、宽窄视图滚动恢复、启动失败保留／成功隐藏、目录刷新移除旧目标、固定失效目标不可启动、重建窗口清空查询。独立审查发现结果元数据变化可重置当前选中行，已按 appId 与查询／修订恢复，并增加实际固定状态变化后的第二行焦点回归；图标／状态通知也检查不重发模型、不抢焦点。两轮独立静态审查未留下 P1/P2 问题。

仓库检查新增离线拼音检查器，校验许可、固定哈希、44,359 码点及常见多音字；从本地固定 Unihan ZIP 执行 `generate_pinyin_data.py --source-zip ... --check` 也通过。普通检查、构建及运行均不下载字音数据。GUI 在 `offscreen/software` 下直接导出完整窗口的[深色](../design/validation/issue-8/search-dark-offscreen.png)和[浅色](../design/validation/issue-8/search-light-offscreen.png)样例，1028×728、DPR 1；已目视核对搜索框、状态、两行名称、固定提示及主题。两图只含夹具名称和图标，不能证明原生 D3D、透明阴影或真实应用目录兼容。

| 最终程序 | SHA-256 |
| --- | --- |
| 产品 `xdstarter.exe` | `43eb1294a35ab5ec33d751de1c88814ee11bf44158992e5cce7d3ffaf93d6544` |
| GUI `xdstarter_ui_tests.exe` | `92df31add8e95367d11d0047cb3df93b931f35148eb643bebac84b4a69a224ab` |
| 搜索 `xdstarter_search_service_tests.exe` | `49553068d76c9376281a988b3ede84ac59cb0fa10e632c3795aa1a49dc95ee7a` |

### 参考搜索延迟

同一最终 Release 的显式 `xdstarter_search_service_tests.exe --search-benchmark` 在本机 Intel Core i7-11370H / Windows 桌面、Qt 6.8.2 上运行。固定数据集为 `synthetic-offline-applications-v1`：4096 个目标、每目标一个别名，混合中文、英文、多音与混合名称；`ten-families-ten-numbers-v2` 含 100 个互异查询，覆盖字面、全拼、首字母、别名和无结果，两路径各预热 20 次。最终测量未并行启动本项目其他测试；原始统计为[benchmark.json](../design/validation/issue-8/benchmark.json)。

| 边界 | P50 | P95 | 最大 |
| --- | --- | --- | --- |
| `SearchIndex::search` 直接查询 | 18.99 ms | 23.78 ms | 43.91 ms |
| `SearchService::setQuery` 到 GUI 接受结果，含 worker 排队 | 19.10 ms | 23.02 ms | 33.76 ms |

直接建索引为 80.63 ms；服务索引就绪探测为 86.62 ms，未计入热查询分位。direct/service 的匹配总数校验和均为 1909，每次超时上限 5 秒。该参考数据集的热搜索 P95 小于 PRD-012 的 100 ms 目标；计时不含 QML 布局／绘制、真实 Shell 扫描或图标加载，也不是键盘到屏幕呈现的 P95。单次参考运行不代表所有设备、系统负载或 8192×16×256 容量上限组合；多音状态匹配仍受查询长度及读音候选影响，正在执行的任务不会因新查询即时终止。

### 本轮 Windows 真机证据

用户确认开始桌面验收后，使用同一 GUI hash、Windows QPA 执行 `searchKeyboardAndLaunchLifecycle`，3/3 通过（含初始化／清理，2.617 秒），日志为本地 `build/issue8-render-native.log`。该用例直接导出[原生浅色样例](../design/validation/issue-8/search-light-native.png)和[原生深色样例](../design/validation/issue-8/search-dark-native.png)，均为 1028×728 RGBA、DPR 1，四角 alpha 为 0；已目视核对搜索框、结果名称、图标占位、固定状态、单层边框和主题。[抓帧元数据](../design/validation/issue-8/search-render-native.json)标明 `fixtureOnly: true`，没有读取用户目录或启动真实应用，不以此替代下述产品验收。

产品以同一 `43eb1294…` hash 和 `build/validation/issue8-product/layout.json` 隔离布局运行，首进程 PID 46088，真实目录显示 281 项。Computer Use 在实际窗口观察到：

| 输入／动作 | 实际结果 |
| --- | --- |
| `huatu` | 仅一项画图，真实系统图标及“已固定”状态可见 |
| `ht` | 三项，包含画图；英文名称的字面包含匹配排在拼音首字母前 |
| `画图` | 同一目标仅一项，目录与固定目标未产生重复 |
| Ctrl+F 后输入 `EXCEL` | 原查询被选中替换，找到一项 Excel，不区分大小写 |
| `xdstarter-no-such-application-20261021` | 无结果与清空入口可见，没有启动操作 |
| 清空 → 刷新目录 → 再搜 `huatu` | 恢复三枚磁贴浏览，目录仍为 281 项，同一画图目标仍只出现一次 |
| Ctrl+F → Down → Enter | 结果行可见焦点，实际打开“无标题 - 画图”，XDStarter 收起；检查空白画布后关闭本轮画图 |

查询由工具输入或通过编辑控件设置，中文名称检查没有使用物理输入法。工具的焦点字段未准确跟随 Qt 控件，键盘结果按可见焦点、选中文字与实际目标窗口判定；不声明屏幕阅读器焦点兼容已通过。完整目录、实际目标配置及含私人桌面背景的截图只保留本地，不提交。

首进程通过菜单退出，保留进程句柄的启动器记录 2026-10-21 14:36:19（Asia/Shanghai）退出代码 0。以同一文件和产品 hash 重启为 PID 27908，实际窗口恢复浅色主题，以及 column 0/2/3、row 0 的 standard/small/standard 三枚磁贴；搜索框为空。schemaVersion、IDs、targets、groups、preferences 与重启前逐对象一致，没有保存搜索查询。环境为 Windows kernel `10.0.26300`、Direct3D 11 / Intel Iris Xe；重启首次呈现日志为 render DPR 1／任务栏 DPI 96，后续托盘唤回日志出现 DPI 144、render DPR 从 1 到 1.5，最终间距请求为 12 物理像素。定位请求成功并禁用原生装饰；没有独立测量跨屏实际位置或稳定性，不把这些日志作为混合 DPI 兼容通过的结论。重启窗口失焦后收起并记录 `trayRecovery: true`，最后同样正常退出，时间 14:39:43、代码 0。

用户在获得本轮程序路径与隔离布局启动命令后，确认“从托盘唤回后，huatu 查询是仍然保留的”，补齐真实托盘唤回时的人工观察。这符合 [ADR-0010](adr/0010-local-application-search.md)：当前进程收起再唤回保留查询，清空按钮或 Esc 清除查询，退出后重启从空查询开始。人工确认与上述自动化启动的两个进程及退出记录分别保存，不额外推定手动实例的 PID 或退出码。

本地工件位于 `build/validation/issue8-product/`：`first/restart-process.json`、`first/restart-exit.json`、隔离布局前后快照、`search-observations.json`、`restart-layout-result.json`、`tray-query-user-result.json` 和日志。本轮必要搜索、启动、托盘查询保留及重启恢复验收已完成。物理 IME、其他设备、系统缩放／混合 DPI、屏幕阅读器以及本轮未重测的 Win 键／开始按钮接管不在当前通过范围；当前远程检查与最终合并状态以关联 PR／Issue 为准。

## 历史：开始按钮只读命中研究（Issue #24）

2026-10-21，[#24](https://github.com/xiaodaozhi/xdstarter/issues/24) 按 [ADR-0009](adr/0009-start-button-hit-probe.md) 为后续开始按钮接管验证身份前提。新增显式 `xdstarter_desktop_platform_tests.exe --probe-start-hit`，输出有限原生窗口与五点采样 JSON；产品唤起不调用该接口，不安装新 Hook、不调用控件动作或发送输入。`captureReady` 固定为 false，CLI 退出 0 表示完成诊断，不表示存在可接管目标。

正式入口 `./scripts/build-windows.ps1 -Preset app-release` 使用 Qt 6.8.2、MSVC 19.29.30153.0/v142、SDK 10.0.19041.0，最终 Release、QML lint 和 11/11 CTest 通过（18.78 秒），日志为本地 `build/issue24-build.log`。通用分类检查覆盖无 HWND、共享宿主、进程/祖先不符、几何/同屏约束、遮挡、缺失/重复采样、采样期间变化、负坐标和极值跨度；默认不支持查询不调用原 UIA anchor。首轮构建已通过，随后只读复核发现 3×3/4×4 矩形会使采样点退化重合，补上最小 5 像素的保守门槛与 2/3/4/5 边界回归后完成最终构建；首轮日志保留 `build/issue24-build-initial.log`。

### 本机只读结果

以普通桌面进程、后台隐藏的命令行方式运行三次显式 probe，没有弹出产品窗口或协调键鼠独占。程序 SHA-256 为 `6111487e6cfe3ea8cc7170c0d27e8e21ff7a0910970b15aac6b81322c446d01e`，程序自行读取二进制 hash 并与启动器核对。环境为 Windows 11 Version 26H2、kernel `10.0.26300`、x64、Qt 构建/运行均 6.8.2、MSVC `_MSC_FULL_VER=192930153`；这是 `QCoreApplication` 诊断，无 Qt Quick 窗口或 RHI 渲染结果。当前任务栏位于屏幕底部，按钮位于左端，DPI 144；未修改任务栏或系统显示设置。

| 采样（Asia/Shanghai） | 查询耗时 | UIA 身份 | 原生 HWND | 五点窗口类 | 结论 |
| --- | --- | --- | --- | --- | --- |
| 12:16:21.894 | 235 ms | 唯一可信按钮 | 0 | 全部 `Shell_TrayWnd` | `no-native-window` |
| 12:16:22.584 | 186 ms | 唯一可信按钮 | 0 | 全部 `Shell_TrayWnd` | `no-native-window` |
| 12:16:22.926 | 170 ms | 唯一可信按钮 | 0 | 全部 `Shell_TrayWnd` | `no-native-window` |

三次按钮物理矩形均为 `(0, 1528, 83, 72)`；任务栏为真实 Explorer `Shell_TrayWnd`。各次前后 UIA 身份/几何及任务栏观察稳定，中心和内缩四角共十五点都位于同一任务栏，不能绑定独立 Start 窗口。这支持“本机不满足本轮独立 HWND 保守准入”的结论，不证明所有 Windows 构建没有独立窗口，也不证明所有其他机制均不可行。耗时是三次研究查询的观察值，不是菜单唤起性能、P95 或持续缓存新鲜度保证。

本地工件为 `build/validation/issue24-native/start-hit-1.json` 至 `start-hit-3.json`、各自 stderr 与 `summary.json`；原始临时 HWND/PID 仅保留本地，不作为持久标识提交。默认产品、常规 CTest 和真实 probe 分开记录；本轮没有重做物理点击、右键/Win+X、Win 键接管或恢复交互，前一轮证据保持各自程序 hash 的历史身份。后续接管必须先解决共享任务栏宿主中的语义命中绑定、变化/失效与完整 down/up 手势，不能直接把一次 UIA 矩形放入输入回调。父 [#6](https://github.com/xiaodaozhi/xdstarter/issues/6) 继续开放，远程检查与合并结果以本轮 PR 和 #24 为准。

## 历史：单独 Win 键唤起（Issue #22）

2026-10-21，[#22](https://github.com/xiaodaozhi/xdstarter/issues/22) 承接父任务 [#6](https://github.com/xiaodaozhi/xdstarter/issues/6)，实现默认关闭、不持久化的单独 Win 键接管，以及窗口和托盘的启用/暂停入口。本轮本机验收已完成；机制与已知限制见 [ADR-0008](adr/0008-win-key-capture.md)，开始按钮仍未接管。

正式入口 `./scripts/build-windows.ps1 -Preset app-release` 使用 Qt 6.8.2、MSVC 19.29.30153.0/v142、SDK 10.0.19041.0，完成 Release、QML lint 和 11/11 CTest（最终 21.62 秒）；GUI 33/33、host 21/21，新增 `platform.win-key-capture` 的 245 项无原生输入断言通过。产品真机验收前的日志为本地 `build/issue22-build-final.log`（17.41 秒），追加测试专用截图导出后的最终日志为 `build/issue22-delivery-build.log`，产品 hash 不变。首轮构建期间并行修订使新测试与旧平台库交错，四项生命周期断言失败；冻结代码后完整重建全部通过，不将首轮失败日志作为通过证据。

新增自动检查覆盖左右 Win、重复/组合/修饰键、按住的鼠标按钮、注入隔离、完整与部分输入派发、代次和 QObject 销毁；常规 CTest 不安装全局 Hook。host 检查显式启用、失败保留原生入口、托盘丢失、停止后旧事件失效，以及托盘状态发布重入导致的取消启用；QML 检查默认禁用、显式意图和忙碌/暂停文字。上述结果与真实 Shell 行为分开记录。

| 程序 | SHA-256 |
| --- | --- |
| 产品 `xdstarter.exe` | `f7d1562aa486fb57d4fad4f789fa956ab4dbb440d9c7ad47d35620ffe77d014f` |
| 最终 GUI `xdstarter_ui_tests.exe` | `0f9cd4663d0ceb4a20e38e7f775bd102e1b191ed8afd8355519b5e6b637a9b8f` |
| Host `xdstarter_desktop_window_host_tests.exe` | `8a0c45e5e5116afdac1e5ea63e83f971ce54387817c83bf7a831cab37ceed921` |
| 输入平台 `xdstarter_win_key_capture_tests.exe` | `e4d3485ae8d5e93ee09a4c62266e8b30b893c3b1dfbca7e4b41077e35a239448` |

同一最终 GUI 在 Windows QPA 下执行 `winKeyCaptureControlRequiresExplicitIntent`，3/3 通过（含初始化/清理，3.221 秒）。显式设置 `XDSTARTER_WIN_KEY_VALIDATION_DIR` 可从该样例夹具导出[启用菜单](../design/validation/issue-22/win-key-enable-sample-native.png)与[暂停菜单](../design/validation/issue-22/win-key-pause-sample-native.png)：均为直接 `QQuickWindow::grabWindow()` 的 1542×1092 RGBA 完整窗口，已目视核对文字与菜单，不连接真实应用目录、不安装 Hook，不能代替下述真实键盘验收。现有 `XDSTARTER_VALIDATION_DIR` 离屏导出路径不变。首轮原生截图在调整窗口大小后未等待布局新帧，首次菜单点击未打开；测试专用导出补上激活与新帧等待后通过，日志为 `build/issue22-menu-native.log`，首轮失败另存 `build/issue22-menu-native-first.log`。

### 已取得的实际窗口与物理按键证据

同一产品在 Windows `10.0.26300`、Qt 6.8.2、144 DPI / DPR 1.5 的普通桌面权限下，以 `build/validation/issue22-product/layout.json` 隔离布局启动，PID 49412。窗口实际显示 281 项应用与三枚真实磁贴，菜单初始为“启用 Win 键唤起”；通过该明确入口启用后，产品日志记录 `active: true`。定位请求仍为 `(12, 496, 1470, 1020)`，工作区 `(0, 0, 2400, 1528)`，左/底各 8 DIP，不将该请求当作独立测得的桌面外窗边界。

用户按要求执行轻按、长按及可用的左右 Win 键测试，回复“均正常，无闪现或卡键”；同轮日志观察到 11 次 `source: win-key` 的菜单切换。该确认来自用户物理操作，不是工具合成按键，也不将切换数量当作独立测得的每个物理键事件。用户随后按要求验证 Win+E、Win+R、Win+数字、Ctrl 配合 Win 和 Win+L，回复“组合键均通过，已测试锁屏”。日志记录输入桌面切换后 `active: false`、`win-key-input-desktop-changed`，支持本机锁屏后停用的行为；500 ms 观察不能推定所有短暂桌面切换均可检测。

首轮用户测试中的物理 Esc 同时触发桌面自动化工具停止，当轮按工具约束停止继续发送桌面输入。用户随后亲自完成锁屏后托盘恢复并重新启用、暂停后 Win 返回原生 Start、再次启用后从电源菜单退出并确认 Win 返回原生行为，回复“三项均正常，已从电源菜单退出 XDStarter”。核对原 PID 与程序/隔离参数后确认第一实例已结束；未保留首轮进程句柄，不推定其退出代码为 0。

同一产品 hash 以相同隔离文件重启为 PID 53424。实际窗口恢复浅色主题，以及 column 0/2/3、row 0 的 standard/small/standard 三枚磁贴；schema、IDs、targets、groups 和 preferences 与重启前逐对象一致。磁盘仅有主题偏好，没有接管开关，实际电源菜单显示“启用 Win 键唤起”，确认新进程不会自动安装接管。

通过菜单重新显式启用后，打开磁贴右键尺寸菜单，工具发送 Esc 仅关闭子菜单，主窗口仍显示。进入编辑后将同一 standard 磁贴从 column 0 / row 0 拖至 row 2，观察到磁盘保存，再拖回 row 0；编辑状态保留，主窗口未误收起，最后布局与重启前逐对象相同。电源菜单仍显示“暂停 Win 键唤起”。最后从电源菜单退出；保留进程句柄的启动器通过 `WaitForExit` 独立记录 2026-10-21 11:40:46（Asia/Shanghai）正常退出、代码 0。

本地工件为 `build/validation/issue22-product/` 内的 `first-process.json`、`first-exit.json`、`build-evidence.json`、`single-key-user-result.json`、`combination-user-result.json`、`manual-recovery-user-result.json`、`live-key-observation.json`、`restart-process.json`、`restart-layout-result.json`、`restart-interaction-result.json`、`restart-exit.json`、隔离布局快照和产品 stdout/stderr。含真实目标描述的配置、完整应用目录和带私人桌面背景的截图不提交。用户物理键盘/托盘确认、纯状态机结果与工具实际窗口交互分别记录。提权前台、其他 Hook、远程/其他 Windows 构建、Explorer 重启、睡眠与混合 DPI 不在当前通过范围；父 Issue #6 继续开放。远程检查和最终合并状态以本轮 PR 与 Issue #22 的记录为准。

## 历史：主题菜单边框（Issue #18）

2026-10-21，[#18](https://github.com/xiaodaozhi/xdstarter/issues/18) 将菜单外轮廓由两主题共用的半透明品牌蓝改为独立 `menuBorder` token：浅色 `#B7BEC8`，深色 `#46505E`。保持不透明的单层 1 DIP 内描边、12 DIP 圆角、24 DIP 阴影留白及左/底 8 DIP 间距；主题切换会重绘。共享磁贴几何、布局 schema、原生装饰抑制和焦点逻辑没有变更。

正式入口 `./scripts/build-windows.ps1 -Preset app-release` 使用 Qt 6.8.2、MSVC 19.29.30153.0/v142、Windows SDK 10.0.19041.0，完成 Release、QML lint 与 10/10 CTest（16.58 秒），其中 GUI 为 32/32、host 为 17/17；日志为本地 `build/issue18-build.log`。仓库检查同时发现既有 `CMakeLists.txt` 被 Windows `core.autocrlf` 转为 CRLF，导致素材清单的原始字节哈希不符；固定该文件名使用 LF 后，最终含两张新预览的 440 文件、255 项素材检查通过，没有刷新清单或重生成素材。

| 程序 | SHA-256 |
| --- | --- |
| 产品 `xdstarter.exe` | `bfc9cd68e8fcf4de6b44343f8cb4f7b537e78f33cb4bbe74a074b1741cec1b82` |
| GUI `xdstarter_ui_tests.exe` | `2dbe6c6e76b5f21e7d4523a691662589ac998ba0c80ff1ddc7a2b55656a8c770` |

现有 `customChromePixels` 增加两主题四条直边的实际像素颜色检查，同时保留透明角、阴影、侧栏裁切与单层描边宽度约束。Windows QPA 原生渲染下，按基础 DPR 1.5 设置 Qt 缩放因子，100/125/150/175/200% 的该专项均通过（每档 3/3，包含初始化与清理）；每次均直接读取完整 `QQuickWindow.grabWindow()`，没有使用离屏 Canvas 回退。该结果是边框专项，不宣称重新运行了完整五档 GUI 矩阵，也没有修改 Windows 系统缩放。

同一 GUI 程序的 `--native-chrome-probe` 在 Windows `10.0.26300`、Qt 6.8.2 / Direct3D 11、DPR 1.5 下通过十二轮显示/隐藏：首次显示和稳定后位置一致，左/底间距均为 8 DIP。深浅主题完整窗口截图均为 1542×1092 RGBA PNG，外窗四角 alpha 为 0，近/远阴影 alpha 分别为 28/3，面板像素 alpha 为 255。已目视核对边框、圆角和主题：[浅色预览](../design/validation/issue-18/light-native-chrome.png)、[深色预览](../design/validation/issue-18/dark-native-chrome.png)。两图使用内存样例磁贴，未连接真实应用目录。

本轮工件位于本地 `build/validation/issue18-native/`：`native-chrome-probe.json`、`probe.log`、`border-matrix.json`、五份 `border-*.log` 和两张原始截图。probe 使用真实工作区查询及模拟托盘信号，不注册或点击真实托盘，也不读写用户布局；本轮未重做真实托盘、用户应用启动或重启布局恢复，相关既有结果保留下节的历史身份。远程检查与合并不在本轮本地验证结果内。

## 历史：统一边距与自绘外观（Issue #16）

2026-10-20，[#16](https://github.com/xiaodaozhi/xdstarter/issues/16) 修复托盘唤起时可信/失败锚点切换与显示后异步移动，改用透明外窗、QML 自绘单层圆角/边框/阴影，并禁用 Windows 原生装饰。可见面板距工作区左侧和底部统一为 8 DIP；外窗每侧 24 DIP 阴影留白不算作面板边距。Windows 产品入口初始隐藏，worker 的 `queryMenuAnchor()` 仅读取显示器、工作区和任务栏 DPI，不执行 COM/UIA，定位后再显示；旧 UIA 研究入口保留显式 probe，不能将产品兼容保留的 `trusted` 字段解释为唤起需要识别按钮。范围与取舍见 [ADR-0007](adr/0007-custom-menu-chrome.md)。继续使用本项目 Qt 6.8.2 与正式 v142/SDK19041 基线，不改变布局 schema 或磁贴几何。

正式 Release、QML lint 与 10/10 CTest 通过（13.64 秒），日志为本地 `build/custom-chrome-check.log`；GUI 为 32/32，host 为 17/17。自动检查覆盖可信/不可信兼容字段下几何一致、DPI/极值、面板与透明外窗坐标、隐藏阶段定位与待显示取消、过期查询不重开、原生装饰抑制、侧栏背景裁切及阴影/透明角像素。程序身份为：

| 程序 | SHA-256 |
| --- | --- |
| 产品 `xdstarter.exe` | `97054e7dc0d61aceed524c425a565b72313506b2d571defa6730c9d38d0b85f3` |
| GUI `xdstarter_ui_tests.exe` | `156aebb50586b94a7c17d94ad4addaf62b2db7d9e1062652fd6a35c4f15d34dc` |
| Host `xdstarter_desktop_window_host_tests.exe` | `2757cecf5ba8257ca9e0b08a7093584f9bb0291e1e720a90ba2b81908fe1a71f` |

本轮 CTest 默认 GUI 为 offscreen/software。该后端的 `QQuickWindow.grabWindow()` 会返回不含 alpha 的 RGB backing store，因此外观测试通过 Canvas `toDataURL("image/png")` 读取实际绘制的原始 RGBA PNG，未合成透明通道；侧栏透明背景及控件不越过圆角的几何另行检查。这验证 Canvas 绘制结果，不是完整 Windows 合成窗口截图，也不证明 DWM 实际外观。Windows 原生路径要求直接检查完整 `grabWindow()`，不能沿用离屏回退作为其通过证据。

### Windows 原生渲染与五档缩放

2026-10-21，同一 GUI hash 的显式 `--native-chrome-probe` 在 Windows QPA / Direct3D 11、Qt 6.8.2、kernel `10.0.26300`、DPR 1.5 下通过十二轮显示/隐藏检查。每轮首次可见窗口矩形与稳定后的矩形相同，十二轮位置一致；可见面板左侧和底部均为 8 DIP，透明外窗每侧留 24 DIP。实际工作区查询不执行 COM/UIA。工件为本地 `build/validation/issue16-native/native-chrome-probe.json` 与 `probe.log`。

此 probe 使用真实 Windows 渲染与工作区查询，通过模拟托盘 `toggleRequested` 进入产品 host 路径；没有注册或点击真实系统托盘，也不读取用户布局或真实应用目录。深浅主题的完整 `QQuickWindow.grabWindow()` 均为 1542×1092 RGBA PNG，未使用离屏 Canvas 回退：外窗四角 alpha 为 0，近/远阴影 alpha 分别为 28/3，面板实色像素 alpha 为 255。以下两图使用内存样例磁贴且未连接真实目录，仅用于核对自绘边框、圆角、阴影与主题，不能当作真实应用目录的产品截图：[深色样例](validation/issue-16/dark-native-chrome.png)、[浅色样例](validation/issue-16/light-native-chrome.png)。

同日 Windows QPA / Direct3D 11 的 100/125/150/175/200% Qt 有效缩放均为 GUI 32/32，耗时依次为 24.702/24.234/25.759/26.288/27.961 秒；基础 DPR 1.5，实际 DPR 约为 1/1.25/1.5/1.75/2。30 份 JSON 和 60 张窗口/网格 PNG 的像素检查通过。`build/validation/ui/native-matrix.json` 记录上述 GUI hash；本矩阵未修改 Windows 显示设置，不代替真实系统缩放切换或混合 DPI 验证。

### 实际产品交互与新进程恢复

同日使用上表产品 hash 和 `build/validation/issue16-product/layout.json` 隔离文件启动，真实目录显示 281 个应用。原生定位日志记录面板请求矩形为 `(12, 496, 1470, 1020)`、工作区为 `(0, 0, 2400, 1528)`，左/底间距均为 12 物理像素，即本机 8 DIP；阴影留白为每侧 36 物理像素。这是面板定位请求记录，不将其当作独立测得的桌面外窗边界。

实际鼠标将 Epic Games Launcher 小磁贴从 column 1 拖到 column 0，再用内部右键菜单改为 standard（104×104 DIP）。重新打开菜单后按 Esc 仅关闭子菜单，主窗口仍显示并保留编辑状态；随后切换 dark 主题，经电源菜单明确选择退出 XDStarter。第一进程 PID 12724 正常结束，`first-exit.json` 记录退出代码 0。

新进程 PID 12980 读取同一隔离文件，恢复 dark 主题及 column 0/2/3、row 0 的 standard/small/standard 三枚磁贴；重启前后 schema、IDs、targets、groups 和 preferences 逐对象一致，并可重新进入编辑。激活已有的仓库 Explorer 窗口后，产品日志记录 `external-focus`、`trayRecovery: true`，窗口收起；再次点击外部空白区域仍保持隐藏。上述是产品实际交互与恢复证据，不将 probe 的模拟托盘事件作为真实托盘点击。

用户随后亲自点击通知区 XDStarter 图标恢复，并按要求再完成两轮收起/唤起，目视检查左侧/底部间距、位置稳定、已完成的编辑保留及单层圆角/阴影，最后经电源菜单明确退出；用户回复“均正常，已从菜单退出”。这部分是用户亲自操作和目视确认，不是自动化点击真实托盘。重启进程日志同时记录多次 `tray-toggle`、`explicit` 与 `external-focus`，各次面板定位请求均为 `(12, 496, 1470, 1020)`。`restarted-exit.json` 独立记录 PID 12980 于 2026-10-21 10:02:32（Asia/Shanghai）正常退出，代码 0；连同第一进程代码 0，本轮完成实际产品交互、用户协助真实托盘恢复与新进程布局恢复的本机验收。

工件位于本地 `build/validation/issue16-product/`，包括进程/退出元数据、`interaction-result.json`、重启前后布局快照和 stdout/stderr；不提交含真实目标描述的隔离配置。本机验收不替代 PR 检查或表示已经合并；混合 DPI、自动隐藏任务栏、Explorer 重启等环境也不在本次通过范围。下方 #15/#14 的 hash、用户确认与待验收事项保持历史身份，不追溯记为本轮结果。

### 远程检查进展

本轮真机验收结束时，CI 受账户付款/消费额度限制阻塞：2026-10-21 手动[运行 39530444397](https://github.com/xiaodaozhi/xdstarter/actions/runs/39530444397) 的 job 未启动、步骤为空；用户当时选择先完成验收和 PR。随后用户恢复 Actions 额度并要求重跑，[PR #17](https://github.com/xiaodaozhi/xdstarter/pull/17) 的仓库检查和 Linux 核心检查已实际成功。恢复后的首轮 Windows 检查在 `setup-python` 获取 Python 3.12.13 时失败：该版本是仅源码安全版本，没有官方 Windows 二进制。

Windows 获取修复由 PR #17 跟踪：仅该 job 使用固定提交的 `setup-uv` 和清单中的 uv 版本获取精确 Python 3.12.13，将 managed 解释器路径传入既有构建入口；正式 Qt、Python、编译器与 SDK 基线不变。获取步骤的修改本身不构成 Windows 构建通过证据；最终远程结果以 PR #17 和 [Issue #12](https://github.com/xiaodaozhi/xdstarter/issues/12) 的实际运行记录为准。

## 历史：原生外观菜单弹层验证（Issue #15）

2026-10-20，[#15](https://github.com/xiaodaozhi/xdstarter/issues/15) 承接 #14 的桌面恢复基础，实现无边框菜单、Windows 圆角/阴影请求、外部焦点收起，以及内部菜单、拖拽和编辑的清理与恢复。该任务只覆盖 [#6](https://github.com/xiaodaozhi/xdstarter/issues/6) 的窗口与焦点部分，不安装输入 Hook，也不接管 Win 键、开始按钮或原生开始菜单。

### 构建与自动检查

正式入口继续使用项目自有 Qt 6.8.2、MSVC 19.29.30153.0/v142、Windows SDK 10.0.19041.0 与固定 Python/CMake/Ninja 工具，Release、QML lint 和 10/10 CTest 通过（13.06 秒），日志为本地 `build/popup-build-verified.log`。GUI 为 30/30，host 为 14/14；精确程序身份分别记录为：

| 程序 | SHA-256 |
| --- | --- |
| 产品 `xdstarter.exe` | `79555e72b5991e3c0cde1ef47d790451fe076d12ca6a3e6bdc0dad425b2c7a11` |
| GUI `xdstarter_ui_tests.exe` | `7d66601675b0068ddb24f403a4af10905ddb860793ce70925560fdf08dce6f12` |
| Host `xdstarter_desktop_window_host_tests.exe` | `0a665a74b785395957959d21da783201a5c2d5fa3f8a47f43e01e211b745e3aa` |

新增检查覆盖无边框且不透明的窗口、共享菜单圆角 token、键盘/鼠标主动收起、`Popup.Item` 内部菜单、呈现清理契约、越界释放取消未提交拖拽，以及 host 的外部焦点收起、内部临时窗口与快速返回保留、拖拽按下期间延后和无托盘最小化。测试使用隔离配置和注入平台；Qt 默认 offscreen/software 结果不当作 Windows 合成器外观或原生托盘通过证据。

`scripts/check_windows_icon.py` 对上述产品 PE 作只读审计，命名组 `IDI_XDSTARTER`、语言 1033，十帧 16/20/24/32/40/48/64/96/128/256 px 的元数据与 PNG 负载逐字节匹配源码 ICO，日志为 `build/popup-icon-audit.log`。仓库检查通过 432 文件和 255 项开发素材。

Windows QPA / Direct3D 11 的 100/125/150/175/200% Qt 有效缩放复核全部通过，每档 GUI 为 30/30，耗时分别为 54.514/33.412/29.427/32.469/33.482 秒。基础 DPR 为 1.5，实际 DPR 约为 1、1.25、1.5、1.75、2；30 份 JSON 与 60 张窗口/网格 PNG 的像素检查通过。本地 `build/validation/ui/native-matrix.json` 记录上述 GUI hash，日志为 `build/popup-native-matrix.log`；该矩阵不修改 Windows 显示设置，也不代替真实系统缩放切换或混合 DPI 验证。

### 实际窗口：隔离权限下的降级路径

实际环境为 Windows `10.0.26300.0`、Qt 6.8.2、Windows QPA / Direct3D 11、Intel Iris Xe、144 DPI。两轮产品均使用上表相同 hash 与 `build/validation/popup-native/layout.json` 隔离文件，不修改日常用户布局。

第一轮在执行环境的沙箱限制内启动；托盘注册与可信 UIA 识别不可用，窗口使用工作区内的明确位置回退。日志记录 `trustedAnchor: false`，初始截图元数据为起点 `(8, 500)`、逻辑尺寸 980×680，不能将这一轮表述为可信开始按钮对齐。产品外观请求被 DWM 接受，但该返回本身不证明圆角和阴影目视通过。

在实际菜单进入编辑后，将 Epic Games Launcher 小磁贴从 column 0 拖到 column 1、row 0，磁盘隔离布局反映同一 tileId 与 targetId 的新列坐标。打开内部右键尺寸菜单并按 Esc，主窗口保持显示与编辑状态。激活本次自建的空白 `manualFocusWindow` 后，产品日志记录 `external-focus`、`trayRecovery: false`，按降级策略最小化到任务栏；恢复后仍处于编辑模式。随后经 QML 文字退出入口正常结束，`first-exit.json` 记录代码 0。这一轮不作为真实托盘恢复验收。

### 实际窗口：正常桌面权限与用户协助托盘恢复

第二轮以正常桌面权限启动同一产品，实际应用图标、原生托盘与 UIA 可用。可信识别 StartButton 为 `(0, 1528, 83, 72)`，工作区为 `(0, 0, 2400, 1528)`；定位日志记录可见窗口请求矩形 `(0, 496, 1470, 1020)`，底部间隔为 12 物理像素，即 8 DIP。独立截图元数据记录实际屏幕起点 `(0, 496)`、逻辑尺寸 980×680，与按钮左侧 `x = 0` 一致；截图逻辑尺寸不作为独立测得的完整物理边界。

实际窗口从浅色切换为深色并进入编辑，随后点击本次自建空白窗口；产品消失，日志记录 `external-focus`、`trayRecovery: true`。自动化工具无法直接选择隐藏的产品窗口或托盘图标，因此由用户亲自点击 XDStarter 托盘恢复。用户回复“恢复正常，圆角和阴影可见”；本次圆角/阴影目视结果来自用户确认，和日志中的 DWM 请求接受状态分别记录。该操作不作为自动化点击托盘或原生托盘右键菜单验收。

托盘恢复后取得的实际窗口截图 `tray-restored-editing.jpg` 显示浅色主题、三枚真实目标图标、column 1/2/3 的磁贴、编辑状态与保存成功提示。用户在目视测试期间已从深色切回浅色，最终隔离配置实际保存为 `preferences.theme = light`，不按较早深色截图推断重启主题。

### 受控中断与新进程恢复

正常桌面进程在多次托盘恢复与原生矩阵焦点交接后，自动化工具无法选择隐藏的产品窗口。为验证已有自动保存快照的恢复，先核对本次进程的精确 PID 41376、EXE 路径和 `--layout-file` 参数，保存 `layout-before-restart.json`，然后仅对该隔离产品进程执行受控中断。`controlled-interruption.json` 记录目标与原因，`desktop-exit.json` 为 -1；这次不是正常退出，不作为 QML 文字退出或退出 flush 通过证据。第一轮沙箱进程的 QML 文字退出代码 0 单独保留。

随后以正常桌面权限启动同一产品 hash 的新进程，UI Automation 树显示三个真实目标名称和“已恢复保存的布局与主题”。`layout-after-restart.json` 与中断前快照逐对象一致：schemaVersion 2、相同 targets/groups/tileId/targetId，三枚磁贴仍为 column 1/2/3、row 0、small/small/standard，主题为 light。该证据支持新进程读取已保存布局，不将编辑模式作为持久化偏好。

新进程的窗口激活被系统拒绝，自动化工具两次返回 `failed to activate captured window`；所得画面是桌面背景，未作为产品窗口证据保存。按工具要求停止进一步 UI 输入，已转由用户目视确认新进程位置与主题并从 QML 电源菜单退出。该轮用户目视与正常退出结果仍待取得，当前不声明新进程窗口截图或正常退出通过。

工件位于本地 `build/validation/popup-native/`：各轮 `*-process.json`、`first-exit.json`、`desktop-exit.json`、`controlled-interruption.json`、`initial-capture-bounds.json`、`desktop-capture-bounds.json`、`light-initial.jpg`、`internal-size-menu.jpg`、`dark-edited.jpg`、`tray-restored-editing.jpg`、`sandbox-fallback-about.jpg`、`layout-before.json`、`layout-before-restart.json`、`layout-after-restart.json`、`layout.json` 及各轮日志。执行权限导致的托盘/UIA 降级与正常桌面结果分开记录，不据此归因为所有用户桌面环境的失败。这些工件位于忽略的构建目录，不提交到 Git。

混合 DPI 跨屏、真实 Windows 系统缩放切换、自动隐藏任务栏、Explorer 重启、原生托盘右键菜单、远程会话、高对比度及系统效果关闭、屏幕阅读器、Win 键/开始按钮接管和 Mica/Acrylic 材料本轮仍未验收。当前本机结果不扩大为所有 Windows 版本或完整系统接管支持。

## 历史：普通窗口桌面集成验证（Issue #14）

2026-10-20，[#14](https://github.com/xiaodaozhi/xdstarter/issues/14) 承接 #13 的设计素材，完成普通窗口快捷栏、圆形头像、软件图标、托盘恢复和只读开始按钮定位的本轮验证。以下记录对应产品 SHA-256 `154d7cff464dbf09473401462c9fe319b03ce7d03a36f2f41d572d0747f11182`；下方应用目录与布局记录保留原版本边界。

正式入口使用项目自有 Qt 6.8.2、MSVC 19.29.30153.0/v142、Windows SDK 10.0.19041.0 与固定 Python/CMake/Ninja 工具完成 Release、QML lint 和 10/10 CTest（11.09 秒）。新增 `platform.desktop`、`services.desktop_shell`、`ui.desktop_window_host`；GUI 26/26 通过。日志为本地 `build/runtime-shell-build.log`。后台动作与 host 测试注入假平台，覆盖白名单、成功/失败、singleflight、查询代次、关闭后旧结果失效、无托盘恢复、退出清理及应用关闭策略，不据此宣称实际 Explorer 重启已通过。

Windows QPA / Direct3D 11 的 100/125/150/175/200% Qt 有效缩放均为 GUI 26/26；基础 DPR 1.5，实际 DPR 约为 1、1.25、1.5、1.75、2，30 份 JSON 与 60 张窗口/网格 PNG 的像素检查通过。GUI 程序 SHA-256 为 `4bff72632e7fcb60153079bdd28fd6a7c99fafa29d0594069992484c45db5802`，与产品 hash 分别记录。当时工件为本地 `build/validation/ui/native-matrix.json`、`native-{100,125,150,175,200}/` 和 `build/runtime-shell-native-matrix.log`；共享的矩阵路径可被后续版本覆盖，#14 历史身份以本节 hash 与对应历史日志为准。该矩阵未修改 Windows 显示设置。

只读 `scripts/check_windows_icon.py` 对实际产品 PE 的资源目录进行审计：命名组 `IDI_XDSTARTER`、语言 1033，含 16、20、24、32、40、48、64、96、128、256 px 十帧 32 位 RGBA PNG，目录元数据、CRC 与每帧负载逐字节匹配源码 ICO；脚本不加载或执行 EXE。日志为 `build/validation/runtime-shell-native/exe-icon-audit.log`。工具单测 13/13 通过，包含原有 4 项 GitHub 初始化边界及新增 9 项 PE/ICO 正反向检查。

### 普通产品窗口与新进程恢复

环境为 Windows `10.0.26300.0`、Qt 6.8.2、Windows QPA / Direct3D 11、144 DPI。显示器物理矩形为 `(0, 0, 2400, 1600)`，工作区为 `(0, 0, 2400, 1528)`。只读 UIA probe 可信识别开始按钮 `(0, 1528, 83, 72)`；原生定位记录的可见窗口请求矩形为 `(0, 449, 1474, 1067)`，截图元数据独立记录实际屏幕起点 `(0, 449)`，与按钮左边 `x = 0` 相符。定位记录中的底部间隔为 12 px，即本机 8 DIP；没有把截图的 983×711 逻辑尺寸当作独立测得的完整物理边界。进程日志记录 `trustedAnchor: true` 与定位成功。

使用隔离布局启动后，实际窗口恢复三枚真实磁贴与深色主题，快捷栏、圆形头像和软件图标可见。点击“文档”打开新的 Explorer Documents 窗口，XDStarter 收起；只关闭该次新开的文档窗口，未读写其中的用户文件。原生托盘未出现在自动化工具可操作窗口列表，用户亲自点击后回复“已恢复可继续测试”；随后实际观察到同一个产品窗口 ID 恢复，日志再次记录同一可信定位矩形。该证据是用户协助的托盘点击恢复，不是自动化点击或原生托盘右键菜单验收。

恢复后切换浅色主题，查看“电源与退出”文字菜单，确认仅启用退出 XDStarter，Esc 关闭菜单并保留主窗口；“关于”显示当前托盘与定位状态。通过 QML 电源菜单退出，进程代码 0。新进程读取同一隔离文件，恢复浅色主题和三枚真实磁贴，targets/groups 与原隔离布局逐对象一致，再经同一明确退出入口正常退出，代码 0。自建产品窗口与新开的文档窗口均已关闭，桌面已交还用户；未修改日常用户布局。

工件位于本地 `build/validation/runtime-shell-native/`：`anchor-probe.json`、`capture-bounds.json`、`dark-restored.jpg`、`light-restored.jpg`、`about-integration.jpg`、`light-restarted.jpg`、`first-process.json`、`restarted-process.json`、`layout.json`，以及各轮 `stdout`/`stderr` 中的定位记录；汇总为 `result.json`，记录两进程相同产品 hash、三次定位、实际截图起点及两次正常退出。这些工件位于忽略的构建目录，不提交到 Git。

本轮未实际操作原生托盘右键文字菜单，下载、设置与账户入口也未专门通过真实平台打开；这些路径的当前证据来自代码边界和注入测试。混合 DPI 跨屏、真实 Windows 系统缩放切换、自动隐藏任务栏、Explorer 重启、远程 CI、屏幕阅读器、Win 键/开始按钮接管、DWM 材料、无边框弹层及外部点击关闭均未验收。当前是带系统边框的普通窗口；本机可信定位与用户协助托盘恢复不扩大为所有 Windows 版本或系统接管支持。

## 工程原型检查

正式工具版本由 [toolchain.json](../../toolchain.json) 声明，获取、版本检查和构建命令见[开发环境](development.md)。本项目拥有自己的 `build/tooling/` 和 `build/Qt/` 缓存，测试不要求 xdzip 仓库存在。

```powershell
./scripts/build-windows.ps1 -InstallTools -Python python -Preset app-release -Fresh
```

入口检查固定工具、选择 v142/SDK19041、配置、编译、运行 `xdstarter_qmllint` 和 CTest，失败即停止。`core-debug`/`core-release` 可不依赖 Qt，仍使用 C++20；`app-debug`/`app-release` 开启以下十套 CTest。本表说明目标覆盖，当前与历史变更的实际结果分节记录；GUI 默认 offscreen/software，不将离屏结果等同于 Windows 原生窗口。

| CTest | 覆盖 |
| --- | --- |
| `core.tile_grid` | 纯 C++ 三档几何、共享边界、负坐标、非法尺寸、横向越界与整数溢出；Release 下仍执行真实检查 |
| `services.prototype` | 稳定标识、三档矩形、方向邻接、预览不变性、组内/跨组移动、尺寸切换、固定/重复固定/取消固定、碰撞及边界拒绝 |
| `services.layout_store` | schema 1/2、真实目标与空布局校验、兼容保存及旧字节备份、引用/数量/坐标校验、损坏保留、未来版本只读、写入失败及有界读取 |
| `services.application_catalog` | 注入平台检查后台线程、目录结果、刷新失败保留列表、图标/缓存、重复请求和启动结果；不执行真实用户应用 |
| `platform.applications` | Windows 隔离 `.lnk` 扫描与去重、中文/特殊字符、目标失效、AUMID 校验、原生图标及受控 helper 启动参数/工作目录 |
| `platform.desktop` | 桌面动作白名单、面板工作区边距/极值与阴影留白转换、DWM 原生装饰抑制及 HWND 所有权边界、托盘旧会话事件失效；常规测试不打开真实用户目录，UIA probe 显式执行 |
| `services.desktop_shell` | 假平台上的后台动作、成功/失败、重复请求拦截、定位查询合并/失效和线程析构等待 |
| `app.qml_smoke` | 产品入口加载 QML 与视觉 token 后退出，加载失败或 QML warnings 使测试失败；不读写布局文件 |
| `ui.prototype` | 同一产品 QML、controller 与视觉资源上的鼠标/键盘、主题、编辑、保存恢复、只读和真实窗口像素检查 |
| `ui.desktop_window_host` | 假托盘/定位平台上的隐藏阶段定位、待显示取消/过期查询、关闭后恢复、内部/外部焦点、拖拽延后、无托盘与托盘丢失恢复、shutdown 和关闭策略还原 |

GUI 用例保留小磁贴 ToolTip/QAccessible 名称、长名称省略、480×480 双向滚动、方向导航和主题几何回归，覆盖鼠标组内/跨组拖拽、非法落点与 Esc 取消、右键/Shift+F10 三档尺寸、冲突拒绝、Ctrl+方向键编辑、重建窗口恢复布局与主题、保存失败重试、未来配置只读，以及深浅主题下尺寸菜单的高亮/按下文字与勾选对比度（至少 4.5）。目录固定/取消固定、启动成功/失败生命周期及失效目标恢复继续保留；桌面集成新增主题资源、圆形头像和快捷栏几何/键盘提示、受控动作结果、电源文字退出及关于状态检查。GUI 使用注入平台，真实 Shell 启动另在平台和产品窗口验证。测试使用隔离文件，不改日常布局；QML warnings 使测试失败。辅助功能名称检查不代表屏幕阅读器焦点已经验收。

[应用 CI](../../.github/workflows/application-build.yml) 读取同一工具清单，Ubuntu job 构建核心；Windows 2022 job 明确作为附加 MSVC 2022 兼容验证，不冒充 v142 精确基线。工作流已经推送，额度恢复、实际运行及最终检查结果由 [PR #17](https://github.com/xiaodaozhi/xdstarter/pull/17) 与 [#12](https://github.com/xiaodaozhi/xdstarter/issues/12) 跟踪；仅采用各次实际执行结果，不以配置存在代表通过。

## 历史：真实应用目录与固定操作验证

2026-10-20，真实应用目录与固定操作：已使用本项目 Qt 6.8.2、v142/SDK19041 和正式 CMake/Ninja 工具完成平台及 fixture 目标的局部 Release 构建，`platform.applications` 通过（2.25 秒）。测试只在 `build/app-release/platform-fixture-*` 创建快捷方式，枚举重复根时仍保留三枚不同快捷方式；实际验证中文/空格/`&` 路径、不同参数身份、失效目标保留及错误分类、删除后不可用、未安装 AUMID 和尾换行拒绝、64 像素原生 Shell 图标。

同一用例通过 `ShellExecuteExW` 启动测试构建的受控 helper，核对中文参数、含 `&` 的单个字面参数及快捷方式工作目录，结果写到隔离 JSON 后清理；没有启动未知用户软件或修改系统开始菜单。日志为本地 `build/platform-validation.log`。该结果不等于 AppsFolder 全目录或产品 GUI 已验收。

### 前一轮构建：按键激活修复后的回归

原生 125% 用例发现固定/取消固定菜单收到 Return 后未触发，现为相应 MenuItem 显式处理 Return/Enter。修复后的产品 SHA-256 为 `9826fbcad29f5c79ea61b02978246963ee76bba7fc9b5cb58a9abaeca661ae9f`，正式 Release、QML lint 和七套 CTest 已通过（7/7，10.72 秒）；GUI 23/23 通过，未出现 QML 或 Qt Pixmap 线程警告。GUI 回归包含目录/磁贴焦点隔离、按目标更新图标版本及菜单键盘操作。工具清单仍为本项目 Qt 6.8.2、Python 3.12.13、CMake 4.4.4、Ninja 1.13.2、MSVC 19.29.30153.0/v142 和 Windows SDK 10.0.19041.0，不把这些结果表述为所有编译诊断均为零。

同轮 GUI 测试 SHA-256 为 `163f1f813eae32f26d791f0753fe387eb7b8db270fd7e8b5db0de89c3cb93d04`，Windows QPA / Direct3D 11 五档 Qt 有效缩放全部通过，每档 23/23。基础 DPR 1.5，实测 DPR 约为 1、1.25、1.5、1.75、2；30 份 JSON 和 60 张窗口/网格 PNG 的像素检查通过。当时的 `build/validation/ui/native-matrix.json` 记录该轮 GUI 程序校验值；这个路径可被后续矩阵更新，历史版本以本节和该轮回归汇总为准，不将测试可执行文件的 hash 当作产品 hash。

该轮回归汇总为本地 `build/validation/application-final-regression.json`。修复后普通产品窗口已打开，但手动复核在输入操作前中止，随后仅清理已确认路径的隔离产品进程；这不作为正常退出或新产品普通窗口交互通过记录。下面普通窗口的完整手工证据属于修复前的 `89edf…` 构建，不能用新测试结果或新 hash 覆写其历史身份。

### 修复前构建：普通产品窗口手工验证

普通产品窗口已实际读取 281 个目录项，覆盖用户/公共 `.lnk` 与 packaged 应用及原生图标；以新进程打开同一隔离文件，恢复两枚桌面快捷方式小磁贴（48×48 DIP）及画图 packaged 标准磁贴（104×104 DIP），位置为 column 0/2/3、row 0，主题 dark。恢复前后布局字节相同，没有在读取阶段自动写回。截图为本地 `build/validation/catalog-native/restored-three-tiles.jpg`，进程清单为 `restored-process.json`；该轮产品 SHA-256 为 `89edf1a293ad7d7e1acb118d2b285997131e3b9fe8b58e15c0dcfbe2846ffc08`。

随后点击真实画图磁贴，通过 AUMID 打开“无标题 - 画图”空白窗口，产品收起，未编辑或保存用户文件。隔离失效快捷方式用例保存为 column 2/row 2/wide、light；实际窗口保留宽磁贴及红色失效标记，启动失败显示“快捷方式已移动或删除”，窗口保持可见。通过真实鼠标菜单取消最后一枚后，主 JSON 的 targets 与 tiles 均为空，`.bak` 保留前一枚有效磁贴；退出码 0。新进程恢复空布局及 light 主题，实际窗口与磁盘一致，再次退出码 0。

修复前的产品验收汇总为本地 `build/validation/catalog-native/result.json`，截图为 `paint-launched.jpg`、`unavailable-preserved.jpg`、`last-tile-unpinned.jpg`、`empty-restored.jpg`；该组手工验收的产品 SHA-256 均为上述 `89edf…`，不代表该轮 `9826…` 产品已重新完成手动复核。正常三枚磁贴的布局另行保留，失效用例没有删除系统开始菜单中的真实快捷方式，日常用户配置未修改。GUI 中缓存版本用例通过 controller 清理引用，鼠标菜单取消固定由另两个 GUI 用例与该组真实窗口单独覆盖。

该轮 `9826…` 构建的普通窗口手动复核未完成；本轮桌面集成窗口另按上方 `154d…` 记录，不追溯补记旧版本结果。真实权限拒绝未全面覆盖，真实系统缩放切换、混合 DPI 跨屏和远程 CI 仍未验收。进度与补充证据由 [#7](https://github.com/xiaodaozhi/xdstarter/issues/7)、[#9](https://github.com/xiaodaozhi/xdstarter/issues/9)、[#10](https://github.com/xiaodaozhi/xdstarter/issues/10) 跟踪。

## 上一轮工具链与布局验证

2026-10-20，应用目录变更前：正式入口已使用本项目 Qt 6.8.2 SDK 与自有工具环境完成 Release 构建、QML lint 和五套 CTest，5/5 通过，GUI 19 项通过。实际工具为 Python 3.12.13、CMake 4.4.4、Ninja Python 分发 1.13.2，MSVC 19.29.30153.0、linker 14.29.30153.0、v142 14.29.30133、Windows SDK 10.0.19041.0；完整版本记录在本地 `build/app-release/toolchain-validation.json`。生成的 QML C++ 编译经过 Qt 第三方头时存在 C4702 诊断，QML lint 本身通过，不将结果表述为“零编译警告”。

构建首次暴露中文用户临时目录被 MSVC v142 错误解码的问题，正式入口改为只对子进程设置项目 `build/tmp/`；重新配置已能识别编译器并完成 ABI 检查。上一轮五档原生矩阵已完成：Windows QPA / Direct3D 11，基础 DPR 1.5；100/125/150/175/200% 各运行完整 GUI 检查，均为 19 项通过、0 失败，实际窗口 DPR 分别约为 1、1.25、1.5、1.75、2。每档深浅主题 × 三组共六组记录，合计 30 份 JSON、60 张 PNG，像素扫描全部通过。当时的 `native-matrix.json` 记录了 GUI 测试校验值，手工窗口记录也核对了同轮产品校验值；当前构建的独立回归结果见上方记录。进度与证据见 [#4](https://github.com/xiaodaozhi/xdstarter/issues/4)、[#5](https://github.com/xiaodaozhi/xdstarter/issues/5)、[#9](https://github.com/xiaodaozhi/xdstarter/issues/9)、[#10](https://github.com/xiaodaozhi/xdstarter/issues/10)。

上一轮已使用产品普通窗口完成可见 Windows QPA / Direct3D 11 验证：通过实际鼠标拖拽将 A 磁贴从工作组第 0 列/第 0 行移至第 0 列/第 2 行；右键菜单依次由标准切换为小、标准、宽，左上锚点不变；尝试扩宽相邻 B 磁贴时因占用冲突而拒绝。自动保存与显式“保存布局”均成功，JSON 和 `.bak` 存在；Esc 先退出编辑，再关闭窗口。

随后以新进程打开同一隔离配置，界面提示已恢复，A 的工作组/column 0/row 2/wide 与 light 主题均和 JSON 一致。三次产品原生进程全部正常退出，代码 0。工件为 `build/validation/editing-native/light-wide-saved.jpg`、`restored-light-wide.jpg` 和 `result.json`。实际窗口还发现 Basic 尺寸菜单高亮白底白字，现已固定文字/选中配色并增加两主题、三尺寸项的可读性回归；实测高亮文字清晰可见。未修改日常用户配置。

前一轮静态原型已在 Windows 11 10.0.26300、Intel Iris Xe / Direct3D 11、Qt 6.8.2 和同版 MSVC 实测选择、主题、焦点滚动、五档 Qt 有效缩放及 Esc 退出；历史工件位于 `build/validation/prototype-native/`、`layout-native/`、`ui/`。当时辅助工具为 CMake 3.31.10、Ninja 1.13.0、Python 3.11，只保留为历史证据，不代表新清单和新编辑功能验收。混合 DPI 跨屏、真实 Windows 系统缩放切换、系统开始菜单接管和屏幕阅读器焦点仍未验收。

## 隔离配置的实际窗口验证

```powershell
$env:PATH = "$PWD/build/Qt/6.8.2/msvc2022_64/bin;$env:PATH"
& ./build/app-release/bin/xdstarter.exe --layout-file "$PWD/build/validation/manual/layout.json"
```

普通首次运行应显示空“常用应用”和真实应用列表，等待扫描完成，核对用户/公共开始菜单与打包应用名称及实际系统图标。选择不会改变用户文件的已知应用验证列表与磁贴启动；成功收起窗口，失败保留窗口并显示原因。不要为验收批量启动未知应用或卸载用户软件。

从新的隔离文件选择应用并固定到指定组，验证标准尺寸与首个空位、重复固定不新增；进入编辑模式验证鼠标拖拽/跨组、合法与冲突预览、Esc 取消、三档尺寸与键盘编辑。切换主题后等待保存成功或点击保存，关闭窗口再以同一参数启动，核对真实 targetId/启动描述、磁贴标识、分组、格坐标、尺寸和主题；对照磁盘 JSON 与实际窗口，不能只观察状态文字。随后取消固定，验证目录项目仍在、快捷方式未删除，取消最后一枚后重启应恢复空布局。

失效目标使用 build 内测试快捷方式：固定并保存后删除 fixture 快捷方式，再重启检查磁贴位置保留、不可用原因及取消固定；不删除真实开始菜单条目。schema 1 升级单独核对旧样例不启动、读取不写回、下次保存 schema 2 且 `.bak` 保留旧字节。损坏/较新版本/无权限目标使用独立测试文件，保留原输入与备份，不在真实日常配置上制造错误。

快捷栏实际验证选择文档等已知入口，检查新系统窗口打开、产品收起及托盘恢复；不批量打开用户目录或设置。托盘文字菜单与 QML 电源菜单分别验收，不将其中一个通过记作另一个通过。测试无托盘恢复和 Explorer 重注册时，分别注明假平台结果与真实系统操作。

`--smoke-test` 不连接真实目录、桌面动作、托盘或定位服务且无布局 IO 的行为单独验证；正常退出应等待待保存快照落盘，失败应有状态或日志并保留前一有效文件。正常窗口运行不等于已接管 Win 键、开始按钮或 Explorer。

## Windows 运行时截图与有效缩放

完成应用和测试构建后运行。正式默认缓存可直接赋给 `XDSTARTER_QT_ROOT`，或传入其他相同版本 SDK：

```powershell
$env:XDSTARTER_QT_ROOT = "$PWD/build/Qt/6.8.2/msvc2022_64"
./scripts/verify-windows-ui.ps1 -QtPrefix $env:XDSTARTER_QT_ROOT
./scripts/verify-windows-ui.ps1 -QtPrefix $env:XDSTARTER_QT_ROOT -NativeScales
```

第一条在 offscreen/software 下运行交互与截图检查；第二条在可见 Windows 桌面使用 Windows QPA 和默认图形后端。脚本先以 `QT_SCALE_FACTOR=1` 探测当前屏幕基础 DPR，再按 `目标 DPR / 基础 DPR` 校准 100/125/150/175/200% 的 Qt 有效缩放，并核对实际窗口 DPR。此前本机探测基础 DPR 为 1.5、后端 Direct3D 11；脚本不假定其他显示器具有同样数值。

原生输出目录为 `build/validation/ui/native-{100,125,150,175,200}/`，`native-matrix.json` 汇总基础 DPR、实际 DPR、图形 API 和被测程序校验值。每档保存六组深浅主题 × 三组的记录，每组包括完整窗口 PNG、网格裁剪 PNG 和 JSON；默认离屏输出到 `build/validation/ui/offscreen/`。测试对真实 `QQuickWindow` 抓帧并扫描物理像素，检查小/标准/宽磁贴的宽高、8 DIP 间隙、四小磁贴含中缝与标准外框相等、两标准磁贴含间隙与宽磁贴相等，允许光栅化边界最多 1 物理像素误差，同时确认主题切换不改变逻辑几何。

验证脚本将当前进程及其测试子进程的 `TEMP`/`TMP` 临时设为 `build/validation/ui/temp/`，避免受管桌面默认临时目录不可写使 QTemporaryDir 夹具失败；脚本通过 finally 恢复原环境，不修改系统/用户设置或产品保存逻辑。构建入口另使用 `build/tmp/`，两者用途分开记录。

几何抓帧只在测试夹具中把 ToolTip opacity 设为 0，避免浮层遮挡磁贴扫描；JSON 明确记录 `tooltipsHiddenForGeometryCapture: true`。像素取样避开图标字形抗锯齿区域，未修改产品 ToolTip 行为或放宽 1 物理像素容差。ToolTip 可见性与完整文本由独立交互用例验证，不能用这些几何抓帧判定提示浮层表现。

批量截图、JSON 和日志属于本地或 CI 工件，不提交到 Git。本轮仅将两张无用户数据的内存样例窗口 PNG 作为代表性验证图收录于 `technical/validation/issue-16/`；真实应用目录、布局目标描述和完整构建工件不随其提交。失败时保留实际 QPA、图形 API、窗口/屏幕几何、DPR 与像素扫描记录，避免仅凭 JSON 参数或设计稿认定布局通过。

此矩阵不修改 Windows 显示设置，不能替代系统设置中真实切换缩放、混合 DPI 跨屏拖动、拔插屏幕、原生开始菜单接管或屏幕阅读器焦点验收。其他 Qt 版本、MSVC 2022 应用 CI 与 macOS 构建也仍需各自验证。

## 应用测试分层

| 层级 | 场景 |
| --- | --- |
| 核心单测 | 网格矩形、占用冲突、边界、搜索排序、配置校验与恢复 |
| QML/集成 | 模型更新、输入焦点、键盘导航、拖拽预览、缩放截图 |
| Windows 真机 | Win 键/按钮接管、系统组合键、托盘、应用启动、原生材料、异常退出 |
| 发行回归 | 干净机器安装、非管理员启动、升级/降级、卸载和配置保留 |

可逆的纯文案修改以文档与视觉检查为主；新增算法、状态转换和数据恢复应有针对性测试，避免只复述实现的测试。

## 首轮矩阵

- Windows 11 x64：记录精确版本；当前菜单原型本机证据来自 10.0.26300.0，执行环境沙箱降级与正常桌面权限结果分别记录。仍需覆盖用户实际使用的稳定构建与另一稳定构建，不能由一次本机 DWM 请求或普通窗口历史结果扩大兼容范围。
- 缩放：当前提供 100/125/150/175/200% 的 Qt 有效缩放检查；真实系统缩放切换、双屏混合 DPI、屏幕拔插与主屏切换仍需真机验证。
- 任务栏：居中/左对齐、自动隐藏、副屏；Explorer 重启、睡眠恢复与会话切换。
- 输入：左右 Win、连续/长按、Win+E/R/L/数字、开始按钮左右键、中文输入法。
- 视觉与可达性：深浅、高对比度、关闭透明、减少动画、屏幕阅读器与纯键盘。
- 数据：卸载应用、快捷方式失效、无权限目标、写盘失败、配置损坏、较新配置版本。

## 性能目标与方法

参考机记录 CPU、内存、GPU、磁盘、Windows 构建、Qt/编译器和 Release 构建参数。以 100 枚磁贴、1000 个应用目录项作为初始数据集，另外覆盖空布局和较大目录。

常驻后从唤起输入到首帧可交互 P95 ≤200 ms；索引就绪后查询至结果更新 P95 ≤100 ms。至少采集 100 次并区分冷/热启动。记录工作集、私有内存、隐藏时 CPU 和动画时帧耗时，暂不凭框架名称承诺内存数值。

原生材料在虚拟机/远程桌面下的表现单独记录，不能替代本地真机结论。失败用例进入 Bug Issue，附版本、复现、预期/实际和日志截图。
