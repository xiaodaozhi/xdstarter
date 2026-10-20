# 测试目录

当前只有仓库工具的无网络单元测试：验证 GitHub 初始化的幂等性、已有 Issue 的状态/正文保留，以及跨仓库写入保护。

```sh
python3 -m unittest discover -s tests/tooling -v
```

Qt 核心、QML 和 Windows 集成测试随应用工程任务建立，范围见[测试策略](../docs/technical/testing.md)。
