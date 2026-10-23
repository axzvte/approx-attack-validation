# Sharpening 单节点位翻转验证

在原工程基础上试验：**固定一个节点、一个监测信号、一个非符号位，按图片调整触发区间**。
默认近似基准仍为全部 5RP；重要区域仍读取 `data/mask/roi_mask.jpg`，最近邻缩放，阈值 128。
计算单元内部不变。这里只验证图像效果，尚未综合，不能据此声称节省了多少面积。

## 运行（WSL，工程根目录）

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target sharpen_bitflip_validation -j4
./build/sharpen_bitflip_validation data results/sharpen_bitflip_trial 24 10
```

输出目录必须是新的。最后两个参数分别是区间边界预算、每阶段图片数。
如需快速检查，可改为 `24 1`，会使用第 1、11、21 张图片。
预算 24 最多评价 300 个区间/结构/图片；0 则枚举全部观测值边界，计算量较大。
非零预算采用不同监测值上的均匀索引网格，不等同于原多节点搜索的自适应边界算法。

已有固定结构时，可以直接复查，无需重新进行 Stage1/2。例如下列命令固定
`node_id=1 (A2), input2, bit=8`，在 21–30 图上枚举全部区间：

```bash
./build/sharpen_bitflip_validation --fixed data results/sharpen_bitflip_fixed_check 1 input2 8 0 21 30
```

该入口按提供的结构执行，不根据这些图片重新选择硬件。输出 summary、fixed_hardware、RUN 和对照图。

## 流程

1. Stage1，图片 1–10：搜索 4 节点 × 3 监测信号 × 12 非符号位，共 144 种结构，
   每种结构单独选择区间；输出每张图片的自由搜索最佳结果。
2. Stage2，图片 11–20：对全部 144 种结构分别按图片调区间。
   由于结构很少，本次直接全部筛选，Stage1 不删减候选。
   按成功图片数、可行图片数、最差 ROI PSNR、平均 ROI PSNR、平均 Advantage 排序，冻结第一名。
3. Stage3，图片 21–30：只搜索冻结结构的区间，节点、监测信号和位位置均不可改变。

区间选择的硬约束为全局 PSNR ≥30 dB；目标是 ROI PSNR 越低越好，平手时全局质量越高越好。
“成功”另要求 ROI MSE 增大，且 `ΔMSE_ROI − ΔMSE_nonROI > 0`。
可行但攻击无效的结果也保留，不能把可行率当成攻击成功率。

## 执行与评分

命中闭区间 `[lower, upper]` 时，原节点输出的固定第 k 位异或 1；未命中时完全旁路。
输出为有符号 13 位，k 只允许 0–11。翻转可能增加或减小数值。
监测输出时使用翻转前的原单元输出，避免组合反馈。
bit-flip 配置里的 `unit` 字段不选择第二个计算单元，保留它只是兼容原配置结构。

对于一个被攻击节点，每个输出像素只有“触发”和“不触发”两种结果。
评估器先模拟翻转后剩余节点的真实近似运算和最终 0–255 截断，再按监测值聚合最终平方误差。
因此区间前缀和能准确计算这个**单节点 Sharpening**的最终 MSE，包含图像边框和原误差。
其准确性不适用于多节点或 DCT，不能直接照搬。

高位翻转可能使后级输入超出当前适配器的有符号 12 位范围。
只要一个区间会触发此类样本，该区间就不合法，记录在 `range_rejected_intervals`。
没有合法且满足全局质量的区间时，`has_interval=0`，攻击指标留空，不能拿基准代替攻击结果。
每个 Stage2 候选选出的区间，以及 Stage1/3 报告的区间，都会再执行完整应用核对。

## 输出

- `summary.csv`：每张图的基准与攻击指标。Stage1/2 行为自由搜索诊断；Stage3 才是固定硬件验证。
- `candidates.csv`：Stage1/2 全部候选，Stage3 唯一固定结构；含失败、触发比例和不合法区间数。
- `hardware_ranking.csv`：Stage2 的 144 种结构排序。平均值仅针对可行图片，失败数量单列。
- `fixed_hardware.csv`：冻结的节点、监测信号、位位置及实验参数。
- `stage*_image_*_{exact,baseline,attack,roi}.png`：Stage1/3 图像，便于对照。
- `RUN.txt`：评价口径与搜索设置。

PSNR 参考图为精确 Sharpening 输出。现有 30 张图片已参与过研发，本次只属于可行性试验，
不能作为全新数据泛化证据。软件配置中的位位置可搜索；最终硬件必须固定该位，不能遗漏
比较器、区间寄存器、控制及布线的面积/延迟成本。

原有近似单元切换程序仍使用默认 payload。本次只开放 Sharpening 的 bit-flip 执行路径，
Conv/Sobel/DCT 会拒绝 bit-flip 配置，避免旧评分路径误用新载荷。
