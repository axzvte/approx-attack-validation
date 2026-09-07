#include "analysis/interval_attack_search.hpp"

#include "region/region_mask.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>


namespace interval_attack_search
{

namespace
{

const std::array<approximate::ApproxUnitId, 6>
APPROX_UNITS =
{
    approximate::ApproxUnitId::Add12se5QT,
    approximate::ApproxUnitId::Add12se5QC,
    approximate::ApproxUnitId::Add12se5TE,
    approximate::ApproxUnitId::Add12se5PN,
    approximate::ApproxUnitId::Add12se5SB,
    approximate::ApproxUnitId::Add12se5Z0
};


struct PixelData
{
    int c;
    int t;
    int b;
    int l;
    int r;

    int fiveC;

    int a1;
    int a2;
    int a3;
    int a4;

    bool roi;
};


int clampByte(
    int value
)
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


double mseToPsnr(
    double mse
)
{
    if (mse <= 0.0)
    {
        return
            std::numeric_limits<double>::infinity();
    }


    return
        10.0
        *
        std::log10(
            (255.0 * 255.0)
            /
            mse
        );
}


std::vector<PixelData>
collectPixelData(
    const cv::Mat& inputImage,
    const cv::Mat& roiMask
)
{
    std::vector<PixelData> data;


    data.reserve(
        static_cast<std::size_t>(
            inputImage.rows - 2
        )
        *
        static_cast<std::size_t>(
            inputImage.cols - 2
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
            PixelData pixel{};


            pixel.c =
                inputImage.at<unsigned char>(
                    row,
                    col
                );

            pixel.t =
                inputImage.at<unsigned char>(
                    row - 1,
                    col
                );

            pixel.b =
                inputImage.at<unsigned char>(
                    row + 1,
                    col
                );

            pixel.l =
                inputImage.at<unsigned char>(
                    row,
                    col - 1
                );

            pixel.r =
                inputImage.at<unsigned char>(
                    row,
                    col + 1
                );


            pixel.fiveC =
                5 * pixel.c;


            pixel.a1 =
                pixel.fiveC
                - pixel.t;

            pixel.a2 =
                pixel.a1
                - pixel.b;

            pixel.a3 =
                pixel.a2
                - pixel.l;

            pixel.a4 =
                pixel.a3
                - pixel.r;


            pixel.roi =
                region_mask::isImportantPixel(
                    roiMask,
                    row,
                    col
                );


            data.push_back(
                pixel
            );
        }
    }


    return data;
}


int monitorValue(
    const PixelData& pixel,
    MonitorSignal signal
)
{
    switch (signal)
    {
        case MonitorSignal::C:
            return pixel.c;

        case MonitorSignal::T:
            return pixel.t;

        case MonitorSignal::B:
            return pixel.b;

        case MonitorSignal::L:
            return pixel.l;

        case MonitorSignal::R:
            return pixel.r;

        case MonitorSignal::FiveC:
            return pixel.fiveC;

        case MonitorSignal::A1:
            return pixel.a1;

        case MonitorSignal::A2:
            return pixel.a2;

        case MonitorSignal::A3:
            return pixel.a3;
    }


    throw std::runtime_error(
        "Unknown monitor signal."
    );
}


std::vector<MonitorSignal>
availableSignals(
    AttackPosition position
)
{
    std::vector<MonitorSignal> result =
    {
        MonitorSignal::C,
        MonitorSignal::T,
        MonitorSignal::B,
        MonitorSignal::L,
        MonitorSignal::R,
        MonitorSignal::FiveC
    };


    if (
        position == AttackPosition::A2
        ||
        position == AttackPosition::A3
        ||
        position == AttackPosition::A4
    )
    {
        result.push_back(
            MonitorSignal::A1
        );
    }


    if (
        position == AttackPosition::A3
        ||
        position == AttackPosition::A4
    )
    {
        result.push_back(
            MonitorSignal::A2
        );
    }


    if (
        position == AttackPosition::A4
    )
    {
        result.push_back(
            MonitorSignal::A3
        );
    }


    return result;
}


// ============================================================
// 如果当前位置触发攻击，计算最终输出像素
// ============================================================

int attackedOutput(
    const PixelData& pixel,
    AttackPosition attackPosition,
    approximate::ApproxUnitId unit
)
{
    int a1 = pixel.a1;
    int a2 = pixel.a2;
    int a3 = pixel.a3;
    int a4 = pixel.a4;


    if (
        attackPosition
        ==
        AttackPosition::A1
    )
    {
        a1 =
            approximate::addSigned12(
                pixel.fiveC,
                -pixel.t,
                unit
            );

        a2 =
            a1
            - pixel.b;

        a3 =
            a2
            - pixel.l;

        a4 =
            a3
            - pixel.r;
    }


    else if (
        attackPosition
        ==
        AttackPosition::A2
    )
    {
        a2 =
            approximate::addSigned12(
                pixel.a1,
                -pixel.b,
                unit
            );

        a3 =
            a2
            - pixel.l;

        a4 =
            a3
            - pixel.r;
    }


    else if (
        attackPosition
        ==
        AttackPosition::A3
    )
    {
        a3 =
            approximate::addSigned12(
                pixel.a2,
                -pixel.l,
                unit
            );

        a4 =
            a3
            - pixel.r;
    }


    else if (
        attackPosition
        ==
        AttackPosition::A4
    )
    {
        a4 =
            approximate::addSigned12(
                pixel.a3,
                -pixel.r,
                unit
            );
    }


    return
        clampByte(
            a4
        );
}


// ============================================================
// 用于保留全局最好的 topK 个结果
// ============================================================

void insertCandidate(
    std::vector<Candidate>& bestCandidates,
    const Candidate& candidate,
    std::size_t topK
)
{
    bestCandidates.push_back(
        candidate
    );


    std::sort(
        bestCandidates.begin(),
        bestCandidates.end(),

        [](
            const Candidate& first,
            const Candidate& second
        )
        {
            if (
                first.errorGap
                !=
                second.errorGap
            )
            {
                return
                    first.errorGap
                    >
                    second.errorGap;
            }


            return
                first.globalPsnr
                >
                second.globalPsnr;
        }
    );


    if (
        bestCandidates.size()
        >
        topK
    )
    {
        bestCandidates.resize(
            topK
        );
    }
}

}


// ============================================================
// 全搜索
// ============================================================

std::vector<Candidate>
searchBestCandidates(
    const cv::Mat& inputImage,
    const cv::Mat& roiMask,
    double minGlobalPsnr,
    std::size_t topK
)
{
    if (
        inputImage.empty()
        ||
        roiMask.empty()
    )
    {
        throw std::runtime_error(
            "Input image or ROI mask is empty."
        );
    }


    if (
        inputImage.type() != CV_8UC1
        ||
        roiMask.type() != CV_8UC1
    )
    {
        throw std::runtime_error(
            "Images must be 8-bit grayscale."
        );
    }


    if (
        inputImage.size()
        !=
        roiMask.size()
    )
    {
        throw std::runtime_error(
            "Image and mask sizes do not match."
        );
    }


    const auto pixels =
        collectPixelData(
            inputImage,
            roiMask
        );


    long long totalRoiPixels = 0;

    long long totalNonRoiPixels = 0;

    long long interiorRoiPixels = 0;

    long long interiorNonRoiPixels = 0;


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
                ++totalRoiPixels;
            }
            else
            {
                ++totalNonRoiPixels;
            }
        }
    }


    for (const auto& pixel : pixels)
    {
        if (pixel.roi)
        {
            ++interiorRoiPixels;
        }
        else
        {
            ++interiorNonRoiPixels;
        }
    }


    const long long totalPixels =
        totalRoiPixels
        +
        totalNonRoiPixels;


    if (
        totalRoiPixels == 0
        ||
        totalNonRoiPixels == 0
    )
    {
        throw std::runtime_error(
            "ROI and non-ROI regions are required."
        );
    }


    const std::array<AttackPosition, 4>
    attackPositions =
    {
        AttackPosition::A1,
        AttackPosition::A2,
        AttackPosition::A3,
        AttackPosition::A4
    };


    std::vector<Candidate>
        bestCandidates;


    // ========================================================
    // 攻击位置
    // ========================================================

    for (
        AttackPosition attackPosition :
        attackPositions
    )
    {
        const auto signals =
            availableSignals(
                attackPosition
            );


        // ====================================================
        // EvoApprox
        // ====================================================

        for (
            approximate::ApproxUnitId unit :
            APPROX_UNITS
        )
        {
            // =================================================
            // 对当前攻击位置和近似加法器，
            // 先计算每个像素：
            //
            // 如果触发，会产生多少最终平方误差
            // =================================================

            std::vector<double>
                squaredErrors(
                    pixels.size(),
                    0.0
                );


            for (
                std::size_t i = 0;
                i < pixels.size();
                ++i
            )
            {
                const int exactOutput =
                    clampByte(
                        pixels[i].a4
                    );


                const int approxOutput =
                    attackedOutput(
                        pixels[i],
                        attackPosition,
                        unit
                    );


                const double difference =
                    static_cast<double>(
                        approxOutput
                        -
                        exactOutput
                    );


                squaredErrors[i] =
                    difference
                    *
                    difference;
            }


            // =================================================
            // 监测信号
            // =================================================

            for (
                MonitorSignal signal :
                signals
            )
            {
                int minValue =
                    std::numeric_limits<int>::max();

                int maxValue =
                    std::numeric_limits<int>::min();


                for (const auto& pixel : pixels)
                {
                    const int value =
                        monitorValue(
                            pixel,
                            signal
                        );


                    minValue =
                        std::min(
                            minValue,
                            value
                        );


                    maxValue =
                        std::max(
                            maxValue,
                            value
                        );
                }


                const int valueCount =
                    maxValue
                    -
                    minValue
                    +
                    1;


                std::vector<double>
                    roiSse(
                        valueCount,
                        0.0
                    );


                std::vector<double>
                    nonRoiSse(
                        valueCount,
                        0.0
                    );


                std::vector<long long>
                    roiCount(
                        valueCount,
                        0
                    );


                std::vector<long long>
                    nonRoiCount(
                        valueCount,
                        0
                    );


                // =============================================
                // 按监测信号的值聚合
                // =============================================

                for (
                    std::size_t i = 0;
                    i < pixels.size();
                    ++i
                )
                {
                    const int value =
                        monitorValue(
                            pixels[i],
                            signal
                        );


                    const int index =
                        value
                        -
                        minValue;


                    if (pixels[i].roi)
                    {
                        roiSse[index]
                            +=
                            squaredErrors[i];

                        ++roiCount[index];
                    }
                    else
                    {
                        nonRoiSse[index]
                            +=
                            squaredErrors[i];

                        ++nonRoiCount[index];
                    }
                }


                // =============================================
                // 前缀和
                // =============================================

                std::vector<double>
                    roiSsePrefix(
                        valueCount + 1,
                        0.0
                    );


                std::vector<double>
                    nonRoiSsePrefix(
                        valueCount + 1,
                        0.0
                    );


                std::vector<long long>
                    roiCountPrefix(
                        valueCount + 1,
                        0
                    );


                std::vector<long long>
                    nonRoiCountPrefix(
                        valueCount + 1,
                        0
                    );


                for (
                    int i = 0;
                    i < valueCount;
                    ++i
                )
                {
                    roiSsePrefix[i + 1] =
                        roiSsePrefix[i]
                        +
                        roiSse[i];


                    nonRoiSsePrefix[i + 1] =
                        nonRoiSsePrefix[i]
                        +
                        nonRoiSse[i];


                    roiCountPrefix[i + 1] =
                        roiCountPrefix[i]
                        +
                        roiCount[i];


                    nonRoiCountPrefix[i + 1] =
                        nonRoiCountPrefix[i]
                        +
                        nonRoiCount[i];
                }


                // =============================================
                // 枚举所有 [lower, upper]
                // =============================================

                for (
                    int lowerIndex = 0;
                    lowerIndex < valueCount;
                    ++lowerIndex
                )
                {
                    for (
                        int upperIndex =
                            lowerIndex;

                        upperIndex
                            <
                            valueCount;

                        ++upperIndex
                    )
                    {
                        const double currentRoiSse =
                            roiSsePrefix[
                                upperIndex + 1
                            ]
                            -
                            roiSsePrefix[
                                lowerIndex
                            ];


                        const double currentNonRoiSse =
                            nonRoiSsePrefix[
                                upperIndex + 1
                            ]
                            -
                            nonRoiSsePrefix[
                                lowerIndex
                            ];


                        const double globalMse =
                            (
                                currentRoiSse
                                +
                                currentNonRoiSse
                            )
                            /
                            static_cast<double>(
                                totalPixels
                            );


                        const double globalPsnr =
                            mseToPsnr(
                                globalMse
                            );


                        if (
                            globalPsnr
                            <
                            minGlobalPsnr
                        )
                        {
                            continue;
                        }


                        const double roiMse =
                            currentRoiSse
                            /
                            static_cast<double>(
                                totalRoiPixels
                            );


                        const double nonRoiMse =
                            currentNonRoiSse
                            /
                            static_cast<double>(
                                totalNonRoiPixels
                            );


                        const double errorGap =
                            roiMse
                            -
                            nonRoiMse;


                        if (errorGap <= 0.0)
                        {
                            continue;
                        }


                        const long long
                        triggeredRoi =
                            roiCountPrefix[
                                upperIndex + 1
                            ]
                            -
                            roiCountPrefix[
                                lowerIndex
                            ];


                        const long long
                        triggeredNonRoi =
                            nonRoiCountPrefix[
                                upperIndex + 1
                            ]
                            -
                            nonRoiCountPrefix[
                                lowerIndex
                            ];


                        Candidate candidate{};


                        candidate.attackPosition =
                            attackPosition;


                        candidate.monitorSignal =
                            signal;


                        candidate.unit =
                            unit;


                        candidate.lower =
                            lowerIndex
                            +
                            minValue;


                        candidate.upper =
                            upperIndex
                            +
                            minValue;


                        candidate.globalPsnr =
                            globalPsnr;


                        candidate.roiMse =
                            roiMse;


                        candidate.nonRoiMse =
                            nonRoiMse;


                        candidate.roiPsnr =
                            mseToPsnr(
                                roiMse
                            );


                        candidate.nonRoiPsnr =
                            mseToPsnr(
                                nonRoiMse
                            );


                        candidate.errorGap =
                            errorGap;


                        candidate.errorRatio =
                            roiMse
                            /
                            (
                                nonRoiMse
                                +
                                1e-9
                            );


                        candidate.roiTriggerRate =
                            static_cast<double>(
                                triggeredRoi
                            )
                            /
                            static_cast<double>(
                                interiorRoiPixels
                            );


                        candidate.nonRoiTriggerRate =
                            static_cast<double>(
                                triggeredNonRoi
                            )
                            /
                            static_cast<double>(
                                interiorNonRoiPixels
                            );


                        insertCandidate(
                            bestCandidates,
                            candidate,
                            topK
                        );
                    }
                }
            }
        }
    }


    return bestCandidates;
}



// ============================================================
// 根据最终候选真实生成攻击图
// ============================================================

cv::Mat renderAttack(
    const cv::Mat& inputImage,
    const Candidate& candidate
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
            PixelData pixel{};


            pixel.c =
                inputImage.at<unsigned char>(
                    row,
                    col
                );


            pixel.t =
                inputImage.at<unsigned char>(
                    row - 1,
                    col
                );


            pixel.b =
                inputImage.at<unsigned char>(
                    row + 1,
                    col
                );


            pixel.l =
                inputImage.at<unsigned char>(
                    row,
                    col - 1
                );


            pixel.r =
                inputImage.at<unsigned char>(
                    row,
                    col + 1
                );


            pixel.fiveC =
                5 * pixel.c;


            pixel.a1 =
                pixel.fiveC
                - pixel.t;


            pixel.a2 =
                pixel.a1
                - pixel.b;


            pixel.a3 =
                pixel.a2
                - pixel.l;


            pixel.a4 =
                pixel.a3
                - pixel.r;


            const int signalValue =
                monitorValue(
                    pixel,
                    candidate.monitorSignal
                );


            const bool trigger =
                   signalValue
                       >=
                       candidate.lower
                &&
                   signalValue
                       <=
                       candidate.upper;


            int outputValue =
                clampByte(
                    pixel.a4
                );


            if (trigger)
            {
                outputValue =
                    attackedOutput(
                        pixel,
                        candidate.attackPosition,
                        candidate.unit
                    );
            }


            outputImage.at<unsigned char>(
                row,
                col
            ) =
                static_cast<unsigned char>(
                    outputValue
                );
        }
    }


    return outputImage;
}



std::string attackPositionName(
    AttackPosition position
)
{
    switch (position)
    {
        case AttackPosition::A1:
            return "A1";

        case AttackPosition::A2:
            return "A2";

        case AttackPosition::A3:
            return "A3";

        case AttackPosition::A4:
            return "A4";
    }


    return "UNKNOWN";
}



std::string monitorSignalName(
    MonitorSignal signal
)
{
    switch (signal)
    {
        case MonitorSignal::C:
            return "C";

        case MonitorSignal::T:
            return "T";

        case MonitorSignal::B:
            return "B";

        case MonitorSignal::L:
            return "L";

        case MonitorSignal::R:
            return "R";

        case MonitorSignal::FiveC:
            return "5C";

        case MonitorSignal::A1:
            return "A1";

        case MonitorSignal::A2:
            return "A2";

        case MonitorSignal::A3:
            return "A3";
    }


    return "UNKNOWN";
}



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
    }


    return "UNKNOWN";
}


}