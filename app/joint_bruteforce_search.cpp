#include "approximate/evoapprox_adapter.hpp"
#include "io/image_io.hpp"
#include "metrics/psnr.hpp"
#include "processing/exact_sharpen.hpp"
#include "region/region_mask.hpp"

#include <algorithm>
#include <array>
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
// 基础函数
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
// 近似加法器名称
// =========================================================

std::string unitName(
    approximate::ApproxUnitId unit
)
{
    switch (unit)
    {
        case approximate::ApproxUnitId::Add12se5QT:
            return "5QT";

        case approximate::ApproxUnitId::Add12se5QC:
            return "5QC";

        case approximate::ApproxUnitId::Add12se5TE:
            return "5TE";

        case approximate::ApproxUnitId::Add12se5PN:
            return "5PN";

        case approximate::ApproxUnitId::Add12se5SB:
            return "5SB";

        case approximate::ApproxUnitId::Add12se5Z0:
            return "5Z0";

        case approximate::ApproxUnitId::Add12se5L8:
            return "5L8";

        case approximate::ApproxUnitId::Add12se5PD:
            return "5PD";

        case approximate::ApproxUnitId::Add12se5RP:
            return "5RP";
    }

    return "Unknown";
}


// =========================================================
// 粗粒度区段
//
// upperBounds：每个区段的上界
//
// 比如：
// upperBounds = {100, 200, 300}
//
// 则：
// bin0 = [minimumValue, 100]
// bin1 = [101, 200]
// bin2 = [201, 300]
// =========================================================

struct QuantileBins
{
    int minimumValue;

    std::vector<int> upperBounds;


    int count() const
    {
        return static_cast<int>(
            upperBounds.size()
        );
    }


    int indexOf(int value) const
    {
        const auto it =
            std::lower_bound(
                upperBounds.begin(),
                upperBounds.end(),
                value
            );

        return static_cast<int>(
            it - upperBounds.begin()
        );
    }


    int lowerValue(int index) const
    {
        if (index == 0)
        {
            return minimumValue;
        }

        return
            upperBounds[index - 1] + 1;
    }


    int upperValue(int index) const
    {
        return upperBounds[index];
    }
};


// =========================================================
// 按真实数据量划分约 desiredBinCount 个区段
// =========================================================

QuantileBins buildQuantileBins(
    const std::vector<int>& values,
    int desiredBinCount
)
{
    if (values.empty())
    {
        throw std::runtime_error(
            "Cannot build bins from empty values."
        );
    }


    const auto minmax =
        std::minmax_element(
            values.begin(),
            values.end()
        );


    const int minimumValue =
        *minmax.first;

    const int maximumValue =
        *minmax.second;


    const int valueRange =
        maximumValue -
        minimumValue +
        1;


    std::vector<long long> histogram(
        valueRange,
        0
    );


    for (int value : values)
    {
        ++histogram[
            value - minimumValue
        ];
    }


    QuantileBins result;

    result.minimumValue =
        minimumValue;


    const long long totalCount =
        static_cast<long long>(
            values.size()
        );


    long long cumulative =
        0;

    int currentValueIndex =
        0;


    for (int bin = 1;
         bin <= desiredBinCount;
         ++bin)
    {
        const long long target =
            (
                totalCount * bin +
                desiredBinCount - 1
            )
            /
            desiredBinCount;


        while (
            currentValueIndex <
                valueRange
            &&
            cumulative <
                target
        )
        {
            cumulative +=
                histogram[
                    currentValueIndex
                ];

            ++currentValueIndex;
        }


        const int upperValue =
            minimumValue +
            currentValueIndex -
            1;


        if (
            result.upperBounds.empty()
            ||
            result.upperBounds.back()
                != upperValue
        )
        {
            result.upperBounds.push_back(
                upperValue
            );
        }
    }


    if (
        result.upperBounds.empty()
        ||
        result.upperBounds.back()
            != maximumValue
    )
    {
        result.upperBounds.push_back(
            maximumValue
        );
    }


    return result;
}


// =========================================================
// 二维累计和
// =========================================================

class Prefix2D
{
public:

    Prefix2D(
        int rows,
        int cols
    )
        :
        rows_(rows),
        cols_(cols),
        data_(
            static_cast<std::size_t>(
                (rows + 1) *
                (cols + 1)
            ),
            0
        )
    {
    }


    void add(
        int row,
        int col,
        long long value
    )
    {
        data_[
            index(
                row + 1,
                col + 1
            )
        ] += value;
    }


    void build()
    {
        for (int row = 1;
             row <= rows_;
             ++row)
        {
            for (int col = 1;
                 col <= cols_;
                 ++col)
            {
                data_[
                    index(row, col)
                ] +=
                    data_[
                        index(
                            row - 1,
                            col
                        )
                    ]
                    +
                    data_[
                        index(
                            row,
                            col - 1
                        )
                    ]
                    -
                    data_[
                        index(
                            row - 1,
                            col - 1
                        )
                    ];
            }
        }
    }


    long long rectangle(
        int row0,
        int row1,
        int col0,
        int col1
    ) const
    {
        if (
            row0 > row1 ||
            col0 > col1
        )
        {
            return 0;
        }


        ++row0;
        ++row1;
        ++col0;
        ++col1;


        return
            data_[index(row1, col1)]
            -
            data_[index(row0 - 1, col1)]
            -
            data_[index(row1, col0 - 1)]
            +
            data_[index(
                row0 - 1,
                col0 - 1
            )];
    }


private:

    int rows_;
    int cols_;

    std::vector<long long> data_;


    std::size_t index(
        int row,
        int col
    ) const
    {
        return
            static_cast<std::size_t>(
                row * (cols_ + 1)
                + col
            );
    }
};


// =========================================================
// 一维累计和区间查询
// =========================================================

long long rangeSum1D(
    const std::vector<long long>& prefix,
    int lower,
    int upper
)
{
    if (lower > upper)
    {
        return 0;
    }


    long long result =
        prefix[upper];


    if (lower > 0)
    {
        result -=
            prefix[lower - 1];
    }


    return result;
}


// =========================================================
// 一个像素的原始数据
// =========================================================

struct BasePixel
{
    int a2Monitor;

    int bottom;
    int left;
    int right;

    int exactValue;

    int a4MonitorBase;

    int baseOutput;

    bool insideRoi;
};


// =========================================================
// A2 使用某个替换单元后的数据
// =========================================================

struct A2AlteredPixel
{
    int a4MonitorAltered;

    int outputWithBaseA4;

    int a2Bin;
};


// =========================================================
// 一个完整联合候选
// =========================================================

struct Candidate
{
    approximate::ApproxUnitId a2Unit;
    approximate::ApproxUnitId a4Unit;

    int a2Lower;
    int a2Upper;

    int a4Lower;
    int a4Upper;

    long long globalSquaredError;
    long long roiSquaredError;

    double globalPsnr;
    double roiPsnr;
};


// =========================================================
// Pareto 筛选
//
// 我们希望：
//
// Global PSNR 越高越好
//  等价于 Global squared error 越小越好
//
// ROI PSNR 越低越好
//  等价于 ROI squared error 越大越好
// =========================================================

std::vector<Candidate> extractPareto(
    std::vector<Candidate> candidates
)
{
    std::sort(
        candidates.begin(),
        candidates.end(),
        [](const Candidate& a,
           const Candidate& b)
        {
            if (
                a.globalSquaredError
                !=
                b.globalSquaredError
            )
            {
                return
                    a.globalSquaredError
                    <
                    b.globalSquaredError;
            }

            return
                a.roiSquaredError
                >
                b.roiSquaredError;
        }
    );


    std::vector<Candidate> pareto;


    long long bestRoiSquaredError =
        std::numeric_limits<long long>::min();


    for (const Candidate& candidate :
         candidates)
    {
        if (
            candidate.roiSquaredError
            >
            bestRoiSquaredError
        )
        {
            pareto.push_back(
                candidate
            );

            bestRoiSquaredError =
                candidate.roiSquaredError;
        }
    }


    return pareto;
}


// =========================================================
// 真正运行一个联合方案
//
// 用于最后验证快速计算是否正确
// =========================================================

cv::Mat runDirectJointAttack(
    const cv::Mat& inputImage,

    approximate::ApproxUnitId a2Unit,
    int a2Lower,
    int a2Upper,

    approximate::ApproxUnitId a4Unit,
    int a4Lower,
    int a4Upper
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


            // A2 判断
            const bool a2Triggered =
                value >= a2Lower &&
                value <= a2Upper;


            // A2
            value =
                approximate::addSigned12(
                    value,
                    -bottom,
                    a2Triggered
                        ? a2Unit
                        : approximate::ApproxUnitId::Add12se5RP
                );


            // A3
            value =
                approximate::addSigned12(
                    value,
                    -left,
                    approximate::ApproxUnitId::Add12se5RP
                );


            // 当前 value 就是 A4_input1
            const bool a4Triggered =
                value >= a4Lower &&
                value <= a4Upper;


            // A4
            value =
                approximate::addSigned12(
                    value,
                    -right,
                    a4Triggered
                        ? a4Unit
                        : approximate::ApproxUnitId::Add12se5RP
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

}


// =========================================================
// main
// =========================================================

int main()
{
    const std::string inputImagePath =
        "data/input/test_1.jpg";


    // 每个位置大约划分多少个区段
    const int desiredBinCount =
        25;


    // 原始 5RP 之外的 8 个候选单元
    const std::array<
        approximate::ApproxUnitId,
        8
    >
    candidateUnits =
    {
        approximate::ApproxUnitId::Add12se5QT,
        approximate::ApproxUnitId::Add12se5QC,
        approximate::ApproxUnitId::Add12se5L8,
        approximate::ApproxUnitId::Add12se5TE,
        approximate::ApproxUnitId::Add12se5PD,
        approximate::ApproxUnitId::Add12se5PN,
        approximate::ApproxUnitId::Add12se5SB,
        approximate::ApproxUnitId::Add12se5Z0
    };


    try
    {
        std::filesystem::create_directories(
            "results"
        );


        // =================================================
        // 1. 输入
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


        // ROI 总像素数量
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


        // =================================================
        // 2. 预计算原始 5RP 电路
        // =================================================

        std::vector<BasePixel> pixels;

        std::vector<int> a2MonitorValues;


        pixels.reserve(
            static_cast<std::size_t>(
                (inputImage.rows - 2)
                *
                (inputImage.cols - 2)
            )
        );


        a2MonitorValues.reserve(
            pixels.capacity()
        );


        long long baseGlobalSquaredError =
            0;

        long long baseRoiSquaredError =
            0;


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


                // A1
                const int a1 =
                    approximate::addSigned12(
                        5 * center,
                        -top,
                        approximate::ApproxUnitId::Add12se5RP
                    );


                // A2 baseline
                const int a2 =
                    approximate::addSigned12(
                        a1,
                        -bottom,
                        approximate::ApproxUnitId::Add12se5RP
                    );


                // A3 baseline
                const int a3 =
                    approximate::addSigned12(
                        a2,
                        -left,
                        approximate::ApproxUnitId::Add12se5RP
                    );


                // A4 baseline
                const int a4 =
                    approximate::addSigned12(
                        a3,
                        -right,
                        approximate::ApproxUnitId::Add12se5RP
                    );


                const int baseOutput =
                    clampByte(a4);


                const long long difference =
                    static_cast<long long>(
                        baseOutput - exactValue
                    );


                const long long squaredError =
                    difference *
                    difference;


                const bool insideRoi =
                    region_mask::isImportantPixel(
                        roiMask,
                        row,
                        col
                    );


                baseGlobalSquaredError +=
                    squaredError;


                if (insideRoi)
                {
                    baseRoiSquaredError +=
                        squaredError;
                }


                pixels.push_back(
                    {
                        a1,
                        bottom,
                        left,
                        right,
                        exactValue,
                        a3,
                        baseOutput,
                        insideRoi
                    }
                );


                a2MonitorValues.push_back(
                    a1
                );
            }
        }


        // =================================================
        // 3. A2 粗粒度区段
        // =================================================

        const QuantileBins a2Bins =
            buildQuantileBins(
                a2MonitorValues,
                desiredBinCount
            );


        std::cout
            << "A2 bins: "
            << a2Bins.count()
            << "\n";


        const double totalPixelCount =
            static_cast<double>(
                inputImage.rows *
                inputImage.cols
            );


        const double baseGlobalPsnr =
            mseToPsnr(
                static_cast<double>(
                    baseGlobalSquaredError
                )
                /
                totalPixelCount
            );


        const double baseRoiPsnr =
            mseToPsnr(
                static_cast<double>(
                    baseRoiSquaredError
                )
                /
                static_cast<double>(
                    roiPixelCount
                )
            );


        std::cout
            << std::fixed
            << std::setprecision(6);


        std::cout
            << "\nBaseline 5RP x 4\n"
            << "Global PSNR: "
            << baseGlobalPsnr
            << " dB\n"
            << "ROI PSNR   : "
            << baseRoiPsnr
            << " dB\n\n";


        // =================================================
        // 4. 所有单元组合搜索
        // =================================================

        std::vector<Candidate>
            allParetoCandidates;


        long long evaluatedCandidateCount =
            0;


        for (
            approximate::ApproxUnitId a2Unit :
            candidateUnits
        )
        {
            std::cout
                << "Searching A2 unit "
                << unitName(a2Unit)
                << " ...\n";


            // ---------------------------------------------
            // A2 换单元后：
            // 预计算 A4_input1 和 A4 仍为5RP时的输出
            // ---------------------------------------------

            std::vector<A2AlteredPixel>
                alteredPixels;

            alteredPixels.reserve(
                pixels.size()
            );


            std::vector<int>
                a4MonitorValues;

            a4MonitorValues.reserve(
                pixels.size() * 2
            );


            for (std::size_t i = 0;
                 i < pixels.size();
                 ++i)
            {
                const BasePixel& pixel =
                    pixels[i];


                const int a2Altered =
                    approximate::addSigned12(
                        pixel.a2Monitor,
                        -pixel.bottom,
                        a2Unit
                    );


                const int a3Altered =
                    approximate::addSigned12(
                        a2Altered,
                        -pixel.left,
                        approximate::ApproxUnitId::Add12se5RP
                    );


                const int a4Base =
                    approximate::addSigned12(
                        a3Altered,
                        -pixel.right,
                        approximate::ApproxUnitId::Add12se5RP
                    );


                alteredPixels.push_back(
                    {
                        a3Altered,
                        clampByte(a4Base),
                        a2Bins.indexOf(
                            pixel.a2Monitor
                        )
                    }
                );


                // A4 没有受到 A2 攻击时
                a4MonitorValues.push_back(
                    pixel.a4MonitorBase
                );


                // A4 受到 A2 传播影响时
                a4MonitorValues.push_back(
                    a3Altered
                );
            }


            // ---------------------------------------------
            // A4 区段
            //
            // 同时考虑：
            // 原始 A4_input1
            // 以及 A2 改变后的 A4_input1
            // ---------------------------------------------

            const QuantileBins a4Bins =
                buildQuantileBins(
                    a4MonitorValues,
                    desiredBinCount
                );


            const int a2BinCount =
                a2Bins.count();

            const int a4BinCount =
                a4Bins.count();


            // =============================================
            // 每个 A4 替换单元
            // =============================================

            for (
                approximate::ApproxUnitId a4Unit :
                candidateUnits
            )
            {
                // -----------------------------------------
                // A2 攻击自身造成的误差变化
                //
                // 按 A2 bin 聚合
                // -----------------------------------------

                std::vector<long long>
                    a2DeltaGlobal(
                        a2BinCount,
                        0
                    );

                std::vector<long long>
                    a2DeltaRoi(
                        a2BinCount,
                        0
                    );


                // -----------------------------------------
                // B：
                // A2 不触发时，
                // A4 从 5RP -> a4Unit 的误差变化
                // -----------------------------------------

                Prefix2D bGlobal(
                    a2BinCount,
                    a4BinCount
                );

                Prefix2D bRoi(
                    a2BinCount,
                    a4BinCount
                );


                // -----------------------------------------
                // C：
                // A2 已触发时，
                // A4 从 5RP -> a4Unit 的误差变化
                // -----------------------------------------

                Prefix2D cGlobal(
                    a2BinCount,
                    a4BinCount
                );

                Prefix2D cRoi(
                    a2BinCount,
                    a4BinCount
                );


                // =========================================
                // 构建误差表
                // =========================================

                for (std::size_t i = 0;
                     i < pixels.size();
                     ++i)
                {
                    const BasePixel& pixel =
                        pixels[i];

                    const A2AlteredPixel& altered =
                        alteredPixels[i];


                    const int xBin =
                        altered.a2Bin;


                    const int y0Bin =
                        a4Bins.indexOf(
                            pixel.a4MonitorBase
                        );


                    const int y1Bin =
                        a4Bins.indexOf(
                            altered.a4MonitorAltered
                        );


                    // -------------------------------------
                    // e00：
                    // A2 baseline + A4 baseline
                    // -------------------------------------

                    const long long d00 =
                        static_cast<long long>(
                            pixel.baseOutput
                            -
                            pixel.exactValue
                        );

                    const long long e00 =
                        d00 * d00;


                    // -------------------------------------
                    // e10：
                    // A2 altered + A4 baseline
                    // -------------------------------------

                    const long long d10 =
                        static_cast<long long>(
                            altered.outputWithBaseA4
                            -
                            pixel.exactValue
                        );

                    const long long e10 =
                        d10 * d10;


                    // -------------------------------------
                    // e01：
                    // A2 baseline + A4 altered
                    // -------------------------------------

                    const int output01 =
                        clampByte(
                            approximate::addSigned12(
                                pixel.a4MonitorBase,
                                -pixel.right,
                                a4Unit
                            )
                        );


                    const long long d01 =
                        static_cast<long long>(
                            output01
                            -
                            pixel.exactValue
                        );

                    const long long e01 =
                        d01 * d01;


                    // -------------------------------------
                    // e11：
                    // A2 altered + A4 altered
                    // -------------------------------------

                    const int output11 =
                        clampByte(
                            approximate::addSigned12(
                                altered.a4MonitorAltered,
                                -pixel.right,
                                a4Unit
                            )
                        );


                    const long long d11 =
                        static_cast<long long>(
                            output11
                            -
                            pixel.exactValue
                        );

                    const long long e11 =
                        d11 * d11;


                    // -------------------------------------
                    // A2 触发带来的变化
                    // -------------------------------------

                    const long long deltaA =
                        e10 - e00;


                    a2DeltaGlobal[xBin] +=
                        deltaA;


                    if (pixel.insideRoi)
                    {
                        a2DeltaRoi[xBin] +=
                            deltaA;
                    }


                    // -------------------------------------
                    // A2 未触发时：
                    // A4 改变的效果
                    // -------------------------------------

                    const long long deltaB =
                        e01 - e00;


                    bGlobal.add(
                        xBin,
                        y0Bin,
                        deltaB
                    );


                    if (pixel.insideRoi)
                    {
                        bRoi.add(
                            xBin,
                            y0Bin,
                            deltaB
                        );
                    }


                    // -------------------------------------
                    // A2 已触发时：
                    // A4 改变的效果
                    // -------------------------------------

                    const long long deltaC =
                        e11 - e10;


                    cGlobal.add(
                        xBin,
                        y1Bin,
                        deltaC
                    );


                    if (pixel.insideRoi)
                    {
                        cRoi.add(
                            xBin,
                            y1Bin,
                            deltaC
                        );
                    }
                }


                // -----------------------------------------
                // A2 一维累计和
                // -----------------------------------------

                for (int i = 1;
                     i < a2BinCount;
                     ++i)
                {
                    a2DeltaGlobal[i] +=
                        a2DeltaGlobal[i - 1];

                    a2DeltaRoi[i] +=
                        a2DeltaRoi[i - 1];
                }


                // -----------------------------------------
                // 二维累计和
                // -----------------------------------------

                bGlobal.build();
                bRoi.build();

                cGlobal.build();
                cRoi.build();


                // -----------------------------------------
                // 当前单元组合全部候选
                // -----------------------------------------

                std::vector<Candidate>
                    pairCandidates;


                // =========================================
                // 枚举 A2 所有连续 bin 区间
                // =========================================

                for (int a2LowerBin = 0;
                     a2LowerBin < a2BinCount;
                     ++a2LowerBin)
                {
                    for (
                        int a2UpperBin =
                            a2LowerBin;
                        a2UpperBin <
                            a2BinCount;
                        ++a2UpperBin
                    )
                    {
                        const long long
                            deltaAglobal =
                                rangeSum1D(
                                    a2DeltaGlobal,
                                    a2LowerBin,
                                    a2UpperBin
                                );


                        const long long
                            deltaAroi =
                                rangeSum1D(
                                    a2DeltaRoi,
                                    a2LowerBin,
                                    a2UpperBin
                                );


                        // =================================
                        // A4 所有连续 bin 区间
                        // =================================

                        for (
                            int a4LowerBin = 0;
                            a4LowerBin <
                                a4BinCount;
                            ++a4LowerBin
                        )
                        {
                            for (
                                int a4UpperBin =
                                    a4LowerBin;
                                a4UpperBin <
                                    a4BinCount;
                                ++a4UpperBin
                            )
                            {
                                // -------------------------
                                // A2 未触发情况下，
                                // 所有 A4 触发效果
                                // -------------------------

                                const long long
                                    allBglobal =
                                        bGlobal.rectangle(
                                            0,
                                            a2BinCount - 1,
                                            a4LowerBin,
                                            a4UpperBin
                                        );


                                const long long
                                    allBroi =
                                        bRoi.rectangle(
                                            0,
                                            a2BinCount - 1,
                                            a4LowerBin,
                                            a4UpperBin
                                        );


                                // -------------------------
                                // 但 A2 已触发的像素
                                // 不再走 y0 路径
                                // 所以减掉
                                // -------------------------

                                const long long
                                    removeBglobal =
                                        bGlobal.rectangle(
                                            a2LowerBin,
                                            a2UpperBin,
                                            a4LowerBin,
                                            a4UpperBin
                                        );


                                const long long
                                    removeBroi =
                                        bRoi.rectangle(
                                            a2LowerBin,
                                            a2UpperBin,
                                            a4LowerBin,
                                            a4UpperBin
                                        );


                                // -------------------------
                                // A2 已触发情况下，
                                // A4 使用 y1 判断
                                // -------------------------

                                const long long
                                    addCglobal =
                                        cGlobal.rectangle(
                                            a2LowerBin,
                                            a2UpperBin,
                                            a4LowerBin,
                                            a4UpperBin
                                        );


                                const long long
                                    addCroi =
                                        cRoi.rectangle(
                                            a2LowerBin,
                                            a2UpperBin,
                                            a4LowerBin,
                                            a4UpperBin
                                        );


                                // -------------------------
                                // 最终总误差
                                // -------------------------

                                const long long
                                    globalSquaredError =
                                        baseGlobalSquaredError
                                        +
                                        deltaAglobal
                                        +
                                        allBglobal
                                        -
                                        removeBglobal
                                        +
                                        addCglobal;


                                const long long
                                    roiSquaredError =
                                        baseRoiSquaredError
                                        +
                                        deltaAroi
                                        +
                                        allBroi
                                        -
                                        removeBroi
                                        +
                                        addCroi;


                                if (
                                    globalSquaredError
                                        <= 0
                                    ||
                                    roiSquaredError
                                        <= 0
                                )
                                {
                                    continue;
                                }


                                const double
                                    globalPsnr =
                                        mseToPsnr(
                                            static_cast<double>(
                                                globalSquaredError
                                            )
                                            /
                                            totalPixelCount
                                        );


                                const double
                                    roiPsnr =
                                        mseToPsnr(
                                            static_cast<double>(
                                                roiSquaredError
                                            )
                                            /
                                            static_cast<double>(
                                                roiPixelCount
                                            )
                                        );


                                pairCandidates.push_back(
                                    {
                                        a2Unit,
                                        a4Unit,

                                        a2Bins.lowerValue(
                                            a2LowerBin
                                        ),

                                        a2Bins.upperValue(
                                            a2UpperBin
                                        ),

                                        a4Bins.lowerValue(
                                            a4LowerBin
                                        ),

                                        a4Bins.upperValue(
                                            a4UpperBin
                                        ),

                                        globalSquaredError,
                                        roiSquaredError,

                                        globalPsnr,
                                        roiPsnr
                                    }
                                );


                                ++evaluatedCandidateCount;
                            }
                        }
                    }
                }


                // =========================================
                // 当前单元组合先做一次 Pareto 剪枝
                // =========================================

                std::vector<Candidate>
                    pairPareto =
                        extractPareto(
                            std::move(
                                pairCandidates
                            )
                        );


                allParetoCandidates.insert(
                    allParetoCandidates.end(),
                    pairPareto.begin(),
                    pairPareto.end()
                );
            }


            std::cout
                << "Finished A2 unit "
                << unitName(a2Unit)
                << "\n";
        }


        // =================================================
        // 5. 所有单元组合再做一次全局 Pareto
        // =================================================

        std::vector<Candidate>
            globalPareto =
                extractPareto(
                    std::move(
                        allParetoCandidates
                    )
                );


        std::cout
            << "\n========================================\n"
            << "Search finished\n"
            << "========================================\n";


        std::cout
            << "Evaluated candidates: "
            << evaluatedCandidateCount
            << "\n";

        std::cout
            << "Global Pareto size   : "
            << globalPareto.size()
            << "\n";


        // =================================================
        // 6. 保存完整 Pareto 前沿
        // =================================================

        const std::string csvPath =
            "results/joint_pareto_frontier.csv";


        std::ofstream csvFile(
            csvPath
        );


        csvFile
            << "index,"
            << "a2_unit,"
            << "a2_lower,"
            << "a2_upper,"
            << "a4_unit,"
            << "a4_lower,"
            << "a4_upper,"
            << "global_psnr,"
            << "roi_psnr\n";


        for (std::size_t i = 0;
             i < globalPareto.size();
             ++i)
        {
            const Candidate& c =
                globalPareto[i];


            csvFile
                << (i + 1)
                << ","
                << unitName(c.a2Unit)
                << ","
                << c.a2Lower
                << ","
                << c.a2Upper
                << ","
                << unitName(c.a4Unit)
                << ","
                << c.a4Lower
                << ","
                << c.a4Upper
                << ","
                << c.globalPsnr
                << ","
                << c.roiPsnr
                << "\n";
        }


        std::cout
            << "Saved: "
            << csvPath
            << "\n";


        // =================================================
        // 7. 打印约 20 个代表性 Pareto 点
        // =================================================

        std::cout
            << "\n========================================\n"
            << "Representative Pareto solutions\n"
            << "========================================\n";


        const std::size_t displayCount =
            std::min<std::size_t>(
                20,
                globalPareto.size()
            );


        for (std::size_t i = 0;
             i < displayCount;
             ++i)
        {
            std::size_t index = 0;


            if (displayCount > 1)
            {
                index =
                    i *
                    (globalPareto.size() - 1)
                    /
                    (displayCount - 1);
            }


            const Candidate& c =
                globalPareto[index];


            std::cout
                << "\n#"
                << (i + 1)
                << "\n";

            std::cout
                << "A2: "
                << unitName(c.a2Unit)
                << "  ["
                << c.a2Lower
                << ", "
                << c.a2Upper
                << "]\n";

            std::cout
                << "A4: "
                << unitName(c.a4Unit)
                << "  ["
                << c.a4Lower
                << ", "
                << c.a4Upper
                << "]\n";

            std::cout
                << "Global PSNR: "
                << c.globalPsnr
                << " dB\n";

            std::cout
                << "ROI PSNR   : "
                << c.roiPsnr
                << " dB\n";
        }


        // =================================================
        // 8. 用 Pareto 中间的一个方案做真实仿真验证
        // =================================================

        if (!globalPareto.empty())
        {
            const Candidate& testCandidate =
                globalPareto[
                    globalPareto.size() / 2
                ];


            const cv::Mat directImage =
                runDirectJointAttack(
                    inputImage,

                    testCandidate.a2Unit,
                    testCandidate.a2Lower,
                    testCandidate.a2Upper,

                    testCandidate.a4Unit,
                    testCandidate.a4Lower,
                    testCandidate.a4Upper
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


            std::cout
                << "\n========================================\n"
                << "Fast calculation validation\n"
                << "========================================\n";


            std::cout
                << "Fast Global PSNR  : "
                << testCandidate.globalPsnr
                << "\n";

            std::cout
                << "Direct Global PSNR: "
                << directGlobalPsnr
                << "\n";

            std::cout
                << "Difference         : "
                << std::abs(
                       testCandidate.globalPsnr
                       -
                       directGlobalPsnr
                   )
                << "\n\n";


            std::cout
                << "Fast ROI PSNR     : "
                << testCandidate.roiPsnr
                << "\n";

            std::cout
                << "Direct ROI PSNR   : "
                << directRoiPsnr
                << "\n";

            std::cout
                << "Difference        : "
                << std::abs(
                       testCandidate.roiPsnr
                       -
                       directRoiPsnr
                   )
                << "\n";
        }


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