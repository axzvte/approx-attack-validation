#pragma once

#include "analysis/stage1_screening.hpp"
#include "applications/dct.hpp"

#include <opencv2/core.hpp>

#include <cstddef>
#include <vector>


namespace analysis
{

class DctStage1Evaluator
{
public:

    // 构造时一次性加载：
    // 1. Stage 1 的 10 张图片
    // 2. 统一 ROI mask
    // 3. 每张图片对应的精确 DCT 重建结果
    //
    // 后续暴力搜索时只重复运行被攻击的近似 DCT，
    // 避免每个配置都重新读取图片和计算参考结果。
    explicit DctStage1Evaluator(
        const TwoStageDataset& dataset
    );


    Stage1ImageMetrics evaluate(
        const AttackConfiguration& configuration,
        std::size_t imageIndex
    ) const;


    std::size_t imageCount() const;


private:

    applications::DctApplication
        application_;


    std::vector<cv::Mat>
        roiMasks_;


    std::vector<cv::Mat>
        inputImages_;


    std::vector<cv::Mat>
        referenceImages_;
};

}
