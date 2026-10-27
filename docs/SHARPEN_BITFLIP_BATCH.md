# 十张图片批量联合搜索

保持当前多节点联合搜索方法和参数，依次处理第1至10张图。默认全局PSNR下限仍为30 dB，不增加背景指标的硬约束。每张图独立搜索；此轮用于观察不同图片的效果，不代表同一组固定硬件配置的跨图验证。

编写方完成静态自查和独立静态审查；没有编译、执行测试或运行实验。以下命令由用户在 Linux/WSL 终端执行。

## 更新和运行

在仓库根目录，需要 C++17、CMake、OpenCV 开发库、Python 3，无额外Python包。

```bash
git fetch origin
git switch feature/sharpen-bitflip
git pull --ff-only origin feature/sharpen-bitflip

python3 scripts/run_sharpen_bitflip_batch.py \
  data results/bitflip/joint_10_images_20261024
```

输出目录必须不存在，重复运行请使用新名称。程序先编译一次 `sharpen_bitflip_joint`，再顺序处理10张图。默认参数与上一轮一致：beam=4、refinement=1、boundary-budget=24、top-per-count=2、restarts=2、global-psnr=30。控制台显示当前图片和搜索进度，各图日志独立保存。

某张图失败时会记录原因并继续其余图片，最终返回非零退出码。完成但没有数值候选的图片记为 `no_numeric_candidate`，不作为程序运行失败，也不会伪造攻击指标或图片。

## 默认只保留重要图片

每张图从2/3/4个实际触发节点的候选中选择一个，排序依次为ROI MSE更高、全局MSE更低、实际节点更少。只保存三张PNG：

- `*_output.png`：最终输出，原尺寸。
- `*_roi_comparison.png`：重要区域对比，最近邻放大2倍。
- `*_background_comparison.png`：ROI统一遮灰后的背景对比。

对比图从左到右为精确结果、近似基准、联合输出。10张图最多产生30张PNG。数值最优不自动代表视觉最佳，仍需检查ROI是否有明显问题及背景是否正常。所有保留候选的指标、配置、逐节点消融和空间统计不因精简图片而删除。

默认是 `--image-mode compact`。需要检查其他候选及消融图时，在新输出目录运行并添加 `--image-mode full`；仅需要数据时用 `--image-mode none`。三种模式不改变搜索和评分。

## 输出在哪里

```text
joint_10_images_20261024/
  best_per_image.csv       每张图一行，含所选候选指标及三张图片路径
  summary_all.csv          所有图片、所有保留候选的指标
  configurations_all.csv   所有候选的节点配置和触发次数
  ablations_all.csv        所有候选的逐节点关闭诊断
  search_stats_all.csv     各图搜索统计
  images_all.csv           所有实际导出图片的路径索引
  run_status.csv           逐图完成、失败、候选数及日志路径
  RUN.json                整体参数、代码版本和完成状态
  build.log               构建日志
  logs/image_01.log       第1张图运行日志（其他图片同理）
  image_01/               第1张图的CSV、RUN.txt和三张图片
  ...
  image_10/
```

汇总表中都有 `image` 列，图片路径相对于批量输出目录。例如 `image_01/n3_rank1_roi_comparison.png`。每张图自己的 `tiles.csv`、`images.csv` 和原始报告仍在对应 `image_XX/` 中。失败图片在汇总表中显示 `run_failed`，指标留空，详细错误见 `run_status.csv` 和日志。

汇总表每处理完一张图更新一次。`RUN.json` 的 `completed=true` 表示已尝试全部请求图片，`all_succeeded=true` 才表示全部运行完成且报告完整；`failed_images` 列出失败编号。手动中断或构建失败时 `completed` 保持false，已写出的结果保留。

## 可选设置

```bash
# 只跑第2至4张图
python3 scripts/run_sharpen_bitflip_batch.py data results/bitflip/joint_images_02_04 \
  --first 2 --last 4

# 已编译当前版本时，跳过构建
python3 scripts/run_sharpen_bitflip_batch.py data results/bitflip/joint_10_images_second \
  --skip-build
```

支持 `--beam`、`--refinement`、`--boundary-budget`、`--top-per-count`、`--restarts`、`--global-psnr` 及 `--build-dir`、`--jobs`。首轮建议保持默认参数，便于与上一轮比较。跳过构建前需确认可执行程序已更新，否则旧程序无法识别图片模式选项。

用户侧完整检查命令见 [联合搜索说明](SHARPEN_BITFLIP_JOINT.md#用户侧测试)。本次新增测试已编写但未执行。跑完后请提供 `best_per_image.csv`、`summary_all.csv`，以及有代表性的ROI和背景对比图；若出现失败，再附 `run_status.csv` 和对应日志。
