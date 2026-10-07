#pragma once

#include "core/attack_config.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>


namespace analysis
{

struct TriggerInterval
{
    int lower;
    int upper;
};


struct MonitorSearchSpace
{
    core::MonitorSignal monitorInput;

    std::vector<TriggerInterval>
        intervals;
};


struct NodeSearchSpace
{
    int nodeId;

    std::vector<approximate::ApproxUnitId>
        attackUnits;

    std::vector<MonitorSearchSpace>
        monitorSpaces;
};


struct BruteForceSearchSpace
{
    std::vector<NodeSearchSpace>
        nodes;

    std::size_t minAttackNodes = 1;

    std::size_t maxAttackNodes = 1;
};


using AttackConfiguration =
    std::vector<core::AttackConfig>;


using ConfigurationCallback =
    std::function<
        void(
            const AttackConfiguration&
        )
    >;


class BruteForceSearch
{
public:

    // 统计完整搜索空间中的配置数量。
    //
    // 对每一个节点组合，都会计算各节点候选配置之间
    // 的完整笛卡尔积。
    static std::uint64_t countConfigurations(
        const BruteForceSearchSpace& searchSpace
    );


    // 逐个枚举完整配置。
    //
    // 采用 callback 流式返回结果，不一次性将全部配置
    // 保存在内存中，便于后续直接执行电路、计算指标并写 CSV。
    static void enumerate(
        const BruteForceSearchSpace& searchSpace,
        const ConfigurationCallback& callback
    );


private:

    static void validate(
        const BruteForceSearchSpace& searchSpace
    );
};

}
