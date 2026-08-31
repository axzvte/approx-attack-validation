#pragma once

#include <opencv2/core/mat.hpp>

namespace region_mask
{

bool isImportantPixel(
    const cv::Mat& mask,
    int row,
    int col
);

}