#include "analysis/roi_feature_analysis.hpp"

#include "region/region_mask.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>


namespace roi_feature_analysis
{

namespace
{

// ============================================================
// 当前分析的全部特征
//
// 注意：
// row / col 会保存到 CSV，
// 但绝对不会拿来参与规则搜索。
// ============================================================

constexpr int FEATURE_COUNT = 23;


// ------------------------------------------------------------
// 根据 featureId 获取特征名称
// ------------------------------------------------------------

std::string getFeatureName(
    int featureId
)
{
    switch (featureId)
    {
        case 0:
            return "C";

        case 1:
            return "T";

        case 2:
            return "B";

        case 3:
            return "L";

        case 4:
            return "R";

        case 5:
            return "A1";

        case 6:
            return "A2";

        case 7:
            return "A3";

        case 8:
            return "A4";

        case 9:
            return "C_minus_T";

        case 10:
            return "C_minus_B";

        case 11:
            return "C_minus_L";

        case 12:
            return "C_minus_R";

        case 13:
            return "abs_C_T";

        case 14:
            return "abs_C_B";

        case 15:
            return "abs_C_L";

        case 16:
            return "abs_C_R";

        case 17:
            return "abs_L_R";

        case 18:
            return "abs_T_B";

        case 19:
            return "local_range";

        case 20:
            return "local_contrast";

        case 21:
            return "structure";

        case 22:
            return "gradient";

        default:
            throw std::runtime_error(
                "Unknown feature id."
            );
    }
}


// ------------------------------------------------------------
// 根据 featureId 获取某个像素对应的特征值
// ------------------------------------------------------------

int getFeatureValue(
    const PixelSample& sample,
    int featureId
)
{
    const int c =
        sample.center;

    const int t =
        sample.top;

    const int b =
        sample.bottom;

    const int l =
        sample.left;

    const int r =
        sample.right;


    switch (featureId)
    {
        // ====================================================
        // 第一类：
        // 原始图像数据
        // ====================================================

        case 0:
            return c;

        case 1:
            return t;

        case 2:
            return b;

        case 3:
            return l;

        case 4:
            return r;


        // ====================================================
        // 第二类：
        // 锐化 DFG 中间数据
        // ====================================================

        case 5:
            return sample.a1;

        case 6:
            return sample.a2;

        case 7:
            return sample.a3;

        case 8:
            return sample.a4;


        // ====================================================
        // 第三类：
        // 有方向的差值
        //
        // 正负号也保留下来
        // ====================================================

        case 9:
            return c - t;

        case 10:
            return c - b;

        case 11:
            return c - l;

        case 12:
            return c - r;


        // ====================================================
        // 第四类：
        // 不考虑方向的局部差异
        // ====================================================

        case 13:
            return std::abs(c - t);

        case 14:
            return std::abs(c - b);

        case 15:
            return std::abs(c - l);

        case 16:
            return std::abs(c - r);


        // 左右变化
        case 17:
            return std::abs(l - r);


        // 上下变化
        case 18:
            return std::abs(t - b);


        // ====================================================
        // 第五类：
        // 局部统计特征
        // ====================================================

        case 19:
        {
            const int maximum =
                std::max(
                    {
                        c,
                        t,
                        b,
                        l,
                        r
                    }
                );

            const int minimum =
                std::min(
                    {
                        c,
                        t,
                        b,
                        l,
                        r
                    }
                );

            return
                maximum
                -
                minimum;
        }


        // ----------------------------------------------------
        // 中心像素与四个邻居的总差异
        // ----------------------------------------------------

        case 20:
            return
                  std::abs(c - t)
                + std::abs(c - b)
                + std::abs(c - l)
                + std::abs(c - r);


        // ----------------------------------------------------
        // 锐化相关的局部结构强度
        //
        // |4C - T - B - L - R|
        // ----------------------------------------------------

        case 21:
            return
                std::abs(
                      4 * c
                    - t
                    - b
                    - l
                    - r
                );


        // ----------------------------------------------------
        // 简单水平 + 垂直变化
        // ----------------------------------------------------

        case 22:
            return
                  std::abs(l - r)
                + std::abs(t - b);


        default:
            throw std::runtime_error(
                "Unknown feature id."
            );
    }
}


// ============================================================
// 根据 TP/FP/TN/FN 计算评价指标
// ============================================================

RuleMetrics calculateMetrics(
    long long tp,
    long long fp,
    long long tn,
    long long fn
)
{
    RuleMetrics metrics{};

    metrics.tp = tp;
    metrics.fp = fp;
    metrics.tn = tn;
    metrics.fn = fn;


    const double predictedPositive =
        static_cast<double>(
            tp + fp
        );


    const double actualPositive =
        static_cast<double>(
            tp + fn
        );


    const double actualNegative =
        static_cast<double>(
            tn + fp
        );


    const double total =
        static_cast<double>(
            tp + fp + tn + fn
        );


    if (predictedPositive > 0.0)
    {
        metrics.precision =
            static_cast<double>(
                tp
            )
            /
            predictedPositive;
    }
    else
    {
        metrics.precision = 0.0;
    }


    if (actualPositive > 0.0)
    {
        metrics.roiCoverage =
            static_cast<double>(
                tp
            )
            /
            actualPositive;
    }
    else
    {
        metrics.roiCoverage = 0.0;
    }


    if (
        metrics.precision
        +
        metrics.roiCoverage
        >
        0.0
    )
    {
        metrics.f1 =
            2.0
            *
            metrics.precision
            *
            metrics.roiCoverage
            /
            (
                metrics.precision
                +
                metrics.roiCoverage
            );
    }
    else
    {
        metrics.f1 = 0.0;
    }


    if (actualNegative > 0.0)
    {
        metrics.specificity =
            static_cast<double>(
                tn
            )
            /
            actualNegative;
    }
    else
    {
        metrics.specificity = 0.0;
    }


    metrics.balancedAccuracy =
        (
            metrics.roiCoverage
            +
            metrics.specificity
        )
        /
        2.0;


    if (total > 0.0)
    {
        metrics.triggerRate =
            static_cast<double>(
                tp + fp
            )
            /
            total;
    }
    else
    {
        metrics.triggerRate = 0.0;
    }


    return metrics;
}


// ============================================================
// 比较两条规则谁更好
//
// 第一优先：F1
//
// F1 同时考虑：
// precision 和 ROI coverage
//
// 第二优先：balanced accuracy
// ============================================================

bool betterMetrics(
    const RuleMetrics& first,
    const RuleMetrics& second
)
{
    constexpr double EPSILON =
        1e-12;


    if (
        first.f1
        >
        second.f1
        +
        EPSILON
    )
    {
        return true;
    }


    if (
        std::abs(
            first.f1
            -
            second.f1
        )
        <=
        EPSILON
    )
    {
        if (
            first.balancedAccuracy
            >
            second.balancedAccuracy
        )
        {
            return true;
        }
    }


    return false;
}


// ============================================================
// 判断某个像素是否满足一条单规则
// ============================================================

bool matchesRule(
    const PixelSample& sample,
    const SingleRule& rule
)
{
    const int value =
        getFeatureValue(
            sample,
            rule.featureId
        );


    if (
        rule.op
        ==
        CompareOp::LessEqual
    )
    {
        return
            value
            <=
            rule.threshold;
    }


    return
        value
        >=
        rule.threshold;
}


// ============================================================
// 搜索一个特征的最佳阈值
//
// 同时测试：
//
// feature <= threshold
//
// feature >= threshold
//
// 这里不是粗略枚举阈值，
// 而是遍历数据中真正出现过的值。
// ============================================================

SingleRule findBestRuleForFeature(
    const std::vector<PixelSample>& samples,
    int featureId
)
{
    struct ValueLabel
    {
        int value;

        bool roi;
    };


    std::vector<ValueLabel>
        data;


    data.reserve(
        samples.size()
    );


    long long totalPositive = 0;

    long long totalNegative = 0;


    for (const auto& sample : samples)
    {
        data.push_back(
            {
                getFeatureValue(
                    sample,
                    featureId
                ),

                sample.roi
            }
        );


        if (sample.roi)
        {
            ++totalPositive;
        }
        else
        {
            ++totalNegative;
        }
    }


    if (
        totalPositive == 0
        ||
        totalNegative == 0
    )
    {
        throw std::runtime_error(
            "ROI analysis requires both "
            "ROI and non-ROI samples."
        );
    }


    std::sort(
        data.begin(),
        data.end(),

        [](
            const ValueLabel& first,
            const ValueLabel& second
        )
        {
            return
                first.value
                <
                second.value;
        }
    );


    SingleRule bestRule{};

    bestRule.featureId =
        featureId;

    bestRule.featureName =
        getFeatureName(
            featureId
        );

    bestRule.op =
        CompareOp::GreaterEqual;

    bestRule.threshold = 0;

    bestRule.metrics =
        calculateMetrics(
            0,
            0,
            totalNegative,
            totalPositive
        );


    // ========================================================
    // 情况一：
    //
    // feature <= threshold
    // ========================================================

    {
        long long tp = 0;
        long long fp = 0;

        long long tn =
            totalNegative;

        long long fn =
            totalPositive;


        std::size_t i = 0;


        while (i < data.size())
        {
            const int threshold =
                data[i].value;


            std::size_t j = i;


            while (
                j < data.size()
                &&
                data[j].value
                ==
                threshold
            )
            {
                if (data[j].roi)
                {
                    ++tp;
                    --fn;
                }
                else
                {
                    ++fp;
                    --tn;
                }

                ++j;
            }


            const RuleMetrics metrics =
                calculateMetrics(
                    tp,
                    fp,
                    tn,
                    fn
                );


            if (
                betterMetrics(
                    metrics,
                    bestRule.metrics
                )
            )
            {
                bestRule.op =
                    CompareOp::LessEqual;

                bestRule.threshold =
                    threshold;

                bestRule.metrics =
                    metrics;
            }


            i = j;
        }
    }


    // ========================================================
    // 情况二：
    //
    // feature >= threshold
    // ========================================================

    {
        long long tp = 0;
        long long fp = 0;

        long long tn =
            totalNegative;

        long long fn =
            totalPositive;


        std::size_t end =
            data.size();


        while (end > 0)
        {
            const int threshold =
                data[
                    end - 1
                ].value;


            std::size_t begin =
                end;


            while (
                begin > 0
                &&
                data[
                    begin - 1
                ].value
                ==
                threshold
            )
            {
                --begin;


                if (data[begin].roi)
                {
                    ++tp;
                    --fn;
                }
                else
                {
                    ++fp;
                    --tn;
                }
            }


            const RuleMetrics metrics =
                calculateMetrics(
                    tp,
                    fp,
                    tn,
                    fn
                );


            if (
                betterMetrics(
                    metrics,
                    bestRule.metrics
                )
            )
            {
                bestRule.op =
                    CompareOp::GreaterEqual;

                bestRule.threshold =
                    threshold;

                bestRule.metrics =
                    metrics;
            }


            end =
                begin;
        }
    }


    return bestRule;
}


// ============================================================
// 计算两条规则组合后的效果
// ============================================================

RuleMetrics evaluatePairRule(
    const std::vector<PixelSample>& samples,
    const SingleRule& first,
    const SingleRule& second,
    bool useAnd
)
{
    long long tp = 0;
    long long fp = 0;
    long long tn = 0;
    long long fn = 0;


    for (const auto& sample : samples)
    {
        const bool firstMatch =
            matchesRule(
                sample,
                first
            );


        const bool secondMatch =
            matchesRule(
                sample,
                second
            );


        bool predictedRoi = false;


        if (useAnd)
        {
            predictedRoi =
                firstMatch
                &&
                secondMatch;
        }
        else
        {
            predictedRoi =
                firstMatch
                ||
                secondMatch;
        }


        if (
            predictedRoi
            &&
            sample.roi
        )
        {
            ++tp;
        }
        else if (
            predictedRoi
            &&
            !sample.roi
        )
        {
            ++fp;
        }
        else if (
            !predictedRoi
            &&
            !sample.roi
        )
        {
            ++tn;
        }
        else
        {
            ++fn;
        }
    }


    return
        calculateMetrics(
            tp,
            fp,
            tn,
            fn
        );
}


// ============================================================
// 创建输出目录
// ============================================================

void createParentDirectory(
    const std::string& path
)
{
    const std::filesystem::path
        outputPath(
            path
        );


    if (
        outputPath.has_parent_path()
    )
    {
        std::filesystem::create_directories(
            outputPath.parent_path()
        );
    }
}

}


// ============================================================
// 收集数据
// ============================================================

std::vector<PixelSample>
collectSamples(
    const cv::Mat& inputImage,
    const cv::Mat& roiMask
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
        inputImage.type()
        !=
        CV_8UC1
        ||
        roiMask.type()
        !=
        CV_8UC1
    )
    {
        throw std::runtime_error(
            "ROI feature analysis requires "
            "8-bit grayscale images."
        );
    }


    if (
        inputImage.size()
        !=
        roiMask.size()
    )
    {
        throw std::runtime_error(
            "Input image and ROI mask "
            "sizes do not match."
        );
    }


    std::vector<PixelSample>
        samples;


    samples.reserve(
        static_cast<std::size_t>(
            inputImage.rows - 2
        )
        *
        static_cast<std::size_t>(
            inputImage.cols - 2
        )
    );


    // ========================================================
    // 边缘一圈不参与，
    // 因为锐化需要上下左右邻居。
    // ========================================================

    for (
        int row = 1;
        row < inputImage.rows - 1;
        ++row
    )
    {
        for (
            int col = 1;
            col < inputImage.cols - 1;
            ++col
        )
        {
            const int c =
                inputImage.at<unsigned char>(
                    row,
                    col
                );


            const int t =
                inputImage.at<unsigned char>(
                    row - 1,
                    col
                );


            const int b =
                inputImage.at<unsigned char>(
                    row + 1,
                    col
                );


            const int l =
                inputImage.at<unsigned char>(
                    row,
                    col - 1
                );


            const int r =
                inputImage.at<unsigned char>(
                    row,
                    col + 1
                );


            // =================================================
            // 锐化 DFG
            //
            // Y = 5C - T - B - L - R
            // =================================================

            const int a1 =
                5 * c
                - t;


            const int a2 =
                a1
                - b;


            const int a3 =
                a2
                - l;


            const int a4 =
                a3
                - r;


            PixelSample sample{};


            sample.row =
                row;

            sample.col =
                col;


            sample.center =
                c;

            sample.top =
                t;

            sample.bottom =
                b;

            sample.left =
                l;

            sample.right =
                r;


            sample.a1 =
                a1;

            sample.a2 =
                a2;

            sample.a3 =
                a3;

            sample.a4 =
                a4;


            // mask 只在这里提供“正确答案”
            sample.roi =
                region_mask::isImportantPixel(
                    roiMask,
                    row,
                    col
                );


            samples.push_back(
                sample
            );
        }
    }


    return samples;
}



// ============================================================
// 保存所有数据
// ============================================================

void saveFeatureCsv(
    const std::vector<PixelSample>& samples,
    const std::string& path
)
{
    createParentDirectory(
        path
    );


    std::ofstream file(
        path
    );


    if (!file.is_open())
    {
        throw std::runtime_error(
            "Cannot open feature CSV."
        );
    }


    file
        << "row,col";


    for (
        int featureId = 0;
        featureId < FEATURE_COUNT;
        ++featureId
    )
    {
        file
            << ","
            << getFeatureName(
                featureId
            );
    }


    file
        << ",ROI\n";


    for (const auto& sample : samples)
    {
        file
            << sample.row
            << ","
            << sample.col;


        for (
            int featureId = 0;
            featureId < FEATURE_COUNT;
            ++featureId
        )
        {
            file
                << ","
                << getFeatureValue(
                    sample,
                    featureId
                );
        }


        file
            << ","
            << (
                sample.roi
                ? 1
                : 0
            )
            << "\n";
    }
}



// ============================================================
// 搜索所有单特征
// ============================================================

std::vector<SingleRule>
findBestSingleRules(
    const std::vector<PixelSample>& samples
)
{
    if (samples.empty())
    {
        throw std::runtime_error(
            "No samples available."
        );
    }


    std::vector<SingleRule>
        rules;


    rules.reserve(
        FEATURE_COUNT
    );


    for (
        int featureId = 0;
        featureId < FEATURE_COUNT;
        ++featureId
    )
    {
        rules.push_back(
            findBestRuleForFeature(
                samples,
                featureId
            )
        );
    }


    std::sort(
        rules.begin(),
        rules.end(),

        [](
            const SingleRule& first,
            const SingleRule& second
        )
        {
            return
                betterMetrics(
                    first.metrics,
                    second.metrics
                );
        }
    );


    return rules;
}



// ============================================================
// 搜索两条件组合
//
// 为控制搜索量：
// 先选择单特征表现最好的若干种不同特征。
//
// 然后分别测试：
//
// rule1 AND rule2
//
// rule1 OR rule2
// ============================================================

std::vector<PairRule>
findBestPairRules(
    const std::vector<PixelSample>& samples,
    const std::vector<SingleRule>& singleRules,
    std::size_t topFeatureCount,
    std::size_t topK
)
{
    if (
        samples.empty()
        ||
        singleRules.empty()
    )
    {
        throw std::runtime_error(
            "No rule-search data available."
        );
    }


    topFeatureCount =
        std::min(
            topFeatureCount,
            singleRules.size()
        );


    std::vector<PairRule>
        pairRules;


    for (
        std::size_t firstIndex = 0;
        firstIndex < topFeatureCount;
        ++firstIndex
    )
    {
        for (
            std::size_t secondIndex =
                firstIndex + 1;

            secondIndex < topFeatureCount;

            ++secondIndex
        )
        {
            const auto& first =
                singleRules[
                    firstIndex
                ];


            const auto& second =
                singleRules[
                    secondIndex
                ];


            // =================================================
            // AND
            // =================================================

            {
                PairRule rule{};

                rule.first =
                    first;

                rule.second =
                    second;

                rule.useAnd =
                    true;


                rule.metrics =
                    evaluatePairRule(
                        samples,
                        first,
                        second,
                        true
                    );


                pairRules.push_back(
                    rule
                );
            }


            // =================================================
            // OR
            // =================================================

            {
                PairRule rule{};

                rule.first =
                    first;

                rule.second =
                    second;

                rule.useAnd =
                    false;


                rule.metrics =
                    evaluatePairRule(
                        samples,
                        first,
                        second,
                        false
                    );


                pairRules.push_back(
                    rule
                );
            }
        }
    }


    std::sort(
        pairRules.begin(),
        pairRules.end(),

        [](
            const PairRule& first,
            const PairRule& second
        )
        {
            return
                betterMetrics(
                    first.metrics,
                    second.metrics
                );
        }
    );


    if (
        pairRules.size()
        >
        topK
    )
    {
        pairRules.resize(
            topK
        );
    }


    return pairRules;
}



// ============================================================
// 保存分析结果
// ============================================================

void saveRuleReport(
    const std::vector<SingleRule>& singleRules,
    const std::vector<PairRule>& pairRules,
    const std::string& path
)
{
    createParentDirectory(
        path
    );


    std::ofstream file(
        path
    );


    if (!file.is_open())
    {
        throw std::runtime_error(
            "Cannot open rule report CSV."
        );
    }


    file
        << std::fixed
        << std::setprecision(6);


    file
        << "method,rank,"
        << "feature1,op1,threshold1,"
        << "logic,"
        << "feature2,op2,threshold2,"
        << "TP,FP,TN,FN,"
        << "precision,"
        << "roi_coverage,"
        << "f1,"
        << "specificity,"
        << "balanced_accuracy,"
        << "trigger_rate\n";


    // ========================================================
    // 单特征规则
    // ========================================================

    for (
        std::size_t i = 0;
        i < singleRules.size();
        ++i
    )
    {
        const auto& rule =
            singleRules[i];


        file
            << "single,"
            << i + 1
            << ","

            << rule.featureName
            << ","

            << compareOpName(
                rule.op
            )
            << ","

            << rule.threshold
            << ","

            << ","
            << ","
            << ","
            << ","

            << rule.metrics.tp
            << ","

            << rule.metrics.fp
            << ","

            << rule.metrics.tn
            << ","

            << rule.metrics.fn
            << ","

            << rule.metrics.precision
            << ","

            << rule.metrics.roiCoverage
            << ","

            << rule.metrics.f1
            << ","

            << rule.metrics.specificity
            << ","

            << rule.metrics.balancedAccuracy
            << ","

            << rule.metrics.triggerRate
            << "\n";
    }


    // ========================================================
    // 双条件规则
    // ========================================================

    for (
        std::size_t i = 0;
        i < pairRules.size();
        ++i
    )
    {
        const auto& rule =
            pairRules[i];


        file
            << "pair,"
            << i + 1
            << ","

            << rule.first.featureName
            << ","

            << compareOpName(
                rule.first.op
            )
            << ","

            << rule.first.threshold
            << ","

            << (
                rule.useAnd
                ? "AND"
                : "OR"
            )
            << ","

            << rule.second.featureName
            << ","

            << compareOpName(
                rule.second.op
            )
            << ","

            << rule.second.threshold
            << ","

            << rule.metrics.tp
            << ","

            << rule.metrics.fp
            << ","

            << rule.metrics.tn
            << ","

            << rule.metrics.fn
            << ","

            << rule.metrics.precision
            << ","

            << rule.metrics.roiCoverage
            << ","

            << rule.metrics.f1
            << ","

            << rule.metrics.specificity
            << ","

            << rule.metrics.balancedAccuracy
            << ","

            << rule.metrics.triggerRate
            << "\n";
    }
}



std::string compareOpName(
    CompareOp op
)
{
    if (
        op
        ==
        CompareOp::LessEqual
    )
    {
        return "<=";
    }


    return ">=";
}


}