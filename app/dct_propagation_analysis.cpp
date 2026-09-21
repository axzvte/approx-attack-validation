#include "analysis/propagation_analyzer.hpp"
#include "applications/dct8_dfg_builder.hpp"
#include "core/dfg_graph.hpp"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <vector>


namespace
{

void printResult(
    const core::DfgGraph& graph,
    const analysis::PropagationResult& result
)
{
    const auto& node =
        graph.nodeById(
            result.sourceNodeId
        );


    std::cout
        << std::left
        << std::setw(8)
        << result.sourceNodeId

        << std::setw(22)
        << node.name

        << std::setw(14)
        << result.l1Gain

        << std::setw(14)
        << result.l2Gain

        << std::setw(14)
        << result.maxAbsGain

        << std::setw(10)
        << result.affectedOutputCount;


    for (const double gain
         : result.outputGains)
    {
        std::cout
            << std::setw(13)
            << gain;
    }


    std::cout
        << "\n";
}


void saveCsv(
    const core::DfgGraph& graph,
    const std::vector<analysis::PropagationResult>& results,
    const std::filesystem::path& outputPath
)
{
    if (
        outputPath.has_parent_path()
        &&
        !outputPath.parent_path().empty()
    )
    {
        std::filesystem::create_directories(
            outputPath.parent_path()
        );
    }


    std::ofstream file(
        outputPath
    );


    if (!file.is_open())
    {
        throw std::runtime_error(
            "Unable to open propagation result file."
        );
    }


    file
        << "NodeId,"
        << "NodeName,"
        << "L1Gain,"
        << "L2Gain,"
        << "MaxAbsGain,"
        << "AffectedOutputs,"
        << "X0,"
        << "X1,"
        << "X2,"
        << "X3,"
        << "X4,"
        << "X5,"
        << "X6,"
        << "X7\n";


    file
        << std::setprecision(12);


    for (const auto& result : results)
    {
        const auto& node =
            graph.nodeById(
                result.sourceNodeId
            );


        file
            << result.sourceNodeId
            << ","
            << node.name
            << ","
            << result.l1Gain
            << ","
            << result.l2Gain
            << ","
            << result.maxAbsGain
            << ","
            << result.affectedOutputCount;


        for (const double gain
             : result.outputGains)
        {
            file
                << ","
                << gain;
        }


        file
            << "\n";
    }
}

}


int main()
{
    // =====================================================
    // 1. 建立真实 DCT DFG
    // =====================================================

    applications::Dct8DfgBuilder
        builder;


    const core::DfgGraph graph =
        builder.build();


    // =====================================================
    // 2. 得到所有候选 ADD/SUB
    //
    // 当前 DCT：
    //
    // ADD_00 ~ ADD_31
    // =====================================================

    const std::vector<int>
        candidateNodeIds =
            graph.approximationCandidateIds();


    if (candidateNodeIds.size() != 32)
    {
        std::cerr
            << "Expected 32 DCT approximation candidates, got "
            << candidateNodeIds.size()
            << ".\n";

        return 1;
    }


    // =====================================================
    // 3. 对32个节点逐个做结构传播分析
    // =====================================================

    analysis::PropagationAnalyzer
        analyzer;


    const auto results =
        analyzer.analyzeAll(
            graph,
            candidateNodeIds
        );


    // =====================================================
    // 4. 输出到终端
    //
    // 暂时保持原始节点顺序：
    //
    // ADD_00
    // ADD_01
    // ...
    // ADD_31
    //
    // 不排序。
    // =====================================================

    std::cout
        << std::fixed
        << std::setprecision(6);


    std::cout
        << "\n"
        << "============================================================\n"
        << "DCT Structural Error Propagation Analysis\n"
        << "============================================================\n\n";


    std::cout
        << std::left
        << std::setw(8)
        << "ID"

        << std::setw(22)
        << "Node"

        << std::setw(14)
        << "L1"

        << std::setw(14)
        << "L2"

        << std::setw(14)
        << "MaxGain"

        << std::setw(10)
        << "Outputs"

        << std::setw(13)
        << "X0"

        << std::setw(13)
        << "X1"

        << std::setw(13)
        << "X2"

        << std::setw(13)
        << "X3"

        << std::setw(13)
        << "X4"

        << std::setw(13)
        << "X5"

        << std::setw(13)
        << "X6"

        << std::setw(13)
        << "X7"

        << "\n";


    for (const auto& result : results)
    {
        printResult(
            graph,
            result
        );
    }


    // =====================================================
    // 5. 同时保存CSV
    //
    // 后面我们分析节点筛选规则时，
    // 直接使用这个文件。
    // =====================================================

    const std::filesystem::path outputPath =
        "data/output/dct_propagation.csv";


    saveCsv(
        graph,
        results,
        outputPath
    );


    std::cout
        << "\nPropagation results saved to:\n"
        << outputPath.string()
        << "\n";


    std::cout
        << "\nTotal candidate nodes: "
        << results.size()
        << "\n";


    return 0;
}