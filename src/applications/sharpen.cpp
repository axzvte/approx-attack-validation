#include "applications/sharpen.hpp"

#include "approximate/evoapprox_adapter.hpp"
#include "region/region_mask.hpp"

#include <algorithm>
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


SharpenApplication::SharpenApplication()
    :
    SharpenApplication(
        createDefaultBaselineConfig()
    )
{
}


SharpenApplication::SharpenApplication(
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
                nodeName(nodeId)
            }
        );
    }
}


SharpenApplication::BaselineConfig
SharpenApplication::createDefaultBaselineConfig()
{
    // Sharpening 只有 4 个 ADD/SUB 节点。
    // 第一版通用框架验证先沿用全 5RP Baseline；
    // 后续可像 DCT 一样单独做 Baseline sweep。
    return
        createAllApproximateBaselineConfig(
            approximate::ApproxUnitId::Add12se5RP
        );
}


SharpenApplication::BaselineConfig
SharpenApplication::createAllExactBaselineConfig()
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


SharpenApplication::BaselineConfig
SharpenApplication::createAllApproximateBaselineConfig(
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


const SharpenApplication::BaselineConfig&
SharpenApplication::baselineConfig() const
{
    return baselineConfig_;
}


std::string
SharpenApplication::name() const
{
    return "Image Sharpening";
}


const std::vector<core::AddNode>&
SharpenApplication::addNodes() const
{
    return addNodes_;
}


std::string
SharpenApplication::nodeName(
    int nodeId
)
{
    switch (nodeId)
    {
        case 0:
            return "A1";

        case 1:
            return "A2";

        case 2:
            return "A3";

        case 3:
            return "A4";
    }


    throw std::runtime_error(
        "Invalid sharpening ADD node ID."
    );
}


void SharpenApplication::validateInput(
    const cv::Mat& inputImage
)
{
    if (inputImage.empty())
    {
        throw std::runtime_error(
            "Sharpen input image is empty."
        );
    }


    if (inputImage.type() != CV_8UC1)
    {
        throw std::runtime_error(
            "Sharpen requires an 8-bit grayscale image."
        );
    }
}


void SharpenApplication::validateMask(
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
            "Sharpen ROI mask is invalid."
        );
    }
}


void SharpenApplication::validateConfiguration(
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
                "Sharpen configuration contains an invalid node ID."
            );
        }


        if (config.lower > config.upper)
        {
            throw std::runtime_error(
                "Sharpen configuration contains an invalid interval."
            );
        }


        if (
            !nodeIds.insert(
                config.nodeId
            ).second
        )
        {
            throw std::runtime_error(
                "Sharpen configuration contains duplicate node IDs."
            );
        }
    }
}


int SharpenApplication::runExactNode(
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


int SharpenApplication::runNode(
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


        int monitorValue = 0;


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


cv::Mat SharpenApplication::runInternal(
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
            const int center =
                inputImage.at<unsigned char>(
                    row,
                    col
                );


            const int top =
                inputImage.at<unsigned char>(
                    row - 1,
                    col
                );


            const int bottom =
                inputImage.at<unsigned char>(
                    row + 1,
                    col
                );


            const int left =
                inputImage.at<unsigned char>(
                    row,
                    col - 1
                );


            const int right =
                inputImage.at<unsigned char>(
                    row,
                    col + 1
                );


            Trace
                trace{};


            int a1 = 0;
            int a2 = 0;
            int a3 = 0;
            int a4 = 0;


            if (configs == nullptr)
            {
                a1 =
                    runExactNode(
                        0,
                        5 * center,
                        -top,
                        samples == nullptr
                        ?
                        nullptr
                        :
                        &trace
                    );


                a2 =
                    runExactNode(
                        1,
                        a1,
                        -bottom,
                        samples == nullptr
                        ?
                        nullptr
                        :
                        &trace
                    );


                a3 =
                    runExactNode(
                        2,
                        a2,
                        -left,
                        samples == nullptr
                        ?
                        nullptr
                        :
                        &trace
                    );


                a4 =
                    runExactNode(
                        3,
                        a3,
                        -right,
                        samples == nullptr
                        ?
                        nullptr
                        :
                        &trace
                    );
            }
            else
            {
                a1 =
                    runNode(
                        0,
                        5 * center,
                        -top,
                        activeConfigs,
                        samples == nullptr
                        ?
                        nullptr
                        :
                        &trace
                    );


                a2 =
                    runNode(
                        1,
                        a1,
                        -bottom,
                        activeConfigs,
                        samples == nullptr
                        ?
                        nullptr
                        :
                        &trace
                    );


                a3 =
                    runNode(
                        2,
                        a2,
                        -left,
                        activeConfigs,
                        samples == nullptr
                        ?
                        nullptr
                        :
                        &trace
                    );


                a4 =
                    runNode(
                        3,
                        a3,
                        -right,
                        activeConfigs,
                        samples == nullptr
                        ?
                        nullptr
                        :
                        &trace
                    );
            }


            outputImage.at<unsigned char>(
                row,
                col
            ) =
                static_cast<unsigned char>(
                    clampToByte(
                        a4
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


cv::Mat SharpenApplication::runExact(
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


cv::Mat SharpenApplication::runApprox(
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


void SharpenApplication::collectExactAddSamples(
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


void SharpenApplication::collectBaselineAddSamples(
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


void SharpenApplication::collectConfiguredAddSamples(
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
