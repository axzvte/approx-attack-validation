#pragma once

#include "analysis/pixel_distribution.hpp"

namespace analysis
{

struct IntervalCoverage
{
    int importantCount;
    int nonImportantCount;

    double importantRatio;
    double nonImportantRatio;

    double importantPurity;
};

IntervalCoverage evaluateInterval(
    const PixelDistribution& distribution,
    int lower,
    int upper
);

}