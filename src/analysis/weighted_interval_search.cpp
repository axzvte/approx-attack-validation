#include "analysis/weighted_interval_search.hpp"

#include <array>
#include <cmath>
#include <stdexcept>

namespace analysis
{

WeightedIntervalSearchResult searchWeightedInterval(
    const WeightedDistribution& distribution,
    double minAttackConcentration
)
{
    // ========================================================
    // 1. 参数检查
    // ========================================================

    if (minAttackConcentration < 0.0 ||
        minAttackConcentration > 1.0)
    {
        throw std::runtime_error(
            "Weighted interval search: "
            "minimum attack concentration must be between 0 and 1."
        );
    }

    if (distribution.totalAttackValue <= 0.0)
    {
        throw std::runtime_error(
            "Weighted interval search: total attack value is zero."
        );
    }

    if (distribution.totalInsidePixels <= 0)
    {
        throw std::runtime_error(
            "Weighted interval search: no ROI pixels."
        );
    }


    // ========================================================
    // 2. 建立三个前缀和
    // ========================================================

    std::array<double, 257> attackPrefix{};
    std::array<int, 257> insidePrefix{};
    std::array<int, 257> outsidePrefix{};


    for (int value = 0; value < 256; ++value)
    {
        attackPrefix[value + 1] =
            attackPrefix[value]
            + distribution.attackValue[value];

        insidePrefix[value + 1] =
            insidePrefix[value]
            + distribution.insideCount[value];

        outsidePrefix[value + 1] =
            outsidePrefix[value]
            + distribution.outsideCount[value];
    }


    // ========================================================
    // 3. 保存当前最佳结果
    // ========================================================

    WeightedIntervalSearchResult bestResult{};

    bool foundValidInterval = false;

    double bestAttackValueCoverage = -1.0;

    constexpr double EPSILON = 1e-12;


    // ========================================================
    // 4. 遍历所有连续区间 [lower, upper]
    // ========================================================

    for (int lower = 0; lower < 256; ++lower)
    {
        for (int upper = lower; upper < 256; ++upper)
        {
            // ------------------------------------------------
            // 4.1 区间覆盖的综合攻击价值
            // ------------------------------------------------

            const double capturedAttackValue =
                attackPrefix[upper + 1]
                - attackPrefix[lower];


            const double attackValueCoverage =
                capturedAttackValue
                / distribution.totalAttackValue;


            // ------------------------------------------------
            // 4.2 ROI 内有多少像素被触发
            // ------------------------------------------------

            const int insideTriggeredPixels =
                insidePrefix[upper + 1]
                - insidePrefix[lower];


            // ------------------------------------------------
            // 4.3 ROI 外有多少像素被触发
            // ------------------------------------------------

            const int outsideTriggeredPixels =
                outsidePrefix[upper + 1]
                - outsidePrefix[lower];


            const int totalTriggeredPixels =
                insideTriggeredPixels
                + outsideTriggeredPixels;


            if (totalTriggeredPixels == 0)
            {
                continue;
            }


            // ------------------------------------------------
            // 4.4 计算“攻击集中度”
            //
            // 所有被触发像素中，
            // 有多少真正位于重点 ROI 内
            // ------------------------------------------------

            const double attackConcentration =
                static_cast<double>(
                    insideTriggeredPixels
                )
                / totalTriggeredPixels;


            // 如果攻击太分散，直接淘汰
            if (attackConcentration <
                minAttackConcentration)
            {
                continue;
            }


            // ------------------------------------------------
            // 4.5 计算重点区域覆盖率
            //
            // ROI 内一共有多少像素被这个区间覆盖
            // ------------------------------------------------

            const double insideCoverage =
                static_cast<double>(
                    insideTriggeredPixels
                )
                / distribution.totalInsidePixels;


            // ------------------------------------------------
            // 4.6 判断当前区间是否更好
            // ------------------------------------------------

            bool better = false;


            // 第一优先级：
            // 覆盖更多综合攻击价值
            if (!foundValidInterval ||
                attackValueCoverage >
                    bestAttackValueCoverage + EPSILON)
            {
                better = true;
            }


            // 第二优先级：
            // 如果攻击价值覆盖基本一样，
            // 优先攻击更集中的区间
            else if (
                std::abs(
                    attackValueCoverage
                    - bestAttackValueCoverage
                ) <= EPSILON
                &&
                attackConcentration >
                    bestResult.attackConcentration
            )
            {
                better = true;
            }


            // 第三优先级：
            // 如果前两项也基本一样，
            // 优先覆盖更多 ROI
            else if (
                std::abs(
                    attackValueCoverage
                    - bestAttackValueCoverage
                ) <= EPSILON
                &&
                std::abs(
                    attackConcentration
                    - bestResult.attackConcentration
                ) <= EPSILON
                &&
                insideCoverage >
                    bestResult.insideCoverage
            )
            {
                better = true;
            }


            // 第四优先级：
            // 如果仍然一样，选择更窄的区间
            else if (
                std::abs(
                    attackValueCoverage
                    - bestAttackValueCoverage
                ) <= EPSILON
                &&
                std::abs(
                    attackConcentration
                    - bestResult.attackConcentration
                ) <= EPSILON
                &&
                std::abs(
                    insideCoverage
                    - bestResult.insideCoverage
                ) <= EPSILON
                &&
                (upper - lower) <
                    (bestResult.upper - bestResult.lower)
            )
            {
                better = true;
            }


            // ------------------------------------------------
            // 4.7 保存最佳结果
            // ------------------------------------------------

            if (better)
            {
                foundValidInterval = true;

                bestAttackValueCoverage =
                    attackValueCoverage;

                bestResult.lower =
                    lower;

                bestResult.upper =
                    upper;

                bestResult.capturedAttackValue =
                    capturedAttackValue;

                bestResult.attackValueCoverage =
                    attackValueCoverage;

                bestResult.insideTriggeredPixels =
                    insideTriggeredPixels;

                bestResult.outsideTriggeredPixels =
                    outsideTriggeredPixels;

                bestResult.insideCoverage =
                    insideCoverage;

                bestResult.attackConcentration =
                    attackConcentration;
            }
        }
    }


    // ========================================================
    // 5. 没找到满足条件的区间
    // ========================================================

    if (!foundValidInterval)
    {
        throw std::runtime_error(
            "Weighted interval search: "
            "no interval satisfies the attack concentration constraint."
        );
    }


    return bestResult;
}

}