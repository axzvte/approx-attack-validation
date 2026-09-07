#pragma once

#include "approximate/evoapprox_adapter.hpp"

#include <opencv2/core/mat.hpp>

#include <cstddef>
#include <string>
#include <vector>


namespace interval_attack_search
{

enum class AttackPosition
{
    A1,
    A2,
    A3,
    A4
};


enum class MonitorSignal
{
    C,
    T,
    B,
    L,
    R,

    FiveC,

    A1,
    A2,
    A3
};


struct Candidate
{
    AttackPosition attackPosition;

    MonitorSignal monitorSignal;

    approximate::ApproxUnitId unit;

    int lower;
    int upper;


    // 最终图像评价
    double globalPsnr;

    double roiPsnr;
    double nonRoiPsnr;

    double roiMse;
    double nonRoiMse;


    // 核心搜索分数
    double errorGap;

    // 辅助观察
    double errorRatio;

    double roiTriggerRate;
    double nonRoiTriggerRate;
};


std::vector<Candidate>
searchBestCandidates(
    const cv::Mat& inputImage,
    const cv::Mat& roiMask,
    double minGlobalPsnr = 30.0,
    std::size_t topK = 20
);


cv::Mat renderAttack(
    const cv::Mat& inputImage,
    const Candidate& candidate
);


std::string attackPositionName(
    AttackPosition position
);


std::string monitorSignalName(
    MonitorSignal signal
);


std::string unitName(
    approximate::ApproxUnitId unit
);

}