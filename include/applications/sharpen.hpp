#pragma once

#include "core/application.hpp"

#include <array>
#include <vector>


namespace applications
{

class SharpenApplication
    : public core::Application
{
public:

    struct BaselineNodeConfig
    {
        bool exact = false;

        approximate::ApproxUnitId unit =
            approximate::ApproxUnitId::Add12se5RP;
    };


    static constexpr int kAddNodeCount =
        4;


    using BaselineConfig =
        std::array<
            BaselineNodeConfig,
            kAddNodeCount
        >;


    SharpenApplication();


    explicit SharpenApplication(
        const BaselineConfig& baselineConfig
    );


    static BaselineConfig
    createDefaultBaselineConfig();


    static BaselineConfig
    createAllExactBaselineConfig();


    static BaselineConfig
    createAllApproximateBaselineConfig(
        approximate::ApproxUnitId unit
    );


    const BaselineConfig&
    baselineConfig() const;


    std::string name() const override;


    const std::vector<core::AddNode>&
    addNodes() const override;


    cv::Mat runExact(
        const cv::Mat& inputImage
    ) const override;


    cv::Mat runApprox(
        const cv::Mat& inputImage,
        const std::vector<core::AttackConfig>& configs
    ) const override;


    void collectExactAddSamples(
        const cv::Mat& inputImage,
        const cv::Mat& roiMask,
        std::vector<core::AddSample>& samples
    ) const override;


    void collectBaselineAddSamples(
        const cv::Mat& inputImage,
        const cv::Mat& roiMask,
        std::vector<core::AddSample>& samples
    ) const override;


    void collectConfiguredAddSamples(
        const cv::Mat& inputImage,
        const cv::Mat& roiMask,
        const std::vector<core::AttackConfig>& configs,
        std::vector<core::AddSample>& samples
    ) const override;


    static std::string nodeName(
        int nodeId
    );


private:

    struct TraceItem
    {
        int input1 = 0;
        int input2 = 0;
        int baselineOutput = 0;
        int output = 0;
    };


    using Trace =
        std::array<
            TraceItem,
            kAddNodeCount
        >;


    BaselineConfig
        baselineConfig_;


    std::vector<core::AddNode>
        addNodes_;


    static void validateInput(
        const cv::Mat& inputImage
    );


    static void validateMask(
        const cv::Mat& inputImage,
        const cv::Mat& roiMask
    );


    static void validateConfiguration(
        const std::vector<core::AttackConfig>& configs
    );


    int runNode(
        int nodeId,
        int input1,
        int input2,
        const std::vector<core::AttackConfig>& configs,
        Trace* trace
    ) const;


    int runExactNode(
        int nodeId,
        int input1,
        int input2,
        Trace* trace
    ) const;


    cv::Mat runInternal(
        const cv::Mat& inputImage,
        const std::vector<core::AttackConfig>* configs,
        const cv::Mat* roiMask,
        std::vector<core::AddSample>* samples
    ) const;
};

}
