#pragma once

#include "processing/sharpen_approx_config.hpp"

#include <opencv2/core/mat.hpp>

namespace image_processing
{

cv::Mat sharpenApproximate(
    const cv::Mat& inputImage,
    const SharpenApproxConfig& config
);

}
