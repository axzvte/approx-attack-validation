#pragma once

#include "analysis/trigger_search.hpp"
#include "approximate/evoapprox_adapter.hpp"

#include <opencv2/core/mat.hpp>


namespace image_processing
{

cv::Mat sharpenTriggeredA2(
    const cv::Mat& inputImage,
    const trigger_search::TriggerRange& range,
    approximate::ApproxUnitId unit
);

}