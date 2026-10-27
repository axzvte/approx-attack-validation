#pragma once

#include <opencv2/core/mat.hpp>
#include <string>

namespace image_io
{

cv::Mat loadGrayImage(const std::string& path);

cv::Mat loadColorImage(const std::string& path);

// Prefer stageN/input/image_NN.jpg; also accept stageN/image_NN.jpg.
// Resolves file paths only and does not read held-out image pixels.
std::string resolveStagedImagePath(const std::string& dataDir,int imageIndex);

void saveImage(
    const std::string& path,
    const cv::Mat& image
);

}
