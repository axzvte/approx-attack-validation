#include "processing/approx_sharpen.hpp"
#include "approximate/evoapprox_adapter.hpp"
#include "region/region_mask.hpp"

#include <stdexcept>

namespace image_processing
{

cv::Mat sharpenApproximate(
    const cv::Mat& inputImage,
    const SharpenApproxConfig& config
)
{
    if (inputImage.empty())
    {
        throw std::runtime_error(
            "Input image is empty."
        );
    }

    if (inputImage.type() != CV_8UC1)
    {
        throw std::runtime_error(
            "Approximate sharpening requires an 8-bit grayscale image."
        );
    }

    cv::Mat outputImage =
        inputImage.clone();
    for (int row = 1; row < inputImage.rows - 1; ++row)
    {
        for (int col = 1; col < inputImage.cols - 1; ++col)
        {
            const int center =
                inputImage.at<unsigned char>(row, col);

            const int top =
                inputImage.at<unsigned char>(row - 1, col);

            const int bottom =
                inputImage.at<unsigned char>(row + 1, col);

            const int left =
                inputImage.at<unsigned char>(row, col - 1);

            const int right =
                inputImage.at<unsigned char>(row, col + 1);
            
             int sharpenedValue =
                5 * center;

            sharpenedValue =
                approximate::addSigned12(
                    sharpenedValue,
                    -top,
                    config.subtractTop
                );

            sharpenedValue =
                approximate::addSigned12(
                    sharpenedValue,
                    -bottom,
                    config.subtractBottom
                );

            sharpenedValue =
                approximate::addSigned12(
                    sharpenedValue,
                    -left,
                    config.subtractLeft
                );

            sharpenedValue =
                approximate::addSigned12(
                    sharpenedValue,
                    -right,
                    config.subtractRight
                );
            
            if (sharpenedValue < 0)
            {
                sharpenedValue = 0;
            }
            else if (sharpenedValue > 255)
            {
                sharpenedValue = 255;
            }

            outputImage.at<unsigned char>(row, col) =
            static_cast<unsigned char>(sharpenedValue);
        }
    }

    return outputImage;
}


cv::Mat sharpenRegionalApproximate(
    const cv::Mat& inputImage,
    const cv::Mat& mask,
    const SharpenApproxConfig& importantConfig,
    const SharpenApproxConfig& nonImportantConfig
)
{
    if (inputImage.empty())
    {
        throw std::runtime_error(
            "Input image is empty."
        );
    }

    if (mask.empty())
    {
        throw std::runtime_error(
            "Mask image is empty."
        );
    }

    if (inputImage.type() != CV_8UC1)
    {
        throw std::runtime_error(
            "Regional sharpening requires an 8-bit grayscale image."
        );
    }

    if (mask.type() != CV_8UC1)
    {
        throw std::runtime_error(
            "Mask must be an 8-bit grayscale image."
        );
    }

    if (inputImage.rows != mask.rows ||
        inputImage.cols != mask.cols)
    {
        throw std::runtime_error(
            "Input image and mask dimensions do not match."
        );
    }

    cv::Mat outputImage =
        inputImage.clone();

    for (int row = 1; row < inputImage.rows - 1; ++row)
    {
        for (int col = 1; col < inputImage.cols - 1; ++col)
        {
            const bool isImportant =
                region_mask::isImportantPixel(
                    mask,
                    row,
                    col
                );

            const SharpenApproxConfig& selectedConfig =
                isImportant
                    ? importantConfig
                    : nonImportantConfig;
            
            const int center =
            inputImage.at<unsigned char>(row, col);

            const int top =
            inputImage.at<unsigned char>(row - 1, col);

            const int bottom =
            inputImage.at<unsigned char>(row + 1, col);

            const int left =
            inputImage.at<unsigned char>(row, col - 1);

            const int right =
            inputImage.at<unsigned char>(row, col + 1);

            int sharpenedValue =
                5 * center;

            sharpenedValue =
                approximate::addSigned12(
                    sharpenedValue,
                    -top,
                    selectedConfig.subtractTop
                );

            sharpenedValue =
                approximate::addSigned12(
                    sharpenedValue,
                    -bottom,
                    selectedConfig.subtractBottom
                );

            sharpenedValue =
                approximate::addSigned12(
                    sharpenedValue,
                    -left,
                    selectedConfig.subtractLeft
                );

            sharpenedValue =
                approximate::addSigned12(
                    sharpenedValue,
                    -right,
                    selectedConfig.subtractRight
                );

            if (sharpenedValue < 0)
            {
                sharpenedValue = 0;
            }
            else if (sharpenedValue > 255)
            {
                sharpenedValue = 255;
            }

            outputImage.at<unsigned char>(row, col) =
                static_cast<unsigned char>(sharpenedValue);
        }
    }
    
    return outputImage;
}

cv::Mat sharpenIntervalApproximate(
    const cv::Mat& inputImage,
    int lower,
    int upper,
    const SharpenApproxConfig& approximateConfig
)
{
    if (inputImage.empty())
    {
        throw std::runtime_error(
            "Input image is empty."
        );
    }

    if (inputImage.type() != CV_8UC1)
    {
        throw std::runtime_error(
            "Interval sharpening requires an 8-bit grayscale image."
        );
    }

    if (lower < 0 || upper > 255 || lower > upper)
    {
        throw std::runtime_error(
            "Invalid trigger interval."
        );
    }

    cv::Mat outputImage =
        inputImage.clone();

    for (int row = 1; row < inputImage.rows - 1; ++row)
    {
        for (int col = 1; col < inputImage.cols - 1; ++col)
        {
            const int center =
                inputImage.at<unsigned char>(row, col);

            const int top =
                inputImage.at<unsigned char>(row - 1, col);

            const int bottom =
                inputImage.at<unsigned char>(row + 1, col);

            const int left =
                inputImage.at<unsigned char>(row, col - 1);

            const int right =
                inputImage.at<unsigned char>(row, col + 1);

            int sharpenedValue = 0;

            if (center >= lower && center <= upper)
            {
                // 区间内：执行近似计算
                sharpenedValue = 5 * center;

                sharpenedValue =
                    approximate::addSigned12(
                        sharpenedValue,
                        -top,
                        approximateConfig.subtractTop
                    );

                sharpenedValue =
                    approximate::addSigned12(
                        sharpenedValue,
                        -bottom,
                        approximateConfig.subtractBottom
                    );

                sharpenedValue =
                    approximate::addSigned12(
                        sharpenedValue,
                        -left,
                        approximateConfig.subtractLeft
                    );

                sharpenedValue =
                    approximate::addSigned12(
                        sharpenedValue,
                        -right,
                        approximateConfig.subtractRight
                    );
            }
            else
            {
                // 区间外：执行精确计算
                sharpenedValue =
                      5 * center
                    - top
                    - bottom
                    - left
                    - right;
            }

            if (sharpenedValue < 0)
            {
                sharpenedValue = 0;
            }
            else if (sharpenedValue > 255)
            {
                sharpenedValue = 255;
            }

            outputImage.at<unsigned char>(row, col) =
                static_cast<unsigned char>(sharpenedValue);
        }
    }

    return outputImage;
}

}