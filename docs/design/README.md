# 视觉设计与资产

当前基准：**v2 精确网格矢量稿**，用户已确认。Logo 采用三枚阶梯状圆角磁贴。原始图像探索仅保留为历史材料。

## 当前设计稿

| 资产 | 位置 |
| --- | --- |
| 深色界面 | [PNG](mockups/v2/xdstarter-desktop-dark-v2.png) / [SVG](mockups/v2/xdstarter-desktop-dark-v2.svg) |
| 浅色界面 | [PNG](mockups/v2/xdstarter-desktop-light-v2.png) / [SVG](mockups/v2/xdstarter-desktop-light-v2.svg) |
| 尺寸与对齐规范 | [PNG](mockups/v2/xdstarter-tile-grid-v2.png) / [SVG](mockups/v2/xdstarter-tile-grid-v2.svg) |
| Logo 展示板 | [PNG](brand/xdstarter-brand-board.png) / [SVG](brand/xdstarter-brand-board.svg) |
| 独立标志 | [SVG](brand/xdstarter-mark.svg) / [1024 透明 PNG](brand/xdstarter-mark-1024.png) |
| 应用图标 | [SVG](brand/xdstarter-app-icon.svg) / [512 PNG](brand/xdstarter-app-icon-512.png) |
| 横向字标 | [SVG](brand/xdstarter-wordmark.svg) / [PNG](brand/xdstarter-wordmark.png) |

`brand/` 还包含单色标志以及 16、24、32、48、256、512 等尺寸导出。两张桌面界面图为 1920×1080；网格规范图为 1600×1080。SVG 中的应用图标为简化矢量示意，运行时从系统应用目录提取实际图标。

## 尺寸和颜色

[tile-grid.json](tokens/tile-grid.json) 是当前几何数据来源；[视觉 tokens](tokens/visual.json) 保存品牌色和基础视觉值。

| 类型 | 实体尺寸（DIP） | 小格跨度 |
| --- | --- | --- |
| 小正方形 | 48×48 | 1×1 |
| 标准正方形 | 104×104 | 2×2 |
| 宽磁贴 | 216×104 | 4×2 |

所有间隙为 8 DIP。`48 + 8 + 48 = 104`，`104 + 8 + 104 = 216`。当前界面图按 150% 绘制，对应 72×72、156×156、324×156 像素，间隙 12 像素。深浅主题共用同一份布局。

品牌蓝 `#368BFF`、青 `#36D6CB`、深蓝 `#142138`。字标 PNG 保留本次导出字形；包含文字的 SVG 依赖字体替换，几何 Logo 无字体依赖。运行时使用系统字体并重新验收中文与缩放。

## 可复现源文件

Python 脚本仅使用标准库；重建 PNG 额外需要 `rsvg-convert`。在仓库根目录执行：

```sh
python3 docs/design/source/build_brand.py
python3 docs/design/source/build_tile_grid.py
python3 docs/design/source/build_desktop_grid.py
python3 scripts/check_repository.py
```

构建脚本输出到对应 `brand/` 和 `mockups/v2/`。文档检查不要求安装渲染器。

- [初始生成提示词](source/prompts/image-prompts.md)
- [磁贴修订探索提示词](source/prompts/image-prompts-v2.md)
- [归档说明](archive/README.md)

修改设计时先更新网格或源文件，再导出 PNG/SVG 并目视核对。不要把生成式探索图作为精确尺寸依据。设计变更关联 Issue 和 PR，版本规则见[文档规范](../standards/documentation.md)。
