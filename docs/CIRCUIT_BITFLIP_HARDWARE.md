# Conv、Sobel、DCT：多图选硬件，新图只搜索区间

本入口沿用锐化已验证的协议，分别为三个原生应用选择自己的固定硬件H*。每套H*只包含安装节点、每节点的监控信号和翻转位；筛选及新图测试只改变闭区间L/U。相同流程、数值门槛、跨图排名和预算参数应用于三个应用，不针对某个电路更换评价目标。

代码及测试仅完成静态审查，编写方未编译、运行测试、语法检查或实验。以下命令由用户在Linux/WSL仓库根目录执行。

## 推荐命令：复用之前三个应用的十图结果

```bash
git fetch origin
git switch feature/sharpen-bitflip
git pull --ff-only origin feature/sharpen-bitflip

python3 scripts/run_circuit_bitflip_hardware.py \
  data results/bitflip/circuit_fixed_hardware_stage4_20261025 \
  --training-report results/bitflip/cross_circuit_10_images_20261024 \
  --screen-workers 4
```

训练来源是包含`summary_all.csv`、`configurations_all.csv`的原三应用批量结果根目录，不是某个单图子目录。每套应用只使用自身标记的数据；所有来源行须属于请求训练分组，来源有未完成/失败、孤立配置或非法位宽区间时明确拒绝。完整来源表按应用归档，原文件只读。输出目录必须不存在，重跑换新名称。

不指定`--training-report`时，脚本调用原`circuit_bitflip_joint`为1～10重新生成候选；这会增加训练发现成本。可用`--applications conv,sobel`或`--applications dct`选择应用，但共同的搜索参数对本次选中的应用一致。

## 三阶段及固定范围

| 阶段 | 默认图片 | 可改变参数 |
|---|---|---|
| 训练候选 | 1～10 | 原生节点、信号、位、区间 |
| 硬件筛选 | 11～20 | 每套候选只调整区间 |
| 冻结后的测试 | 31～40 | 复用各应用H*，每图只调整区间 |

默认21～30不参与本轮选择或测试。可显式设置`--train 1:10 --screen 11:20 --test 31:40`，分组必须互斥。原生节点数量为Conv8、Sobel11、DCT32；候选可以安装2～该应用全部节点，不套用锐化4节点上限，也不要求所有已安装节点在每图均触发。合法零触发节点保留，报告分别记录installed_nodes和triggered_nodes。无合法区间状态时仅报告固定H身份与失败状态，不伪造执行配置。

输入文件优先`data/stageN/input/image_NN.jpg`，不存在时查找`data/stageN/image_NN.jpg`，支持1～40。截图中的`data/stage4/image_31.jpg`等可直接使用。正式批量运行在创建输出、编译或筛选前先检查路径存在；只检查文件位置，不读取测试像素参与硬件选择。ROI仍读取`data/mask/roi_mask.jpg`并按各图尺寸最近邻缩放，不自动识别主体，也不改变掩码策略。

Conv、Sobel、DCT使用原应用默认近似单元及精确参考。DCT仍为原Balanced-10配置；Conv/Sobel仍为原默认混合精确/近似配置。输出位反转为signed13非符号位0～11，监控输入区间为signed12、原单元输出区间为signed13。保留原下游signed12合法性检查，不通过扩宽、截断或反向区间掩盖错误。不执行消融，不添加使能位。

## 候选、排名和冻结

候选按完整`(node_id,monitor,bit)`集合去重，保留原训练区间来源。默认最多12套结构，按不同训练图片排名轮流取，避免只保留单张图的结果；`--max-hardware 0`可比较整个池。每套结构保存最多8组去重训练起点，所有证据另行归档。

每个候选均在全部10张筛选图上进行固定结构区间校准。最终数值门槛为整体PSNR≥30 dB且ROI MSE高于正常近似基准；无额外背景数值门槛，不要求整体指标完全不变。排名依次按数值达标张数、全部筛选图的最弱ROI PSNR下降、全部图平均下降，再按更少安装节点及确定结构键打破平局。未达标图计0分并保留在分母；成功图单独统计列仅用于诊断。排名用PSNR100 dB数值封顶处理零MSE，原始指标不封顶。

筛选程序/报告失败会记录并继续本应用剩余任务，随后阻止其冻结；外层继续其他应用并在根RUN中标明失败。完整搜索但无数值候选是正常结果，不能伪造H*或图片。不同应用的H*互相独立，不能把相同的h0001编号当成相同电路硬件。

读取测试像素前写出每个应用自己的`frozen_hardware.csv`、`frozen_seeds.csv`及`frozen_protocol.json`。每张新图独立加载同一训练起点库，不能把上一张测试图的新区间写入后续起点。前后哈希校验保证固定文件不变。数值达标仍标记pending_user_review，视觉成功须看ROI及背景对比。

## 评分正确性与统一预算

Conv/Sobel按原树形、分支、符号及绝对值操作建立固定信号条件响应，下游监控值随联合配置重新计算；保留状态通过原应用完整重放核对。DCT同一静态节点会在块内行、列变换中多次执行，因此只以局部代理筛选有限区间提案，候选状态必须完整执行联合DCT并重建，再在真实像素ROI上评分。不能把DCT调用级误差相加当成最终图像损伤。DCT的block_roi_weight只用于触发诊断。

三个应用采用相同默认参数：beam4、refinement1、restarts2、boundary-budget24、context-budget128、max-full-evals256、full-candidates8。DCT使用完整评价预算时，优先覆盖安装节点并为后续联合细化预留预算；覆盖不足或预算耗尽在报告中明确记录。原基准/无命中初始化检查及最终核验独立于搜索完整评价预算，最终重放次数另列。Conv/Sobel用精确条件响应评价大量区间，不需每个区间完整跑图，所以full_evaluations的执行含义不同，不能直接据此比较电路难度。

训练发现的共同预算为training-context-budget256、training-max-full-evals4096、training-full-candidates16，复用旧结果时不执行该阶段。所有参数统一记录，不承诺穷举最优或某个固定加速比。预算耗尽不等于理论无解。

筛选默认并发`min(4,CPU逻辑核数)`，`--screen-workers 1`恢复串行。三个应用按顺序执行，以免将应用层与筛选层并行叠加；只构建一次两个目标。`--jobs`只控制构建。评估每个应用的硬件确定耗时看其RUN.json的screen_wall_seconds，不能把并行任务累计秒数当作实际等待时间。DCT原生完整重建成本更高，性能结果由实际报告判断。

`calibration_seconds`记录求解器搜索墙钟时间；`reference_seconds`和`final_replay_seconds`也包括CLI为核对结果而做的额外参考/重放，不能把所有阶段秒数相加要求等于该搜索时间。`run_status.csv`的elapsed_seconds覆盖完整子进程及报告读取。原锐化补跑入口会拒绝其他应用的冻结配置，避免误用锐化求解器。

## 输出与回传

```text
circuit_fixed_hardware_stage4_20261025/
  RUN.json                    三应用协议与失败列表
  applications.csv            各应用筛选/测试耗时与数值成功数
  best_per_image.csv          最终测试，每应用每图一行，最多30行
  screening_ranking_all.csv   各应用筛选排名
  frozen_hardware_all.csv     带application的固定结构汇总
  summary_all.csv
  configurations_all.csv
  search_stats_all.csv
  images_all.csv              图片路径已加conv/sobel/dct前缀
  run_status.csv
  conv/                       完整训练证据、筛选结果、冻结文件及测试输出
  sobel/
  dct/
```

各应用最终图保存在`APP/test/hXXXX/image_31/`等目录；默认compact每张达标图只输出结果、ROI对比、背景对比三张PNG，三个应用最多90张。对比顺序为精确输出、正常近似基准、固定H配合该图区间的结果。训练/筛选默认无PNG；`--image-mode none`关闭测试图，`full`包含合法未达标状态诊断图。

请返回根目录`RUN.json`、`applications.csv`、`screening_ranking_all.csv`、`frozen_hardware_all.csv`、`best_per_image.csv`、`search_stats_all.csv`，以及每个应用效果较好和较弱的ROI/背景对比图。程序失败时补充对应应用的RUN.json和日志。

用户侧验证命令：

```bash
cmake -S . -B build-bitflip -DCMAKE_BUILD_TYPE=Release
cmake --build build-bitflip -j4
ctest --test-dir build-bitflip --output-on-failure
```

这些是用户运行命令；编写方没有执行上述测试或实验。当前验证依赖精确参考和ROI，是离线区间校准；尚无综合RTL或实测面积/功耗结论。
