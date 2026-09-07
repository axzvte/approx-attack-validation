#pragma once

#include <opencv2/core/mat.hpp>

#include <vector>
#include <string>

namespace sobel_intermediate_analysis
{

struct AdderValues
{
    int input1;
    int input2;
    int output;
};


struct SobelIntermediateValues
{
    AdderValues x1;
    AdderValues x2;
    AdderValues x3;
    AdderValues x4;
    AdderValues x5;

    AdderValues y1;
    AdderValues y2;
    AdderValues y3;
    AdderValues y4;
    AdderValues y5;

    AdderValues m3;
};


struct SobelSample
{
    int row;
    int col;

    bool insideRoi;

    SobelIntermediateValues values;
};


SobelIntermediateValues computeSobelIntermediateValues(
    int p1,
    int p2,
    int p3,
    int p4,
    int p6,
    int p7,
    int p8,
    int p9
);


std::vector<SobelSample> collectSobelSamples(
    const cv::Mat& image,
    const cv::Mat& roiMask
);

void saveSobelSamplesCsv(
    const std::string& outputPath,
    const std::vector<SobelSample>& samples
);

}