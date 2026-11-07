#include "region/region_mask.hpp"

#include <algorithm>
#include <opencv2/imgproc.hpp>
#include <stdexcept>


namespace region_mask
{

bool isImportantPixel(
    const cv::Mat& mask,
    int row,
    int col
)
{
    const unsigned char pixelValue =
        mask.at<unsigned char>(
            row,
            col
        );

    return pixelValue >= 128;
}


cv::Mat resizeMaskToImage(
    const cv::Mat& mask,
    const cv::Mat& targetImage
)
{
    if (mask.empty())
    {
        throw std::runtime_error(
            "ROI mask is empty."
        );
    }


    if (targetImage.empty())
    {
        throw std::runtime_error(
            "Target image is empty."
        );
    }


    if (mask.type() != CV_8UC1)
    {
        throw std::runtime_error(
            "ROI mask must be CV_8UC1."
        );
    }


    if (
        mask.rows == targetImage.rows
        &&
        mask.cols == targetImage.cols
    )
    {
        return mask.clone();
    }


    cv::Mat resizedMask;


    cv::resize(
        mask,
        resizedMask,
        targetImage.size(),
        0.0,
        0.0,
        cv::INTER_NEAREST
    );


    return resizedMask;
}


cv::Mat createStatisticalRoiMask(
    int width,
    int height
)
{
    if (
        width <= 0
        ||
        height <= 0
    )
    {
        throw std::runtime_error(
            "Image width and height must be positive."
        );
    }


    // PASCAL-S 统计 ROI
    //
    // 原始 320×320 坐标系：
    //
    // x = [86, 230)
    // y = [108, 233)
    //
    // 注意：
    // 230 和 233 是右边界、下边界，
    // 本身不属于 ROI。

    constexpr int BASE_SIZE = 320;

    constexpr int ROI_X0 = 86;
    constexpr int ROI_X1 = 230;

    constexpr int ROI_Y0 = 108;
    constexpr int ROI_Y1 = 233;


    // 左上角使用向下取整
    const int x0 =
        static_cast<int>(
            static_cast<long long>(
                ROI_X0
            )
            *
            width
            /
            BASE_SIZE
        );

    const int y0 =
        static_cast<int>(
            static_cast<long long>(
                ROI_Y0
            )
            *
            height
            /
            BASE_SIZE
        );


    // 右下角使用向上取整
    int x1 =
        static_cast<int>(
            (
                static_cast<long long>(
                    ROI_X1
                )
                *
                width
                +
                BASE_SIZE
                -
                1
            )
            /
            BASE_SIZE
        );

    int y1 =
        static_cast<int>(
            (
                static_cast<long long>(
                    ROI_Y1
                )
                *
                height
                +
                BASE_SIZE
                -
                1
            )
            /
            BASE_SIZE
        );


    // 防止坐标超出图像范围
    x1 =
        std::min(
            x1,
            width
        );

    y1 =
        std::min(
            y1,
            height
        );


    // 创建全黑 mask
    //
    // 0   = Non-ROI
    // 255 = ROI
    cv::Mat mask =
        cv::Mat::zeros(
            height,
            width,
            CV_8UC1
        );


    // 把矩形 ROI 设置成白色
    mask(
        cv::Rect(
            x0,
            y0,
            x1 - x0,
            y1 - y0
        )
    ).setTo(255);


    return mask;
}

}