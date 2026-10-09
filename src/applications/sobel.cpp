#include "applications/sobel.hpp"

#include "approximate/evoapprox_adapter.hpp"
#include "region/region_mask.hpp"

#include <algorithm>
#include <cstdlib>
#include <set>
#include <stdexcept>


namespace applications
{

namespace
{

int clampToByte(
    int value
)
{
    return
        std::max(
            0,
            std::min(
                255,
                value
            )
        );
}

}


SobelApplication::SobelApplication()
    :
    SobelApplication(
        createDefaultBaselineConfig()
    )
{
}


SobelApplication::SobelApplication(
    const BaselineConfig& baselineConfig
)
    :
    baselineConfig_(
        baselineConfig
    )
{
    addNodes_.reserve(
        kAddNodeCount
    );


    for (int nodeId = 0;
         nodeId < kAddNodeCount;
         ++nodeId)
    {
        addNodes_.push_back(
            core::AddNode{
                nodeId,
                nodeName(
                    nodeId
                )
            }
        );
    }
}


SobelApplication::BaselineConfig
SobelApplication::createDefaultBaselineConfig()
{
    // 第一版先使用全 5RP Baseline 跑通通用流程。
    // 是否需要减少近似节点数量，后续根据真实 Baseline PSNR 再决定。
    return
        createAllApproximateBaselineConfig(
            approximate::ApproxUnitId::Add12se5RP
        );
}


SobelApplication::BaselineConfig
SobelApplication::createAllExactBaselineConfig()
{
    BaselineConfig
        config{};


    for (auto& node : config)
    {
        node.exact =
            true;
    }


    return config;
}


SobelApplication::BaselineConfig
SobelApplication::createAllApproximateBaselineConfig(
    approximate::ApproxUnitId unit
)
{
    BaselineConfig
        config{};


    for (auto& node : config)
    {
        node.exact =
            false;

        node.unit =
            unit;
    }


    return config;
}


SobelApplication::BaselineConfig
SobelApplication::createSparseApproximateBaselineConfig(
    const std::vector<int>& approximateNodeIds,
    approximate::ApproxUnitId unit
)
{
    BaselineConfig
        config =
            createAllExactBaselineConfig();


    std::array<bool, kAddNodeCount>
        seen{};


    for (const int nodeId : approximateNodeIds)
    {
        if (
            nodeId < 0
            ||
            nodeId >= kAddNodeCount
        )
        {
            throw std::runtime_error(
                "Sparse Sobel baseline contains an invalid node ID."
            );
        }


        if (seen[nodeId])
        {
            throw std::runtime_error(
                "Sparse Sobel baseline contains duplicate node IDs."
            );
        }


        seen[nodeId] =
            true;


        config[
            static_cast<std::size_t>(
                nodeId
            )
        ].exact =
            false;


        config[
            static_cast<std::size_t>(
                nodeId
            )
        ].unit =
            unit;
    }


    return config;
}


const SobelApplication::BaselineConfig&
SobelApplication::baselineConfig() const
{
    return baselineConfig_;
}


std::string
SobelApplication::name() const
{
    return "Sobel";
}


const std::vector<core::AddNode>&
SobelApplication::addNodes() const
{
    return addNodes_;
}


std::string
SobelApplication::nodeName(
    int nodeId
)
{
    switch (nodeId)
    {
        case 0:
            return "X1";
        case 1:
            return "X2";
        case 2:
            return "X3";
        case 3:
            return "X4";
        case 4:
            return "X5_GX";
        case 5:
            return "Y1";
        case 6:
            return "Y2";
        case 7:
            return "Y3";
        case 8:
            return "Y4";
        case 9:
            return "Y5_GY";
        case 10:
            return "M3";
    }


    throw std::runtime_error(
        "Invalid Sobel ADD node ID."
    );
}


void SobelApplication::validateInput(
    const cv::Mat& inputImage
)
{
    if (inputImage.empty())
    {
        throw std::runtime_error(
            "Sobel input image is empty."
        );
    }


    if (inputImage.type() != CV_8UC1)
    {
        throw std::runtime_error(
            "Sobel requires an 8-bit grayscale image."
        );
    }
}


void SobelApplication::validateMask(
    const cv::Mat& inputImage,
    const cv::Mat& roiMask
)
{
    if (
        roiMask.empty()
        ||
        roiMask.type() != CV_8UC1
        ||
        roiMask.size() != inputImage.size()
    )
    {
        throw std::runtime_error(
            "Sobel ROI mask is invalid."
        );
    }
}


void SobelApplication::validateConfiguration(
    const std::vector<core::AttackConfig>& configs
)
{
    std::set<int>
        nodeIds;


    for (const auto& config : configs)
    {
        if (
            config.nodeId < 0
            ||
            config.nodeId >= kAddNodeCount
        )
        {
            throw std::runtime_error(
                "Sobel configuration contains an invalid node ID."
            );
        }


        if (config.lower > config.upper)
        {
            throw std::runtime_error(
                "Sobel configuration contains an invalid interval."
            );
        }


        if (
            !nodeIds.insert(
                config.nodeId
            ).second
        )
        {
            throw std::runtime_error(
                "Sobel configuration contains duplicate node IDs."
            );
        }
    }
}


int SobelApplication::runExactNode(
    int nodeId,
    int input1,
    int input2,
    Trace* trace
) const
{
    const int output =
        input1
        +
        input2;


    if (trace != nullptr)
    {
        (*trace)[nodeId] =
        {
            input1,
            input2,
            output,
            output
        };
    }


    return output;
}


int SobelApplication::runNode(
    int nodeId,
    int input1,
    int input2,
    const std::vector<core::AttackConfig>& configs,
    Trace* trace
) const
{
    const auto& baselineNode =
        baselineConfig_.at(
            static_cast<std::size_t>(
                nodeId
            )
        );


    const int baselineOutput =
        baselineNode.exact
        ?
        input1 + input2
        :
        approximate::addSigned12(
            input1,
            input2,
            baselineNode.unit
        );


    int output =
        baselineOutput;


    for (const auto& config : configs)
    {
        if (config.nodeId != nodeId)
        {
            continue;
        }


        int monitorValue =
            0;


        switch (config.monitorInput)
        {
            case core::MonitorSignal::Input1:
                monitorValue =
                    input1;
                break;

            case core::MonitorSignal::Input2:
                monitorValue =
                    input2;
                break;

            case core::MonitorSignal::BaselineOutput:
                monitorValue =
                    baselineOutput;
                break;
        }


        if (
            monitorValue >= config.lower
            &&
            monitorValue <= config.upper
        )
        {
            output =
                approximate::addSigned12(
                    input1,
                    input2,
                    config.unit
                );
        }


        break;
    }


    if (trace != nullptr)
    {
        (*trace)[nodeId] =
        {
            input1,
            input2,
            baselineOutput,
            output
        };
    }


    return output;
}


cv::Mat SobelApplication::runInternal(
    const cv::Mat& inputImage,
    const std::vector<core::AttackConfig>* configs,
    const cv::Mat* roiMask,
    std::vector<core::AddSample>* samples
) const
{
    validateInput(
        inputImage
    );


    if (roiMask != nullptr)
    {
        validateMask(
            inputImage,
            *roiMask
        );
    }


    const std::vector<core::AttackConfig>
        emptyConfigs;


    const auto& activeConfigs =
        configs == nullptr
        ?
        emptyConfigs
        :
        *configs;


    validateConfiguration(
        activeConfigs
    );


    cv::Mat outputImage =
        cv::Mat::zeros(
            inputImage.size(),
            CV_8UC1
        );


    for (int row = 1;
         row < inputImage.rows - 1;
         ++row)
    {
        for (int col = 1;
             col < inputImage.cols - 1;
             ++col)
        {
            const int p1 =
                inputImage.at<unsigned char>(
                    row - 1,
                    col - 1
                );

            const int p2 =
                inputImage.at<unsigned char>(
                    row - 1,
                    col
                );

            const int p3 =
                inputImage.at<unsigned char>(
                    row - 1,
                    col + 1
                );

            const int p4 =
                inputImage.at<unsigned char>(
                    row,
                    col - 1
                );

            const int p6 =
                inputImage.at<unsigned char>(
                    row,
                    col + 1
                );

            const int p7 =
                inputImage.at<unsigned char>(
                    row + 1,
                    col - 1
                );

            const int p8 =
                inputImage.at<unsigned char>(
                    row + 1,
                    col
                );

            const int p9 =
                inputImage.at<unsigned char>(
                    row + 1,
                    col + 1
                );


            Trace
                trace{};


            auto run =
                [&](
                    int nodeId,
                    int input1,
                    int input2
                )
                {
                    if (configs == nullptr)
                    {
                        return
                            runExactNode(
                                nodeId,
                                input1,
                                input2,
                                samples == nullptr
                                ?
                                nullptr
                                :
                                &trace
                            );
                    }


                    return
                        runNode(
                            nodeId,
                            input1,
                            input2,
                            activeConfigs,
                            samples == nullptr
                            ?
                            nullptr
                            :
                            &trace
                        );
                };


            const int x1 =
                run(
                    0,
                    p3,
                    2 * p6
                );


            const int x2 =
                run(
                    1,
                    x1,
                    p9
                );


            const int x3 =
                run(
                    2,
                    p1,
                    2 * p4
                );


            const int x4 =
                run(
                    3,
                    x3,
                    p7
                );


            const int x5 =
                run(
                    4,
                    x2,
                    -x4
                );


            const int y1 =
                run(
                    5,
                    p7,
                    2 * p8
                );


            const int y2 =
                run(
                    6,
                    y1,
                    p9
                );


            const int y3 =
                run(
                    7,
                    p1,
                    2 * p2
                );


            const int y4 =
                run(
                    8,
                    y3,
                    p3
                );


            const int y5 =
                run(
                    9,
                    y2,
                    -y4
                );


            const int m3 =
                run(
                    10,
                    std::abs(
                        x5
                    ),
                    std::abs(
                        y5
                    )
                );


            outputImage.at<unsigned char>(
                row,
                col
            ) =
                static_cast<unsigned char>(
                    clampToByte(
                        m3
                    )
                );


            if (samples != nullptr)
            {
                const double roiWeight =
                    region_mask::isImportantPixel(
                        *roiMask,
                        row,
                        col
                    )
                    ?
                    1.0
                    :
                    0.0;


                for (int nodeId = 0;
                     nodeId < kAddNodeCount;
                     ++nodeId)
                {
                    const auto& item =
                        trace[nodeId];


                    samples->push_back(
                        core::AddSample{
                            nodeId,
                            item.input1,
                            item.input2,
                            item.output,
                            roiWeight,
                            item.baselineOutput
                        }
                    );
                }
            }
        }
    }


    return outputImage;
}


cv::Mat SobelApplication::runExact(
    const cv::Mat& inputImage
) const
{
    return
        runInternal(
            inputImage,
            nullptr,
            nullptr,
            nullptr
        );
}


cv::Mat SobelApplication::runApprox(
    const cv::Mat& inputImage,
    const std::vector<core::AttackConfig>& configs
) const
{
    return
        runInternal(
            inputImage,
            &configs,
            nullptr,
            nullptr
        );
}


void SobelApplication::collectExactAddSamples(
    const cv::Mat& inputImage,
    const cv::Mat& roiMask,
    std::vector<core::AddSample>& samples
) const
{
    runInternal(
        inputImage,
        nullptr,
        &roiMask,
        &samples
    );
}


void SobelApplication::collectBaselineAddSamples(
    const cv::Mat& inputImage,
    const cv::Mat& roiMask,
    std::vector<core::AddSample>& samples
) const
{
    const std::vector<core::AttackConfig>
        emptyConfigs;


    runInternal(
        inputImage,
        &emptyConfigs,
        &roiMask,
        &samples
    );
}


void SobelApplication::collectConfiguredAddSamples(
    const cv::Mat& inputImage,
    const cv::Mat& roiMask,
    const std::vector<core::AttackConfig>& configs,
    std::vector<core::AddSample>& samples
) const
{
    runInternal(
        inputImage,
        &configs,
        &roiMask,
        &samples
    );
}

}
