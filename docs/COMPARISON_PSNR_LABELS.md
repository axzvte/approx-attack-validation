# 在对比图上显示实际 PSNR

四应用共用的 `circuit_bitflip_fixed` 图片导出会在每列标题栏显示实际数值：

```text
exact                         baseline                       fixed H + intervals
Global PSNR: inf dB            Global PSNR: 34.650 dB          Global PSNR: 30.000 dB
ROI PSNR: inf dB               ROI PSNR: 34.819 dB             ROI PSNR: 23.011 dB
ROI: original mask            ROI: original mask             numeric_eligible
```

- 左、中、右仍为精确输出、原近似基准、固定硬件加本图区间的实际输出。
- 显示绝对 PSNR，单位 dB，保留三位小数；精确输出相对自身为 `inf`，不会用100 dB等有限数值替代。
- Global/ROI PSNR来自生成CSV的同一组完整原图评价。ROI指标使用本图真实二值掩码，不是裁剪外接矩形中的所有像素。
- 整体、ROI、背景对比图均注明同一组完整输出指标。ROI外接矩形的内容仍为最近邻放大2倍，文字独立绘制。
- 只在新标题栏写字；窄图在左右加黑色留白，图像内容不会被文字覆盖。原始攻击图和差异图的像素保持原导出行为。
- 未达标输出会显示 `quality_not_met`，不写成“成功”。数值达标输出仍需人工查看视觉效果。

## 给已经完成的18图结果补标注（不需要重新运行实验）

在VS Code的WSL终端，拉取本分支后运行：

```bash
cd "$HOME/projects/approx_attack_validation"
PY="$HOME/projects/u2net_roi/.venv/bin/python"
SOURCE="results/bitflip/u2net_stage6_four_apps_20261028-103543"
OUT="results/bitflip/u2net_stage6_psnr_$(date +%Y%m%d-%H%M%S)"
"$PY" scripts/annotate_bitflip_comparisons.py "$SOURCE" "$OUT"
```

该命令只读取已经完成的stage6报告和已有对比PNG，不加载U²-Net、不运行训练、不启动原生程序，也不重新搜索区间。它保留原图内容像素，另存144张带标注的整体/ROI对比图。图片平铺在新的OUT目录中，继续使用 `sharpen_01_overall.png`、`sharpen_01_roi.png` 等名称，不建立逐图文件夹。目录中另有一个 `annotation_manifest.json`，记录实际显示的文字、原始图片哈希和内容位置。

使用 `--check` 可只校验来源，不创建输出。源目录、已有输出目录和同步sources目录不会被覆盖。该补标工具专门读取此前stage6的旧版24像素整体标题和48像素ROI标题；已使用新版原生导出生成的图片本身就带指标，无需再次补标。若整个运行未完成、任务失败、来源图片哈希不符或布局不匹配，会拒绝导出；无合法区间且没有图片的任务单独记录为skipped。

## 后续新实验的原生导出

由用户手动重新编译图片导出程序：

```bash
cmake -S . -B build-bitflip -DCMAKE_BUILD_TYPE=Release
cmake --build build-bitflip --target circuit_bitflip_fixed -j4
```

之后运行实验时，必须把 `--fixed-exe` 指向刚编译的 `build-bitflip/circuit_bitflip_fixed`。旧实验中绝对路径指向的旧build目录不会自动更新。`comparisons` 模式仍然只导出整体、ROI两张对比图；compact/full等模式的文件数量保持原行为。

新版原生标题高度为84像素，单列至少240像素宽，窄图可左右留白；不能再用旧的24/48像素常量截取图片内容。统计CSV、硬件、种子、ROI、搜索控制和成功标准不变。

## 仅图片标注的单元测试

```bash
cmake --build build-bitflip --target test_comparison_psnr -j4
ctest --test-dir build-bitflip -R '^(comparison_psnr|annotate_bitflip_comparisons)$' --output-on-failure
```

这两项只使用临时像素样例，不读取实验数据、不调用搜索或模型。训练/搜索相关的集成测试和实际实验由用户自行启动。
