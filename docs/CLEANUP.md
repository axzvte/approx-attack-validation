# 2026-10-23 工程整理记录

## 范围

基于完整备份分支 `backup/before-cleanup-20261023`，提交 `581b11e2a596979d5b55b64c0c703b8a4ba082c5`。整理版位于 `cleanup/project-files`，完整备份分支保持原样。

本次只整理已有工程，没有实现位翻转方法，没有更改应用算法、当前近似基准、ROI、搜索参数或计算单元内部结构。

## 文件处理

- 从整理分支移除 763 个编译产物、167 个 Python 虚拟环境文件，以及下列 17 个未参与当前 CMake 构建的旧代码文件；均可从完整备份分支恢复。
- 将 1044 个历史文件移入 `archive/`，内容与备份 Git blob 完全一致；包括实验目录、根目录输出和整理前清单。归档保留日志与 CSV。
- `data/` 下 45 个文件的路径和 Git blob 均保持不变。
- 添加工程入口 README、归档说明、忽略规则及 Bash 脚本行尾约定。整理前 2169 个文件，整理后 1226 个受 Git 管理的文件。

## 旧代码移除名单

```text
src/analysis/intermediate_analysis.cpp
src/analysis/interval_attack_search.cpp
src/analysis/sobel_intermediate_analysis.cpp
src/analysis/trigger_search.cpp
src/processing/approx_sharpen.cpp
src/processing/exact_sharpen.cpp
src/processing/exact_sobel.cpp
src/processing/triggered_sharpen.cpp
include/analysis/intermediate_analysis.hpp
include/analysis/interval_attack_search.hpp
include/analysis/sobel_intermediate_analysis.hpp
include/analysis/trigger_search.hpp
include/processing/approx_sharpen.hpp
include/processing/exact_sharpen.hpp
include/processing/exact_sobel.hpp
include/processing/sharpen_approx_config.hpp
include/processing/triggered_sharpen.hpp
```

## 路径兼容

`scripts/rerank_force_active_stage3.sh` 自动查找时兼顾工程根目录与 `archive/experiments/`，显式目录参数仍然有效。新实验继续按原运行脚本生成目录，Git 忽略新输出；已有归档通过例外规则完整保留。

其他历史工具读取旧实验时，应显式指定归档目录。原先根目录的结果现在位于 `archive/experiments/root_outputs/`。没有将 CSV 一律作为可删的临时文件。

## 验证与旧测试修复

在 WSL Ubuntu-24.04、GCC 13.3.0、OpenCV 4.6.0 下：

- 全部 6 个 Bash 脚本通过 `bash -n`。
- `cmake -S . -B build` 配置成功。
- `cmake --build build -j2` 完整构建成功。
- `ctest --test-dir build --output-on-failure`：22/22 通过。
- 自动查找能够定位已归档的 `force_active_pipeline_20261014_221356`。
- 将四个应用的各 10 份 Stage1 CSV 复制到忽略的构建目录后，`hardware_config_statistics` 对四组数据均返回成功，没有改写归档。
- 所有移动文件与备份的 blob 对照一致，原始数据和仍使用的应用源码没有内容变化。

完整构建暴露原有 `tests/test_search_space_statistics.cpp` 中的 `FakeApplication` 缺少 `collectConfiguredAddSamples` 实现。该问题在未修改的备份文件中已经存在。本次仅补齐测试替身接口，沿用它原有的固定样本行为；没有改动生产代码或删除失败测试。

本次没有重新执行完整的逐图搜索或硬件综合，不能将测试通过解释为新的图像质量或面积实验结果。

## 在原工程加载

```bash
cd ~/projects/approx_attack_validation
git fetch origin
git switch --track origin/cleanup/project-files
code .
```

加载后按工程 README 重新生成 `build/`。完整备份可随时从 `backup/before-cleanup-20261023` 分支查看。
