# ADR-0017：Windows 动态部署裁剪与当前用户安装器

日期：2026-10-28。状态：已接受，按所有者确认方案实施。工程跟踪 [#64](https://github.com/xiaodaozhi/xdstarter/issues/64)，发行验收继续由 [#36](https://github.com/xiaodaozhi/xdstarter/issues/36) 管理。

## 背景

已有入口只负责开发构建，构建 EXE 不能据此独立分发。xdzip 的 app-local CRT、Basic 裁剪、依赖审计和当前用户安装器实践可复用，但需适配本项目 QGuiApplication、Dialogs、常驻接管与布局保存。

## 决定

- 维持 ADR-0002 的本机 v142 基线，增加独立 MSVC 2022 Release distribution 身份，由 CI 交付候选。本机无需安装 VS 2022，兼容开发结果与交付构建分别记录。
- 现有 Python／PowerShell 入口扩展 prepare-package、package、verify。普通构建不需要安装器工具；项目独立锁定输入、缓存和回执，不运行时读取其他仓库。
- 官方动态 Qt 6.8.2 先做部署裁剪，保留 Basic、Dialogs、Network IPC、Svg 和图形回退，不定制或静态编译 Qt；通用领域层及 QML 不依赖 Win32。
- CRT 从固定微软包提取为 app-local DLL，保留原始协议。图形编译器从明确 SDK REDIST 获取，核对下载与文件哈希，更新通过依赖 PR。
- Inno Setup 6.7.3 当前用户安装，稳定 AppId，登录启动默认不选。Windows 平台与安装器共享 SID 跨会话互斥，正常退出后再修改安装，保留单例 IPC 与保存退出屏障。
- 升级清理由路径、清单和哈希约束，未知文件保留；卸载默认保留数据，显式清理限默认应用数据与缓存，schema 和安装版本独立。
- 同一审计目录生成 ZIP／EXE，绑定版本、commit、build、dirty、工具链、文件哈希及体积。收集源码、通知和 SPDX，缺口明确报告，不自动签发许可或发布结论。

## 影响与验收

app-local 支持普通用户及解压运行，CRT 安全维护随应用升级；图形回退体积以报告建立基线。固定 Inno 官方编译器的 PE 版本为 0.0.0.0，通过 Output=no 编译探针核对实际 engine 版本。

自动化覆盖依赖闭包、QML、翻译、安装互斥、升级清理和 Server 夹具。夹具仅改变 OS 门槛，不替代 Windows 11 产品安装。干净 Windows 11、真实系统入口恢复、签名和所有者许可保持独立门槛，工程交付不关闭 #36 或 #3。

依据：[xdzip 部署](https://github.com/xiaodaozhi/xdzip/issues/15)、[Qt 部署](https://doc.qt.io/qt-6.8/windows-deployment.html)、[MSVC 兼容要求](https://learn.microsoft.com/en-us/cpp/porting/binary-compat-2015-2017?view=msvc-170)、[Inno 最低权限](https://jrsoftware.org/ishelp/topic_setup_privilegesrequired.htm)。
