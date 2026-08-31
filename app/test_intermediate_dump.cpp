#include <opencv2/opencv.hpp>

#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>


int main()
{
    // ========================================================
    // 1. 文件路径
    // ========================================================

    const std::string inputImagePath =
        "data/input/test_1.jpg";

    const std::string roiMaskPath =
        "data/input/test_1_mask.png";

    const std::string outputCsvPath =
        "results/test_1_intermediate_values.csv";


    try
    {
        // ====================================================
        // 2. 读取图像
        // ====================================================

        const cv::Mat image =
            cv::imread(
                inputImagePath,
                cv::IMREAD_GRAYSCALE
            );

        const cv::Mat roiMask =
            cv::imread(
                roiMaskPath,
                cv::IMREAD_GRAYSCALE
            );


        if (image.empty())
        {
            throw std::runtime_error(
                "Failed to load input image."
            );
        }


        if (roiMask.empty())
        {
            throw std::runtime_error(
                "Failed to load ROI mask."
            );
        }


        if (image.size() != roiMask.size())
        {
            throw std::runtime_error(
                "Image and ROI mask sizes do not match."
            );
        }


        // ====================================================
        // 3. 创建 CSV 文件
        // ====================================================

        std::ofstream csvFile(
            outputCsvPath
        );


        if (!csvFile.is_open())
        {
            throw std::runtime_error(
                "Failed to create CSV file."
            );
        }


        // ====================================================
        // 4. 写表头
        // ====================================================

        csvFile
            << "row,"
            << "col,"
            << "inside_roi,"

            << "C,"
            << "Top,"
            << "Bottom,"
            << "Left,"
            << "Right,"

            << "Z1_5C,"
            << "Z2_5C_Top,"
            << "Z3_5C_Top_Bottom,"
            << "Z4_5C_Top_Bottom_Left,"
            << "Y_raw,"

            << "C_minus_Top,"
            << "C_minus_Bottom,"
            << "C_minus_Left,"
            << "C_minus_Right,"

            << "Top_plus_Bottom,"
            << "Left_plus_Right,"
            << "Neighbor_sum,"

            << "Local_Z,"
            << "Abs_Local_Z"

            << "\n";


        long long totalPixels = 0;
        long long roiPixels = 0;


        // ====================================================
        // 5. 遍历所有非边界像素
        // ====================================================

        for (int row = 1;
             row < image.rows - 1;
             ++row)
        {
            for (int col = 1;
                 col < image.cols - 1;
                 ++col)
            {
                // --------------------------------------------
                // 原始像素和邻域
                // --------------------------------------------

                const int C =
                    image.at<unsigned char>(
                        row,
                        col
                    );

                const int Top =
                    image.at<unsigned char>(
                        row - 1,
                        col
                    );

                const int Bottom =
                    image.at<unsigned char>(
                        row + 1,
                        col
                    );

                const int Left =
                    image.at<unsigned char>(
                        row,
                        col - 1
                    );

                const int Right =
                    image.at<unsigned char>(
                        row,
                        col + 1
                    );


                // --------------------------------------------
                // ROI 标记
                // --------------------------------------------

                const bool insideRoi =
                    roiMask.at<unsigned char>(
                        row,
                        col
                    ) > 0;


                // =================================================
                // 6. 锐化逐步计算中的中间值
                //
                // Y = 5C - Top - Bottom - Left - Right
                // =================================================

                const int Z1 =
                    5 * C;

                const int Z2 =
                    Z1 - Top;

                const int Z3 =
                    Z2 - Bottom;

                const int Z4 =
                    Z3 - Left;

                const int Yraw =
                    Z4 - Right;


                // =================================================
                // 7. 中心与各邻居的差值
                // =================================================

                const int diffTop =
                    C - Top;

                const int diffBottom =
                    C - Bottom;

                const int diffLeft =
                    C - Left;

                const int diffRight =
                    C - Right;


                // =================================================
                // 8. 邻域组合
                // =================================================

                const int topBottom =
                    Top + Bottom;

                const int leftRight =
                    Left + Right;

                const int neighborSum =
                    Top
                    + Bottom
                    + Left
                    + Right;


                // =================================================
                // 9. 局部结构响应
                // =================================================

                const int localZ =
                    4 * C
                    - neighborSum;

                const int absLocalZ =
                    std::abs(
                        localZ
                    );


                // =================================================
                // 10. 写入 CSV
                // =================================================

                csvFile
                    << row << ","
                    << col << ","
                    << (insideRoi ? 1 : 0) << ","

                    << C << ","
                    << Top << ","
                    << Bottom << ","
                    << Left << ","
                    << Right << ","

                    << Z1 << ","
                    << Z2 << ","
                    << Z3 << ","
                    << Z4 << ","
                    << Yraw << ","

                    << diffTop << ","
                    << diffBottom << ","
                    << diffLeft << ","
                    << diffRight << ","

                    << topBottom << ","
                    << leftRight << ","
                    << neighborSum << ","

                    << localZ << ","
                    << absLocalZ

                    << "\n";


                ++totalPixels;

                if (insideRoi)
                {
                    ++roiPixels;
                }
            }
        }


        csvFile.close();


        // ====================================================
        // 11. 终端只输出简单信息
        // ====================================================

        std::cout
            << "Intermediate data saved to:"
            << std::endl;

        std::cout
            << outputCsvPath
            << std::endl;


        std::cout
            << "Total pixels: "
            << totalPixels
            << std::endl;


        std::cout
            << "ROI pixels: "
            << roiPixels
            << std::endl;


        std::cout
            << "Outside pixels: "
            << totalPixels - roiPixels
            << std::endl;
    }
    catch (const std::exception& error)
    {
        std::cerr
            << "Error: "
            << error.what()
            << std::endl;

        return 1;
    }


    return 0;
}