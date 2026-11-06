# 固定区域统计与电路验证

本工程按三个步骤运行。先用 U²-Net 与 RC 统计固定矩形，再从算术误差推导并冻结四种电路的配置，最后用图片验证区域失真和全图 PSNR。已有的 RC 统计结果可直接用于第二步。

[WSL 安装和运行命令](docs/FIXED_ROI_PIPELINE.md)

| 目录 | 内容 |
|---|---|
| `roi/` | 区域统计、U²-Net 掩膜导出和统计测试 |
| `scripts/` | 配置冻结和逐图验证入口 |
| `configs/` | 误差模型所对应的源码指纹 |
| `app/`、`src/`、`include/` | C++ 验证程序、四种算术核心及共用接口 |
| `third_party/evoapproxlib/` | 可替换的近似加法器源码 |
| `hardware/` | 后续 DC 综合可参考的基准 RTL 和来源记录 |
| `tests/` | C++ 位翻转回归测试 |

配置以 `frozen.json` 保存。验证生成 `images.csv`、`summary.json` 和少量输出示例，分别记录全图达标、区域 PSNR 下降以及两项同时满足的结果。

历史区间搜索、十图训练、旧综合脚本和实验归档可在 [原整合分支](https://github.com/axzvte/approx-attack-validation/tree/feature/fixed-roi-pipeline) 查阅。当前流程与后续 DC 综合的范围见[运行说明](docs/FIXED_ROI_PIPELINE.md)。
