<img src="docs/design/brand/xdstarter-mark.svg" width="80" alt="XDStarter 标志">

# XDStarter

**让 Windows 11 的开始菜单，重新按你的习惯展开。**

XDStarter 是一款面向 Windows 11 的自定义磁贴开始菜单项目：保留 Windows 10 的应用列表与磁贴分组，结合 Windows 11 的现代视觉，支持整理常用应用、键盘搜索与快速启动。

> 当前处于工程原型阶段。Qt Quick 原型已实现真实应用目录、系统图标、启动与固定/取消固定、三档磁贴编辑，以及布局与主题保存恢复。无边框菜单已接入快捷栏、圆形头像、软件图标、托盘恢复与外部焦点收起，并采用自绘单层圆角、边框、阴影及隐藏阶段定位。单独 Win 键提供默认关闭、可显式启用及暂停的局部原型，系统开始按钮保持原生入口。应用名称、英文、拼音全拼与首字母支持离线搜索；[#9](https://github.com/xiaodaozhi/xdstarter/issues/9) 接入分组新建、重命名、排序与空组删除。构建、自动检查和实际 Windows 窗口证据按版本记录于[测试记录](docs/technical/testing.md)，历史结果不替代本轮验收。磁贴显示名称覆盖及搜索别名已接入，原名和启动目标保留；其余系统接管继续开发，尚未提供可安装的软件。下面展示的是设计稿。

![XDStarter 深色界面设计稿](docs/design/mockups/v2/xdstarter-desktop-dark-v2.png)

[浅色设计稿](docs/design/mockups/v2/xdstarter-desktop-light-v2.png) / [磁贴尺寸规范](docs/design/mockups/v2/xdstarter-tile-grid-v2.png) / [完整设计资产](docs/design/README.md) / [运行时视觉验收](docs/design/validation/issue-29/README.md)

当前设计稿为 **v2 修订 2.2**，将顶部搜索、紧凑应用目录、分组磁贴和工具入口统一为完整布局；[Issue #29](https://github.com/xiaodaozhi/xdstarter/issues/29) 跟踪实际 QML 落地与视觉验收。参考画布为 1920×1200、150% 缩放，默认面板 980×680 DIP。可见面板距工作区左侧和底部任务栏均为 8 DIP，不随开始按钮位置切换。旧修订已[归档](docs/design/archive/v2-revision-2.1/README.md)。

[开发素材](docs/design/assets/README.md)提供深浅主题、多缩放透明 PNG/SVG、三档磁贴底图和 [Windows 多帧软件图标](docs/design/assets/app/xdstarter.ico)，共255项，规格见[素材清单](docs/design/assets/manifest.json)。快捷栏、圆形头像及软件图标已接入运行窗口；QML自绘12 DIP圆角、单层主题边框与阴影，Windows原生装饰被禁用。外窗透明区容纳阴影与圆角，面板采用主题实色；背景材料继续由系统集成研究跟踪。设计导出与实际运行证据分别记录。


## 我们准备做什么

- 按 Win 键或点击开始按钮打开自定义菜单；保留系统组合键与恢复入口。
- 在应用列表旁固定磁贴，自由调整分组、位置和大小。
- 提供小正方形、标准正方形、宽磁贴三种尺寸，统一间隙，精确对齐。
- 支持中文、拼音及英文应用搜索，首版保持本地运行。
- 提供深浅主题、键盘操作和辅助功能支持。

磁贴与分组编辑、应用目录、搜索与启动已接入工程原型，单独 Win 键提供可选开启的局部原型；磁贴显示名称编辑使用每磁贴覆盖，开始按钮接管等目标继续按 Issues 推进。实际验证范围与未验收环境以测试记录为准。

## 技术方向

**Qt Quick / QML + C++20 + CMake**。产品首先面向 Windows 11 x64；通用界面和布局逻辑可在 macOS 开发验证，Windows 系统集成单独实现。技术选择见 [ADR-0001](docs/technical/adr/0001-qt-quick.md)，与 xdzip 一致的正式构建工具基线见 [ADR-0002](docs/technical/adr/0002-toolchain-baseline.md)。

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

文档和设计资产检查：

```sh
python3 scripts/check_repository.py
```

无 Qt 的核心构建和测试（需 C++20 编译器、CMake 3.25+ 和 Ninja）：

```sh
cmake --preset core-debug
cmake --build --preset core-debug
ctest --preset core-debug
```

正式工具版本由本项目 [toolchain.json](toolchain.json) 声明，与 xdzip 已核实的 Windows 构建环境对齐：Qt 6.8.2、MSVC 19.29.30153.0/v142、Windows SDK 10.0.19041.0、Python 3.12.13、CMake 4.4.4、Ninja 1.13.2。项目拥有自己的工具和 Qt 缓存；已有相同 SDK 可作为下载缓存复用。MSVC 2022 CI 是另行标记的附加兼容检查。

```powershell
# Python 参数需指向声明的 Python 3.12.13；首次获取项目工具和 Qt。
./scripts/build-windows.ps1 -InstallTools -Python python -Preset app-release -Fresh
# 使用隔离文件验证，避免改变日常布局。
$env:PATH = "$PWD/build/Qt/6.8.2/msvc2022_64/bin;$env:PATH"
& ./build/app-release/bin/xdstarter.exe --layout-file "$PWD/build/manual-layout.json"
```

正常首次启动在托盘后台驻留，不弹出菜单；开始按钮或托盘图标可唤起同一实例。后台读取当前用户／公共开始菜单和打包应用，同层同相对路径文件夹合并并读取系统本地化显示名。在“所有应用”中单击或 Enter 启动，拖到磁贴区或通过右键／Shift+F10“固定为磁贴”，默认固定到最后一个分组；列表不显示逐项图钉。已固定应用可在右键菜单“取消固定磁贴”，不卸载应用。窄窗口支持目录与磁贴切换及受控拖拽。

顶部搜索框或 Ctrl+F 可搜索应用目录与已固定目标，支持名称、英文、拼音全拼和首字母，例如“画图／huatu／ht”。结果按目标 id 去重，名称完整、前缀、包含、全拼、首字母依次排序，同档按规范化名称和 id 稳定排列；方向键选择、Enter 启动。搜索在专用后台线程运行，更新查询或目录后丢弃旧结果，最多展示前 100 项并提示总数；固定但失效的应用保留不可用说明。输入法预编辑不触发查询或启动，Esc 先交给输入法。清空查询还原浏览视图与滚动位置；重新唤起聚焦搜索框，保留当前进程中的查询，重启不保存查询。拼音来自随程序编译的 [Unicode Unihan 离线数据](third_party/unicode/README.md)，不发起网络搜索，也不把输入当成命令执行；范围及限制见 [ADR-0010](docs/technical/adr/0010-local-application-search.md)。

左侧圆形头像打开账户设置，文档、下载和设置打开受控系统入口；底部快捷按钮间距收紧，48×48 DIP命中区保持。电源文字菜单提供独立的Win键、开始按钮接管启用／暂停及退出，系统关机等操作尚未启用。菜单为置顶工具窗口，显示或隐藏均不占系统任务栏窗口按钮，通知区图标保留；自有对话框可覆盖菜单。外部点击、开始按钮、收起按钮和Esc共用收尾，退出动画后真正隐藏；已提交布局保留。托盘失效时暂停接管并显示持续可见的重试／退出恢复界面，不依赖最小化的任务栏按钮。

每次启动默认关闭单独Win键唤起，不随布局恢复；支持且托盘可用时可显式启用。开始按钮点击服务在托盘注册成功后启用，仅接管取得实时点资格的完整左键；未识别点、右键和Win组合键保留原生路径。暂停、失败或退出释放对应接管，托盘恢复不自动重启已暂停的服务。当前物理验证范围按[测试策略](docs/technical/testing.md)记录，不扩大为所有Windows构建兼容。

默认可直接拖拽磁贴，或Ctrl+方向键移动；磁贴右键／Shift+F10的“磁贴大小”子菜单提供小磁贴 (1×1)、中磁贴 (2×2)、宽磁贴 (4×2)。实体几何仍为48/104/216 DIP、8 DIP间隙。窗口宽度按共享56 DIP步距吸附，实际可用列随宽度改变；扩列保位，缩列原子重排，空间不足保留原布局。窗口最大可见面板不超过扣除任务栏与四方8 DIP边距的工作区。布局schema4记录gridColumns并严格兼容读取1/2/3；旧原字节在迁移保存时备份，未来版本只读。普通运行使用用户应用数据目录，smoke不连接真实目录／托盘或读写布局。

从“更多 → 新建分组”管理分组，组标题菜单提供重命名、上移、下移和删除空组；组名严格校验，最多32组且至少保留一组。磁贴菜单可修改显示名称或恢复原名，仅影响当前磁贴；原名和覆盖名均可搜索。布局与主题自动合并保存，也可从更多菜单保存布局或导入布局。导入经严格校验、只读预览和确认后先落盘再发布，取消或失败保留内存；不再显示“恢复上一份布局”菜单项。详情见[导入事务](docs/technical/adr/0012-layout-import-transaction.md)与[动态列ADR](docs/technical/adr/0015-responsive-tile-columns.md)。

完整命令与支持边界见[开发环境](docs/technical/development.md)、[测试策略](docs/technical/testing.md)。进度与证据见 [#4 工程](https://github.com/xiaodaozhi/xdstarter/issues/4)、[#5 几何](https://github.com/xiaodaozhi/xdstarter/issues/5)、[#7 应用目录与启动](https://github.com/xiaodaozhi/xdstarter/issues/7)、[#8 离线搜索](https://github.com/xiaodaozhi/xdstarter/issues/8)、[#9 编辑](https://github.com/xiaodaozhi/xdstarter/issues/9)、[#10 保存恢复](https://github.com/xiaodaozhi/xdstarter/issues/10)、[#14 设计接入与桌面恢复](https://github.com/xiaodaozhi/xdstarter/issues/14)、[#15 无边框菜单与焦点](https://github.com/xiaodaozhi/xdstarter/issues/15)、[#16 统一边距与自绘外观](https://github.com/xiaodaozhi/xdstarter/issues/16)、[#22 单独 Win 键原型](https://github.com/xiaodaozhi/xdstarter/issues/22)；设计重建方法见[设计资产说明](docs/design/README.md)。

## 许可

项目代码与原创设计资产的发布许可证尚待项目所有者确定，暂未授予通用开源许可。Qt 及其他第三方组件遵循各自许可证；设计稿中的第三方应用图标为视觉示意。详见[许可与依赖决策](docs/governance/licensing.md)。
