# 第一阶段：逐图 U²-Net ROI → 固定硬件训练 / 单图区间搜索

U²-Net **只在离线生成掩膜时运行**。10 张训练图共同选择硬件 H，H 的节点、监测信号、位宽和翻转位保持固定；新图只搜索区间端点。ROI 用于离线评分、追踪统计和图片对比，不是运行时硬件模块，也不是在线逐像素的翻转使能信号。

本次不改 `src/analysis/` 搜索算法，不替换近似算子或修改有符号位宽。旧 `data/`、掩膜、报告只读。新数据和每次结果均使用新目录；路径已存在就停止。

## 接口

```text
NEW_DATA/
  stage1/input/image_01.jpg ... image_10.jpg
  stage4/input/image_31.jpg ... image_40.jpg
  roi/image_01_mask.png ... image_40_mask.png   # 只需要本次使用的 ID
  roi_manifest.json
```

- 按图片 ID 一对一加载，不使用目录遍历顺序配对；阶段中的平铺图片路径仍兼容。
- 掩膜必须是真正的单通道 PNG，像素只含 0/1 或 0/255；0/1 在内存中统一为 0/255。不接受灰度显著图、JPEG、混用 1 和 255、RGB/16 位掩膜。
- 图片和 mask 尺寸必须完全相同。严格模式不缩放、不自动二值化；空 ROI、全图 ROI 均报错，因为评分需要 ROI 和背景都非空。
- 严格模式按图片文件的原始像素朝向解码，忽略 EXIF 方向标记，与当前 U²-Net 的 Pillow 推理一致。图像浏览器可能会自动旋转显示；不要仅凭浏览器显示朝向判断坐标。外部 mask 也必须使用此原始像素坐标。
- `run_circuit_bitflip_ten_train.py` 和 batch 默认 `--roi-mode per-image`，目录默认 `DATA/roi`；可用 `--roi-dir DIR` 指定外部目录。严格模式缺失文件就报错，不回退到固定 ROI。
- C++ `circuit_bitflip_ten_train` 接受 `--roi-dir DIR`。`circuit_bitflip_fixed` 和 `sharpen_bitflip_fixed` 接受 `--roi-dir DIR` 或 `--roi-mask FILE`，二者互斥。泛用 fixed 还支持 `--input-image FILE` 和明确配对的 `--roi-mask FILE`。
- 未传 ROI 参数的历史 C++ 调用保留旧行为；只用于历史复现。新流程请始终使用下面命令。历史 Python 复现须明确加 `--roi-mode legacy-fixed`。

## 在 VS Code 的 WSL 终端准备数据

以下是**供用户运行的命令**。作者实际测试使用生成的小图，没有读取、运行或验证你的 WSL 实验图片、U²-Net 权重及预测。先调整 `OLD_DATA` 和 `WEIGHTS`；若已有新分支 checkout，可改 `APPROX` / `U2` 指向该目录。

```bash
set -euo pipefail
RUN_ROOT="$HOME/work/u2net-stage1-$(date +%Y%m%d-%H%M%S)"
mkdir -m 700 "$RUN_ROOT"
git clone --branch feature/u2net-roi-stage1 https://github.com/axzvte/approx-attack-validation.git "$RUN_ROOT/approx"
git clone --branch feature/u2net-roi-stage1 --recurse-submodules https://github.com/axzvte/u2net_roi.git "$RUN_ROOT/u2net"
export APPROX="$RUN_ROOT/approx" U2="$RUN_ROOT/u2net"
export OLD_DATA="/替换为现有approx工程的data绝对路径"
export WEIGHTS="/替换为已有u2net.pth绝对路径"
export PRED="$RUN_ROOT/u2net_predictions" DATA="$RUN_ROOT/paired_data"
python3 -m venv "$RUN_ROOT/venv"
source "$RUN_ROOT/venv/bin/activate"
python -m pip install -r "$U2/requirements.txt" -r "$APPROX/requirements-roi.txt"
python -m pip install torch   # 根据本机 CUDA 情况选择匹配的 PyTorch 安装方式；已有兼容环境可复用
test -f "$WEIGHTS"
mkdir "$PRED"
```

对工程**实际使用的同一张图**运行 U²-Net；不要把 PASCAL-S 编号直接当作 `image_NN`，除非已确认映射。下面只准备训练 1–10 和冻结后评估 31–40，省略未参与该流程的 11–30：

```bash
python - <<'PY'
import csv, os, subprocess, sys
from pathlib import Path
u2, old, pred = (Path(os.environ[k]) for k in ('U2','OLD_DATA','PRED'))
rows=[]
for i in [*range(1,11), *range(31,41)]:
    stage=old/f'stage{(i-1)//10+1}'
    paths=[stage/'input'/f'image_{i:02d}.jpg', stage/f'image_{i:02d}.jpg']
    image=next((p for p in paths if p.is_file()), None)
    if image is None: raise FileNotFoundError(paths)
    out=pred/f'image_{i:02d}'
    if out.exists(): raise FileExistsError(out)
    subprocess.run([sys.executable,str(u2/'infer_u2net.py'),'--image',str(image),
                    '--repo',str(u2/'U-2-Net'),'--weights',os.environ['WEIGHTS'],
                    '--output',str(out),'--threshold','0.5','--device','auto'],check=True)
    rows.append((i,str(image.resolve()),str((out/f'image_{i:02d}_mask.png').resolve())))
with (pred/'pairs.csv').open('x',newline='') as f:
    w=csv.writer(f);w.writerow(['image_id','image','mask']);w.writerows(rows)
PY
python "$U2/export_attack_dataset.py" --manifest "$PRED/pairs.csv" --output "$DATA"
python "$APPROX/scripts/image_roi.py" --data "$DATA" --images 1:10 --report "$RUN_ROOT/train_pairs.json"
python "$APPROX/scripts/image_roi.py" --data "$DATA" --images 31:40 --report "$RUN_ROOT/test_pairs.json"
```

已有 U²-Net mask 时，跳过推理，自行创建 CSV `image_id,image,mask`，路径可为绝对路径或相对 CSV 所在目录的路径，再运行 exporter。导出器先校验全部配对、唯一 ID/源图/源掩膜及输出路径；复制原图字节，二值 mask 无损写为 PNG，记录源路径、尺寸、面积比例与 SHA-256。它不修改源图片或已有预测。不合格 mask 应回到 U²-Net 导出步骤修正，不能静默改阈值或缩放。

阈值 0.5 是当前推理默认值，并未由本次工作证明最优；需事先按训练集确定并冻结，不能根据测试集质量挑阈值。U²-Net 的 320×320 是模型内部输入尺寸，导出 mask 应已恢复到其源图尺寸。

## 构建、测试和 10 图训练（Sharpening）

需要 CMake、C++17、OpenCV 开发库；下面只写新构建目录。

```bash
BUILD="$RUN_ROOT/build"
cmake -S "$APPROX" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DPython3_EXECUTABLE="$(command -v python)"
cmake --build "$BUILD" -j4
ctest --test-dir "$BUILD" --output-on-failure -j4

# 可选：跨仓库合成小图完整链路，不调用 U²-Net、不读取实验数据
python "$APPROX/tests/test_u2net_roi_end_to_end.py" --u2net-dir "$U2" \
  --build-dir "$BUILD" --output "$RUN_ROOT/synthetic_smoke"

export TRAIN_OUT="$RUN_ROOT/sharpen_ten_train"
python "$APPROX/scripts/run_circuit_bitflip_ten_train.py" "$DATA" "$TRAIN_OUT" \
  --application sharpen --roi-dir "$DATA/roi" --skip-build \
  --train-exe "$BUILD/circuit_bitflip_ten_train" --fixed-exe "$BUILD/circuit_bitflip_fixed"
```

保持原有 1–10 训练、31–40 评估协议，保留原搜索预算默认值。训练完成后冻结 H 和训练种子库，再逐张读取、验证和哈希测试图及其 mask。没有合格 H 时明确报告并不启动测试。坏的测试配对作为失败样本保留，不据此重选硬件。`--resume-from OLD_RUN` 只读复用一致任务，输出仍须使用新目录。

## 单张新图：冻结 H，只搜索区间

如果训练没有选出合格 H，先检查训练报告，不应使用其他候选伪装成冻结结果。

```bash
test -f "$TRAIN_OUT/frozen_hardware.csv"
"$BUILD/circuit_bitflip_fixed" sharpen "$DATA" "$RUN_ROOT/sharpen_image31" \
  --hardware "$TRAIN_OUT/frozen_hardware.csv" --seeds "$TRAIN_OUT/frozen_seeds.csv" \
  --image 31 --roi-dir "$DATA/roi" --continuous --image-mode full
```

任意路径新图可使用 `--input-image /path/new.jpg --roi-mask /path/new_mask.png --image 41` 替换上面的 `--image 31 --roi-dir ...`。图像 ID 在显式输入模式下仅作诊断标识；掩膜必须与新图严格对齐。`sharpen_bitflip_fixed` 旧专用入口也支持逐图 ROI，但十图流程统一使用泛用 fixed 以保留连续搜索协议。

## 证据与边界

- 训练 `train/roi_inputs.csv` 记录原生入口逐图实际加载路径、尺寸及 ROI 像素数；原生 `RUN.txt` 记录 ROI 模式与路径。
- 顶层 `RUN.json` / `frozen_protocol.json` 记录逐图训练 mask 哈希与离线属性。测试 mask 哈希在 H 冻结后加入 `RUN.json`；每个 `TASK.json` 的身份包括该图自己的 mask 哈希。
- 改训练 mask 会使训练缓存失效；只改某张测试 mask 不改变 H，且只使该图测试任务失效。运行中改变输入则报错，不应继续作为一致任务复用。
- 将 ROI 目录搬迁到新路径会重新运行对应任务，不复用带旧路径的报告。
- mask 内容是否对应正确语义无法仅靠尺寸和二值检查证明；需检查 U²-Net overlay、源图映射及 manifest，不能把文件命名校验当成分割准确率证明。
- 第一阶段接通十图训练、batch 子流程和两个 fixed 入口。旧 discovery、completion、frozen-retest 等独立脚本仍使用历史 ROI，不要将其报告混入本流程；后续需单独适配。
- 新 ROI 会改变质量指标，历史固定 ROI 的排名/统计不能直接合并。成功仍需人工查看 ROI 与背景图；本次没有证明真实数据性能、视觉效果、泛化能力或综合面积收益。
