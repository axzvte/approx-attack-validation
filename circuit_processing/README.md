# 电路结构编码、统一选择与验证

## 三项改变

四个应用的新配置都采用 `C 内核 → 标量 SSA 图 → 统一选择器 → 通用执行器`。DCT 的完整行变换、列变换和逆变换同样进入节点图，处理位置由结构评分和误差证书确定。

精确替换采用结构贡献排序后的逐步补偿，未展开所有替换子集。`.c` 编码器直接读取 AST、展开固定循环和辅助函数，生成节点与连接，保存源码位置和调用栈。

历史版本的手写模型与组合枚举放在 `scripts/legacy_planning.py`、`scripts/legacy_circuit_profiles.py`，只供已冻结 schema 1/2 结果复现。新配置统一走 `scripts/circuit_ir.py`。

## 安装和编译

从工程根目录执行，Python 环境沿用已有环境。

```bash
source ~/projects/u2net_roi/.venv/bin/activate
python -m pip install -r circuit_processing/requirements.txt
cmake -S circuit_processing -B circuit_processing/build -DCMAKE_BUILD_TYPE=Release
cmake --build circuit_processing/build -j 4
ctest --test-dir circuit_processing/build --output-on-failure
```

`pycparser==2.23` 用于 C 语法解析。原先的验证二进制需要重新编译，schema 3 验证会先检查 IR 执行能力，避免使用旧二进制处理整个数据集后才发现接口不匹配。

## 查看自动生成的结构

```bash
python circuit_processing/scripts/fixed_roi_pipeline.py encode-c \
  --interface circuit_processing/configs/circuits/dct_mixed.json \
  --output circuit_processing/results/dct_encoded.json
```

接口文件只声明 C 源码、入口函数、像素采样/输出位置、分块边界和输入基准。运算连接由源码读取，编码结果保存在 `ir.operations`，每个节点包含 `id`、`op`、`args`、有符号性、位宽及源码位置。

`unit_node` 表示共享的硬件加法器组。展开后的 `id` 表示一次执行实例，两者共同确定替换与翻转位置。DCT 的 32 个加法器组对应 512 次前向执行实例，逆变换的常数乘法、加法与舍入也包含在图中。`circuit_add(group,a,b)` 给出共享硬件编号；普通整数加减可使用 `all_integer_adds` 按源码位置自动编号。

内置四个算法的 C 内核位于 `kernels/`。它们是从已有 C++ 算术实现整理的适配内核，编码器解析这些 C 文件。支持范围与运算约定见 `kernels/README.md`。

## 新的配置选择步骤

1. 读取输入基准及固定矩形，计算区域面积占比。
2. 在统一图上计算保守数值范围和单元误差传播。近似单元误差来自 `configs/unit_error_bounds.json` 的全输入审计。
3. 从输出反向传播绝对传递系数，给每个可翻转加法执行实例计算结构评分，给每个近似单元组计算误差贡献。
4. 在全精确补偿状态下，按结构评分检查有限个翻转候选，选择已有误差证书的候选。评分用于排序，实际扰动仍需图片验证。
5. 固定该翻转位置和位，按照结构贡献顺序逐步精确化单元组，找到满足全图预算的一个补偿前缀。
6. 通过前向误差传播重新验证区域内外误差上界，记录全部分析次数和选择轨迹。

默认最多检查 128 个候选、512 次前向误差分析。补偿前缀最多分 64 步。图中的节点数为 V、输出数为 P 时，反向传递计算按 V×P 的规模开展；每次前向分析按节点数线性开展。候选排序和补偿排序随节点及单元组数增长，没有 2^M 的替换子集项。传递矩阵超过 800 万元素时会给出资源上限说明。

`--max-candidates` 与 `--max-evaluations` 可修改上述预算。选择结果包含 `analysis_evaluations`、`selection_trace`、`structural_score`、`unit_node`、`source_coord` 等字段。

该过程是有分析预算的启发式方法，未找到证书不等价于无可行解，选中结果也不声明全局最优。绝对传递系数可能高估作用，误差上界仅用于安全检查。内部翻转的区域 MSE 下界仍为 0，视觉效果和实际 PSNR 由图片验证。

## 全图误差约束

对于区域面积占比 a、区域内外绝对误差上界 BR、BB，要求 `a*BR^2 + (1-a)*BB^2 <= 65.025`，对应 8 位灰度输出的全图 PSNR 下界 30 dB。程序检查近似加法器输入的有符号 12 位范围、翻转位置的 13 位范围，并处理绝对值、截断、舍入与限幅。

每次图像尺寸映射后再次核对面积预算。所有分块内核只在完整输出块落入重要矩形时启用内部翻转，使扰动局限于矩形。精确替换在全图生效。

## 执行四个混合基准

```bash
bash circuit_processing/scripts/run_all_applications.sh
```

脚本仍读取 `sharpen_mixed.json`、`conv_mixed.json`、`sobel_mixed.json`、`dct_mixed.json` 的原基准分配，默认每个应用 850 张图片。`ROI_RUN`、`IMAGE_DIR`、`RUN_DIR`、`IMAGE_COUNT`、`PYTHON_BIN` 可以通过环境变量指定。

也可以单独指定一个或多个 C 接口、已编码 schema 3 文件或旧显式图文件。

```bash
RUN="circuit_processing/results/unified_$(date +%Y%m%d_%H%M%S)"
python circuit_processing/scripts/fixed_roi_pipeline.py freeze \
  --circuit circuit_processing/configs/circuits/sharpen_mixed.json \
  --roi-run "$HOME/projects/u2net_roi/output/union_rc_850_01" \
  --threshold 85 --output "$RUN/frozen.json"
python circuit_processing/scripts/fixed_roi_pipeline.py verify \
  --config "$RUN/frozen.json" \
  --images "$HOME/datasets/PASCAL-S/datasets/imgs/pascal" \
  --count 850 --output "$RUN/verify"
```

新 freeze 对旧内置应用描述和显式图也先转换为同一 IR。历史 schema 1/2 冻结文件维持原配置复现。

## 结果

`frozen.json` 嵌入完整输入图与固定处理配置。`verify/images.csv` 和 `summary.json` 逐图记录原基准、仅补偿及处理后的全图、区域内外 PSNR，以及全图达标、区域下降和两者同时满足的数量。

`verify/comparisons/` 集中保存 `应用名_0001.png` 等三图拼图，包含精确参考、原基准、最终处理，以及处理前全图、处理后全图和处理后区域 PSNR。中间单图在临时目录使用，结束后清理。

`verify/psnr_results.xlsx` 每个配置一个工作表，保留五列 PSNR 数据和算术平均，显示两位小数。无穷值显式记录，失败图保留编号和标记，平均仅计完成结果。

```bash
python circuit_processing/scripts/fixed_roi_pipeline.py export-excel --results /实际运行目录/verify
python -m unittest discover -s circuit_processing/tests -p 'test_*.py' -v
```

本阶段验证软件算术与结构分析。RTL 映射、硬件计数器时序和 DC 综合面积留到后续。近似单元内部的布尔门级 C 实现继续使用已审计单元函数，自动源码编码目前面向上述有明确接口的应用算术内核。
