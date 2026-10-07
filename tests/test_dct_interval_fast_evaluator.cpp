#include "analysis/dct_interval_fast_evaluator.hpp"

#include "approximate/evoapprox_adapter.hpp"

#include <cmath>
#include <iostream>
#include <vector>


namespace
{

bool nearlyEqual(
    double first,
    double second,
    double tolerance = 1.0e-9
)
{
    return
        std::abs(
            first
            -
            second
        )
        <=
        tolerance;
}


long double localDelta(
    const core::AddSample& sample,
    approximate::ApproxUnitId attackUnit
)
{
    const int baselineOutput =
        sample.baselineOutput;


    const int attackedOutput =
        approximate::addSigned12(
            sample.input1,
            sample.input2,
            attackUnit
        );


    const long double exact =
        static_cast<long double>(
            sample.input1
        )
        +
        static_cast<long double>(
            sample.input2
        );


    const long double baselineError =
        static_cast<long double>(
            baselineOutput
        )
        -
        exact;


    const long double attackError =
        static_cast<long double>(
            attackedOutput
        )
        -
        exact;


    return
        attackError
        *
        attackError
        -
        baselineError
        *
        baselineError;
}

}


int main()
{
    const std::vector<core::AddSample>
        samples =
    {
        {
            6,
            10,
            5,
            0,
            1.0
        },
        {
            6,
            20,
            5,
            0,
            0.0
        },
        {
            6,
            30,
            5,
            0,
            0.5
        },
        {
            7,
            99,
            1,
            0,
            1.0
        }
    };


    const auto unit =
        approximate::ApproxUnitId::Add12se5L8;


    const auto result =
        analysis::
            DctIntervalFastEvaluator::
                evaluateFromSamples(
                    samples,
                    6,
                    core::MonitorSignal::Input1,
                    unit
                );


    if (
        result.sampleCount != 3
        ||
        result.distinctMonitorValueCount != 3
        ||
        result.metrics.size() != 6
    )
    {
        std::cerr
            << "Unexpected DCT fast interval result size.\n";


        return 1;
    }


    // 最后一个区间应为 [30, 30]。
    const auto& singleValueMetric =
        result.metrics.back();


    if (
        singleValueMetric.interval.lower != 30
        ||
        singleValueMetric.interval.upper != 30
    )
    {
        std::cerr
            << "Unexpected final DCT interval.\n";


        return 1;
    }


    // ROI 总权重 = 1.0 + 0.0 + 0.5 = 1.5
    // Non-ROI 总权重 = 0.0 + 1.0 + 0.5 = 1.5
    //
    // [30,30] 只触发第三条样本，因此两边触发率都应为 0.5 / 1.5。
    if (
        !nearlyEqual(
            singleValueMetric.roiTriggerRate,
            1.0 / 3.0
        )
        ||
        !nearlyEqual(
            singleValueMetric.nonRoiTriggerRate,
            1.0 / 3.0
        )
    )
    {
        std::cerr
            << "Unexpected DCT fast interval trigger rate.\n";


        return 1;
    }


    const long double expectedDelta =
        localDelta(
            samples[2],
            unit
        );


    const double expectedNormalizedDelta =
        static_cast<double>(
            0.5L
            *
            expectedDelta
            /
            1.5L
        );


    if (
        !nearlyEqual(
            singleValueMetric.roiErrorChange,
            expectedNormalizedDelta
        )
        ||
        !nearlyEqual(
            singleValueMetric.nonRoiErrorChange,
            expectedNormalizedDelta
        )
        ||
        !nearlyEqual(
            singleValueMetric.redistributionScore,
            0.0
        )
    )
    {
        std::cerr
            << "Unexpected DCT fast interval error-change metric.\n";


        return 1;
    }


    // 全范围区间应触发目标节点的全部 ROI / Non-ROI 权重。
    const auto& fullRangeMetric =
        result.metrics[2];


    if (
        fullRangeMetric.interval.lower != 10
        ||
        fullRangeMetric.interval.upper != 30
        ||
        !nearlyEqual(
            fullRangeMetric.roiTriggerRate,
            1.0
        )
        ||
        !nearlyEqual(
            fullRangeMetric.nonRoiTriggerRate,
            1.0
        )
    )
    {
        std::cerr
            << "Unexpected full-range DCT fast interval metric.\n";


        return 1;
    }



    // BaselineOutput 也必须能作为 monitor signal。
    const auto outputMonitoredResult =
        analysis::
            DctIntervalFastEvaluator::
                evaluateFromSamples(
                    samples,
                    6,
                    core::MonitorSignal::BaselineOutput,
                    unit
                );

    if (
        outputMonitoredResult.sampleCount != 3
        ||
        outputMonitoredResult.metrics.empty()
    )
    {
        std::cerr
            << "Baseline-output fast interval evaluation failed.\n";
        return 1;
    }


    // =====================================================
    // 三类候选排序测试
    // =====================================================

    analysis::DctIntervalFastEvaluation
        rankingEvaluation;


    rankingEvaluation.metrics =
    {
        {
            {0, 0},
            0.0,
            0.0,
            5.0,
            4.0,
            1.0
        },
        {
            {1, 1},
            0.0,
            0.0,
            3.0,
            -6.0,
            9.0
        },
        {
            {2, 2},
            0.0,
            0.0,
            8.0,
            2.0,
            6.0
        }
    };


    const auto topRoi =
        analysis::
            DctIntervalFastEvaluator::
                selectTopMetrics(
                    rankingEvaluation,
                    analysis::DctIntervalFastRanking::RoiAttack,
                    1
                );


    const auto topCompensation =
        analysis::
            DctIntervalFastEvaluator::
                selectTopMetrics(
                    rankingEvaluation,
                    analysis::DctIntervalFastRanking::NonRoiCompensation,
                    1
                );


    const auto topRedistribution =
        analysis::
            DctIntervalFastEvaluator::
                selectTopMetrics(
                    rankingEvaluation,
                    analysis::DctIntervalFastRanking::Redistribution,
                    1
                );


    if (
        topRoi.size() != 1
        ||
        topRoi[0].interval.lower != 2
        ||
        topCompensation.size() != 1
        ||
        topCompensation[0].interval.lower != 1
        ||
        topRedistribution.size() != 1
        ||
        topRedistribution[0].interval.lower != 1
    )
    {
        std::cerr
            << "DCT fast interval ranking is incorrect.\n";


        return 1;
    }


    // =====================================================
    // Module 3-A：非支配前沿 + 代表区间
    // =====================================================

    analysis::DctIntervalFastEvaluation
        representativeEvaluation;


    representativeEvaluation.metrics =
    {
        // 非支配：ROI 最强端
        {
            {10, 10},
            0.0,
            0.0,
            10.0,
            10.0,
            0.0
        },

        // 非支配
        {
            {20, 20},
            0.0,
            0.0,
            8.0,
            6.0,
            2.0
        },

        // 被 [20,20] 全面压制：
        // ROI 更小，Non-ROI 代价反而更大。
        {
            {30, 30},
            0.0,
            0.0,
            7.0,
            7.0,
            0.0
        },

        // 非支配
        {
            {40, 40},
            0.0,
            0.0,
            5.0,
            2.0,
            3.0
        },

        // 被 [40,40] 全面压制。
        {
            {50, 50},
            0.0,
            0.0,
            4.0,
            3.0,
            1.0
        },

        // 非支配：Non-ROI 补偿端
        {
            {60, 60},
            0.0,
            0.0,
            2.0,
            -2.0,
            4.0
        }
    };


    const auto allFront =
        analysis::
            DctIntervalFastEvaluator::
                selectRepresentativeMetrics(
                    representativeEvaluation,
                    10
                );


    if (
        allFront.totalMetricCount != 6
        ||
        allFront.paretoMetricCount != 4
        ||
        allFront.representatives.size() != 4
    )
    {
        std::cerr
            << "Representative interval Pareto filtering is incorrect.\n";


        return 1;
    }


    for (const auto& metric : allFront.representatives)
    {
        if (
            metric.interval.lower == 30
            ||
            metric.interval.lower == 50
        )
        {
            std::cerr
                << "Dominated interval survived representative selection.\n";


            return 1;
        }
    }


    const auto limited =
        analysis::
            DctIntervalFastEvaluator::
                selectRepresentativeMetrics(
                    representativeEvaluation,
                    3
                );


    if (
        limited.representatives.size() != 3
        ||
        limited.representatives.front().interval.lower != 10
        ||
        limited.representatives.back().interval.lower != 60
    )
    {
        std::cerr
            << "Representative interval coverage is incorrect.\n";


        return 1;
    }


    std::cout
        << "DCT interval fast evaluator test passed.\n";


    return 0;
}
