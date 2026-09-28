#pragma once

#include <opencv2/core/mat.hpp>


namespace region_mask
{

bool isImportantPixel(
    const cv::Mat& mask,
    int row,
    int col
);


// 将统一 ROI mask 缩放到目标图像尺寸。
//
// 使用最近邻插值，避免二值 mask 边界产生灰度值。
cv::Mat resizeMaskToImage(
    const cv::Mat& mask,
    const cv::Mat& targetImage
);


// 根据 PASCAL-S 统计得到的 ROI
// 自动生成与输入图片尺寸一致的二值掩码
cv::Mat createStatisticalRoiMask(
    int width,
    int height
);

}