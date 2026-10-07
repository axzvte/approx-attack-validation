#include "analysis/dct_monitor_signal_analyzer.hpp"

#include "applications/dct.hpp"
#include "applications/dct8_fixed_graph.hpp"
#include "io/image_io.hpp"
#include "region/region_mask.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
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


std::string signalName(
    analysis::DctMonitorSignal signal
)
{
    switch (signal)
    {
        case analysis::DctMonitorSignal::Input1:
            return "input1";

        case analysis::DctMonitorSignal::Input2:
            return "input2";

        case analysis::DctMonitorSignal::BaselineOutput:
            return "5RP_output";
    }


    return "unknown";
}


std::string biasName(
    analysis::DctMonitorSignalBias bias
)
{
    return
        bias
            ==
            analysis::DctMonitorSignalBias::Roi
        ?
        "ROI"
        :
        "Non-ROI";
}


const analysis::DctMonitorSignalSummary&
findSummary(
    const analysis::DctMonitorSignalReport& report,
    int nodeId,
    analysis::DctMonitorSignal signal,
    analysis::DctMonitorSignalBias bias
)
{
    for (const auto& summary : report.summaries)
    {
        if (
            summary.nodeId == nodeId
            &&
            summary.signal == signal
            &&
            summary.bias == bias
        )
        {
            return summary;
        }
    }


    throw std::runtime_error(
        "Unable to find DCT monitor-signal summary."
    );
}


analysis::DctMonitorSignal bestSignal(
    const analysis::DctMonitorSignalReport& report,
    int nodeId,
    analysis::DctMonitorSignalBias bias
)
{
    const std::vector<analysis::DctMonitorSignal>
        signals =
    {
        analysis::DctMonitorSignal::Input1,
        analysis::DctMonitorSignal::Input2,
        analysis::DctMonitorSignal::BaselineOutput
    };


    double bestGap =
        -std::numeric_limits<double>::infinity();


    auto best =
        signals.front();


    for (const auto signal : signals)
    {
        const auto& summary =
            findSummary(
                report,
                nodeId,
                signal,
                bias
            );


        if (summary.meanGap > bestGap)
        {
            bestGap =
                summary.meanGap;


            best =
                signal;
        }
    }


    return best;
}


void writePerImageCsv(
    const std::filesystem::path& path,
    const analysis::DctMonitorSignalReport& report
)
{
    std::ofstream file(
        path
    );


    if (!file.is_open())
    {
        throw std::runtime_error(
            "Unable to open monitor-signal per-image CSV."
        );
    }


    file
        << std::setprecision(12)
        << "node_id,node_name,signal,bias,image_index,"
        << "lower,upper,roi_trigger_rate,non_roi_trigger_rate,gap\n";


    for (const auto& result : report.perImageResults)
    {
        file
            << result.nodeId
            << ","
            << applications::Dct8FixedGraph::nodeName(
                result.nodeId
            )
            << ","
            << signalName(
                result.signal
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
    const analysis::DctMonitorSignalReport& report
)
{
    std::ofstream file(
        path
    );


    if (!file.is_open())
    {
        throw std::runtime_error(
            "Unable to open monitor-signal summary CSV."
        );
    }


    file
        << std::setprecision(12)
        << "node_id,node_name,signal,bias,"
        << "mean_gap,std_gap,cv_gap,"
        << "mean_roi_trigger_rate,mean_non_roi_trigger_rate\n";


    for (const auto& summary : report.summaries)
    {
        file
            << summary.nodeId
            << ","
            << applications::Dct8FixedGraph::nodeName(
                summary.nodeId
            )
            << ","
            << signalName(
                summary.signal
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
                            twoDigit(
                                i
                            )
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


        applications::DctApplication application;


        // 当前节点敏感性筛选后保留的 12 个候选节点。
        const std::vector<int>
            candidateNodeIds =
        {
            6,
            7,
            0,
            5,
            4,
            20,
            26,
            2,
            1,
            27,
            28,
            21
        };


        const auto start =
            std::chrono::steady_clock::now();


        const auto report =
            analysis::
                DctMonitorSignalAnalyzer::
                    analyze(
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
            << "DCT monitor-signal comparison\n"
            << "=============================\n"
            << "Images: 10\n"
            << "Candidate nodes: 12\n"
            << "Signals: input1 / input2 / 5RP_output\n"
            << "Interval boundaries: exact observed integer values\n"
            << "Metric: maximum ROI/Non-ROI trigger-rate gap per image\n"
            << "Elapsed: "
            << std::fixed
            << std::setprecision(
                2
            )
            << elapsedSeconds
            << " s\n\n";


        std::cout
            << std::left
            << std::setw(6)
            << "Node"
            << std::setw(22)
            << "Name"
            << std::setw(12)
            << "I1_ROI"
            << std::setw(12)
            << "I1_NonROI"
            << std::setw(12)
            << "I2_ROI"
            << std::setw(12)
            << "I2_NonROI"
            << std::setw(12)
            << "Out_ROI"
            << std::setw(12)
            << "Out_NonROI"
            << std::setw(14)
            << "BestROI"
            << "BestNonROI"
            << "\n";


        std::cout
            << std::fixed
            << std::setprecision(
                4
            );


        for (const int nodeId : candidateNodeIds)
        {
            const auto& i1Roi =
                findSummary(
                    report,
                    nodeId,
                    analysis::DctMonitorSignal::Input1,
                    analysis::DctMonitorSignalBias::Roi
                );


            const auto& i1NonRoi =
                findSummary(
                    report,
                    nodeId,
                    analysis::DctMonitorSignal::Input1,
                    analysis::DctMonitorSignalBias::NonRoi
                );


            const auto& i2Roi =
                findSummary(
                    report,
                    nodeId,
                    analysis::DctMonitorSignal::Input2,
                    analysis::DctMonitorSignalBias::Roi
                );


            const auto& i2NonRoi =
                findSummary(
                    report,
                    nodeId,
                    analysis::DctMonitorSignal::Input2,
                    analysis::DctMonitorSignalBias::NonRoi
                );


            const auto& outRoi =
                findSummary(
                    report,
                    nodeId,
                    analysis::DctMonitorSignal::BaselineOutput,
                    analysis::DctMonitorSignalBias::Roi
                );


            const auto& outNonRoi =
                findSummary(
                    report,
                    nodeId,
                    analysis::DctMonitorSignal::BaselineOutput,
                    analysis::DctMonitorSignalBias::NonRoi
                );


            std::cout
                << std::left
                << std::setw(6)
                << nodeId
                << std::setw(22)
                << applications::Dct8FixedGraph::nodeName(
                    nodeId
                )
                << std::setw(12)
                << i1Roi.meanGap
                << std::setw(12)
                << i1NonRoi.meanGap
                << std::setw(12)
                << i2Roi.meanGap
                << std::setw(12)
                << i2NonRoi.meanGap
                << std::setw(12)
                << outRoi.meanGap
                << std::setw(12)
                << outNonRoi.meanGap
                << std::setw(14)
                << signalName(
                    bestSignal(
                        report,
                        nodeId,
                        analysis::DctMonitorSignalBias::Roi
                    )
                )
                << signalName(
                    bestSignal(
                        report,
                        nodeId,
                        analysis::DctMonitorSignalBias::NonRoi
                    )
                )
                << "\n";
        }


        const auto perImagePath =
            outputDirectory
            /
            "dct_monitor_signal_per_image.csv";


        const auto summaryPath =
            outputDirectory
            /
            "dct_monitor_signal_summary.csv";


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
            << "DCT monitor-signal comparison failed: "
            << exception.what()
            << "\n";


        return 1;
    }
}
