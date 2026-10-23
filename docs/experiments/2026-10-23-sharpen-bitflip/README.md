# Sharpening 单节点位翻转：首次验证

结论：在原近似基准和 ROI 不变的条件下，固定一个节点和一个输出位，按图片调整区间，
可以集中增加 ROI 误差。但单节点配置还不能稳定地产生强攻击，且尚无面积综合结论。

## 设置

- 基础版本：`cleanup/project-files`，`07bc30bc1acc1d9347e756efec7422c078fcdc00`。
- 所有 4 个加法节点的正常配置为 5RP；计算单元内部未改动。
- ROI：原 `data/mask/roi_mask.jpg`，最近邻缩放，阈值 ≥128。
- 监测信号：input1、input2、翻转前 baseline_output；非符号位 0–11。
- 每次只攻击一个节点；搜索 144 种固定结构，区间为闭区间。
- 硬约束：Global PSNR ≥30 dB；目标：最小化 ROI PSNR。
- 成功判据：满足全局约束，ROI MSE 增加，且 `ΔMSE_ROI − ΔMSE_nonROI > 0`。
- PSNR 参考图为精确 Sharpening 输出。
- WSL Ubuntu 24.04，GCC 13.3，OpenCV 4.6，Release；完整测试 **26/26** 通过。

本次使用现有开发图片，不能据此声称全新图片上的泛化成功率。

## 三阶段结果（24 个边界）

| 阶段 | 图片 | 硬件选择 | 成功数 | 平均基准 ROI PSNR | 平均攻击 ROI PSNR | 最低 Global PSNR |
|---|---|---|---:|---:|---:|---:|
| Stage1 | 1–10 | 每图自由选择 | 10/10 | 34.25 | 23.42 | 30.001 |
| Stage2 自由搜索诊断 | 11–20 | 每图自由选择，非冻结结果 | 10/10 | 34.32 | 24.00 | 30.008 |
| Stage3 | 21–30 | 同一个冻结结构 | 9/10 | 35.23 | 28.76 | 30.097 |

Stage2 实际对每张图的全部 144 种结构分别选区间，然后按跨图片表现排序。
冻结结果为 **A2（node_id=1）、input2、第 8 位（零起始，2^8 位）**。
它在 Stage2 固定硬件时为 10/10 成功，平均 ROI PSNR 为 26.12 dB，
最差 ROI PSNR 为 31.83 dB；不能把 Stage2 自由搜索的 24.00 dB 当成冻结硬件表现。

## 固定同一硬件，枚举全部区间的复查

保持 Stage2 选出的 A2/input2/bit8 不变，在 Stage3 每张图上枚举全部观测值端点。
这只扩大每图的区间搜索预算，没有根据 Stage3 重新选择节点或位。

| Stage3 区间搜索 | 成功数 | 平均基准 ROI PSNR | 平均攻击 ROI PSNR | 最低 Global PSNR |
|---|---:|---:|---:|---:|
| 24 边界网格 | 9/10 | 35.23 | 28.76 | 30.097 |
| 全部端点 | 9/10 | 35.23 | 26.48 | 30.020 |

全端点复查包含失败图片，未筛除失败后再算平均值。攻击 ROI PSNR 范围为 20.41–38.14 dB。
每张图约评价 2.96–3.29 万个区间，所有最终选中区间均再次执行完整应用核对。

第 22 张图的 ROI PSNR 始终为 38.1412 dB，与基准相同；在这个固定结构、单区间及
Global≥30 的约束下，全区间枚举也未找到有效攻击。其候选区间均无下游位宽违规，
因此不能把失败解释为 24 边界网格漏搜或范围检查误删。

这些结果说明两点：区间搜索预算明显影响强度；单节点单个位仍有适应性和强度限制。
该方法也不是严格“误差守恒”的重新分配：全端点复查的平均 non-ROI MSE 增量为 +27.69，
只是维持全局质量下限并让误差更偏向 ROI，不能声称非重要区域被恢复或其误差必然减少。

## 复现

在工程根目录执行，输出目录需要尚未用于实验：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target sharpen_bitflip_validation -j4
./build/sharpen_bitflip_validation data results/sharpen_bitflip_trial 24 10
./build/sharpen_bitflip_validation --fixed data results/sharpen_bitflip_fixed_check 1 input2 8 0 21 30
```

第二条实验命令必须沿用本次明确冻结的结构；若第一条命令在不同数据上选出其他结构，
请从它的 fixed_hardware.csv 读取相应节点、监测信号和位，不能按测试集表现另选。
各阶段输出图及全量 candidates.csv 在重新运行后生成，本目录仅保留紧凑的汇总记录。

## 文件

- `budget24_summary.csv`：30 张图的主试验结果，包括失败。
- `stage2_hardware_ranking.csv`：Stage2 全部结构的跨图片排序。
- `fixed_hardware.csv`：冻结结构与主试验搜索预算。
- `fixed_all_endpoints_summary.csv`：同一固定结构在 21–30 图上的全区间复查。
- `statistics.json`：各阶段统计，均为逐图 PSNR 的算术平均，非合并图像 MSE 后再取 PSNR。
- `input_sha256.csv`：30 张输入图及 ROI 掩码的 SHA-256，供核对数据版本。
- 两份 `*_RUN.txt`：执行口径。

硬件尚未综合。单个固定翻转位的载荷可用一个异或门表达，但比较器、区间寄存器、
控制、扇出和布线仍有成本；面积和延迟收益需要与原方案在同一综合条件下比较。
