#pragma once

#include "applications/sharpen.hpp"
#include <cstdint>
#include <functional>
#include <vector>

namespace analysis {

struct SharpenBaselineSample {
    int row=0, col=0, nodeId=0;
    bool roi=false;
    int input1=0, input2=0, baselineOutput=0;
    int exactSameInputOutput=0, exactPathOutput=0;
    int localError=0, accumulatedError=0;
    int baselinePixel=0, referencePixel=0;
};

struct SharpenBaselineData {
    int rows=0, cols=0;
    applications::SharpenApplication::BaselineConfig config{};
    // Row-major interior pixels, four nodes per pixel. Borders have no nodes.
    std::vector<SharpenBaselineSample> samples;
    std::uint64_t roiPixels=0, nonRoiPixels=0;
    std::int64_t roiSquaredError=0, nonRoiSquaredError=0;
};

struct SharpenBitflipResponse {
    int bit=0, flippedOutput=0, nodeDelta=0;
    int localErrorAfter=0, accumulatedErrorAfter=0;
    std::int64_t localSquaredErrorDelta=0, accumulatedSquaredErrorDelta=0;
    bool valid=false; // False: a downstream approximate input exceeds signed 12 bits.
    // Final fields are meaningful only when valid is true.
    int finalPixel=0;
    std::int64_t finalSquaredErrorDelta=0;
};

SharpenBaselineData collectSharpenBaseline(const applications::SharpenApplication& app,
    const cv::Mat& image, const cv::Mat& mask);

// Independent probes from the SAME baseline, not a combined multi-node attack.
// Uses the saved baseline configuration, including exact/mixed downstream nodes.
void visitSharpenBitflips(const SharpenBaselineData& data,
    const std::function<void(const SharpenBaselineSample&,const SharpenBitflipResponse&)>& visitor);

}
