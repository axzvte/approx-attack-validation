#pragma once

#include "core/application.hpp"
#include "approximate/evoapprox_adapter.hpp"

#include <opencv2/core.hpp>

#include <cstdint>
#include <functional>
#include <vector>


namespace analysis
{

struct DctNodeUnitSensitivity
{
    int nodeId = -1;

    approximate::ApproxUnitId
        attackUnit =
            approximate::ApproxUnitId::Add12se5RP;


    // 在当前 Baseline 的真实节点输入上，
    // 将当前节点替换成 attackUnit 后产生的局部误差：
    //
    // e = attackUnit(a, b) - baseline(a, b)
    //
    // localMse = mean(e^2)
    double localMse = 0.0;


    // 最终重建图像相对当前 Baseline 图像的 MSE。
    double outputMse = 0.0;


    // ROI / Non-ROI 输出 MSE。
    double roiMse = 0.0;

    double nonRoiMse = 0.0;


    // 节点传播敏感性：
    //
    // sqrt(outputMse / localMse)
    //
    // 表示节点本地产生单位 RMS 误差后，
    // 最终图像大约产生多少 RMS 误差。
    double sensitivity = 0.0;


    // ROI 对应的传播敏感性。
    double roiSensitivity = 0.0;


    // 区域选择性：
    //
    // roiMse / nonRoiMse
    double roiToNonRoiRatio = 0.0;


    std::uint64_t localSampleCount = 0;
};


struct DctNodeImageUnitSensitivity
{
    std::size_t imageIndex = 0;

    int nodeId = -1;

    approximate::ApproxUnitId
        attackUnit =
            approximate::ApproxUnitId::Add12se5RP;


    double localMse = 0.0;

    double outputMse = 0.0;

    double roiMse = 0.0;

    double nonRoiMse = 0.0;

    double sensitivity = 0.0;

    double roiSensitivity = 0.0;

    double roiToNonRoiRatio = 0.0;

    std::uint64_t localSampleCount = 0;
};


struct DctNodeSensitivitySummary
{
    int nodeId = -1;


    // 对所有攻击近似加法器的 sensitivity 取平均。
    // 这里保留原有“全部图片汇总后”的统计方式。
    double meanSensitivity = 0.0;


    // 同一节点在全部攻击单元中的最大 sensitivity。
    double maxSensitivity = 0.0;


    approximate::ApproxUnitId
        maxSensitivityUnit =
            approximate::ApproxUnitId::Add12se5RP;


    double meanRoiSensitivity = 0.0;

    double meanRoiToNonRoiRatio = 0.0;


    // 跨图片稳定性：
    //
    // 第一步：
    // 对每张图片，先对所有攻击单元的 sensitivity 取平均，
    // 得到该节点在这张图片上的 S_i^(m)。
    //
    // 第二步：
    // 再对所有图片的 S_i^(m) 计算均值、标准差和 CV。
    double meanImageSensitivity = 0.0;

    double stdImageSensitivity = 0.0;

    double cvImageSensitivity = 0.0;
};


struct DctNodeSensitivityReport
{
    // 原有结果：
    // 每个节点 × 每个攻击单元，所有图片汇总后的指标。
    std::vector<DctNodeUnitSensitivity>
        unitResults;


    // 新增结果：
    // 每张图片 × 每个节点 × 每个攻击单元的独立指标。
    std::vector<DctNodeImageUnitSensitivity>
        perImageUnitResults;


    std::vector<DctNodeSensitivitySummary>
        nodeSummaries;
};


enum class DctNodeSensitivityPhase
{
    Baseline,
    Attack
};


struct DctNodeSensitivityProgress
{
    DctNodeSensitivityPhase phase =
        DctNodeSensitivityPhase::Baseline;

    std::size_t completed = 0;
    std::size_t total = 0;

    int nodeId = -1;

    approximate::ApproxUnitId
        attackUnit =
            approximate::ApproxUnitId::Add12se5RP;

    std::size_t imageIndex = 0;
    std::size_t imageCount = 0;
};


using DctNodeSensitivityProgressCallback =
    std::function<
        void(
            const DctNodeSensitivityProgress& progress
        )
    >;


class DctNodeSensitivityAnalyzer
{
public:

    // 使用多张真实图像，对任意 Application 暴露的 ADD/SUB 节点
    // 进行“单节点、全触发”敏感性分析。
    //
    // Baseline:
    //     使用 DctApplication 当前持有的 BaselineConfig。
    //     当前节点筛选实验使用工作 Baseline Balanced-10：
    //     Node 8、10、11、13、16、19、22、25、28、31
    //     使用 5RP，其余节点使用 Exact。
    //
    // 单节点攻击:
    //     仅当前节点切换到 attackUnit，
    //     触发范围固定为完整 signed-12 输入范围，
    //     因而该节点的每次动态调用都触发。
    //
    // localMse 使用 Baseline 运行时采集到的真实节点输入计算，
    // 因此不会把攻击已经传播到后续输入后的变化再次算入
    // “局部误差”。
    //
    // 除原有的全数据集汇总结果外，还会保留逐图片结果，
    // 用于评价节点的跨图片稳定性。
    static DctNodeSensitivityReport analyze(
        const core::Application& application,
        const std::vector<cv::Mat>& inputImages,
        const std::vector<cv::Mat>& roiMasks,
        const std::vector<approximate::ApproxUnitId>& attackUnits,
        const DctNodeSensitivityProgressCallback& progressCallback = {}
    );
};

}
