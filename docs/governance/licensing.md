# 许可与依赖决策

状态：项目代码、文档和原创设计资产采用 [Apache License 2.0](../../LICENSE)。第三方组件及数据的独立许可和通知另行保留。

## 项目许可范围

按项目所有者的决定，项目代码、文档和原创 Logo／设计资产适用仓库根目录的 Apache-2.0 许可。第三方内容仍按其自身条款使用和分发；Apache-2.0 不额外授予商标权。Windows 运行包在 `share/notices/XDStarter-LICENSE.txt` 携带项目许可原文。仓库可见性由所有者单独管理。

[Issue #3](https://github.com/xiaodaozhi/xdstarter/issues/3) 继续跟踪 Qt 许可渠道及具体发行义务，项目许可证的选定不等于完成第三方组件或安装包的发行审查。

## Qt 许可方向

Qt 具有开源与商业许可选项。多数核心模块提供 LGPL 路径，部分模块的开源许可可能仅为 GPL。需要按实际使用模块核对，不以“使用 Qt”直接推导应用必须开源或必然免费发行。

当前采用动态库部署方向，仍需按实际选用模块和 Qt 许可渠道核对通知、依赖许可文本、源代码提供／重新链接等适用要求。Qt LTS 维护与补丁可用性按取得的软件版本和许可渠道记录。

## 离线拼音数据

[Issue #8](https://github.com/xiaodaozhi/xdstarter/issues/8) 使用 Unicode Unihan 17.0.0 的四个普通话字音属性，生成去声调、保留 `ü → v` 区分的派生表。官方[数据使用条款](https://www.unicode.org/copyright.html)指定 Unicode License v3，除非发布位置另有声明；所选四字段没有附加许可声明。该许可允许使用、修改及再分发，但须在数据／软件副本或其随附文档中保留版权与许可通知。

完整 [Unicode License v3 原文](../../third_party/unicode/LICENSE.txt)、源版权通知、固定版本、转换范围和本地计算哈希存放于 [third_party/unicode](../../third_party/unicode/README.md) 与 [manifest](../../third_party/unicode/manifest.json)。原始 ZIP 不入库，运行不下载数据；规范 TSV 和 C++ 表作为派生数据一同维护。未来二进制／安装包必须携带这些通知，不能只打包生成表而遗漏许可材料。

这只记录特定第三方数据的使用依据，不改变项目自身的 Apache-2.0 许可，也不完成 Qt 或安装包的整体发行审查。没有引入带其他许可要求的词级拼音库或词库。

## 依赖清单要求

每个引入的第三方依赖记录名称、精确版本、上游地址、许可、用途、修改与再分发材料。拼音库/词库、图标、字体、安装器与测试工具也纳入清单。当前工程与 CI 的版本及来源见[工程依赖记录](../technical/dependencies.md)；Qt 二进制由开发者或 CI 取得，没有第三方运行库入库或安装包再分发。

设计稿中的第三方应用图标为简化示意，原始应用名称/标志归其权利人。品牌几何源和 PNG 字标分别记录；运行时使用系统字体与从已安装应用提取的图标，避免分发未获授权的字体或图标集合。

依据：[Apache-2.0 许可原文](../../LICENSE)、[Qt 许可](https://doc.qt.io/qt-6/licensing.html)、[Qt LGPL 义务说明](https://www.qt.io/development/open-source-lgpl-obligations)。Qt 许可渠道与发行审查结果以关联 Issue/PR 为准。
