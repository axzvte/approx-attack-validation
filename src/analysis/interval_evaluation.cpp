#include "analysis/interval_evaluation.hpp"

#include <stdexcept>

namespace analysis
{

IntervalCoverage evaluateInterval(
    const PixelDistribution& distribution,
    int lower,
    int upper
)
{
    if (lower < 0 || upper > 255 || lower > upper)
    {
        throw std::runtime_error("Invalid interval.");
    }

    int importantTotal = 0;
    int nonImportantTotal = 0;

    for (int value = 0; value < 256; ++value)
    {
        importantTotal += distribution.important[value];
        nonImportantTotal += distribution.nonImportant[value];
    }

    int importantCount = 0;
    int nonImportantCount = 0;

    for (int value = lower; value <= upper; ++value)
    {
        importantCount += distribution.important[value];
        nonImportantCount += distribution.nonImportant[value];
    }

    IntervalCoverage coverage{};

    coverage.importantCount = importantCount;
    coverage.nonImportantCount = nonImportantCount;

    coverage.importantRatio =
        static_cast<double>(importantCount) / importantTotal;

    coverage.nonImportantRatio =
        static_cast<double>(nonImportantCount) / nonImportantTotal;

    const int triggeredTotal =
        importantCount + nonImportantCount;

    if (triggeredTotal > 0)
    {
        coverage.importantPurity =
            static_cast<double>(importantCount)
            / triggeredTotal;
    }
    else
    {
        coverage.importantPurity = 0.0;
    }

    return coverage;
}

}