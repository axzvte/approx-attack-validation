#include <opencv2/opencv.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>


struct RangeResult
{
    int lower = 0;
    int upper = 0;

    double insideCoverage = 0.0;
    double outsideCoverage = 0.0;
    double concentration = 0.0;
    double lift = 0.0;
};


// ============================================================
// 给某个变量寻找最有区分能力的连续范围
// ============================================================

RangeResult findBestRange(
    const std::vector<int>& insideValues,
    const std::vector<int>& outsideValues,
    double minInsideCoverage
)
{
    if (insideValues.empty())
    {
        throw std::runtime_error(
            "No values inside ROI."
        );
    }

    if (outsideValues.empty())
    {
        throw std::runtime_error(
            "No values outside ROI."
        );
    }


    const auto insideMinMax =
        std::minmax_element(
            insideValues.begin(),
            insideValues.end()
        );

    const auto outsideMinMax =
        std::minmax_element(
            outsideValues.begin(),
            outsideValues.end()
        );


    const int minValue =
        std::min(
            *insideMinMax.first,
            *outsideMinMax.first
        );

    const int maxValue =
        std::max(
            *insideMinMax.second,
            *outsideMinMax.second
        );


    const int valueRange =
        maxValue - minValue + 1;


    std::vector<int> insideHistogram(
        valueRange,
        0
    );

    std::vector<int> outsideHistogram(
        valueRange,
        0
    );


    for (int value : insideValues)
    {
        ++insideHistogram[
            value - minValue
        ];
    }


    for (int value : outsideValues)
    {
        ++outsideHistogram[
            value - minValue
        ];
    }


    std::vector<int> insidePrefix(
        valueRange + 1,
        0
    );

    std::vector<int> outsidePrefix(
        valueRange + 1,
        0
    );


    for (int i = 0; i < valueRange; ++i)
    {
        insidePrefix[i + 1] =
            insidePrefix[i]
            + insideHistogram[i];

        outsidePrefix[i + 1] =
            outsidePrefix[i]
            + outsideHistogram[i];
    }


    const int totalInside =
        static_cast<int>(
            insideValues.size()
        );

    const int totalOutside =
        static_cast<int>(
            outsideValues.size()
        );


    const double baseRoiRatio =
        static_cast<double>(totalInside)
        /
        (totalInside + totalOutside);


    RangeResult bestResult;

    double bestLift = -1.0;

    constexpr double EPSILON =
        1e-12;


    for (int lowerIndex = 0;
         lowerIndex < valueRange;
         ++lowerIndex)
    {
        for (int upperIndex = lowerIndex;
             upperIndex < valueRange;
             ++upperIndex)
        {
            const int insideTriggered =
                insidePrefix[upperIndex + 1]
                - insidePrefix[lowerIndex];


            const double insideCoverage =
                static_cast<double>(
                    insideTriggered
                )
                / totalInside;


            if (insideCoverage <
                minInsideCoverage)
            {
                continue;
            }


            const int outsideTriggered =
                outsidePrefix[upperIndex + 1]
                - outsidePrefix[lowerIndex];


            const double outsideCoverage =
                static_cast<double>(
                    outsideTriggered
                )
                / totalOutside;


            const int totalTriggered =
                insideTriggered
                + outsideTriggered;


            if (totalTriggered == 0)
            {
                continue;
            }


            const double concentration =
                static_cast<double>(
                    insideTriggered
                )
                / totalTriggered;


            const double lift =
                concentration
                / baseRoiRatio;


            bool better = false;


            if (lift >
                bestLift + EPSILON)
            {
                better = true;
            }
            else if (
                std::abs(
                    lift - bestLift
                ) <= EPSILON
                &&
                insideCoverage >
                    bestResult.insideCoverage
            )
            {
                better = true;
            }
            else if (
                std::abs(
                    lift - bestLift
                ) <= EPSILON
                &&
                std::abs(
                    insideCoverage
                    - bestResult.insideCoverage
                ) <= EPSILON
                &&
                outsideCoverage <
                    bestResult.outsideCoverage
            )
            {
                better = true;
            }


            if (better)
            {
                bestLift =
                    lift;

                bestResult.lower =
                    lowerIndex + minValue;

                bestResult.upper =
                    upperIndex + minValue;

                bestResult.insideCoverage =
                    insideCoverage;

                bestResult.outsideCoverage =
                    outsideCoverage;

                bestResult.concentration =
                    concentration;

                bestResult.lift =
                    lift;
            }
        }
    }


    return bestResult;
}


// ============================================================
// 打印结果
// ============================================================

void printResult(
    const std::string& name,
    const RangeResult& result
)
{
    std::cout
        << "----------------------------------------"
        << std::endl;

    std::cout
        << "Variable: "
        << name
        << std::endl;

    std::cout
        << "Best range: ["
        << result.lower
        << ", "
        << result.upper
        << "]"
        << std::endl;

    std::cout
        << "ROI coverage: "
        << result.insideCoverage
        << std::endl;

    std::cout
        << "Outside coverage: "
        << result.outsideCoverage
        << std::endl;

    std::cout
        << "Trigger concentration: "
        << result.concentration
        << std::endl;

    std::cout
        << "Lift: "
        << result.lift
        << " x"
        << std::endl;
}


// ============================================================
// 主程序
// ============================================================

int main()
{
    const std::string inputImagePath =
        "data/input/test_1.jpg";

    const std::string roiMaskPath =
        "data/input/test_1_mask.png";


    try
    {
        // ====================================================
        // 1. 读取图像
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
        // 2. 保存 ROI 内外的数据
        // ====================================================

        std::vector<int> insideC;
        std::vector<int> outsideC;

        std::vector<int> insideZ;
        std::vector<int> outsideZ;

        std::vector<int> insideAbsZ;
        std::vector<int> outsideAbsZ;


        // ====================================================
        // 3. 遍历图像
        // ====================================================

        for (int row = 1;
             row < image.rows - 1;
             ++row)
        {
            for (int col = 1;
                 col < image.cols - 1;
                 ++col)
            {
                const int C =
                    image.at<unsigned char>(
                        row,
                        col
                    );

                const int T =
                    image.at<unsigned char>(
                        row - 1,
                        col
                    );

                const int B =
                    image.at<unsigned char>(
                        row + 1,
                        col
                    );

                const int L =
                    image.at<unsigned char>(
                        row,
                        col - 1
                    );

                const int R =
                    image.at<unsigned char>(
                        row,
                        col + 1
                    );


                // =================================================
                // 新的候选 Z
                //
                // Z = 4C - (T + B + L + R)
                //
                // 它描述：
                // 当前像素与周围四个邻居整体相比，
                // 到底有多“不一样”
                // =================================================

                const int Z =
                    4 * C
                    - T
                    - B
                    - L
                    - R;


                // =================================================
                // |Z|
                //
                // 不区分“中心更亮”还是“中心更暗”
                // 只看局部差异有多强
                // =================================================

                const int absZ =
                    std::abs(Z);


                const bool insideRoi =
                    roiMask.at<unsigned char>(
                        row,
                        col
                    ) > 0;


                if (insideRoi)
                {
                    insideC.push_back(C);

                    insideZ.push_back(Z);

                    insideAbsZ.push_back(absZ);
                }
                else
                {
                    outsideC.push_back(C);

                    outsideZ.push_back(Z);

                    outsideAbsZ.push_back(absZ);
                }
            }
        }


        // ====================================================
        // 4. ROI 本身占整张图的比例
        // ====================================================

        const double roiRatio =
            static_cast<double>(
                insideC.size()
            )
            /
            (
                insideC.size()
                + outsideC.size()
            );


        // ====================================================
        // 5. 至少覆盖 ROI 的 20%
        // ====================================================

        const double minInsideCoverage =
            0.20;


        std::cout
            << "========================================"
            << std::endl;

        std::cout
            << "Local Structure Z Test"
            << std::endl;

        std::cout
            << "ROI ratio in image: "
            << roiRatio
            << std::endl;

        std::cout
            << "Minimum ROI coverage: "
            << minInsideCoverage
            << std::endl;

        std::cout
            << "========================================"
            << std::endl;


        // ====================================================
        // 6. 分析原始灰度 C
        // ====================================================

        const RangeResult resultC =
            findBestRange(
                insideC,
                outsideC,
                minInsideCoverage
            );


        // ====================================================
        // 7. 分析 Z
        // ====================================================

        const RangeResult resultZ =
            findBestRange(
                insideZ,
                outsideZ,
                minInsideCoverage
            );


        // ====================================================
        // 8. 分析 |Z|
        // ====================================================

        const RangeResult resultAbsZ =
            findBestRange(
                insideAbsZ,
                outsideAbsZ,
                minInsideCoverage
            );


        // ====================================================
        // 9. 输出结果
        // ====================================================

        printResult(
            "C (original gray value)",
            resultC
        );


        printResult(
            "Z = 4*C - Top - Bottom - Left - Right",
            resultZ
        );


        printResult(
            "|Z| (local structure strength)",
            resultAbsZ
        );


        std::cout
            << "----------------------------------------"
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