# 新测试集：固定原硬件，每图只输出两张对比

本入口沿用原来1–10训练图确定的硬件，不重新训练、不重新排名，也不使用新测试图更新起点库。新图片只搜索触发区间L/U。四个应用各自沿用已有的节点、监测信号和翻转位，以及其原新图搜索预算和整体PSNR要求。

默认读取`data/stage5`下全部图片，按文件名中的数字排序，例如`1.jpg、2.jpg、…、28.jpg`。文件夹内的图片数量自动读取，不限于原来的10张，也不会把`stage5/1.jpg`当成`stage1`中的旧图。输入保留原尺寸，使用原有灰度处理流程；DCT仍按原应用完成重建后评分。

## 在你的终端运行

作者未编译或运行本次代码、测试和实验；以下命令由你执行。新入口只测试，不负责构建。

```bash
git pull --ff-only origin feature/sharpen-bitflip &&
cmake -S . -B build-bitflip -DCMAKE_BUILD_TYPE=Release &&
cmake --build build-bitflip --target circuit_bitflip_fixed -j4 &&
ctest --test-dir build-bitflip \
  -R '^(circuit_bitflip_fixed_cli|circuit_bitflip_frozen_retest)$' \
  --output-on-failure &&
python3 scripts/retest_circuit_bitflip_frozen.py \
  data results/bitflip/stage5_frozen_retest_20261027
```

输出必须使用新目录；重复运行时换一个目录名。默认四个硬件来源是：

| 应用 | 原报告目录（相对仓库） |
|---|---|
| 锐化 | results/bitflip/other_apps_ten_train_scheduled_20261026/sharpen |
| Conv | results/bitflip/other_apps_ten_train_scheduled_20261026/conv |
| Sobel | results/bitflip/sobel_ten_train_scheduled_20261026 |
| DCT | results/bitflip/other_apps_ten_train_scheduled_20261026/dct |

程序会核对原报告已经完成、硬件与训练证据一致、起点只来自训练图，以及冻结文件的指纹。来源缺失或不一致时会停止，不会临时寻找另一套硬件。

若只测Conv，或原结果目录不同，可以明确指定来源。提供`--source`后只运行明确列出的应用；可重复此参数指定多项。

```bash
python3 scripts/retest_circuit_bitflip_frozen.py \
  data results/bitflip/stage5_conv_retest_20261027 \
  --source conv=results/bitflip/other_apps_ten_train_scheduled_20261026/conv
```

可用`--test-dir PATH`指定其他新图目录，或`--fixed-exe PATH`指定更新后的统一固定硬件程序。程序依次处理应用和图片，不并行启动多张图，避免改变单图耗时的比较条件。

## 图片只有两张

以Conv的`1.jpg`为例，图片位于：

```text
stage5_frozen_retest_20261027/conv/test/1/
  selected_comparison.png
  selected_roi_comparison.png
```

- `selected_comparison.png`：整张图三栏对比。
- `selected_roi_comparison.png`：重要区域外接矩形的三栏放大对比。

每张图从左到右均为`exact`（精确计算）、`baseline`（原近似基准）、`fixed H + intervals`（现有方案）。不单独输出结果图、背景图、差分图或消融图片。

合法结果即使整体质量未达标，也会输出这两张图，并在CSV中保留`quality_not_met`，方便人工比较；这不代表实验成功。没有合法区间状态或程序运行失败时不会伪造图片，原因保留在报告中。数值达标与视觉效果分开判断。

本轮仍使用原来的`data/mask/roi_mask.jpg`，按每张图片尺寸用最近邻缩放，阈值128区分ROI。它是统一的区域掩码，不会自动识别新图主体；裁剪框也可能含有背景。若掩码没有覆盖实际主体，ROI指标不能直接解释成主体质量。本轮不改变训练集或ROI规则。

## 保留的小型报告

根目录的`best_per_image.csv`包含逐图整体、ROI、背景PSNR，以及相应基准PSNR和两张图的路径；`applications.csv`给出每应用均值、数值达标张数、指标实际覆盖张数和测试时间，硬件搜索时间记为0。

`RUN.json`记录新图文件清单、输入/掩码/程序指纹、硬件来源与实际参数；`run_status.csv`保留每次运行耗时和错误。`images_all.csv`是图片索引，`configurations_all.csv`和`search_stats_all.csv`分别记录实际区间和搜索投入。每图目录保留原生小型报告供核对。

单图运行错误会保留失败行并继续后续图片；最终返回失败。质量未达标是有效实验结果，不冒充程序错误。手动中断会停止后续任务，并保存已经得到的报告。程序不复用旧测试结果，每张新图都重新校准，从同一份训练起点库独立开始。

运行后回传根目录的`RUN.json、applications.csv、best_per_image.csv、run_status.csv`和少量代表性对比图即可；需要分析搜索效率时再附`search_stats_all.csv`。
