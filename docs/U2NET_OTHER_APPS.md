# Conv / Sobel / DCT：复用逐图 U²-Net ROI

本轮复用已经完成的 Sharpening 实验中的 staged 图片和离线 ROI，输入只读，不重新选择阈值或运行 U²-Net。三个应用各自在图片1–10上选择自己的固定硬件 H，在各自 H 和训练种子库冻结后，使用图片31–40及各自的 ROI 只调整区间。不能直接把 Sharpening 的硬件复制给其他应用。

保持原应用的近似算子、图结构、位宽和搜索算法。每个应用均使用原默认预算：最多80次硬件/图像评估、训练 context64、测试 context128、workers4、全局 PSNR 门槛30。上限不意味着一定用满预算。三个应用依次运行，避免 DCT 的追踪数据与其他应用争用内存。

默认 batch 现已包括 `sharpen,conv,sobel,dct`。已完成 Sharpening 时，明确选择 `conv,sobel,dct` 可避免重复运行。原历史三应用复现可明确传入 `--applications sharpen,conv,dct --roi-mode legacy-fixed`。

## 后台一键运行

在 VS Code WSL 终端，用具有 Pillow 的现有 Python 环境执行：

```bash
PY="$HOME/projects/u2net_roi/.venv/bin/python"
SOURCE_RUN="$HOME/work/u2net-stage1-20261027-230630-1057483/sharpen_ten_train"
BUILD="$HOME/work/u2net-stage1-20261027-230630-1057483/build"
OUT="$HOME/projects/approx_attack_validation/results/bitflip/u2net_other_apps_$(date +%Y%m%d-%H%M%S)"
"$PY" scripts/run_u2net_other_apps.py "$SOURCE_RUN" "$OUT" "$BUILD"
```

`SOURCE_RUN` 必须包含已完成逐图 ROI 实验的 `RUN.json`；`BUILD` 必须有本分支或 stage1 对应版本的 `circuit_bitflip_ten_train` 和 `circuit_bitflip_fixed`。如重新编译：

```bash
cmake -S . -B build-u2net -DCMAKE_BUILD_TYPE=Release -DPython3_EXECUTABLE="$PY"
cmake --build build-u2net -j4
ctest --test-dir build-u2net --output-on-failure -j4
```

启动器 `--check` 只检查路径/来源报告，不创建输出、不读取测试像素；`--foreground` 等待任务结束，默认脱离终端后台运行。输出必须是新目录，拒绝覆盖数据、ROI、构建目录、历史报告和 sources。启动器记录来源报告 SHA-256，源路径以 LAUNCH.json 为准。电脑需保持开机、不休眠，不要终止 WSL。

## 结果

- `STATUS.txt`、`run.log`、`SUMMARY.json`：整体状态、日志和逐应用总结。没有合格 H 是保留的实验结论，不伪造测试结果；运行失败会明确标为 FAILED。
- `results/applications.csv`：三应用的时间、实际评估数、候选完成度和测试 PSNR 平均值。
- `results/conv/`、`results/sobel/`、`results/dct/`：各自独立的 frozen_hardware.csv、frozen_seeds.csv、RUN.json 和逐图报告。
- `results/images_all.csv`：已经加应用前缀的全图、ROI、背景与差异图路径；对应 `results/应用名/test/硬件ID/image_NN/`。
- `results/roi_inputs_all.csv`：原生训练入口逐图使用的 ROI 路径、尺寸与面积，并保留应用标识。

全图图像 `selected_comparison.png` 从左到右为精确、近似基准、固定 H 加区间输出。ROI 对比是外接矩形裁剪；背景对比将 ROI 填灰。diff_x4 仅辅助定位差异，实际输出看 selected_output.png。所有应用均须单独审查 ROI 破坏和背景视觉；数值合格不等于视觉成功。

## 验证范围

新增测试分别覆盖 Conv/Sobel/DCT 的真实原生训练和搜索、逐图不同 mask、缺失 ROI 拒绝、完整图像导出、外部 ROI 目录、应用隔离、后台入口路径保护和失败状态。合成测试的低预算/阈值0仅验证接口，正式实验仍用阈值30和上述默认预算。DCT 仍按原图像块图计算，ROI 不构成运行时硬件模块。
