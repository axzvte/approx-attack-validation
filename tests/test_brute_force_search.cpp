#include "analysis/brute_force_search.hpp"

#include "approximate/evoapprox_adapter.hpp"
#include "core/attack_config.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>


namespace
{

std::string configurationKey(
    const analysis::AttackConfiguration& configuration
)
{
    std::ostringstream stream;


    for (const auto& config : configuration)
    {
        stream
            << config.nodeId
            << ":"
            << static_cast<int>(
                config.unit
            )
            << ":"
            << static_cast<int>(
                config.monitorInput
            )
            << ":"
            << config.lower
            << ":"
            << config.upper
            << "|";
    }


    return stream.str();
}

}


int main()
{
    analysis::BruteForceSearchSpace
        searchSpace;


    // =====================================================
    // Node 0
    //
    // attack units: 2
    //
    // Input1 intervals: 2
    // Input2 intervals: 1
    //
    // 单节点候选数：
    // 2 * (2 + 1) = 6
    // =====================================================

    analysis::NodeSearchSpace
        node0;


    node0.nodeId =
        0;


    node0.attackUnits =
    {
        approximate::ApproxUnitId::Add12se5Z0,
        approximate::ApproxUnitId::Add12se5QT
    };


    node0.monitorSpaces =
    {
        {
            core::MonitorInput::Input1,
            {
                { 0, 10 },
                { 11, 20 }
            }
        },
        {
            core::MonitorInput::Input2,
            {
                { -10, 10 }
            }
        }
    };


    // =====================================================
    // Node 1
    //
    // attack units: 1
    //
    // Input1 intervals: 1
    // Input2 intervals: 1
    //
    // 单节点候选数：
    // 1 * (1 + 1) = 2
    // =====================================================

    analysis::NodeSearchSpace
        node1;


    node1.nodeId =
        1;


    node1.attackUnits =
    {
        approximate::ApproxUnitId::Add12se5SB
    };


    node1.monitorSpaces =
    {
        {
            core::MonitorInput::Input1,
            {
                { 100, 120 }
            }
        },
        {
            core::MonitorInput::Input2,
            {
                { 200, 220 }
            }
        }
    };


    searchSpace.nodes =
    {
        node0,
        node1
    };


    searchSpace.minAttackNodes =
        1;


    searchSpace.maxAttackNodes =
        2;


    // =====================================================
    // 预期：
    //
    // 单节点：
    // Node0 = 6
    // Node1 = 2
    // 共 8
    //
    // 双节点完整笛卡尔积：
    // 6 * 2 = 12
    //
    // 总计：
    // 8 + 12 = 20
    // =====================================================

    const std::uint64_t expectedTotal =
        20;


    const std::uint64_t countedTotal =
        analysis::BruteForceSearch::
            countConfigurations(
                searchSpace
            );


    if (countedTotal != expectedTotal)
    {
        std::cerr
            << "Unexpected configuration count: "
            << countedTotal
            << ", expected "
            << expectedTotal
            << ".\n";

        return 1;
    }


    std::uint64_t emittedTotal =
        0;


    std::uint64_t singleNodeTotal =
        0;


    std::uint64_t twoNodeTotal =
        0;


    std::set<std::string>
        uniqueConfigurations;


    analysis::BruteForceSearch::enumerate(
        searchSpace,

        [&](
            const analysis::AttackConfiguration&
                configuration
        )
        {
            ++emittedTotal;


            if (configuration.size() == 1)
            {
                ++singleNodeTotal;
            }
            else if (configuration.size() == 2)
            {
                ++twoNodeTotal;


                if (
                    configuration[0].nodeId
                    ==
                    configuration[1].nodeId
                )
                {
                    throw std::runtime_error(
                        "Duplicate node in one attack configuration."
                    );
                }
            }
            else
            {
                throw std::runtime_error(
                    "Unexpected attack node count."
                );
            }


            uniqueConfigurations.insert(
                configurationKey(
                    configuration
                )
            );
        }
    );


    if (
        emittedTotal != 20
        ||
        singleNodeTotal != 8
        ||
        twoNodeTotal != 12
    )
    {
        std::cerr
            << "Enumeration count mismatch. "
            << "total="
            << emittedTotal
            << ", single="
            << singleNodeTotal
            << ", double="
            << twoNodeTotal
            << ".\n";

        return 1;
    }


    if (
        uniqueConfigurations.size()
        !=
        static_cast<std::size_t>(
            expectedTotal
        )
    )
    {
        std::cerr
            << "Duplicate configurations were emitted.\n";

        return 1;
    }


    std::cout
        << "Brute-force configuration count: "
        << countedTotal
        << "\n";


    std::cout
        << "Single-node configurations: "
        << singleNodeTotal
        << "\n";


    std::cout
        << "Two-node Cartesian-product configurations: "
        << twoNodeTotal
        << "\n";


    std::cout
        << "BruteForceSearch test passed.\n";


    return 0;
}
