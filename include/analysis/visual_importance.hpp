#pragma once

#include <opencv2/core.hpp>

namespace analysis
{

cv::Mat calculateVisualImportance(
    const cv::Mat& grayImage,
    const cv::Mat& roiMask
);

}