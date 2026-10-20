# 技术架构

状态：Qt Quick 路线已确认，以下为待实现架构。当前没有应用运行时工程。

## 模块与依赖方向

| 模块 | 职责 | 边界 |
| --- | --- | --- |
| app | 生命周期、单实例、依赖组装、菜单控制器 | 组装服务；不承载布局算法 |
| ui | QML 页面、控件、主题与输入反馈 | 绑定 C++ 模型；不直接操作注册表或 Shell |
| core | 网格、磁贴/分组、搜索匹配、配置校验 | 不包含 Win32；可在 macOS/Windows 单测 |
| services | 应用目录、搜索索引、图标缓存、配置存储 | 通过接口调用平台模块 |
| platform/windows | Win32/COM、窗口材料、托盘、输入接管、应用启动 | 专用线程与资源清理；向上暴露稳定事件/结果 |
| platform/stub | 通用 UI 开发的假目录、普通窗口唤起 | 明确标注模拟功能，不能代表 Windows 兼容 |

依赖方向为 `ui → app/models → services/core → platform interface`，Windows 实现由 app 注入。布局与搜索无需运行 Explorer；平台模块不依赖 QML 页面类型。

未来代码建议目录为 `src/app/`、`src/core/`、`src/services/`、`src/platform/windows/`、`qml/`、`tests/`。在工程初始化 Issue 中建立实际 CMake targets 与对应目录，避免空工程造成可运行误解。

## 数据流

1. 启动：读取偏好与布局 → 预创建菜单窗口 → 后台加载应用目录与图标 → 更新模型。
2. 唤起：平台事件 → MenuController → 选择显示器/位置 → 显示并聚焦搜索。
3. 搜索：查询 → SearchService → 稳定排序结果模型 → QML 列表。
4. 编辑：指针/键盘操作 → LayoutEngine 生成预览 → 提交新布局 → 保存 → 更新模型。
5. 启动目标：UI 发出 targetId → LaunchService 校验目标 → 平台启动 → 成功收起、失败保留菜单。

## 核心接口方向

- `ApplicationCatalog`：异步目录快照与增量变化；返回稳定 appId、名称、目标和图标键。
- `LayoutEngine`：以格坐标计算矩形、校验占用、生成落点；不读取屏幕物理坐标。
- `SearchService`：输入查询和目录版本，输出稳定 appId 列表；旧查询结果不覆盖新查询。
- `LayoutStore`：读取、校验、原子保存与备份恢复。
- `PlatformIntegration`：显示器、材料、唤起、暂停接管与资源释放。

具体 C++ 方法签名随各模块 Issue 落地；对持久化格式、错误语义或跨模块职责的变更应更新本设计。

## 线程与性能

GUI 线程更新 QML/QAbstractItemModel；扫描、图标读取、索引与磁盘写入在后台执行。使用 queued signal 或受控任务返回结果，禁止后台直接修改场景图对象。

菜单保持常驻，隐藏时停止不必要的动画与轮询。按需加载图标并限制缓存，避免每次唤起重新扫描。启动延迟、CPU、内存与 GPU 使用需要通过[测试策略](testing.md)测量。
