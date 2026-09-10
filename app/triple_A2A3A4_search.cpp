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
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>


namespace
{

using Unit =
    approximate::ApproxUnitId;


const Unit BASE_UNIT =
    Unit::Add12se5RP;


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


bool insideInterval(
    int value,
    int lower,
    int upper
)
{
    return
        value >= lower
        &&
        value <= upper;
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
        static_cast<double>(
            squaredError
        )
        /
        pixelCount;


    return
        10.0
        *
        std::log10(
            (255.0 * 255.0)
            /
            mse
        );
}


// =========================================================
// 加法器名称
// =========================================================

std::string unitName(Unit unit)
{
    switch (unit)
    {
        case Unit::Add12se5L8:
            return "5L8";

        case Unit::Add12se5PD:
            return "5PD";

        case Unit::Add12se5PN:
            return "5PN";

        case Unit::Add12se5QC:
            return "5QC";

        case Unit::Add12se5QT:
            return "5QT";

        case Unit::Add12se5RP:
            return "5RP";

        case Unit::Add12se5TE:
            return "5TE";

        case Unit::Add12se5SB:
            return "5SB";

        case Unit::Add12se5Z0:
            return "5Z0";
    }

    throw std::runtime_error(
        "Unknown approximate unit."
    );
}


Unit parseUnit(
    const std::string& name
)
{
    if (name == "5L8")
    {
        return Unit::Add12se5L8;
    }

    if (name == "5PD")
    {
        return Unit::Add12se5PD;
    }

    if (name == "5PN")
    {
        return Unit::Add12se5PN;
    }

    if (name == "5QC")
    {
        return Unit::Add12se5QC;
    }

    if (name == "5QT")
    {
        return Unit::Add12se5QT;
    }

    if (name == "5RP")
    {
        return Unit::Add12se5RP;
    }

    if (name == "5TE")
    {
        return Unit::Add12se5TE;
    }

    if (name == "5SB")
    {
        return Unit::Add12se5SB;
    }

    if (name == "5Z0")
    {
        return Unit::Add12se5Z0;
    }


    throw std::runtime_error(
        "Unknown unit name in CSV: "
        + name
    );
}


// =========================================================
// 双位置候选
//
// 这里只保存：
// A2 + A3
// =========================================================

struct PairCandidate
{
    Unit a2Unit;

    int a2Lower;
    int a2Upper;


    Unit a3Unit;

    int a3Lower;
    int a3Upper;


    double csvGlobalPsnr;
    double csvRoiPsnr;
};


// =========================================================
// 每个像素固定不变的信息
// =========================================================

struct PixelInput
{
    int startValue;

    std::array<int, 4> rhs;

    int exactValue;

    bool insideRoi;
};


// =========================================================
// 固定 A2+A3 后，每个像素在 A4 前的信息
// =========================================================

struct PairPixelState
{
    int a4Monitor;

    int baseOutput;

    long long baseSquaredError;

    bool insideRoi;
};


// =========================================================
// 三位置搜索结果
// =========================================================

struct TripleCandidate
{
    Unit a2Unit;

    int a2Lower;
    int a2Upper;


    Unit a3Unit;

    int a3Lower;
    int a3Upper;


    Unit a4Unit;

    int a4Lower;
    int a4Upper;


    double pairGlobalPsnr;
    double pairRoiPsnr;


    long long globalSquaredError;
    long long roiSquaredError;
};


// =========================================================
// 25-bin 区段
// =========================================================

struct QuantileBins
{
    int minimumValue =
        0;

    std::vector<int>
        upperBounds;


    int count() const
    {
        return
            static_cast<int>(
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


        if (it ==
            upperBounds.end())
        {
            return
                count() - 1;
        }


        return
            static_cast<int>(
                it
                -
                upperBounds.begin()
            );
    }


    int lowerValue(int index) const
    {
        if (index == 0)
        {
            return minimumValue;
        }


        return
            upperBounds[
                index - 1
            ]
            +
            1;
    }


    int upperValue(int index) const
    {
        return
            upperBounds[index];
    }
};


// =========================================================
// 根据真实 A4_input1 分布划分约25个区段
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
        maximumValue
        -
        minimumValue
        +
        1;


    std::vector<long long>
        histogram(
            range,
            0
        );


    for (int value : values)
    {
        ++histogram[
            value
            -
            minimumValue
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
                histogram[
                    valueIndex
                ];

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
// 一维累计和查询
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
            prefix[
                lower - 1
            ];
    }


    return result;
}


// =========================================================
// 读取 all_pairs_pareto.csv
//
// 当前版本按之前确定的方案：
// 1. 只取 A2+A3
// 2. Global PSNR 在 [30,34]
// =========================================================

std::vector<PairCandidate>
readPairCandidates(
    const std::string& csvPath
)
{
    std::ifstream file(
        csvPath
    );


    if (!file.is_open())
    {
        throw std::runtime_error(
            "Cannot open pair Pareto CSV: "
            + csvPath
        );
    }


    std::vector<PairCandidate>
        result;


    std::string line;


    // 跳过标题
    std::getline(
        file,
        line
    );


    while (
        std::getline(
            file,
            line
        )
    )
    {
        if (line.empty())
        {
            continue;
        }


        std::stringstream ss(
            line
        );


        std::array<std::string, 12>
            fields;


        for (std::size_t i = 0;
             i < fields.size();
             ++i)
        {
            if (
                !std::getline(
                    ss,
                    fields[i],
                    ','
                )
            )
            {
                throw std::runtime_error(
                    "Invalid CSV row: "
                    + line
                );
            }
        }


        const std::string&
            position1 =
                fields[1];

        const std::string&
            position2 =
                fields[5];


        const double globalPsnr =
            std::stod(
                fields[9]
            );


        const double roiPsnr =
            std::stod(
                fields[10]
            );


        // -----------------------------------------
        // 只保留我们这一轮需要的 A2+A3
        // -----------------------------------------

        if (
            position1 != "A2"
            ||
            position2 != "A3"
        )
        {
            continue;
        }


        // -----------------------------------------
        // 当前三位置实验的双位置筛选范围
        // -----------------------------------------

        if (
            globalPsnr < 30.0
            ||
            globalPsnr > 34.0
        )
        {
            continue;
        }


        PairCandidate candidate;


        candidate.a2Unit =
            parseUnit(
                fields[2]
            );

        candidate.a2Lower =
            std::stoi(
                fields[3]
            );

        candidate.a2Upper =
            std::stoi(
                fields[4]
            );


        candidate.a3Unit =
            parseUnit(
                fields[6]
            );

        candidate.a3Lower =
            std::stoi(
                fields[7]
            );

        candidate.a3Upper =
            std::stoi(
                fields[8]
            );


        candidate.csvGlobalPsnr =
            globalPsnr;

        candidate.csvRoiPsnr =
            roiPsnr;


        result.push_back(
            candidate
        );
    }


    return result;
}


// =========================================================
// 根据固定 A2+A3 方案运行一个像素
//
// A1 = 5RP
//
// A2：区间决定 5RP / candidate.a2Unit
//
// A3：使用经过A2改变后的真实输入重新判断
//
// A4 = 5RP
//
// 同时记录执行A4之前的 input1
// =========================================================

PairPixelState simulatePairPixel(
    const PixelInput& pixel,
    const PairCandidate& candidate
)
{
    int value =
        pixel.startValue;


    // -----------------------------------------------------
    // A1
    // -----------------------------------------------------

    value =
        approximate::addSigned12(
            value,
            pixel.rhs[0],
            BASE_UNIT
        );


    // -----------------------------------------------------
    // A2
    // -----------------------------------------------------

    Unit a2Unit =
        BASE_UNIT;


    if (
        insideInterval(
            value,
            candidate.a2Lower,
            candidate.a2Upper
        )
    )
    {
        a2Unit =
            candidate.a2Unit;
    }


    value =
        approximate::addSigned12(
            value,
            pixel.rhs[1],
            a2Unit
        );


    // -----------------------------------------------------
    // A3
    //
    // 注意：
    // 此时 value 已经包含 A2 的真实影响
    // -----------------------------------------------------

    Unit a3Unit =
        BASE_UNIT;


    if (
        insideInterval(
            value,
            candidate.a3Lower,
            candidate.a3Upper
        )
    )
    {
        a3Unit =
            candidate.a3Unit;
    }


    value =
        approximate::addSigned12(
            value,
            pixel.rhs[2],
            a3Unit
        );


    // -----------------------------------------------------
    // value 即当前真实的 A4_input1
    // -----------------------------------------------------

    const int a4Monitor =
        value;


    // -----------------------------------------------------
    // A4 暂时保持 baseline 5RP
    // -----------------------------------------------------

    value =
        approximate::addSigned12(
            value,
            pixel.rhs[3],
            BASE_UNIT
        );


    const int finalOutput =
        clampByte(
            value
        );


    const long long difference =
        static_cast<long long>(
            finalOutput
            -
            pixel.exactValue
        );


    const long long squaredError =
        difference
        *
        difference;


    return
    {
        a4Monitor,
        finalOutput,
        squaredError,
        pixel.insideRoi
    };
}


// =========================================================
// 真正运行一个三位置方案
//
// 只用于最后验证快速搜索是否准确
// =========================================================

cv::Mat runDirectTripleAttack(
    const cv::Mat& inputImage,
    const TripleCandidate& candidate
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


            int value =
                5 * center;


            // =============================================
            // A1
            // =============================================

            value =
                approximate::addSigned12(
                    value,
                    -top,
                    BASE_UNIT
                );


            // =============================================
            // A2
            // =============================================

            Unit a2Unit =
                BASE_UNIT;


            if (
                insideInterval(
                    value,
                    candidate.a2Lower,
                    candidate.a2Upper
                )
            )
            {
                a2Unit =
                    candidate.a2Unit;
            }


            value =
                approximate::addSigned12(
                    value,
                    -bottom,
                    a2Unit
                );


            // =============================================
            // A3
            // =============================================

            Unit a3Unit =
                BASE_UNIT;


            if (
                insideInterval(
                    value,
                    candidate.a3Lower,
                    candidate.a3Upper
                )
            )
            {
                a3Unit =
                    candidate.a3Unit;
            }


            value =
                approximate::addSigned12(
                    value,
                    -left,
                    a3Unit
                );


            // =============================================
            // A4
            // =============================================

            Unit a4Unit =
                BASE_UNIT;


            if (
                insideInterval(
                    value,
                    candidate.a4Lower,
                    candidate.a4Upper
                )
            )
            {
                a4Unit =
                    candidate.a4Unit;
            }


            value =
                approximate::addSigned12(
                    value,
                    -right,
                    a4Unit
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
// Pareto筛选
//
// Global SSE 越小越好
// ROI SSE 越大越好
//
// 等价于：
//
// Global PSNR 越高越好
// ROI PSNR 越低越好
// =========================================================

std::vector<TripleCandidate>
extractPareto(
    std::vector<TripleCandidate>
        candidates
)
{
    std::sort(
        candidates.begin(),
        candidates.end(),

        [](
            const TripleCandidate& a,
            const TripleCandidate& b
        )
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


    std::vector<TripleCandidate>
        pareto;


    long long bestRoiError =
        std::numeric_limits<
            long long
        >::min();


    for (
        const TripleCandidate& candidate :
        candidates
    )
    {
        if (
            candidate.roiSquaredError
            >
            bestRoiError
        )
        {
            pareto.push_back(
                candidate
            );


            bestRoiError =
                candidate.roiSquaredError;
        }
    }


    return pareto;
}


// =========================================================
// 保存候选结果
// =========================================================

void saveCandidates(
    const std::string& path,
    const std::vector<TripleCandidate>& candidates,
    double globalPixelCount,
    double roiPixelCount
)
{
    std::ofstream file(
        path
    );


    if (!file.is_open())
    {
        throw std::runtime_error(
            "Cannot write CSV: "
            + path
        );
    }


    file
        << "index,"
        << "a2_unit,"
        << "a2_lower,"
        << "a2_upper,"
        << "a3_unit,"
        << "a3_lower,"
        << "a3_upper,"
        << "a4_unit,"
        << "a4_lower,"
        << "a4_upper,"
        << "pair_global_psnr,"
        << "pair_roi_psnr,"
        << "global_psnr,"
        << "roi_psnr,"
        << "psnr_gap\n";


    for (std::size_t i = 0;
         i < candidates.size();
         ++i)
    {
        const TripleCandidate& c =
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
            << unitName(c.a2Unit)
            << ","
            << c.a2Lower
            << ","
            << c.a2Upper
            << ","
            << unitName(c.a3Unit)
            << ","
            << c.a3Lower
            << ","
            << c.a3Upper
            << ","
            << unitName(c.a4Unit)
            << ","
            << c.a4Lower
            << ","
            << c.a4Upper
            << ","
            << c.pairGlobalPsnr
            << ","
            << c.pairRoiPsnr
            << ","
            << globalPsnr
            << ","
            << roiPsnr
            << ","
            << globalPsnr - roiPsnr
            << "\n";
    }
}

}


// =========================================================
// main
// =========================================================

int main()
{
    const std::string inputImagePath =
        "data/input/test_1.jpg";


    const std::string pairCsvPath =
        "results/all_pair_search/"
        "all_pairs_pareto.csv";


    const int desiredBinCount =
        25;


    const std::array<Unit, 8>
        a4CandidateUnits =
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


    try
    {
        std::filesystem::create_directories(
            "results/triple_search"
        );


        // =================================================
        // 1. 读取双位置候选
        // =================================================

        const std::vector<PairCandidate>
            pairCandidates =
                readPairCandidates(
                    pairCsvPath
                );


        if (pairCandidates.empty())
        {
            throw std::runtime_error(
                "No A2+A3 pair candidates found."
            );
        }


        std::cout
            << std::fixed
            << std::setprecision(6);


        std::cout
            << "\n========================================\n"
            << "A2 + A3 + A4 triple search\n"
            << "========================================\n";


        std::cout
            << "A2+A3 pair candidates: "
            << pairCandidates.size()
            << "\n";


        // =================================================
        // 2. 输入图片 / exact / ROI
        // =================================================

        const cv::Mat inputImage =
            image_io::loadGrayImage(
                inputImagePath
            );


        const cv::Mat exactImage =
            image_processing::sharpenExact(
                inputImage
            );


        const cv::Mat roiMask =
            region_mask::createStatisticalRoiMask(
                inputImage.cols,
                inputImage.rows
            );


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
        // 3. 把所有像素固定信息提前保存
        // =================================================

        std::vector<PixelInput>
            pixels;


        pixels.reserve(
            static_cast<std::size_t>(
                (inputImage.rows - 2)
                *
                (inputImage.cols - 2)
            )
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


                PixelInput pixel;


                pixel.startValue =
                    5 * center;


                pixel.rhs =
                {
                    -top,
                    -bottom,
                    -left,
                    -right
                };


                pixel.exactValue =
                    exactImage.at<unsigned char>(
                        row,
                        col
                    );


                pixel.insideRoi =
                    region_mask::isImportantPixel(
                        roiMask,
                        row,
                        col
                    );


                pixels.push_back(
                    pixel
                );
            }
        }


        // =================================================
        // 4. 开始三位置搜索
        // =================================================

        std::vector<TripleCandidate>
            allCandidates;


        // 大约：
        // 108 × 8 × 325 ≈ 28万
        allCandidates.reserve(
            pairCandidates.size()
            *
            8
            *
            325
        );


        long long evaluatedCandidateCount =
            0;


        double maximumPairGlobalDifference =
            0.0;

        double maximumPairRoiDifference =
            0.0;


        for (std::size_t pairIndex = 0;
             pairIndex < pairCandidates.size();
             ++pairIndex)
        {
            const PairCandidate& pair =
                pairCandidates[
                    pairIndex
                ];


            // =============================================
            // 4.1 固定A2+A3，整张图只运行一次
            // =============================================

            std::vector<PairPixelState>
                pairStates;


            pairStates.reserve(
                pixels.size()
            );


            std::vector<int>
                a4MonitorValues;


            a4MonitorValues.reserve(
                pixels.size()
            );


            long long pairGlobalSquaredError =
                0;

            long long pairRoiSquaredError =
                0;


            for (const PixelInput& pixel :
                 pixels)
            {
                const PairPixelState state =
                    simulatePairPixel(
                        pixel,
                        pair
                    );


                pairStates.push_back(
                    state
                );


                a4MonitorValues.push_back(
                    state.a4Monitor
                );


                pairGlobalSquaredError +=
                    state.baseSquaredError;


                if (state.insideRoi)
                {
                    pairRoiSquaredError +=
                        state.baseSquaredError;
                }
            }


            // =============================================
            // 4.2 重新算一下双位置PSNR
            //
            // 用来检查和CSV里的结果是否一致
            // =============================================

            const double pairGlobalPsnr =
                psnrFromSse(
                    pairGlobalSquaredError,
                    globalPixelCount
                );


            const double pairRoiPsnr =
                psnrFromSse(
                    pairRoiSquaredError,
                    roiPixelCountDouble
                );


            maximumPairGlobalDifference =
                std::max(
                    maximumPairGlobalDifference,
                    std::abs(
                        pairGlobalPsnr
                        -
                        pair.csvGlobalPsnr
                    )
                );


            maximumPairRoiDifference =
                std::max(
                    maximumPairRoiDifference,
                    std::abs(
                        pairRoiPsnr
                        -
                        pair.csvRoiPsnr
                    )
                );


            // =============================================
            // 4.3 根据当前真实A4_input1建立25 bins
            // =============================================

            const QuantileBins a4Bins =
                buildQuantileBins(
                    a4MonitorValues,
                    desiredBinCount
                );


            const int a4BinCount =
                a4Bins.count();


            // =============================================
            // 4.4 遍历A4候选近似单元
            // =============================================

            for (Unit a4Unit :
                 a4CandidateUnits)
            {
                std::vector<long long>
                    globalDeltaByBin(
                        a4BinCount,
                        0
                    );


                std::vector<long long>
                    roiDeltaByBin(
                        a4BinCount,
                        0
                    );


                // -----------------------------------------
                // 对每个像素计算：
                //
                // A4=候选单元
                // 相对于
                // A4=5RP
                //
                // 的平方误差变化
                // -----------------------------------------

                for (std::size_t i = 0;
                     i < pixels.size();
                     ++i)
                {
                    const PixelInput& pixel =
                        pixels[i];


                    const PairPixelState& state =
                        pairStates[i];


                    const int changedValue =
                        approximate::addSigned12(
                            state.a4Monitor,
                            pixel.rhs[3],
                            a4Unit
                        );


                    const int changedOutput =
                        clampByte(
                            changedValue
                        );


                    const long long changedDifference =
                        static_cast<long long>(
                            changedOutput
                            -
                            pixel.exactValue
                        );


                    const long long
                        changedSquaredError =
                            changedDifference
                            *
                            changedDifference;


                    const long long errorDelta =
                        changedSquaredError
                        -
                        state.baseSquaredError;


                    const int bin =
                        a4Bins.indexOf(
                            state.a4Monitor
                        );


                    globalDeltaByBin[
                        bin
                    ] +=
                        errorDelta;


                    if (state.insideRoi)
                    {
                        roiDeltaByBin[
                            bin
                        ] +=
                            errorDelta;
                    }
                }


                // -----------------------------------------
                // 建立累计和
                // -----------------------------------------

                for (int i = 1;
                     i < a4BinCount;
                     ++i)
                {
                    globalDeltaByBin[i] +=
                        globalDeltaByBin[
                            i - 1
                        ];


                    roiDeltaByBin[i] +=
                        roiDeltaByBin[
                            i - 1
                        ];
                }


                // =========================================
                // 4.5 枚举A4全部连续区间
                // =========================================

                for (int lowerBin = 0;
                     lowerBin < a4BinCount;
                     ++lowerBin)
                {
                    for (int upperBin = lowerBin;
                         upperBin < a4BinCount;
                         ++upperBin)
                    {
                        const long long globalDelta =
                            rangeSum(
                                globalDeltaByBin,
                                lowerBin,
                                upperBin
                            );


                        const long long roiDelta =
                            rangeSum(
                                roiDeltaByBin,
                                lowerBin,
                                upperBin
                            );


                        const long long
                            newGlobalSquaredError =
                                pairGlobalSquaredError
                                +
                                globalDelta;


                        const long long
                            newRoiSquaredError =
                                pairRoiSquaredError
                                +
                                roiDelta;


                        if (
                            newGlobalSquaredError <= 0
                            ||
                            newRoiSquaredError <= 0
                        )
                        {
                            continue;
                        }


                        allCandidates.push_back(
                            {
                                pair.a2Unit,
                                pair.a2Lower,
                                pair.a2Upper,

                                pair.a3Unit,
                                pair.a3Lower,
                                pair.a3Upper,

                                a4Unit,

                                a4Bins.lowerValue(
                                    lowerBin
                                ),

                                a4Bins.upperValue(
                                    upperBin
                                ),

                                pairGlobalPsnr,
                                pairRoiPsnr,

                                newGlobalSquaredError,
                                newRoiSquaredError
                            }
                        );


                        ++evaluatedCandidateCount;
                    }
                }
            }


            if (
                (pairIndex + 1) % 10 == 0
                ||
                pairIndex + 1 ==
                    pairCandidates.size()
            )
            {
                std::cout
                    << "Processed pair candidates: "
                    << (pairIndex + 1)
                    << " / "
                    << pairCandidates.size()
                    << "\n";
            }
        }


        // =================================================
        // 5. 保存所有三位置候选
        // =================================================

        const std::string allResultPath =
            "results/triple_search/"
            "A2_A3_A4_all_candidates.csv";


        saveCandidates(
            allResultPath,
            allCandidates,
            globalPixelCount,
            roiPixelCountDouble
        );


        // =================================================
        // 6. Pareto筛选
        // =================================================

        std::vector<TripleCandidate>
            pareto =
                extractPareto(
                    std::move(
                        allCandidates
                    )
                );


        const std::string paretoPath =
            "results/triple_search/"
            "A2_A3_A4_pareto.csv";


        saveCandidates(
            paretoPath,
            pareto,
            globalPixelCount,
            roiPixelCountDouble
        );


        // =================================================
        // 7. 输出总体信息
        // =================================================

        std::cout
            << "\n========================================\n"
            << "Triple search finished\n"
            << "========================================\n";


        std::cout
            << "Pair candidates        : "
            << pairCandidates.size()
            << "\n";


        std::cout
            << "Evaluated A4 candidates: "
            << evaluatedCandidateCount
            << "\n";


        std::cout
            << "Triple Pareto size     : "
            << pareto.size()
            << "\n";


        std::cout
            << "\nPair CSV validation\n";


        std::cout
            << "Max Global difference: "
            << maximumPairGlobalDifference
            << " dB\n";


        std::cout
            << "Max ROI difference   : "
            << maximumPairRoiDifference
            << " dB\n";


        std::cout
            << "\nSaved:\n"
            << allResultPath
            << "\n"
            << paretoPath
            << "\n";


        // =================================================
        // 8. 打印20个代表性Pareto方案
        // =================================================

        std::cout
            << "\n========================================\n"
            << "Representative triple Pareto solutions\n"
            << "========================================\n";


        const std::size_t displayCount =
            std::min<std::size_t>(
                20,
                pareto.size()
            );


        for (std::size_t i = 0;
             i < displayCount;
             ++i)
        {
            std::size_t index =
                0;


            if (displayCount > 1)
            {
                index =
                    i
                    *
                    (pareto.size() - 1)
                    /
                    (displayCount - 1);
            }


            const TripleCandidate& c =
                pareto[index];


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
                << "A2: "
                << unitName(c.a2Unit)
                << " ["
                << c.a2Lower
                << ", "
                << c.a2Upper
                << "]\n";


            std::cout
                << "A3: "
                << unitName(c.a3Unit)
                << " ["
                << c.a3Lower
                << ", "
                << c.a3Upper
                << "]\n";


            std::cout
                << "A4: "
                << unitName(c.a4Unit)
                << " ["
                << c.a4Lower
                << ", "
                << c.a4Upper
                << "]\n";


            std::cout
                << "Pair Global PSNR: "
                << c.pairGlobalPsnr
                << " dB\n";


            std::cout
                << "Pair ROI PSNR   : "
                << c.pairRoiPsnr
                << " dB\n";


            std::cout
                << "Triple Global PSNR: "
                << globalPsnr
                << " dB\n";


            std::cout
                << "Triple ROI PSNR   : "
                << roiPsnr
                << " dB\n";


            std::cout
                << "Triple PSNR gap   : "
                << globalPsnr
                   -
                   roiPsnr
                << " dB\n";
        }


        // =================================================
        // 9. 真实运行一个三位置Pareto方案进行验证
        // =================================================

        if (!pareto.empty())
        {
            const TripleCandidate&
                testCandidate =
                    pareto[
                        pareto.size() / 2
                    ];


            const cv::Mat directImage =
                runDirectTripleAttack(
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


            std::cout
                << "\n========================================\n"
                << "Triple fast calculation validation\n"
                << "========================================\n";


            std::cout
                << "Fast Global PSNR  : "
                << fastGlobalPsnr
                << "\n";


            std::cout
                << "Direct Global PSNR: "
                << directGlobalPsnr
                << "\n";


            std::cout
                << "Difference         : "
                << std::abs(
                    fastGlobalPsnr
                    -
                    directGlobalPsnr
                )
                << "\n\n";


            std::cout
                << "Fast ROI PSNR     : "
                << fastRoiPsnr
                << "\n";


            std::cout
                << "Direct ROI PSNR   : "
                << directRoiPsnr
                << "\n";


            std::cout
                << "Difference        : "
                << std::abs(
                    fastRoiPsnr
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