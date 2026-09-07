#include "analysis/sobel_intermediate_analysis.hpp"

#include "region/region_mask.hpp"

#include <cstdlib>
#include <fstream>
#include <stdexcept>


namespace sobel_intermediate_analysis
{

// ============================================================
// 计算一个像素对应的 Sobel 中间运算值
//
// 3×3 邻域：
//
// p1  p2  p3
// p4  p5  p6
// p7  p8  p9
//
// p5 在 Sobel 中权重为 0，因此不参与计算。
// ============================================================

SobelIntermediateValues computeSobelIntermediateValues(
    int p1,
    int p2,
    int p3,
    int p4,
    int p6,
    int p7,
    int p8,
    int p9
)
{
    SobelIntermediateValues values{};


    // ========================================================
    // Gx
    //
    // Gx =
    // (p3 + 2*p6 + p9)
    // -
    // (p1 + 2*p4 + p7)
    // ========================================================


    // --------------------------------------------------------
    // X1 = p3 + 2*p6
    // --------------------------------------------------------

    values.x1.input1 =
        p3;

    values.x1.input2 =
        2 * p6;

    values.x1.output =
        values.x1.input1
        + values.x1.input2;


    // --------------------------------------------------------
    // X2 = X1 + p9
    // --------------------------------------------------------

    values.x2.input1 =
        values.x1.output;

    values.x2.input2 =
        p9;

    values.x2.output =
        values.x2.input1
        + values.x2.input2;


    // --------------------------------------------------------
    // X3 = p1 + 2*p4
    // --------------------------------------------------------

    values.x3.input1 =
        p1;

    values.x3.input2 =
        2 * p4;

    values.x3.output =
        values.x3.input1
        + values.x3.input2;


    // --------------------------------------------------------
    // X4 = X3 + p7
    // --------------------------------------------------------

    values.x4.input1 =
        values.x3.output;

    values.x4.input2 =
        p7;

    values.x4.output =
        values.x4.input1
        + values.x4.input2;


    // --------------------------------------------------------
    // X5 = X2 - X4
    //
    // 为了统一表示成加法器：
    //
    // X5 = X2 + (-X4)
    //
    // X5 的输出就是 Gx
    // --------------------------------------------------------

    values.x5.input1 =
        values.x2.output;

    values.x5.input2 =
        -values.x4.output;

    values.x5.output =
        values.x5.input1
        + values.x5.input2;



    // ========================================================
    // Gy
    //
    // Gy =
    // (p7 + 2*p8 + p9)
    // -
    // (p1 + 2*p2 + p3)
    // ========================================================


    // --------------------------------------------------------
    // Y1 = p7 + 2*p8
    // --------------------------------------------------------

    values.y1.input1 =
        p7;

    values.y1.input2 =
        2 * p8;

    values.y1.output =
        values.y1.input1
        + values.y1.input2;


    // --------------------------------------------------------
    // Y2 = Y1 + p9
    // --------------------------------------------------------

    values.y2.input1 =
        values.y1.output;

    values.y2.input2 =
        p9;

    values.y2.output =
        values.y2.input1
        + values.y2.input2;


    // --------------------------------------------------------
    // Y3 = p1 + 2*p2
    // --------------------------------------------------------

    values.y3.input1 =
        p1;

    values.y3.input2 =
        2 * p2;

    values.y3.output =
        values.y3.input1
        + values.y3.input2;


    // --------------------------------------------------------
    // Y4 = Y3 + p3
    // --------------------------------------------------------

    values.y4.input1 =
        values.y3.output;

    values.y4.input2 =
        p3;

    values.y4.output =
        values.y4.input1
        + values.y4.input2;


    // --------------------------------------------------------
    // Y5 = Y2 - Y4
    //
    // 统一表示为：
    //
    // Y5 = Y2 + (-Y4)
    //
    // Y5 的输出就是 Gy
    // --------------------------------------------------------

    values.y5.input1 =
        values.y2.output;

    values.y5.input2 =
        -values.y4.output;

    values.y5.output =
        values.y5.input1
        + values.y5.input2;



    // ========================================================
    // 最终梯度幅值
    //
    // M3 = |Gx| + |Gy|
    //
    // 注意：
    // 这里不进行 255 截断。
    //
    // 因为我们分析的是 DFG 内部真实运算数据，
    // M3 最大理论值可以达到 2040。
    // ========================================================

    values.m3.input1 =
        std::abs(
            values.x5.output
        );

    values.m3.input2 =
        std::abs(
            values.y5.output
        );

    values.m3.output =
        values.m3.input1
        + values.m3.input2;


    return values;
}



// ============================================================
// 遍历整张图像，采集每个像素的 Sobel 中间数据
// ============================================================

std::vector<SobelSample> collectSobelSamples(
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


    if (image.type() != CV_8UC1)
    {
        throw std::runtime_error(
            "Sobel analysis requires an 8-bit grayscale image."
        );
    }


    if (roiMask.type() != CV_8UC1)
    {
        throw std::runtime_error(
            "ROI mask must be an 8-bit grayscale image."
        );
    }


    if (image.size() != roiMask.size())
    {
        throw std::runtime_error(
            "Image and ROI mask sizes do not match."
        );
    }


    std::vector<SobelSample> samples;


    // Sobel 需要 3×3 邻域，
    // 所以第一行、最后一行、
    // 第一列、最后一列不参与计算。

    for (int row = 1;
         row < image.rows - 1;
         ++row)
    {
        for (int col = 1;
             col < image.cols - 1;
             ++col)
        {
            // ------------------------------------------------
            // 读取 3×3 邻域
            //
            // p1  p2  p3
            // p4  p5  p6
            // p7  p8  p9
            // ------------------------------------------------

            const int p1 =
                image.at<unsigned char>(
                    row - 1,
                    col - 1
                );

            const int p2 =
                image.at<unsigned char>(
                    row - 1,
                    col
                );

            const int p3 =
                image.at<unsigned char>(
                    row - 1,
                    col + 1
                );

            const int p4 =
                image.at<unsigned char>(
                    row,
                    col - 1
                );

            const int p6 =
                image.at<unsigned char>(
                    row,
                    col + 1
                );

            const int p7 =
                image.at<unsigned char>(
                    row + 1,
                    col - 1
                );

            const int p8 =
                image.at<unsigned char>(
                    row + 1,
                    col
                );

            const int p9 =
                image.at<unsigned char>(
                    row + 1,
                    col + 1
                );


            SobelSample sample{};


            sample.row =
                row;

            sample.col =
                col;


            // ------------------------------------------------
            // ROI 标签
            //
            // 注意：
            // ROI 这里只用于离线分析和验证。
            //
            // 后续真正硬件触发时不会使用 ROI mask。
            // ------------------------------------------------

            sample.insideRoi =
                region_mask::isImportantPixel(
                    roiMask,
                    row,
                    col
                );


            // ------------------------------------------------
            // 计算 11 个 ADD/SUB 运算位置
            // ------------------------------------------------

            sample.values =
                computeSobelIntermediateValues(
                    p1,
                    p2,
                    p3,
                    p4,
                    p6,
                    p7,
                    p8,
                    p9
                );


            samples.push_back(
                sample
            );
        }
    }


    return samples;
}



// ============================================================
// 将 Sobel 中间运算数据保存为 CSV
// ============================================================

void saveSobelSamplesCsv(
    const std::string& outputPath,
    const std::vector<SobelSample>& samples
)
{
    std::ofstream csvFile(
        outputPath
    );


    if (!csvFile.is_open())
    {
        throw std::runtime_error(
            "Failed to create Sobel CSV file: "
            + outputPath
        );
    }


    // ========================================================
    // CSV 表头
    // ========================================================

    csvFile
        << "row,"
        << "col,"
        << "inside_roi,"

        << "X1_input1,"
        << "X1_input2,"
        << "X1_output,"

        << "X2_input1,"
        << "X2_input2,"
        << "X2_output,"

        << "X3_input1,"
        << "X3_input2,"
        << "X3_output,"

        << "X4_input1,"
        << "X4_input2,"
        << "X4_output,"

        << "X5_input1,"
        << "X5_input2,"
        << "X5_output,"

        << "Y1_input1,"
        << "Y1_input2,"
        << "Y1_output,"

        << "Y2_input1,"
        << "Y2_input2,"
        << "Y2_output,"

        << "Y3_input1,"
        << "Y3_input2,"
        << "Y3_output,"

        << "Y4_input1,"
        << "Y4_input2,"
        << "Y4_output,"

        << "Y5_input1,"
        << "Y5_input2,"
        << "Y5_output,"

        << "M3_input1,"
        << "M3_input2,"
        << "M3_output"

        << "\n";


    // ========================================================
    // 保存每一个像素
    // ========================================================

    for (const auto& sample : samples)
    {
        csvFile
            << sample.row << ","
            << sample.col << ","

            << (sample.insideRoi ? 1 : 0)
            << ","


            // X1
            << sample.values.x1.input1 << ","
            << sample.values.x1.input2 << ","
            << sample.values.x1.output << ","


            // X2
            << sample.values.x2.input1 << ","
            << sample.values.x2.input2 << ","
            << sample.values.x2.output << ","


            // X3
            << sample.values.x3.input1 << ","
            << sample.values.x3.input2 << ","
            << sample.values.x3.output << ","


            // X4
            << sample.values.x4.input1 << ","
            << sample.values.x4.input2 << ","
            << sample.values.x4.output << ","


            // X5
            << sample.values.x5.input1 << ","
            << sample.values.x5.input2 << ","
            << sample.values.x5.output << ","


            // Y1
            << sample.values.y1.input1 << ","
            << sample.values.y1.input2 << ","
            << sample.values.y1.output << ","


            // Y2
            << sample.values.y2.input1 << ","
            << sample.values.y2.input2 << ","
            << sample.values.y2.output << ","


            // Y3
            << sample.values.y3.input1 << ","
            << sample.values.y3.input2 << ","
            << sample.values.y3.output << ","


            // Y4
            << sample.values.y4.input1 << ","
            << sample.values.y4.input2 << ","
            << sample.values.y4.output << ","


            // Y5
            << sample.values.y5.input1 << ","
            << sample.values.y5.input2 << ","
            << sample.values.y5.output << ","


            // M3
            << sample.values.m3.input1 << ","
            << sample.values.m3.input2 << ","
            << sample.values.m3.output

            << "\n";
    }
}


}   // namespace sobel_intermediate_analysis