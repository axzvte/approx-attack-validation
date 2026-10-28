# 固定位翻转：Yosys / Nangate45 面积评估

这是用户手动执行的面积流程。脚本不会随导入、拉取代码或运行图片实验自动启动综合。
U²-Net、ROI、PSNR、训练和区间搜索均不在待综合RTL中。软件搜索算法、历史图片和原始报告保持不变。

## 评价范围与论文对齐

参考论文第8页只写明DC + FreePDK45，未披露具体库版本、工艺角或时序约束。
本流程采用基于FreePDK45的Nangate45 typical标准单元库，由Yosys/ABC映射。
这是明确记录设置的同工艺体系评估，不声称复现论文的DC网表、面积、时序或功耗。

库来源为The-OpenROAD-Project/OpenROAD-flow-scripts，固定commit：
`64939f3717ad7c694199ba04cb98924b98022868`。
下载器核对Liberty、对应LEF和LICENSE的Git blob，保存SHA-256；用Liberty面积与LEF的微米尺寸交叉验证um^2单位。
电压、温度和时间单位从实际库读取，不猜测。库文件按原许可证保留，不随本代码复制到GitHub。

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
真实库下载、实际Yosys综合以及相关真实面积数值由用户手动运行后确认；当前没有预先生成或编造面积结果。
