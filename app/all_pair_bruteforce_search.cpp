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
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>


namespace
{

using Unit = approximate::ApproxUnitId;

const Unit BASE_UNIT =
    Unit::Add12se5RP;


// =========================================================
// 位置
// =========================================================

enum class Position
{
    A1 = 0,
    A2 = 1,
    A3 = 2,
    A4 = 3
};


int positionIndex(Position position)
{
    return static_cast<int>(position);
}


std::string positionName(Position position)
{
    switch (position)
    {
        case Position::A1:
            return "A1";

        case Position::A2:
            return "A2";

        case Position::A3:
            return "A3";

        case Position::A4:
            return "A4";
    }

    return "Unknown";
}


// =========================================================
// 加法器名称
// =========================================================

std::string unitName(Unit unit)
{
    switch (unit)
    {
        case Unit::Add12se5QT:
            return "5QT";

        case Unit::Add12se5QC:
            return "5QC";

        case Unit::Add12se5L8:
            return "5L8";

        case Unit::Add12se5TE:
            return "5TE";

        case Unit::Add12se5PD:
            return "5PD";

        case Unit::Add12se5PN:
            return "5PN";

        case Unit::Add12se5SB:
            return "5SB";

        case Unit::Add12se5Z0:
            return "5Z0";

        case Unit::Add12se5RP:
            return "5RP";
    }

    return "Unknown";
}


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


double psnrFromSse(
    long long squaredError,
    double pixelCount
)
{
    if (squaredError <= 0)
    {
        return
            std::numeric_limits<double>::infinity();
    }

    const double mse =
        static_cast<double>(squaredError)
        /
        pixelCount;

    return
        10.0
        *
        std::log10(
            (255.0 * 255.0) / mse
        );
}


// =========================================================
// 一次完整电路运行结果
// =========================================================

struct SimulationResult
{
    std::array<int, 4> monitor;

    int output;
};


// =========================================================
// 模拟整个 A1-A4
//
// changePosition：要改变的位置
//
// 如果 changePosition = -1，全部使用5RP
// =========================================================

SimulationResult simulateSingleChange(
    int startValue,
    const std::array<int, 4>& rhs,

    int changePosition,
    Unit changedUnit
)
{
    SimulationResult result{};

    int value =
        startValue;


    for (int i = 0;
         i < 4;
         ++i)
    {
        // 当前操作执行之前的 input1
        result.monitor[i] =
            value;


        Unit unit =
            BASE_UNIT;


        if (i == changePosition)
        {
            unit =
                changedUnit;
        }


        value =
            approximate::addSigned12(
                value,
                rhs[i],
                unit
            );
    }


    result.output =
        clampByte(value);


    return result;
}


// =========================================================
// 从某个较晚位置开始运行
//
// 输入 monitorValue 已经是该位置的 input1。
// 该位置使用 changedUnit，之后全部5RP。
// =========================================================

int finishFromPosition(
    int monitorValue,

    const std::array<int, 4>& rhs,

    int position,

    Unit changedUnit
)
{
    int value =
        monitorValue;


    for (int i = position;
         i < 4;
         ++i)
    {
        Unit unit =
            BASE_UNIT;


        if (i == position)
        {
            unit =
                changedUnit;
        }


        value =
            approximate::addSigned12(
                value,
                rhs[i],
                unit
            );
    }


    return clampByte(value);
}


// =========================================================
// 粗粒度区段
// =========================================================

struct QuantileBins
{
    int minimumValue = 0;

    std::vector<int> upperBounds;


    int count() const
    {
        return
            static_cast<int>(
                upperBounds.size()
            );
    }


    int indexOf(int value) const
    {
        auto it =
            std::lower_bound(
                upperBounds.begin(),
                upperBounds.end(),
                value
            );


        if (it == upperBounds.end())
        {
            return
                count() - 1;
        }


        return
            static_cast<int>(
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
            upperBounds[index - 1]
            + 1;
    }


    int upperValue(int index) const
    {
        return
            upperBounds[index];
    }
};


// =========================================================
// 按真实像素分布划分 bins
// =========================================================

QuantileBins buildQuantileBins(
    const std::vector<int>& values,
    int desiredBinCount
)
{
    if (values.empty())
    {
        throw std::runtime_error(
            "Cannot build bins from empty data."
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


    const int range =
        maximumValue -
        minimumValue +
        1;


    std::vector<long long>
        histogram(
            range,
            0
        );


    for (int value : values)
    {
        ++histogram[
            value - minimumValue
        ];
    }


    QuantileBins bins;

    bins.minimumValue =
        minimumValue;


    const long long total =
        static_cast<long long>(
            values.size()
        );


    long long cumulative =
        0;

    int valueIndex =
        0;


    for (int bin = 1;
         bin <= desiredBinCount;
         ++bin)
    {
        const long long target =
            (
                total * bin
                +
                desiredBinCount - 1
            )
            /
            desiredBinCount;


        while (
            valueIndex < range
            &&
            cumulative < target
        )
        {
            cumulative +=
                histogram[valueIndex];

            ++valueIndex;
        }


        const int upper =
            minimumValue
            +
            valueIndex
            -
            1;


        if (
            bins.upperBounds.empty()
            ||
            bins.upperBounds.back()
                != upper
        )
        {
            bins.upperBounds.push_back(
                upper
            );
        }
    }


    if (
        bins.upperBounds.empty()
        ||
        bins.upperBounds.back()
            != maximumValue
    )
    {
        bins.upperBounds.push_back(
            maximumValue
        );
    }


    return bins;
}


// =========================================================
// 一维区间累计
// =========================================================

long long rangeSum(
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
                (rows + 1)
                *
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
                ]
                +=
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
            row0 > row1
            ||
            col0 > col1
        )
        {
            return 0;
        }


        const int r0 =
            row0;

        const int r1 =
            row1 + 1;

        const int c0 =
            col0;

        const int c1 =
            col1 + 1;


        return
            data_[index(r1, c1)]
            -
            data_[index(r0, c1)]
            -
            data_[index(r1, c0)]
            +
            data_[index(r0, c0)];
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
                +
                col
            );
    }
};


// =========================================================
// 每个像素的 baseline 信息
// =========================================================

struct Pixel
{
    int startValue;

    std::array<int, 4> rhs;

    std::array<int, 4> baseMonitor;

    int exactValue;

    int baseOutput;

    long long baseSquaredError;

    bool insideRoi;
};


// =========================================================
// 第一个位置发生改变后的信息
// =========================================================

struct FirstAlteredPixel
{
    int secondMonitor;

    long long error10;

    long long deltaA;

    int firstBin;
};


// =========================================================
// 搜索结果
// =========================================================

struct Candidate
{
    Position firstPosition;

    Position secondPosition;

    Unit firstUnit;

    Unit secondUnit;

    int firstLower;

    int firstUpper;

    int secondLower;

    int secondUpper;

    long long globalSquaredError;

    long long roiSquaredError;
};


// =========================================================
// Pareto 筛选
//
// Global SSE：越小越好
// ROI SSE：越大越好
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


    std::vector<Candidate> result;


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
            result.push_back(
                candidate
            );


            bestRoiSquaredError =
                candidate.roiSquaredError;
        }
    }


    return result;
}


// =========================================================
// 保存 Pareto CSV
// =========================================================

void savePareto(
    const std::string& path,

    const std::vector<Candidate>& candidates,

    double globalPixelCount,
    double roiPixelCount
)
{
    std::ofstream file(path);


    file
        << "index,"
        << "position1,"
        << "unit1,"
        << "lower1,"
        << "upper1,"
        << "position2,"
        << "unit2,"
        << "lower2,"
        << "upper2,"
        << "global_psnr,"
        << "roi_psnr,"
        << "psnr_gap\n";


    for (std::size_t i = 0;
         i < candidates.size();
         ++i)
    {
        const Candidate& c =
            candidates[i];


        const double globalPsnr =
            psnrFromSse(
                c.globalSquaredError,
                globalPixelCount
            );


        const double roiPsnr =
            psnrFromSse(
                c.roiSquaredError,
                roiPixelCount
            );


        file
            << (i + 1)
            << ","
            << positionName(
                   c.firstPosition
               )
            << ","
            << unitName(c.firstUnit)
            << ","
            << c.firstLower
            << ","
            << c.firstUpper
            << ","
            << positionName(
                   c.secondPosition
               )
            << ","
            << unitName(c.secondUnit)
            << ","
            << c.secondLower
            << ","
            << c.secondUpper
            << ","
            << globalPsnr
            << ","
            << roiPsnr
            << ","
            << globalPsnr - roiPsnr
            << "\n";
    }
}


// =========================================================
// 真实运行一个双位置方案
//
// 第二个位置的判断会自然受到第一个位置影响
// =========================================================

cv::Mat runDirectPairAttack(
    const cv::Mat& inputImage,

    const Candidate& candidate
)
{
    cv::Mat outputImage =
        inputImage.clone();


    const int firstPosition =
        positionIndex(
            candidate.firstPosition
        );


    const int secondPosition =
        positionIndex(
            candidate.secondPosition
        );


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


            const std::array<int, 4> rhs =
            {
                -top,
                -bottom,
                -left,
                -right
            };


            int value =
                5 * center;


            for (int i = 0;
                 i < 4;
                 ++i)
            {
                Unit unit =
                    BASE_UNIT;


                if (
                    i == firstPosition
                    &&
                    value >= candidate.firstLower
                    &&
                    value <= candidate.firstUpper
                )
                {
                    unit =
                        candidate.firstUnit;
                }


                if (
                    i == secondPosition
                    &&
                    value >= candidate.secondLower
                    &&
                    value <= candidate.secondUpper
                )
                {
                    unit =
                        candidate.secondUnit;
                }


                value =
                    approximate::addSigned12(
                        value,
                        rhs[i],
                        unit
                    );
            }


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


    const int desiredBinCount =
        25;


    const std::array<Unit, 8>
        candidateUnits =
    {
        Unit::Add12se5QT,
        Unit::Add12se5QC,
        Unit::Add12se5L8,
        Unit::Add12se5TE,
        Unit::Add12se5PD,
        Unit::Add12se5PN,
        Unit::Add12se5SB,
        Unit::Add12se5Z0
    };


    const std::array<
        std::pair<Position, Position>,
        6
    >
    positionPairs =
    {{
        {Position::A1, Position::A2},
        {Position::A1, Position::A3},
        {Position::A1, Position::A4},

        {Position::A2, Position::A3},
        {Position::A2, Position::A4},

        {Position::A3, Position::A4}
    }};


    try
    {
        std::filesystem::create_directories(
            "results/all_pair_search"
        );


        // =================================================
        // 输入
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
        // ROI 总像素数
        // =================================================

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


        const double globalPixelCount =
            static_cast<double>(
                inputImage.rows
                *
                inputImage.cols
            );


        const double roiPixelCountDouble =
            static_cast<double>(
                roiPixelCount
            );


        // =================================================
        // baseline 预计算
        // =================================================

        std::vector<Pixel> pixels;


        pixels.reserve(
            static_cast<std::size_t>(
                (inputImage.rows - 2)
                *
                (inputImage.cols - 2)
            )
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


                const std::array<int, 4> rhs =
                {
                    -top,
                    -bottom,
                    -left,
                    -right
                };


                const int startValue =
                    5 * center;


                const SimulationResult baseline =
                    simulateSingleChange(
                        startValue,
                        rhs,
                        -1,
                        BASE_UNIT
                    );


                const int exactValue =
                    exactImage.at<unsigned char>(
                        row,
                        col
                    );


                const long long difference =
                    static_cast<long long>(
                        baseline.output
                        -
                        exactValue
                    );


                const long long squaredError =
                    difference
                    *
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
                        startValue,
                        rhs,
                        baseline.monitor,
                        exactValue,
                        baseline.output,
                        squaredError,
                        insideRoi
                    }
                );
            }
        }


        const double baseGlobalPsnr =
            psnrFromSse(
                baseGlobalSquaredError,
                globalPixelCount
            );


        const double baseRoiPsnr =
            psnrFromSse(
                baseRoiSquaredError,
                roiPixelCountDouble
            );


        std::cout
            << std::fixed
            << std::setprecision(6);


        std::cout
            << "\n========================================\n"
            << "Baseline 5RP x 4\n"
            << "========================================\n"
            << "Global PSNR: "
            << baseGlobalPsnr
            << " dB\n"
            << "ROI PSNR   : "
            << baseRoiPsnr
            << " dB\n";


        // =================================================
        // 所有位置组合
        // =================================================

        std::vector<Candidate>
            allPairParetoCandidates;


        std::ofstream summaryFile(
            "results/all_pair_search/"
            "pair_summary.csv"
        );


        summaryFile
            << "pair,"
            << "evaluated_candidates,"
            << "pareto_size,"
            << "validation_global_difference,"
            << "validation_roi_difference\n";


        for (const auto& positionPair :
             positionPairs)
        {
            const Position firstPosition =
                positionPair.first;

            const Position secondPosition =
                positionPair.second;


            const int firstIndex =
                positionIndex(
                    firstPosition
                );

            const int secondIndex =
                positionIndex(
                    secondPosition
                );


            const std::string pairName =
                positionName(firstPosition)
                +
                "_"
                +
                positionName(secondPosition);


            std::cout
                << "\n========================================\n"
                << "Searching "
                << pairName
                << "\n"
                << "========================================\n";


            // =============================================
            // 第一个位置的 baseline monitor
            // =============================================

            std::vector<int>
                firstMonitorValues;


            firstMonitorValues.reserve(
                pixels.size()
            );


            for (const Pixel& pixel :
                 pixels)
            {
                firstMonitorValues.push_back(
                    pixel.baseMonitor[
                        firstIndex
                    ]
                );
            }


            const QuantileBins firstBins =
                buildQuantileBins(
                    firstMonitorValues,
                    desiredBinCount
                );


            std::cout
                << positionName(firstPosition)
                << " bins: "
                << firstBins.count()
                << "\n";


            std::vector<Candidate>
                pairParetoCandidates;


            long long evaluatedCandidateCount =
                0;


            // =============================================
            // 第一个位置替换单元
            // =============================================

            for (Unit firstUnit :
                 candidateUnits)
            {
                std::cout
                    << "  "
                    << positionName(firstPosition)
                    << " unit "
                    << unitName(firstUnit)
                    << " ...\n";


                std::vector<
                    FirstAlteredPixel
                >
                alteredPixels;


                alteredPixels.reserve(
                    pixels.size()
                );


                std::vector<int>
                    secondMonitorValues;


                secondMonitorValues.reserve(
                    pixels.size() * 2
                );


                std::vector<long long>
                    deltaAGlobal(
                        firstBins.count(),
                        0
                    );


                std::vector<long long>
                    deltaARoi(
                        firstBins.count(),
                        0
                    );


                // =========================================
                // 只改变第一个位置
                // =========================================

                for (const Pixel& pixel :
                     pixels)
                {
                    const SimulationResult altered =
                        simulateSingleChange(
                            pixel.startValue,
                            pixel.rhs,
                            firstIndex,
                            firstUnit
                        );


                    const int secondMonitor =
                        altered.monitor[
                            secondIndex
                        ];


                    const long long difference10 =
                        static_cast<long long>(
                            altered.output
                            -
                            pixel.exactValue
                        );


                    const long long error10 =
                        difference10
                        *
                        difference10;


                    const long long deltaA =
                        error10
                        -
                        pixel.baseSquaredError;


                    const int firstBin =
                        firstBins.indexOf(
                            pixel.baseMonitor[
                                firstIndex
                            ]
                        );


                    alteredPixels.push_back(
                        {
                            secondMonitor,
                            error10,
                            deltaA,
                            firstBin
                        }
                    );


                    deltaAGlobal[
                        firstBin
                    ] +=
                        deltaA;


                    if (pixel.insideRoi)
                    {
                        deltaARoi[
                            firstBin
                        ] +=
                            deltaA;
                    }


                    // 第二个位置需要同时覆盖：
                    // 1. 第一个位置不触发时
                    // 2. 第一个位置触发时

                    secondMonitorValues.push_back(
                        pixel.baseMonitor[
                            secondIndex
                        ]
                    );


                    secondMonitorValues.push_back(
                        secondMonitor
                    );
                }


                // 一维累计
                for (int i = 1;
                     i < firstBins.count();
                     ++i)
                {
                    deltaAGlobal[i] +=
                        deltaAGlobal[i - 1];

                    deltaARoi[i] +=
                        deltaARoi[i - 1];
                }


                // =========================================
                // 第二个位置 bins
                // =========================================

                const QuantileBins secondBins =
                    buildQuantileBins(
                        secondMonitorValues,
                        desiredBinCount
                    );


                // =========================================
                // 第二个位置替换单元
                // =========================================

                for (Unit secondUnit :
                     candidateUnits)
                {
                    Prefix2D bGlobal(
                        firstBins.count(),
                        secondBins.count()
                    );

                    Prefix2D bRoi(
                        firstBins.count(),
                        secondBins.count()
                    );

                    Prefix2D cGlobal(
                        firstBins.count(),
                        secondBins.count()
                    );

                    Prefix2D cRoi(
                        firstBins.count(),
                        secondBins.count()
                    );


                    // =====================================
                    // 构建误差变化表
                    // =====================================

                    for (std::size_t i = 0;
                         i < pixels.size();
                         ++i)
                    {
                        const Pixel& pixel =
                            pixels[i];


                        const FirstAlteredPixel&
                            altered =
                                alteredPixels[i];


                        const int firstBin =
                            altered.firstBin;


                        // ---------------------------------
                        // 第一个位置不触发：
                        // 第二个位置 monitor
                        // ---------------------------------

                        const int secondBin0 =
                            secondBins.indexOf(
                                pixel.baseMonitor[
                                    secondIndex
                                ]
                            );


                        // ---------------------------------
                        // 第一个位置触发：
                        // 第二个位置 monitor
                        // ---------------------------------

                        const int secondBin1 =
                            secondBins.indexOf(
                                altered.secondMonitor
                            );


                        // =================================
                        // e01
                        //
                        // 第一个位置 baseline
                        // 第二个位置 changed
                        // =================================

                        const int output01 =
                            finishFromPosition(
                                pixel.baseMonitor[
                                    secondIndex
                                ],

                                pixel.rhs,

                                secondIndex,

                                secondUnit
                            );


                        const long long difference01 =
                            static_cast<long long>(
                                output01
                                -
                                pixel.exactValue
                            );


                        const long long error01 =
                            difference01
                            *
                            difference01;


                        const long long deltaB =
                            error01
                            -
                            pixel.baseSquaredError;


                        bGlobal.add(
                            firstBin,
                            secondBin0,
                            deltaB
                        );


                        if (pixel.insideRoi)
                        {
                            bRoi.add(
                                firstBin,
                                secondBin0,
                                deltaB
                            );
                        }


                        // =================================
                        // e11
                        //
                        // 第一个位置 changed
                        // 第二个位置 changed
                        // =================================

                        const int output11 =
                            finishFromPosition(
                                altered.secondMonitor,

                                pixel.rhs,

                                secondIndex,

                                secondUnit
                            );


                        const long long difference11 =
                            static_cast<long long>(
                                output11
                                -
                                pixel.exactValue
                            );


                        const long long error11 =
                            difference11
                            *
                            difference11;


                        const long long deltaC =
                            error11
                            -
                            altered.error10;


                        cGlobal.add(
                            firstBin,
                            secondBin1,
                            deltaC
                        );


                        if (pixel.insideRoi)
                        {
                            cRoi.add(
                                firstBin,
                                secondBin1,
                                deltaC
                            );
                        }
                    }


                    bGlobal.build();
                    bRoi.build();

                    cGlobal.build();
                    cRoi.build();


                    // =====================================
                    // 当前两个单元的全部区间组合
                    // =====================================

                    std::vector<Candidate>
                        unitPairCandidates;


                    const int firstBinCount =
                        firstBins.count();


                    const int secondBinCount =
                        secondBins.count();


                    const std::size_t
                        firstIntervalCount =
                            static_cast<std::size_t>(
                                firstBinCount
                                *
                                (firstBinCount + 1)
                                /
                                2
                            );


                    const std::size_t
                        secondIntervalCount =
                            static_cast<std::size_t>(
                                secondBinCount
                                *
                                (secondBinCount + 1)
                                /
                                2
                            );


                    unitPairCandidates.reserve(
                        firstIntervalCount
                        *
                        secondIntervalCount
                    );


                    // =====================================
                    // 枚举第一个位置区间
                    // =====================================

                    for (int firstLowerBin = 0;
                         firstLowerBin <
                             firstBinCount;
                         ++firstLowerBin)
                    {
                        for (
                            int firstUpperBin =
                                firstLowerBin;

                            firstUpperBin <
                                firstBinCount;

                            ++firstUpperBin
                        )
                        {
                            const long long
                                deltaAglobal =
                                    rangeSum(
                                        deltaAGlobal,
                                        firstLowerBin,
                                        firstUpperBin
                                    );


                            const long long
                                deltaAroi =
                                    rangeSum(
                                        deltaARoi,
                                        firstLowerBin,
                                        firstUpperBin
                                    );


                            // =================================
                            // 枚举第二个位置区间
                            // =================================

                            for (
                                int secondLowerBin = 0;

                                secondLowerBin <
                                    secondBinCount;

                                ++secondLowerBin
                            )
                            {
                                for (
                                    int secondUpperBin =
                                        secondLowerBin;

                                    secondUpperBin <
                                        secondBinCount;

                                    ++secondUpperBin
                                )
                                {
                                    // =========================
                                    // 第一个位置不触发时
                                    // 第二个位置产生的影响
                                    // =========================

                                    const long long
                                        allBGlobal =
                                            bGlobal.rectangle(
                                                0,
                                                firstBinCount - 1,

                                                secondLowerBin,
                                                secondUpperBin
                                            );


                                    const long long
                                        allBRoi =
                                            bRoi.rectangle(
                                                0,
                                                firstBinCount - 1,

                                                secondLowerBin,
                                                secondUpperBin
                                            );


                                    // =========================
                                    // 第一个位置已经触发的像素
                                    // 不能再使用 baseline
                                    // 第二位置 monitor
                                    // =========================

                                    const long long
                                        removeBGlobal =
                                            bGlobal.rectangle(
                                                firstLowerBin,
                                                firstUpperBin,

                                                secondLowerBin,
                                                secondUpperBin
                                            );


                                    const long long
                                        removeBRoi =
                                            bRoi.rectangle(
                                                firstLowerBin,
                                                firstUpperBin,

                                                secondLowerBin,
                                                secondUpperBin
                                            );


                                    // =========================
                                    // 第一个位置触发以后，
                                    // 使用新的第二位置 monitor
                                    // =========================

                                    const long long
                                        addCGlobal =
                                            cGlobal.rectangle(
                                                firstLowerBin,
                                                firstUpperBin,

                                                secondLowerBin,
                                                secondUpperBin
                                            );


                                    const long long
                                        addCRoi =
                                            cRoi.rectangle(
                                                firstLowerBin,
                                                firstUpperBin,

                                                secondLowerBin,
                                                secondUpperBin
                                            );


                                    const long long
                                        globalSquaredError =
                                            baseGlobalSquaredError

                                            +
                                            deltaAglobal

                                            +
                                            allBGlobal

                                            -
                                            removeBGlobal

                                            +
                                            addCGlobal;


                                    const long long
                                        roiSquaredError =
                                            baseRoiSquaredError

                                            +
                                            deltaAroi

                                            +
                                            allBRoi

                                            -
                                            removeBRoi

                                            +
                                            addCRoi;


                                    if (
                                        globalSquaredError <= 0
                                        ||
                                        roiSquaredError <= 0
                                    )
                                    {
                                        continue;
                                    }


                                    unitPairCandidates.push_back(
                                        {
                                            firstPosition,
                                            secondPosition,

                                            firstUnit,
                                            secondUnit,

                                            firstBins.lowerValue(
                                                firstLowerBin
                                            ),

                                            firstBins.upperValue(
                                                firstUpperBin
                                            ),

                                            secondBins.lowerValue(
                                                secondLowerBin
                                            ),

                                            secondBins.upperValue(
                                                secondUpperBin
                                            ),

                                            globalSquaredError,
                                            roiSquaredError
                                        }
                                    );


                                    ++evaluatedCandidateCount;
                                }
                            }
                        }
                    }


                    // 当前两个单元先压缩
                    std::vector<Candidate>
                        unitPareto =
                            extractPareto(
                                std::move(
                                    unitPairCandidates
                                )
                            );


                    pairParetoCandidates.insert(
                        pairParetoCandidates.end(),

                        unitPareto.begin(),
                        unitPareto.end()
                    );
                }
            }


            // =============================================
            // 当前位置组合的最终 Pareto
            // =============================================

            std::vector<Candidate>
                pairPareto =
                    extractPareto(
                        std::move(
                            pairParetoCandidates
                        )
                    );


            const std::string pairCsvPath =
                "results/all_pair_search/"
                +
                pairName
                +
                "_pareto.csv";


            savePareto(
                pairCsvPath,

                pairPareto,

                globalPixelCount,
                roiPixelCountDouble
            );


            std::cout
                << "\n"
                << pairName
                << " finished\n";

            std::cout
                << "Evaluated candidates: "
                << evaluatedCandidateCount
                << "\n";

            std::cout
                << "Pareto size         : "
                << pairPareto.size()
                << "\n";

            std::cout
                << "Saved               : "
                << pairCsvPath
                << "\n";


            // =============================================
            // 真实仿真验证
            // =============================================

            double globalDifference =
                0.0;

            double roiDifference =
                0.0;


            if (!pairPareto.empty())
            {
                const Candidate& testCandidate =
                    pairPareto[
                        pairPareto.size() / 2
                    ];


                const cv::Mat directImage =
                    runDirectPairAttack(
                        inputImage,
                        testCandidate
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


                const double fastGlobalPsnr =
                    psnrFromSse(
                        testCandidate.globalSquaredError,
                        globalPixelCount
                    );


                const double fastRoiPsnr =
                    psnrFromSse(
                        testCandidate.roiSquaredError,
                        roiPixelCountDouble
                    );


                globalDifference =
                    std::abs(
                        fastGlobalPsnr
                        -
                        directGlobalPsnr
                    );


                roiDifference =
                    std::abs(
                        fastRoiPsnr
                        -
                        directRoiPsnr
                    );


                std::cout
                    << "Validation Global difference: "
                    << globalDifference
                    << " dB\n";

                std::cout
                    << "Validation ROI difference   : "
                    << roiDifference
                    << " dB\n";
            }


            summaryFile
                << pairName
                << ","
                << evaluatedCandidateCount
                << ","
                << pairPareto.size()
                << ","
                << globalDifference
                << ","
                << roiDifference
                << "\n";


            allPairParetoCandidates.insert(
                allPairParetoCandidates.end(),

                pairPareto.begin(),
                pairPareto.end()
            );
        }


        // =================================================
        // 6组位置组合再次统一比较
        // =================================================

        std::vector<Candidate>
            globalPareto =
                extractPareto(
                    std::move(
                        allPairParetoCandidates
                    )
                );


        savePareto(
            "results/all_pair_search/"
            "all_pairs_pareto.csv",

            globalPareto,

            globalPixelCount,
            roiPixelCountDouble
        );


        // =================================================
        // 打印20个全局代表点
        // =================================================

        std::cout
            << "\n========================================\n"
            << "All pair search finished\n"
            << "========================================\n";

        std::cout
            << "Global Pareto size: "
            << globalPareto.size()
            << "\n";


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
                    i
                    *
                    (globalPareto.size() - 1)
                    /
                    (displayCount - 1);
            }


            const Candidate& c =
                globalPareto[index];


            const double globalPsnr =
                psnrFromSse(
                    c.globalSquaredError,
                    globalPixelCount
                );


            const double roiPsnr =
                psnrFromSse(
                    c.roiSquaredError,
                    roiPixelCountDouble
                );


            std::cout
                << "\n#"
                << (i + 1)
                << "\n";

            std::cout
                << positionName(c.firstPosition)
                << ": "
                << unitName(c.firstUnit)
                << " ["
                << c.firstLower
                << ", "
                << c.firstUpper
                << "]\n";

            std::cout
                << positionName(c.secondPosition)
                << ": "
                << unitName(c.secondUnit)
                << " ["
                << c.secondLower
                << ", "
                << c.secondUpper
                << "]\n";

            std::cout
                << "Global PSNR: "
                << globalPsnr
                << " dB\n";

            std::cout
                << "ROI PSNR   : "
                << roiPsnr
                << " dB\n";

            std::cout
                << "PSNR gap   : "
                << globalPsnr - roiPsnr
                << " dB\n";
        }


        std::cout
            << "\nSaved:\n"
            << "results/all_pair_search/"
            << "pair_summary.csv\n"
            << "results/all_pair_search/"
            << "all_pairs_pareto.csv\n";


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