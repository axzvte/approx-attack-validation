#pragma once


namespace core
{

struct AddSample
{
    int nodeId;

    int input1;
    int input2;
    int output;

    // 当前这次 ADD 运算对应区域的 ROI 权重
    // 取值范围：[0.0, 1.0]
    double roiWeight;
};

}