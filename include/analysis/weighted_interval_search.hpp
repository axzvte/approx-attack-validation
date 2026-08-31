#pragma once

#include "analysis/weighted_distribution.hpp"

namespace analysis
{

struct WeightedIntervalSearchResult
{
    int lower;
    int upper;

    // 区间覆盖到的综合攻击价值
    double capturedAttackValue;

    // 区间覆盖了全部综合攻击价值的多少
    double attackValueCoverage;

    // ROI 内被区间触发的像素数量
    int insideTriggeredPixels;

    // ROI 外被区间触发的像素数量
    int outsideTriggeredPixels;

    // ROI 本身有多少比例被触发
    double insideCoverage;

    // 所有被触发的像素中，有多少位于 ROI 内
    double attackConcentration;
};


WeightedIntervalSearchResult searchWeightedInterval(
    const WeightedDistribution& distribution,
    double minAttackConcentration
);

}