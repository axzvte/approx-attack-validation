#pragma once

#include "core/add_sample.hpp"
#include "core/application.hpp"

#include <opencv2/core.hpp>

#include <cstddef>
#include <vector>


namespace analysis
{

class AddSampleCollector
{
public:

    void collect(
        const core::Application& application,
        const cv::Mat& inputImage,
        const cv::Mat& roiMask
    );


    void clear();


    const std::vector<core::AddSample>&
    samples() const;


    std::size_t size() const;


    std::size_t countForNode(
        int nodeId
    ) const;


private:

    std::vector<core::AddSample>
        samples_;
};

}