#pragma once

#include <opencv2/core/mat.hpp>


namespace region_mask
{

bool isImportantPixel(
    const cv::Mat& mask,
    int row,
    int col
);


// 根据 PASCAL-S 统计得到的 ROI
// 自动生成与输入图片尺寸一致的二值掩码
cv::Mat createStatisticalRoiMask(
    int width,
    int height
);

}