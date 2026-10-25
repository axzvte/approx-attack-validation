# 锐化：固定硬件结构，逐图自适应搜索区间

本入口实现两层配置：先用多张图选定硬件结构H*（安装节点、每节点的监测信号、翻转位），随后这些字段固定，每张新图仅联合搜索各节点的区间上下界。原近似单元、ROI和位宽保持不变。本轮先在Sharpening上验证协议，不宣称已实现任意电路的通用入口。

编写方只完成代码、测试编写和静态审查；没有编译、执行测试或运行实验。下面命令由用户在Linux/WSL仓库根目录执行。

## 推荐：复用已有十图训练结果

```bash
git fetch origin
git switch feature/sharpen-bitflip
git pull --ff-only origin feature/sharpen-bitflip

python3 scripts/run_sharpen_bitflip_hardware.py \
  data results/bitflip/sharpen_fixed_hardware_20261025 \
  --training-report results/bitflip/joint_10_images_20261024
```

`--training-report` 指向之前锐化十图批量输出根目录，其中应有 `summary_all.csv` 和 `configurations_all.csv`。只读取这些数据，不修改、不重跑已有实验。不能传入Conv/Sobel/DCT混合结果。来源必须包含每张训练图的基准及完整候选配置，不能包含筛选/测试图片、运行失败或孤立配置。

脚本先构建一次两个目标，然后形成候选池、筛选固定硬件、冻结结构并对新图校准区间。输出目录必须不存在，重复运行请换名称。依赖与原工程一致：C++17、CMake、OpenCV开发库、Python3标准库。

## 不复用旧结果：完整运行三阶段

```bash
python3 scripts/run_sharpen_bitflip_hardware.py \
  data results/bitflip/sharpen_fixed_hardware_fresh_20261025
```

默认分组如下，三个集合必须互不重叠：

| 阶段 | 图片 | 可以改变的内容 | 用途 |
|---|---|---|---|
| 训练 | 1～10 | 节点、监测信号、翻转位、区间 | 形成完整联合硬件结构候选 |
| 筛选 | 11～20 | 对每个候选结构仅改变区间 | 比较跨图表现并选定H* |
| 新图测试 | 21～30 | 保持H*，每图仅改变区间 | 评价固定硬件的逐图区间校准效果 |

测试时重新搜索区间是协议的一部分，不是重新选硬件。不能根据测试结果反选节点、信号或位，也不能把前一张测试图的新区间加入后一张图的起点库。

可用 `--train 1:10 --screen 11:20 --test 21:30` 显式设置分组；参数是包含两端的范围。较小的互斥范围可用于用户侧试跑，但不能把单张筛选结果当作跨图稳定性证据。

## 阶段一：去重并保留完整联合结构

硬件结构定义为所有安装节点的 `(node_id, monitor, bit)` 集合，忽略区间上下界。相同结构即使在多张图、多个区间中出现，也只作为一个候选结构，保留原来源证据和区间种子。不通过独立节点出现频率直接拼接新结构。

`candidate_pool.csv` 保留全部去重结构。默认最多筛选12套，按各训练图片候选排序轮流取结构，避免只采用某一张图的最优结果；未进入筛选的结构也保留在池中。`--max-hardware 0` 可比较全部去重结构，耗时随数量增加，不能声称默认12套就是全局最优。

每套结构的起点库仅来自训练图中该结构的合法区间组合，去重后默认最多8组（`--seed-limit`）。原始候选证据不会因种子数量限制而删除。导入旧结果时，完整来源表及存在的`RUN.json`复制到`training_source/`，随后从该归档建池；来源元数据若标记未完成或含运行失败则拒绝导入。

不复用来源时，训练入口使用新的有预算发现模式：先给锐化全部4个节点建立原基准上下文，再扩展联合状态，交替方向共享剩余预算。默认每张训练图最多256个新上下文（`--training-context-budget`），最终候选仍由原应用完整执行核对。训练阶段不执行消融，也不输出PNG。

## 阶段二：对固定结构仅搜索区间

新程序 `sharpen_bitflip_fixed` 的硬件文件只有三列：

```csv
node_id,monitor,bit
0,input1,7
2,baseline_output,3
3,input2,4
```

这是格式示意，不是实验推荐配置。锐化节点编号为0～3，监测信号为input1/input2/baseline_output，非符号翻转位为0～11。每个候选安装2～4个节点，搜索过程中节点集合、监测信号和翻转位都不能改变。

上下界分别写在各图 `configurations.csv` 中。输入监测信号的上下界必须在signed12范围内，原单元输出监测信号的上下界在signed13范围内，均要求lower≤upper。原下游近似单元的signed12输入合法性检查保留，不通过扩宽或截断掩盖越界。

求解器先检查该结构的训练区间种子，并为当前图片尝试构造合法无命中起点：在监测信号可表示范围内寻找未出现值，令L=U等于该值。它只保证当前图片不命中，并不是对任意输入强制禁用。若没有这种取值，还可使用已有合法种子；若全部起点均不可执行，明确报告 `no_legal_interval_state`。

无命中起点不需要额外使能位、反向区间或超范围哨兵。节点始终安装，比较器与固定位置异或门继续存在。区间上下界是可写配置参数。

区间优化基于当前联合配置重新采样，用既有精确条件响应评价目标节点的固定信号、固定翻转位和不同区间；完整重算下游输入、位状态、触发与截断。允许合法但暂时不满足整体质量的中间状态。节点覆盖先于多状态精调，缓存复用相同条件上下文，最终保留状态通过原 `SharpenApplication` 完整执行核对。

与自由搜索不同，**固定结构不删除零触发节点，也不要求每张图至少有两个节点实际触发**。报告分别记录 `installed_nodes` 和 `triggered_nodes`。某张图仅一个节点触发时，其余节点仍物理安装，不能据此声称每图均有多节点补偿。

筛选和测试使用相同的区间搜索设置及同一套训练起点库，默认：beam=4、refinement=1、restarts=2、boundary-budget=24、context-budget=128。位和信号不再作为逐图搜索变量。默认最终整体PSNR≥30 dB且ROI MSE高于原近似基准；没有额外背景数值硬门槛，不要求整体指标不变或deltaROI大于deltaNonROI。

## 跨图排名和冻结

对每套结构使用全部筛选图片，依次比较：

1. 数值达标图片数/请求筛选图片总数。
2. 全部筛选图中最弱的ROI PSNR下降得分。
3. 全部筛选图的平均ROI PSNR下降得分。
4. 表现相同时优先安装节点较少的结构，再按确定的结构键排序。

未找到数值候选的图片，排名得分为0，仍计入总分母；失败图不会被悄悄过滤。精确零MSE对应无限PSNR，排名计算单独将PSNR封顶100 dB以保持分数有限；原始MSE/PSNR报告不封顶。没有零MSE时，正常范围下该处理不改变PSNR下降值。

数值达标不自动表示视觉成功。自动选择的H*仍标记 `pending_user_review`。若要用视觉效果进一步筛选结构，应只查看11～20筛选图片，不根据21～30图片更换H*；可以用下面单图命令对相应筛选图生成对比图。

安装节点数仅是结构规模记录，不是实测面积或功耗。`screening_ranking.csv` 同时保留每套结构的成功比例、最弱/平均损伤、样本数和安装节点数。另有`success_mean_roi_drop_score`、`success_min_roi_drop_score`仅描述达标图片，不参与排名，不能替代全部筛选图的统计。

某项筛选程序或报告失败时，脚本继续处理剩余筛选项、保存日志和部分排名，但默认阻止冻结与最终测试；先修正失败再用新目录重跑。完整运行但没有数值候选，是正常搜索结果，不是程序失败。若所有结构均未找到数值达标图片，明确记录无可选硬件，不伪造H*或测试结果。

选定结构后，在读取任何测试图之前写出 `frozen_hardware.csv`、`frozen_seeds.csv` 和 `frozen_protocol.json`。测试图各自重新加载相同训练起点库。脚本检查训练库及冻结文件的SHA256，验证前后不变，防止测试间悄悄更新结构或起点。

## 输出位置

```text
sharpen_fixed_hardware_20261025/
  training_source/             导入时归档的完整来源表及来源元数据
  candidate_pool.csv            全部去重结构与筛选入选标记
  candidate_evidence.csv        训练来源与候选记录
  hardware/h0001/hardware.csv   一套固定节点/信号/位结构
  hardware/h0001/seeds.csv      该结构的训练区间起点库
  screen/h0001/image_11/        筛选阶段指标及逐图区间，不输出图片
  screening_results.csv        所有结构、所有筛选图片的结果
  screening_ranking.csv        跨图排名
  frozen_hardware.csv          最终固定结构
  frozen_seeds.csv             冻结的训练起点库
  frozen_protocol.json         测试前冻结的协议与摘要
  test/h0001/image_21/          新图指标、逐图区间及关键PNG
  best_per_image.csv           最终每张新图一行
  summary_all.csv              最终测试的基准与所选结果
  configurations_all.csv       最终测试的全部安装节点（含零触发）
  search_stats_all.csv         预算、覆盖、种子及耗时
  images_all.csv               最终实际导出图片的相对路径
  run_status.csv               各项执行状态、耗时、日志位置
  RUN.json                     全部参数、分组、来源摘要与完成状态
  logs/
```

默认仅最终测试的数值候选输出三张图：结果图、ROI对比、背景对比，十张新图最多30张PNG。对比图从左至右为精确输出、原近似基准、固定H*配合该图区间的输出。`--image-mode none`不输出图片；`full`包含额外对比和未达标合法状态的诊断图。本入口不生成消融表或逐节点关闭图片。

逐图状态为 `numeric_eligible`、`quality_not_met`、`no_legal_interval_state` 或 `run_failed`。预算耗尽通过 `search_status=budget_exhausted` 单独记录，不等于理论无解。`calibration_seconds` 是C++区间搜索阶段墙钟时间；`run_status.csv` 另记录包含进程启动及报告导出的整项耗时。

`RUN.json` 的 `completed=true` 表示协议正常结束；请同时看 `selection_status`、`test_executed` 和 `all_succeeded`，以区分真正完成测试、没有可选硬件及中途失败。不会用基准图伪装攻击成功。

## 用户侧测试和单图区间搜索

```bash
cmake -S . -B build-bitflip -DCMAKE_BUILD_TYPE=Release
cmake --build build-bitflip -j4
ctest --test-dir build-bitflip --output-on-failure

# 保持冻结结构，给一张图重新搜索区间；使用新的输出目录
./build-bitflip/sharpen_bitflip_fixed \
  data results/bitflip/fixed_h_image11_review \
  --hardware results/bitflip/sharpen_fixed_hardware_20261025/frozen_hardware.csv \
  --seeds results/bitflip/sharpen_fixed_hardware_20261025/frozen_seeds.csv \
  --image 11 --image-mode compact
```

默认需要精确参考与ROI、多次仿真，是离线逐图区间校准，不是单次实时处理。软件提供固定硬件所需参数，不等于已完成可综合RTL或实测硬件资源验证。以上新增测试和实验均未由编写方执行。

请返回 `screening_ranking.csv`、`frozen_hardware.csv`、`best_per_image.csv`、`search_stats_all.csv` 及代表性的ROI/背景对比图。出现失败时一并提供 `RUN.json`、`run_status.csv` 和对应日志。
