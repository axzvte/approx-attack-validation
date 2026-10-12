#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>


namespace
{

struct IntervalStats
{
    std::size_t count = 0;

    double centerSum = 0.0;
    double centerSquareSum = 0.0;

    double widthSum = 0.0;
    double widthSquareSum = 0.0;


    void add(
        int lower,
        int upper
    )
    {
        const double center =
            (
                static_cast<double>(lower)
                +
                static_cast<double>(upper)
            )
            /
            2.0;


        const double width =
            static_cast<double>(
                upper
                -
                lower
            );


        ++count;

        centerSum +=
            center;

        centerSquareSum +=
            center
            *
            center;

        widthSum +=
            width;

        widthSquareSum +=
            width
            *
            width;
    }


    double meanCenter() const
    {
        return
            centerSum
            /
            static_cast<double>(
                count
            );
    }


    double meanWidth() const
    {
        return
            widthSum
            /
            static_cast<double>(
                count
            );
    }


    double centerStd() const
    {
        const double mean =
            meanCenter();


        const double variance =
            std::max(
                0.0,
                centerSquareSum
                    /
                    static_cast<double>(
                        count
                    )
                    -
                    mean
                    *
                    mean
            );


        return
            std::sqrt(
                variance
            );
    }


    double widthStd() const
    {
        const double mean =
            meanWidth();


        const double variance =
            std::max(
                0.0,
                widthSquareSum
                    /
                    static_cast<double>(
                        count
                    )
                    -
                    mean
                    *
                    mean
            );


        return
            std::sqrt(
                variance
            );
    }
};


struct ResultRow
{
    int nodeId = -1;

    std::string nodeName;

    std::string monitor;

    std::string unit;

    int lower = 0;

    int upper = 0;
};


using NodeMonitorKey =
    std::pair<
        int,
        std::string
    >;


using NodeUnitKey =
    std::pair<
        int,
        std::string
    >;


using TupleKey =
    std::tuple<
        int,
        std::string,
        std::string
    >;


std::string twoDigit(
    int value
)
{
    std::ostringstream stream;

    stream
        << std::setw(2)
        << std::setfill('0')
        << value;

    return stream.str();
}


std::vector<std::string> splitCsvLine(
    const std::string& line
)
{
    std::vector<std::string>
        fields;


    std::stringstream stream(
        line
    );


    std::string field;


    while (
        std::getline(
            stream,
            field,
            ','
        )
    )
    {
        fields.push_back(
            field
        );
    }


    return fields;
}


std::vector<ResultRow> loadResult(
    const std::filesystem::path& path
)
{
    std::ifstream file(
        path
    );


    if (!file.is_open())
    {
        throw std::runtime_error(
            "Unable to open result CSV: "
            +
            path.string()
        );
    }


    std::string line;


    if (
        !std::getline(
            file,
            line
        )
    )
    {
        throw std::runtime_error(
            "Result CSV is empty: "
            +
            path.string()
        );
    }


    std::vector<ResultRow>
        rows;


    while (
        std::getline(
            file,
            line
        )
    )
    {
        if (line.empty())
        {
            continue;
        }


        const auto fields =
            splitCsvLine(
                line
            );


        if (fields.size() < 6)
        {
            throw std::runtime_error(
                "Malformed result row in "
                +
                path.string()
            );
        }


        ResultRow row;


        row.nodeId =
            std::stoi(
                fields[0]
            );


        row.nodeName =
            fields[1];


        row.monitor =
            fields[2];


        row.unit =
            fields[3];


        row.lower =
            std::stoi(
                fields[4]
            );


        row.upper =
            std::stoi(
                fields[5]
            );


        rows.push_back(
            std::move(
                row
            )
        );
    }


    return rows;
}


template<typename Key>
std::vector<
    std::pair<
        Key,
        std::size_t
    >
>
rankFrequency(
    const std::map<
        Key,
        std::size_t
    >& frequencies
)
{
    std::vector<
        std::pair<
            Key,
            std::size_t
        >
    >
        ranked(
            frequencies.begin(),
            frequencies.end()
        );


    std::sort(
        ranked.begin(),
        ranked.end(),

        [](
            const auto& first,
            const auto& second
        )
        {
            if (
                first.second
                !=
                second.second
            )
            {
                return
                    first.second
                    >
                    second.second;
            }


            return
                first.first
                <
                second.first;
        }
    );


    return ranked;
}

}


int main(
    int argc,
    char** argv
)
{
    try
    {
        if (argc < 3)
        {
            throw std::runtime_error(
                "Usage: hardware_config_statistics <result_dir> <prefix> [image_count]"
            );
        }


        const std::filesystem::path resultDirectory =
            argv[1];


        const std::string prefix =
            argv[2];


        const int imageCount =
            argc >= 4
            ?
            std::stoi(
                argv[3]
            )
            :
            10;


        if (imageCount <= 0)
        {
            throw std::runtime_error(
                "image_count must be greater than zero."
            );
        }


        std::vector<ResultRow>
            allRows;


        std::map<int, std::string>
            nodeNames;


        for (int imageIndex = 1;
             imageIndex <= imageCount;
             ++imageIndex)
        {
            const auto path =
                resultDirectory
                /
                (
                    prefix
                    +
                    "_joint_search_best_image_"
                    +
                    twoDigit(
                        imageIndex
                    )
                    +
                    ".csv"
                );


            if (
                !std::filesystem::exists(
                    path
                )
            )
            {
                throw std::runtime_error(
                    "Missing result CSV: "
                    +
                    path.string()
                );
            }


            const auto rows =
                loadResult(
                    path
                );


            std::set<int>
                seenNodes;


            for (const auto& row : rows)
            {
                if (
                    !seenNodes.insert(
                        row.nodeId
                    ).second
                )
                {
                    throw std::runtime_error(
                        "Duplicate node in one result CSV: "
                        +
                        path.string()
                    );
                }


                nodeNames[
                    row.nodeId
                ] =
                    row.nodeName;


                allRows.push_back(
                    row
                );
            }
        }


        std::map<int, std::size_t>
            nodeFrequency;


        std::map<NodeMonitorKey, std::size_t>
            monitorFrequency;


        std::map<NodeUnitKey, std::size_t>
            unitFrequency;


        std::map<TupleKey, std::size_t>
            tupleFrequency;


        std::map<TupleKey, IntervalStats>
            tupleIntervals;


        for (const auto& row : allRows)
        {
            ++nodeFrequency[
                row.nodeId
            ];


            ++monitorFrequency[
                {
                    row.nodeId,
                    row.monitor
                }
            ];


            ++unitFrequency[
                {
                    row.nodeId,
                    row.unit
                }
            ];


            const TupleKey key{
                row.nodeId,
                row.monitor,
                row.unit
            };


            ++tupleFrequency[
                key
            ];


            tupleIntervals[
                key
            ].add(
                row.lower,
                row.upper
            );
        }


        const auto rankedNodes =
            rankFrequency(
                nodeFrequency
            );


        const auto rankedTuples =
            rankFrequency(
                tupleFrequency
            );


        std::cout
            << prefix
            << " hardware configuration statistics\n"
            << "========================================\n"
            << "Images loaded: "
            << imageCount
            << "\n"
            << "Total selected node instances: "
            << allRows.size()
            << "\n\n"
            << "1. Node selection frequency\n"
            << "---------------------------\n"
            << std::left
            << std::setw(7)
            << "Rank"
            << std::setw(14)
            << "Node"
            << std::setw(10)
            << "Count"
            << "ImageRate\n"
            << std::fixed
            << std::setprecision(
                2
            );


        for (std::size_t index = 0;
             index < rankedNodes.size();
             ++index)
        {
            const int nodeId =
                rankedNodes[index].first;


            const std::string label =
                std::to_string(
                    nodeId
                )
                +
                "("
                +
                nodeNames[
                    nodeId
                ]
                +
                ")";


            std::cout
                << std::setw(7)
                << (index + 1)
                << std::setw(14)
                << label
                << std::setw(10)
                << rankedNodes[index].second
                << (
                    100.0
                    *
                    static_cast<double>(
                        rankedNodes[index].second
                    )
                    /
                    static_cast<double>(
                        imageCount
                    )
                )
                << "%\n";
        }


        std::cout
            << "\n2. Per-node monitor / unit modes\n"
            << "---------------------------------\n"
            << std::left
            << std::setw(14)
            << "Node"
            << std::setw(22)
            << "TopMonitor"
            << std::setw(14)
            << "MonitorCount"
            << std::setw(14)
            << "TopUnit"
            << "UnitCount\n";


        for (const auto& nodeItem : rankedNodes)
        {
            const int nodeId =
                nodeItem.first;


            std::string bestMonitor;

            std::size_t bestMonitorCount =
                0;


            std::string bestUnit;

            std::size_t bestUnitCount =
                0;


            for (const auto& item : monitorFrequency)
            {
                if (
                    item.first.first == nodeId
                    &&
                    item.second > bestMonitorCount
                )
                {
                    bestMonitor =
                        item.first.second;


                    bestMonitorCount =
                        item.second;
                }
            }


            for (const auto& item : unitFrequency)
            {
                if (
                    item.first.first == nodeId
                    &&
                    item.second > bestUnitCount
                )
                {
                    bestUnit =
                        item.first.second;


                    bestUnitCount =
                        item.second;
                }
            }


            const std::string label =
                std::to_string(
                    nodeId
                )
                +
                "("
                +
                nodeNames[
                    nodeId
                ]
                +
                ")";


            std::cout
                << std::setw(14)
                << label
                << std::setw(22)
                << bestMonitor
                << std::setw(14)
                << bestMonitorCount
                << std::setw(14)
                << bestUnit
                << bestUnitCount
                << "\n";
        }


        std::cout
            << "\n3. Full Node + Monitor + Unit frequency\n"
            << "---------------------------------------\n"
            << std::left
            << std::setw(7)
            << "Rank"
            << std::setw(14)
            << "Node"
            << std::setw(22)
            << "Monitor"
            << std::setw(10)
            << "Unit"
            << std::setw(8)
            << "Count"
            << std::setw(12)
            << "MeanCtr"
            << std::setw(12)
            << "CtrStd"
            << std::setw(12)
            << "MeanWidth"
            << "WidthStd\n";


        for (std::size_t index = 0;
             index < rankedTuples.size();
             ++index)
        {
            const auto& item =
                rankedTuples[index];


            const int nodeId =
                std::get<0>(
                    item.first
                );


            const auto& stats =
                tupleIntervals.at(
                    item.first
                );


            const std::string label =
                std::to_string(
                    nodeId
                )
                +
                "("
                +
                nodeNames[
                    nodeId
                ]
                +
                ")";


            std::cout
                << std::setw(7)
                << (index + 1)
                << std::setw(14)
                << label
                << std::setw(22)
                << std::get<1>(
                    item.first
                )
                << std::setw(10)
                << std::get<2>(
                    item.first
                )
                << std::setw(8)
                << item.second
                << std::setw(12)
                << stats.meanCenter()
                << std::setw(12)
                << stats.centerStd()
                << std::setw(12)
                << stats.meanWidth()
                << stats.widthStd()
                << "\n";
        }


        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr
            << "Hardware configuration statistics failed: "
            << exception.what()
            << "\n";


        return 1;
    }
}
