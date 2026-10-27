# 位反转触发区间：首轮对照

本轮增加基于真实电路响应的离线区间筛选：对相同图片、节点、位、监测信号比较“不触发”“全范围触发”“选择区间”。原近似基准与 ROI 不变。

此版本由用户在自己的终端编译、测试和运行。编写方只进行了代码阅读与静态审查，没有运行本次新增代码；此前通过的 28 项测试不代表本版本已经通过。

## 最简运行

在已有仓库根目录，使用有 C++17、CMake、OpenCV 开发库和 Python 3.9+ 的 Linux/WSL 终端：

```bash
git fetch origin
git switch feature/sharpen-bitflip
git pull --ff-only origin feature/sharpen-bitflip

# 第一次先跑第 1 张，目录必须是新的
bash scripts/run_bitflip_interval_trial.sh data results/bitflip/interval_trial_01 1 1 64
```

脚本会编译诊断工具、生成原始 profiles、筛选区间，并保存日志。数据仍读取 stage1–3 的原图和 `data/mask/roi_mask.jpg`。大体积逐像素数据仅保留在本机，不提交到 GitHub。

如果已有本版本的完整诊断输出，可以直接复用，省去重新采样：

```bash
python3 scripts/search_bitflip_intervals.py \
  results/bitflip/baseline_diagnostics_20261023_image01_v2 \
  results/bitflip/intervals_from_existing_01 --boundary-budget 64 --global-psnr 30
```

输入需有完成标记、schema_version=1 和四份 CSV。首次完整端点复查可以使用一个新目录：

```bash
python3 scripts/search_bitflip_intervals.py \
  results/bitflip/interval_trial_01/profiles \
  results/bitflip/intervals_exhaustive_01 --boundary-budget 0 --global-psnr 30
```

默认 64 边界采用不同观测值索引的均匀网格，同时包含**每个观测值的单点区间**和全范围；不能称为全局最优。0 枚举全部观测值端点，耗时可能更长；其最优性只针对当前图像、节点、位、监测信号和单闭区间。未观测值不改变该图片上的触发集合，但不提供其他图片保证。

## 输出解释

`comparisons.csv` 为每张图片的全部 144 种结构各输出四行：

| mode | 含义 |
|---|---|
| off | 不触发，原近似基准 |
| full | 所有已观测监测值都触发；越界和低于质量阈值照实标记 |
| roi_amplify | 在质量约束下，最大化 ROI MSE 增量；还要求增量为正且超过背景 MSE 增量 |
| background_compensate | 在质量约束下，最小化背景 MSE 增量，且增量必须为负；ROI 的变化同时报告 |

两个目标的平手规则分别为：ROI 增量相同时全局误差更小；背景减量相同时 ROI 增量更大、再选全局误差更小。最后优先较短观测值跨度和较低下界。两种目标可能选同一个区间。

`selected_intervals.csv` 每图每目标选一个探索性最佳结构，方便先看结果；这是**逐图自由选择**，不是跨图片冻结硬件。没有符合要求的区间时，`has_interval=0`、`status=no_feasible_interval`，攻击质量字段留空，不能算作成功或基准替代结果。

全局阈值默认 PSNR≥30 dB。触发为闭区间 `[lower,upper]`；监测值必须取原反转前数值。只要一个区间触发任何下游越界样本，该区间就被拒绝，不能仅对有效子集评价质量。

ROI/背景 MSE 增量、触发比例均以**整个对应区域**像素数为分母，含不执行节点的边框。PSNR 使用同一全精确应用参考。净变化来自有符号最终 SSE 增量之和，不是局部节点误差或“误差增加的像素比例”。

## 评分依据与边界

输入的每个样本已经通过原真实后级近似单元和最终截断计算响应，单像素独立的 Sharpening 中，某区间的最终 SSE 变化可准确按原监测值聚合、用前缀和相减得到。因此本工具重用响应，无需为每个区间重新执行整张图。它没有简化原加法器内部，也没有把越界输入夹紧。

这两个目标都是相对于原基准分别筛选。背景补偿候选有可能同时恢复 ROI，必须查看 `delta_roi_mse`。把某个放大候选与补偿候选直接组合后，输入与位状态会改变，不能把两行增量相加。A 开启后的 B 评分仍需下一步重新生成。

这些是用原 ROI 与精确参考指导的逐图离线筛选；运行时触发只需要监测值和上下界。数据依赖、未见图片、固定硬件、多节点效果和面积延迟均不由本轮结果证明。

## 用户侧检查

```bash
python3 tests/test_bitflip_trigger_intervals.py
cmake -S . -B build-bitflip -DCMAKE_BUILD_TYPE=Release
cmake --build build-bitflip -j4
ctest --test-dir build-bitflip --output-on-failure
```

新测试用手算数据检查区间优于全范围、背景补偿、质量约束、越界拒绝、无解保留、窄区间、输入缺失/重复和输出覆盖保护。请将运行输出及 `selected_intervals.csv`、`comparisons.csv` 返回后再作结论。
