#pragma once

#include <opencv2/core/mat.hpp>
#include <string>

#include <vector>

namespace intermediate_analysis
{

struct AdderValues
{
    int input1;
    int input2;
    int output;
};


struct SharpenIntermediateValues
{
    AdderValues a1;
    AdderValues a2;
    AdderValues a3;
    AdderValues a4;
};


struct SharpenSample
{
    int row;
    int col;

    bool insideRoi;

    SharpenIntermediateValues values;
};


SharpenIntermediateValues computeSharpenIntermediateValues(
    int center,
    int top,
    int bottom,
    int left,
    int right
);


std::vector<SharpenSample> collectSharpenSamples(
    const cv::Mat& image,
    const cv::Mat& roiMask
);

void saveSharpenSamplesCsv(
    const std::string& outputPath,
    const std::vector<SharpenSample>& samples
);

}