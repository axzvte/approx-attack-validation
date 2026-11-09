# 三个版本的 RTL

## 算术核心口径的面积综合

使用 `area/rtl/` 下的顶层做面积对照。三个版本使用同一算术数据通路，区域处理需要的硬件归入完整方案。

| 版本 | 计入综合的硬件 |
|---|---|
| `baseline` | 原混合近似算术核心 |
| `compensated` | 按配置做精确替换后的算术核心 |
| `processed` | 仅补偿版的算术核心，加上坐标计数、尺寸和边界寄存、区域比较及固定位翻转 |

Sharpen、Conv、Sobel 三版均以准备好的像素窗口为输入、组合像素为输出。窗口生成、图像边缘的输出旁路和流式输出寄存器在共同边界之外。完整版在 `in_valid && in_ready` 的接收沿提供该窗口的结果并推进坐标。

DCT 三版均包含计算二维前向变换所需的输入、行中间数据和系数寄存器，以及 16 周期行列调度。完整版还包含图像块坐标计数、区域状态锁存和指定阶段的翻转控制。基准和仅补偿版按单块握手接收，由外部决定一帧包含的块数。

完整版在 `frame_start` 锁存帧尺寸和四个半开区间边界。帧输入端计算 `edge(c,n)=(2*c*n+319)//640`，按 `frame_roi_left/top/right/bottom` 提供像素坐标。边界应落在对应帧尺寸内，允许空区间。面积包含硬件里的边界锁存和比较，投影计算属于帧输入端。固定选点、位编号和替换列表见 `area/config/selection.json`。

在 WSL 工程根目录执行：

```bash
bash circuit_processing/hardware/scripts/run_dc_core_area.sh sharpen
# 综合四个应用时使用 all
bash circuit_processing/hardware/scripts/run_dc_core_area.sh all
```

每次输出到独立的 `results/dc_core_area/run.*` 目录。脚本默认使用现有的 Nangate45 `stdcells.db` 和 10 ns 周期，组合核心用虚拟时钟设置相同的输入输出预算。比较每个应用的 `Total cell area`、时序和约束报告。支持用 `DC_LIB` 和 `DC_PERIOD` 指定库及周期。

重新生成这套 RTL 时运行 `python circuit_processing/hardware/scripts/generate_rtl.py --area-scope`。下文的流式协议对应常规 `rtl/` 顶层。

本目录提供四个应用的静态 SystemVerilog 实现，每个应用包含三个版本。生成器读取当前 C 运算图和冻结选择；RTL 不执行图像特征提取或配置搜索。

| 文件 | 内容 |
|---|---|
| `rtl/应用/baseline.sv` | 当前混合近似基准 |
| `rtl/应用/compensated.sv` | 对指定加法器进行精确替换 |
| `rtl/应用/processed.sv` | 精确替换，加上区域和执行位置控制的固定位翻转 |
| `rtl/common/evoapprox_units.sv` | 从原 C 布尔表达式生成的九个近似单元 |
| `rtl/common/roi_position.sv` | 坐标计数、尺寸锁存和矩形边界映射 |
| `config/selection.json` | 本次使用的基准、翻转和精确替换配置 |
| `manifest.json` | 12 个顶层模块、源文件列表、哈希、接口及测量范围 |

三个版本使用相同数据通路架构、接口及延迟。精确替换是静态替换，节点与位编号是编译常量，不保留运行时任意节点/位选择器或双套精确/近似单元。

## 当前综合范围

Sharpen、Conv、Sobel 包含算术核心、输出寄存器、帧坐标控制及新增逻辑。输入邻域窗口由外部提供，未包含行缓冲和窗口生成器。

DCT 包含完整二维前向变换：一个 32 组加法器的一维核心被行、列阶段复用 16 次，并包含中间数据寄存器、调度及坐标控制。循环中的 512 次加法执行没有生成成 512 个物理加法器。每个版本使用相同的存储与调度结构。

DCT 逆变换仍用于软件重建评价，不在本轮 RTL 内。外部窗口生成、边缘补齐、整帧存储和主机接口均不计入。后续面积结果应标明这些边界，不能称为完整图像处理系统面积。

## 重新生成

在工程根目录、已有 Python 环境下执行：

```bash
python -m pip install -r circuit_processing/requirements.txt
python circuit_processing/hardware/scripts/generate_rtl.py
```

默认根据四个现有混合基准和已确定的 85% 矩形 `[120,135,193,201)` 重新分析、生成。也可以使用已冻结的 schema 3 配置：

```bash
python circuit_processing/hardware/scripts/generate_rtl.py \
  --frozen circuit_processing/results/某次运行/frozen.json
```

第一版硬件后端要求这四个当前 C 内核及相应基准，检查冻结图与源码一致，并重新核对选择结果。修改混合基准后应先重新冻结或使用默认生成。像素端备选、其他 C 内核及分数运算暂不由此后端接收；不会自动将双精度逆变换量化。

## 时钟与帧协议

所有顶层均使用 `clk`、低有效异步复位 `rst_n`，以及 `frame_start`、`in_valid`、`in_ready`、`out_valid`。

- `frame_start` 在时钟上升沿锁存 16 位 `frame_width`、`frame_height`，清除先前在途处理并重置坐标。该周期不接收输入。
- 支持尺寸为 1～65535；零尺寸使帧不活跃。尺寸在一帧期间以锁存值为准。
- 仅在 `in_valid && in_ready` 的上升沿接受输入并推进坐标。输入可以间断。
- 处理完一帧所需的输入数量后，`in_ready` 保持低，直到新的 `frame_start`。
- 输出没有反压接口，使用方必须接收 `out_valid` 标记的结果。

逐像素模块每个接收沿登记一次 `pixel[7:0]` 和 `out_valid`，可连续接收。`samples` 按 C 接口输入顺序打包：第 i 个像素放在 `[8*i +: 8]`。Sharpen 顺序为中心、上、下、左、右；Conv/Sobel 为 3×3 行优先。边界像素按软件规则保留原值或输出零。

DCT 的 `samples[511:0]` 是一个 8×8 块，像素按行优先排列，每个像素 8 位。每帧输入块数为 `ceil(width/8)*ceil(height/8)`，块按行优先提供；右、下边缘不足部分由外部按复制边界补齐。接收后运行 16 个计算周期，最后一列结束时产生 `out_valid`。忙碌期间 `in_ready` 为低。输出 `coefficients[2047:0]` 包含 64 个有符号 32 位前向系数，第 i 个位于 `[32*i +: 32]`，按行优先排列。

矩形坐标按像素中心映射：`edge(c,n)=(2*c*n+319)/640`，整数除法。逐像素翻转仅在矩形和有效内核内部启用。DCT 要求完整 8×8 输出块位于矩形内，同时匹配选定的行/列执行阶段。

## 功能检查

需要 GCC、Icarus Verilog (`iverilog`/`vvp`)；不需要 DC 许可证。以下命令只进行生成和仿真，不进行综合：

```bash
python -m unittest discover -s circuit_processing/hardware/tests -p 'test_*.py' -v
```

逐像素软件对照测试需要已编译的验证程序，默认读取 `circuit_processing/build/fixed_roi_verify`，也可以明确指定程序：

```bash
FIXED_ROI_VERIFY="$PWD/circuit_processing/build/fixed_roi_verify" \
python -m unittest discover -s circuit_processing/hardware/tests -p 'test_*.py' -v
```

近似单元与原 C 函数逐位对照，逐像素内核与软件运算图对照，DCT 前向系数与 C 参考对照。仿真包括随机/常量输入、边界、输入间断和帧重新开始。测试通过表示已覆盖输入的功能一致性，不等价于任意输入的形式等价证明。

本次交付不包含 DC 面积结果。综合时应对 `manifest.json` 中每个顶层读取共同库及其版本文件，在相同库和时序约束下分别测量。近似单元文件保留原库注释，其中的面积、功耗数值不作为本次综合结果。
