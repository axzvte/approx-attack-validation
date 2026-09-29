#include "analysis/dct_monitor_input_analyzer.hpp"

#include "applications/dct.hpp"
#include "applications/dct8_fixed_graph.hpp"
#include "io/image_io.hpp"
#include "region/region_mask.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
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
    core::MonitorInput input
)
{
    return
        input
            ==
            core::MonitorInput::Input1
        ?
        "input1"
        :
        "input2";
}


std::string biasName(
    analysis::RegionBias bias
)
{
    return
        bias
            ==
            analysis::RegionBias::Roi
        ?
        "ROI"
        :
        "Non-ROI";
}


void writePerImageCsv(
    const std::filesystem::path& path,
    const analysis::DctMonitorInputReport& report
)
{
    std::ofstream file(
        path
    );


    if (!file.is_open())
    {
        throw std::runtime_error(
            "Unable to open DCT monitor-input per-image CSV."
        );
    }


    file
        << "node_id,node_name,monitor_input,bias,image_index,"
        << "lower,upper,roi_trigger_rate,non_roi_trigger_rate,gap\n";


    file
        << std::setprecision(12);


    for (const auto& result : report.perImageResults)
    {
        file
            << result.nodeId
            << ","
            << applications::Dct8FixedGraph::nodeName(
                result.nodeId
            )
            << ","
            << monitorName(
                result.monitorInput
            )
            << ","
            << biasName(
                result.bias
            )
            << ","
            << (result.imageIndex + 1)
            << ","
            << result.lower
            << ","
            << result.upper
            << ","
            << result.roiTriggerRate
            << ","
            << result.nonRoiTriggerRate
            << ","
            << result.gap
            << "\n";
    }
}


void writeSummaryCsv(
    const std::filesystem::path& path,
    const analysis::DctMonitorInputReport& report
)
{
    std::ofstream file(
        path
    );


    if (!file.is_open())
    {
        throw std::runtime_error(
            "Unable to open DCT monitor-input summary CSV."
        );
    }


    file
        << "node_id,node_name,monitor_input,bias,"
        << "mean_gap,std_gap,cv_gap,"
        << "mean_roi_trigger_rate,mean_non_roi_trigger_rate\n";


    file
        << std::setprecision(12);


    for (const auto& summary : report.summaries)
    {
        file
            << summary.nodeId
            << ","
            << applications::Dct8FixedGraph::nodeName(
                summary.nodeId
            )
            << ","
            << monitorName(
                summary.monitorInput
            )
            << ","
            << biasName(
                summary.bias
            )
            << ","
            << summary.meanGap
            << ","
            << summary.stdGap
            << ","
            << summary.cvGap
            << ","
            << summary.meanRoiTriggerRate
            << ","
            << summary.meanNonRoiTriggerRate
            << "\n";
    }
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


        const std::filesystem::path outputDirectory =
            (
                argc >= 3
            )
            ?
            std::filesystem::path(
                argv[2]
            )
            :
            std::filesystem::path(
                "."
            );


        std::filesystem::create_directories(
            outputDirectory
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


        std::vector<cv::Mat>
            inputImages;


        std::vector<cv::Mat>
            roiMasks;


        for (int i = 1;
             i <= 10;
             ++i)
        {
            cv::Mat inputImage =
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
                            twoDigit(i)
                            +
                            ".jpg"
                        )
                    ).string()
                );


            cv::Mat roiMask =
                region_mask::resizeMaskToImage(
                    sharedRoiMask,
                    inputImage
                );


            inputImages.push_back(
                std::move(
                    inputImage
                )
            );


            roiMasks.push_back(
                std::move(
                    roiMask
                )
            );
        }


        applications::DctApplication
            application;


        // 当前先对全部 32 个节点分析。
        // 后续完成“敏感性 + 跨图片稳定性”筛选后，
        // 这里直接替换为保留下来的节点 ID 即可。
        std::vector<int>
            candidateNodeIds;


        for (const auto& node : application.addNodes())
        {
            candidateNodeIds.push_back(
                node.id
            );
        }


        const auto start =
            std::chrono::steady_clock::now();


        const auto report =
            analysis::DctMonitorInputAnalyzer::analyze(
                application,
                inputImages,
                roiMasks,
                candidateNodeIds
            );


        const double elapsedSeconds =
            std::chrono::duration<double>(
                std::chrono::steady_clock::now()
                -
                start
            ).count();


        std::cout
            << "DCT monitor-input regional separability\n"
            << "======================================\n"
            << "Images: "
            << inputImages.size()
            << "\n"
            << "Nodes: "
            << candidateNodeIds.size()
            << "\n"
            << "Interval boundaries: exact observed integer values\n"
            << "Elapsed: "
            << std::fixed
            << std::setprecision(2)
            << elapsedSeconds
            << " s\n\n";


        std::cout
            << std::left
            << std::setw(6)
            << "Node"
            << std::setw(22)
            << "Name"
            << std::setw(9)
            << "Input"
            << std::setw(10)
            << "Bias"
            << std::setw(12)
            << "MeanGap"
            << std::setw(12)
            << "Std"
            << std::setw(12)
            << "CV"
            << std::setw(12)
            << "ROITrig"
            << "NonROITrig"
            << "\n";


        std::cout
            << std::fixed
            << std::setprecision(4);


        for (const auto& summary : report.summaries)
        {
            std::cout
                << std::left
                << std::setw(6)
                << summary.nodeId
                << std::setw(22)
                << applications::Dct8FixedGraph::nodeName(
                    summary.nodeId
                )
                << std::setw(9)
                << monitorName(
                    summary.monitorInput
                )
                << std::setw(10)
                << biasName(
                    summary.bias
                )
                << std::setw(12)
                << summary.meanGap
                << std::setw(12)
                << summary.stdGap
                << std::setw(12)
                << summary.cvGap
                << std::setw(12)
                << summary.meanRoiTriggerRate
                << summary.meanNonRoiTriggerRate
                << "\n";
        }


        const std::filesystem::path perImagePath =
            outputDirectory
            /
            "dct_monitor_input_per_image.csv";


        const std::filesystem::path summaryPath =
            outputDirectory
            /
            "dct_monitor_input_summary.csv";


        writePerImageCsv(
            perImagePath,
            report
        );


        writeSummaryCsv(
            summaryPath,
            report
        );


        std::cout
            << "\nPer-image CSV: "
            << perImagePath.string()
            << "\n"
            << "Summary CSV: "
            << summaryPath.string()
            << "\n";


        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr
            << "DCT monitor-input analysis failed: "
            << exception.what()
            << "\n";


        return 1;
    }
}
