#pragma once

#include <opencv2/core.hpp>

namespace analysis
{

cv::Mat calculateAttackValue(
    const cv::Mat& visualImportance,
    const cv::Mat& applicationResponse
);

}