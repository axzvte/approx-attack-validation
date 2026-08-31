#include "analysis/pixel_distribution.hpp"
#include "region/region_mask.hpp"

#include <stdexcept>

namespace analysis
{

PixelDistribution calculatePixelDistribution(
    const cv::Mat& inputImage,
    const cv::Mat& mask
)
{
    if (inputImage.empty() || mask.empty())
    {
        throw std::runtime_error("Input image or mask is empty.");
    }

    if (inputImage.rows != mask.rows ||
        inputImage.cols != mask.cols)
    {
        throw std::runtime_error(
            "Input image and mask sizes do not match."
        );
    }

    PixelDistribution distribution{};

    for (int row = 0; row < inputImage.rows; ++row)
    {
        for (int col = 0; col < inputImage.cols; ++col)
        {
            const unsigned char pixelValue =
                inputImage.at<unsigned char>(row, col);

            if (region_mask::isImportantPixel(mask, row, col))
            {
                ++distribution.important[pixelValue];
            }
            else
            {
                ++distribution.nonImportant[pixelValue];
            }
        }
    }

    return distribution;
}

}