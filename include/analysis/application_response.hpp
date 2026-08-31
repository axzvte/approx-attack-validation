#pragma once

#include <opencv2/core.hpp>

namespace analysis
{

cv::Mat calculateApplicationResponse(
    const cv::Mat& inputImage,
    const cv::Mat& exactOutput
);

}