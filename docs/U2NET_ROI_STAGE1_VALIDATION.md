# 第一阶段验证记录（2026-10-27）

实现基于 `axzvte/approx-attack-validation` 的 `feature/sharpen-bitflip` 提交 `c84e2682673773eb0135aff9e0f621dae83a40a7`；配套导出基于 `axzvte/u2net_roi` 的 `main` 提交 `31bb06acda1cbd689a7ccb16ac81bc605bf03278`。两个仓库的新工作均发布到独立 `feature/u2net-roi-stage1` 分支。

所有输入是本次生成的合成图。没有访问、执行或验证用户 WSL 数据集、预训练权重或真实 U²-Net 推理；没有综合硬件或测量面积。当前用户明确授权实际测试，因此本次不沿用旧任务中“仅提供用户运行命令”的限制。

## 实际完成

- WSL Ubuntu-24.04，g++ / CMake / OpenCV 4.6.0，Python 3.12。消费者完整 Release 构建成功。
- `ctest --test-dir BUILD --output-on-failure -j4`：**62/62 通过，0 失败**，包含原有算法与协议回归、原生 image-mask 检查、真实十图/fixed-H ROI CLI、缓存/冻结流程以及 EXIF 回归。最终一轮 CTest 耗时 79.16 秒（不是实验性能基准）。
- 生产者原有 `python -m unittest discover -s tests -p 'test_*.py'`：**7/7 通过**；新增 `python -m unittest test_export_attack_dataset.py`：**2/2 通过**（含多个失败输入子案例）。依赖使用独立环境中的 NumPy 1.26.4、h5py 3.12.1、Pillow 13.0.0。
- `python tests/test_u2net_roi_end_to_end.py --u2net-dir U2_REPO --build-dir BUILD --output NEW_DIR`：实际跑通导出 → 原生十图训练 → 冻结 H → 原生十张新图独立区间搜索。20 张 16×16 合成图片、逐图不同的 0/1 源 mask，导出统一为 0/255；训练实际评估 20 次，冻结种子库前后哈希一致，测试任务无失败。
- 上述跨仓库 smoke 为覆盖流程使用 `--global-psnr 0`、小搜索预算、`--image-mode none`。其数值通过**不能当作 PSNR≥30、视觉攻击效果或真实数据成功率证据**；正式运行命令保留原默认阈值 30 和原预算。
- 缺失/尺寸不符/非二值/空/全图 mask、重复配对、已有输出与输入重叠均有拒绝测试。0/1 与 1-bit PNG 有兼容测试。目录搬迁、测试 mask 单独变化、运行中训练/测试 mask 变化均有缓存或失败行为测试。
- JPEG EXIF Orientation=1/2/3/6：严格入口均忽略 EXIF，原生 exact/baseline/mask 输出一致，和当前离线 U²-Net 的原始像素坐标约定一致。

## 独立审查

独立代码审查提出两项 P2：ROI 目录搬迁后旧缓存报告路径不匹配，以及 EXIF 朝向不一致。均先以回归测试复现失败，再修复并通过测试；修复后完整 62 项套件通过。审查未发现其他重要问题。

实施选择：沿用原 staged image ID 与确定的 `image_NN_mask.png` 命名，manifest 负责来源审计；未新增原生 CSV 映射解析器。历史 C++ 不带 ROI 参数的行为保留，新 Python 训练默认逐图模式。

分割准确性、同尺寸语义错配和真实数据性能不由这些测试判定。请检查来源映射/overlay，并按 [运行说明](U2NET_ROI_STAGE1.md) 在用户数据上验证。旧 discovery/completion/frozen-retest 独立脚本仍待单独适配，不应混入新流程报告。
