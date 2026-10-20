# 产研流程

## 单一跟踪入口

需求、Bug、研究与交付任务统一使用 GitHub Issues。Issue 保存场景、验收、负责人、优先级、进度与阻塞；PR 保存实现、审查与验证；文档保存稳定规格与决策。

聊天中形成的新需求或 Bug 也应先查重并建立/关联 Issue，再更新文档和实现。没有实际缺陷证据时使用研究任务，不虚构 Bug。

## 从提出到交付

1. **提出**：使用模板，补齐问题/价值、范围、验收或复现。进入 `status:needs-triage`。
2. **分流**：确定 type、priority、area 和 Milestone；缺信息在原 Issue 追问。达到可开发条件后标 `status:ready`。
3. **实施**：认领负责人，切分支，设 `status:in-progress`。范围变化或阻塞及时写入原 Issue，并链接相关任务。
4. **评审**：PR 链接 Issue、相关设计与测试证据。架构变更含 ADR；功能变化同步产品与技术文档。
5. **验收**：按 Issue 标准检查实际行为；需要真机的内容提供 Windows 构建与环境记录。
6. **关闭**：全部标准满足后由 PR `Closes` 或维护者关闭；仅交付部分使用 `Refs` 并保留 Issue。

阻塞时使用 `status:blocked`，注明阻塞任务、需要的输入和恢复条件。解决后回到 ready/in-progress。已关闭 Issue 无需保留进行中的 status 标签。

## 可开发条件

清楚说明目标用户与场景、范围、不做什么、验收、受影响文档与关键依赖。研究任务需要明确要回答的问题和结果形式，不要求先知道答案。

## 审查与合并

使用短生命周期分支，默认 squash merge。Conventional Commit 表达最终变更。PR 检查通过后合并；代码变更尽量由另一维护者审查，单维护者项目可记录自审与实际验证证据。

Branch protection 和组织级 Projects 不在本次初始化中强制修改。仓库级 Issues、Labels、Milestones 和模板即可支撑当前规模；需要额外权限或工作流时另开任务。

完成定义见[DoD](definition-of-done.md)，分类规则见[Issue 管理](issue-management.md)。
