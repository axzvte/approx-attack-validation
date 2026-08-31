#include "analysis/visual_importance.hpp"

#include <opencv2/imgproc.hpp>

#include <stdexcept>

namespace analysis
{

cv::Mat calculateVisualImportance(
    const cv::Mat& grayImage,
    const cv::Mat& roiMask
)
{
    // ========================================================
    // 1. 基本检查
    // ========================================================

    if (grayImage.empty())
    {
        throw std::runtime_error(
            "Visual importance: input image is empty."
        );
    }

    if (roiMask.empty())
    {
        throw std::runtime_error(
            "Visual importance: ROI mask is empty."
        );
    }

    if (grayImage.size() != roiMask.size())
    {
        throw std::runtime_error(
            "Visual importance: image and ROI mask sizes do not match."
        );
    }

    if (grayImage.type() != CV_8UC1)
    {
        throw std::runtime_error(
            "Visual importance: input image must be 8-bit grayscale."
        );
    }

    if (roiMask.type() != CV_8UC1)
    {
        throw std::runtime_error(
            "Visual importance: ROI mask must be 8-bit grayscale."
        );
    }


    // ========================================================
    // 2. 将 ROI mask 转成标准二值 mask
    // ========================================================

    cv::Mat binaryMask;

    cv::threshold(
        roiMask,
        binaryMask,
        0,
        255,
        cv::THRESH_BINARY
    );


    if (cv::countNonZero(binaryMask) == 0)
    {
        throw std::runtime_error(
            "Visual importance: ROI mask contains no valid pixels."
        );
    }


    // ========================================================
    // 3. Sobel 计算水平方向和垂直方向梯度
    // ========================================================

    cv::Mat gradientX;
    cv::Mat gradientY;

    cv::Sobel(
        grayImage,
        gradientX,
        CV_32F,
        1,
        0,
        3
    );

    cv::Sobel(
        grayImage,
        gradientY,
        CV_32F,
        0,
        1,
        3
    );


    // ========================================================
    // 4. 计算梯度幅值
    // ========================================================

    cv::Mat gradientMagnitude;

    cv::magnitude(
        gradientX,
        gradientY,
        gradientMagnitude
    );


    // ========================================================
    // 5. 找 ROI 内最大的梯度值
    // ========================================================

    double maxGradient = 0.0;

    cv::minMaxLoc(
        gradientMagnitude,
        nullptr,
        &maxGradient,
        nullptr,
        nullptr,
        binaryMask
    );


    // ========================================================
    // 6. 生成视觉重要性图
    // ========================================================

    cv::Mat visualImportance =
        cv::Mat::zeros(
            grayImage.size(),
            CV_32FC1
        );


    if (maxGradient > 0.0)
    {
        gradientMagnitude.convertTo(
            visualImportance,
            CV_32FC1,
            1.0 / maxGradient
        );
    }


    // ========================================================
    // 7. ROI 外部全部置 0
    // ========================================================

    cv::Mat outsideMask;

    cv::bitwise_not(
        binaryMask,
        outsideMask
    );

    visualImportance.setTo(
        0.0F,
        outsideMask
    );


    // ========================================================
    // 8. 防止极少数数值超过 1
    // ========================================================

    cv::threshold(
        visualImportance,
        visualImportance,
        1.0,
        1.0,
        cv::THRESH_TRUNC
    );


    return visualImportance;
}

}