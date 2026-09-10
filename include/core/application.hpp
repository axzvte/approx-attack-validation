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
};

}