# 第二部分：电路分析、修改与验证

入口为 `scripts/fixed_roi_pipeline.py`，包含 `freeze` 和 `verify` 两步。完整可复制命令见[根目录说明](../README.md)。从根目录编译时使用 `cmake -S circuit_processing -B circuit_processing/build`。

## 两步的作用

`freeze` 读取第一部分的矩形统计，根据已审核的算术模型、近似加法器误差界和全图 MSE 预算，确定精确替换节点、翻转节点和位。它不读取验证图片。`--threshold 85` 选择统计中的 85% 矩形，`--min-bit` 默认 4。

`verify` 固定使用上述配置，默认验证 sharpen、conv、sobel、dct 四种应用。`--count 10` 用于先检查流程，后续可在新输出目录运行 `--count 850`。每次输出目录必须不存在，程序不会覆盖历史验证。

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

当前算术规则和实现范围见 [METHOD.md](METHOD.md)。已有配置与源码指纹绑定，单纯移动整个模块不改变指纹；建议为每次独立运行生成配置和结果目录。扩展新 C/C++ 算法需要审核其运算关系，本工程尚不支持任意程序自动推导。DC 面积和硬件计数器时序留待后续。
