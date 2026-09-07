#include "processing/exact_sobel.hpp"

#include <cstdlib>
#include <stdexcept>

namespace image_processing
{

cv::Mat sobelExact(
    const cv::Mat& inputImage
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
            "Sobel requires an 8-bit grayscale image."
        );
    }


    // Sobel 输出初始化为全黑
    cv::Mat outputImage =
        cv::Mat::zeros(
            inputImage.size(),
            CV_8UC1
        );


    for (int row = 1;
         row < inputImage.rows - 1;
         ++row)
    {
        for (int col = 1;
             col < inputImage.cols - 1;
             ++col)
        {
            // 3×3 邻域
            //
            // p1 p2 p3
            // p4 p5 p6
            // p7 p8 p9

            const int p1 =
                inputImage.at<unsigned char>(
                    row - 1,
                    col - 1
                );

            const int p2 =
                inputImage.at<unsigned char>(
                    row - 1,
                    col
                );

            const int p3 =
                inputImage.at<unsigned char>(
                    row - 1,
                    col + 1
                );

            const int p4 =
                inputImage.at<unsigned char>(
                    row,
                    col - 1
                );

            const int p6 =
                inputImage.at<unsigned char>(
                    row,
                    col + 1
                );

            const int p7 =
                inputImage.at<unsigned char>(
                    row + 1,
                    col - 1
                );

            const int p8 =
                inputImage.at<unsigned char>(
                    row + 1,
                    col
                );

            const int p9 =
                inputImage.at<unsigned char>(
                    row + 1,
                    col + 1
                );


            // ============================================
            // Gx
            // ============================================

            const int x1 =
                p3 + 2 * p6;

            const int x2 =
                x1 + p9;

            const int x3 =
                p1 + 2 * p4;

            const int x4 =
                x3 + p7;

            const int gx =
                x2 - x4;


            // ============================================
            // Gy
            // ============================================

            const int y1 =
                p7 + 2 * p8;

            const int y2 =
                y1 + p9;

            const int y3 =
                p1 + 2 * p2;

            const int y4 =
                y3 + p3;

            const int gy =
                y2 - y4;


            // ============================================
            // 梯度幅值近似
            //
            // G = |Gx| + |Gy|
            // ============================================

            int magnitude =
                std::abs(gx)
                + std::abs(gy);


            // 输出图像是 8 bit，只能保存 0~255
            if (magnitude > 255)
            {
                magnitude = 255;
            }


            outputImage.at<unsigned char>(
                row,
                col
            ) =
                static_cast<unsigned char>(
                    magnitude
                );
        }
    }


    return outputImage;
}

}