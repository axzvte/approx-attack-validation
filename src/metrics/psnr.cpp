#include "metrics/psnr.hpp"
#include "region/region_mask.hpp"

#include <stdexcept>
#include <cmath>
#include <limits>

namespace metrics
{

double calculateGlobalPSNR(
    const cv::Mat& referenceImage,
    const cv::Mat& testImage
)
{
    if (referenceImage.empty() ||
        testImage.empty())
    {
        throw std::runtime_error(
            "PSNR input image is empty."
        );
    }

    if (referenceImage.type() != CV_8UC1 ||
        testImage.type() != CV_8UC1)
    {
        throw std::runtime_error(
            "PSNR requires 8-bit grayscale images."
        );
    }

    if (referenceImage.rows != testImage.rows ||
        referenceImage.cols != testImage.cols)
    {
        throw std::runtime_error(
            "PSNR image dimensions do not match."
        );
    }

    double sumSquaredError = 0.0;

    for (int row = 0; row < referenceImage.rows; ++row)
    {
        for (int col = 0; col < referenceImage.cols; ++col)
        {
            const double referencePixel =
                referenceImage.at<unsigned char>(row, col);

            const double testPixel =
                testImage.at<unsigned char>(row, col);

            const double difference =
                referencePixel - testPixel;

            sumSquaredError +=
                difference * difference;
        }
    }

    const double totalPixels =
        static_cast<double>(
            referenceImage.rows * referenceImage.cols
        );

    const double meanSquaredError =
        sumSquaredError / totalPixels;

    if (meanSquaredError == 0.0)
    {
        return std::numeric_limits<double>::infinity();
    }

    return 10.0 * std::log10(
        (255.0 * 255.0) / meanSquaredError
    );

    }

    double calculateImportantRegionPSNR(
    const cv::Mat& referenceImage,
    const cv::Mat& testImage,
    const cv::Mat& mask
    )
    {
    
    if (referenceImage.empty() ||
        testImage.empty() ||
        mask.empty())
    {
        throw std::runtime_error(
            "Important-region PSNR input is empty."
        );
    }

    if (referenceImage.type() != CV_8UC1 ||
        testImage.type() != CV_8UC1 ||
        mask.type() != CV_8UC1)
    {
        throw std::runtime_error(
            "Important-region PSNR requires "
            "8-bit grayscale images."
        );
    }

    if (referenceImage.rows != testImage.rows ||
        referenceImage.cols != testImage.cols ||
        referenceImage.rows != mask.rows ||
        referenceImage.cols != mask.cols)
    {
        throw std::runtime_error(
            "Important-region PSNR dimensions "
            "do not match."
        );
    }

    double sumSquaredError = 0.0;
    int importantPixelCount = 0;

    for (int row = 0;
         row < referenceImage.rows;
         ++row)
    {
        for (int col = 0;
             col < referenceImage.cols;
             ++col)
        {
            if (!region_mask::isImportantPixel(
                    mask,
                    row,
                    col
                ))
            {
                continue;
            }

            const double referencePixel =
                referenceImage.at<unsigned char>(
                    row,
                    col
                );

            const double testPixel =
                testImage.at<unsigned char>(
                    row,
                    col
                );

            const double difference =
                referencePixel - testPixel;

            sumSquaredError +=
                difference * difference;

            ++importantPixelCount;
        }
    }

    if (importantPixelCount == 0)
    {
        throw std::runtime_error(
            "The mask contains no important pixels."
        );
    }

    const double meanSquaredError =
        sumSquaredError /
        static_cast<double>(importantPixelCount);

    if (meanSquaredError == 0.0)
    {
        return std::numeric_limits<double>::infinity();
    }

    return 10.0 * std::log10(
        (255.0 * 255.0) / meanSquaredError
    );
    }

}