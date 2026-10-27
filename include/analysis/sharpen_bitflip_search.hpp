#pragma once

#include "applications/sharpen.hpp"
#include <cstddef>
#include <vector>

namespace analysis {

struct BitflipQuality {
    double globalMse = 0, roiMse = 0, nonRoiMse = 0;
    double globalPsnr() const;
    double roiPsnr() const;
    double nonRoiPsnr() const;
};

BitflipQuality measureBitflipQuality(const cv::Mat& reference,
    const cv::Mat& output, const cv::Mat& mask);

struct SharpenBitflipCandidate {
    core::AttackConfig config{};
    bool hasInterval = false; // No feasible interval is a recorded failure.
    BitflipQuality quality;
    double triggerRate = 0, roiTriggerRate = 0, nonRoiTriggerRate = 0;
    std::size_t evaluatedIntervals = 0, rangeRejectedIntervals = 0;
    bool successful(const BitflipQuality& baseline) const;
};

struct SharpenBitflipResult {
    BitflipQuality baseline;
    std::vector<SharpenBitflipCandidate> candidates;
};

class SharpenBitflipSearch {
public:
    // Exactly scores final pixel errors for a SINGLE attacked node. Samples
    // and downstream replay include all existing approximate units and clipping.
    // Zero boundary budget enumerates all observed endpoints; otherwise use an
    // evenly spaced grid over sorted distinct observed values (not all intervals).
    static SharpenBitflipResult evaluate(const applications::SharpenApplication& app,
        const cv::Mat& image, const cv::Mat& mask,
        std::size_t boundaryBudget = 24, double globalPsnrThreshold = 30.0,
        const core::AttackConfig* fixedHardware = nullptr);
};

}
