#pragma once

#include <array>
#include <opencv2/core/mat.hpp>

namespace analysis
{

struct PixelDistribution
{
    std::array<int, 256> important;
    std::array<int, 256> nonImportant;
};

PixelDistribution calculatePixelDistribution(
    const cv::Mat& inputImage,
    const cv::Mat& mask
);

}