#include "analysis/add_sample_collector.hpp"

#include <algorithm>


namespace analysis
{

void AddSampleCollector::collect(
    const core::Application& application,
    const cv::Mat& inputImage,
    const cv::Mat& roiMask
)
{
    application.collectExactAddSamples(
        inputImage,
        roiMask,
        samples_
    );
}


void AddSampleCollector::clear()
{
    samples_.clear();
}


const std::vector<core::AddSample>&
AddSampleCollector::samples() const
{
    return samples_;
}


std::size_t
AddSampleCollector::size() const
{
    return samples_.size();
}


std::size_t
AddSampleCollector::countForNode(
    int nodeId
) const
{
    return
        static_cast<std::size_t>(
            std::count_if(
                samples_.begin(),
                samples_.end(),

                [nodeId](
                    const core::AddSample& sample
                )
                {
                    return sample.nodeId == nodeId;
                }
            )
        );
}

}