#pragma once

#include "analysis/intermediate_analysis.hpp"

#include <vector>

namespace trigger_search
{

enum class AdderPosition
{
    A1,
    A2,
    A3,
    A4
};


struct TriggerRange
{
    int input1Lower;
    int input1Upper;

    int input2Lower;
    int input2Upper;
};


struct TriggerMetrics
{
    long long roiTriggered;
    long long nonRoiTriggered;

    long long totalRoi;
    long long totalNonRoi;

    double roiCoverage;
    double precision;
};

struct TriggerCandidate
{
    TriggerRange range;
    TriggerMetrics metrics;
};


TriggerMetrics evaluateTriggerRange(
    const std::vector<intermediate_analysis::SharpenSample>& samples,
    AdderPosition position,
    const TriggerRange& range
);

std::vector<TriggerCandidate> searchTriggerRanges(
    const std::vector<intermediate_analysis::SharpenSample>& samples,
    AdderPosition position
);

}