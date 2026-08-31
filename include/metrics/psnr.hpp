#pragma once

#include <opencv2/core/mat.hpp>

namespace metrics
{

double calculateGlobalPSNR(
    const cv::Mat& referenceImage,
    const cv::Mat& testImage
);

double calculateImportantRegionPSNR(
    const cv::Mat& referenceImage,
    const cv::Mat& testImage,
    const cv::Mat& mask
);

}