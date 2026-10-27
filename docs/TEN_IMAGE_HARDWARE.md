# 只用10张图直接确定硬件：共享结构联合搜索

本入口将训练发现和硬件选择合并：仅使用图片1–10得到一套固定H*，不读取或评价11–20；冻结后对31–40只搜索L/U。统一接口支持Sharpen、Conv、Sobel、DCT，规则相同。每个应用分别固定自己的H，同一应用的全部新图共用其节点、监控信号和翻转位。原 balanced/continuation 入口保留作对照。

编写方只编写代码、测试并做静态审查，没有执行构建、测试、语法检查、实验或仓库分析程序。下面命令由用户运行。新策略改变搜索轨迹和预算，不承诺相同硬件、同等效果或某个加速比例。

## 本次减少哪些工作

1. **没有逐图深度发现阶段。** 单个原生进程一次读入1–10及原ROI，为每图建立精确参考与正常近似基准；汇总各图内部信号的ROI/背景分布，提出少量初始共享硬件。每张图对优先级的贡献归一化，图片尺寸不同不会直接改变权重。代理统计只决定提案顺序，不作为最终图像评分。
2. **统一候选池。** 默认先提出3套不同规模的结构；随后每轮最多提出3套局部/全局结构。第一套先完成全部10图作为参照，其余候选交错获得初评批次，再优先补齐有竞争力的候选。局部提案来自当前已完整评价的优胜H，可增删节点、改变监控信号或翻转位；每轮优先保留一个独立全局提案。
3. **没有第二轮完整筛选。** 候选直接在同一批1–10上逐图校准区间并排名。当前候选已经无法在成功数或最弱ROI分数上超过完整优胜者时，可按严格上界提前停止。没有完成全部10图的候选不会成为最终H。
4. **统一计算预算。** 默认最多80个“硬件H＋训练图”校准任务、最多24套结构，每个训练任务上限64个条件分析。80次分为首套参照10次、探索最多50次、预留补齐20次；探索本身也可完成候选，未用的探索预算可转入补齐。每轮最多3提案、最多6轮，连续2轮没有更换优胜者时停止新增结构，再处理仍有竞争力的候选。所有这些都是启发式有限搜索，不是穷举。
5. **复用与连续校准。** 相同结构不重复加入，已评估的候选/图像结果直接保留。新结构继承父结构在同一张训练图上的区间作为起点，仍由原求解器验证；非法起点不会被当作合法执行。每个任务只调用一次连续区间搜索，保留最好解，重算改变配置后的联合下游触发。

精确参考和基准输出在同一训练进程内每图准备一次。预处理对象含不可变输入、掩码及应用配置身份检查，避免错用缓存；不保存10张图的全部大轨迹。每个候选仍可能为无触发起点收集基准轨迹，改变联合配置后也仍需重算相应条件统计，所以并非消除了所有像素扫描。

工作线程默认4，同一候选的一批图片并行执行；先预留任务预算，再启动线程，结果按图片编号汇总，完成时间不会改变排名。每个任务的追踪/条件缓存独立，没有跨线程共享可变追踪。

锐化也接入统一的准备缓存、连续区间校准和预算调度。其原计算链为A0(5×中心,-上方)、A1(A0,-下方)、A2(A1,-左侧)、A3(A2,-右侧)；条件评分按该依赖链重算下游操作数与触发，最终以原SharpenApplication完整运行复核。四个应用都保留原近似基准、ROI与位宽，DCT仍使用完整变换及重建评分。

## 预算如何调度

默认 `--scheduling interleaved --initial-batch 4 --completion-reserve 20`。同一批新候选先分别获得最多4张初评，随后选择有竞争力的部分候选继续评价。停止新增结构后，优先处理缺图较少的候选，再比较成功数上界、已观察到的最弱ROI分数、预计剩余条件分析量及结构键。预计工作量来自已完成任务的条件数，实际耗时不会改变调度顺序。优先级只决定先算谁，不作为淘汰证据。

已经评价过的“候选＋图片”不会重算，暂停期间也不会更换其创建时保存的起点。只有严格的成功数或最弱分数上界能淘汰候选；部分均分偏低不能证明它必输。即使8/10都很好，仍需实际完成另外两张才能参选。预留预算增加了补齐机会，不能保证所有候选都完成，也不能保证某个旧候选一定再次出现。

为避免总是先算1–8，默认用训练图的原近似基准ROI PSNR安排一个固定顺序：先按PSNR从低到高排序，再按排序位置0、9、4、2、7、1、8、3、6、5交错选图，同值按图片编号。它覆盖基准较差、较好及中间情况，只是训练数据上的排序代理，不代表攻击难度。报告的每图结果仍按图片编号排列，实际调度顺序另存。

小预算自动缩减预留量：`有效预留=min(请求预留, max(0, 总预算-首套参照所需次数-初评批次))`，给至少一套备选结构留下初评空间；原生接口若连10次都不足，只报告真实部分结果。`--completion-reserve 0`可取消预留，但仍使用交错策略。`--scheduling sequential`保留原来的候选逐套搜索及图片1–10顺序，预留量记0，用作对照。两种方式的区间校准预算、图像评分及最终排名规则相同。

## 评价、冻结与新图

最终数值资格仍为整体PSNR>=30dB且ROI MSE高于原近似基准，背景不加数值硬门槛；视觉破坏及背景正常仍需人工看图。Sobel/Conv保留精确条件响应和原应用重放；DCT按完整联合变换及重建图像评价，局部调用误差不能相加替代最终指标。

完整候选按10图数值达标张数、最弱及平均非负ROI PSNR下降排名，然后按更少安装节点和确定结构键打破平局。内部排名沿用基准归一化分数，报告同时提供整体/ROI/背景PSNR具体值。失败图记0分。只有已结束其同一预算校准协议的真实结果可作为淘汰证据；代理值不被冒充为上界。

部分候选标记 bound_eliminated 或 budget_incomplete，保留实际 evaluated_images；其平均分为已观察分数之和除以计划10张，仅是部分证据，不是完整十图效果。程序错误直接使本轮失败，不能算作可行性失败或提前淘汰。

选定H必须有全部10张的执行结果，且至少一张数值达标；不是要求算法一定找到10/10。若没有完整达标候选，报告正常无解，不伪造冻结文件或测试图片。最终默认保存最多2组不同的、数值达标训练区间，ID为train_NN_ten；不把测试图区间写入后续起点。

新图仍固定节点、信号和位，独立加载同一训练起点库，只调L/U；安装节点即使零触发也保留。测试校准默认128条件分析、连续细化；训练默认64，所以两阶段的搜索预算不是同一个数。只有训练图决定H，测试像素仅在冻结文件写出并哈希确认后读取。

## 用户运行

在WSL仓库根目录执行，构建/测试失败会阻止启动实验：

```bash
git switch feature/sharpen-bitflip &&
git pull --ff-only origin feature/sharpen-bitflip &&
cmake -S . -B build-bitflip -DCMAKE_BUILD_TYPE=Release &&
cmake --build build-bitflip -j4 &&
ctest --test-dir build-bitflip --output-on-failure &&
python3 scripts/run_circuit_bitflip_ten_train.py \
  data results/bitflip/sobel_ten_train_scheduled_20261026 \
  --application sobel --workers 4 --skip-build
```

不需要也不支持 `--screen` 或导入上一轮训练报告。本试点训练固定为1:10，新图固定为31:40，11–20不会参与任何阶段；其他分组会在写出结果前拒绝。输出目录必须不存在，不能写入只读sources目录。本轮沿用已经看过的31–40作开发对照，不应称为新的完全盲测。

可用 `--max-train-evaluations 40`进一步限制候选/图像任务（最低10），`--train-context-budget 64`限制单训练任务，`--context-budget 128`控制新图；预算越低可能漏掉更好配置。先用默认参数验证新流程，避免同时改变太多因素。旧Sobel continuation硬件确定约49分10秒是历史对照，不是同版本单因素实测基线。

要做同版本调度对照，在另一个新输出目录执行相同命令，追加 `--scheduling sequential`，并保持图片、线程和其余预算一致。两轮都不用resume。80次上限不等于固定用时；不同候选的单次校准成本不同，应同时比较硬件确定秒数、完成候选数、最终指标和背景视觉，不能预先声称提速。

可追加 `--resume-from OLD_OUTPUT`只读复用完整训练任务或匹配的测试任务，输出仍使用新目录。当前不支持恢复一个中断原生训练进程的局部搜索状态。内容指纹覆盖训练图片、ROI、程序、源码和搜索参数；首次测算法耗时请不用resume。缓存中保留的旧搜索用时与本次复制耗时分别报告，不能算作算法提速。

## 输出与回传

- RUN.json、applications.csv：实际硬件确定及测试等待时间、是否使用缓存、原生准备/搜索时间、无独立筛选的标记，以及首套参照/探索/补齐的调用次数和补齐完成的候选数。
- training_ranking.csv：所有候选及完整/淘汰/预算未完成状态。
- training_evaluations.csv、training_configurations.csv：每个实际训练任务的具体指标、配置、条件数和缓存标记；phase列标明reference、exploration、completion，顺序对照则为sequential。
- candidate_hardware.csv：候选结构池；frozen_hardware.csv、frozen_seeds.csv、frozen_protocol.json为最终冻结证据。
- best_per_image.csv、search_stats_all.csv、interval_attempts.csv、run_status.csv：新图指标及全部调用成本。准备/搜索原生计时另见search_stats.csv，其中还保存请求/有效预留、实际评价顺序和三个阶段的调用数。三个阶段次数之和应等于actual_evaluations；completion_candidates统计最后补齐阶段完成的候选，探索阶段完成的候选不计入此列。
- images_all.csv与test/H/image_NN：每张数值达标测试图三张关键PNG：结果、ROI对比、背景对比。训练无PNG。

请回传RUN.json、applications.csv、training_ranking.csv、training_evaluations.csv、best_per_image.csv、search_stats_all.csv、interval_attempts.csv、run_status.csv，并附好/弱图片的ROI和背景对比。比较硬件确定实际用时、达标张数、ROI PSNR具体均值/最弱图以及背景视觉，不能只比较调用次数。

## 批量测试其他应用

默认顺序运行锐化、Conv、DCT，Sobel可显式加入。各应用使用同一组搜索参数，构建一次，每个应用内部默认4个工作线程。默认三个应用合计最多240次训练校准，每个应用各80次；训练单任务64条件、新图单任务128条件。硬件排名仍依次比较数值达标图数、最弱与平均ROI下降分数，最后用节点数与结构键打破平局。

```bash
git switch feature/sharpen-bitflip &&
git pull --ff-only origin feature/sharpen-bitflip &&
cmake -S . -B build-bitflip -DCMAKE_BUILD_TYPE=Release &&
cmake --build build-bitflip -j4 &&
ctest --test-dir build-bitflip --output-on-failure &&
python3 scripts/run_circuit_bitflip_ten_train_batch.py \
  data results/bitflip/other_apps_ten_train_scheduled_20261026 \
  --workers 4 --skip-build
```

`--applications sharpen,conv,dct`为默认，可用`--applications conv,dct`仅跑两项，或`--applications sharpen,conv,sobel,dct`跑四项。也可用原单应用入口加`--application sharpen`等名称。所有应用保留1:10/31:40分组，数值达标新图默认各输出三张关键图片，没有消融实验。

新入口自动构建时只构建两个统一程序；`--skip-build`表示使用已经更新的程序。外部程序仍要求显式`--skip-build`。锐化在新入口调用`circuit_bitflip_fixed sharpen ...`，旧锐化入口继续使用其原程序；任务缓存记录入口类型，避免混用。

批量根目录包含：

- `applications.csv`：每应用的硬件确定时间、10张新图校准时间、应用总时间、实际训练调用数、各阶段计数、完整/淘汰/未完成候选数、安装节点数、数值达标张数及整体/ROI/背景PSNR均值。
- `metric_images`给出均值实际包含的合法测试图数量；无合法结果时均值留空，失败图不会被填成0分贝。PSNR均值为逐图PSNR的算术平均。
- `training_ranking_all.csv`、`training_evaluations_all.csv`、`training_configurations_all.csv`、`training_search_stats_all.csv`及候选/基准汇总保留全部应用标识。
- `best_per_image.csv`、`search_stats_all.csv`、`images_all.csv`、`interval_attempts_all.csv`、`run_status.csv`汇总新图与调用记录。图片与日志路径带应用前缀，例如`conv/test/H/image_31/...`；原始报告在`sharpen/`、`conv/`、`dct/`子目录。
- `RUN.json`记录整体完成与失败情况。单个应用失败会保留错误并继续后续应用；批量最终返回失败。手动中断会停止启动后续应用，汇总表仍保留已经完成的训练计数、阶段时间和已测图片指标，并给出实际metric_images数量。

要比较调度带来的时间变化，可在另一新目录运行相同命令并追加`--scheduling sequential`，两轮均不用resume，保持预算、图片和线程一致。以前多阶段流程与本入口的候选和任务数不同，应将其作为完整流程的历史对照。不同电路单次校准成本不同，有限预算搜索可能用不满80次；实际加速、ROI破坏和背景视觉均以用户运行结果为准。

批量复用可加`--resume-from OLD_BATCH`，只读访问其对应应用子目录，输出必须为新目录。比较算法耗时请关闭复用。回传批量根目录的RUN.json、applications.csv、training_ranking_all.csv、training_evaluations_all.csv、best_per_image.csv、training_search_stats_all.csv及代表性图片即可。

## 补齐Conv四节点候选

对已完成的`other_apps_ten_train_scheduled_20261026/conv`报告，`ten_h2`只评价了训练图3、4、7、9。专用补充入口复用这4条结果，仅补图1、2、5、6、8、10；按原评价顺序安排任务。固定原四个节点、监控信号及翻转位，沿用训练的64条件、beam4、refinement1、restarts2、boundary24等全部单图预算。

该候选属于初始全局提案，原本没有继承区间起点；补跑也不传入任何训练或测试起点库。已有的4张不重算，剩余6张全部评价，不做提前淘汰。默认使用原4个工作线程。精确参考与基准由独立固定硬件程序准备，因此补充耗时包含额外的进程、参考构建和报告开销，和原进程内缓存后的单任务计时分别标识。

补充入口不构建程序，会核对原训练任务的报告哈希、程序二进制指纹、训练图1–10与原ROI指纹；固定程序的身份来自原任务签名元数据，不读取31–40像素或测试分数。请保留运行原实验时的两个程序；若二进制已更换，入口会停止，不能直接混用旧4条证据。可用`--train-exe`和`--fixed-exe`指定保存的原程序。

```bash
git pull --ff-only origin feature/sharpen-bitflip &&
cmake -S . -B build-bitflip -DCMAKE_BUILD_TYPE=Release &&
ctest --test-dir build-bitflip -R '^conv_ten_train_completion$' --output-on-failure &&
python3 scripts/complete_conv_ten_train.py \
  data \
  results/bitflip/other_apps_ten_train_scheduled_20261026/conv \
  results/bitflip/conv_ten_h2_completed_20261027
```

这里只重新配置CMake以注册新Python测试，不需要重新编译实验程序。原80次搜索报告只读保留；新增6次单独记账，合计86次。这是预算之外的补充验证，不应当作为原80次算法的结果或耗时。发生程序错误时，失败任务保持可见，缺少有效结果的候选不会被标记完整。

新目录的`completed_candidate_evaluations.csv`保留10条候选评价及original/supplementary来源；`completed_candidate_configurations.csv`保留合法区间。`ranking_completed.csv`基于补充后的训练证据更新排名，`comparison.csv`直接对照该候选与原5节点优胜者，包含具体PSNR均值、达标张数、完整性与排名分数。`RUN.json`记录原调用数、补充调用数、版本核对和耗时，`run_status.csv`与`search_stats_all.csv`记录新增任务。

此入口不生成新图测试或PNG，不修改原冻结硬件；补齐后先看完整训练结果，再决定是否将该四节点配置用于31–40。回传新目录的`RUN.json`、`comparison.csv`、`ranking_completed.csv`、`completed_candidate_evaluations.csv`、`run_status.csv`即可。
