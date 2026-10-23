<!-- docs/technical/research/start-capture-comparison-52.md -->

# 开始按钮接管对照原型使用与证据边界

对应 [Issue #52](https://github.com/xiaodaozhi/xdstarter/issues/52)，范围决定见 [ADR-0016](../adr/0016-start-capture-experiments.md)。本页说明可重复的运行方法；当前 head 的通过结果以[测试策略](../testing.md)与 Issue/PR 为准。

## 外部 Hook 的点资格和区域缓存

先从托盘正常退出已有 XDStarter。单例 IPC 只请求唤起，不能把第二次启动携带的实验参数应用到现有进程。使用当前构建的 `bin/xdstarter.exe`，将 Qt SDK 的 `bin` 放入该子进程 PATH 后分别运行：

```powershell
./build/app-release/bin/xdstarter.exe --start-capture-mode point --start-capture-diagnostics ./build/point-capture.json
./build/app-release/bin/xdstarter.exe --start-capture-mode area-cache --start-capture-diagnostics ./build/area-capture.json
```

两条命令分两轮执行，中间正常退出。默认不创建诊断文件；显式选择后每两秒原子覆盖同一文件并在退出时补写，不无限追加日志。文件父目录须已经存在。比较时保留程序 SHA-256、模式、Windows 构建、显示器/DPI、任务栏方向/对齐和每个场景的人工观察。

诊断中的 `hookInstalled` 是安装记录，`hookLiveness=unknown` 保留系统静默移除无法直接判断的事实。`inputObservedThisSession` 说明本会话见过回调，不能证明现在或未来健康。`qualificationLeaseAvailable`、`eligibleAtCurrentCursor`、`downDecisions`、`upCanceled`、`epochSources`、订阅 HRESULT 和 `lastQuery` 用于区分资格缺口与失败。

`consumedDown/consumedUp` 是本回调的消费决定；`toggleQueued` 是完整手势派发。host 观察另记录实际显示与原生前台结果，只有原生测量成功才增加 `hostForegroundConfirmedCount`。这些计数都不能单独证明 Windows 原生菜单从未出现，需要与真实结果对照。统计输入范围包括全部鼠标回调，不把所有放行事件都当成用户尝试点击 Start。

区域模式会提前识别指针所在屏幕的 Start，但仍有查询租约、遮挡、任务栏变化与跨屏首次查询的空窗。不得把一条成功查询当作整轮输入成功。默认点模式作为控制组，继续要求原点附近的实时资格。

## 只读原生查询

测试程序提供独立入口，在 MTA 内复用 UIA client 和查询上下文，输出八份有界样本：

```powershell
./build/app-release/xdstarter_start_button_capture_tests.exe --inspect-native-target point
./build/app-release/xdstarter_start_button_capture_tests.exe --inspect-native-target area-cache
```

它不安装 Hook，不移动指针、不发送输入、不打开菜单。点模式在指针不在 Start 时没有资格是正常现象。区域模式也必须拒绝真实遮挡；`details` 可以说明按钮矩形、期望任务栏和实际命中窗口。诊断含临时原生窗口身份，仅保留本地，公开证据应脱敏。

## 最小进程内队列桥接

普通产品构建不生成或加载该实验。在相应 x64 Developer PowerShell 中复用已经配置好的 Windows Qt 构建树，显式开启：

```powershell
cmake -S . -B build/app-release -DXDSTARTER_BUILD_START_BRIDGE_POC=ON
cmake --build build/app-release --target xdstarter_start_bridge_tests xdstarter_start_bridge_controller
ctest --test-dir build/app-release -R '^experimental\.start-bridge' --output-on-failure
./build/app-release/bin/xdstarter_start_bridge_controller.exe
```

控制器创建本实验自己的 Win32 fixture，系统通过指定线程的 Hook 将无 Qt DLL 加载到该 fixture。Qt 界面和 IPC server 留在控制器进程。界面可发送受控鼠标消息、诊断原生指针消息投递、暂停接管/心跳及重建窗口，观察原生放行与完整鼠标手势派发。它不会查找或接管用户应用，不注入 Explorer。

自动集成检查覆盖真实 DLL 加载、PM_REMOVE 消息处理、跨进程 named pipe、心跳、身份/nonce、成对释放、重建和安全停止。兼容鼠标与原始指针属于不同输入通路，必须验证匹配窗口/代次，避免二次切换或误吞其他窗口。未排空持有的按下时，停止超时属于未完成卸载；正常关闭必须先释放，异常进程退出另列恢复边界。

本机原生指针投递探针为两次尝试、零次成功，返回 `ERROR_INVALID_MESSAGE`（1002），Hook 观察到零份 `WM_POINTER`。这属于系统拒绝投递，不能写成成功消费。B 本轮对真实 `WM_POINTER` 全部放行；指针/兼容消息的状态回归通过独立命名的仿真帧进入同一规范化逻辑，测试会话显式开启 `enableEmulatedFrames`，普通控制器默认关闭。即便仿真通过，也没有获得真实触控支持。

该实验没有现代 Explorer InputSite 适配器，目标白名单会拒绝 Explorer。它也没有证明真实任务栏输入都经过 `GetMessage`。自有消息投递通过，只能证明队列桥接与 IPC 机制，不构成 Explorer 接管、硬件触控或 Win 键路由通过。

## 对照记录

每轮记录：场景次数、原生菜单单独出现、双菜单、无响应、重复切换、误吞邻近按钮和恢复失败。零停留快速移入、微动、连续点击、拖出取消分别执行；右键、Win+X、暂停/退出后的原生恢复单独确认。物理鼠标记录与合成消息分表保存。

单机一个任务栏配置的结果不能替代四边/双对齐、混合 DPI、多屏、自动隐藏、Explorer 重建/睡眠和其他 Windows 构建。当前原型用于决定下一步方案，尚未作出新的产品接管选型。
