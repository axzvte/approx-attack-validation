#include "analysis/distribution_analysis.hpp"

#include <limits>

namespace distribution_analysis
{

namespace
{

struct StatisticsAccumulator
{
    long long count = 0;

    int min = std::numeric_limits<int>::max();
    int max = std::numeric_limits<int>::min();

    long long sum = 0;
};


void updateAccumulator(
    StatisticsAccumulator& accumulator,
    int value
)
{
    ++accumulator.count;

    if (value < accumulator.min)
    {
        accumulator.min = value;
    }

    if (value > accumulator.max)
    {
        accumulator.max = value;
    }

    accumulator.sum += value;
}


BasicStatistics makeBasicStatistics(
    const StatisticsAccumulator& accumulator
)
{
    BasicStatistics statistics;

    statistics.count = accumulator.count;
    statistics.min = accumulator.min;
    statistics.max = accumulator.max;

    statistics.mean =
        static_cast<double>(accumulator.sum)
        / accumulator.count;

    statistics.stddev = 0.0;

    return statistics;
}

}


SharpenDistributionStatistics analyzeSharpenDistributions(
    const std::vector<intermediate_analysis::SharpenSample>& samples
)
{
    StatisticsAccumulator a1Roi;
    StatisticsAccumulator a1NonRoi;

    StatisticsAccumulator a2Roi;
    StatisticsAccumulator a2NonRoi;

    StatisticsAccumulator a3Roi;
    StatisticsAccumulator a3NonRoi;

    StatisticsAccumulator a4Roi;
    StatisticsAccumulator a4NonRoi;


    for (const auto& sample : samples)
    {
        if (sample.insideRoi)
        {
            updateAccumulator(
                a1Roi,
                sample.values.a1
            );

            updateAccumulator(
                a2Roi,
                sample.values.a2
            );

            updateAccumulator(
                a3Roi,
                sample.values.a3
            );

            updateAccumulator(
                a4Roi,
                sample.values.a4
            );
        }
        else
        {
            updateAccumulator(
                a1NonRoi,
                sample.values.a1
            );

            updateAccumulator(
                a2NonRoi,
                sample.values.a2
            );

            updateAccumulator(
                a3NonRoi,
                sample.values.a3
            );

            updateAccumulator(
                a4NonRoi,
                sample.values.a4
            );
        }
    }


    SharpenDistributionStatistics result;

    result.a1.roi =
        makeBasicStatistics(a1Roi);

    result.a1.nonRoi =
        makeBasicStatistics(a1NonRoi);


    result.a2.roi =
        makeBasicStatistics(a2Roi);

    result.a2.nonRoi =
        makeBasicStatistics(a2NonRoi);


    result.a3.roi =
        makeBasicStatistics(a3Roi);

    result.a3.nonRoi =
        makeBasicStatistics(a3NonRoi);


    result.a4.roi =
        makeBasicStatistics(a4Roi);

    result.a4.nonRoi =
        makeBasicStatistics(a4NonRoi);


    return result;
}

}