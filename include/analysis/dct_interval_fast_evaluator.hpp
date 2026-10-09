#pragma once

#include "analysis/brute_force_search.hpp"
#include "approximate/evoapprox_adapter.hpp"
#include "core/add_sample.hpp"
#include "core/attack_config.hpp"

#include <cstddef>
#include <vector>


namespace analysis
{

// 模块 2-A 的快速区间指标。
//
// 这里只根据当前真实动态样本，估计把目标节点从“当前正常 Baseline 单元”
// 切换到指定 approximate unit 后，区间内的局部误差变化。
//
// 不运行完整 DCT，不计算 PSNR，也不直接决定最终区间。
struct DctIntervalFastMetric
{
    TriggerInterval interval;


    // 当前区间在 ROI / Non-ROI 动态计算中的加权触发比例。
    // Non-ROI 仅保留用于统计与 Global 误差估计，不再作为独立筛选目标。
    double roiTriggerRate = 0.0;
    double nonRoiTriggerRate = 0.0;


    // 相对该节点当前 Baseline 单元的加权局部平方误差变化。
    //
    // > 0 : 切换到 attackUnit 后局部误差增大
    // < 0 : 切换到 attackUnit 后局部误差减小
    //
    // 这里分别除以整张图片全部 ROI / Non-ROI 的总权重，
    // 因而不同宽度区间之间可以直接比较。
    double roiErrorChange = 0.0;
    double nonRoiErrorChange = 0.0;


    // 兼容旧分析工具保留的方向性指标：
    //
    // redistributionScore
    //     = roiErrorChange - nonRoiErrorChange
    //
    // 越大表示局部误差越倾向于被推向 ROI，
    // 或者更多地从 Non-ROI 中被减小。
    //
    // 当前联合搜索的代表区间筛选不再使用该分数；
    // 最终选择由完整应用的 Global / ROI PSNR 决定。
    double redistributionScore = 0.0;
};


enum class DctIntervalFastRanking
{
    // ROI 局部误差增加越多越靠前。
    RoiAttack,

    // Non-ROI 局部误差降低越多越靠前。
    NonRoiCompensation,

    // ROIErrorChange - NonROIErrorChange 越大越靠前。
    Redistribution
};


struct DctIntervalRepresentativeSelection
{
    std::size_t totalMetricCount = 0;

    // 在“ROIErrorChange 越大越好、GlobalErrorChange 越小越好”
    // 这两个方向上，没有被其它区间同时压制的候选数量。
    std::size_t paretoMetricCount = 0;


    // 从非支配前沿中挑出的少量代表区间。
    //
    // 它们不是最终最佳区间，只是为了给完整应用保留
    // 不同“ROI 破坏 / Global 代价”权衡。
    std::vector<DctIntervalFastMetric>
        representatives;
};


struct DctIntervalFastEvaluation
{
    int nodeId = -1;

    core::MonitorSignal monitorInput =
        core::MonitorSignal::Input1;

    approximate::ApproxUnitId attackUnit =
        approximate::ApproxUnitId::Add12se5RP;


    std::size_t sampleCount = 0;
    std::size_t distinctMonitorValueCount = 0;


    // 本次快速搜索真正用于构造区间的边界数量。
    // 等于 distinctMonitorValueCount 表示全枚举；
    // 更小时表示启用了基于当前图片的自适应候选边界。
    std::size_t candidateBoundaryValueCount = 0;


    double totalRoiWeight = 0.0;
    double totalNonRoiWeight = 0.0;


    // 与 Module 1 生成的区间一一对应。
    std::vector<DctIntervalFastMetric>
        metrics;
};


class DctIntervalFastEvaluator
{
public:

    // 使用当前真实运行采集到的 samples，
    // 对一个固定：
    //
    // node + monitor signal + attack unit
    //
    // 的全部合法区间做快速评价。
    //
    // 计算过程：
    // 1. 对目标节点每条动态样本计算：
    //      当前 Baseline 输出相对精确加法的平方误差；
    //      attackUnit 相对精确加法的平方误差；
    //      二者差值；
    // 2. 按 monitor value 聚合；
    // 3. 建立 ROI / Non-ROI 前缀和；
    // 4. O(1) 得到任意 [lower, upper] 的快速指标。
    //
    // maxCandidateBoundaryCount == 0：
    //   保持旧行为，全部不同 monitor value 都可以作为边界；
    //
    // maxCandidateBoundaryCount > 0：
    //   当实际取值很多时，根据当前图片动态选择少量候选边界，
    //   主要覆盖 ROI 分位点、整图分位点以及 ROI 局部误差贡献大的值。
    //
    // 不需要对每个区间重新运行完整应用。
    static DctIntervalFastEvaluation evaluateFromSamples(
        const std::vector<core::AddSample>& samples,
        int nodeId,
        core::MonitorSignal monitorInput,
        approximate::ApproxUnitId attackUnit,
        std::size_t maxCandidateBoundaryCount = 0
    );


    // 从已经完成快速评价的全部区间中，按指定方向取前 count 个。
    //
    // 这里只做排序，不改变 DctIntervalFastEvaluation::metrics，
    // 也不代表最终区间已经确定。
    //
    // 三种排序分别服务于后续完整 DCT 验证：
    // 1. RoiAttack：寻找可能明显增加 ROI 误差的区间；
    // 2. NonRoiCompensation：寻找可能降低 Non-ROI 误差的区间；
    // 3. Redistribution：寻找局部误差更倾向 ROI 的区间。
    static std::vector<DctIntervalFastMetric>
    selectTopMetrics(
        const DctIntervalFastEvaluation& evaluation,
        DctIntervalFastRanking ranking,
        std::size_t count
    );


    // Module 3-A：代表区间选择。
    //
    // 目标不是找“单节点最佳区间”，而是为后续多节点联合搜索
    // 保留一组不同误差权衡的区间。
    //
    // 第一步：构造二维非支配前沿。
    //
    // 目标方向为：
    //   ROI 局部误差增加越大越好；
    //   对整体 Global 误差的增加越小越好。
    //
    // 第二步：如果前沿仍超过 maxCount，
    // 在归一化后的 ROI / Global 二维空间中保留两端极值，
    // 再逐个选择离已有代表点最远的候选。
    //
    // 这里仍然不运行完整 DCT。
    static DctIntervalRepresentativeSelection
    selectRepresentativeMetrics(
        const DctIntervalFastEvaluation& evaluation,
        std::size_t maxCount
    );
};

}
