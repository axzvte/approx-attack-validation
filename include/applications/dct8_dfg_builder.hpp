#pragma once

#include "core/dfg_graph.hpp"


namespace applications
{

// =========================================================
// 将当前 8-point DCT 的固定运算结构
// 转换成通用 DfgGraph。
//
// 只描述结构：
//
// 8 Inputs
// 32 ADD/SUB
// 32 Constant Multipliers
//
// 不包含：
//
// 图片
// ROI
// 近似加法器
// 触发区间
// =========================================================

class Dct8DfgBuilder
{
public:

    core::DfgGraph build() const;
};

}