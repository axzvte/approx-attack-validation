#include "applications/conv.hpp"

#include "approximate/evoapprox_adapter.hpp"
#include "region/region_mask.hpp"

#include <algorithm>
#include <array>
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


int roundDivideBy16(
    int value
)
{
    if (value >= 0)
    {
        return
            (value + 8)
            /
            16;
    }


    return
        -
        (
            ((-value) + 8)
            /
            16
        );
}

}


ConvApplication::ConvApplication()
    :
    ConvApplication(
        createDefaultBaselineConfig()
    )
{
}


ConvApplication::ConvApplication(
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


ConvApplication::BaselineConfig
ConvApplication::createDefaultBaselineConfig()
{
    // 正式工作 Baseline：
    // Node 3(C4)、4(C5)、5(C6)、6(C7) 使用 5Z0，
    // 其余节点保持精确。
    //
    // 该配置由 Conv exhaustive baseline sweep 得到：
    // Mean Global PSNR = 35.039471 dB
    // Min  Global PSNR = 34.466574 dB
    return
        createSparseApproximateBaselineConfig(
            {
                3,
                4,
                5,
                6
            },
            approximate::ApproxUnitId::Add12se5Z0
        );
}


ConvApplication::BaselineConfig
ConvApplication::createAllExactBaselineConfig()
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


ConvApplication::BaselineConfig
ConvApplication::createAllApproximateBaselineConfig(
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


ConvApplication::BaselineConfig
ConvApplication::createSparseApproximateBaselineConfig(
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
                "Sparse Conv baseline contains an invalid node ID."
            );
        }


        if (seen[nodeId])
        {
            throw std::runtime_error(
                "Sparse Conv baseline contains duplicate node IDs."
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


const ConvApplication::BaselineConfig&
ConvApplication::baselineConfig() const
{
    return baselineConfig_;
}


std::string
ConvApplication::name() const
{
    return "3x3 Convolution";
}


const std::vector<core::AddNode>&
ConvApplication::addNodes() const
{
    return addNodes_;
}


std::string
ConvApplication::nodeName(
    int nodeId
)
{
    switch (nodeId)
    {
        case 0:
            return "C1";
        case 1:
            return "C2";
        case 2:
            return "C3";
        case 3:
            return "C4";
        case 4:
            return "C5";
        case 5:
            return "C6";
        case 6:
            return "C7";
        case 7:
            return "C8";
    }


    throw std::runtime_error(
        "Invalid Conv ADD node ID."
    );
}


void ConvApplication::validateInput(
    const cv::Mat& inputImage
)
{
    if (inputImage.empty())
    {
        throw std::runtime_error(
            "Conv input image is empty."
        );
    }


    if (inputImage.type() != CV_8UC1)
    {
        throw std::runtime_error(
            "Conv requires an 8-bit grayscale image."
        );
    }
}


void ConvApplication::validateMask(
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
            "Conv ROI mask is invalid."
        );
    }
}


void ConvApplication::validateConfiguration(
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
                "Conv configuration contains an invalid node ID."
            );
        }


        if (config.lower > config.upper)
        {
            throw std::runtime_error(
                "Conv configuration contains an invalid interval."
            );
        }


        if (
            !nodeIds.insert(
                config.nodeId
            ).second
        )
        {
            throw std::runtime_error(
                "Conv configuration contains duplicate node IDs."
            );
        }
    }
}


int ConvApplication::runExactNode(
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


int ConvApplication::runNode(
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


cv::Mat ConvApplication::runInternal(
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
        inputImage.clone();


    for (int row = 1;
         row < inputImage.rows - 1;
         ++row)
    {
        for (int col = 1;
             col < inputImage.cols - 1;
             ++col)
        {
            const int p1 =
                static_cast<int>(
                    inputImage.at<unsigned char>(
                        row - 1,
                        col - 1
                    )
                )
                -
                128;

            const int p2 =
                static_cast<int>(
                    inputImage.at<unsigned char>(
                        row - 1,
                        col
                    )
                )
                -
                128;

            const int p3 =
                static_cast<int>(
                    inputImage.at<unsigned char>(
                        row - 1,
                        col + 1
                    )
                )
                -
                128;

            const int p4 =
                static_cast<int>(
                    inputImage.at<unsigned char>(
                        row,
                        col - 1
                    )
                )
                -
                128;

            const int p5 =
                static_cast<int>(
                    inputImage.at<unsigned char>(
                        row,
                        col
                    )
                )
                -
                128;

            const int p6 =
                static_cast<int>(
                    inputImage.at<unsigned char>(
                        row,
                        col + 1
                    )
                )
                -
                128;

            const int p7 =
                static_cast<int>(
                    inputImage.at<unsigned char>(
                        row + 1,
                        col - 1
                    )
                )
                -
                128;

            const int p8 =
                static_cast<int>(
                    inputImage.at<unsigned char>(
                        row + 1,
                        col
                    )
                )
                -
                128;

            const int p9 =
                static_cast<int>(
                    inputImage.at<unsigned char>(
                        row + 1,
                        col + 1
                    )
                )
                -
                128;


            // 3x3 kernel:
            //
            // [1 2 1]
            // [2 4 2] / 16
            // [1 2 1]
            //
            // 乘2/乘4作为精确移位，近似只发生在下面8个ADD节点。
            const std::array<int, 9>
                weighted =
            {
                p1,
                2 * p2,
                p3,
                2 * p4,
                4 * p5,
                2 * p6,
                p7,
                2 * p8,
                p9
            };


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


            // 平衡树，避免单链累加让中间值过早接近signed-12边界。
            const int c1 =
                run(
                    0,
                    weighted[0],
                    weighted[1]
                );


            const int c2 =
                run(
                    1,
                    weighted[2],
                    weighted[3]
                );


            const int c3 =
                run(
                    2,
                    weighted[4],
                    weighted[5]
                );


            const int c4 =
                run(
                    3,
                    weighted[6],
                    weighted[7]
                );


            const int c5 =
                run(
                    4,
                    c1,
                    c2
                );


            const int c6 =
                run(
                    5,
                    c3,
                    c4
                );


            const int c7 =
                run(
                    6,
                    c5,
                    c6
                );


            const int c8 =
                run(
                    7,
                    c7,
                    weighted[8]
                );


            outputImage.at<unsigned char>(
                row,
                col
            ) =
                static_cast<unsigned char>(
                    clampToByte(
                        roundDivideBy16(
                            c8
                        )
                        +
                        128
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


cv::Mat ConvApplication::runExact(
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


cv::Mat ConvApplication::runApprox(
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


void ConvApplication::collectExactAddSamples(
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


void ConvApplication::collectBaselineAddSamples(
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


void ConvApplication::collectConfiguredAddSamples(
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
