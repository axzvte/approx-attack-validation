#pragma once

#include <opencv2/core/mat.hpp>

namespace image_processing
{

cv::Mat sharpenExact(const cv::Mat& inputImage);

}