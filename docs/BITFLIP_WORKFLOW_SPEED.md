# 位反转固定硬件：整体流程效率版

本版同时调整训练候选生成、跨图筛选、新图区间校准和已完成任务复用。四个应用使用相同规则，不按电路手工改变目标或门槛。代码及新增测试仅做静态审查，编写方未编译、语法检查、运行测试或实验；加速和质量变化须由用户运行确认。

## 完整流程

1. 检查图片、原 ROI 和互斥分组；记录代码、程序、ROI 与任务输入的内容指纹。
2. 在训练图片 1–10 建立共享候选池。默认前两张进行完整预算的发现搜索；后续图片用四分之一发现预算继续探索，并校准来自先前训练图的最多两套结构。新发现及新训练区间都回到训练池。
3. 按完整 `(节点,监控信号,翻转位)` 去重，选最多 12 套候选。每套保存最多 8 组训练区间起点；筛选与测试阶段不会改写这份起点库。
4. 在筛选图 11–20 进行逐图区间校准。先完整评估一个候选作为当前最佳，再按有证明的排名上界提前停止无法胜出的候选。
5. 根据完整候选的成功数、最弱 ROI 损伤、平均 ROI 损伤、节点数和结构键选择 H*。读取测试像素前冻结硬件与训练起点库。
6. 测试图 31–40 独立加载相同 H* 与训练起点；每图只调整 L/U，可采用分档预算，最后保留所有实际执行档位中最好的合法结果。
7. 所有最终指标仍由原应用重放得到；仅保留选中结果、ROI 对比和背景对比三张关键图。视觉效果由用户确认。

整体 PSNR 默认不低于 30 dB、ROI MSE 高于原近似基准；不增加背景数值门槛。固定节点即使本图零触发也保留。掩码仍使用原文件并按图像尺寸最近邻缩放。DCT 仍在完整重建图像上评分。

## 两种工作策略

| 参数 | balanced（默认） | reference |
|---|---|---|
| 训练发现 | 共享候选 + 有限独立探索 | 原逐图完整预算独立发现 |
| 节点优先级 | 低成本敏感性代理排序 | 原访问顺序 |
| 硬件筛选 | progressive | exhaustive |
| 区间投入 | auto 对应分档投入 | auto 对应单档原预算 |

`balanced` 会改变搜索轨迹，候选或最终 H* 可能不同，不能声称与原算法质量等价。`reference` 用于比较原搜索策略；底层准备数据复用仍然有效。两者都不是穷举最优。

节点优先级只提供搜索顺序，使用有限、确定性 ROI/背景样本上的信号可分性和翻转幅度代理。不会永久删除节点，也不会把单节点表现解释成多节点补偿作用。真正的补偿效果仍在已有扰动的联合上下文中重新计算。代理存在额外准备开销，必须计入整体时间。

可调参数：`--pilot-images 2 --shared-hardware 2 --exploration-fraction 0.25`。它们对本轮所选应用一致。`--shared-hardware 0`仅关闭跨训练图的固定结构校准；独立探索仍保留。

## 安全提前停止与启发式投入是两件事

筛选排名先比较成功张数。候选的成功数上界为“已成功张数 + 剩余未评估张数”，低于完整当前最佳时可提前停止。上界相等时，如果已经观察到的最弱 ROI 分数严格低于完整最佳的最弱分数，也可停止。最弱分数相等时，不凭当前平均数提前停止。

只对**已结束该图完整校准策略**的结果使用这个界；低预算中间档不是淘汰证据。一个程序失败也不是淘汰证据，会阻止本应用冻结硬件。被提前淘汰的候选标记 `bound_eliminated`，保留实际评估张数、剩余张数和成功数上界，不伪装成评估了全部 10 张。

对于同一候选池、相同逐图校准策略和确定性求解器，提前停止不会改变最终获胜者。它不保证保留所有非获胜候选的完整排名。可用 `--screening exhaustive` 做完整筛选对照；并发按小批次推进，已开始的批次会结束，因而并发数可能影响节省的任务数。

分档区间投入是另一项**启发式**：默认使用 context/full-evaluation 上限的 1/4、1/2、全部三个档位，预算允许时低档至少保留安装节点数量对应的额度，但不突破用户设置的上限。低档采用一次访问、零额外细化，高档恢复所设 refinement/restarts；beam、边界参数不变。每档重新加载同一训练起点库，所有档位候选都通过原应用验证，最终保留最好的实际结果。至少比较两档，数值达标且 ROI 损伤改善低于 `--adaptive-min-gain 0.001` 才提前结束；未达标图继续到上限。

分档重启会重复部分准备与搜索工作；困难图三个档位总工作可能比单档更多，不能承诺这一选项必然加速或不损失搜索质量。`--adaptive-intervals off`恢复单档上限，适合先单独验证训练与筛选的收益；`on`强制启用，`auto`随 workflow。最终预算耗尽仍不是理论无解。

## 已完成任务复用

`--resume-from OLD_OUTPUT`读取旧结果，输出仍必须是一个不存在的新目录。旧目录保持只读，可以从被中断的运行中复用已完成单图任务；未完成、失败和缺少 `TASK.json` 的任务重新执行。

匹配内容包括应用、阶段、图片字节、原 ROI 字节、源码、程序文件、预算/停止策略、H、起点库和图片导出模式。任务目录中每个文件的 SHA256 与清单必须吻合；复制后再校验文件与报告。变更输入、ROI、硬件、程序、源码或搜索参数会使受影响任务失效。源码指纹不依赖可能落后的 Git HEAD。

这是一项**完整任务结果复用**；本版没有跨独立子进程共享整个图像追踪，也没有添加 Conv/Sobel 局部子图或 DCT 未受影响块的增量执行。单次搜索中的参考/追踪复用仍保留，不能宣称所有重复计算已经消除。

测试任务只会在本次 H* 冻结之后请求复用，仍检查本次图片与配置指纹。旧实验没有任务清单时，可继续用 `--training-report`显式导入其训练表，但不能把旧筛选结果当成缓存命中。`--resume-from`不替代训练来源，也不自动跳过本次协议的来源验证和硬件选择。

## 用户运行命令

在 WSL 仓库根目录更新并构建、检查：

```bash
git switch feature/sharpen-bitflip
git pull --ff-only origin feature/sharpen-bitflip
cmake -S . -B build-bitflip -DCMAKE_BUILD_TYPE=Release
cmake --build build-bitflip -j4
ctest --test-dir build-bitflip --output-on-failure
```

先复用已有训练候选，只比较跨图筛选效率，保持原单图预算：

```bash
python3 scripts/run_circuit_bitflip_hardware.py \
  data results/bitflip/circuit_workflow_screen_fast_20261025 \
  --training-report results/bitflip/cross_circuit_10_images_20261024 \
  --adaptive-intervals off --screen-workers 4 --skip-build
```

完整测试新训练方式与分档区间校准，不导入旧训练结果：

```bash
python3 scripts/run_circuit_bitflip_hardware.py \
  data results/bitflip/circuit_workflow_balanced_20261025 \
  --workflow balanced --screen-workers 4 --skip-build
```

锐化单独运行使用相同参数：

```bash
python3 scripts/run_sharpen_bitflip_hardware.py \
  data results/bitflip/sharpen_workflow_balanced_20261025 \
  --workflow balanced --screen-workers 4 --skip-build
```

更换新输出名称并追加 `--resume-from results/bitflip/circuit_workflow_balanced_20261025`可以复用该轮匹配任务。三应用入口会分别读取旧根目录下的 conv/sobel/dct。

原策略对照使用 `--workflow reference`，仍采用 1–10/11–20/31–40 的相同分组。单独检验安全筛选时，两次都导入同一训练来源并设置 `--adaptive-intervals off`，只改变 `--screening progressive`与 `--screening exhaustive`。若希望测量完整从头用时，不传 `--training-report`，不要把导入候选所省时间算作新训练策略的加速。

## 读取报告

- `RUN.json`：整体、准备、训练、筛选、测试和报告时间；筛选计划数、实际数、界淘汰数、缓存命中数。实际等待时间用 wall_seconds，不能将并发任务累计秒数当作等待时间。
- `screening_ranking.csv` / 三应用 `screening_ranking_all.csv`：新增 evaluated_images、remaining_images、screening_status、elimination_reason、success_upper_bound 和 score_scope。partial_evidence 行只有已观察证据，平均分仍使用计划图片数作分母，不能当成该候选完整十图的质量结论。
- `run_status.csv`：单任务状态和 cache_hit。缓存任务的原搜索指标仍是旧次执行证据，当前 elapsed_seconds 是校验/复制耗时；无当前求解日志。
- `interval_attempts.csv` / `interval_attempts_all.csv`：每档预算、实际上下文/完整评价次数、耗时、是否选中。单档也记录。失败档在任务日志和状态中保留，不会偷偷退回早期达标结果伪装成功。
- `best_per_image.csv`和三张关键图：最终选中的实际结果；不是盲目采用最后一档。
- `search_stats_all.csv`：选中档的原生求解统计。衡量所有投入时同时查看 attempts 和 wall 时间，不只看选中档。

请回传根目录 RUN.json、applications.csv（多应用）、筛选排名、best_per_image.csv、interval_attempts_all.csv（锐化为 interval_attempts.csv）和 search_stats_all.csv，并保留几张 ROI/背景对比图。评价应同时看总时间、候选数/实际任务数、ROI 平均与最弱效果以及背景视觉表现。
