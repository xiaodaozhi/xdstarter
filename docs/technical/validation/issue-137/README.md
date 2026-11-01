# 图片加载警告的原始观察

2026-11-01 的[原始测试日志](original-ui-warning.log)来自本机 Windows 11、Qt 6.8.2、附加 MSVC v142 的自有 offscreen/software 窗口。`tileDisplayNameEditingAndRestart` 使用模拟应用目录和隔离布局，QtTest 报告 3 passed、0 failed，但输出两条 `QQuickPixmap` 连接警告。日志 SHA-256 为 `110e13fbd5467eaf8a57b7d28789d408709575f8d90688b53b2e3155e49f29ee`，测试程序 SHA-256 为 `0ea761e8fe5b5e0ada57f0ca6769bab80b6786d9dadd75287506398fa6edf89d`；它不是发行候选。

单独的匿名 QML 探针可在图片进入 Loading 时同步替换来源并触发相同警告；另一受控变体最终显示替换后的图片。两者只说明一种可能的 Qt 重入机制，不能证明上述项目用例或其他场景发生相同交错，也不能证明所有警告都无害。[Issue #137](https://github.com/xiaodaozhi/xdstarter/issues/137) 持续记录实验和未解决的问题。
