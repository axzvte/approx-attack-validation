#include "analysis/intermediate_analysis.hpp"

#include "approximate/evoapprox_adapter.hpp"

#include "io/image_io.hpp"

#include "processing/approx_sharpen.hpp"
#include "processing/exact_sharpen.hpp"
#include "processing/sharpen_approx_config.hpp"

#include "region/region_mask.hpp"

#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>


int main()
{
    // =========================================================
    // 输入图片
    // =========================================================

    const std::string inputImagePath =
        "data/input/test_1.jpg";


    try
    {
        // =====================================================
        // 创建结果目录
        // =====================================================

        std::filesystem::create_directories(
            "results"
        );


        // =====================================================
        // 1. 读取输入图片
        // =====================================================

        const cv::Mat inputImage =
            image_io::loadGrayImage(
                inputImagePath
            );


        std::cout
            << "Input image size: "
            << inputImage.cols
            << " x "
            << inputImage.rows
            << "\n";


        // =====================================================
        // 2. 自动生成 PASCAL-S 统计 ROI
        // =====================================================

        const cv::Mat roiMask =
            region_mask::createStatisticalRoiMask(
                inputImage.cols,
                inputImage.rows
            );


        image_io::saveImage(
            "results/statistical_roi_mask.png",
            roiMask
        );


        std::cout
            << "Saved ROI mask: "
            << "results/statistical_roi_mask.png\n";


        // =====================================================
        // 3. 生成精确锐化结果
        //
        // 后面计算 PSNR 时仍然以精确结果作为参考
        // =====================================================

        const cv::Mat exactImage =
            image_processing::sharpenExact(
                inputImage
            );


        image_io::saveImage(
            "results/exact_sharpen.png",
            exactImage
        );


        // =====================================================
        // 4. 设置原始近似电路
        //
        // 当前固定：
        //
        // A1 -> 5RP
        // A2 -> 5RP
        // A3 -> 5RP
        // A4 -> 5RP
        // =====================================================

        const image_processing::SharpenApproxConfig baseConfig =
        {
            approximate::ApproxUnitId::Add12se5RP,
            approximate::ApproxUnitId::Add12se5RP,
            approximate::ApproxUnitId::Add12se5RP,
            approximate::ApproxUnitId::Add12se5RP
        };


        // =====================================================
        // 5. 生成原始近似锐化结果
        // =====================================================

        const cv::Mat approximateImage =
            image_processing::sharpenApproximate(
                inputImage,
                baseConfig
            );


        image_io::saveImage(
            "results/base_approx_sharpen.png",
            approximateImage
        );


        std::cout
            << "Saved base approximate image: "
            << "results/base_approx_sharpen.png\n";


        // =====================================================
        // 6. 采集原始近似电路内部数据
        //
        // A1 的近似输出 -> A2 输入
        // A2 的近似输出 -> A3 输入
        // A3 的近似输出 -> A4 输入
        //
        // 因此这里记录的是近似误差真实传播后的内部值
        // =====================================================

        const auto samples =
            intermediate_analysis::
                collectApproximateSharpenSamples(
                    inputImage,
                    roiMask,
                    baseConfig
                );


        // =====================================================
        // 7. 保存内部数据 CSV
        // =====================================================

        const std::string csvPath =
            "results/approx_internal_values.csv";


        intermediate_analysis::
            saveSharpenSamplesCsv(
                csvPath,
                samples
            );


        // =====================================================
        // 输出基本信息
        // =====================================================

        std::cout
            << "\nBase approximate configuration:\n"
            << "A1 = 5RP\n"
            << "A2 = 5RP\n"
            << "A3 = 5RP\n"
            << "A4 = 5RP\n";


        std::cout
            << "\nCollected samples: "
            << samples.size()
            << "\n";


        std::cout
            << "Saved internal values: "
            << csvPath
            << "\n";


        std::cout
            << "\nFinished.\n";


        return 0;
    }


    catch (const std::exception& error)
    {
        std::cerr
            << "Error: "
            << error.what()
            << "\n";

        return 1;
    }
}