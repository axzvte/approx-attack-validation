#include "analysis/search_space_statistics.hpp"

#include "analysis/add_sample_collector.hpp"
#include "io/image_io.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>


namespace analysis
{

namespace
{

void updateRange(
    IntegerRange& range,
    int value
)
{
    if (!range.initialized)
    {
        range.initialized =
            true;

        range.minimum =
            value;

        range.maximum =
            value;

        return;
    }


    range.minimum =
        std::min(
            range.minimum,
            value
        );


    range.maximum =
        std::max(
            range.maximum,
            value
        );
}


void validateAttackUnits(
    const std::vector<approximate::ApproxUnitId>& attackUnits
)
{
    if (attackUnits.empty())
    {
        throw std::runtime_error(
            "Attack unit list is empty."
        );
    }


    std::set<int>
        uniqueUnits;


    for (
        approximate::ApproxUnitId unit :
        attackUnits
    )
    {
        if (
            !uniqueUnits.insert(
                static_cast<int>(
                    unit
                )
            ).second
        )
        {
            throw std::runtime_error(
                "Duplicate attack approximate unit."
            );
        }
    }
}

}


// =========================================================
// 全部 [L,U] 数量
// =========================================================

boost::multiprecision::cpp_int
SearchSpaceStatistics::countAllIntervals(
    int minimum,
    int maximum
)
{
    if (minimum > maximum)
    {
        throw std::runtime_error(
            "Invalid integer range."
        );
    }


    const boost::multiprecision::cpp_int
        valueCount =
            static_cast<long long>(
                maximum
            )
            -
            static_cast<long long>(
                minimum
            )
            +
            1;


    return
        valueCount
        *
        (
            valueCount
            +
            1
        )
        /
        2;
}


// =========================================================
// Stage 1 Baseline 搜索规模统计
// =========================================================

SearchSpaceStatisticsResult
SearchSpaceStatistics::analyze(
    const core::Application& application,
    const TwoStageDataset& dataset,
    const std::vector<approximate::ApproxUnitId>& attackUnits,
    std::size_t maxAttackNodes
)
{
    TwoStageSearch::validateDataset(
        dataset
    );


    validateAttackUnits(
        attackUnits
    );


    const auto& addNodes =
        application.addNodes();


    if (addNodes.empty())
    {
        throw std::runtime_error(
            "Application contains no ADD nodes."
        );
    }


    if (
        maxAttackNodes == 0
        ||
        maxAttackNodes
            >
            addNodes.size()
    )
    {
        throw std::runtime_error(
            "maxAttackNodes is outside the available node range."
        );
    }


    const cv::Mat roiMask =
        image_io::loadGrayImage(
            dataset.roiMaskPath
        );


    SearchSpaceStatisticsResult
        result;


    result.nodes.reserve(
        addNodes.size()
    );


    std::map<int, std::size_t>
        nodeIndexById;


    for (
        std::size_t index = 0;
        index < addNodes.size();
        ++index
    )
    {
        NodeSearchSpaceStatistics
            nodeStatistics;


        nodeStatistics.nodeId =
            addNodes[index].id;


        nodeIndexById[
            addNodes[index].id
        ] =
            index;


        result.nodes.push_back(
            nodeStatistics
        );
    }


    AddSampleCollector
        collector;


    // =====================================================
    // 10 张 Stage 1 图片全部用于范围统计
    //
    // 关键：
    // 这里采集的是正常近似 Baseline 电路，
    // 不是精确电路。
    // =====================================================

    for (
        const auto& imageCase :
        dataset.stage1Images
    )
    {
        const cv::Mat inputImage =
            image_io::loadGrayImage(
                imageCase.inputPath
            );


        if (
            inputImage.rows
                !=
                roiMask.rows
            ||
            inputImage.cols
                !=
                roiMask.cols
        )
        {
            throw std::runtime_error(
                "Stage 1 image size does not match the shared ROI mask."
            );
        }


        collector.collectBaseline(
            application,
            inputImage,
            roiMask
        );
    }


    for (
        const auto& sample :
        collector.samples()
    )
    {
        const auto iterator =
            nodeIndexById.find(
                sample.nodeId
            );


        if (
            iterator
            ==
            nodeIndexById.end()
        )
        {
            throw std::runtime_error(
                "Collected sample contains an unknown node ID."
            );
        }


        auto& nodeStatistics =
            result.nodes[
                iterator->second
            ];


        updateRange(
            nodeStatistics.input1Range,
            sample.input1
        );


        updateRange(
            nodeStatistics.input2Range,
            sample.input2
        );
    }


    const boost::multiprecision::cpp_int
        attackUnitCount =
            attackUnits.size();


    for (
        auto& nodeStatistics :
        result.nodes
    )
    {
        if (
            !nodeStatistics.input1Range.initialized
            ||
            !nodeStatistics.input2Range.initialized
        )
        {
            throw std::runtime_error(
                "A node has no baseline samples."
            );
        }


        nodeStatistics.input1IntervalCount =
            countAllIntervals(
                nodeStatistics.input1Range.minimum,
                nodeStatistics.input1Range.maximum
            );


        nodeStatistics.input2IntervalCount =
            countAllIntervals(
                nodeStatistics.input2Range.minimum,
                nodeStatistics.input2Range.maximum
            );


        nodeStatistics.candidateCount =
            attackUnitCount
            *
            (
                nodeStatistics.input1IntervalCount
                +
                nodeStatistics.input2IntervalCount
            );
    }


    // =====================================================
    // 精确计算不同攻击节点数量下的完整笛卡尔积总数
    //
    // dp[k] 表示：
    // 从已经处理的节点中选 k 个节点时，
    // 所有节点候选配置笛卡尔积的总数量。
    //
    // 使用 cpp_int，避免 uint64_t 溢出。
    // =====================================================

    std::vector<boost::multiprecision::cpp_int>
        dp(
            maxAttackNodes + 1
        );


    dp[0] =
        1;


    std::size_t processedNodes =
        0;


    for (
        const auto& nodeStatistics :
        result.nodes
    )
    {
        const std::size_t maximumK =
            std::min(
                maxAttackNodes,
                processedNodes + 1
            );


        for (
            std::size_t k = maximumK;
            k >= 1;
            --k
        )
        {
            dp[k] +=
                dp[k - 1]
                *
                nodeStatistics.candidateCount;
        }


        ++processedNodes;
    }


    for (
        std::size_t k = 1;
        k <= maxAttackNodes;
        ++k
    )
    {
        result.byAttackNodeCount.push_back(
            {
                k,
                dp[k]
            }
        );


        result.totalConfigurations +=
            dp[k];
    }


    return
        result;
}

}
