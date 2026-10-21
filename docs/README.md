# 文档导航

本目录保存稳定的产品规格、设计、技术决策和协作规范。Qt Quick 原型已实现真实应用目录、图标、启动与固定/取消固定，三档磁贴编辑及 schemaVersion 2 布局与主题恢复，并接入快捷栏、圆形头像、软件图标、托盘和无边框菜单焦点交互。[#16](https://github.com/xiaodaozhi/xdstarter/issues/16) 统一可见面板左侧/底部 8 DIP 边距，接入自绘外观与隐藏阶段定位；[#22](https://github.com/xiaodaozhi/xdstarter/issues/22) 接入默认关闭、可显式启用及暂停的单独 Win 键原型，系统开始按钮保持原生入口。[#8](https://github.com/xiaodaozhi/xdstarter/issues/8) 增加目录与固定目标的离线名称、英文、拼音和首字母搜索，使用独立后台索引与有界结果列表；本轮 [#9](https://github.com/xiaodaozhi/xdstarter/issues/9) 增加编辑模式的分组新建、重命名、排序及空组删除。验收发现的 [#28](https://github.com/xiaodaozhi/xdstarter/issues/28) 另补隐藏窗口跨屏迁移、逻辑尺寸保持和有界稳定后显示；本机 96/144 DPI 原生往返测量与实际产品验收分别记录。当前与历史构建、实际窗口证据及未覆盖环境统一见[测试策略](technical/testing.md)，历史结果不替代新版本验收。正式工具清单与 xdzip 核实的版本对齐，继续使用本项目 Qt 6.8.2 的获取与构建入口。磁贴显示名称编辑、导入界面、透明背景材料与其余系统接管继续按 Issues 推进，父 [#6](https://github.com/xiaodaozhi/xdstarter/issues/6) 不因局部原型交付关闭。

开始按钮后续方案由 [Issue #24](https://github.com/xiaodaozhi/xdstarter/issues/24) 先验证原生窗口身份与只读命中，见 [ADR-0009](technical/adr/0009-start-button-hit-probe.md)。研究查询与产品唤起分开；一次可信矩形或命中候选不表示已可接管点击。

## 产品与设计

| 文档 | 内容 |
| --- | --- |
| [产品愿景](product/vision.md) | 目标用户、价值与首版边界 |
| [产品需求](product/requirements.md) | 功能要求与验收依据 |
| [交互设计](product/interaction.md) | 菜单、搜索、编辑与异常状态 |
| [阶段路线](product/roadmap.md) | 里程碑目标与 GitHub 跟踪入口 |
| [视觉与资产](design/README.md) | Logo、当前设计稿、网格参数与重建方法 |

## 技术设计

| 文档 | 内容 |
| --- | --- |
| [技术架构](technical/architecture.md) | 模块边界与数据流 |
| [磁贴布局](technical/tile-layout.md) | 网格算法、拖拽、缩放与对齐 |
| [数据与持久化](technical/data-persistence.md) | 配置、标识、恢复与兼容性 |
| [Windows 集成](technical/windows-integration.md) | 唤起、焦点、接管与降级验证 |
| [开发环境](technical/development.md) | Qt、编译器、工具及当前可运行检查 |
| [工程依赖](technical/dependencies.md) | 固定工具版本、来源与许可审查边界 |
| [测试策略](technical/testing.md) | 自动检查、真机矩阵与性能目标 |
| [发行设计](technical/release.md) | 部署依赖、安装、更新与回退 |
| [ADR-0001：Qt Quick](technical/adr/0001-qt-quick.md) | 技术选择与取舍 |
| [ADR-0002：工具链基线](technical/adr/0002-toolchain-baseline.md) | 与 xdzip 一致的正式工具清单、Windows 基线与附加兼容验证 |
| [ADR-0003：布局快照](technical/adr/0003-layout-snapshot.md) | 原型 schema、后台保存、备份恢复与未来版本保护 |
| [ADR-0004：真实应用目标](technical/adr/0004-application-catalog.md) | Shell 应用目录、图标与启动边界、固定操作和 schema 2 迁移 |
| [ADR-0005：普通窗口桌面集成](technical/adr/0005-desktop-shell-prototype.md) | 快捷动作后台边界、原生托盘恢复、可信定位与系统接管的阶段区分 |
| [ADR-0006：菜单弹层与焦点](technical/adr/0006-menu-popup.md) | 历史原生外观选择；延后焦点复核与可恢复收起继续有效 |
| [ADR-0007：自绘菜单与统一边距](technical/adr/0007-custom-menu-chrome.md) | 自绘圆角/边框/阴影、透明外窗、对称工作区间距与隐藏阶段定位 |
| [ADR-0008：单独 Win 键原型](technical/adr/0008-win-key-capture.md) | 默认关闭、专用输入线程、组合键放行、有界释放派发与失败停用 |
| [ADR-0009：开始按钮命中研究](technical/adr/0009-start-button-hit-probe.md) | 显式只读原生身份与采样、共享窗口降级及后续接管门槛 |
| [ADR-0010：离线应用搜索](technical/adr/0010-local-application-search.md) | Qt Core 后台索引、名称与多音拼音匹配、查询代次及 Unicode 数据许可 |

## 协作与规范

- [Issue → PR → 验收流程](process/workflow.md)
- [Issue 分类与优先级](process/issue-management.md)
- [完成定义](process/definition-of-done.md)
- [C++ / QML 技术规范](standards/cpp-qml.md)
- [文档与设计版本规范](standards/documentation.md)
- [许可与依赖决策](governance/licensing.md)
- [贡献指南](../CONTRIBUTING.md)

需求和 Bug 的最新状态请查 [Issues](https://github.com/xiaodaozhi/xdstarter/issues)；阶段完成度请查 [Milestones](https://github.com/xiaodaozhi/xdstarter/milestones)。文档中的里程碑描述不代表该阶段已完成。
