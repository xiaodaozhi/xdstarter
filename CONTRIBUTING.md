# 贡献指南

开始前请阅读[文档导航](docs/README.md)和[产研流程](docs/process/workflow.md)。

1. 先搜索 GitHub Issues，复用已有讨论；需求、Bug、研究任务使用对应模板。
2. 实现前补齐目标、范围和验收标准。涉及技术方向变化时先更新 ADR。
3. 使用 `feat/<issue>-<slug>`、`fix/<issue>-<slug>` 或 `docs/<issue>-<slug>` 分支。
4. 提交前运行适用检查，并在 PR 中写明实际验证环境与结果。
5. PR 关联 Issue。完成全部验收后使用 `Closes #编号`；仅完成部分时使用 `Refs #编号`。

使用中文撰写项目文档与 Issue，代码标识符使用英文。提交信息使用 Conventional Commits，例如 `docs: initialize product and technical documentation`。

现阶段从修正文档、验证设计稿、复现 Windows 集成行为入手。不要将设计稿或规划文档中的目标写成已实现能力。

[C++ / QML 规范](docs/standards/cpp-qml.md) · [文档规范](docs/standards/documentation.md) · [完成定义](docs/process/definition-of-done.md)
