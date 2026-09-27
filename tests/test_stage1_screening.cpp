#include "analysis/stage1_screening.hpp"

#include "approximate/evoapprox_adapter.hpp"
#include "core/attack_config.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>


int main()
{
    // =====================================================
    // 10 + 10 数据集
    // =====================================================

    analysis::TwoStageDataset
        dataset;


    for (int i = 0; i < 10; ++i)
    {
        dataset.stage1Images.push_back(
            {
                "stage1_image_"
                    +
                    std::to_string(i)
                    +
                    ".png",

                "stage1_mask_"
                    +
                    std::to_string(i)
                    +
                    ".png"
            }
        );


        dataset.stage2Images.push_back(
            {
                "stage2_image_"
                    +
                    std::to_string(i)
                    +
                    ".png",

                "stage2_mask_"
                    +
                    std::to_string(i)
                    +
                    ".png"
            }
        );
    }


    // =====================================================
    // 构造 5 个完整配置
    //
    // 同一个节点、同一个攻击单元、同一个 monitor input，
    // 只有触发区间不同。
    // =====================================================

    analysis::NodeSearchSpace
        node;


    node.nodeId =
        0;


    node.attackUnits =
    {
        approximate::ApproxUnitId::Add12se5Z0
    };


    node.monitorSpaces =
    {
        {
            core::MonitorInput::Input1,
            {
                { 0, 9 },
                { 10, 19 },
                { 20, 29 },
                { 30, 39 },
                { 40, 49 }
            }
        }
    };


    analysis::BruteForceSearchSpace
        searchSpace;


    searchSpace.nodes =
    {
        node
    };


    searchSpace.minAttackNodes =
        1;


    searchSpace.maxAttackNodes =
        1;


    // =====================================================
    // PSNR 类筛选测试
    //
    // 全图 PSNR：
    // 前 4 个配置 = 31 dB，正常
    // 第 5 个配置 = 28 dB，不正常
    //
    // ROI PSNR（越低破坏越严重）：
    // [20,29] 的 ROI PSNR = 12 dB，最差
    //
    // 因此：
    // 5 个配置都必须完整跑 10 张图 = 50 次评价
    // 全图正常的有 4 个
    // 前 20% = ceil(4 * 0.2) = 1 个
    // 最终应保留 [20,29]
    // =====================================================

    analysis::Stage1ScreeningOptions
        psnrOptions;


    psnrOptions.globalMetricThreshold =
        30.0;


    psnrOptions.globalMetricDirection =
        analysis::MetricDirection::
            HigherIsBetter;


    psnrOptions.roiMetricDirection =
        analysis::MetricDirection::
            HigherIsBetter;


    psnrOptions.keepWorstFraction =
        0.20;


    std::uint64_t
        psnrEvaluationCount =
            0;


    const auto psnrSelected =
        analysis::Stage1Screening::screen(
            dataset,
            searchSpace,

            [&psnrEvaluationCount](
                const analysis::AttackConfiguration&
                    configuration,
                std::size_t,
                const analysis::ImageCase&
            )
            {
                ++psnrEvaluationCount;


                const int lower =
                    configuration[0].lower;


                analysis::Stage1ImageMetrics
                    metrics;


                metrics.globalMetric =
                    (
                        lower == 40
                    )
                    ?
                    28.0
                    :
                    31.0;


                if (lower == 20)
                {
                    metrics.roiMetric =
                        12.0;
                }
                else if (lower == 30)
                {
                    metrics.roiMetric =
                        18.0;
                }
                else
                {
                    metrics.roiMetric =
                        22.0;
                }


                return metrics;
            },

            psnrOptions
        );


    if (psnrEvaluationCount != 50)
    {
        std::cerr
            << "Expected 50 Stage 1 evaluations, got "
            << psnrEvaluationCount
            << ".\n";

        return 1;
    }


    if (psnrSelected.size() != 1)
    {
        std::cerr
            << "Expected one PSNR candidate after 20 percent screening, got "
            << psnrSelected.size()
            << ".\n";

        return 1;
    }


    if (
        psnrSelected[0].configuration.size()
            !=
            1
        ||
        psnrSelected[0].configuration[0].lower
            !=
            20
        ||
        psnrSelected[0].configuration[0].upper
            !=
            29
    )
    {
        std::cerr
            << "PSNR screening did not select the worst ROI candidate.\n";

        return 1;
    }


    // =====================================================
    // MED 类筛选测试
    //
    // MED 越低越好，因此：
    // 全图 MED <= 5 为正常
    // ROI MED 越高说明攻击越严重
    //
    // [30,39] 的 ROI MED 最大，应被选中。
    // =====================================================

    analysis::Stage1ScreeningOptions
        medOptions;


    medOptions.globalMetricThreshold =
        5.0;


    medOptions.globalMetricDirection =
        analysis::MetricDirection::
            LowerIsBetter;


    medOptions.roiMetricDirection =
        analysis::MetricDirection::
            LowerIsBetter;


    medOptions.keepWorstFraction =
        0.20;


    const auto medSelected =
        analysis::Stage1Screening::screen(
            dataset,
            searchSpace,

            [](
                const analysis::AttackConfiguration&
                    configuration,
                std::size_t,
                const analysis::ImageCase&
            )
            {
                const int lower =
                    configuration[0].lower;


                analysis::Stage1ImageMetrics
                    metrics;


                metrics.globalMetric =
                    (
                        lower == 40
                    )
                    ?
                    7.0
                    :
                    4.0;


                if (lower == 30)
                {
                    metrics.roiMetric =
                        15.0;
                }
                else if (lower == 20)
                {
                    metrics.roiMetric =
                        10.0;
                }
                else
                {
                    metrics.roiMetric =
                        6.0;
                }


                return metrics;
            },

            medOptions
        );


    if (
        medSelected.size() != 1
        ||
        medSelected[0].configuration[0].lower
            !=
            30
        ||
        medSelected[0].configuration[0].upper
            !=
            39
    )
    {
        std::cerr
            << "MED screening did not select the worst ROI candidate.\n";

        return 1;
    }


    std::cout
        << "PSNR evaluations: "
        << psnrEvaluationCount
        << "\n";


    std::cout
        << "Selected PSNR interval: ["
        << psnrSelected[0].configuration[0].lower
        << ", "
        << psnrSelected[0].configuration[0].upper
        << "]\n";


    std::cout
        << "Selected MED interval: ["
        << medSelected[0].configuration[0].lower
        << ", "
        << medSelected[0].configuration[0].upper
        << "]\n";


    std::cout
        << "Stage1Screening test passed.\n";


    return 0;
}
