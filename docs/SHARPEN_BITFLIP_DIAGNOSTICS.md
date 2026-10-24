# 基准中间数据与位反转响应诊断

本工具先运行原基准电路，保存中间值，再离线逐节点、逐位探测放大/补偿能力。当前范围是 Sharpening、全部 5RP、原共享 ROI；没有选择攻击区间或组合 A/B。

## 运行

在工程根目录的 WSL 环境中：

```bash
cmake -S . -B build-bitflip -DCMAKE_BUILD_TYPE=Release
cmake --build build-bitflip --target sharpen_bitflip_diagnostics -j4

# 默认只处理第 1 张图片，输出目录必须不存在
./build-bitflip/sharpen_bitflip_diagnostics data results/bitflip/baseline_diagnostics_01

# 显式处理第 1–10 张，每张单独统计
./build-bitflip/sharpen_bitflip_diagnostics data results/bitflip/baseline_diagnostics_01_10 1 10

# 可选：增加每个像素、每个节点、每个位的完整反转明细
./build-bitflip/sharpen_bitflip_diagnostics data results/bitflip/baseline_diagnostics_details 1 --responses
```

图片沿用 `stage1/input/image_01.jpg` 至 `stage3/input/image_30.jpg`。ROI 读取 `data/mask/roi_mask.jpg`，使用原最近邻缩放和 ≥128 判定。行列坐标从 0 开始，A1–A4 的 node_id 为 0–3。

完整中间数据默认导出，真实图片会有较大 CSV。四个节点、十二个位意味着每个内部像素有 48 个独立探针，`--responses` 会明显增加文件体积。可以先运行一张图片。

## 输出与误差定义

| 文件 | 用途 |
|---|---|
| `baseline_samples.csv` | 每个内部像素的四节点输入、原输出、区域、局部/累计误差和最终参考 |
| `bit_summary.csv` | 每图、节点、位、区域的响应统计；包括无样本区域 |
| `monitor_groups.csv` | 在上面维度上，再按 input1/input2/baseline_output 的原始整数值分组 |
| `image_summary.csv` | 基准全图/区域 MSE、PSNR、像素数和平方误差总和 |
| `bit_responses.csv` | 可选，逐像素逐节点逐位的原始响应；与 baseline_samples 按 image/row/col/node_id 关联 |
| `RUN.txt` | 基准、输入路径、ROI、范围、位宽、统计口径和完成状态 |

节点局部误差 = 原输出 −（该节点实际输入1 + 输入2）。

节点累计误差 = 原输出 − 同位置全精确路径的该节点输出。全精确路径独立执行，上游输入可能不同。

最终误差 = 最终 0–255 截断输出 − 全精确应用的最终输出。正负方向均采用“被评价输出减精确参考”。

`*_delta_sse` 是反转后平方误差减反转前平方误差的**总和**，正值表示增大，负值表示减少。`final_increase_sse` 只加总正增量；`final_reduction_sse` 为负增量绝对值之和。这两列可揭示正负抵消，且其差等于 `final_delta_sse`。

`local_increase_sse` / `local_reduction_sse` 同样分别记录节点局部误差增加/减少的总量，包含下游越界的探针。`final_valid_rate = valid_final_samples / samples`，用于检查有效性覆盖，空区域留空。

`bit_zero` / `bit_one` 表示原补码第 k 位的状态，翻转分别产生 +2^k / −2^k。输出实际为有符号 13 位，只探测非符号位 0–11。

每个探针从原基准重新出发，仅改变一个节点的一个位，后级使用原近似/精确配置重放，包含最终截断。后级近似单元输入若超出有符号 12 位范围，记为 `invalid_downstream`；不夹紧、不当成零变化。该样本的局部和累计节点数据仍然有效，最终明细字段留空。

## 分母与边界

- `samples = valid_final_samples + invalid_downstream`。
- 局部/累计节点统计及 `mean_local_delta` 使用全部 `samples`。
- `baseline_final_sse_valid`、`final_delta_sse` 只累计有效探针；`mean_final_delta_valid`、最终增减比例以 `valid_final_samples` 为分母，不能误解为整个区域的 MSE 增量。
- 区域均值/比例分母为零时留空；总数和总和仍为 0。某区域没有观测值时，monitor_groups 没有该组行。
- 图像边框直接复制，不执行加法节点；节点表与响应表仅含内部像素，image_summary 的区域像素数及基准指标包含边框。
- 监测值来自**反转前原基准**。group 中的 `samples` 是该区域中该值的样本数；须结合对应 summary 总数计算触发覆盖率，不能只比较两类区域的原始计数。

工具不根据 PSNR 或误差方向删除样本。所有探针用于诊断，不代表能够同时触发的合法攻击。一个区间只要包含任何越界样本，未来搜索就必须拒绝该区间。

运行开始写 `completed=0`，所有报表成功关闭后追加 `completed=1`。只有最终标记为 1 且程序返回 0 才算完整运行；失败目录保留供检查，重跑请用新目录。

## 如何用于下一步

先比较每个节点/位在 ROI 和背景中的最终增减比例、平均变化及增减总量，再查看监测值分组，识别有利样本是否集中在可选区间内。本次仅诊断基准状态；候选 B 必须在 A 已经施加后重新计算输入、位状态、触发值与补偿效果，才能评价双节点协同。

现有 30 张图是研发数据。本工具不提供新图片泛化结论，也没有硬件综合结论。

2026-10-24 新增基于上述报表的闭区间筛选，比较 off/full/ROI 放大/背景补偿；见 [触发区间对照](SHARPEN_BITFLIP_INTERVALS.md)。新增版本由用户运行，未由编写方执行。

## 验证

```bash
cmake --build build-bitflip -j4
ctest --test-dir build-bitflip --output-on-failure
```

新增 C++ 测试逐位对照完整应用的真实执行，覆盖局部/累计误差、负值、混合精确/近似后级、饱和截断和越界。Python CLI 测试从完整响应重算区域与监测值分组结果，检查空区域、输出保护、写入失败及完成状态。Python 3 只用于该报表测试，运行诊断命令不需要 Python。
