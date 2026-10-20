# XDStarter 工作约定

- 产品是 Windows 11 磁贴开始菜单工具；已选定 Qt Quick / QML + C++，不要自行改回 WinUI、WebView 或其他 UI 技术栈。
- 开始任务先阅读 `docs/README.md`、相关产品/技术文档和对应 GitHub Issue。
- 需求、Bug 和进展统一跟踪于 `xiaodaozhi/xdstarter` 的 GitHub Issues。先查重再建 Issue；更新实现进展、验证证据与阻塞原因，不以本地 TODO 文档替代 GitHub 状态。
- 仓库当前以文档与设计准备为主。明确区分已实现、待验证、规划目标；不要宣称尚未运行的构建、测试或 Windows 兼容性通过。
- 图形与布局以 `docs/design/tokens/tile-grid.json` 为准：48 DIP 小格、8 DIP 间隙，标准 2×2、宽磁贴 4×2；保持共享几何，避免单独修改图片造成失配。
- 当前设计稿在 `docs/design/mockups/v2/`；历史探索在 `docs/design/archive/`，不能将历史生成图作为像素验收基准。
- 通用布局、领域模型和 QML 不依赖 Win32；系统接管由 Windows 平台模块承担，输入回调仅派发事件，保留原生系统恢复路径。
- 代码、文档、设计变更在同一 PR 中保持一致。涉及范围/架构/数据兼容性时更新对应文档与 ADR。
- 修改后运行 `python3 scripts/check_repository.py`，再根据变更执行适用检查；仅在新增风险或失败时扩大验证。
- 不提交密钥、机器路径配置、构建目录或发布 ZIP。许可证尚未确定，不擅自添加 MIT/GPL 等授权。

遵循 `CONTRIBUTING.md`、`docs/process/` 与 `docs/standards/`。用户在当前会话中的明确指令优先。
