# Unihan 拼音字音数据

本目录是 Unicode Unihan **17.0.0** 的离线派生子集，用于本地应用名称的拼音全拼与首字母匹配。它不引入拼音运行库，也不在构建、索引或查询时下载数据。XDStarter 自身代码和原创资产的许可仍按项目治理决定，本目录的第三方许可不授予整个项目新的许可证。

## 来源与许可

- 官方固定版本：[Unihan.zip](https://www.unicode.org/Public/17.0.0/ucd/Unihan.zip)，8,518,517 字节。
- [版本声明](https://www.unicode.org/Public/17.0.0/ucd/ReadMe.txt)及对应 [UAX #38 修订 39](https://www.unicode.org/reports/tr38/tr38-39.html)。本轮固定版本，不以未固定的最新数据替代。
- 源文件通知：`© 2025 Unicode®, Inc.`；Unicode 和 Unicode Logo 是 Unicode, Inc. 的注册商标。
- [官方使用条款](https://www.unicode.org/copyright.html)第 3.3 节规定数据文件采用 Unicode License v3，除非具体发布位置另有说明；选定字音属性未附加其他许可。
- 完整许可原文在 [LICENSE.txt](LICENSE.txt)，2026-10-21 从[官方许可地址](https://www.unicode.org/license.txt)取得。派生文件再分发时保留许可及源版权通知；许可允许将通知放在随附文档。应用未来部署时必须同时带上该通知，不能只复制表而省略许可。

[manifest.json](manifest.json)记录精确来源、源压缩包与选定文本 SHA256、许可文本哈希、转换方式、输出哈希和统计。哈希是从官方 HTTPS 下载后本地计算的固定值，不是上游签名或独立认证。原始压缩包不入库。

## 提取与边界

按以下固定顺序合并同一 Unicode 标量的字音，去重后保留顺序：

1. `kMandarin`：惯用读音；两个值可能反映简体中国与繁体台湾的优先读音差异。
2. `kTGHZ2013`：通用规范汉字字典读音。
3. `kXHC1983`：现代汉语词典读音。
4. `kHanyuPinyin`：汉语大字典读音。

后面三个属性丢弃字典位置，提取冒号后的拼音列表；输入按 NFC 规范化，先把 `ü/ǖ/ǘ/ǚ/ǜ` 转为 ASCII `v`，再以 NFD 去声调，避免把 `ü` 误折叠为 `u`。原始字音均保留为小写 ASCII，显示名称仍使用原文字形。

四字段并集包含 **44,359 个码点、52,982 个字音**；6,964 个码点有多个无声调读音，单字最多 8 音，单音最长 6 个 ASCII 字母。`乐` 包含 `le/yue`，`长` 包含 `zhang/chang`，`重` 包含 `zhong/chong/tong`，`行` 包含 `xing/hang/heng`。这比只使用 `kMandarin` 与 `kHanyuPinyin` 多覆盖简体常见多音字。

这是逐字候选读音，**没有词级断词、上下文消歧或读音频率排序**；例如 `重庆` 也可能被 `zhongqing` 命中，罕见和历史读音可能增加误匹配。本轮没有纳入其他 Unihan 字音属性、简繁变体传播、品牌词典、任意文件搜索或网络搜索，不承诺所有汉字和读音完整覆盖。未知码点由调用方保留原文匹配，不能把空字音当作删除原字符的依据。

## 文件与离线重建

- [unihan_pinyin.tsv](unihan_pinyin.tsv)：规范提取结果，码点升序；每行 `大写十六进制码点<TAB>逗号分隔读音<LF>`。
- [unihan_pinyin.inc](unihan_pinyin.inc)：同一结果生成的 C++ 初始化记录，每条记录单独使用短字符串，避免 MSVC 拼接字符串长度限制。
- [生成器](../../scripts/generate_pinyin_data.py)只读本地输入，无网络代码；[检查器](../../scripts/check_pinyin_data.py)核对许可、哈希、格式、生成结果与常见多音字。

普通构建使用已入库表，不需要 Python 或原始压缩包。开发检查：

```powershell
python3 scripts/check_pinyin_data.py
python3 scripts/generate_pinyin_data.py --check
```

从官方取得原始固定版本压缩包，保存在忽略的 `build/Unihan-17.0.0.zip` 后，可离线从上游输入复现，生成器先校验固定 SHA256：

```powershell
python3 scripts/generate_pinyin_data.py --source-zip build/Unihan-17.0.0.zip --check
```

不带 `--check` 时写回同一规范 TSV 与 C++ 表；默认只从已入库 TSV 重建 C++ 表。升级数据必须同时审查来源版本、许可、转换与兼容性，更新生成器中的固定版本及源哈希、manifest 和生成文件，再运行检查，不在普通构建中自动升级。
