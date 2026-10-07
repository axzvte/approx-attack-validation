#include "analysis/dct_multi_state_joint_search.hpp"

#include "applications/dct.hpp"
#include "applications/dct8_fixed_graph.hpp"
#include "approximate/evoapprox_adapter.hpp"

#include <opencv2/core.hpp>

#include <iostream>


int main()
{
    const auto baseline =
        applications::Dct8FixedGraph::
            createSparseApproximateBaselineConfig(
                {
                    25,
                    19,
                    13
                },
                approximate::ApproxUnitId::Add12se5RP
            );


    applications::DctApplication
        application(
            baseline
        );


    cv::Mat inputImage(
        16,
        16,
        CV_8UC1
    );


    for (int row = 0;
         row < inputImage.rows;
         ++row)
    {
        for (int col = 0;
             col < inputImage.cols;
             ++col)
        {
            inputImage.at<unsigned char>(
                row,
                col
            ) =
                static_cast<unsigned char>(
                    75
                    +
                    row
                    +
                    2 * col
                );
        }
    }


    cv::Mat roiMask =
        cv::Mat::zeros(
            inputImage.size(),
            CV_8UC1
        );


    roiMask(
        cv::Rect(
            0,
            0,
            inputImage.cols / 2,
            inputImage.rows
        )
    ).setTo(
        255
    );


    const analysis::AttackStructure
        structure =
    {
        {
            6,
            approximate::ApproxUnitId::Add12se5Z0,
            core::MonitorSignal::Input1
        },

        {
            20,
            approximate::ApproxUnitId::Add12se5QC,
            core::MonitorSignal::Input2
        }
    };


    analysis::DctMultiStateJointSearchOptions
        options;


    options.representativeIntervalCount =
        3;


    options.beamWidth =
        4;


    options.refinementRounds =
        1;


    options.globalPsnrThreshold =
        0.0;


    const auto result =
        analysis::
            DctMultiStateJointSearch::
                search(
                    application,
                    inputImage,
                    roiMask,
                    structure,
                    options
                );


    if (
        result.finalStates.empty()
        ||
        result.finalStates.size()
            >
            options.beamWidth
        ||
        result.layers.size()
            !=
            structure.size()
            *
            2
    )
    {
        std::cerr
            << "Unexpected DCT multi-state joint-search shape.\n";


        return 1;
    }


    for (const auto& layer : result.layers)
    {
        if (
            layer.inputStateCount == 0
            ||
            layer.expandedStateCount
                <
                layer.inputStateCount
            ||
            layer.retainedStateCount == 0
            ||
            layer.retainedStateCount
                >
                options.beamWidth
        )
        {
            std::cerr
                << "Invalid DCT multi-state joint-search layer statistics.\n";


            return 1;
        }
    }


    std::cout
        << "DCT multi-state joint search test passed.\n";


    return 0;
}
