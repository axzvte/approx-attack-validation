#include "analysis/stage1_screening.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>


namespace analysis
{

namespace
{

std::string structureKey(
    const AttackStructure& structure
)
{
    std::ostringstream stream;


    for (const auto& node : structure)
    {
        stream
            << node.nodeId
            << ":"
            << static_cast<int>(
                node.unit
            )
            << ":"
            << static_cast<int>(
                node.monitorInput
            )
            << "|";
    }


    return stream.str();
}


bool isBetterConfiguration(
    double meanScore,
    double worstScore,
    const Stage1SelectedStructure& currentBest
)
{
    constexpr double epsilon =
        1e-12;


    if (
        meanScore
        >
        currentBest.meanScore
        +
        epsilon
    )
    {
        return true;
    }


    if (
        std::abs(
            meanScore
            -
            currentBest.meanScore
        )
        <=
        epsilon
        &&
        worstScore
        >
        currentBest.worstScore
        +
        epsilon
    )
    {
        return true;
    }


    return false;
}

}


// =========================================================
// Stage 1 结构筛选
// =========================================================

std::vector<Stage1SelectedStructure>
Stage1Screening::screen(
    const TwoStageDataset& dataset,
    const BruteForceSearchSpace& searchSpace,
    const Stage1Evaluator& evaluator,
    const Stage1ScreeningOptions& options
)
{
    TwoStageSearch::validateDataset(
        dataset
    );


    if (!evaluator)
    {
        throw std::runtime_error(
            "Stage 1 evaluator is empty."
        );
    }


    if (
        options.minimumValidImages == 0
        ||
        options.minimumValidImages
            >
            dataset.stage1Images.size()
    )
    {
        throw std::runtime_error(
            "minimumValidImages is outside the Stage 1 image range."
        );
    }


    if (
        options.topStructuresPerNodeCount
        ==
        0
    )
    {
        throw std::runtime_error(
            "topStructuresPerNodeCount must be greater than zero."
        );
    }


    // key = 结构本身：
    // nodeId + attack unit + monitor input
    //
    // 同一结构的不同区间配置会落入同一个 key。
    std::map<
        std::string,
        Stage1SelectedStructure
    >
        bestByStructure;


    BruteForceSearch::enumerate(
        searchSpace,

        [&](
            const AttackConfiguration&
                configuration
        )
        {
            std::size_t validImageCount =
                0;


            double scoreSum =
                0.0;


            double worstScore =
                std::numeric_limits<double>::
                    infinity();


            for (
                std::size_t imageIndex = 0;
                imageIndex
                    <
                    dataset.stage1Images.size();
                ++imageIndex
            )
            {
                const Stage1ImageScore
                    imageScore =
                        evaluator(
                            configuration,
                            imageIndex,
                            dataset.stage1Images[
                                imageIndex
                            ]
                        );


                if (!imageScore.valid)
                {
                    continue;
                }


                if (
                    !std::isfinite(
                        imageScore.score
                    )
                )
                {
                    throw std::runtime_error(
                        "Stage 1 evaluator returned a non-finite score."
                    );
                }


                ++validImageCount;


                scoreSum +=
                    imageScore.score;


                worstScore =
                    std::min(
                        worstScore,
                        imageScore.score
                    );
            }


            if (
                validImageCount
                <
                options.minimumValidImages
            )
            {
                return;
            }


            const double meanScore =
                scoreSum
                /
                static_cast<double>(
                    validImageCount
                );


            const AttackStructure
                structure =
                    TwoStageSearch::
                        extractStructure(
                            configuration
                        );


            const std::string key =
                structureKey(
                    structure
                );


            auto iterator =
                bestByStructure.find(
                    key
                );


            if (
                iterator
                ==
                bestByStructure.end()
            )
            {
                Stage1SelectedStructure
                    selected;


                selected.structure =
                    structure;


                selected.bestConfiguration =
                    configuration;


                selected.validImageCount =
                    validImageCount;


                selected.meanScore =
                    meanScore;


                selected.worstScore =
                    worstScore;


                bestByStructure.emplace(
                    key,
                    std::move(
                        selected
                    )
                );


                return;
            }


            if (
                isBetterConfiguration(
                    meanScore,
                    worstScore,
                    iterator->second
                )
            )
            {
                iterator->second.structure =
                    structure;


                iterator->second.bestConfiguration =
                    configuration;


                iterator->second.validImageCount =
                    validImageCount;


                iterator->second.meanScore =
                    meanScore;


                iterator->second.worstScore =
                    worstScore;
            }
        }
    );


    // 不同攻击节点数量分别筛选。
    std::map<
        std::size_t,
        std::vector<Stage1SelectedStructure>
    >
        groupedByNodeCount;


    for (
        auto& entry :
        bestByStructure
    )
    {
        Stage1SelectedStructure
            selected =
                std::move(
                    entry.second
                );


        groupedByNodeCount[
            selected.structure.size()
        ].push_back(
            std::move(
                selected
            )
        );
    }


    std::vector<Stage1SelectedStructure>
        result;


    for (
        auto& groupEntry :
        groupedByNodeCount
    )
    {
        auto& group =
            groupEntry.second;


        std::sort(
            group.begin(),
            group.end(),

            [](
                const Stage1SelectedStructure& first,
                const Stage1SelectedStructure& second
            )
            {
                if (
                    first.meanScore
                    !=
                    second.meanScore
                )
                {
                    return
                        first.meanScore
                        >
                        second.meanScore;
                }


                return
                    first.worstScore
                    >
                    second.worstScore;
            }
        );


        if (
            group.size()
            >
            options.topStructuresPerNodeCount
        )
        {
            group.resize(
                options.topStructuresPerNodeCount
            );
        }


        for (
            auto& selected :
            group
        )
        {
            result.push_back(
                std::move(
                    selected
                )
            );
        }
    }


    return result;
}

}
