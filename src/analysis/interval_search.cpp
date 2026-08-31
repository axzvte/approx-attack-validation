#include "analysis/interval_search.hpp"
#include "analysis/interval_evaluation.hpp"

namespace analysis
{

IntervalSearchResult findBestInterval(
    const PixelDistribution& distribution
)
{
    IntervalSearchResult bestResult{};

    bestResult.score = -2.0;

    for (int lower = 0; lower < 256; ++lower)
    {
        for (int upper = lower; upper < 256; ++upper)
        {
            const IntervalCoverage coverage =
                evaluateInterval(
                    distribution,
                    lower,
                    upper
                );

            const double coverageRatio =
                coverage.importantRatio;

            const double purity =
                coverage.importantPurity;

            double score = 0.0;

            if (coverageRatio + purity > 0.0)
            {
                score =
                    2.0 * coverageRatio * purity
                    / (coverageRatio + purity);
            }

            if (score > bestResult.score)
            {
                bestResult.lower = lower;
                bestResult.upper = upper;

                bestResult.importantRatio =
                    coverage.importantRatio;

                bestResult.nonImportantRatio =
                    coverage.nonImportantRatio;

                bestResult.importantPurity =
                    coverage.importantPurity;
                
                bestResult.score = score;
            }
        }
    }

    return bestResult;
}

}