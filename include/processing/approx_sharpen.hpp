#pragma once

#include "processing/sharpen_approx_config.hpp"

#include <opencv2/core/mat.hpp>

namespace image_processing
{

cv::Mat sharpenApproximate(
    const cv::Mat& inputImage,
    const SharpenApproxConfig& config
);

cv::Mat sharpenRegionalApproximate(
    const cv::Mat& inputImage,
    const cv::Mat& mask,
    const SharpenApproxConfig& importantConfig,
    const SharpenApproxConfig& nonImportantConfig
);

cv::Mat sharpenIntervalApproximate(
    const cv::Mat& inputImage,
    int lower,
    int upper,
    const SharpenApproxConfig& approximateConfig
);

}