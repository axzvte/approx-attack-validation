#include "processing/triggered_sharpen.hpp"

#include <stdexcept>


namespace image_processing
{

cv::Mat sharpenTriggeredA2(
    const cv::Mat& inputImage,
    const trigger_search::TriggerRange& range,
    approximate::ApproxUnitId unit
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
            "Triggered sharpening requires an 8-bit grayscale image."
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


            // ================================================
            // A1：保持精确
            // A1 = 5C + (-Top)
            // ================================================

            const int a1 =
                5 * center
                - top;


            // ================================================
            // A2 的两个真实输入
            // ================================================

            const int x1 =
                a1;

            const int x2 =
                -bottom;


            // ================================================
            // 精确 A2
            // ================================================

            const int exactA2 =
                x1 + x2;


            // ================================================
            // 近似 A2
            // ================================================

            const int approximateA2 =
                approximate::addSigned12(
                    x1,
                    x2,
                    unit
                );


            // ================================================
            // 二维触发条件
            //
            // 这里不使用 row / col / ROI mask
            // 只使用 A2 自己的两个输入
            // ================================================

            const bool trigger =
                   x1 >= range.input1Lower
                && x1 <= range.input1Upper
                && x2 >= range.input2Lower
                && x2 <= range.input2Upper;


            // ================================================
            // 软件中的 MUX
            //
            // trigger = true  → 近似输出
            // trigger = false → 精确输出
            // ================================================

            const int a2 =
                trigger
                ? approximateA2
                : exactA2;


            // ================================================
            // A3、A4继续保持精确
            // ================================================

            const int a3 =
                a2 - left;

            int a4 =
                a3 - right;


            // 输出限制在 0~255
            if (a4 < 0)
            {
                a4 = 0;
            }
            else if (a4 > 255)
            {
                a4 = 255;
            }


            outputImage.at<unsigned char>(
                row,
                col
            ) =
                static_cast<unsigned char>(
                    a4
                );
        }
    }


    return outputImage;
}

}