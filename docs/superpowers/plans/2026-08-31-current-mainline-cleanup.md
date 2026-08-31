# Current Mainline Cleanup Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 删除已放弃的 ROI 自动定位、灰度区间、加权灰度和临时 Z 分析代码，并让工程保持可构建、可继续开发。

**Architecture:** 本阶段不实现新的课题算法，只做最小清理。保留图像 I/O、人工 ROI mask、精确锐化、EvoApproxLib 适配和 PSNR；删除旧方案相关模块，并同步修正 CMake 与近似锐化接口。

**Tech Stack:** C++17, CMake, OpenCV, EvoApproxLib

**Spec:** `docs/superpowers/specs/2026-08-31-current-mainline-cleanup-design.md`

## Global Constraints

- 不修改 `main` 分支，所有改动先落在 `cleanup-current-mainline`。
- 本阶段不新增 ROI 自动定位算法。
- 本阶段不新增关键运算位置搜索算法。
- 人工 ROI mask 作为验证输入保留。
- EvoApproxLib 及其适配层保留。

---

### Task 1: 删除自动 ROI、灰度区间、加权灰度和临时探索代码

**Files:**
- Delete: `app/test_roi_search.cpp`
- Delete: `app/test_visual_importance.cpp`
- Delete: `app/test_z_analysis.cpp`
- Delete: `include/analysis/visual_importance.hpp`
- Delete: `src/analysis/visual_importance.cpp`
- Delete: `include/analysis/application_response.hpp`
- Delete: `src/analysis/application_response.cpp`
- Delete: `include/analysis/attack_value.hpp`
- Delete: `src/analysis/attack_value.cpp`
- Delete: `include/analysis/pixel_distribution.hpp`
- Delete: `src/analysis/pixel_distribution.cpp`
- Delete: `include/analysis/interval_evaluation.hpp`
- Delete: `src/analysis/interval_evaluation.cpp`
- Delete: `include/analysis/interval_search.hpp`
- Delete: `src/analysis/interval_search.cpp`
- Delete: `include/analysis/weighted_distribution.hpp`
- Delete: `src/analysis/weighted_distribution.cpp`
- Delete: `include/analysis/weighted_interval_search.hpp`
- Delete: `src/analysis/weighted_interval_search.cpp`

**Interfaces:**
- Consumes: none
- Produces: a repository without abandoned analysis modules

- [ ] **Step 1: Delete the abandoned files listed above.**

- [ ] **Step 2: Check the repository tree and confirm those paths no longer exist.**

- [ ] **Step 3: Commit**

```bash
git add -A
git commit -m "refactor: remove abandoned analysis experiments"
```

---

### Task 2: 简化近似锐化接口

**Files:**
- Modify: `include/processing/approx_sharpen.hpp`
- Modify: `src/processing/approx_sharpen.cpp`
- Modify: `include/processing/sharpen_approx_config.hpp`
- Modify: `src/processing/sharpen_approx_config.cpp`

**Interfaces:**
- Consumes: `approximate::addSigned12`
- Produces: only the generic whole-image approximate sharpening entry point for now

- [ ] **Step 1: Remove declarations of `sharpenRegionalApproximate` and `sharpenIntervalApproximate`.**

- [ ] **Step 2: Remove implementations of `sharpenRegionalApproximate` and `sharpenIntervalApproximate`.**

- [ ] **Step 3: Remove `createNormalConfig`, `createImportantConfig`, and `createNonImportantConfig` declarations and definitions.**

- [ ] **Step 4: Keep `SharpenApproxConfig` and `sharpenApproximate` unchanged as the temporary generic approximation interface.**

- [ ] **Step 5: Commit**

```bash
git add include/processing src/processing
git commit -m "refactor: simplify approximate sharpening interface"
```

---

### Task 3: 让主程序暂时回到最小可运行基线

**Files:**
- Modify: `app/main.cpp`

**Interfaces:**
- Consumes: `image_io::loadGrayImage`, `image_processing::sharpenExact`, `image_io::saveImage`
- Produces: a minimal executable that verifies the cleaned project still runs

- [ ] **Step 1: Replace the old fixed grayscale-interval experiment with a minimal exact-sharpen smoke test.**

```cpp
#include "io/image_io.hpp"
#include "processing/exact_sharpen.hpp"

#include <exception>
#include <iostream>
#include <string>

int main()
{
    const std::string inputImagePath = "data/input/test_1.jpg";
    const std::string outputImagePath = "results/test_1_exact_sharpen.png";

    try
    {
        const cv::Mat inputImage = image_io::loadGrayImage(inputImagePath);
        const cv::Mat outputImage = image_processing::sharpenExact(inputImage);
        image_io::saveImage(outputImagePath, outputImage);
        std::cout << "Saved: " << outputImagePath << std::endl;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Error: " << error.what() << std::endl;
        return 1;
    }

    return 0;
}
```

- [ ] **Step 2: Commit**

```bash
git add app/main.cpp
git commit -m "refactor: reset main to exact sharpening baseline"
```

---

### Task 4: 清理 CMake 依赖并验证构建

**Files:**
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: remaining source files only
- Produces: a successful `run_validation` build

- [ ] **Step 1: Remove deleted analysis source files from `add_executable`.**

- [ ] **Step 2: Keep `src/analysis/` out of the target until the new intermediate-data module is designed.**

- [ ] **Step 3: Configure from a clean build directory.**

```bash
rm -rf build
cmake -S . -B build
```

Expected: CMake configuration completes without missing-source errors.

- [ ] **Step 4: Build.**

```bash
cmake --build build -j
```

Expected: `run_validation` builds successfully.

- [ ] **Step 5: Run smoke test.**

```bash
./build/run_validation
```

Expected: terminal prints the saved output path and `results/test_1_exact_sharpen.png` is created.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt
git commit -m "build: update sources after cleanup"
```

---

### Task 5: 第一阶段验收

**Files:**
- Review only

**Interfaces:**
- Consumes: Tasks 1-4
- Produces: clean baseline ready for DFG intermediate-data module

- [ ] **Step 1: Confirm abandoned analysis and ROI files are absent.**
- [ ] **Step 2: Confirm `app/test_intermediate_dump.cpp` remains untouched for reference.**
- [ ] **Step 3: Confirm EvoApproxLib, ROI mask, PSNR, I/O, and exact sharpening remain.**
- [ ] **Step 4: Confirm the branch builds successfully.**
- [ ] **Step 5: Stop before implementing new DFG analysis logic.**
