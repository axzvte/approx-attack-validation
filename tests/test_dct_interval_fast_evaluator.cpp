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
        approximate::addSigned12(
            sample.input1,
            sample.input2,
            approximate::ApproxUnitId::Add12se5RP
        );


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
                    core::MonitorInput::Input1,
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


    std::cout
        << "DCT interval fast evaluator test passed.\n";


    return 0;
}
