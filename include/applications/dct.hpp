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


private:

    std::vector<core::AddNode>
        addNodes_;
};

}