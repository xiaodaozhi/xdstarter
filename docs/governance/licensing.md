# 许可与依赖决策

状态：项目代码、文档和原创设计资产采用 [Apache License 2.0](../../LICENSE)。第三方组件及数据的独立许可和通知另行保留。

## 项目许可范围

按项目所有者的决定，项目代码、文档和原创 Logo／设计资产适用仓库根目录的 Apache-2.0 许可。第三方内容仍按其自身条款使用和分发；Apache-2.0 不额外授予商标权。Windows 运行包在 `share/notices/XDStarter-LICENSE.txt` 携带项目许可原文。仓库可见性由所有者单独管理。

项目许可证的选定与下述 Qt 许可记录不等于第三方组件或安装包的发行审查已完成；具体候选和发行验证由 [#36](https://github.com/xiaodaozhi/xdstarter/issues/36) 跟踪。

## Qt 许可渠道与发行义务

当前工程从公开 Qt 6.8.2 归档获取 Windows x64 动态 SDK，候选以 LGPL 3.0 开源许可作为 Qt 库的审查路径；这不是商业授权或商业支持资格的声明。按本机该版本 SDK 的 SPDX 声明，直接依赖的 Core、Gui、Network（qtbase），Qml、QmlModels、Quick、QuickControls2、QuickDialogs2（qtdeclarative）及 Svg（qtsvg）均列有 `LGPL-3.0-only` 等许可选项。Test 仅用于测试，不进入产品；Qt 平台插件、QML 导入、翻译及嵌入的第三方代码需按实际发行文件逐项核对，不能把上述模块结论扩大到整个 SDK。Qt 有仅 GPL 或其他条款的组件，升级或新增模块时须重新核对[版本许可说明](https://doc.qt.io/qt-6.8/licensing.html)、随该版 SDK 的 SPDX 和组件许可。

按 [LGPL 3.0](https://www.gnu.org/licenses/lgpl-3.0.html) 及适用的 GPL 3.0 条款，面向接收者分发该动态组合时，需要结合具体包和分发方式落实：

- 明确告知使用 Qt，提供适用许可原文、版权和更改通知；界面若显示版权信息，也应按条款包含库的相关通知。嵌入的第三方组件按自身许可分别保留通知。
- 通过条款允许的方式提供与实际分发库对应的完整源码，包括适用的修改和构建／安装所需材料；要核对实际 SDK 字节、补丁和配置，不能仅因存在同版本上游源码压缩包就认定对应完整。若采用书面提供方式，应逐项遵守适用条件与有效期，不把单个上游链接当成自动履约。
- 不禁止为调试对库的修改而逆向工程；动态库机制须允许接收者使用接口兼容的修改版库。适用时还需提供安装信息并允许修改后的版本运行。替换库、插件或 QML 模块后应在隔离副本验证，静态链接、签名或安装策略变化须重新审查。

当前候选会收集固定哈希的 Qt 原始源码归档、许可文本和 SDK SPDX，生成配套材料与替换说明，详见[发行设计](../technical/release.md)；材料存在并不证明 SDK 对应源码、插件归属、第三方许可或替换运行已完成全部核验。公开发行前按实际候选完成这些检查、记录结果并履行适用义务；[Qt 官方义务说明](https://www.qt.io/development/open-source-lgpl-obligations)和许可原文是核对依据。项目自身 Apache-2.0 不改变 Qt 或第三方条款。

### 维护渠道

`toolchain.json` 固定 Qt 6.8.2 和现有公开归档，不承诺该补丁仍获得安全维护。Qt 将 6.8 作为 LTS；[Qt 版本政策](https://doc.qt.io/qt-6/qt-releases.html)区分开源初期补丁与商业客户的 LTS 补丁及支持。当前没有证据表明本项目持有商业支持；维护者需关注公开安全公告、补丁来源及自身使用渠道，评估升级或自维护，更新工具清单、依赖记录和相关构建／界面／部署验证。不能因为版本标为 LTS 就将商业补丁、技术支持或具体修复视为已获得。

## 离线拼音数据

[Issue #8](https://github.com/xiaodaozhi/xdstarter/issues/8) 使用 Unicode Unihan 17.0.0 的四个普通话字音属性，生成去声调、保留 `ü → v` 区分的派生表。官方[数据使用条款](https://www.unicode.org/copyright.html)指定 Unicode License v3，除非发布位置另有声明；所选四字段没有附加许可声明。该许可允许使用、修改及再分发，但须在数据／软件副本或其随附文档中保留版权与许可通知。

完整 [Unicode License v3 原文](../../third_party/unicode/LICENSE.txt)、源版权通知、固定版本、转换范围和本地计算哈希存放于 [third_party/unicode](../../third_party/unicode/README.md) 与 [manifest](../../third_party/unicode/manifest.json)。原始 ZIP 不入库，运行不下载数据；规范 TSV 和 C++ 表作为派生数据一同维护。未来二进制／安装包必须携带这些通知，不能只打包生成表而遗漏许可材料。

这只记录特定第三方数据的使用依据，不改变项目自身的 Apache-2.0 许可，也不完成 Qt 或安装包的整体发行审查。没有引入带其他许可要求的词级拼音库或词库。

## 依赖清单要求

每个引入的第三方依赖记录名称、精确版本、上游地址、许可、用途、修改与再分发材料。拼音库/词库、图标、字体、安装器与测试工具也纳入清单。当前工程与 CI 的版本及来源见[工程依赖记录](../technical/dependencies.md)；Qt 二进制由开发者或 CI 取得，不入源码仓库；候选运行目录与安装器包含第三方运行库，公开发行尚未完成。

设计稿中的第三方应用图标为简化示意，原始应用名称/标志归其权利人。品牌几何源和 PNG 字标分别记录；运行时使用系统字体与从已安装应用提取的图标，避免分发未获授权的字体或图标集合。

依据：[Apache-2.0 许可原文](../../LICENSE)、[Qt 许可](https://doc.qt.io/qt-6/licensing.html)、[Qt LGPL 义务说明](https://www.qt.io/development/open-source-lgpl-obligations)。每个候选的第三方发行审查结果以关联 Issue 和实际工件为准。
