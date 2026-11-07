# 固定重要区域与电路处理

当前只保留两个研究部分。所有命令均在本工程根目录执行。

| 文件夹 | 用途 | 运行入口 |
|---|---|---|
| [roi_selection](roi_selection/README.md) | U²-Net 与 RC 逐图取并集，统计固定矩形 | `roi_selection/union_frequency_rectangles.py` |
| [circuit_processing](circuit_processing/README.md) | 根据算术特性固定节点配置，再用图片验证 | `circuit_processing/scripts/fixed_roi_pipeline.py` |

第一部分输出的 `protocol.json` 和 `rectangles.json` 是第二部分的输入。已有的 RC 统计结果可直接复用，不必重新计算 850 张图片。

第二部分支持在 freeze 时使用 `--apps sharpen sobel` 选择内置算法，或使用 `--circuit circuit_processing/configs/circuits/sharpen_mixed.json` 输入逐节点配置。可重复指定 `--circuit` 比较多套配置。新增逐像素算法可通过运算图文件接入，完整说明见 `circuit_processing/README.md`。

四个应用一起完整验证时，激活现有 Python 环境后执行 `bash circuit_processing/scripts/run_all_applications.sh`。该入口采用当前混合锐化基准及卷积、Sobel、DCT 的现有默认基准，每个应用验证 850 张图片，统一输出编号三图对比及含四个工作表的 Excel。

## 现在从这里开始

在 WSL 中执行。沿用现有 Python 环境和已有 RC 结果。

```bash
cd ~/projects/approx_attack_validation
source ~/projects/u2net_roi/.venv/bin/activate
cmake -S circuit_processing -B circuit_processing/build -DCMAKE_BUILD_TYPE=Release
cmake --build circuit_processing/build -j 4
ctest --test-dir circuit_processing/build --output-on-failure

ROI_RUN="$HOME/projects/u2net_roi/output/union_rc_850_01"
DATA="$HOME/datasets/PASCAL-S"
RUN="circuit_processing/results/run_$(date +%Y%m%d_%H%M%S)"

python circuit_processing/scripts/fixed_roi_pipeline.py freeze \
  --roi-run "$ROI_RUN" --threshold 85 --output "$RUN/frozen.json"
python circuit_processing/scripts/fixed_roi_pipeline.py verify \
  --config "$RUN/frozen.json" --images "$DATA/datasets/imgs/pascal" \
  --count 10 --output "$RUN/verify"
cat "$RUN/verify/summary.json"
```

先看 `frozen.json`、`verify/summary.json` 和 `verify/images.csv`。验证不使用图片重新挑选节点或位。

## 文件管理

区域统计输出放在 `roi_selection/results/`，电路输出放在 `circuit_processing/results/`，编译产物放在 `circuit_processing/build/`。原始 PASCAL-S 图片、已有掩膜、U²-Net 模型与权重继续放在原位置。

旧训练/搜索方案、PC/FT 对照、旧实验脚本及历史 DC 工程不再放在当前源码树中。旧版本由 Git 历史保存，本地整理前的文件另存于工程外备份。当前 DC 综合尚未开展。

本次电路源码内容与算术规则未改变，配置兼容性由源码指纹检查。建议按上面的命令为本次运行生成新的 `frozen.json`，便于把配置和结果放在同一运行目录。
