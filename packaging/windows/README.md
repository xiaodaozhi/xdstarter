# Windows 固定打包输入

本目录是 XDStarter 自己的构建声明，不读取其他仓库。初始来源与策略参考 [xdzip Windows 部署](https://github.com/xiaodaozhi/xdzip/issues/15)。官方 URL、版本和 SHA256 是供应方输入记录，不是项目许可证或自动发行批准。

- runtime.json：Microsoft VC++ x64 官方包、八个 app-local DLL、原始协议及 WiX 提取工具。
- installer-toolchain.json：Inno Setup 6.7.3、官方源码提交、中文语言文件和引擎许可材料。
- qt-license-sources.json：Qt 原始许可正文的固定获取记录，保守集合可包含未使用组件；其中 commit 不冒充 SDK 构建 commit。
- sources.json：Qt 模块和构建工具的原始归档及哈希，不继承 xdzip 的 Rust、字体或其他平台依赖。默认包不含软件 OpenGL，旧 Mesa／LLVM 材料不再作为它的来源。

源码归档与 SDK SPDX 一同提供，仍需核对补丁、配置和实际组件对应关系。缺口在 runtime-attribution 报告中保留。运行目录中的 Windows-runtime-NOTICE 和 Qt-replacement 分别说明原始条款与动态库替换边界。使用和验收见[发行流程](../../docs/technical/release.md)。

图形故障测试代理仅供隔离副本使用；发行审计拒绝 `d3d11.dll` 和 `xdstarter_d3d11_failure_proxy.dll`，系统 D3D11 始终由 Windows 提供。
