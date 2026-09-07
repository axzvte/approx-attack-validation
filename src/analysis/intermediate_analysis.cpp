#include "analysis/intermediate_analysis.hpp"
#include "region/region_mask.hpp"

#include <stdexcept>
#include <fstream>

namespace intermediate_analysis
{

SharpenIntermediateValues computeSharpenIntermediateValues(
    int center,
    int top,
    int bottom,
    int left,
    int right
)
{
    SharpenIntermediateValues values;


    // A1 = 5C + (-Top)
    values.a1.input1 =
        5 * center;

    values.a1.input2 =
        -top;

    values.a1.output =
        values.a1.input1
        + values.a1.input2;


    // A2 = A1 + (-Bottom)
    values.a2.input1 =
        values.a1.output;

    values.a2.input2 =
        -bottom;

    values.a2.output =
        values.a2.input1
        + values.a2.input2;


    // A3 = A2 + (-Left)
    values.a3.input1 =
        values.a2.output;

    values.a3.input2 =
        -left;

    values.a3.output =
        values.a3.input1
        + values.a3.input2;


    // A4 = A3 + (-Right)
    values.a4.input1 =
        values.a3.output;

    values.a4.input2 =
        -right;

    values.a4.output =
        values.a4.input1
        + values.a4.input2;


    return values;
}


std::vector<SharpenSample> collectSharpenSamples(
    const cv::Mat& image,
    const cv::Mat& roiMask
)
{
    if (image.empty())
    {
        throw std::runtime_error(
            "Input image is empty."
        );
    }

    if (roiMask.empty())
    {
        throw std::runtime_error(
            "ROI mask is empty."
        );
    }

    if (image.size() != roiMask.size())
    {
        throw std::runtime_error(
            "Image and ROI mask sizes do not match."
        );
    }


    std::vector<SharpenSample> samples;


    for (int row = 1;
         row < image.rows - 1;
         ++row)
    {
        for (int col = 1;
             col < image.cols - 1;
             ++col)
        {
            const int center =
                image.at<unsigned char>(
                    row,
                    col
                );

            const int top =
                image.at<unsigned char>(
                    row - 1,
                    col
                );

            const int bottom =
                image.at<unsigned char>(
                    row + 1,
                    col
                );

            const int left =
                image.at<unsigned char>(
                    row,
                    col - 1
                );

            const int right =
                image.at<unsigned char>(
                    row,
                    col + 1
                );


            SharpenSample sample;

            sample.row = row;
            sample.col = col;

            sample.insideRoi =
                region_mask::isImportantPixel(
                    roiMask,
                    row,
                    col
                );

            sample.values =
                computeSharpenIntermediateValues(
                    center,
                    top,
                    bottom,
                    left,
                    right
                );


            samples.push_back(
                sample
            );
        }
    }


    return samples;
}

void saveSharpenSamplesCsv(
    const std::string& outputPath,
    const std::vector<SharpenSample>& samples
)
{
    std::ofstream csvFile(outputPath);

    if (!csvFile.is_open())
    {
        throw std::runtime_error(
            "Failed to create CSV file: " + outputPath
        );
    }


    // CSV 表头
    csvFile
        << "row,"
        << "col,"
        << "inside_roi,"

        << "A1_input1,"
        << "A1_input2,"
        << "A1_output,"

        << "A2_input1,"
        << "A2_input2,"
        << "A2_output,"

        << "A3_input1,"
        << "A3_input2,"
        << "A3_output,"

        << "A4_input1,"
        << "A4_input2,"
        << "A4_output"

        << "\n";


    // 逐个保存像素的数据
    for (const SharpenSample& sample : samples)
    {
        csvFile
            << sample.row << ","
            << sample.col << ","
            << (sample.insideRoi ? 1 : 0) << ","

            << sample.values.a1.input1 << ","
            << sample.values.a1.input2 << ","
            << sample.values.a1.output << ","

            << sample.values.a2.input1 << ","
            << sample.values.a2.input2 << ","
            << sample.values.a2.output << ","

            << sample.values.a3.input1 << ","
            << sample.values.a3.input2 << ","
            << sample.values.a3.output << ","

            << sample.values.a4.input1 << ","
            << sample.values.a4.input2 << ","
            << sample.values.a4.output

            << "\n";
    }
}

}