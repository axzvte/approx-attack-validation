#include "processing/exact_sharpen.hpp"

#include <stdexcept>

namespace image_processing
{

cv::Mat sharpenExact(const cv::Mat& inputImage)
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
            "Exact sharpening requires an 8-bit grayscale image."
        );
    }

    cv::Mat outputImage = inputImage.clone();

    for (int row = 1;
         row < inputImage.rows - 1;
         ++row)
    {
        for (int col = 1;
             col < inputImage.cols - 1;
             ++col)
        {
            const int center =
                inputImage.at<unsigned char>(
                    row,
                    col
                );

            const int top =
                inputImage.at<unsigned char>(
                    row - 1,
                    col
                );

            const int bottom =
                inputImage.at<unsigned char>(
                    row + 1,
                    col
                );

            const int left =
                inputImage.at<unsigned char>(
                    row,
                    col - 1
                );

            const int right =
                inputImage.at<unsigned char>(
                    row,
                    col + 1
                );

            int sharpenedValue =
                  5 * center
                - top
                - bottom
                - left
                - right;

            if (sharpenedValue < 0)
            {
                sharpenedValue = 0;
            }
            else if (sharpenedValue > 255)
            {
                sharpenedValue = 255;
            }

            outputImage.at<unsigned char>(
                row,
                col
            ) = static_cast<unsigned char>(
                sharpenedValue
            );
        }
    }

    return outputImage;
}

}