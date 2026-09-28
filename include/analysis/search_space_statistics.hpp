#pragma once

#include "analysis/big_uint.hpp"
#include "analysis/two_stage_search.hpp"
#include "core/application.hpp"

#include <cstddef>
#include <vector>


namespace analysis
{

struct IntegerRange
{
    bool initialized = false;

    int minimum = 0;
    int maximum = 0;
};


struct NodeSearchSpaceStatistics
{
    int nodeId = -1;

    IntegerRange input1Range;
    IntegerRange input2Range;

    BigUInt
        input1IntervalCount = 0;

    BigUInt
        input2IntervalCount = 0;

    // 当前节点的完整单节点配置数：
    //
    // attackUnits
    // ×
    // (
    //   input1 的全部 [L,U]
    //   +
    //   input2 的全部 [L,U]
    // )
    BigUInt
        candidateCount = 0;
};


struct AttackNodeCountStatistics
{
    std::size_t attackNodeCount = 0;

    BigUInt
        configurationCount = 0;
};


struct SearchSpaceStatisticsResult
{
    std::vector<NodeSearchSpaceStatistics>
        nodes;

    std::vector<AttackNodeCountStatistics>
        byAttackNodeCount;

    BigUInt
        totalConfigurations = 0;
};


class SearchSpaceStatistics
{
public:

    // 对连续整数范围 [minimum, maximum]，
    // 统计所有合法闭区间 [L,U] 的数量：
    //
    // N = maximum - minimum + 1
    //
    // intervalCount = N * (N + 1) / 2
    static BigUInt
    countAllIntervals(
        int minimum,
        int maximum
    );


    // 使用 Stage 1 的 10 张图片，
    // 在正常近似 Baseline 电路上采集每个节点的：
    //
    // input1 min/max
    // input2 min/max
    //
    // 然后精确计算 1~maxAttackNodes 的完整暴力搜索规模。
    //
    // 不做区间采样。
    // 多节点之间使用完整笛卡尔积。
    static SearchSpaceStatisticsResult
    analyze(
        const core::Application& application,
        const TwoStageDataset& dataset,
        const std::vector<approximate::ApproxUnitId>& attackUnits,
        std::size_t maxAttackNodes
    );
};

}
