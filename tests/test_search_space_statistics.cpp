#include "analysis/search_space_statistics.hpp"

#include "core/add_node.hpp"
#include "core/add_sample.hpp"
#include "core/application.hpp"

#include <opencv2/imgcodecs.hpp>

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>


namespace
{

class FakeApplication
    : public core::Application
{
public:

    FakeApplication()
        :
        nodes_(
            {
                { 0, "N0" },
                { 1, "N1" }
            }
        )
    {
    }


    std::string name() const override
    {
        return "Fake";
    }


    const std::vector<core::AddNode>&
    addNodes() const override
    {
        return nodes_;
    }


    cv::Mat runExact(
        const cv::Mat& inputImage
    ) const override
    {
        return inputImage.clone();
    }


    cv::Mat runApprox(
        const cv::Mat& inputImage,
        const std::vector<core::AttackConfig>&
    ) const override
    {
        return inputImage.clone();
    }


    void collectExactAddSamples(
        const cv::Mat&,
        const cv::Mat&,
        std::vector<core::AddSample>& samples
    ) const override
    {
        collectBaselineSamples(
            samples
        );
    }


    void collectBaselineAddSamples(
        const cv::Mat&,
        const cv::Mat&,
        std::vector<core::AddSample>& samples
    ) const override
    {
        collectBaselineSamples(
            samples
        );
    }


private:

    static void collectBaselineSamples(
        std::vector<core::AddSample>& samples
    )
    {
        // Node 0:
        // input1 range [0,2]  -> 6 intervals
        // input2 range [10,11] -> 3 intervals
        samples.push_back(
            { 0, 0, 10, 0, 0.0 }
        );

        samples.push_back(
            { 0, 2, 11, 0, 0.0 }
        );


        // Node 1:
        // input1 range [-1,1] -> 6 intervals
        // input2 range [5,5]  -> 1 interval
        samples.push_back(
            { 1, -1, 5, 0, 0.0 }
        );

        samples.push_back(
            { 1, 1, 5, 0, 0.0 }
        );
    }


    std::vector<core::AddNode>
        nodes_;
};

}


int main()
{
    const std::filesystem::path
        directory =
            "test_search_space_statistics_data";


    std::filesystem::create_directories(
        directory
    );


    cv::Mat image(
        1,
        1,
        CV_8UC1,
        cv::Scalar(128)
    );


    const std::string maskPath =
        (
            directory
            /
            "mask.png"
        ).string();


    if (
        !cv::imwrite(
            maskPath,
            image
        )
    )
    {
        std::cerr
            << "Failed to write test mask.\n";

        return 1;
    }


    analysis::TwoStageDataset
        dataset;


    dataset.roiMaskPath =
        maskPath;


    for (int i = 0; i < 10; ++i)
    {
        const std::string stage1Path =
            (
                directory
                /
                (
                    "stage1_"
                    +
                    std::to_string(i)
                    +
                    ".png"
                )
            ).string();


        if (
            !cv::imwrite(
                stage1Path,
                image
            )
        )
        {
            std::cerr
                << "Failed to write Stage 1 image.\n";

            return 1;
        }


        dataset.stage1Images.push_back(
            {
                stage1Path
            }
        );


        dataset.stage2Images.push_back(
            {
                (
                    directory
                    /
                    (
                        "stage2_"
                        +
                        std::to_string(i)
                        +
                        ".png"
                    )
                ).string()
            }
        );
    }


    FakeApplication
        application;


    const std::vector<approximate::ApproxUnitId>
        attackUnits =
    {
        approximate::ApproxUnitId::Add12se5Z0,
        approximate::ApproxUnitId::Add12se5QT
    };


    const auto result =
        analysis::SearchSpaceStatistics::analyze(
            application,
            dataset,
            attackUnits,
            2
        );


    if (result.nodes.size() != 2)
    {
        std::cerr
            << "Expected 2 node statistics.\n";

        return 1;
    }


    // Node 0:
    // 2 units * (6 + 3) = 18
    if (
        result.nodes[0].candidateCount
        !=
        18
    )
    {
        std::cerr
            << "Unexpected Node 0 candidate count.\n";

        return 1;
    }


    // Node 1:
    // 2 units * (6 + 1) = 14
    if (
        result.nodes[1].candidateCount
        !=
        14
    )
    {
        std::cerr
            << "Unexpected Node 1 candidate count.\n";

        return 1;
    }


    if (
        result.byAttackNodeCount.size()
        !=
        2
    )
    {
        std::cerr
            << "Unexpected attack-node-count statistics size.\n";

        return 1;
    }


    // 1 node:
    // 18 + 14 = 32
    if (
        result.byAttackNodeCount[0].configurationCount
        !=
        32
    )
    {
        std::cerr
            << "Unexpected 1-node configuration count.\n";

        return 1;
    }


    // 2 nodes:
    // 18 * 14 = 252
    if (
        result.byAttackNodeCount[1].configurationCount
        !=
        252
    )
    {
        std::cerr
            << "Unexpected 2-node configuration count.\n";

        return 1;
    }


    if (
        result.totalConfigurations
        !=
        284
    )
    {
        std::cerr
            << "Unexpected total configuration count.\n";

        return 1;
    }


    std::filesystem::remove_all(
        directory
    );


    std::cout
        << "1-node configurations: "
        << result.byAttackNodeCount[0].configurationCount
        << "\n";


    std::cout
        << "2-node configurations: "
        << result.byAttackNodeCount[1].configurationCount
        << "\n";


    std::cout
        << "SearchSpaceStatistics test passed.\n";


    return 0;
}
