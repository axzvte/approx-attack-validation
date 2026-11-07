# 第一部分：确定固定重要区域

当前流程为：读取每张图的 U²-Net 二值掩膜 → 计算 RC 响应并逐图 Otsu 二值化 → 逐图取并集 → 按像素中心拉伸映射到 320×320 → 累计出现频率 → 导出高频掩膜和外接矩形。

## 运行

从工程根目录执行，已有 RC 结果时可以跳过这部分。

```bash
source ~/projects/u2net_roi/.venv/bin/activate
python -m pip install -r roi_selection/requirements.txt
python roi_selection/union_frequency_rectangles.py \
  --data "$HOME/datasets/PASCAL-S" --count 850 --grid 320 \
  --thresholds 80 81 82 83 84 85 86 87 88 89 90 \
  --output "roi_selection/results/union_rc_$(date +%Y%m%d_%H%M%S)"
```

输入目录下的图片为 `datasets/imgs/pascal/1.jpg` 等，掩膜为 `pred_masks/1_mask.png` 等。输出目录必须是新目录。唯一特征分支为 RC，`--feature rc` 可省略。未指定比例时默认 80%—90%，仍可用 `--thresholds` 指定其他整数比例。

若缺少 U²-Net 掩膜，安装 `requirements-u2net.txt` 后用 `batch_infer_u2net.py --data ... --repo /实际的/U-2-Net目录` 生成。该目录需包含原模型源码和 `saved_models/u2net/u2net.pth`。默认保护已有掩膜。

## 看哪些结果

| 输出 | 用途 |
|---|---|
| `union_overview.png` | 所有比例的矩形对比 |
| `thresholds/union/85_region_mask.png` | 85% 高频点掩膜 |
| `thresholds/union/85_rectangle_mask.png` | 对应外接矩形掩膜 |
| `rectangles.csv`、`rectangles.json` | 坐标、面积及覆盖统计 |
| `protocol.json` | 方法、参数、输入范围、代码指纹及完成状态 |
| `frequencies/`、`per_image_masks/`、`rc_responses/` | 可核查的频率、逐图掩膜和原始 RC 响应 |

把整个结果目录传给第二部分的 `--roi-run` 即可。频率 85% 表示该坐标在至少 85% 的图片联合掩膜中出现，不表示矩形覆盖了每张图片 85% 的特征。

`color_saliency.py` 为 RC 实现，`mask_utils.py` 仅包含当前流程共用函数，`infer_u2net.py` 和 `batch_infer_u2net.py` 为预训练模型推理。计算约定见 [METHOD.md](METHOD.md)。

```bash
PYTHONPATH=roi_selection python -m unittest discover -s roi_selection/tests -v
```
