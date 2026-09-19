#pragma once

#include <string>
#include <vector>


namespace core
{

// =========================================================
// DFG 节点的运算类型
// =========================================================

enum class DfgOperation
{
    Input,

    Add,
    Subtract,

    // 一个变量乘固定常数，例如：
    //
    // y = 3.5 * x
    MultiplyConstant,

    // 两个运行时变量相乘：
    //
    // y = a * b
    //
    // 这种情况的误差传播依赖真实输入数据。
    Multiply,

    // 以下运算属于数据相关运算，
    // 后续结构传播分析时需要单独处理。
    Relu,
    Max,
    Min,
    Clip
};


// =========================================================
// DFG 节点
// =========================================================

struct DfgNode
{
    int id;

    std::string name;

    DfgOperation operation;


    // 仅 MultiplyConstant 使用。
    //
    // 例如：
    //
    // y = 2.5 * x
    //
    // constant = 2.5
    double constant = 0.0;


    // 当前课题只允许 ADD / SUB
    // 作为近似节点。
    //
    // true:
    //     后续进入候选节点集合
    //
    // false:
    //     只参与误差传播，不作为攻击位置
    bool approximationCandidate = false;
};


// =========================================================
// DFG 中的一条边
// =========================================================

struct DfgEdge
{
    int fromNodeId;

    int toNodeId;


    // 当前数据接到目标节点的第几个输入。
    //
    // 例如：
    //
    // y = a - b
    //
    // a -> inputIndex = 0
    // b -> inputIndex = 1
    int inputIndex;
};


// =========================================================
// 通用 DFG
// =========================================================

class DfgGraph
{
public:

    void addNode(
        const DfgNode& node
    );


    void addEdge(
        const DfgEdge& edge
    );


    void addOutputNode(
        int nodeId
    );


    const std::vector<DfgNode>&
    nodes() const;


    const std::vector<DfgEdge>&
    edges() const;


    const std::vector<int>&
    outputNodeIds() const;


    bool containsNode(
        int nodeId
    ) const;


    const DfgNode& nodeById(
        int nodeId
    ) const;


    // 返回所有允许近似化的节点。
    std::vector<int>
    approximationCandidateIds() const;


    // 检查整个 DFG 是否合法。
    void validate() const;


private:

    std::vector<DfgNode>
        nodes_;


    std::vector<DfgEdge>
        edges_;


    std::vector<int>
        outputNodeIds_;
};

}