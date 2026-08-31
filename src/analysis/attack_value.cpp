#include "analysis/attack_value.hpp"

#include <stdexcept>

namespace analysis
{

cv::Mat calculateAttackValue(
    const cv::Mat& visualImportance,
    const cv::Mat& applicationResponse
)
{
    // ========================================================
    // 1. 基本检查
    // ========================================================

    if (visualImportance.empty())
    {
        throw std::runtime_error(
            "Attack value: visual importance map is empty."
        );
    }

    if (applicationResponse.empty())
    {
        throw std::runtime_error(
            "Attack value: application response map is empty."
        );
    }

    if (visualImportance.size() != applicationResponse.size())
    {
        throw std::runtime_error(
            "Attack value: map sizes do not match."
        );
    }

    if (visualImportance.type() != CV_32FC1 ||
        applicationResponse.type() != CV_32FC1)
    {
        throw std::runtime_error(
            "Attack value: input maps must be CV_32FC1."
        );
    }


    // ========================================================
    // 2. 逐像素相乘
    // ========================================================

    cv::Mat attackValue =
        visualImportance.mul(
            applicationResponse
        );


    return attackValue;
}

}