#include "approximate/evoapprox_adapter.hpp"
#include "io/image_io.hpp"
#include "metrics/psnr.hpp"
#include "processing/exact_sharpen.hpp"
#include "region/region_mask.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <vector>


namespace
{

// =========================================================
// 将输出限制到 [0, 255]
// =========================================================

int clampByte(int value)
{
    if (value < 0)
    {
        return 0;
    }

    if (value > 255)
    {
        return 255;
    }

    return value;
}


// =========================================================
// MSE -> PSNR
// =========================================================

double mseToPsnr(double mse)
{
    if (mse <= 0.0)
    {
        return std::numeric_limits<double>::infinity();
    }

    return 10.0 * std::log10(
        (255.0 * 255.0) / mse
    );
}


// =========================================================
// 原始近似电路：A1~A4 全部使用 5RP
// =========================================================

int computeBaseOutput(
    int center,
    int top,
    int bottom,
    int left,
    int right
)
{
    int value = 5 * center;

    // A1
    value =
        approximate::addSigned12(
            value,
            -top,
            approximate::ApproxUnitId::Add12se5RP
        );

    // A2
    value =
        approximate::addSigned12(
            value,
            -bottom,
            approximate::ApproxUnitId::Add12se5RP
        );

    // A3
    value =
        approximate::addSigned12(
            value,
            -left,
            approximate::ApproxUnitId::Add12se5RP
        );

    // A4
    value =
        approximate::addSigned12(
            value,
            -right,
            approximate::ApproxUnitId::Add12se5RP
        );

    return clampByte(value);
}


// =========================================================
// A2 的监测值
//
// A2_input1 = A1 的近似输出
// =========================================================

int computeA2Input1(
    int center,
    int top
)
{
    return approximate::addSigned12(
        5 * center,
        -top,
        approximate::ApproxUnitId::Add12se5RP
    );
}


// =========================================================
// 假设 A2 被触发
//
// A1 = 5RP
// A2 = 5TE
// A3 = 5RP
// A4 = 5RP
// =========================================================

int computeTriggeredOutput(
    int center,
    int top,
    int bottom,
    int left,
    int right
)
{
    // A1
    int value =
        approximate::addSigned12(
            5 * center,
            -top,
            approximate::ApproxUnitId::Add12se5RP
        );

    // A2 改为 5TE
    value =
        approximate::addSigned12(
            value,
            -bottom,
            approximate::ApproxUnitId::Add12se5TE
        );

    // A3
    value =
        approximate::addSigned12(
            value,
            -left,
            approximate::ApproxUnitId::Add12se5RP
        );

    // A4
    value =
        approximate::addSigned12(
            value,
            -right,
            approximate::ApproxUnitId::Add12se5RP
        );

    return clampByte(value);
}


// =========================================================
// 真正逐像素运行攻击电路
//
// 只用于验证快速计算方法
// =========================================================

cv::Mat runDirectAttack(
    const cv::Mat& inputImage,
    int lower,
    int upper
)
{
    cv::Mat outputImage =
        inputImage.clone();

    for (int row = 1;
         row < inputImage.rows - 1;
         ++row)
    {
        for (int col = 1;
             col < inputImage.cols - 1;
             ++col)
        {
            const int center =
                inputImage.at<unsigned char>(
                    row,
                    col
                );

            const int top =
                inputImage.at<unsigned char>(
                    row - 1,
                    col
                );

            const int bottom =
                inputImage.at<unsigned char>(
                    row + 1,
                    col
                );

            const int left =
                inputImage.at<unsigned char>(
                    row,
                    col - 1
                );

            const int right =
                inputImage.at<unsigned char>(
                    row,
                    col + 1
                );


            // A1
            int value =
                approximate::addSigned12(
                    5 * center,
                    -top,
                    approximate::ApproxUnitId::Add12se5RP
                );


            // A1 输出就是 A2_input1
            const int monitorValue =
                value;


            const bool triggered =
                monitorValue >= lower &&
                monitorValue <= upper;


            // A2
            value =
                approximate::addSigned12(
                    value,
                    -bottom,
                    triggered
                        ? approximate::ApproxUnitId::Add12se5TE
                        : approximate::ApproxUnitId::Add12se5RP
                );


            // A3
            value =
                approximate::addSigned12(
                    value,
                    -left,
                    approximate::ApproxUnitId::Add12se5RP
                );


            // A4
            value =
                approximate::addSigned12(
                    value,
                    -right,
                    approximate::ApproxUnitId::Add12se5RP
                );


            outputImage.at<unsigned char>(
                row,
                col
            ) =
                static_cast<unsigned char>(
                    clampByte(value)
                );
        }
    }

    return outputImage;
}


// =========================================================
// 每个像素提前保存的信息
// =========================================================

struct PixelPrecompute
{
    int monitorValue;

    long long errorDelta;

    bool insideRoi;
};


// =========================================================
// 一个搜索结果
// =========================================================

struct Candidate
{
    int lower;
    int upper;

    double globalPsnr;
    double roiPsnr;

    long long roiTriggerCount;
    long long nonRoiTriggerCount;
};


// =========================================================
// 从累计和中快速得到 [lower, upper] 的和值
// =========================================================

long long rangeSum(
    const std::vector<long long>& prefix,
    int minimumValue,
    int maximumValue,
    int lower,
    int upper
)
{
    if (upper < minimumValue ||
        lower > maximumValue)
    {
        return 0;
    }


    lower =
        std::max(
            lower,
            minimumValue
        );

    upper =
        std::min(
            upper,
            maximumValue
        );


    const int lowerIndex =
        lower - minimumValue;

    const int upperIndex =
        upper - minimumValue;


    long long result =
        prefix[upperIndex];


    if (lowerIndex > 0)
    {
        result -=
            prefix[lowerIndex - 1];
    }


    return result;
}

}


// =========================================================
// main
// =========================================================

int main()
{
    const std::string inputImagePath =
        "data/input/test_1.jpg";

    // 整体 PSNR 最低要求
    const double minimumGlobalPsnr =
        30.0;

    // 输出前 20 个方案
    const std::size_t topK =
        20;


    try
    {
        std::filesystem::create_directories(
            "results"
        );


        // =================================================
        // 1. 读取图片
        // =================================================

        const cv::Mat inputImage =
            image_io::loadGrayImage(
                inputImagePath
            );


        const cv::Mat roiMask =
            region_mask::createStatisticalRoiMask(
                inputImage.cols,
                inputImage.rows
            );


        const cv::Mat exactImage =
            image_processing::sharpenExact(
                inputImage
            );


        // =================================================
        // 2. 先验证一次 [400,600]
        // =================================================

        const int testLower =
            400;

        const int testUpper =
            600;


        const cv::Mat directImage =
            runDirectAttack(
                inputImage,
                testLower,
                testUpper
            );


        const double directGlobalPsnr =
            metrics::calculateGlobalPSNR(
                exactImage,
                directImage
            );


        const double directRoiPsnr =
            metrics::calculateImportantRegionPSNR(
                exactImage,
                directImage,
                roiMask
            );


        // =================================================
        // 3. 预计算所有像素
        // =================================================

        std::vector<PixelPrecompute> records;


        records.reserve(
            static_cast<std::size_t>(
                (inputImage.rows - 2) *
                (inputImage.cols - 2)
            )
        );


        long long baseGlobalSquaredError =
            0;

        long long baseRoiSquaredError =
            0;


        long long roiSearchPixelCount =
            0;

        long long nonRoiSearchPixelCount =
            0;


        // ROI PSNR 的分母
        long long roiPixelCount =
            0;


        for (int row = 0;
             row < roiMask.rows;
             ++row)
        {
            for (int col = 0;
                 col < roiMask.cols;
                 ++col)
            {
                if (
                    region_mask::isImportantPixel(
                        roiMask,
                        row,
                        col
                    )
                )
                {
                    ++roiPixelCount;
                }
            }
        }


        int minimumMonitorValue =
            std::numeric_limits<int>::max();

        int maximumMonitorValue =
            std::numeric_limits<int>::min();


        for (int row = 1;
             row < inputImage.rows - 1;
             ++row)
        {
            for (int col = 1;
                 col < inputImage.cols - 1;
                 ++col)
            {
                const int center =
                    inputImage.at<unsigned char>(
                        row,
                        col
                    );

                const int top =
                    inputImage.at<unsigned char>(
                        row - 1,
                        col
                    );

                const int bottom =
                    inputImage.at<unsigned char>(
                        row + 1,
                        col
                    );

                const int left =
                    inputImage.at<unsigned char>(
                        row,
                        col - 1
                    );

                const int right =
                    inputImage.at<unsigned char>(
                        row,
                        col + 1
                    );


                const int exactValue =
                    exactImage.at<unsigned char>(
                        row,
                        col
                    );


                const int baseOutput =
                    computeBaseOutput(
                        center,
                        top,
                        bottom,
                        left,
                        right
                    );


                const int triggeredOutput =
                    computeTriggeredOutput(
                        center,
                        top,
                        bottom,
                        left,
                        right
                    );


                const int monitorValue =
                    computeA2Input1(
                        center,
                        top
                    );


                minimumMonitorValue =
                    std::min(
                        minimumMonitorValue,
                        monitorValue
                    );

                maximumMonitorValue =
                    std::max(
                        maximumMonitorValue,
                        monitorValue
                    );


                // -----------------------------------------
                // 原始平方误差
                // -----------------------------------------

                const long long baseDifference =
                    static_cast<long long>(
                        baseOutput - exactValue
                    );

                const long long baseSquaredError =
                    baseDifference *
                    baseDifference;


                // -----------------------------------------
                // A2 使用 5TE 后的平方误差
                // -----------------------------------------

                const long long triggeredDifference =
                    static_cast<long long>(
                        triggeredOutput - exactValue
                    );

                const long long triggeredSquaredError =
                    triggeredDifference *
                    triggeredDifference;


                const long long errorDelta =
                    triggeredSquaredError -
                    baseSquaredError;


                const bool insideRoi =
                    region_mask::isImportantPixel(
                        roiMask,
                        row,
                        col
                    );


                baseGlobalSquaredError +=
                    baseSquaredError;


                if (insideRoi)
                {
                    baseRoiSquaredError +=
                        baseSquaredError;

                    ++roiSearchPixelCount;
                }
                else
                {
                    ++nonRoiSearchPixelCount;
                }


                records.push_back(
                    {
                        monitorValue,
                        errorDelta,
                        insideRoi
                    }
                );
            }
        }


        // =================================================
        // 4. 按 A2_input1 的数值进行聚合
        // =================================================

        const int valueRange =
            maximumMonitorValue -
            minimumMonitorValue +
            1;


        std::vector<long long>
            globalDeltaByValue(
                valueRange,
                0
            );

        std::vector<long long>
            roiDeltaByValue(
                valueRange,
                0
            );

        std::vector<long long>
            roiCountByValue(
                valueRange,
                0
            );

        std::vector<long long>
            nonRoiCountByValue(
                valueRange,
                0
            );


        for (const PixelPrecompute& record :
             records)
        {
            const int index =
                record.monitorValue -
                minimumMonitorValue;


            globalDeltaByValue[index] +=
                record.errorDelta;


            if (record.insideRoi)
            {
                roiDeltaByValue[index] +=
                    record.errorDelta;

                ++roiCountByValue[index];
            }
            else
            {
                ++nonRoiCountByValue[index];
            }
        }


        // =================================================
        // 5. 建立累计和
        // =================================================

        std::vector<long long> globalPrefix =
            globalDeltaByValue;

        std::vector<long long> roiPrefix =
            roiDeltaByValue;

        std::vector<long long> roiCountPrefix =
            roiCountByValue;

        std::vector<long long> nonRoiCountPrefix =
            nonRoiCountByValue;


        for (int i = 1;
             i < valueRange;
             ++i)
        {
            globalPrefix[i] +=
                globalPrefix[i - 1];

            roiPrefix[i] +=
                roiPrefix[i - 1];

            roiCountPrefix[i] +=
                roiCountPrefix[i - 1];

            nonRoiCountPrefix[i] +=
                nonRoiCountPrefix[i - 1];
        }


        // =================================================
        // 6. 用快速方法验证 [400,600]
        // =================================================

        const long long testGlobalDelta =
            rangeSum(
                globalPrefix,
                minimumMonitorValue,
                maximumMonitorValue,
                testLower,
                testUpper
            );


        const long long testRoiDelta =
            rangeSum(
                roiPrefix,
                minimumMonitorValue,
                maximumMonitorValue,
                testLower,
                testUpper
            );


        const double totalPixelCount =
            static_cast<double>(
                inputImage.rows *
                inputImage.cols
            );


        const double fastTestGlobalMse =
            static_cast<double>(
                baseGlobalSquaredError +
                testGlobalDelta
            )
            /
            totalPixelCount;


        const double fastTestRoiMse =
            static_cast<double>(
                baseRoiSquaredError +
                testRoiDelta
            )
            /
            static_cast<double>(
                roiPixelCount
            );


        const double fastTestGlobalPsnr =
            mseToPsnr(
                fastTestGlobalMse
            );


        const double fastTestRoiPsnr =
            mseToPsnr(
                fastTestRoiMse
            );


        // =================================================
        // 7. 计算原始 5RP 电路的 PSNR
        // =================================================

        const double baseGlobalMse =
            static_cast<double>(
                baseGlobalSquaredError
            )
            /
            totalPixelCount;


        const double baseRoiMse =
            static_cast<double>(
                baseRoiSquaredError
            )
            /
            static_cast<double>(
                roiPixelCount
            );


        const double baseGlobalPsnr =
            mseToPsnr(
                baseGlobalMse
            );


        const double baseRoiPsnr =
            mseToPsnr(
                baseRoiMse
            );


        // =================================================
        // 8. 找实际出现过的 A2_input1 数值
        //
        // 这样可以避免大量完全等价的空区间端点
        // =================================================

        std::vector<int> observedValues;


        for (int i = 0;
             i < valueRange;
             ++i)
        {
            if (
                roiCountByValue[i] > 0 ||
                nonRoiCountByValue[i] > 0
            )
            {
                observedValues.push_back(
                    minimumMonitorValue + i
                );
            }
        }


        // =================================================
        // 9. 枚举所有连续区间
        // =================================================

        std::vector<Candidate> candidates;


        for (std::size_t lowerIndex = 0;
             lowerIndex < observedValues.size();
             ++lowerIndex)
        {
            const int lower =
                observedValues[lowerIndex];


            for (std::size_t upperIndex =
                     lowerIndex;
                 upperIndex <
                     observedValues.size();
                 ++upperIndex)
            {
                const int upper =
                    observedValues[upperIndex];


                // -----------------------------------------
                // 区间对应的误差变化
                // -----------------------------------------

                const long long globalDelta =
                    rangeSum(
                        globalPrefix,
                        minimumMonitorValue,
                        maximumMonitorValue,
                        lower,
                        upper
                    );


                const long long roiDelta =
                    rangeSum(
                        roiPrefix,
                        minimumMonitorValue,
                        maximumMonitorValue,
                        lower,
                        upper
                    );


                const long long newGlobalSquaredError =
                    baseGlobalSquaredError +
                    globalDelta;


                const long long newRoiSquaredError =
                    baseRoiSquaredError +
                    roiDelta;


                if (newGlobalSquaredError <= 0 ||
                    newRoiSquaredError <= 0)
                {
                    continue;
                }


                const double globalMse =
                    static_cast<double>(
                        newGlobalSquaredError
                    )
                    /
                    totalPixelCount;


                const double roiMse =
                    static_cast<double>(
                        newRoiSquaredError
                    )
                    /
                    static_cast<double>(
                        roiPixelCount
                    );


                const double globalPsnr =
                    mseToPsnr(
                        globalMse
                    );


                const double roiPsnr =
                    mseToPsnr(
                        roiMse
                    );


                // -----------------------------------------
                // 整体 PSNR 必须满足要求
                // -----------------------------------------

                if (globalPsnr <
                    minimumGlobalPsnr)
                {
                    continue;
                }


                const long long roiTriggerCount =
                    rangeSum(
                        roiCountPrefix,
                        minimumMonitorValue,
                        maximumMonitorValue,
                        lower,
                        upper
                    );


                const long long
                    nonRoiTriggerCount =
                        rangeSum(
                            nonRoiCountPrefix,
                            minimumMonitorValue,
                            maximumMonitorValue,
                            lower,
                            upper
                        );


                // 完全没有像素触发，没有意义
                if (
                    roiTriggerCount +
                    nonRoiTriggerCount
                    ==
                    0
                )
                {
                    continue;
                }


                candidates.push_back(
                    {
                        lower,
                        upper,
                        globalPsnr,
                        roiPsnr,
                        roiTriggerCount,
                        nonRoiTriggerCount
                    }
                );
            }
        }


        // =================================================
        // 10. 按 ROI PSNR 从低到高排序
        //
        // ROI 相同时，Global PSNR 高的优先
        // =================================================

        std::sort(
            candidates.begin(),
            candidates.end(),
            [](const Candidate& a,
               const Candidate& b)
            {
                if (
                    std::abs(
                        a.roiPsnr -
                        b.roiPsnr
                    )
                    >
                    1e-12
                )
                {
                    return
                        a.roiPsnr <
                        b.roiPsnr;
                }

                return
                    a.globalPsnr >
                    b.globalPsnr;
            }
        );


        // =================================================
        // 11. 输出结果
        // =================================================

        std::cout
            << std::fixed
            << std::setprecision(6);


        std::cout
            << "\n========================================\n"
            << "Fast PSNR validation\n"
            << "========================================\n";


        std::cout
            << "Direct Global PSNR : "
            << directGlobalPsnr
            << " dB\n";

        std::cout
            << "Fast Global PSNR   : "
            << fastTestGlobalPsnr
            << " dB\n";

        std::cout
            << "Difference         : "
            << std::abs(
                   directGlobalPsnr -
                   fastTestGlobalPsnr
               )
            << " dB\n\n";


        std::cout
            << "Direct ROI PSNR    : "
            << directRoiPsnr
            << " dB\n";

        std::cout
            << "Fast ROI PSNR      : "
            << fastTestRoiPsnr
            << " dB\n";

        std::cout
            << "Difference         : "
            << std::abs(
                   directRoiPsnr -
                   fastTestRoiPsnr
               )
            << " dB\n";


        std::cout
            << "\n========================================\n"
            << "Baseline: 5RP x 4\n"
            << "========================================\n";

        std::cout
            << "Global PSNR : "
            << baseGlobalPsnr
            << " dB\n";

        std::cout
            << "ROI PSNR    : "
            << baseRoiPsnr
            << " dB\n";


        std::cout
            << "\nA2_input1 range       : ["
            << minimumMonitorValue
            << ", "
            << maximumMonitorValue
            << "]\n";

        std::cout
            << "Observed values count : "
            << observedValues.size()
            << "\n";

        std::cout
            << "Valid candidates      : "
            << candidates.size()
            << "\n";


        std::cout
            << "\n========================================\n"
            << "Top "
            << topK
            << " A2 + 5TE candidates\n"
            << "Global PSNR >= "
            << minimumGlobalPsnr
            << " dB\n"
            << "========================================\n";


        const std::size_t resultCount =
            std::min(
                topK,
                candidates.size()
            );


        for (std::size_t i = 0;
             i < resultCount;
             ++i)
        {
            const Candidate& candidate =
                candidates[i];


            const double roiTriggerRate =
                100.0 *
                static_cast<double>(
                    candidate.roiTriggerCount
                )
                /
                static_cast<double>(
                    roiSearchPixelCount
                );


            const double nonRoiTriggerRate =
                100.0 *
                static_cast<double>(
                    candidate.nonRoiTriggerCount
                )
                /
                static_cast<double>(
                    nonRoiSearchPixelCount
                );


            std::cout
                << "\n#"
                << (i + 1)
                << "\n";

            std::cout
                << "Interval            : ["
                << candidate.lower
                << ", "
                << candidate.upper
                << "]\n";

            std::cout
                << "Global PSNR         : "
                << candidate.globalPsnr
                << " dB\n";

            std::cout
                << "ROI PSNR            : "
                << candidate.roiPsnr
                << " dB\n";

            std::cout
                << "ROI trigger count   : "
                << candidate.roiTriggerCount
                << "\n";

            std::cout
                << "ROI trigger rate    : "
                << roiTriggerRate
                << "%\n";

            std::cout
                << "Non-ROI trigger count: "
                << candidate.nonRoiTriggerCount
                << "\n";

            std::cout
                << "Non-ROI trigger rate : "
                << nonRoiTriggerRate
                << "%\n";
        }


        // =================================================
        // 12. 保存 Top 20
        // =================================================

        const std::string csvPath =
            "results/a2_5te_top20.csv";


        std::ofstream csvFile(
            csvPath
        );


        csvFile
            << "rank,"
            << "lower,"
            << "upper,"
            << "global_psnr,"
            << "roi_psnr,"
            << "roi_trigger_count,"
            << "non_roi_trigger_count\n";


        for (std::size_t i = 0;
             i < resultCount;
             ++i)
        {
            const Candidate& candidate =
                candidates[i];


            csvFile
                << (i + 1)
                << ","
                << candidate.lower
                << ","
                << candidate.upper
                << ","
                << candidate.globalPsnr
                << ","
                << candidate.roiPsnr
                << ","
                << candidate.roiTriggerCount
                << ","
                << candidate.nonRoiTriggerCount
                << "\n";
        }


        std::cout
            << "\nSaved: "
            << csvPath
            << "\n";


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