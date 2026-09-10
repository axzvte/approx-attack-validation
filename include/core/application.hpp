#pragma once

#include "core/add_node.hpp"
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


    // 应用名称
    virtual std::string name() const = 0;


    // 返回该应用所有可以近似化的 ADD/SUB 位置
    virtual const std::vector<AddNode>&
    addNodes() const = 0;


    // 全精确运行
    virtual cv::Mat runExact(
        const cv::Mat& inputImage
    ) const = 0;


    // 按照给定配置运行近似版本
    virtual cv::Mat runApprox(
        const cv::Mat& inputImage,
        const std::vector<AttackConfig>& configs
    ) const = 0;
};

}