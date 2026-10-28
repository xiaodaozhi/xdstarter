# 发行与维护设计

状态：开发候选流程已接入，工程跟踪 [#64](https://github.com/xiaodaozhi/xdstarter/issues/64)。干净 Windows 11、签名和公开发行验收继续由 [#36](https://github.com/xiaodaozhi/xdstarter/issues/36) 跟踪，当前不提供公开发行下载。

## 首版发行方向

Windows 11 x64、Qt 6.8.2 动态库、普通用户运行。保留本机 v142 开发基线，新增 MSVC 2022 CI distribution 候选，见 [ADR-0017](adr/0017-windows-packaging.md)。采用当前用户 Inno Setup 6.7.3 安装器与便携 ZIP；开发候选记录 signed=false，公开发行保持原有签名和许可门槛。

Qt 部署使用 CMake 部署脚本或 `windeployqt`，明确扫描 QML imports，包含 Quick、平台插件、图像插件和匹配的运行库。必须在没有 Qt 开发环境的干净机器验证，不能只在开发者机器运行。

Qt 运行库许可与应用许可证分别处理，依赖清单、许可证文本及所需源代码/重新链接材料随实际组件选择准备。许可未完成决定前，公开发行是阻塞事项。

## 版本与更新

使用 `v0.x.y` 表示早期版本。Release notes 链接对应 Milestone 和已关闭 Issues，列出已验证的 Windows 构建、已知问题、包校验值及恢复方法。

首版由用户主动下载安装更新。更新前暂停接管、退出进程并保留有效配置备份。数据 schema 独立版本，降级遇到较新 schema 时不覆盖用户数据。

## 安装与卸载验收

- 标准用户安装、首次启动及开机启动选择。
- 接管默认经过明确启用与兼容探测；退出或失败后原生菜单仍可用。
- 升级保留分组与布局，失败可回退前一版本。
- 卸载清理启动项与应用文件，用户配置是否删除由用户选择。
- 不写入系统文件，不依赖管理员主进程长期常驻。

每次发行使用独立 Release checklist Issue。未验证的系统组合标为未知，不扩大支持承诺。

参考：[Qt Windows 部署](https://doc.qt.io/qt-6/windows-deployment.html)、[Qt 许可](https://doc.qt.io/qt-6/licensing.html)。

## 分阶段入口

普通构建保留编译、qmllint 和 CTest，不要求 WiX 或 Inno。已准备项目 Python／Qt 后执行：

```powershell
./scripts/build-windows.ps1 -Toolchain MSVC2022 -Distribution -BuildNumber 1 -Fresh
./scripts/build-windows.ps1 -Toolchain MSVC2022 -Stage prepare-package
./scripts/build-windows.ps1 -Toolchain MSVC2022 -Stage package -OutputDirectory build/package/candidate-1
./scripts/build-windows.ps1 -Toolchain MSVC2022 -Stage verify -OutputDirectory build/package/candidate-1
```

首次获取工具另传 `-InstallTools -Python <解释器>`。`-Stage all` 依次构建、准备与打包；`-Kind runtime|installer|all` 控制交付类型。runtime 先部署、验证再生成 ZIP；installer 要求同一目录已有审计通过的运行目录；all 默认同时生成 ZIP／EXE。候选使用新 OutputDirectory，拒绝覆盖 staging。Python 入口为 `toolchain.py prepare-package|package|verify`，参数见 `--help`。

干净候选拒绝 dirty 源码、Debug、旧构建回执及变化后的文件。`--build-info` 返回版本、完整 commit、build、dirty、Qt 和布局 schema，`--version` 与 Windows VERSIONINFO 使用相同身份。记录实际编译器、linker、SDK，核对固定 CRT 不早于应用与 Qt 工具链。

显式 `--development`／`-DevelopmentPackage` 允许本机兼容夹具，带 development 后缀，不能转成干净候选。此时源码附件仅代表 HEAD，不含 dirty 编辑，不是 dirty 二进制的完整对应源码。

## 固定输入与裁剪

[packaging/windows](../../packaging/windows) 保存本项目独立的官方 URL、版本与 SHA256，构建无需 xdzip 仓库。策略参考 [xdzip Windows 部署](https://github.com/xiaodaozhi/xdzip/issues/15)。

- VC_redist 只用于提取八个 x64 app-local CRT DLL 与原始协议，不安装运行库，不从 System32 收集 DLL。WiX dark 和 expand 只用于构建。CRT 安全维护通过依赖 PR 更新锁定材料与回归。
- 当前锁定 v14 14.51.36247.0，以满足 MSVC 2022 runner 的实际工具链；其原始协议由微软包内链接指向 DOCX。原件与带编号的确定性 UTF-8 文本同时交付，分别固定哈希，安装器展示文本且保留原件；没有借用旧版协议。
- 同版 windeployqt 扫描产品 QML 和构建 imports，固定 Basic；移除其他样式、未使用 Effects、qmltooling 和 `.qmltypes`。保留运行 QML、qmldir、Dialogs 原生及 Quick 回退依赖；Test、offscreen、实验和设计程序不进入产品。
- 保留 Qt Mesa 的 opengl32sw，以及明确 SDK REDIST 的 D3D／DXC／DXIL，附来源与协议，不从环境搜索替代输入。
- Qt 翻译兼容 SDK 的 qtbase 和部署合并的 qt 目录名；携带简体中文目录，英文使用源文，不扩展为完整产品双语能力。
- prepare-package 显式准备 Inno 构建编译器；首次可将该构建工具安装至项目缓存，不安装 XDStarter。仅对应阶段需要网络和打包工具，缓存或下载哈希不符则失败。

## 安装生命周期

安装器要求 Windows 11 x64，以最低用户权限安装至当前用户 Programs/XDStarter，独立 AppId，登录启动默认未选，升级保留已有选择；升级时显式取消会移除本产品启动项，重新选择则写回。便携包解压不写启动项，继续用现有用户数据目录，并非数据随身模式。

程序与安装器共享用户 SID 范围的跨会话互斥：驻留实例阻止安装／卸载，安装期间拒绝新驻留实例。正常退出先释放接管，再等待布局保存；Inno 自动关闭和重启关闭，静默占用返回失败。此机制不替代 SessionInstance IPC，也不承诺抵抗同用户恶意并发目录修改。

升级只删除旧 SHA256 清单中、不在新包且内容仍匹配的文件；清单损坏、越界、重解析点或已修改的过时文件阻断操作，未知文件保留。安装器不转换布局 schema，回退后沿用未来格式只读保护。

卸载默认保留配置。交互卸载可显式选择清理；静默卸载仅 `/REMOVEUSERDATA=1` 才清理。范围限默认数据目录内的 layout.json、备份、损坏副本及已检查的 cache/qmlcache；自定义 layout-file 和未知文件保留。

## 工件与验证边界

输出含 ZIP、安装 EXE、应用源码 TAR、materials.tar.xz、SHA256SUMS，以及 deployment-audit、runtime-attribution、size-report、runtime-verification 与 package 报告。ZIP／EXE 来自同一审计目录，逐项核对哈希，体积以实际报告建立基线；生成物只在 build/ 或 CI 工件。

包内携带 Qt、Unicode、CRT、SDK、Mesa／LLVM 和安装器引擎通知，配套固定哈希的 Qt 模块、构建工具及 Mesa／LLVM 原始源码。SDK SPDX 与实际字节用于观察文件归属，未匹配、内嵌组件及源码对应关系明确为 incomplete。保守许可集合不表示所有组件被使用，源码归档存在不证明 SDK 二进制等价，应用许可仍由 [#3](https://github.com/xiaodaozhi/xdstarter/issues/3) 决定。

审计递归核对普通与延迟 DLL 导入，CRT 必须来自包内，第三方 DLL 不回退到 SDK 或 System32。无 SDK 搜索路径的独立副本验证软件／原生 D3D11 和界面，测试 DLL 只加入副本。

普通 CI 不发布 Release。生命周期脚本仅允许一次性 hosted runner，Server 夹具只降低 OS 门槛，验证安装、占用拒绝、升级清理、启动项和卸载且不上传夹具。实际安装器仍要求 Windows 11。Server 夹具、无 SDK 运行和干净 Windows 11 同哈希候选验收分别记录。
