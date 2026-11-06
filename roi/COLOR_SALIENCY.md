# U²-Net ∪ RC / FT 的多比例区域统计

复用现有 `union_frequency_rectangles.py`。新增 `--feature rc` 和 `--feature ft`，
原 PC 命令默认仍为 `--feature pc`。两次运行分别比较 U²-Net∪RC、U²-Net∪FT；
没有把 RC、FT 同时加入同一个并集。

## WSL 拉取和运行

```bash
cd ~/projects/u2net_roi
git status
git fetch origin
git switch feature/spatial-feature-statistics
git pull --ff-only origin feature/spatial-feature-statistics
source .venv/bin/activate
python -m pip install -r requirements-color.txt
python -m unittest discover -s tests -p 'test_color_saliency.py' -v

for feature in rc ft; do
  python union_frequency_rectangles.py \
    --feature "$feature" \
    --data ~/datasets/PASCAL-S \
    --count 850 --grid 320 \
    --output "output/union_${feature}_850_01" || break
done
```

有本地修改导致切换/拉取失败时先保存修改，避免强制覆盖。
首次可先将 count 改成 20，使用新的输出目录做图片检查。
全量两组结果分别进入 `output/union_rc_850_01` 和 `output/union_ft_850_01`。
RC 逐图分割和比较区域直方图，计算量可能明显高于 FT，日志每25张显示进度。
运行期间保持电脑开机。脚本不覆盖已有输出目录，失败目录保留 protocol 供排查。

## 计算定义及来源

以下是按原论文公式独立编写的 Python 实现，数值约定完整记录，尚未与原作者二进制
逐像素比较。论文写作应披露本实验的分割、颜色转换和 Otsu 二值化方式。

### RC

依据 Cheng 等 CVPR2011 的区域对比公式5–7：

1. 用 scikit-image Felzenszwalb 对原尺寸 RGB 图分割，明确选择
   `scale=100, sigma=0.5, min_size=50`。这些是本实现的分割参数。
2. RGB 每通道均分12档。按颜色频数保留覆盖至少95%像素的颜色箱，
   每箱代表色为其中像素的平均 RGB，再转为 Lab。其余颜色箱映射到最近保留 Lab 代表色。
3. 每个区域构建归一化颜色直方图。区域颜色距离为各颜色对 Lab 欧氏距离的加权和，
   权重为两区域对应颜色的概率乘积，使用完整直方图而非单个区域均值色近似。
4. 区域对比度为其与其他区域的颜色距离加权和。区域大小权重为像素数/全图像素数，
   空间权重为 `exp(-centroid_euclidean_distance/0.4)`。
   质心使用像素中心坐标除以原图宽高。区域与自身的比较排除。
5. 区域分数映射回所有原像素，形成原尺寸连续响应图。

使用相对区域大小只使全部分数共同缩放，逐图 Otsu 分类保持同一尺度变换意义。
本实现保留原始连续响应，不添加中心偏置、不删除图像边界、不做 GrabCut。

原论文与作者页：
- https://cg.cs.tsinghua.edu.cn/people/~cmm/Saliency/ChengSaliencyCVPR2011S.pdf
- https://cg.cs.tsinghua.edu.cn/people/~cmm/Saliency/Index.htm

### FT

依据 Achanta 等 CVPR2009 公式8：原图 sRGB 转 CIELAB（D65），计算原图 Lab 全局均值，
对 Lab 三通道分别应用可分离5点二项式核 `[1,4,6,4,1]/16`，
每个像素的响应为平滑后 Lab 向量与原图全局均值的欧氏距离。
平滑采用反射边界，不使用周期 FFT。连续响应保持原尺寸。
这里按公式在 Lab 上平滑，明确记录这一数值约定。

原论文： https://www.cs.toronto.edu/~strider/publications/SaliencyCVPR09.pdf

## 二值化、统计与比例

RC、FT 各自的原尺寸连续响应使用现有逐图 Otsu，严格大于阈值的像素记为1。
常量响应为空候选。Otsu 为本实验统一的二值化规则，与原论文后续的 SaliencyCut、
mean-shift 分割流程有所区别。不能直接声称复现原论文的分割结果。

每张图与原有 U²-Net 二值掩膜取并集，然后按原有 stretch 像素中心规则映射到320×320。
相同坐标的分母为850张图，每张图每个位置最多贡献一次。
`added=feature AND NOT U²-Net`，记录第二路实际新增证据。

沿用全部19档百分比：10到95每5档，额外72。可加 `--thresholds 50 60 65 70 72 75 80 85 90`
更改导出列表。每档以整数计数比较，不从预览灰度反推阈值。
外接框包括全部高频点，不自动移除孤立点。空区域对应空掩膜和 null 矩形。
坐标仍是0起始半开 `[left,top,right,bottom)`。

## 输出

每个实验目录都保留 `union_overview.png`、`rectangles.csv/json`、`summary.json`、
`protocol.json`、`per_image.csv`。其他目录与上一轮相同：

- `thresholds/union/` 下有19档高频掩膜、矩形掩膜和预览。
- `thresholds/u2net/` 与 `thresholds/rc/` 或 `thresholds/ft/` 是单路对照。
- `frequencies/added_frequency.png` 展示第二路新增出现频率。
- `per_image_masks/added/` 保留逐图新增位置。`examples/` 展示前12张原图及区域叠加。
- `rc_responses/` 或 `ft_responses/` 保存原尺寸 float32 连续响应 NPZ，键名为 rc 或 ft。
- `per_image.csv` 含 `rc_otsu_threshold` 或 `ft_otsu_threshold`、面积、来源哈希。
- `protocol.json` 保存方法参数、scikit-image版本和代码哈希。

覆盖率字段仍按本次同一套联合特征计算。RC、FT 的目标掩膜不同，因此两次实验的
`mean_union_coverage_nonempty_images` 数字不宜直接解释为同一目标下的方法优劣。
可以先看各自相对 U²-Net 矩形的增益，以及新增内容和新增面积。
实际是否能形成更高频且合适的固定矩形，依靠本次实验结果判断。

## 打包回传

连续响应保留在本地。打包其他记录供分析：

```bash
tar --exclude='union_rc_850_01/rc_responses' \
    --exclude='union_ft_850_01/ft_responses' \
    -czf union_rc_ft_850_01_review.tar.gz \
    -C output union_rc_850_01 union_ft_850_01
```

先确认两个目录的 `protocol.json` 都为 completed，再回传压缩包。

## 开发验证

2026-11-05 对应发布内容的完整测试55项通过，包含5项新增 RC/FT 测试及原有PC回归。
FT 与直接实现的二项式Lab定义核对，RC 区域直方图对比与显式颜色对求和核对。
两种方法分别在6张合成图上执行完整命令行和全部19档输出，已检查叠加图和总览。
独立审查核对了原论文公式，并独立运行5项新增测试，未发现阻塞问题。
实际850张图的 RC/FT 实验由用户运行；尚未与作者原始程序比较逐像素结果。
