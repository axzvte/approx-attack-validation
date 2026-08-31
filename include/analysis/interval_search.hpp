#pragma once

#include "analysis/pixel_distribution.hpp"

namespace analysis
{

struct IntervalSearchResult
{
    int lower;
    int upper;

    double importantRatio;
    double nonImportantRatio;
    double importantPurity;

    double score;
};

IntervalSearchResult findBestInterval(
    const PixelDistribution& distribution
);

}