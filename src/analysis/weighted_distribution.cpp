#include "analysis/weighted_distribution.hpp"

#include <stdexcept>

namespace analysis
{

WeightedDistribution calculateWeightedDistribution(
    const cv::Mat& grayImage,
    const cv::Mat& attackValueMap,
    const cv::Mat& roiMask
)
{
    // ========================================================
    // 1. 基本检查
    // ========================================================

    if (grayImage.empty())
    {
        throw std::runtime_error(
            "Weighted distribution: gray image is empty."
        );
    }

    if (attackValueMap.empty())
    {
        throw std::runtime_error(
            "Weighted distribution: attack value map is empty."
        );
    }

    if (roiMask.empty())
    {
        throw std::runtime_error(
            "Weighted distribution: ROI mask is empty."
        );
    }


    if (grayImage.size() != attackValueMap.size() ||
        grayImage.size() != roiMask.size())
    {
        throw std::runtime_error(
            "Weighted distribution: input sizes do not match."
        );
    }


    if (grayImage.type() != CV_8UC1)
    {
        throw std::runtime_error(
            "Weighted distribution: gray image must be CV_8UC1."
        );
    }


    if (attackValueMap.type() != CV_32FC1)
    {
        throw std::runtime_error(
            "Weighted distribution: attack value map must be CV_32FC1."
        );
    }


    if (roiMask.type() != CV_8UC1)
    {
        throw std::runtime_error(
            "Weighted distribution: ROI mask must be CV_8UC1."
        );
    }


    // ========================================================
    // 2. 初始化统计结果
    // ========================================================

    WeightedDistribution distribution{};

    distribution.totalAttackValue = 0.0;

    distribution.totalInsidePixels = 0;

    distribution.totalOutsidePixels = 0;


    // ========================================================
    // 3. 遍历整张图像
    // ========================================================

    for (int row = 0;
         row < grayImage.rows;
         ++row)
    {
        for (int col = 0;
             col < grayImage.cols;
             ++col)
        {
            // 当前像素的原始灰度值
            const unsigned char grayValue =
                grayImage.at<unsigned char>(
                    row,
                    col
                );


            // 判断当前位置是否位于 ROI 内
            const bool insideRoi =
                roiMask.at<unsigned char>(
                    row,
                    col
                ) > 0;


            if (insideRoi)
            {
                // --------------------------------------------
                // ROI 内
                // --------------------------------------------

                // ① 统计这个灰度值出现多少次
                ++distribution.insideCount[grayValue];

                ++distribution.totalInsidePixels;


                // ② 累加这个位置的综合攻击价值 W(x,y)
                const float value =
                    attackValueMap.at<float>(
                        row,
                        col
                    );


                distribution.attackValue[grayValue] +=
                    static_cast<double>(value);


                distribution.totalAttackValue +=
                    static_cast<double>(value);
            }
            else
            {
                // --------------------------------------------
                // ROI 外
                // --------------------------------------------

                ++distribution.outsideCount[grayValue];

                ++distribution.totalOutsidePixels;
            }
        }
    }


    return distribution;
}

}