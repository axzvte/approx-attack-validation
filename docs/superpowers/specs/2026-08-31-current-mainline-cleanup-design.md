# 当前主线代码整理设计

## 目标

将仓库从“多轮课题方案叠加”的状态整理为围绕当前验证主线的最小工程：人工 ROI → 锐化 DFG 中间数据分析 → 关键运算位置筛选 → 触发区间 → 近似加法器 → ROI / 全局 PSNR 验证。

本次整理不实现新的 ROI 自动定位算法，也不实现新的关键运算位置搜索算法；第一阶段只清理已经放弃的代码，并保留后续主线需要的公共能力。

## 当前验证输入

验证阶段只使用：

- 原始灰度图像；
- 人工标记的 ROI mask。

ROI 自动定位暂不属于当前工程主线，后续如重新研究，将作为独立模块重新设计。

## 保留模块

保留以下公共能力：

- `include/io/`、`src/io/`：图像读取与保存；
- `include/region/`、`src/region/`：人工 ROI mask 判断；
- `include/metrics/`、`src/metrics/`：全局 PSNR 与 ROI PSNR；
- `include/approximate/`、`src/approximate/`：EvoApproxLib 适配；
- `third_party/evoapproxlib/`：近似加法器实现；
- `include/processing/exact_sharpen.hpp`、`src/processing/exact_sharpen.cpp`：精确锐化基准。

## 删除模块

### 自动 ROI / 视觉重点旧方案

删除：

- `app/test_roi_search.cpp`
- `app/test_visual_importance.cpp`
- `include/analysis/visual_importance.hpp`
- `src/analysis/visual_importance.cpp`
- `include/analysis/application_response.hpp`
- `src/analysis/application_response.cpp`
- `include/analysis/attack_value.hpp`
- `src/analysis/attack_value.cpp`

### 原始灰度区间旧方案

删除：

- `include/analysis/pixel_distribution.hpp`
- `src/analysis/pixel_distribution.cpp`
- `include/analysis/interval_evaluation.hpp`
- `src/analysis/interval_evaluation.cpp`
- `include/analysis/interval_search.hpp`
- `src/analysis/interval_search.cpp`

### 加权灰度区间旧方案

删除：

- `include/analysis/weighted_distribution.hpp`
- `src/analysis/weighted_distribution.cpp`
- `include/analysis/weighted_interval_search.hpp`
- `src/analysis/weighted_interval_search.cpp`

### 临时探索程序

删除：

- `app/test_z_analysis.cpp`

`app/test_intermediate_dump.cpp` 中与 DFG 中间数据采集有关的思路仍有价值，但当前文件结构不保留。后续会把有效逻辑拆成正式分析模块后再删除该测试程序。

## 需要重构的现有模块

### `app/main.cpp`

当前 `main.cpp` 是固定灰度区间触发实验，不再作为主线。后续重写为当前验证流程入口；本次第一阶段不新增主线功能。

### `processing/approx_sharpen.*`

删除旧的：

- ROI 两配置路径；
- 原始灰度区间触发路径。

保留基础整体近似能力，后续再设计“指定运算位置 + 指定触发区间 + 指定近似单元”的新接口。

### `processing/sharpen_approx_config.*`

删除 `createNormalConfig()`、`createImportantConfig()`、`createNonImportantConfig()` 这类旧的两配置辅助函数。`SharpenApproxConfig` 是否保留到下一阶段再决定，不在第一阶段强行扩展。

## 数据目录

后续统一为：

```text
data/
├── images/
└── masks/
```

避免 `data/input/` 与 `data/masks/` 中出现重复 mask。数据迁移属于结构整理步骤，但不会改变图像或 mask 内容。

## 第一阶段完成标准

第一阶段只做“删除废弃代码 + 修正 CMake 依赖”，完成后应满足：

1. 仓库不再包含自动 ROI、原始灰度区间、加权灰度区间和临时 Z 分析代码；
2. `CMakeLists.txt` 不再引用已删除源文件；
3. 保留下来的公共模块职责清晰；
4. `main` 分支保持不变，所有整理先在 `cleanup-current-mainline` 分支进行；
5. 不在本阶段引入新的课题算法逻辑。

## 后续阶段

清理完成后再进入下一阶段：把 `test_intermediate_dump.cpp` 中有价值的逻辑重构为正式的 DFG 中间数据采集与 ROI / 非 ROI 分布分析模块。
