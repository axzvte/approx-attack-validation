#pragma once

#include "analysis/intermediate_analysis.hpp"

#include <vector>

namespace distribution_analysis
{

struct BasicStatistics
{
    long long count;

    int min;
    int max;

    double mean;
    double stddev;
};


struct PositionStatistics
{
    BasicStatistics roi;
    BasicStatistics nonRoi;
};


struct SharpenDistributionStatistics
{
    PositionStatistics a1;
    PositionStatistics a2;
    PositionStatistics a3;
    PositionStatistics a4;
};


SharpenDistributionStatistics analyzeSharpenDistributions(
    const std::vector<intermediate_analysis::SharpenSample>& samples
);

}