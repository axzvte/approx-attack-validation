# U²-Net 与 PC 并集的多比例矩形统计

每张图读取已有 U²-Net 二值掩膜，提取原图 PC 响应，用逐图 Otsu 阈值保留高响应
结构候选，然后逐图取并集。所有掩膜直接映射到 320×320，统计 850 张图的出现频率。
输出每个比例的高频区域掩膜、外接矩形掩膜、预览和坐标，最终比例由用户查看后决定。

## 拉取与运行（WSL）

在现有 u2net_roi 项目目录执行。先确认 `git status`，有本地修改时先保存，避免强制切换。

```bash
git fetch origin
git switch feature/spatial-feature-statistics
git pull --ff-only origin feature/spatial-feature-statistics
source .venv/bin/activate
python -m pip install -r requirements-pc.txt
python -m unittest discover -s tests -p 'test_union_frequency_rectangles.py' -v
python union_frequency_rectangles.py \
  --data ~/datasets/PASCAL-S \
  --count 850 --grid 320 \
  --output output/union_frequency_850_01
```

默认输入为 `--data` 下 `datasets/imgs/pascal/<id>.jpg` 和 `pred_masks/<id>_mask.png`，
编号 1–850。可用 `--start`、`--count` 先进行小规模路径和图片检查，例如 count=20。
`--image-dir`、`--mask-dir` 接受绝对路径或相对 --data 的路径。
输出目录必须为新目录，失败后保留 protocol 供排查，下次换输出目录名。

有已有 PC 缓存时，可在上述运行命令中加上：

```bash
--pc-dir output/pc_train_600/pc_maps
```

缓存为原尺寸 16 位单通道 `<id>_pc16.png`，值/65535 为 PC。找到的缓存只读复用，
缺失 ID 在当前新输出中计算，路径、SHA-256 和来源逐图记录。缓存参数版本需由来源
实验核对，文件本身无法证明历史提取参数；首次统一参数实验可以省略 --pc-dir。
不接受 8 位预览图当缓存。所有实际使用的 PC 响应另存为无损压缩的 float NPZ。
复用 16 位缓存时，保存的响应包含该缓存既有量化精度。

## 两类阈值

PC 阈值在每张原图内部计算，使用已有 `consensus_region_statistics.otsu_mask`。
高于其阈值的像素作为 PC 候选，常量 PC 图产生空 PC 掩膜及空阈值。
Otsu 只依据 PC 值的分布，不读取 GT 或眼动。PC 测量结构，联合区域定义为显著目标
与结构候选的并集，语义合理性需要结合原图观察。

频率阈值在 850 张图累计之后应用。默认百分比为：

`10, 15, 20, 25, 30, 35, 40, 45, 50, 55, 60, 65, 70, 72, 75, 80, 85, 90, 95`。

可用 `--thresholds 50 60 65 70 72 75 80 85 90` 自定义整数百分比。
阈值使用原始整数计数比较，最低图数为 `ceil(percent*N/100)`，不先把频率图转成
8 位或四舍五入。72%、850 张对应至少 612 张。

## 几何与方法

- 原图与原掩膜尺寸必须一致。沿用 `spatial_feature_statistics.sample_mask` 的 stretch
  规则，按画布像素中心逆映射取源像素。每个坐标的分母都为实际处理图片数。
- 同一张图同一位置只计一次。`union_count = u2net_count + added_count`，其中 added
  是 `PC AND NOT U2-Net`。added 的频率是第二路特征实际补充的图片比例。
- 掩膜输入严格使用已有 PNG 0/1 或 0/255 二值结果。没有重新运行 U²-Net。
- PC 在原尺寸灰度图上计算，参数复用 `pc_extract.PC_PARAMS`，二值化后再直接映射。
- 输出的所有矩形包含相应阈值下的全部高频像素，不删除零散点。记录 8 连通分量数量
  和“高频像素数/外接框像素数”，帮助识别孤立点将矩形拉大的情况。
- 统计坐标为 0 起始半开 `[left,top,right,bottom)`，宽为 right-left，高为 bottom-top。
  若用于闭区间像素坐标，应将 right/bottom 各减 1。
- 无高频像素时输出全黑的两张掩膜、带 empty 标记的预览。JSON 矩形和面积为 null，
  CSV 对应字段留空，不将空结果报告为有效的 0×0 矩形。
- 预览固定 0–1 色标，频率原值另存 NPY。细小特征可能受采样分辨率影响。

## 看哪些文件

| 文件/目录 | 内容 |
| --- | --- |
| `union_overview.png` | 19 个比例的并集矩形预览总览，比例和宽高在每格标注 |
| `thresholds/union/72_region_mask.png` | 72% 的高频区域，其他比例同样命名 |
| `thresholds/union/72_rectangle_mask.png` | 对应外接矩形掩膜 |
| `thresholds/union/72_preview.png` | 频率图上的矩形预览 |
| `thresholds/u2net/`、`thresholds/pc/` | 相同比例下的两路单独结果，便于对照 |
| `rectangles.csv` / `.json` | 所有方法与比例的坐标、面积、连通数、填充率与覆盖数据 |
| `frequencies/` | U²-Net、PC、并集、added 的计数/频率 NPY 和 PNG |
| `per_image_masks/` | 映射后的逐图二值掩膜，包含 added |
| `examples/` | 前 12 张图的原图和三路区域叠加，可用 --preview-count 调整 |
| `pc_responses/` | 每张原图 PC 的 float 响应 NPZ，读取键 `pc` |
| `per_image.csv` | 原尺寸面积、采样像素数、Otsu 阈值、输入和 PC 响应哈希 |
| `summary.json`、`protocol.json` | 完成状态、配置、代码哈希、频率峰值 |

覆盖字段都以同一套“逐图并集掩膜”为目标，便于比较不同来源的矩形。
`mean_union_coverage_nonempty_images` 为每张非空并集图的覆盖率等权平均，
`pooled_union_coverage` 将所有采样前景像素合并计算。两者均使用 320×320 采样掩膜，
空目标和空矩形的覆盖指标留空。结果属于 850 图内描述统计，用户事后选比例的过程需在
论文中披露。达到某个出现频率不等于矩形完整覆盖相同比例的图片。

## 打包回传

PC 原始响应可能占较大空间。先打包统计、掩膜、预览及来源记录供判断：

```bash
tar --exclude='union_frequency_850_01/pc_responses' \
  -czf union_frequency_850_01_review.tar.gz \
  -C output union_frequency_850_01
```

原始 PC 响应保留在本地。先看 `union_overview.png`，然后查看感兴趣比例的两种掩膜、
`rectangles.csv` 和 `examples/`。确认 `protocol.json` 的 status 为 completed。

## 开发验证记录

2026-11-05 在对应发布内容的副本中运行完整测试，共 50 项通过（含本次 8 项）。
覆盖真实 PC 计算、16 位缓存、常量 PC、并集计数、72% 的整数边界、空矩形、输出保护。
另外对 6 张合成图跑完全部 19 档导出及命令行流程，并检查总览和叠加预览。
实际 850 张图的实验由用户在终端执行。
独立只读审查未发现实质性问题；根据审查建议增加了覆盖率具体值和输入缺失/尺寸不符的回归测试。
