#include "processing/approx_sharpen.hpp"
#include "approximate/evoapprox_adapter.hpp"

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

    for (int row = 1;
         row < inputImage.rows - 1;
         ++row)
    {
        for (int col = 1;
             col < inputImage.cols - 1;
             ++col)
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
                static_cast<unsigned char>(
                    sharpenedValue
                );
        }
    }

    return outputImage;
}

}
