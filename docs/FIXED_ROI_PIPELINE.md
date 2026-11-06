# 固定区域统计与电路验证

在 WSL 中进入 `approx-attack-validation` 的 `feature/fixed-roi-pipeline` 分支。需要 Python 3.10+、CMake、C++17 编译器和 OpenCV 开发库。区域统计复用已有的 U²-Net 二值掩膜。

```bash
git fetch origin
git switch feature/fixed-roi-pipeline
git pull --ff-only origin feature/fixed-roi-pipeline

python3 -m venv .venv
source .venv/bin/activate
python -m pip install -r roi/requirements-color.txt
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target fixed_roi_verify -j 4
```

数据根目录沿用 `datasets/imgs/pascal/<id>.jpg` 和 `pred_masks/<id>_mask.png`。已经生成过 RC 并集统计时，可以直接把已有的完整结果目录传给第二步。该目录需要包含 `protocol.json` 和 `rectangles.json`。

```bash
DATA="$HOME/datasets/PASCAL-S"
RUN="results/fixed_roi_$(date +%Y%m%d_%H%M%S)"

# 1. 统计 850 张图，输出候选矩形。可按需要修改阈值列表。
python roi/union_frequency_rectangles.py --data "$DATA" --feature rc \
  --count 850 --grid 320 --thresholds 80 81 82 83 84 85 86 87 88 89 90 \
  --output "$RUN/roi"

# 2. 从统计结果选择 85% 的矩形，并在读取验证图像前冻结四套配置。
python scripts/fixed_roi_pipeline.py freeze --roi-run "$RUN/roi" \
  --threshold 85 --output "$RUN/frozen.json"

# 3. 先跑前 10 张检查输入和输出。完整验证时可改 --count 850。
python scripts/fixed_roi_pipeline.py verify --config "$RUN/frozen.json" \
  --images "$DATA/datasets/imgs/pascal" --count 10 --output "$RUN/verify"
```

若要生成缺失掩膜，可在安装 PyTorch 后运行 `python roi/batch_infer_u2net.py --data "$DATA" --repo /path/to/U-2-Net`。此参数指向已有 U²-Net 源码及权重。保留现有掩膜时省略 `--overwrite-masks`。

配置选择依据如下。

1. 统计阶段在每张图上合并 U²-Net 和 RC 掩膜，累计像素频率，再取选定频率的高频点外接矩形。验证阶段将同一个半开矩形按像素中心映射到每张图的原分辨率。
2. 对逐像素算术核心，模型记录源代码里的加减连接和绝对值。5RP 的单元误差绝对值上界为 4。传播时检查位宽，并计算保留的近似误差与翻转共同形成的输出误差界。先选择满足约束且精确替换节点数最少的组合，再优先选择较高位和距离输出较近的节点。最小候选位默认是第 4 位，只规定考虑的位权，局部 PSNR 是否下降由后续图片验证判定。修改绑定的算术源码后需要复核模型。
3. DCT 采用保守的全精确加法路径。选择最后一遍列变换中输出 DC 系数的末级加法节点 10，只在完全位于矩形内的 8×8 块启用。该系数的变化除以 8 就是块内每个像素的限幅前偏移。实现按逆变换的线性关系单独计算该项，使用偶整数偏移和整数限幅。块外、跨边界的块不受翻转影响。
4. 全图约束为 `ROI面积占比 × 区域误差上界² + 其余面积占比 × 区域外误差上界² ≤ 65.025`。实际图片尺寸映射后再次核对这个上界。内部翻转可能在饱和位置失效。若静态候选不足，配置会明确标注像素端备选，或报无可行配置。

`verify/images.csv` 逐图记录原近似基准、仅补偿、补偿加翻转三种输出相对于精确参考的全图、矩形内和矩形外 PSNR。`summary.json` 汇总全图达标、区域相对原基准下降和同时达标的张数。命令的退出码反映运行错误或全图不达标，区域是否下降以 `joint_pass` 和逐图报告为准。首批示例在 `examples/`。配置文件与源代码版本绑定，验证不重新选节点。验证图与区域统计图重叠时，结果属于同批数据验证。

本阶段验证的是软件中的坐标判定和算术行为。DC 面积综合和硬件计数器的时序实现留到后续。扩展其他算法时需要补充其代码运算关系，当前流程没有从任意 C/C++ 自动提取误差模型。
