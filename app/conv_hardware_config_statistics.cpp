#include "applications/conv.hpp"

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

    double lowerSum = 0.0;
    double upperSum = 0.0;
    double centerSum = 0.0;
    double centerSquareSum = 0.0;
    double widthSum = 0.0;
    double widthSquareSum = 0.0;

    int minimumLower = 0;
    int maximumUpper = 0;

    bool initialized = false;


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


        if (!initialized)
        {
            minimumLower =
                lower;

            maximumUpper =
                upper;

            initialized =
                true;
        }
        else
        {
            minimumLower =
                std::min(
                    minimumLower,
                    lower
                );


            maximumUpper =
                std::max(
                    maximumUpper,
                    upper
                );
        }


        ++count;

        lowerSum +=
            lower;

        upperSum +=
            upper;

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


    double meanLower() const
    {
        return
            lowerSum
            /
            static_cast<double>(
                count
            );
    }


    double meanUpper() const
    {
        return
            upperSum
            /
            static_cast<double>(
                count
            );
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
        if (count <= 1)
        {
            return 0.0;
        }


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
        if (count <= 1)
        {
            return 0.0;
        }


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
    int imageIndex = 0;

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


std::vector<ResultRow> loadImageResult(
    const std::filesystem::path& path,
    int imageIndex
)
{
    std::ifstream file(
        path
    );


    if (!file.is_open())
    {
        throw std::runtime_error(
            "Unable to open Conv best-result CSV: "
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
            "Conv best-result CSV is empty: "
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
                "Malformed Conv best-result CSV row in "
                +
                path.string()
            );
        }


        ResultRow row;


        row.imageIndex =
            imageIndex;


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


std::string nodeLabel(
    int nodeId
)
{
    return
        std::to_string(
            nodeId
        )
        +
        "("
        +
        applications::ConvApplication::nodeName(
            nodeId
        )
        +
        ")";
}


template<typename Key>
std::vector<
    std::pair<
        Key,
        std::size_t
    >
>
sortFrequency(
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
        result(
            frequencies.begin(),
            frequencies.end()
        );


    std::sort(
        result.begin(),
        result.end(),

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


    return result;
}

}


int main(
    int argc,
    char** argv
)
{
    try
    {
        const std::filesystem::path resultDirectory =
            argc >= 2
            ?
            std::filesystem::path(
                argv[1]
            )
            :
            std::filesystem::path(
                "."
            );


        const std::filesystem::path outputDirectory =
            argc >= 3
            ?
            std::filesystem::path(
                argv[2]
            )
            :
            resultDirectory;


        std::filesystem::create_directories(
            outputDirectory
        );


        std::vector<ResultRow>
            allRows;


        std::size_t loadedImageCount =
            0;


        for (int imageIndex = 1;
             imageIndex <= 10;
             ++imageIndex)
        {
            const auto path =
                resultDirectory
                /
                (
                    "conv_joint_search_best_image_"
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
                    "Missing Conv result CSV: "
                    +
                    path.string()
                );
            }


            auto rows =
                loadImageResult(
                    path,
                    imageIndex
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
                        "Duplicate node in one Conv best-result CSV."
                    );
                }
            }


            allRows.insert(
                allRows.end(),
                rows.begin(),
                rows.end()
            );


            ++loadedImageCount;
        }


        if (loadedImageCount == 0)
        {
            throw std::runtime_error(
                "No Conv result CSV files were loaded."
            );
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
            tupleIntervalStats;


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


            const TupleKey tupleKey =
            {
                row.nodeId,
                row.monitor,
                row.unit
            };


            ++tupleFrequency[
                tupleKey
            ];


            tupleIntervalStats[
                tupleKey
            ].add(
                row.lower,
                row.upper
            );
        }


        const auto rankedNodes =
            sortFrequency(
                nodeFrequency
            );


        const auto rankedTuples =
            sortFrequency(
                tupleFrequency
            );


        std::cout
            << "Conv fixed-hardware configuration statistics\n"
            << "============================================\n"
            << "Input result directory: "
            << resultDirectory.string()
            << "\n"
            << "Images loaded: "
            << loadedImageCount
            << "\n"
            << "Total selected node instances: "
            << allRows.size()
            << "\n\n";


        std::cout
            << "1. Node selection frequency\n"
            << "---------------------------\n"
            << std::left
            << std::setw(7)
            << "Rank"
            << std::setw(12)
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
            const auto& item =
                rankedNodes[index];


            const double imageRate =
                100.0
                *
                static_cast<double>(
                    item.second
                )
                /
                static_cast<double>(
                    loadedImageCount
                );


            std::cout
                << std::left
                << std::setw(7)
                << (index + 1)
                << std::setw(12)
                << nodeLabel(
                    item.first
                )
                << std::setw(10)
                << item.second
                << imageRate
                << "%\n";
        }


        std::cout
            << "\n2. Per-node monitor / unit modes\n"
            << "---------------------------------\n"
            << std::left
            << std::setw(12)
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


            std::cout
                << std::left
                << std::setw(12)
                << nodeLabel(
                    nodeId
                )
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
            << std::setw(12)
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
                tupleIntervalStats.at(
                    item.first
                );


            std::cout
                << std::left
                << std::setw(7)
                << (index + 1)
                << std::setw(12)
                << nodeLabel(
                    nodeId
                )
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


        const auto nodeCsvPath =
            outputDirectory
            /
            "conv_hardware_node_frequency.csv";


        const auto tupleCsvPath =
            outputDirectory
            /
            "conv_hardware_tuple_frequency.csv";


        std::ofstream nodeCsv(
            nodeCsvPath
        );


        std::ofstream tupleCsv(
            tupleCsvPath
        );


        if (
            !nodeCsv.is_open()
            ||
            !tupleCsv.is_open()
        )
        {
            throw std::runtime_error(
                "Unable to create Conv hardware statistics CSV."
            );
        }


        nodeCsv
            << "rank,node_id,node_name,count,image_rate,"
            << "top_monitor,top_monitor_count,"
            << "top_unit,top_unit_count\n";


        for (std::size_t index = 0;
             index < rankedNodes.size();
             ++index)
        {
            const int nodeId =
                rankedNodes[index].first;


            const std::size_t nodeCount =
                rankedNodes[index].second;


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


            nodeCsv
                << (index + 1)
                << ","
                << nodeId
                << ","
                << applications::ConvApplication::nodeName(
                    nodeId
                )
                << ","
                << nodeCount
                << ","
                << (
                    static_cast<double>(
                        nodeCount
                    )
                    /
                    static_cast<double>(
                        loadedImageCount
                    )
                )
                << ","
                << bestMonitor
                << ","
                << bestMonitorCount
                << ","
                << bestUnit
                << ","
                << bestUnitCount
                << "\n";
        }


        tupleCsv
            << "rank,node_id,node_name,monitor,unit,count,image_rate,"
            << "mean_lower,mean_upper,mean_center,center_std,"
            << "mean_width,width_std,min_lower,max_upper\n";


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
                tupleIntervalStats.at(
                    item.first
                );


            tupleCsv
                << (index + 1)
                << ","
                << nodeId
                << ","
                << applications::ConvApplication::nodeName(
                    nodeId
                )
                << ","
                << std::get<1>(
                    item.first
                )
                << ","
                << std::get<2>(
                    item.first
                )
                << ","
                << item.second
                << ","
                << (
                    static_cast<double>(
                        item.second
                    )
                    /
                    static_cast<double>(
                        loadedImageCount
                    )
                )
                << ","
                << stats.meanLower()
                << ","
                << stats.meanUpper()
                << ","
                << stats.meanCenter()
                << ","
                << stats.centerStd()
                << ","
                << stats.meanWidth()
                << ","
                << stats.widthStd()
                << ","
                << stats.minimumLower
                << ","
                << stats.maximumUpper
                << "\n";
        }


        std::cout
            << "\nNode summary CSV: "
            << nodeCsvPath.string()
            << "\n"
            << "Tuple summary CSV: "
            << tupleCsvPath.string()
            << "\n";


        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr
            << "Conv hardware configuration statistics failed: "
            << exception.what()
            << "\n";


        return 1;
    }
}
