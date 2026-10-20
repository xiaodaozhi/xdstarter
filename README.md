<img src="docs/design/brand/xdstarter-mark.svg" width="80" alt="XDStarter 标志">

# XDStarter

**让 Windows 11 的开始菜单，重新按你的习惯展开。**

XDStarter 是一款面向 Windows 11 的自定义磁贴开始菜单项目：保留 Windows 10 的应用列表与磁贴分组，结合 Windows 11 的现代视觉，支持整理常用应用、键盘搜索与快速启动。

> 当前处于前期准备与原型规划阶段。已完成产品方向、视觉设计、技术选型和协作规范，尚未提供可安装的软件；下面展示的是设计稿。

![XDStarter 深色界面设计稿](docs/design/mockups/v2/xdstarter-desktop-dark-v2.png)

[浅色设计稿](docs/design/mockups/v2/xdstarter-desktop-light-v2.png) · [磁贴尺寸规范](docs/design/mockups/v2/xdstarter-tile-grid-v2.png) · [完整设计资产](docs/design/README.md)

## 我们准备做什么

- 按 Win 键或点击开始按钮打开自定义菜单；保留系统组合键与恢复入口。
- 在应用列表旁固定磁贴，自由调整分组、位置和大小。
- 提供小正方形、标准正方形、宽磁贴三种尺寸，统一间隙，精确对齐。
- 支持中文、拼音及英文应用搜索，首版保持本地运行。
- 提供深浅主题、键盘操作和辅助功能支持。

以上为计划能力。实际兼容范围、性能与系统接管效果将在 Windows 真机原型中验证。

## 技术方向

**Qt Quick / QML + C++20 + CMake**。产品首先面向 Windows 11 x64；通用界面和布局逻辑可在 macOS 开发验证，Windows 系统集成单独实现。技术基线与选择理由见 [ADR-0001](docs/technical/adr/0001-qt-quick.md)。

磁贴逻辑尺寸为 48×48、104×104、216×104 DIP，间隙 8 DIP。布局数据、界面渲染和 Win32 集成保持清晰边界。

## 参与项目

- [提出需求](https://github.com/xiaodaozhi/xdstarter/issues/new?template=feature.yml)
- [报告 Bug](https://github.com/xiaodaozhi/xdstarter/issues/new?template=bug.yml)
- [查看全部任务](https://github.com/xiaodaozhi/xdstarter/issues)
- [查看阶段里程碑](https://github.com/xiaodaozhi/xdstarter/milestones)
- [贡献指南](CONTRIBUTING.md)

需求讨论、任务进度、Bug 复现与验收统一记录在 GitHub Issues 和关联 PR 中。文档保存稳定规格与决策，进度以 GitHub 为准。

## 文档与本地检查

[文档导航](docs/README.md)包含产品需求、交互设计、技术架构、开发规范、测试与发布流程。

当前仓库可运行的检查仅针对文档和设计资产：

```sh
python3 scripts/check_repository.py
```

设计稿的可选重建方法见[设计资产说明](docs/design/README.md)。Qt 应用工程将在后续原型任务中建立；当前没有应用启动或安装命令。

## 许可

项目代码与原创设计资产的发布许可证尚待项目所有者确定，暂未授予通用开源许可。Qt 及其他第三方组件遵循各自许可证；设计稿中的第三方应用图标为视觉示意。详见[许可与依赖决策](docs/governance/licensing.md)。
