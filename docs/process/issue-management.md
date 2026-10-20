# Issue 管理规范

## 类型、优先级与范围

| 分类 | 标签 | 规则 |
| --- | --- | --- |
| 类型 | type:feature / type:bug / type:research / type:docs | 选择一个主要类型 |
| 优先级 | priority:P0 / priority:P1 / priority:P2 | 分流时确定一个 |
| 状态 | status:needs-triage / status:ready / status:in-progress / status:blocked | 开放 Issue 同时保持一个主要状态 |
| 范围 | area:ui / area:windows / area:core / area:docs / area:build / area:release | 可选择多个 |

P0：阻塞关键可行性或日常系统使用，例如 Win 组合键被破坏、无法恢复原生菜单。P1：里程碑核心能力或明确影响使用的缺陷。P2：不阻塞当前里程碑的改进与体验优化。

优先级表示排序，不表示已承诺交付时间。维护者按风险、依赖和收益分流。

## 需求 Issue

写明使用场景、目标行为、范围、验收标准，链接 PRD 编号和设计稿。避免只写“支持某功能”而没有可验证结果。任务过大时用父 Issue 的 task list 关联子 Issue，状态仍由子 Issue 自身维护。

## Bug Issue

提供软件版本/commit、Windows 精确构建、显示器/DPI/任务栏、Qt/渲染后端、复现步骤、预期与实际、影响与日志截图。没有软件版本的设计问题标注设计稿版本。

复现后补充最小用例；修复 PR 说明根因与回归验证。无法复现先保留待分流，并说明缺少信息。重复任务链接原 Issue 后以 not planned 关闭；回归原问题可以重开并补充版本证据。

## 状态更新与里程碑

开始、范围变化、阻塞、完成验证时更新 Issue。评论以新增证据为主，避免重复无变化的状态。每个进入实施的 Issue 关联 Milestone，关闭时链接交付 PR 和验收结果。

初始化定义位于 `.github/project-bootstrap.json`；脚本只确保标签、Milestone 和初始任务存在，不覆盖已有任务正文、负责人或状态。项目进展不回写成第二份本地任务表。
