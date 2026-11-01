# 高对比度历史截图复核

2026-10-29 曾将一张暗色截图判断为局部绘制不完整，并在后续测试中认为暗色画面完整。重新查找当时的工具调用及其返回图像，发现首次查看的是 [v2 暗色图](original-v2.png)；2026-10-29 14:07:30 UTC 的工具响应中保留的 PNG 字节与此文件完全一致，SHA-256 为 `f26a1fbf60e6667d11210d42f3c843b72ebe9a2f68ab58b2e70ac24cdc4fc42f`。2026-10-29 14:11:39 UTC 查看了 [v4 暗色图](later-v4.png)，工具响应中的首次与后续抓帧字节相同，SHA-256 均为 `eb7c0ac1c8586b3454ed6e6c1cfb550c14310e7740dea1df42205c58457b28b7`。两张图来自自有测试窗口与模拟数据，不是设计验收基准。

两图尺寸均为 1028×728；以 Windows `System.Drawing.Bitmap` 的 `GetPixel` 逐像素比较，只有 18 个像素不同，范围为 `(152,59)` 至 `(152,76)`，对应搜索框光标，v2 为 `#FFFF00`，v4 为 `#101020`。未发现此前所称的额外内容缺失；原判断缺乏证据，应予更正。图像对照不证明其他控件或物理屏幕的完整性，也不撤销另一受控实验中图标尚未就绪时已经发生帧交换的观察。

在 Windows PowerShell 中可从本目录复核文件哈希和像素差异：

```powershell
Get-FileHash original-v2.png, later-v4.png -Algorithm SHA256
Add-Type -AssemblyName System.Drawing
$first = [System.Drawing.Bitmap]::FromFile((Resolve-Path original-v2.png))
$later = [System.Drawing.Bitmap]::FromFile((Resolve-Path later-v4.png))
try {
    if ($first.Size -ne $later.Size) { throw 'Image sizes differ.' }
    $differences = foreach ($y in 0..($first.Height - 1)) {
        foreach ($x in 0..($first.Width - 1)) {
            if ($first.GetPixel($x, $y).ToArgb() -ne $later.GetPixel($x, $y).ToArgb()) {
                [pscustomobject]@{ X = $x; Y = $y }
            }
        }
    }
    $differences | Measure-Object X, Y -Minimum -Maximum
} finally {
    $first.Dispose()
    $later.Dispose()
}
```
