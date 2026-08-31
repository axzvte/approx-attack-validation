#include "analysis/application_response.hpp"

#include <opencv2/core.hpp>

#include <stdexcept>

namespace analysis
{

cv::Mat calculateApplicationResponse(
    const cv::Mat& inputImage,
    const cv::Mat& exactOutput
)
{
    // ========================================================
    // 1. 基本检查
    // ========================================================

    if (inputImage.empty())
    {
        throw std::runtime_error(
            "Application response: input image is empty."
        );
    }

    if (exactOutput.empty())
    {
        throw std::runtime_error(
            "Application response: exact output is empty."
        );
    }

    if (inputImage.size() != exactOutput.size())
    {
        throw std::runtime_error(
            "Application response: image sizes do not match."
        );
    }

    if (inputImage.type() != CV_8UC1 ||
        exactOutput.type() != CV_8UC1)
    {
        throw std::runtime_error(
            "Application response: images must be 8-bit grayscale."
        );
    }


    // ========================================================
    // 2. 计算输入与精确输出之间的绝对差值
    // ========================================================

    cv::Mat difference;

    cv::absdiff(
        inputImage,
        exactOutput,
        difference
    );


    // ========================================================
    // 3. 转换成 0~1 的浮点响应图
    // ========================================================

    cv::Mat response;

    difference.convertTo(
        response,
        CV_32FC1,
        1.0 / 255.0
    );


    return response;
}

}