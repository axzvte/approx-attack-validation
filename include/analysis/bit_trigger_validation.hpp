#pragma once

#include "approximate/evoapprox_adapter.hpp"

#include <opencv2/core/mat.hpp>

#include <cstddef>
#include <string>
#include <vector>


namespace bit_trigger_validation
{

struct BitTriggerCandidate
{
    // 监测 A1 的哪三个 bit
    int bitA;
    int bitB;
    int bitC;

    // 三个位需要匹配的模式
    // 例如：
    // 5 -> 二进制 101
    unsigned int pattern;

    // 使用哪个 EvoApprox 加法器
    approximate::ApproxUnitId unit;


    // 最终攻击结果
    double globalPsnr;
    double roiPsnr;
    double nonRoiPsnr;


    // 下面两个指标只作为辅助观察，
    // 不参与最终排名
    long long roiTriggered;
    long long nonRoiTriggered;

    double roiCoverage;
    double precision;
};


std::vector<BitTriggerCandidate>
searchA2BitTriggerCandidates(
    const cv::Mat& inputImage,
    const cv::Mat& roiMask,
    double minGlobalPsnr = 30.0,
    std::size_t topK = 10
);


cv::Mat renderA2BitTriggerAttack(
    const cv::Mat& inputImage,
    const BitTriggerCandidate& candidate
);


std::string unitName(
    approximate::ApproxUnitId unit
);


std::string patternString(
    unsigned int pattern
);

}