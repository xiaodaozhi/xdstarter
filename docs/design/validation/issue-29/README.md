# v2 修订 2.2 运行时视觉验收

对应 [Issue #29](https://github.com/xiaodaozhi/xdstarter/issues/29)。本目录保存 Qt 6.8.2 的匿名运行时证据，设计基准见[设计说明](../../README.md)，正式构建与真实产品交互见[测试记录](../../../technical/testing.md)。这些图片由产品 QML/controller 实际渲染，没有将设计整图贴入窗口，也没有读取用户布局或真实应用目录。

## 视觉及产品验收程序

| 程序 | SHA-256 |
| --- | --- |
| 产品 | `34f18e80c19b5d81356ad71dec76c6fc7870c32c34e26d86ca2cf46d9131cb23` |
| GUI 及显式视觉探针 | `35c4a04c00e72703ef986cb06d66c2fd112e244d4e7bfc477b082c4e21b184ff` |

[源码摘要](source-digests.json) 按 UTF-8、LF 换行规范化后计算 SHA-256，覆盖相关 QML、controller、目录状态、tokens 和测试源文件。程序散列来自本机视觉验收时的正式 Release；附加 MSVC 2022 CI 的输出属于另一构建，不应具有相同二进制散列。

后续 CI 暴露跨组滚动用例缺少稳定的几何前提，追加的[测试修正](ci-scroll-regression.json)只控制该用例的窗口高度及目标越界前提，没有改变产品、QML 或显式视觉探针。以下图片和元数据保留实际抓帧程序 `35c4a04c…` 的身份；修正后的最终 GUI 与构建结果另见[测试记录](../../../technical/testing.md)，产品散列保持相同。

## 深浅主题与状态

[原生元数据](visual-acceptance-probe.json) 记录 Windows kernel `10.0.26300`、Windows QPA、Direct3D 11、DPR 1，共 **22/22** 状态通过；`errors` 与 `qmlWarnings` 为空。以下均为 `QQuickWindow::grabWindow()` 完整窗口，默认外窗 1028×728、可见面板 980×680 DIP，窄窗外窗 528×528 DIP。原始 capture 具有 alpha，四角透明；命名弹层的蒙层限制在同一圆角面板内。

| 状态 | 浅色 | 深色 |
| --- | --- | --- |
| 浏览 | [截图](light-browse.png) | [截图](dark-browse.png) |
| 更多菜单 | [截图](light-more-menu.png) | [截图](dark-more-menu.png) |
| 搜索结果 | [截图](light-search-results.png) | [截图](dark-search-results.png) |
| 无搜索结果 | [截图](light-search-empty.png) | [截图](dark-search-empty.png) |
| 编辑 | [截图](light-edit.png) | [截图](dark-edit.png) |
| 命名弹层 | [截图](light-group-name-dialog.png) | [截图](dark-group-name-dialog.png) |
| 分组菜单 | [截图](light-group-menu.png) | [截图](dark-group-menu.png) |
| 失效目标 | [截图](light-unavailable.png) | [截图](dark-unavailable.png) |
| 窄窗 | [截图](light-narrow.png) | [截图](dark-narrow.png) |
| 空布局及目录 | [截图](light-empty.png) | [截图](dark-empty.png) |
| 加载 | [截图](light-loading.png) | [截图](dark-loading.png) |

已目视核对文字、图标、头像、边框、菜单和滚动裁切。匿名图标用于稳定验证布局与图标加载状态，不模拟第三方品牌；Segoe UI Variable 的中文使用系统字体回退。自动断言覆盖状态、几何、图标就绪、新帧与原始 alpha，不代表自动计算了与设计图的逐像素相似度。

同一最终 GUI 的[离屏摘要](offscreen-summary.json) 也为 22/22，但原始离屏 capture 没有 alpha。其转换后的 RGBA 不能证明原生透明或桌面合成；离屏 PNG 仅保留本地 build 目录。

## 五档原生像素专项

[最终矩阵](native-matrix.json) 对两块已连接显示器使用测试进程的 `QT_SCREEN_SCALE_FACTORS` 同目标 DPR 覆盖，固定 `QT_SCALE_FACTOR=1`，没有修改 Windows 显示设置。每档执行 `renderedGroups` 的初始化、抓帧与清理 **3/3**，包含深浅主题的三组磁贴；不是每档完整 44 项 GUI。逐张核对实际 DPR 和磁贴扫描结果，仍严格检查 48／104／216×104 DIP 及 8 DIP 间隙，扫描线避开图标与文字。

| 有效缩放 | DPR | 组元数据及原图 |
| --- | --- | --- |
| 100% | 1 | [工作组示例](native-100/dark-work.json) |
| 125% | 1.25 | [工作组示例](native-125/dark-work.json) |
| 150% | 1.5 | [工作组示例](native-150/dark-work.json) |
| 175% | 1.75 | [工作组示例](native-175/dark-work.json) |
| 200% | 2 | [工作组示例](native-200/dark-work.json) |

每个子目录含 6 份 JSON 和 12 张对应 PNG，保留完整窗口与组裁切；共 30 份组元数据、60 张图片。该矩阵覆盖 Qt 有效缩放下的渲染几何，不证明系统缩放切换、混合 DPI 迁移、热插拔、Explorer 重启、远程桌面或完整材料兼容。真实托盘和独立产品重启按测试记录另行验收，不由固定样例推断。

## 真实产品与重启

[脱敏摘要](product-acceptance.json) 记录同一最终产品的两个独立进程、菜单正常退出代码 0、跨组拖拽和尺寸修改前后的严格快照比对。实际观察深浅主题、拼音搜索、编辑和命名取消；重启恢复深色、两组四枚磁贴，查询为空且非编辑。用户另外确认真实托盘恢复位置／大小稳定、当前进程查询保留，并从电源菜单正常退出。

第一个受限启动进程使用了通用的部分应用图标和最小化恢复路径；同一程序通过普通桌面启动后，真实图标和托盘恢复正常。两个环境的结果分别记录，不将备用恢复声明为真实托盘通过。完整布局、真实目标描述、目录和私人桌面背景截图没有提交。
