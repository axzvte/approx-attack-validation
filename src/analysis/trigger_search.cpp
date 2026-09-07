#include "analysis/trigger_search.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>


namespace trigger_search
{

// ============================================================
// 只在当前 .cpp 文件内部使用的辅助内容
// ============================================================

namespace
{

constexpr int BIN_COUNT = 16;


// ------------------------------------------------------------
// 根据加法位置，取得对应的 input1 / input2 / output
// ------------------------------------------------------------
const intermediate_analysis::AdderValues& getAdderValues(
    const intermediate_analysis::SharpenSample& sample,
    AdderPosition position
)
{
    switch (position)
    {
        case AdderPosition::A1:
            return sample.values.a1;

        case AdderPosition::A2:
            return sample.values.a2;

        case AdderPosition::A3:
            return sample.values.a3;

        case AdderPosition::A4:
            return sample.values.a4;
    }

    throw std::runtime_error(
        "Invalid adder position."
    );
}


// ------------------------------------------------------------
// 判断当前两个输入是否满足二维触发范围
// ------------------------------------------------------------
bool isTriggered(
    const intermediate_analysis::AdderValues& values,
    const TriggerRange& range
)
{
    return
           values.input1 >= range.input1Lower
        && values.input1 <= range.input1Upper
        && values.input2 >= range.input2Lower
        && values.input2 <= range.input2Upper;
}


// ------------------------------------------------------------
// 将一个实际输入值映射到 0~15 的某个 bin
// ------------------------------------------------------------
int getBinIndex(
    int value,
    int minValue,
    int maxValue
)
{
    if (minValue == maxValue)
    {
        return 0;
    }

    const long long valueRange =
        static_cast<long long>(maxValue)
        - minValue
        + 1;


    int index =
        static_cast<int>(
            static_cast<long long>(
                value - minValue
            )
            * BIN_COUNT
            / valueRange
        );


    // 防止极端情况下越界
    if (index < 0)
    {
        index = 0;
    }

    if (index >= BIN_COUNT)
    {
        index = BIN_COUNT - 1;
    }


    return index;
}


// ------------------------------------------------------------
// 得到某个 bin 对应的实际数值下界
// ------------------------------------------------------------
int getBinLowerBound(
    int bin,
    int minValue,
    int maxValue
)
{
    const long long valueRange =
        static_cast<long long>(maxValue)
        - minValue
        + 1;


    return static_cast<int>(
        minValue
        +
        (
            static_cast<long long>(bin)
            * valueRange
            + BIN_COUNT - 1
        )
        / BIN_COUNT
    );
}


// ------------------------------------------------------------
// 得到某个 bin 对应的实际数值上界
// ------------------------------------------------------------
int getBinUpperBound(
    int bin,
    int minValue,
    int maxValue
)
{
    const long long valueRange =
        static_cast<long long>(maxValue)
        - minValue
        + 1;


    return static_cast<int>(
        minValue
        +
        (
            static_cast<long long>(bin + 1)
            * valueRange
            + BIN_COUNT - 1
        )
        / BIN_COUNT
        - 1
    );
}

}   // namespace



// ============================================================
// 给定一个二维范围，评价它的触发情况
// ============================================================

TriggerMetrics evaluateTriggerRange(
    const std::vector<intermediate_analysis::SharpenSample>& samples,
    AdderPosition position,
    const TriggerRange& range
)
{
    TriggerMetrics metrics{};


    for (const auto& sample : samples)
    {
        // 统计 ROI / 非 ROI 总数
        if (sample.insideRoi)
        {
            ++metrics.totalRoi;
        }
        else
        {
            ++metrics.totalNonRoi;
        }


        // 获得当前加法位置的数据
        const auto& values =
            getAdderValues(
                sample,
                position
            );


        // 判断是否触发
        const bool triggered =
            isTriggered(
                values,
                range
            );


        if (!triggered)
        {
            continue;
        }


        // 统计触发位置属于 ROI 还是非 ROI
        if (sample.insideRoi)
        {
            ++metrics.roiTriggered;
        }
        else
        {
            ++metrics.nonRoiTriggered;
        }
    }


    // ROI Coverage
    if (metrics.totalRoi > 0)
    {
        metrics.roiCoverage =
            static_cast<double>(
                metrics.roiTriggered
            )
            /
            static_cast<double>(
                metrics.totalRoi
            );
    }


    // Precision
    const long long totalTriggered =
        metrics.roiTriggered
        + metrics.nonRoiTriggered;


    if (totalTriggered > 0)
    {
        metrics.precision =
            static_cast<double>(
                metrics.roiTriggered
            )
            /
            static_cast<double>(
                totalTriggered
            );
    }


    return metrics;
}



// ============================================================
// 搜索二维触发范围
// ============================================================

std::vector<TriggerCandidate> searchTriggerRanges(
    const std::vector<intermediate_analysis::SharpenSample>& samples,
    AdderPosition position
)
{
    if (samples.empty())
    {
        return {};
    }


    // ========================================================
    // 第一步：
    // 找到当前加法位置 input1 和 input2 的数值范围
    // ========================================================

    int input1Min =
        std::numeric_limits<int>::max();

    int input1Max =
        std::numeric_limits<int>::min();

    int input2Min =
        std::numeric_limits<int>::max();

    int input2Max =
        std::numeric_limits<int>::min();


    for (const auto& sample : samples)
    {
        const auto& values =
            getAdderValues(
                sample,
                position
            );


        input1Min =
            std::min(
                input1Min,
                values.input1
            );

        input1Max =
            std::max(
                input1Max,
                values.input1
            );


        input2Min =
            std::min(
                input2Min,
                values.input2
            );

        input2Max =
            std::max(
                input2Max,
                values.input2
            );
    }



    // ========================================================
    // 第二步：
    // 建立 16 × 16 二维网格
    // ========================================================

    long long roiGrid[BIN_COUNT][BIN_COUNT]{};

    long long nonRoiGrid[BIN_COUNT][BIN_COUNT]{};


    long long totalRoi = 0;

    long long totalNonRoi = 0;


    for (const auto& sample : samples)
    {
        const auto& values =
            getAdderValues(
                sample,
                position
            );


        const int bin1 =
            getBinIndex(
                values.input1,
                input1Min,
                input1Max
            );


        const int bin2 =
            getBinIndex(
                values.input2,
                input2Min,
                input2Max
            );


        if (sample.insideRoi)
        {
            ++roiGrid[bin1][bin2];

            ++totalRoi;
        }
        else
        {
            ++nonRoiGrid[bin1][bin2];

            ++totalNonRoi;
        }
    }



    // ========================================================
    // 第三步：
    // 枚举所有二维矩形范围
    // ========================================================

    std::vector<TriggerCandidate> allCandidates;


    for (int x1LowerBin = 0;
         x1LowerBin < BIN_COUNT;
         ++x1LowerBin)
    {
        for (int x1UpperBin = x1LowerBin;
             x1UpperBin < BIN_COUNT;
             ++x1UpperBin)
        {
            for (int x2LowerBin = 0;
                 x2LowerBin < BIN_COUNT;
                 ++x2LowerBin)
            {
                for (int x2UpperBin = x2LowerBin;
                     x2UpperBin < BIN_COUNT;
                     ++x2UpperBin)
                {
                    long long roiTriggered = 0;

                    long long nonRoiTriggered = 0;


                    // ----------------------------------------
                    // 统计当前矩形中所有 bin
                    // ----------------------------------------

                    for (int bin1 = x1LowerBin;
                         bin1 <= x1UpperBin;
                         ++bin1)
                    {
                        for (int bin2 = x2LowerBin;
                             bin2 <= x2UpperBin;
                             ++bin2)
                        {
                            roiTriggered +=
                                roiGrid[bin1][bin2];


                            nonRoiTriggered +=
                                nonRoiGrid[bin1][bin2];
                        }
                    }


                    // 完全没有覆盖 ROI 的范围没有意义
                    if (roiTriggered == 0)
                    {
                        continue;
                    }


                    TriggerCandidate candidate{};


                    // ----------------------------------------
                    // 将 bin 范围转换回实际数值范围
                    // ----------------------------------------

                    candidate.range.input1Lower =
                        getBinLowerBound(
                            x1LowerBin,
                            input1Min,
                            input1Max
                        );


                    candidate.range.input1Upper =
                        getBinUpperBound(
                            x1UpperBin,
                            input1Min,
                            input1Max
                        );


                    candidate.range.input2Lower =
                        getBinLowerBound(
                            x2LowerBin,
                            input2Min,
                            input2Max
                        );


                    candidate.range.input2Upper =
                        getBinUpperBound(
                            x2UpperBin,
                            input2Min,
                            input2Max
                        );


                    // ----------------------------------------
                    // 保存统计结果
                    // ----------------------------------------

                    candidate.metrics.roiTriggered =
                        roiTriggered;


                    candidate.metrics.nonRoiTriggered =
                        nonRoiTriggered;


                    candidate.metrics.totalRoi =
                        totalRoi;


                    candidate.metrics.totalNonRoi =
                        totalNonRoi;


                    candidate.metrics.roiCoverage =
                        static_cast<double>(
                            roiTriggered
                        )
                        /
                        static_cast<double>(
                            totalRoi
                        );


                    const long long totalTriggered =
                        roiTriggered
                        + nonRoiTriggered;


                    candidate.metrics.precision =
                        static_cast<double>(
                            roiTriggered
                        )
                        /
                        static_cast<double>(
                            totalTriggered
                        );


                    allCandidates.push_back(
                        candidate
                    );
                }
            }
        }
    }



    // ========================================================
    // 第四步：
    // Coverage 从高到低排序
    //
    // Coverage 相同时：
    // Precision 从高到低排序
    // ========================================================

    std::sort(
        allCandidates.begin(),
        allCandidates.end(),

        [](const TriggerCandidate& a,
           const TriggerCandidate& b)
        {
            if (a.metrics.roiCoverage
                != b.metrics.roiCoverage)
            {
                return
                    a.metrics.roiCoverage
                    >
                    b.metrics.roiCoverage;
            }


            return
                a.metrics.precision
                >
                b.metrics.precision;
        }
    );



    // ========================================================
    // 第五步：
    // 提取 Pareto 候选
    //
    // 如果 Coverage 已经越来越低，
    // Precision 也没有变高，
    // 那么这个候选没有保留价值。
    // ========================================================

    std::vector<TriggerCandidate> paretoCandidates;


    double bestPrecision = -1.0;


    for (const auto& candidate : allCandidates)
    {
        if (candidate.metrics.precision
            > bestPrecision)
        {
            paretoCandidates.push_back(
                candidate
            );


            bestPrecision =
                candidate.metrics.precision;
        }
    }



    return paretoCandidates;
}


}   // namespace trigger_search