# 第二部分：电路分析、修改与验证

入口为 `scripts/fixed_roi_pipeline.py`，包含 `freeze` 和 `verify` 两步。完整可复制命令见[根目录说明](../README.md)。从根目录编译时使用 `cmake -S circuit_processing -B circuit_processing/build`。

## 切换输入电路

从工程根目录执行以下命令，查看内置算法节点数和支持的加法器。

```bash
python circuit_processing/scripts/fixed_roi_pipeline.py list-circuits
```

`configs/circuits/` 中有四种原基准的完整配置、混合单元示例 `sharpen_mixed.json` 和新运算图示例 `custom_sharpen.json`。复制一份配置，修改 `name` 和每个节点的 `unit`，即可定义自己的输入基准。`unit` 支持 `exact`、`5QC`、`5QT`、`5L8`、`5RP`、`5TE`、`5PD`、`5PN`、`5SB`、`5Z0`。每个加减节点必须出现一次，编号从 0 开始。

下面以混合单元基准为例。生成配置时已经选定输入电路，验证时读取冻结文件中的完整输入配置。

```bash
source ~/projects/u2net_roi/.venv/bin/activate
ROI_RUN="$HOME/projects/u2net_roi/output/union_rc_850_01"
DATA="$HOME/datasets/PASCAL-S"
RUN="circuit_processing/results/mixed_$(date +%Y%m%d_%H%M%S)"

python circuit_processing/scripts/fixed_roi_pipeline.py freeze \
  --roi-run "$ROI_RUN" --threshold 85 \
  --circuit circuit_processing/configs/circuits/sharpen_mixed.json \
  --output "$RUN/frozen.json"
python circuit_processing/scripts/fixed_roi_pipeline.py verify \
  --config "$RUN/frozen.json" --images "$DATA/datasets/imgs/pascal" \
  --count 10 --save-examples 10 --output "$RUN/verify"
```

可重复指定 `--circuit`，在同一次运行中比较多个独立输入基准，各配置的 `name` 必须不同。也可在 freeze 时使用 `--apps sharpen sobel` 只选内置默认基准。verify 的 `--apps` 指冻结文件中的配置名称，例如 `sharpen_mixed`。

输入基准由使用者给定。混合示例只是展示文件格式，未经图片筛选。冻结文件同时保存原始节点配置和推导出的处理配置。验证不会再次读取原来的配置文件，编辑原文件不会改变已冻结的运行。

## 接入新算法

`application: "graph"` 使用逐像素运算图执行新算法。`custom_sharpen.json` 给出了完整示例，可以直接替换上面命令中的配置路径。`graph.inputs` 把变量绑定到当前像素附近的 `dx`、`dy` 位置，并可使用整数 `scale`、`bias`。边界采用 `replicate` 或 `reflect101`。

`graph.operations` 按执行顺序列出节点，支持 `add`、`sub`、`abs`、整数 `scale`、算术右移 `shift` 和 `clamp`。`args` 使用先前变量名或整数常量。加减节点的 `id` 连续编号为字符串 `"0"`、`"1"` 等，其余运算使用唯一名称。`graph.output` 指定输出，最终限幅至 0—255。右移对负数向下取整。

新增 C/C++ 算法需先把实际运算关系、位宽和边界规则转换到该接口。块变换、循环状态、浮点或当前算子之外的操作，需要补充对应执行与误差模型。现有 DCT 通过已审核的专用后端接入。每个近似加法器使用有符号 12 位输入和 13 位输出，无法证明输入范围满足要求的图会在 freeze 时给出原因。

九种单元的误差界保存在 `configs/unit_error_bounds.json`，来自逐单元全输入遍历。复核程序为 `tests/audit_unit_errors.cpp`。区间传播可保守高估误差，因此“未找到可证明满足约束的配置”需要结合报告理解。`--max-subsets` 默认 10000，限制精确替换子集的枚举数量，返回内部翻转配置时，`examined_subsets` 和 `search_exhaustive` 记录检查数量与是否完成全部子集检查。像素端备选通过 `reason` 说明未找到已认证的内部候选。

```bash
python -m unittest discover -s circuit_processing/tests -p 'test_*.py' -v
```

原 schema 1 冻结文件可以继续验证。新生成的 schema 2 文件嵌入输入配置和源码指纹，可核查输入基准与后续处理的区别。

## 两步的作用

`freeze` 读取第一部分的矩形统计和输入电路，根据算术模型、逐节点近似加法器误差界和全图 MSE 预算，确定精确替换节点、翻转节点和位。`--threshold 85` 选择统计中的 85% 矩形，`--min-bit` 默认 4。

`verify` 固定使用冻结文件中选定的电路。省略输入电路参数时，freeze 沿用 sharpen、conv、sobel、dct 四种原基准。`--count 10` 用于先检查流程，后续可在新输出目录运行 `--count 850`。每次输出目录必须不存在。

| 文件夹 | 内容 |
|---|---|
| `scripts/` | 冻结配置与汇总验证结果 |
| `configs/` | 算术模型对应的源代码指纹 |
| `app/` | C++ 验证入口 |
| `src/`、`include/` | 算术核心、区域判定和共用接口 |
| `third_party/evoapproxlib/` | 保留可供替换的近似加法器源码 |
| `tests/` | 当前位翻转回归测试 |
| `build/`、`results/` | 本地生成的编译产物和结果，不上传 Git |

## 结果怎么读

`frozen.json` 给出区域坐标、节点配置和误差界。`verify/images.csv` 对每张图片记录三种输出：原近似基准 baseline、仅精确替换 compensated、精确替换加翻转 processed。三者都与精确参考比较，分别统计全图、矩形内、矩形外 PSNR。

`verify/summary.json` 中 `global_pass` 是全图 PSNR 达到 30 dB 的张数，`roi_decreased` 是区域 PSNR 比原近似基准下降的张数，`joint_pass` 是两者同时满足的张数。另有 `roi_decreased_vs_compensated` 用于观察翻转本身的作用。退出码非零表示运行错误或全图不达标；退出码为零仍需检查区域是否下降。

每次 verify 完成时，自动生成 `verify/psnr_results.xlsx`，并在终端打印绝对路径。每个应用或输入基准配置独占一张工作表，依次列出图片编号、处理前全图 PSNR、处理后全图 PSNR、处理前重要区域 PSNR、处理后重要区域 PSNR。末行是逐图 PSNR 的算术平均。数值单位为 dB，显示两位小数并保留底层精度，表头冻结，支持筛选。

`∞` 表示该范围内与精确参考完全一致，平均中也计入无穷值。失败图片保留编号并标记，PSNR 留空，平均仅使用完成结果。Excel 导出仅依赖 Python 标准库。

已有运行可以直接从 `verify/images.csv` 补导出 Excel。

```bash
python circuit_processing/scripts/fixed_roi_pipeline.py export-excel \
  --results /实际运行目录/verify
```

当前原始算术规则见 [METHOD.md](METHOD.md)。混合配置使用每种单元的实际误差界传播，DCT 的处理路径将输入基准中的全部近似节点精确化后修改终端 DC 系数。已有配置与源码指纹绑定。DC 面积和硬件计数器时序留待后续。

