# 固定位翻转：Yosys / Nangate45 面积评估

这是用户手动执行的面积流程。脚本不会随导入、拉取代码或运行图片实验自动启动综合。
U²-Net、ROI、PSNR、训练和区间搜索均不在待综合RTL中。软件搜索算法、历史图片和原始报告保持不变。

## 按旧方法评估当前方案（2026-10-28）

要与之前`DC_Area`中的结果核对，请显式使用`--legacy-kernel`。
不加此参数仍为原来的“选中单元组”模式，两类百分比不能混用。

已核对旧工程的Liberty SHA-256为
`8d540a4d4cf6d09d27c87ad067857a9c0c2eeb023ab7a56e058cd3113db4e9b1`，
与当前下载器固定的文件完全相同。新模式会再次检查字节一致性。
旧脚本的映射步骤为`read_verilog; synth -top ... -flatten -noabc; dfflibmap; abc; clean`，
该模式不额外执行`dffunmap`。映射完成后才读入库黑盒作严格检查和JSON导出，不再执行逻辑优化。

新模式复用旧实验的四个完整Baseline算术核心RTL（含原连接关系和输出处理）：
Sharpening、Conv、Sobel分别为单像素算术核心；DCT为包含乘法和Q15舍入的一维8点变换核心。
不含行缓存、完整二维DCT/IDCT、总线、图像存储或U²-Net。
来源和原文件指纹见`hardware/legacy_area/provenance.json`，文件均保持原始字节；
程序不执行外部旧Python构建脚本，也不依赖本机必须保存旧工程。

本次不做结构优化：仍使用当前固定H的4/6/6/6个节点、监控信号、翻转位、96/148/148/148位区间配置。
像素核原生监控/结果为signed13，DCT为signed32；输入监控的界限仍signed12，输出监控的界限仍signed13。
比较时保留原生监控值并将界限符号扩展，不能把DCT监控值截为12位或擅自改成0～255。
翻转发生在完整原生结果的固定位0..11，再进入原来的后续运算和输出处理。

在VS Code WSL项目终端运行（复用已经成功校验的工艺库）：

```bash
cd ~/projects/approx_attack_validation &&
git switch feature/u2net-roi-stage1 &&
git pull --ff-only origin feature/u2net-roi-stage1 &&
OUT="results/area/bitflip_legacy_method_$(date +%Y%m%d-%H%M%S)" &&
python3 scripts/run_bitflip_area.py \
  --legacy-kernel \
  --library hardware/libcache/nangate45/library_manifest.json \
  --output "$OUT" &&
cat "$OUT/summary.md"
```

如仅准备脚本，可加`--dry-run`，不会查询Yosys版本或启动综合。
所有结果进入新目录，不覆盖旧实验。真实综合仍由用户手动执行。

用户需要核对的三个数值在`summary.csv`中对应：

| 评估项 | 字段 | 含义 |
|---|---|---|
| 配置存储面积 | `config_storage_um2` | 独立上下界寄存器及装载选择逻辑 |
| 区间比较和翻转逻辑面积 | `attack_comb_um2` | 独立、可变界限的有符号比较和固定位翻转 |
| 集成后实际新增面积 | `integrated_delta_um2` | 完整算术核心接入后面积减去同一核心Baseline面积 |

配置存储进一步分为`config_storage_sequential_um2`和`config_storage_load_logic_um2`。
`attack_complete_um2`为完整新增模块单独综合的面积，不用前两项相加替代。
`kernel_overhead_percent`分母为重测的完整算术核心Baseline，不能称为整帧图像系统开销。

报告同时列出`configured_bits`、`retained_config_bits`和`pruned_config_bits`。
独立存储模块必须保留所有配置位；集成核心允许综合器根据原图的常量/可达范围消除冗余状态，但必须明确报告减少位数。
这不代表搜索算法改变了配置位宽，也不能用静默少计寄存器来声称优化。

`historical_baseline_um2`和`historical_baseline_delta_um2`用于核对旧结果：
Sharpening293.132、Conv482.790、Sobel513.380、DCT16686.446 um²。
`historical_baseline_reproduced`若为False，应先检查工具版本、映射和数据范围，不能直接作优劣比较。
即使Baseline复现，两轮攻击节点和监控选择仍不同，旧/新总差异也不能全部归因于载荷机制。
先取得本轮三部分面积和单元数量，之后再决定是否研究配置压缩、监控范围缩减或节点减少。

## 评价范围与论文对齐

参考论文第8页只写明DC + FreePDK45，未披露具体库版本、工艺角或时序约束。
本流程采用基于FreePDK45的Nangate45 typical标准单元库，由Yosys/ABC映射。
这是明确记录设置的同工艺体系评估，不声称复现论文的DC网表、面积、时序或功耗。

库来源为The-OpenROAD-Project/OpenROAD-flow-scripts，固定commit：
`64939f3717ad7c694199ba04cb98924b98022868`。
下载器核对Liberty、对应LEF和LICENSE的Git blob，保存SHA-256；用Liberty面积与LEF的微米尺寸交叉验证um^2单位。
电压、温度和时间单位从实际库读取，不猜测。库文件按原许可证保留，不随本代码复制到GitHub。

校验仅将逻辑单元列为可用于映射面积统计的单元。LEF中标记为`CORE SPACER`、
且Liberty中为`dont_use`并且无信号引脚或状态的物理填充单元，单独记入
`library_manifest.json`的`excluded_physical_cells`，不进入`lef_verified_cells`。
固定版本的`FILLCELL_X2`在Liberty中的面积为0.266，而LEF尺寸0.38×1.4对应0.532 um²；
该差异保留记录，不修改上游文件，也不阻断逻辑综合。
普通逻辑门和寄存器仍须通过面积一致性校验；如果映射网表出现被排除的填充单元，报告器仍会拒绝统计。

第一版**每个选中节点对应一份独立算术单元，不做时分复用**。原本精确或5RP的选择保持不变。
一个应用的多个单元构成独立输入的算术单元组，不包含完整滤波/DCT图、帧缓存、流水线寄存器、主机或配置总线。
所以配对面积百分比仅指**选中算术单元组**，不能改写为整个图像应用的面积开销。

|应用|选中节点|其中Exact / 5RP|上下界存储位数|
|---|---|---:|---:|
|sharpen|0,1,2,3|0 / 4|96|
|conv|0,1,3,5,6,7|4 / 2|148|
|sobel|0,2,3,5,7,8|3 / 3|148|
|dct|5,15,16,22,28,29|3 / 3|148|

固定node/monitor/bit见`hardware/bitflip_area/current_hardware.json`。
节点源信号与翻转位在展开时固定；L/U保留为有符号可写寄存器，一次并行cfg_load，无复位和双缓冲，处理前必须装载。
输入监控为signed12，原始输出监控为signed13；只翻转输出0..11中的固定非符号位。
baseline_output在翻转前获取，避免组合反馈。配置寄存器和装载选择逻辑均计入完整开销。

5RP的RTL逐门对应工程C实现，输入次序遵守软件C(raw_b,raw_a)，没有用普通加法替代近似加法。
基准源文件的规范化文本哈希随配置固定，若基准代码变化，生成器会拒绝沿用旧配置；需要重新核对基准节点列表及功能测试。

## 在VS Code的WSL终端运行

需要Python3、Yosys及其ABC。Icarus Verilog/vvp和gcc只用于功能测试。
不需要U²-Net环境、图片、DC或OpenCV才能运行本面积流程。

```bash
cd "$HOME/projects/approx_attack_validation"

# 首次下载约7MB公开工艺库，并校验来源、物理尺寸和实际工作条件。
python3 scripts/fetch_nangate45.py --output hardware/libcache/nangate45

# 用户启动实际综合。输出必须是新的目录。
OUT="results/area/bitflip_nangate45_$(date +%Y%m%d-%H%M%S)"
python3 scripts/run_bitflip_area.py \
  --library hardware/libcache/nangate45/library_manifest.json \
  --output "$OUT"

cat "$OUT/summary.md"
```

逐条执行并检查返回状态；或用`&&`连接，保证上一步失败后不继续。
下载器只在用户运行时访问网络，已有有效缓存只做验证。中断下载不会发布缓存；若复制阶段中断留下不完整缓存，会拒绝复用，改用新的`--output`目录即可，程序不会覆盖已有文件。

只生成RTL、综合脚本和计划，不启动Yosys（包括不查询版本）：

```bash
python3 scripts/run_bitflip_area.py \
  --library hardware/libcache/nangate45/library_manifest.json \
  --output "results/area/bitflip_plan_$(date +%Y%m%d-%H%M%S)" \
  --dry-run
```

dry-run仍需已经验证的库，状态为`dry_run`，没有测量结果和面积汇总。
可通过`--config`提供相同JSON格式的其他固定结构；上下界不能写进硬件配置成为常量。
输出禁止与库、代码、原配置重叠；已有输出目录不会覆盖。

## 每个应用的五项综合

|top种类|范围|主要用途|
|---|---|---|
|attack_comb|边界为输入的比较、命中与固定单比特异或|组合逻辑开销|
|config_storage|L/U寄存器和装载控制，寄存器输出可观测|配置存储开销|
|attack_complete|完整可配置新增模块，monitor和原输出是模块输入|新增模块自身面积|
|host_baseline|选中节点的原精确/5RP算术单元组|局部对照基准|
|host_attacked|同一个算术单元组加可配置攻击|集成后的局部面积|

四应用共20项；五种top分别综合。独立综合分项不可直接相加当作集成开销。
每项另按映射单元类型统计组合、时序面积。汇总的`complete_combinational_um2`和`complete_sequential_um2`是完整新增模块内的两项面积；独立综合的`config_storage`还包含装载组合逻辑，不能直接当成纯时序面积。
比较器监控信号在独立模块中为端口，在host_attacked中真实连接到原单元输入或原输出。

`integrated_delta_um2 = host_attacked_um2 - host_baseline_um2`。
`selected_bank_overhead_percent = 100 * integrated_delta_um2 / host_baseline_um2`。
如差值异常或为负，应检查映射及共享优化，不能直接解释为新增硬件无开销。

## 综合约束与输出验证

所有top采用相同脚本：读取RTL、flatten/synth -noabc、dffunmap、dfflibmap、ABC库映射、clean/check、统计及导出网表。
为兼容Yosys 0.33，`tee -o`使用运行输出目录下不带引号的相对报告路径；
即使输出目录本身包含空格，也不把引号作为文件名写入。其他文件路径继续使用各命令支持的引号处理。
dffunmap将配置使能转换为普通寄存器和选择逻辑，避免把配置装载成本遗漏。
当前固定映射流程未设定用户时钟目标，不声明时序收敛，也不报告功耗或布局布线面积。

报告器逐个核对映射单元的库面积与LEF验证名单，交叉核对Yosys统计、网表单元数量及面积之和。
完整/存储/集成top必须保留所声明的96或148个配置存储位；自定义结构按实际位数验证。
遇到内部未映射单元、空面积、位数不足、未知单元或源文件变化立即失败，保留日志与已完成条目，不生成完整成功结果。

输出目录包含：

- `summary.csv`、`summary.md`：四应用、五项面积、集成差值及局部百分比。
- `run_manifest.json`：`preparing/running/complete/failed/dry_run`状态，库条件、工具版本、输入和输出哈希。
- `design/design_manifest.json`：明确的评价边界、当前硬件和算子配置。
- `design/rtl/`：本次实际使用的RTL快照。
- `scripts/*.ys`：20份完整综合脚本。
- `results/<top>/yosys.log`、`stat.json`、`mapped.v`、`mapped.json`：每项结果与证据。

只有`run_manifest.json`状态为`complete`且20项均通过核验，才能使用汇总数值。
面积是映射后的标准单元面积um^2，不是芯片占地面积。论文的1.47%指延迟增幅，不能拿来当面积开销率。

## 功能检查与当前验证界限

```bash
python3 -m unittest discover -s tests -p 'test*area*.py' -v
python3 tests/test_fetch_nangate45.py -v
```

测试仅用临时RTL、合成输入、原C算子和虚构综合报告。Icarus/gcc不可用时相关功能测试明确跳过，不会冒充已验证。
已包含5RP/C的16,640组输入比对、有符号闭区间、全部非符号翻转位、寄存器保持/更新、20个生成top的展开及不相等输入的集成检查。
综合流程测试通过拦截工具调用验证20项调度、面积与位数检查、dry-run不调用Yosys及失败状态。
另用真实Yosys的`tee`和`help stat`检查报告文件写出及带空格目录的兼容性；该项不加载RTL、工艺库或执行综合。
真实库下载、实际Yosys综合以及相关真实面积数值由用户手动运行后确认；当前没有预先生成或编造面积结果。
