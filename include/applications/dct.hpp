#pragma once

#include "applications/dct8_fixed_graph.hpp"
#include "core/application.hpp"


namespace applications
{

class DctApplication
    : public core::Application
{
public:

    DctApplication();


    explicit DctApplication(
        const Dct8FixedGraph::BaselineConfig& baselineConfig
    );


    const Dct8FixedGraph::BaselineConfig&
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


    // 在当前 AttackConfig 下真实运行近似 DCT，
    // 并采集这一运行状态中的 ADD/SUB 动态输入与输出。
    //
    // configs 为空时等价于当前 DctApplication 持有的正常 Baseline。
    // 后续多节点区间优化可在配置变化后重新采样，
    // 使下游节点的区间空间反映上游误差传播后的真实输入。
    void collectConfiguredAddSamples(
        const cv::Mat& inputImage,
        const cv::Mat& roiMask,
        const std::vector<core::AttackConfig>& configs,
        std::vector<core::AddSample>& samples
    ) const override;


private:

    Dct8FixedGraph::BaselineConfig
        baselineConfig_;


    std::vector<core::AddNode>
        addNodes_;
};

}