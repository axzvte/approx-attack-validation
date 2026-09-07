#include "analysis/bit_trigger_validation.hpp"

#include "region/region_mask.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>


namespace bit_trigger_validation
{

namespace
{

constexpr int SIGNAL_BITS = 12;
constexpr int VALUE_COUNT = 4096;


const std::array<approximate::ApproxUnitId, 6> APPROX_UNITS =
{
    approximate::ApproxUnitId::Add12se5QT,
    approximate::ApproxUnitId::Add12se5QC,
    approximate::ApproxUnitId::Add12se5TE,
    approximate::ApproxUnitId::Add12se5PN,
    approximate::ApproxUnitId::Add12se5SB,
    approximate::ApproxUnitId::Add12se5Z0
};


// ============================================================
// 将一个 signed 12-bit 数转换成它真正的 12-bit 二进制形式
//
// 例如：
//  5  -> 000000000101
// -1  -> 111111111111
//
// 这里只保留低 12 bit。
// ============================================================

std::uint16_t encodeSigned12Bits(
    int value
)
{
    return
        static_cast<std::uint16_t>(
            value
        )
        & 0x0FFFU;
}


// ============================================================
// 判断 A1 的三个 bit 是否匹配当前 pattern
//
// pattern:
//
// 000
// 001
// ...
// 111
//
// pattern最高位对应 bitA
// 中间位对应 bitB
// 最低位对应 bitC
// ============================================================

bool matchesPattern(
    std::uint16_t rawValue,
    int bitA,
    int bitB,
    int bitC,
    unsigned int pattern
)
{
    const unsigned int actualA =
        (rawValue >> bitA) & 1U;

    const unsigned int actualB =
        (rawValue >> bitB) & 1U;

    const unsigned int actualC =
        (rawValue >> bitC) & 1U;


    const unsigned int expectedA =
        (pattern >> 2) & 1U;

    const unsigned int expectedB =
        (pattern >> 1) & 1U;

    const unsigned int expectedC =
        pattern & 1U;


    return
           actualA == expectedA
        && actualB == expectedB
        && actualC == expectedC;
}


// ============================================================
// 图像输出限制在 0~255
// ============================================================

int clampToByte(
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


// ============================================================
// 根据累计平方误差计算 PSNR
// ============================================================

double calculatePsnrFromSse(
    double sumSquaredError,
    long long pixelCount
)
{
    if (pixelCount <= 0)
    {
        throw std::runtime_error(
            "Pixel count must be positive."
        );
    }


    if (sumSquaredError == 0.0)
    {
        return
            std::numeric_limits<double>::infinity();
    }


    const double mse =
        sumSquaredError
        /
        static_cast<double>(
            pixelCount
        );


    return
        10.0
        *
        std::log10(
            (255.0 * 255.0)
            / mse
        );
}


// ============================================================
// 输入检查
// ============================================================

void validateInput(
    const cv::Mat& inputImage,
    const cv::Mat& roiMask
)
{
    if (inputImage.empty())
    {
        throw std::runtime_error(
            "Input image is empty."
        );
    }


    if (roiMask.empty())
    {
        throw std::runtime_error(
            "ROI mask is empty."
        );
    }


    if (inputImage.type() != CV_8UC1)
    {
        throw std::runtime_error(
            "Bit-trigger validation requires "
            "an 8-bit grayscale image."
        );
    }


    if (roiMask.type() != CV_8UC1)
    {
        throw std::runtime_error(
            "ROI mask must be an 8-bit grayscale image."
        );
    }


    if (inputImage.size() != roiMask.size())
    {
        throw std::runtime_error(
            "Image and ROI mask sizes do not match."
        );
    }
}

}


// ============================================================
// 搜索 A2 的三 bit 触发模式
//
// 攻击结构：
//
// A1 = 5C - Top
//
// A2:
//      A1 + (-Bottom)
//
// 如果 A1 的三个 bit 匹配：
//      使用 EvoApprox
//
// 否则：
//      使用 Exact
//
// A3、A4继续精确计算。
// ============================================================

std::vector<BitTriggerCandidate>
searchA2BitTriggerCandidates(
    const cv::Mat& inputImage,
    const cv::Mat& roiMask,
    double minGlobalPsnr,
    std::size_t topK
)
{
    validateInput(
        inputImage,
        roiMask
    );


    // ========================================================
    // 每一个 A1 二进制值对应的数据统计
    //
    // A1 是 12 bit：
    //
    // 000000000000
    // ...
    // 111111111111
    //
    // 一共 4096 种。
    // ========================================================

    std::array<long long, VALUE_COUNT>
        roiCountByA1{};

    std::array<long long, VALUE_COUNT>
        nonRoiCountByA1{};


    // --------------------------------------------------------
    // 对 6 个 EvoApprox 分别保存：
    //
    // 每种 A1 值产生的最终输出平方误差
    // --------------------------------------------------------

    std::array<
        std::array<double, VALUE_COUNT>,
        6
    >
    globalSseByA1{};


    std::array<
        std::array<double, VALUE_COUNT>,
        6
    >
    roiSseByA1{};


    long long totalRoiPixels = 0;

    long long interiorRoiPixels = 0;

    long long interiorNonRoiPixels = 0;


    // ========================================================
    // 先统计整个 mask 中有多少 ROI 像素
    //
    // PSNR 分母使用整个 ROI，
    // 与原来的 ROI PSNR 定义保持一致。
    // ========================================================

    for (int row = 0;
         row < roiMask.rows;
         ++row)
    {
        for (int col = 0;
             col < roiMask.cols;
             ++col)
        {
            if (region_mask::isImportantPixel(
                    roiMask,
                    row,
                    col
                ))
            {
                ++totalRoiPixels;
            }
        }
    }


    if (totalRoiPixels == 0)
    {
        throw std::runtime_error(
            "ROI mask contains no important pixels."
        );
    }


    // ========================================================
    // 第一步：
    //
    // 对整张图片只遍历一次。
    //
    // 对每一个像素计算：
    //
    // A1
    // Exact A2
    // 6种 Approx A2
    // 最终输出误差
    //
    // 然后按照 A1 的 12-bit 数值聚合起来。
    // ========================================================

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


            // ------------------------------------------------
            // A1保持精确
            //
            // A1 = 5C - Top
            // ------------------------------------------------

            const int a1 =
                5 * center
                - top;


            // A1的12-bit表示
            const std::uint16_t rawA1 =
                encodeSigned12Bits(
                    a1
                );


            const int a1Code =
                static_cast<int>(
                    rawA1
                );


            const bool insideRoi =
                region_mask::isImportantPixel(
                    roiMask,
                    row,
                    col
                );


            if (insideRoi)
            {
                ++roiCountByA1[a1Code];

                ++interiorRoiPixels;
            }
            else
            {
                ++nonRoiCountByA1[a1Code];

                ++interiorNonRoiPixels;
            }


            // ------------------------------------------------
            // 精确 A2
            // ------------------------------------------------

            const int exactA2 =
                a1
                - bottom;


            // ------------------------------------------------
            // 精确最终输出
            //
            // A3 = A2 - Left
            // A4 = A3 - Right
            // ------------------------------------------------

            const int exactOutput =
                clampToByte(
                    exactA2
                    - left
                    - right
                );


            // =================================================
            // 对 6 个 EvoApprox 计算：
            //
            // 如果这个像素被触发，
            // 最终会产生多大的实际输出误差。
            // =================================================

            for (std::size_t unitIndex = 0;
                 unitIndex < APPROX_UNITS.size();
                 ++unitIndex)
            {
                const int approximateA2 =
                    approximate::addSigned12(
                        a1,
                        -bottom,
                        APPROX_UNITS[
                            unitIndex
                        ]
                    );


                const int approximateOutput =
                    clampToByte(
                        approximateA2
                        - left
                        - right
                    );


                const double difference =
                    static_cast<double>(
                        approximateOutput
                        - exactOutput
                    );


                const double squaredError =
                    difference
                    * difference;


                globalSseByA1[
                    unitIndex
                ][
                    a1Code
                ]
                += squaredError;


                if (insideRoi)
                {
                    roiSseByA1[
                        unitIndex
                    ][
                        a1Code
                    ]
                    += squaredError;
                }
            }
        }
    }


    // ========================================================
    // PSNR 的像素数量
    // ========================================================

    const long long totalPixels =
        static_cast<long long>(
            inputImage.rows
        )
        *
        static_cast<long long>(
            inputImage.cols
        );


    const long long totalNonRoiPixels =
        totalPixels
        - totalRoiPixels;


    if (totalNonRoiPixels <= 0)
    {
        throw std::runtime_error(
            "Image contains no non-ROI pixels."
        );
    }


    std::vector<BitTriggerCandidate>
        candidates;


    // ========================================================
    // 第二步：
    //
    // 搜索三个 bit
    //
    // 从12位中选择3位：
    //
    // C(12,3) = 220
    // ========================================================

    for (int bitA = SIGNAL_BITS - 1;
         bitA >= 2;
         --bitA)
    {
        for (int bitB = bitA - 1;
             bitB >= 1;
             --bitB)
        {
            for (int bitC = bitB - 1;
                 bitC >= 0;
                 --bitC)
            {
                // ============================================
                // 三个位共有 8 种模式：
                //
                // 000
                // 001
                // ...
                // 111
                // ============================================

                for (unsigned int pattern = 0;
                     pattern < 8;
                     ++pattern)
                {
                    long long roiTriggered = 0;

                    long long nonRoiTriggered = 0;


                    std::array<double, 6>
                        globalSse{};

                    std::array<double, 6>
                        roiSse{};


                    // ----------------------------------------
                    // 找到满足当前 bit pattern 的所有 A1 值
                    // ----------------------------------------

                    for (int a1Code = 0;
                         a1Code < VALUE_COUNT;
                         ++a1Code)
                    {
                        if (!matchesPattern(
                                static_cast<std::uint16_t>(
                                    a1Code
                                ),
                                bitA,
                                bitB,
                                bitC,
                                pattern
                            ))
                        {
                            continue;
                        }


                        roiTriggered +=
                            roiCountByA1[
                                a1Code
                            ];


                        nonRoiTriggered +=
                            nonRoiCountByA1[
                                a1Code
                            ];


                        for (std::size_t unitIndex = 0;
                             unitIndex < APPROX_UNITS.size();
                             ++unitIndex)
                        {
                            globalSse[
                                unitIndex
                            ]
                            +=
                                globalSseByA1[
                                    unitIndex
                                ][
                                    a1Code
                                ];


                            roiSse[
                                unitIndex
                            ]
                            +=
                                roiSseByA1[
                                    unitIndex
                                ][
                                    a1Code
                                ];
                        }
                    }


                    // 一个 ROI 像素都没触发，
                    // 这个模式没有意义。
                    if (roiTriggered == 0)
                    {
                        continue;
                    }


                    const long long totalTriggered =
                        roiTriggered
                        + nonRoiTriggered;


                    const double roiCoverage =
                        static_cast<double>(
                            roiTriggered
                        )
                        /
                        static_cast<double>(
                            interiorRoiPixels
                        );


                    const double precision =
                        static_cast<double>(
                            roiTriggered
                        )
                        /
                        static_cast<double>(
                            totalTriggered
                        );


                    // ========================================
                    // 同一个 bit pattern
                    //
                    // 分别测试 6 个 EvoApprox
                    // ========================================

                    for (std::size_t unitIndex = 0;
                         unitIndex < APPROX_UNITS.size();
                         ++unitIndex)
                    {
                        const double globalPsnr =
                            calculatePsnrFromSse(
                                globalSse[
                                    unitIndex
                                ],
                                totalPixels
                            );


                        // ------------------------------------
                        // Global PSNR 是当前搜索约束
                        // ------------------------------------

                        if (globalPsnr
                            < minGlobalPsnr)
                        {
                            continue;
                        }


                        const double roiPsnr =
                            calculatePsnrFromSse(
                                roiSse[
                                    unitIndex
                                ],
                                totalRoiPixels
                            );


                        double nonRoiSse =
                            globalSse[
                                unitIndex
                            ]
                            -
                            roiSse[
                                unitIndex
                            ];


                        if (nonRoiSse < 0.0)
                        {
                            nonRoiSse = 0.0;
                        }


                        const double nonRoiPsnr =
                            calculatePsnrFromSse(
                                nonRoiSse,
                                totalNonRoiPixels
                            );


                        BitTriggerCandidate candidate{};


                        candidate.bitA =
                            bitA;

                        candidate.bitB =
                            bitB;

                        candidate.bitC =
                            bitC;


                        candidate.pattern =
                            pattern;


                        candidate.unit =
                            APPROX_UNITS[
                                unitIndex
                            ];


                        candidate.globalPsnr =
                            globalPsnr;

                        candidate.roiPsnr =
                            roiPsnr;

                        candidate.nonRoiPsnr =
                            nonRoiPsnr;


                        candidate.roiTriggered =
                            roiTriggered;

                        candidate.nonRoiTriggered =
                            nonRoiTriggered;


                        candidate.roiCoverage =
                            roiCoverage;

                        candidate.precision =
                            precision;


                        candidates.push_back(
                            candidate
                        );
                    }
                }
            }
        }
    }


    // ========================================================
    // 第三步：
    //
    // Global PSNR已经满足约束。
    //
    // 现在按照：
    //
    // ROI PSNR 越低越优先
    //
    // 如果 ROI PSNR 相同：
    // Non-ROI PSNR 越高越优先
    // ========================================================

    std::sort(
        candidates.begin(),
        candidates.end(),

        [](
            const BitTriggerCandidate& a,
            const BitTriggerCandidate& b
        )
        {
            if (a.roiPsnr
                != b.roiPsnr)
            {
                return
                    a.roiPsnr
                    <
                    b.roiPsnr;
            }


            return
                a.nonRoiPsnr
                >
                b.nonRoiPsnr;
        }
    );


    if (candidates.size() > topK)
    {
        candidates.resize(
            topK
        );
    }


    return candidates;
}



// ============================================================
// 根据某个候选真正生成攻击图片
//
// 注意：
// 这个函数完全不需要 ROI mask。
//
// 它模拟的是运行时电路。
// ============================================================

cv::Mat renderA2BitTriggerAttack(
    const cv::Mat& inputImage,
    const BitTriggerCandidate& candidate
)
{
    if (inputImage.empty())
    {
        throw std::runtime_error(
            "Input image is empty."
        );
    }


    if (inputImage.type() != CV_8UC1)
    {
        throw std::runtime_error(
            "Bit-trigger attack requires "
            "an 8-bit grayscale image."
        );
    }


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


            // ------------------------------------------------
            // A1保持精确
            // ------------------------------------------------

            const int a1 =
                5 * center
                - top;


            const std::uint16_t rawA1 =
                encodeSigned12Bits(
                    a1
                );


            // ------------------------------------------------
            // bit pattern 产生 trigger
            // ------------------------------------------------

            const bool trigger =
                matchesPattern(
                    rawA1,
                    candidate.bitA,
                    candidate.bitB,
                    candidate.bitC,
                    candidate.pattern
                );


            // ------------------------------------------------
            // Exact A2
            // ------------------------------------------------

            const int exactA2 =
                a1
                - bottom;


            // ------------------------------------------------
            // 模拟 MUX
            // ------------------------------------------------

            int a2 = exactA2;


            if (trigger)
            {
                a2 =
                    approximate::addSigned12(
                        a1,
                        -bottom,
                        candidate.unit
                    );
            }


            // ------------------------------------------------
            // A3、A4保持精确
            // ------------------------------------------------

            const int a3 =
                a2
                - left;


            const int a4 =
                clampToByte(
                    a3
                    - right
                );


            outputImage.at<unsigned char>(
                row,
                col
            ) =
                static_cast<unsigned char>(
                    a4
                );
        }
    }


    return outputImage;
}



// ============================================================
// EvoApprox名字
// ============================================================

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



// ============================================================
// 例如：
//
// pattern = 5
//
// 返回：
// "101"
// ============================================================

std::string patternString(
    unsigned int pattern
)
{
    std::string result;


    result +=
        ((pattern >> 2) & 1U)
        ? '1'
        : '0';


    result +=
        ((pattern >> 1) & 1U)
        ? '1'
        : '0';


    result +=
        (pattern & 1U)
        ? '1'
        : '0';


    return result;
}


}