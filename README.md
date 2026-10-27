# Approximate Attack Validation

逐图 U²-Net ROI 第一阶段已接通十图固定硬件训练与单图区间搜索，详见 [VS Code/WSL 准备、校验和运行命令](docs/U2NET_ROI_STAGE1.md)。新十图流程默认逐图 ROI，U²-Net 仅离线运行。

近似计算攻击验证工程，包含 Conv、Sharpening、DCT、Sobel、区间搜索和固定硬件配置验证，以及 Sharpening 位反转分析与多节点联合搜索。

## 目录

| 目录 | 内容 |
| --- | --- |
| `app/` | 分析和实验程序入口 |
| `src/`、`include/` | 当前算法实现与接口 |
| `tests/` | 当前 CMake 注册的测试 |
| `third_party/evoapproxlib/` | 计算单元源码，运行必需 |
| `data/` | 已上传的原始图片与 ROI；保持原路径和内容 |
| `scripts/` | 实验运行脚本 |
| `hardware/` | 硬件面积结果 |
| `archive/` | 完整保留的历史实验输出和整理前清单 |
| `docs/` | 文档及本次整理记录 |

## 在 WSL 中构建和检查

需要 C++17 编译器、CMake ≥ 3.16 和 OpenCV 开发库。先在工程根目录执行：

```bash
cmake -S . -B build
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

`build/` 不再存储到 Git，需要在本机重新生成。新增实验输出也会被忽略；已有历史结果继续在 `archive/` 内受 Git 管理。

## 原有实验入口

```bash
# 四个应用的固定硬件实验：会运行完整搜索，耗时较长
bash scripts/run_all_force_active_pipeline.sh

# 复用已归档的 Stage1 数据进行重评估
bash scripts/rerank_force_active_stage3.sh \
  archive/experiments/force_active_pipeline_20261014_221356
```

重评估脚本也会自动从工程根目录和 `archive/experiments/` 查找可用的 `force_active_pipeline_*`。

其他历史工具若需要旧结果目录，请将其参数指向 `archive/experiments/` 下对应目录；原先散落在根目录的结果位于 `archive/experiments/root_outputs/`。当前运行脚本的算法参数不变。

完整备份分支为 `backup/before-cleanup-20261023`。本次移除项和验证记录见 [整理记录](docs/CLEANUP.md)。

## Sharpening 位翻转试验

单节点、单个位翻转的三阶段验证已单独提供入口，保留原近似基准与 ROI。
使用方法及评价口径见 [位翻转验证说明](docs/SHARPEN_BITFLIP.md)。

## 基准中间数据与位反转响应

`sharpen_bitflip_diagnostics` 导出原基准逐节点中间值，并按区域、节点、位和原监测值统计位反转后的局部及最终误差变化。它用于分析放大/补偿潜力，不搜索或选择攻击方案。
运行与数据口径见 [中间数据诊断说明](docs/SHARPEN_BITFLIP_DIAGNOSTICS.md)。

对照不触发、全范围触发、区间触发的入口与运行命令见 [触发区间对照](docs/SHARPEN_BITFLIP_INTERVALS.md)。同时筛选 ROI 放大区间和背景补偿区间；由用户在自己的终端运行。

多节点联合位反转、条件区间评分、候选图像与节点消融见 [多节点联合搜索](docs/SHARPEN_BITFLIP_JOINT.md)。该入口允许多个节点协同，最终数值达标与视觉确认分别记录。

一次运行前10张图片并精简图片输出，见 [十图批量运行说明](docs/SHARPEN_BITFLIP_BATCH.md)。默认每张图只保存一个候选的输出、ROI对比和背景对比，所有候选的数值指标与消融记录仍然保留。

Conv、Sobel、DCT 三个电路的多节点位反转验证见 [跨电路位反转说明](docs/CIRCUIT_BITFLIP.md)。默认各运行前10张图，每项三张关键图片，原基准和ROI保持不变；本轮入口不执行消融。

锐化的“多图筛选硬件、逐图自适应区间”试验见 [固定硬件与区间校准](docs/SHARPEN_BITFLIP_HARDWARE.md)。先用训练图产生节点/信号/位结构，在筛选图上只搜区间选定结构，最后冻结结构与训练起点库，对新图只更新区间。保留零触发安装节点，无额外使能参数，不执行消融。

Conv、Sobel、DCT采用同一套多图固定硬件流程见 [三应用固定硬件与新图区间校准](docs/CIRCUIT_BITFLIP_HARDWARE.md)：复用1～10训练结果，在11～20筛选各应用H*，最后31～40仅搜索区间。支持并行筛选、原生8/11/32节点和每图三张关键图片。

整体效率版见 [共享训练、渐进筛选与任务复用](docs/BITFLIP_WORKFLOW_SPEED.md)：默认 balanced 使用敏感性优先级、跨训练图共享候选、有排名上界的提前停止和分档区间投入；可用 `--workflow reference` 对照原搜索策略，或 `--adaptive-intervals off` 先独立验证筛选收益。提供只读 `--resume-from`，报告实际任务数及所有尝试成本。新增代码与测试由用户在终端运行验证。

针对上一轮耗时较长的 Sobel，新增可选 [连续细化与局部硬件搜索试点](docs/SOBEL_CONTINUATION_TRIAL.md)：使用 `--applications sobel --workflow continuation`，先校准已有结构再做局部修改，同时保留全局探索预算；区间搜索只启动一次并保留状态。原 balanced/reference 模式保留，实际效率及效果由用户运行比较。

将硬件发现与选择合并到同一批1–10的新入口见 [10图直接确定硬件](docs/TEN_IMAGE_HARDWARE.md)：`scripts/run_circuit_bitflip_ten_train.py`维护有总预算的共享结构池，默认80次中先完成首套10图、交错初评并预留20次补齐；复用训练参考/基准及既有逐图结果，不再运行11–20筛选。`--scheduling sequential`保留顺序搜索作对照。先用 `--application sobel`试点；31–40只校准冻结H的区间。

其他应用使用同一流程：`scripts/run_circuit_bitflip_ten_train_batch.py`默认运行锐化、Conv、DCT，构建一次，每应用各80次训练上限；`applications.csv`汇总实际耗时、候选完成情况和具体PSNR。可用`--applications sharpen,conv,sobel,dct`运行四项，单应用入口也支持锐化。各应用使用相同预算和排名规则，保留各自原基准、ROI与完整图像评分。

Conv四节点`ten_h2`可用`scripts/complete_conv_ten_train.py`补齐剩余6张训练图，复用原4条评价，保持原硬件与单图预算；原80次报告只读保留，新增6次单独计账。命令和版本校验要求见[10图硬件说明](docs/TEN_IMAGE_HARDWARE.md)。

更换测试集而保持原硬件，见[stage5冻结硬件测试](docs/STAGE5_FROZEN_RETEST.md)：`scripts/retest_circuit_bitflip_frozen.py`默认读取`data/stage5`全部新图，复用四个应用原来的冻结配置和训练起点，只逐图搜索区间。每张合法输出仅保留整图、ROI两张“精确／基准／现有方案”对比图；不重新训练，也不输出消融图。
