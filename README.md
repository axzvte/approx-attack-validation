# 统一电路编码、配置分析与图片验证

工程包含 `roi_selection/` 和 `circuit_processing/` 两部分。第一部分用 U²-Net 与 RC 统计固定矩形，第二部分将 C 算术内核编码为统一结构，分析处理配置，再验证图片。

## 当前运行流程

在 WSL 中进入工程并更新依赖及编译程序。

```bash
cd ~/projects/approx_attack_validation
source ~/projects/u2net_roi/.venv/bin/activate
python -m pip install -r circuit_processing/requirements.txt
cmake -S circuit_processing -B circuit_processing/build -DCMAKE_BUILD_TYPE=Release
cmake --build circuit_processing/build -j 4
```

四个应用使用同一个结构编码器、误差传播器和配置选择器。

```bash
bash circuit_processing/scripts/run_all_applications.sh
```

默认各验证 850 张图片。基准仍由 `circuit_processing/configs/circuits/*_mixed.json` 提供，其中的 `baseline` 可以逐节点修改。`source` 指向对应 C 内核，节点连接从源码 AST 提取。已有重要区域统计目录仍作为 `--roi-run` 输入。

输出位于 `circuit_processing/results/all_apps_时间戳/`。

- `frozen.json` 保存完整结构、源码指纹、输入基准、处理位置、误差界和选择过程。
- `verify/comparisons/` 保存全部编号三图拼图。
- `verify/psnr_results.xlsx` 为每个配置提供逐图数据与平均值。
- `verify/images.csv`、`verify/summary.json` 保存详细数据和失败记录。

编码接口、支持的 C 语法、结构选择的复杂度与限制，见 `circuit_processing/README.md`。旧 schema 1/2 冻结文件通过独立历史路径复现，新 freeze 统一生成 schema 3。
