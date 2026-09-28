#include "analysis/dct_stage1_evaluator.hpp"

#include "approximate/evoapprox_adapter.hpp"
#include "core/attack_config.hpp"

#include <opencv2/imgcodecs.hpp>

#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>


int main()
{
    const std::filesystem::path
        testDirectory =
            "test_dct_stage1_data";


    std::filesystem::create_directories(
        testDirectory
    );


    // =====================================================
    // 统一 ROI mask
    // =====================================================

    // 故意使用 8x8 的统一 mask，
    // Stage 1 测试图为 16x16。
    //
    // 用来验证程序会自动采用最近邻插值
    // 将统一 mask 缩放到每张输入图尺寸。
    cv::Mat mask =
        cv::Mat::zeros(
            8,
            8,
            CV_8UC1
        );


    mask(
        cv::Rect(
            2,
            2,
            4,
            4
        )
    ).setTo(255);


    const std::string maskPath =
        (
            testDirectory
            /
            "shared_roi_mask.png"
        ).string();


    if (
        !cv::imwrite(
            maskPath,
            mask
        )
    )
    {
        std::cerr
            << "Failed to write test ROI mask.\n";

        return 1;
    }


    // =====================================================
    // 构造 10 + 10 数据集
    //
    // Stage 2 这里只用于通过数据集结构检查，
    // DctStage1Evaluator 不会加载 Stage 2 图片。
    // =====================================================

    analysis::TwoStageDataset
        dataset;


    dataset.roiMaskPath =
        maskPath;


    for (int imageIndex = 0;
         imageIndex < 10;
         ++imageIndex)
    {
        cv::Mat image(
            16,
            16,
            CV_8UC1
        );


        for (int row = 0;
             row < image.rows;
             ++row)
        {
            for (int col = 0;
                 col < image.cols;
                 ++col)
            {
                image.at<unsigned char>(
                    row,
                    col
                ) =
                    static_cast<unsigned char>(
                        120
                        +
                        imageIndex
                        +
                        row / 2
                        +
                        col / 2
                    );
            }
        }


        const std::string stage1Path =
            (
                testDirectory
                /
                (
                    "stage1_"
                    +
                    std::to_string(
                        imageIndex
                    )
                    +
                    ".png"
                )
            ).string();


        if (
            !cv::imwrite(
                stage1Path,
                image
            )
        )
        {
            std::cerr
                << "Failed to write Stage 1 test image.\n";

            return 1;
        }


        dataset.stage1Images.push_back(
            {
                stage1Path
            }
        );


        dataset.stage2Images.push_back(
            {
                (
                    testDirectory
                    /
                    (
                        "stage2_"
                        +
                        std::to_string(
                            imageIndex
                        )
                        +
                        ".png"
                    )
                ).string()
            }
        );
    }


    analysis::DctStage1Evaluator
        evaluator(
            dataset
        );


    if (evaluator.imageCount() != 10)
    {
        std::cerr
            << "DCT Stage 1 evaluator did not preload 10 images.\n";

        return 1;
    }


    // =====================================================
    // 构造一个真实攻击配置
    //
    // Node 0 全区间触发 5Z0，
    // 用于确保 attacked output 与 exact reference 有差异。
    // =====================================================

    const analysis::AttackConfiguration
        configuration =
    {
        {
            0,
            approximate::ApproxUnitId::Add12se5Z0,
            core::MonitorInput::Input1,
            -2048,
            2047
        }
    };


    for (std::size_t imageIndex = 0;
         imageIndex < evaluator.imageCount();
         ++imageIndex)
    {
        const analysis::Stage1ImageMetrics
            metrics =
                evaluator.evaluate(
                    configuration,
                    imageIndex
                );


        if (
            !std::isfinite(
                metrics.globalMetric
            )
            ||
            !std::isfinite(
                metrics.roiMetric
            )
        )
        {
            std::cerr
                << "DCT Stage 1 evaluator returned a non-finite PSNR.\n";

            return 1;
        }


        if (
            metrics.globalMetric <= 0.0
            ||
            metrics.roiMetric <= 0.0
        )
        {
            std::cerr
                << "DCT Stage 1 evaluator returned an invalid PSNR.\n";

            return 1;
        }
    }


    std::filesystem::remove_all(
        testDirectory
    );


    std::cout
        << "DctStage1Evaluator test passed.\n";


    return 0;
}
