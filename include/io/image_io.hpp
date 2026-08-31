#pragma once

#include <opencv2/core/mat.hpp>
#include <string>

namespace image_io
{

cv::Mat loadGrayImage(const std::string& path);

cv::Mat loadColorImage(const std::string& path);

void saveImage(
    const std::string& path,
    const cv::Mat& image
);

}