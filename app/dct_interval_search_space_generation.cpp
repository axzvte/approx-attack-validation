#include "analysis/dct_interval_search_space_generator.hpp"

#include "applications/dct.hpp"
#include "applications/dct8_fixed_graph.hpp"
#include "io/image_io.hpp"
#include "region/region_mask.hpp"

#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>


namespace
{

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


std::string monitorName(
    core::MonitorInput monitorInput
)
{
    return
        monitorInput
            ==
            core::MonitorInput::Input1
        ?
        "input1"
        :
        "input2";
}

}


int main(
    int argc,
    char** argv
)
{
    try
    {
        const std::filesystem::path dataRoot =
            (
                argc >= 2
            )
            ?
            std::filesystem::path(
                argv[1]
            )
            :
            std::filesystem::path(
                "data"
            );


        const int imageIndex =
            (
                argc >= 3
            )
            ?
            std::stoi(
                argv[2]
            )
            :
            1;


        if (
            imageIndex < 1
            ||
            imageIndex > 10
        )
        {
            throw std::runtime_error(
                "Stage 1 image index must be in [1, 10]."
            );
        }


        const cv::Mat inputImage =
            image_io::loadGrayImage(
                (
                    dataRoot
                    /
                    "stage1"
                    /
                    "input"
                    /
                    (
                        "image_"
                        +
                        twoDigit(
                            imageIndex
                        )
                        +
                        ".jpg"
                    )
                ).string()
            );


        const cv::Mat sharedRoiMask =
            image_io::loadGrayImage(
                (
                    dataRoot
                    /
                    "mask"
                    /
                    "roi_mask.jpg"
                ).string()
            );


        const cv::Mat roiMask =
            region_mask::resizeMaskToImage(
                sharedRoiMask,
                inputImage
            );


        applications::DctApplication
            application;


        // 当前已经完成节点筛选和 monitor-input 分析后，
        // 使用的 12 个候选节点。
        //
        // 这里只作为当前 DCT 实验的检查程序；
        // 生成器本身并没有把这些节点写死。
        const std::vector<
            analysis::DctIntervalSearchTarget
        >
            targets =
        {
            {
                6,
                core::MonitorInput::Input1
            },
            {
                7,
                core::MonitorInput::Input1
            },
            {
                0,
                core::MonitorInput::Input1
            },
            {
                5,
                core::MonitorInput::Input1
            },
            {
                4,
                core::MonitorInput::Input1
            },
            {
                20,
                core::MonitorInput::Input2
            },
            {
                26,
                core::MonitorInput::Input2
            },
            {
                2,
                core::MonitorInput::Input1
            },
            {
                1,
                core::MonitorInput::Input1
            },
            {
                27,
                core::MonitorInput::Input2
            },
            {
                28,
                core::MonitorInput::Input2
            },
            {
                21,
                core::MonitorInput::Input2
            }
        };


        const auto searchSpaces =
            analysis::
                DctIntervalSearchSpaceGenerator::
                    generate(
                        application,
                        inputImage,
                        roiMask,
                        targets
                    );


        std::cout
            << "DCT interval search-space generation\n"
            << "====================================\n"
            << "Stage 1 image: image_"
            << twoDigit(
                imageIndex
            )
            << ".jpg\n"
            << "Baseline: 5RP\n"
            << "Interval rule: boundaries use observed monitor values only\n"
            << "Effect filtering: disabled (Module 2 responsibility)\n\n";


        std::cout
            << std::left
            << std::setw(7)
            << "Node"
            << std::setw(22)
            << "Name"
            << std::setw(10)
            << "Monitor"
            << std::setw(14)
            << "Samples"
            << std::setw(16)
            << "UniqueValues"
            << std::setw(12)
            << "Minimum"
            << std::setw(12)
            << "Maximum"
            << "Intervals"
            << "\n";


        for (const auto& searchSpace : searchSpaces)
        {
            if (searchSpace.observedValues.empty())
            {
                throw std::runtime_error(
                    "Generated DCT interval search space has no observed values."
                );
            }


            std::cout
                << std::left
                << std::setw(7)
                << searchSpace.nodeId
                << std::setw(22)
                << applications::Dct8FixedGraph::nodeName(
                    searchSpace.nodeId
                )
                << std::setw(10)
                << monitorName(
                    searchSpace.monitorInput
                )
                << std::setw(14)
                << searchSpace.sampleCount
                << std::setw(16)
                << searchSpace.observedValues.size()
                << std::setw(12)
                << searchSpace.observedValues.front()
                << std::setw(12)
                << searchSpace.observedValues.back()
                << searchSpace.intervals.size()
                << "\n";
        }


        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr
            << "DCT interval search-space generation failed: "
            << exception.what()
            << "\n";


        return 1;
    }
}
