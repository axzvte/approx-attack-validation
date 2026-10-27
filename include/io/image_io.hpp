#pragma once

#include <opencv2/core/mat.hpp>
#include <string>

namespace image_io
{

// Strict ROI paths ignore EXIF orientation, matching Pillow/U2-Net raw coordinates.
cv::Mat loadGrayImage(const std::string& path,bool ignoreExifOrientation=false);

cv::Mat loadColorImage(const std::string& path);

// Prefer stageN/input/image_NN.jpg; also accept stageN/image_NN.jpg.
// Resolves file paths only and does not read held-out image pixels.
std::string resolveStagedImagePath(const std::string& dataDir,int imageIndex);

// Strict per-image U2-Net ROI: no resize, threshold or legacy fallback.
std::string resolveRoiMaskPath(const std::string& roiDir,int imageIndex);
cv::Mat loadBinaryRoiMask(const std::string& path,const cv::Mat& image);

void saveImage(
    const std::string& path,
    const cv::Mat& image
);

}
