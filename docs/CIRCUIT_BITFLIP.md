# Conv、Sobel、DCT 多节点位反转验证

本入口将位反转方法扩展到另外三个现有应用，用于观察不同电路结构上的可行性。每个应用每张图独立搜索，不能据此宣称同一组固定硬件参数对未见图片泛化。当前版本由编写方完成静态检查，未编译、执行测试或运行实验；以下步骤由用户在 Linux/WSL 终端执行。

## 更新和运行30项试验

需要 C++17、CMake、OpenCV 开发库、Python 3（无需额外Python包）。在仓库根目录执行：

```bash
git fetch origin
git switch feature/sharpen-bitflip
git pull --ff-only origin feature/sharpen-bitflip

python3 scripts/run_circuit_bitflip_batch.py \
  data results/bitflip/cross_circuit_10_images_20261024
```

默认依次运行 Conv、Sobel、DCT 的第1至10张图片。构建一次 `circuit_bitflip_joint`，各项单独保存日志和报告；一项失败仍继续后续任务。输出目录必须不存在，重复运行请换新名称。

只选择某个电路或图片范围：

```bash
python3 scripts/run_circuit_bitflip_batch.py \
  data results/bitflip/dct_10_images_20261024 --applications dct

python3 scripts/run_circuit_bitflip_batch.py \
  data results/bitflip/circuit_first_images_20261024 --first 1 --last 2
```

可用 `--applications conv,sobel` 选择多个电路。`--skip-build` 仅适用于已编译本次版本；默认构建目录为 `build-bitflip`，可用 `--build-dir` 和 `--jobs` 调整。

## 保持什么、扩展什么

| 应用 | 原节点数量 | 保留的默认近似基准 | 最终质量参考 |
|---|---:|---|---|
| Conv | 8 | C2/C3/C5/C8为5RP，其余精确 | 精确Laplacian幅值结构图 |
| Sobel | 11 | 节点0/2/4/7/9为5RP，其余精确 | 精确Sobel输出 |
| DCT | 32个静态加减节点 | Balanced-10：8/10/11/13/16/19/22/25/28/31为5RP | 精确前向变换及IDCT重建输出 |

节点编号从0开始。所有原节点（含精确基准节点）都可在原单元输出后附加条件位反转；原计算单元不替换。每节点一个监测信号（input1/input2/baseline_output）、一个闭区间和一个非符号位。使用原13位有符号输出，候选位0至11；下游近似加法输入必须仍符合原signed12范围，不通过截断或扩宽来掩盖非法配置。

最终数值候选至少包含2个实际触发节点，默认整体PSNR≥30 dB，ROI MSE高于该应用的原近似基准，并尽量降低ROI PSNR。没有背景MSE/PSNR额外硬约束，不要求整体指标不变，也不要求deltaROI大于deltaNonROI。背景是否正常和主体是否明显受损仍需看图确认，数值候选保留 `pending_user_review`。

原基准低于30 dB时会明确记录，但仍允许它作为合法中间起点尝试后续补偿；不静默降低最终阈值。没有候选不等于理论不可能。

## 评分与搜索预算

Conv和Sobel按其真实树形/分支计算逐像素条件响应，包括符号变换、绝对值、下游触发和最终截断。条件区间评分与完整应用输出核对。中间状态不因暂时整体质量不达标而全部删除。

DCT的同一静态节点会在每块的行、列变换中多次调用。它使用当前联合配置真实采样的监测值生成候选区间，以局部代理及不同方向的代表候选形成有限短名单；**每个进入搜索状态的候选都运行完整联合配置及IDCT重建，以真实逐像素ROI计算图像指标**。局部代理只用于候选筛选，不是最终误差评分，也不把各次调用误差相加来声称图像损伤。

默认参数：

| 参数 | 默认值 | 含义 |
|---|---:|---|
| `--beam` | 4 | 按已配置节点数保留的状态宽度 |
| `--refinement` | 1 | 初始遍历后的调整轮数 |
| `--boundary-budget` | 24 | 候选边界预算（0表示该阶段全端点枚举） |
| `--top-per-count` | 2 | 每个实际节点数保留的报告候选数 |
| `--restarts` | 2 | 交替方向的搜索起点数 |
| `--global-psnr` | 30 | 最终全局PSNR下限 |
| `--max-active` | 0 | 0允许原电路全部节点；可显式限制为≥2 |
| `--max-contexts` | 256 | 每项条件上下文预算 |
| `--max-full-evals` | 4096 | 每项搜索中的完整应用评价预算 |
| `--dct-full-candidates` | 16 | DCT每次扩展进入完整评价的候选上限 |
| `--bits` | 0至11 | 可用逗号列表缩小候选位集合 |

不同电路评分代价不同，不承诺相同运行时间或穷举最优性。预算限制会使部分状态/节点/轮次未被覆盖；查看 `search_status` 和具体统计，不能把预算耗尽当作完整无解。预算可按需要加大，全部参数会记录。最终报告的复核与触发统计是额外步骤，不计入搜索阶段完整评价预算。

`search_stats.csv` 的 `context_nodes`（分号分隔编号）和 `context_node_count` 记录实际建立过条件上下文的节点，便于识别预算限制下的节点覆盖；覆盖全部节点也不表示穷举了全部联合配置。

`evaluated_intervals` 在DCT中统计提出的区间，在Conv/Sobel中统计精确条件区间评价；DCT的范围拒绝发生在完整配置评价阶段，`range_rejection_basis` 明确记录口径，不能直接以拒绝数除以所有提出区间数来比较三个电路的拒绝率。重复保留事件也不等于不同配置数量。

## 输出与图片

```text
cross_circuit_10_images_20261024/
  best_per_image.csv       每个电路、每张图片一行，含最优数值候选和图片路径
  summary_all.csv          所有保留候选、原基准及无候选状态
  configurations_all.csv   节点、位、监测区间及实际触发统计
  search_stats_all.csv     搜索上下文、完整评价数及预算耗尽标志
  images_all.csv           实际输出图片路径
  run_status.csv           各项运行状态、候选数、日志位置
  RUN.json                整体参数、版本与完成状态
  build.log
  logs/conv_image_01.log
  conv/image_01/           单项CSV、RUN.txt和PNG
  sobel/image_01/
  dct/image_01/
  ...
```

汇总表包含 `application` 和 `image`。默认 `--image-mode compact`，每项只从全部节点数候选中选择一个，按ROI MSE更高、全局MSE更低、节点更少排序，保存三张PNG：

- `*_output.png`：原尺寸联合结果。
- `*_roi_comparison.png`：ROI包围盒对比，最近邻放大2倍。
- `*_background_comparison.png`：ROI统一遮灰后的背景对比。

对比图从左至右为该应用精确结果、原近似基准、联合输出。30项最多90张PNG。数值最优仍需人工判断视觉效果。`--image-mode none`不输出图片；`full`输出所有报告候选的对比和差图及参考图。图片模式不参与搜索。**本入口没有消融CSV或逐节点关闭图片，也不执行这些额外实验。** 原Sharpening入口及已有实验不修改。

`dynamic_triggers` 是静态节点在实际运行中命中的调用次数。Conv/Sobel的区域触发按像素计；DCT的 `roi_triggers`、`non_roi_triggers` 是按块ROI比例分配的调用权重，可能为小数，`trigger_region_basis=block_roi_weight`。这不是重建图像上受损像素数。最终区域MSE和变化比例始终来自原尺寸重建图像的真实像素ROI（阈值128）。

`status=numeric_eligible`、`no_numeric_candidate`、`run_failed` 分别表示找到数值候选、当前搜索未找到候选、运行或报告失败。`search_status=budget_exhausted` 单独说明搜索预算不足，可能与前两者同时出现，不是程序失败。`RUN.json` 的 `completed=true` 表示已尝试全部请求项，`all_succeeded=true` 才表示所有项均完整运行并生成报告。

## 用户侧检查和单项运行

```bash
cmake -S . -B build-bitflip -DCMAKE_BUILD_TYPE=Release
cmake --build build-bitflip -j4
ctest --test-dir build-bitflip --output-on-failure

# 单项命令，也适合失败后使用新目录复核
./build-bitflip/circuit_bitflip_joint \
  conv data results/bitflip/conv_image01_manual --image 1
```

新增测试覆盖载荷执行、下游传播、原近似路径兼容、条件评分、DCT完整重放、图片模式、无候选和批量失败处理。测试代码已编写，但编写方未运行上述命令。

跑完后请返回 `best_per_image.csv`、`summary_all.csv`、`search_stats_all.csv`，以及各电路有代表性的ROI和背景对比图。出现失败时同时提供 `run_status.csv` 和对应日志。
