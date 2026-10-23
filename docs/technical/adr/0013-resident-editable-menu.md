# ADR-0013：单例常驻与默认可编辑菜单

- 状态：已采纳；验证结果按版本记录于测试策略
- 跟踪：[Issue #41](https://github.com/xiaodaozhi/xdstarter/issues/41)
- 替代：ADR-0004 的目录平铺呈现、早期编辑模式开关，以及 ADR-0007 的固定左下定位政策；其他安全与数据约束保留

## 决策

正常启动先取得同一用户／Windows 会话的 `QLockFile` 锁，再建立带 UserAccessOption 的 `QLocalServer` 本机通道。二次启动只发送固定 activate 指令，握手确认后退出；不传入布局路径或任意命令。Windows 使用真实会话 id，不依赖可变的 SESSIONNAME 环境变量。锁的年龄不使存活进程失效；无法取得锁、创建通道或完成有界握手时禁止创建第二实例。受用户明确启动的第二进程可把前台激活权限交给锁文件指向的原实例，平台边界调用 AllowSetForegroundWindow，随后由原 host 唤起。Qt Network 仅用于本机 IPC，不新增远程监听。

首次启动只注册托盘，注册失败则显示可恢复窗口。后台、二次唤起、Win 键、托盘与退出继续由同一个 DesktopWindowHost 管理。主菜单关闭动画采用有界计时，重复唤起取消旧计时；关闭、进程退出和失效回调不得重新显示窗口。

布局默认可编辑，普通单击仍启动；超过拖拽阈值才进入拖放。碰撞采用完整副本规划和 10×64 占位表；目标磁贴优先，其他磁贴按旧行列顺序保留原位或向后挤开，必要时复用前方空位。预览与提交同源；空间不足原子拒绝。持久化仍为 schema3，不引入像素、动画或目录展开字段。

目录模型保留有界枚举结果及 parentFolderId，根目录与当前文件夹通过代理过滤，所有应用视图不显示其他层级。文件夹不是启动目标，不可写入布局或固定。离线搜索继续覆盖子目录的合法应用目标。分类使用本地既有拼音表，归入 #、A–Z 或末尾地球图标；无实时网络或新增拼音依赖。

所有可见字体使用实际可用的系统 Microsoft YaHei UI／微软雅黑，控件与 Text 共用解析后的应用字体；字体不随产品分发。Qt offscreen 漏扫 TTC 时仅注册系统已有的微软雅黑集合，设计 SVG 使用单一明确字体族，并以 QFontInfo／QRawFont 验证真实字形，防止缺失别名回退成 Agency FB。焦点装饰由 Tab 输入方式门控，鼠标、触摸、滚轮、Esc 和应用停用清除装饰，实际焦点不被随意清除。tooltip 延迟 600 ms。禁用菜单项保留辅助功能解释，不展示多余 hint。

窗口按实际任务栏边缘与工作区定位，水平居中偏好从 Explorer 的只读 TaskbarAl 值获得，缺少值时回退左对齐；该注册表偏好并非公开稳定 API，未知取值不作为点击命中依据。QML 用 Qt startSystemResize，按停靠方向暴露远离任务栏的边缘，通用布局和 QML 不包含 Win32。

## 历史：Issue #41 的开始按钮排查结论

后续点击能力由 [ADR-0014](0014-start-click-and-adaptive-taskbar.md) 定义，以下保留只读排查时的原始范围。

当前产品从未实现开始按钮点击接管：ADR-0009 明确只读研究，现有输入模块只接管用户显式开启的独按 Win 键。2026-10-23 本机 Windows 10.0.26300／Qt6.8.2 只读探针得到 `uiaIdentified=true`、`nativeHandleRead=true`、`classification=no-native-window`，不是独立原生窗口候选。沙箱探针曾返回 `visible-explorer-taskbar-unavailable`，正常权限复测后才取得上述真实结果，两者不混用。

因此本任务完成原因核查，继续保留原生点击；没有把 UIA 矩形、TTL 或共享宿主冒充可安全吞掉鼠标手势的身份。后续接管仍由 #6／ADR-0009 的有界命中、完整 down/up、右键和暂停恢复门槛约束，不能通过“先显示再关闭原生 Start”的方式掩盖闪现。

## 验证

按 xdzip 的测试分层方式，优先领域不变量、服务 fixture、进程级 IPC 和离屏 QtTest；无必要不使用 computer use。系统按钮只运行显式只读探针。动画截帧和设计导出可直接检查本地图片，不需要操纵用户桌面。Qt 强制缩放和模拟任务栏矩形不替代实际系统任务栏变更、物理 resize、Explorer 重启或其他 Windows 版本兼容性验收。

参考：[QLocalServer 访问控制](https://doc.qt.io/qt-6.8/qlocalserver.html#SocketOption-enum)、[QWindow 系统缩放](https://doc.qt.io/qt-6.8/qwindow.html#startSystemResize)、[ToolTip 延迟](https://doc.qt.io/qt-6.8/qml-qtquick-controls-tooltip.html#delay-prop)。
