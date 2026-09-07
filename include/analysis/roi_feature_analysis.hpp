#pragma once

#include <opencv2/core/mat.hpp>

#include <cstddef>
#include <string>
#include <vector>


namespace roi_feature_analysis
{

struct PixelSample
{
    int row;
    int col;

    int center;
    int top;
    int bottom;
    int left;
    int right;

    int a1;
    int a2;
    int a3;
    int a4;

    bool roi;
};


enum class CompareOp
{
    LessEqual,
    GreaterEqual
};


struct RuleMetrics
{
    long long tp;
    long long fp;
    long long tn;
    long long fn;

    double precision;

    // 也就是：
    // ROI 中有多少比例被规则识别出来
    double roiCoverage;

    double f1;

    double specificity;

    double balancedAccuracy;

    double triggerRate;
};


struct SingleRule
{
    int featureId;

    std::string featureName;

    CompareOp op;

    int threshold;

    RuleMetrics metrics;
};


struct PairRule
{
    SingleRule first;

    SingleRule second;

    bool useAnd;

    RuleMetrics metrics;
};


// 收集锐化过程中每个像素的数据
std::vector<PixelSample>
collectSamples(
    const cv::Mat& inputImage,
    const cv::Mat& roiMask
);


// 保存所有原始数据和派生特征
void saveFeatureCsv(
    const std::vector<PixelSample>& samples,
    const std::string& path
);


// 搜索每个特征最好的单阈值规则
std::vector<SingleRule>
findBestSingleRules(
    const std::vector<PixelSample>& samples
);


// 从表现较好的单规则中组合：
// condition1 AND condition2
// condition1 OR condition2
std::vector<PairRule>
findBestPairRules(
    const std::vector<PixelSample>& samples,
    const std::vector<SingleRule>& singleRules,
    std::size_t topFeatureCount = 10,
    std::size_t topK = 30
);


// 保存分析结果
void saveRuleReport(
    const std::vector<SingleRule>& singleRules,
    const std::vector<PairRule>& pairRules,
    const std::string& path
);


std::string compareOpName(
    CompareOp op
);

}