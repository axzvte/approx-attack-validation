#pragma once

#include "core/add_node.hpp"
#include "core/add_sample.hpp"
#include "core/attack_config.hpp"

#include <opencv2/core.hpp>

#include <string>
#include <vector>


namespace core
{

class Application
{
public:

    virtual ~Application() = default;


    virtual std::string name() const = 0;


    virtual const std::vector<AddNode>&
    addNodes() const = 0;


    virtual cv::Mat runExact(
        const cv::Mat& inputImage
    ) const = 0;


    virtual cv::Mat runApprox(
        const cv::Mat& inputImage,
        const std::vector<AttackConfig>& configs
    ) const = 0;


    virtual void collectExactAddSamples(
        const cv::Mat& inputImage,
        const cv::Mat& roiMask,
        std::vector<AddSample>& samples
    ) const = 0;


    // 采集正常近似 Baseline 电路中的节点输入/输出。
    //
    // 这与 collectExactAddSamples 不同：
    // 后级节点的输入会包含前级 Baseline 近似误差传播。
    virtual void collectBaselineAddSamples(
        const cv::Mat& inputImage,
        const cv::Mat& roiMask,
        std::vector<AddSample>& samples
    ) const = 0;


    // 在任意当前配置下真实运行应用，并采集节点动态输入/输出。
    //
    // 这是多节点联合搜索的关键通用接口：
    // 上游节点配置变化后，下游节点的真实输入也会随之变化，
    // 因此不能只使用 Baseline 样本。
    virtual void collectConfiguredAddSamples(
        const cv::Mat& inputImage,
        const cv::Mat& roiMask,
        const std::vector<AttackConfig>& configs,
        std::vector<AddSample>& samples
    ) const = 0;
};

}