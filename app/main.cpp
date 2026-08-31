#include "io/image_io.hpp"

#include "processing/exact_sharpen.hpp"
#include "processing/sharpen_approx_config.hpp"
#include "processing/approx_sharpen.hpp"

#include "metrics/psnr.hpp"

#include <exception>
#include <iostream>
#include <string>
#include <vector>


// ============================================================
// 保存一组待测试的区间
// ============================================================

struct IntervalCase
{
    std::string name;

    int lower;
    int upper;
};


int main()
{
    // ========================================================
    // 1. 文件路径
    // ========================================================

    const std::string inputImagePath =
        "data/input/test_1.jpg";

    // 这里使用我们已经得到的重点 ROI mask
    // 如果你的实际文件名不同，只改这一行
    const std::string roiMaskPath =
        "data/input/test_1_mask.png";

    const std::string exactOutputPath =
        "results/test_1_exact_sharpen.png";


    try
    {
        // ====================================================
        // 2. 读取输入灰度图
        // ====================================================

        const cv::Mat inputImage =
            image_io::loadGrayImage(
                inputImagePath
            );


        // ====================================================
        // 3. 读取重点 ROI mask
        // ====================================================

        const cv::Mat roiMask =
            image_io::loadGrayImage(
                roiMaskPath
            );


        // ====================================================
        // 4. 生成精确锐化结果
        //
        // 后面所有近似结果都和这一张进行比较
        // ====================================================

        const cv::Mat exactOutput =
            image_processing::sharpenExact(
                inputImage
            );


        image_io::saveImage(
            exactOutputPath,
            exactOutput
        );


        // ====================================================
        // 5. 固定同一套近似配置
        //
        // 这一阶段不做近似配置搜索。
        //
        // 四个区间全部使用同样的配置，
        // 这样可以单独观察不同区间带来的影响。
        // ====================================================

        const image_processing::SharpenApproxConfig
            intervalApproxConfig =
                image_processing::createImportantConfig();


        // ====================================================
        // 6. 四组待测试区间
        //
        // 分别对应之前：
        //
        // ROI 外允许触发：
        // 5%
        // 10%
        // 15%
        // 20%
        // ====================================================

        const std::vector<IntervalCase> intervalCases =
        {
            {
                "beta_005",
                113,
                136
            },

            {
                "beta_010",
                97,
                143
            },

            {
                "beta_015",
                82,
                149
            },

            {
                "beta_020",
                69,
                154
            }
        };


        // ====================================================
        // 7. 开始逐个测试
        // ====================================================

        std::cout
            << "========================================"
            << std::endl;

        std::cout
            << "Interval Approximation Test"
            << std::endl;

        std::cout
            << "========================================"
            << std::endl;


        for (const IntervalCase& currentCase : intervalCases)
        {
            // =================================================
            // 7.1 使用当前区间执行近似锐化
            //
            // 只有当前输入数据落入：
            //
            // [lower, upper]
            //
            // 时才使用固定近似配置。
            // =================================================

            const cv::Mat intervalApproxOutput =
                image_processing::sharpenIntervalApproximate(
                    inputImage,
                    currentCase.lower,
                    currentCase.upper,
                    intervalApproxConfig
                );


            // =================================================
            // 7.2 保存当前处理结果
            // =================================================

            const std::string outputPath =
                "results/test_1_interval_"
                + currentCase.name
                + ".png";


            image_io::saveImage(
                outputPath,
                intervalApproxOutput
            );


            // =================================================
            // 7.3 计算整幅图像 PSNR
            // =================================================

            const double globalPSNR =
                metrics::calculateGlobalPSNR(
                    exactOutput,
                    intervalApproxOutput
                );


            // =================================================
            // 7.4 计算重点 ROI PSNR
            // =================================================

            const double roiPSNR =
                metrics::calculateImportantRegionPSNR(
                    exactOutput,
                    intervalApproxOutput,
                    roiMask
                );


            // =================================================
            // 7.5 输出当前实验结果
            // =================================================

            std::cout
                << "Case: "
                << currentCase.name
                << std::endl;


            std::cout
                << "Interval: ["
                << currentCase.lower
                << ", "
                << currentCase.upper
                << "]"
                << std::endl;


            std::cout
                << "Saved image: "
                << outputPath
                << std::endl;


            std::cout
                << "Global PSNR: "
                << globalPSNR
                << " dB"
                << std::endl;


            std::cout
                << "ROI PSNR: "
                << roiPSNR
                << " dB"
                << std::endl;


            std::cout
                << "----------------------------------------"
                << std::endl;
        }
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