#pragma once

#include "analysis/dct_interval_full_validator.hpp"
#include "analysis/two_stage_search.hpp"
#include "applications/dct.hpp"

#include <cstddef>
#include <vector>


namespace analysis
{

struct DctMultiNodeIntervalOptimizerOptions
{
    // Module 2-A 每种方向进入完整 DCT 验证的候选数。
    std::size_t candidatesPerRole = 3;


    // 补偿初始化后，最多进行多少轮逐节点 ROI 优化。
    std::size_t maxAttackRounds = 2;


    // 当前课题的全图质量约束。
    double globalPsnrThreshold = 30.0;


    // 用于判断 PSNR 是否发生实质变化。
    double improvementEpsilon = 1.0e-9;
};


struct DctMultiNodeOptimizationStep
{
    std::size_t round = 0;

    int nodeId = -1;

    bool compensationPhase = false;
    bool changed = false;


    bool hadPreviousInterval = false;

    TriggerInterval previousInterval{
        0,
        0
    };


    TriggerInterval selectedInterval{
        0,
        0
    };


    std::size_t fastCandidateCount = 0;
    std::size_t fullCandidateCount = 0;


    DctImageQualityMetrics beforeMetrics;
    DctImageQualityMetrics afterMetrics;
};


struct DctMultiNodeIntervalOptimizationResult
{
    // 最终真正启用的区间配置。
    //
    // 某个结构节点如果始终没有找到有价值的区间，
    // 可以暂时不出现在这里；外层硬件搜索阶段可将其视为
    // “该节点在当前图片上不需要触发”。
    AttackConfiguration configuration;


    DctImageQualityMetrics initialMetrics;
    DctImageQualityMetrics finalMetrics;


    std::size_t completedAttackRounds = 0;


    std::vector<DctMultiNodeOptimizationStep>
        steps;
};


class DctMultiNodeIntervalOptimizer
{
public:

    // 给定固定硬件结构：
    //
    // node + monitor input + approximate unit
    //
    // 在一张图片上联合优化各节点的触发区间。
    //
    // 流程：
    //
    // 1. 补偿初始化
    //    每个节点暂时移除自身配置，保留其它节点当前配置；
    //    基于当前真实输入生成快速区间候选；
    //    只在完整 DCT 中确实提高 Global PSNR 时启用该区间。
    //
    // 2. ROI 攻击优化
    //    逐节点重新采样、重新生成候选并完整 DCT 验证；
    //    在 Global PSNR >= threshold 的候选中，
    //    选择 ROI PSNR 更低的区间。
    //
    // 3. 最多重复 maxAttackRounds 轮；
    //    如果一整轮没有节点发生变化则提前结束。
    //
    // 每次优化目标节点时会暂时移除该节点的旧区间，
    // 因而候选空间不会被自身旧触发行为锁死；
    // 其它节点保持当前配置，因此多节点间的传播影响会被保留。
    static DctMultiNodeIntervalOptimizationResult optimize(
        const applications::DctApplication& application,
        const cv::Mat& inputImage,
        const cv::Mat& roiMask,
        const AttackStructure& structure,
        const DctMultiNodeIntervalOptimizerOptions& options =
            DctMultiNodeIntervalOptimizerOptions{}
    );
};

}
