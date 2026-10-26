<!-- docs/technical/research/start-capture-comparison-52.md -->

# 开始按钮接管对照原型使用与证据边界

对应 [Issue #52](https://github.com/xiaodaozhi/xdstarter/issues/52)，范围决定见 [ADR-0016](../adr/0016-start-capture-experiments.md)。本页说明可重复的运行方法；当前 head 的通过结果以[测试策略](../testing.md)与 Issue/PR 为准。

## 外部 Hook 的点资格和区域缓存

先从托盘正常退出已有 XDStarter。单例 IPC 只请求唤起，不能把第二次启动携带的实验参数应用到现有进程。使用当前构建的 `bin/xdstarter.exe`，将 Qt SDK 的 `bin` 放入该子进程 PATH 后分别运行：

```powershell
./build/app-release/bin/xdstarter.exe --start-capture-mode point --start-capture-diagnostics ./build/point-capture.json
./build/app-release/bin/xdstarter.exe --start-capture-mode area-cache --start-capture-diagnostics ./build/area-capture.json
```

两条命令分两轮执行，中间正常退出。默认不创建诊断文件；显式选择后每250 ms原子覆盖同一文件并在退出时补写，不无限追加日志。可加`--start-capture-duration 120`限定实验时长并正常退出。文件父目录须已经存在。比较时保留程序 SHA-256、模式、Windows 构建、显示器/DPI、任务栏方向/对齐和每个场景的人工观察。相同场景使用同一EXE，仅切换模式；显式布局文件用于隔离验收，不覆盖用户布局。

Windows读者短暂占用目标文件可能使原子替换失败。显式导出保留250 ms后续尝试，连续失败期间只首次输出告警，恢复后的`diagnosticWriteFailures`累计失败次数；不降级为直接写入，也不延长收集器的2秒陈旧守卫。文件持续不可写时仍不能取得有效试次证据。正常退出后若收集器稍后检测到源陈旧，应与产品退出记录对照，不当作一次物理点击失败。

项目固定Qt6.8.2的[Windows原子覆盖实现](https://github.com/qt/qtbase/blob/v6.8.2/src/corelib/io/qfilesystemengine_win.cpp)使用`MoveFileExW`。本机实验中即使读句柄共享删除，保留旧句柄仍可能阻止该覆盖；没有采用仅修改读者共享标志的候选。回归直接复现普通读句柄占用，验证产品写者在释放后的恢复。

可在退出已有XDStarter、正确Qt PATH的Windows环境运行文件占用恢复回归。输出目录必须位于本仓库build内且尚不存在；脚本使用隔离布局，程序十秒后正常退出，不生成鼠标输入：

```powershell
./build/tooling/Scripts/python.exe scripts/verify_capture_diagnostics.py --executable ./build/app-release/bin/xdstarter.exe --output ./build/validation/capture-diagnostics-recovery
```

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

## 逐试次收集与报告

产品schema 2导出同一进程runId、EXE散列，以及以session/gestureId关联的输入、派发、Qt交付和host历史。仅观察到的普通全局点击也会有ID，不能因此算作Start点击。旧会话丢弃、被后续切换替代和未测量前台都保留为独立状态。环容量有限，收集器记录缺口；源runId/散列变化或超过2秒未更新时停止，不将缺口当作通过。

在对应Windows Python环境，程序启动且首份诊断生成后运行：

```powershell
python3 scripts/start_capture_acceptance.py collect --snapshot ./build/point-capture.json --output ./build/point-events.ndjson --executable ./build/app-release/bin/xdstarter.exe --mode point --seconds 120
python3 scripts/start_capture_acceptance.py clock
python3 scripts/start_capture_acceptance.py report --events ./build/point-events.ndjson --trials ./build/point-trials.json --output ./build/point-report.json
```

收集文件只新建、不覆盖，最长900秒且最多16 MiB。源失效/身份变化或达到文件上限会以2结束并保留原因；正常到期/人工停止为0。历史存在缺口时保留独立结果，但不输出显示延迟分位数。`clock`输出与原生记录同域的GetTickCount64；在每次真实操作前后记录`startAt`与`endAt`，独立观察原生菜单与XDStarter的结果。试次时间区间为左闭右开，不得重叠；一试次多次点击会标记关联不唯一。标注格式示例如下，仅为结构示例，不能当作物理通过证据：

```json
{
  "schemaVersion": 1,
  "runId": "copy-from-the-run-header",
  "mode": "point",
  "executableSha256": "copy-from-the-run-header",
  "inputSource": "physical-mouse",
  "outcomeSource": "operator",
  "trials": [
    {"trialId": "fast-entry-01", "startAt": 1000, "endAt": 2000,
     "intent": "start", "expectedAction": "show", "observed": "unknown"}
  ]
}
```

`observed`取`xdstarter-only`、`native-only`、`both`、`none`、`hidden`、`neighbor-unchanged`或`unknown`。预期`show/hide/cancel`与邻近按钮`neighbor`分开；未知结果不进入已标注Start分母，未看到回调的真实Start意图仍保留，不能从中推断Hook失活原因。`inputSource`区分`physical-mouse/posted-fixture/unknown`，`outcomeSource`区分`operator/independent-observer/fixture`。报告的p50/p95/p99只来自成功且唯一关联的原生前台测量，起点是松开回调，不是硬件事件或动画完成；同时读取失败计数，避免只看成功样本的延迟。查询直方图是进程累计值，不冒充逐试次精确分位数。

## 真实Explorer只观察探针

本轮新增的独立观察探针由`XDSTARTER_BUILD_EXPLORER_ROUTE_PROBE=ON`显式构建，产品不加载它。它不消费输入，先验证当前系统路径、Microsoft签名、完整build/UBR与散列，再观察任务栏线程的队列和窗口过程通路：

```powershell
cmake -S . -B build/app-release -DXDSTARTER_BUILD_EXPLORER_ROUTE_PROBE=ON
cmake --build build/app-release --target xdstarter_route_probe
ctest --test-dir build/app-release -R '^experimental\.explorer-route' --output-on-failure
./build/app-release/bin/xdstarter_route_probe.exe --inspect
./build/app-release/bin/xdstarter_route_probe.exe --observe --expected-build BUILD.UBR --expected-explorer-sha256 SHA256_FROM_INSPECTION --seconds 30 --output ./build/explorer-route.ndjson
```

先单独正常退出XDStarter，避免外部Hook已消费的输入使原生路由观察不完整。真实Start需无遮挡，锁屏或身份不匹配时在安装Hook前拒绝。观察期间按计划进行真实鼠标操作并单独标注时间与结果；队列和窗口过程可能记录同一事件，不能累加当点击次数。窗口过程字段缺少队列时间/坐标时为null，不补造数据。观察器不设置任务栏位置/显示器、不发送物理输入，不调用远程窗口子类或私有接口。

自测只对自有隐藏fixture投递/发送消息，确认Peek不被改变、子窗口消息原样到达、其他根窗口不被记录，以及两种Hook解除后的模块缺席；自测不附加Explorer。真实路由文件可能含临时窗口身份和任务栏位置，只留本地，公开记录仅提交脱敏结论、程序散列、系统构建和验证范围。

## 对照记录

每轮记录：场景次数、原生菜单单独出现、双菜单、无响应、重复切换、误吞邻近按钮和恢复失败。零停留快速移入、微动、连续点击、拖出取消分别执行；右键、Win+X、暂停/退出后的原生恢复单独确认。物理鼠标记录与合成消息分表保存。

单机一个任务栏配置的结果不能替代四边/双对齐、混合 DPI、多屏、自动隐藏、Explorer 重建/睡眠和其他 Windows 构建。当前原型用于决定下一步方案，尚未作出新的产品接管选型。
