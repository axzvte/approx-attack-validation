#pragma once


namespace core
{

struct AddSample
{
    int nodeId;

    int input1;
    int input2;


    // 当前配置真正送往后续电路的节点输出。
    int output;


    // 当前这次 ADD 运算对应区域的 ROI 权重
    // 取值范围：[0.0, 1.0]
    double roiWeight;


    // 原 Baseline 单元在当前 input1/input2 下的输出。
    //
    // 即使当前节点配置触发了新增近似单元，
    // baselineOutput 仍保存原正常路径（当前默认 5RP）的结果。
    //
    // 这样 BaselineOutput 可以作为独立 monitor signal，
    // 同时上游节点造成的输入变化仍会自然反映在该值中。
    //
    // 默认使用 output 保持旧的五字段测试样本兼容。
    int baselineOutput =
        output;
};

}
