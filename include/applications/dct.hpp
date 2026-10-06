#pragma once

#include "core/application.hpp"


namespace applications
{

class DctApplication
    : public core::Application
{
public:

    DctApplication();


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
    // configs 为空时等价于正常 Baseline（当前默认全部 5RP）。
    // 后续多节点区间优化可在配置变化后重新采样，
    // 使下游节点的区间空间反映上游误差传播后的真实输入。
    void collectConfiguredAddSamples(
        const cv::Mat& inputImage,
        const cv::Mat& roiMask,
        const std::vector<core::AttackConfig>& configs,
        std::vector<core::AddSample>& samples
    ) const;


private:

    std::vector<core::AddNode>
        addNodes_;
};

}