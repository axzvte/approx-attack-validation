#pragma once

#include <array>

#include <opencv2/core.hpp>

namespace analysis
{

struct WeightedDistribution
{
    // 每个灰度值累积的综合攻击价值 Q(v)
    std::array<double, 256> attackValue;

    // ROI 内每个灰度值出现多少次
    std::array<int, 256> insideCount;

    // ROI 外每个灰度值出现多少次
    std::array<int, 256> outsideCount;

    // ROI 内所有综合攻击价值之和
    double totalAttackValue;

    // ROI 内总像素数量
    int totalInsidePixels;

    // ROI 外总像素数量
    int totalOutsidePixels;
};


WeightedDistribution calculateWeightedDistribution(
    const cv::Mat& grayImage,
    const cv::Mat& attackValueMap,
    const cv::Mat& roiMask
);

}