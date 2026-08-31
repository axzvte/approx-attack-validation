#include "region/region_mask.hpp"

namespace region_mask
{

bool isImportantPixel(
    const cv::Mat& mask,
    int row,
    int col
)
{
    const unsigned char pixelValue =
        mask.at<unsigned char>(row, col);

    return pixelValue >= 128;
}

}