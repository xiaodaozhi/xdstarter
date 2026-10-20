# 文档导航

本目录保存稳定的产品规格、设计、技术决策和协作规范。当前基线建立于 2026-10-20，Qt Quick 技术选型已确认，运行时功能仍待开发。

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
| [测试策略](technical/testing.md) | 自动检查、真机矩阵与性能目标 |
| [发行设计](technical/release.md) | 部署依赖、安装、更新与回退 |
| [ADR-0001：Qt Quick](technical/adr/0001-qt-quick.md) | 技术选择与取舍 |

## 协作与规范

- [Issue → PR → 验收流程](process/workflow.md)
- [Issue 分类与优先级](process/issue-management.md)
- [完成定义](process/definition-of-done.md)
- [C++ / QML 技术规范](standards/cpp-qml.md)
- [文档与设计版本规范](standards/documentation.md)
- [许可与依赖决策](governance/licensing.md)
- [贡献指南](../CONTRIBUTING.md)

需求和 Bug 的最新状态请查 [Issues](https://github.com/xiaodaozhi/xdstarter/issues)；阶段完成度请查 [Milestones](https://github.com/xiaodaozhi/xdstarter/milestones)。文档中的里程碑描述不代表该阶段已完成。
