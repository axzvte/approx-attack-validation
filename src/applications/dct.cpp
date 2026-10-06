#include "applications/dct.hpp"
#include "applications/dct8_fixed_graph.hpp"

#include <opencv2/imgproc.hpp>

#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>


namespace applications
{

namespace
{

void validateInput(
    const cv::Mat& inputImage
)
{
    if (inputImage.empty())
    {
        throw std::runtime_error(
            "DCT input image is empty."
        );
    }


    if (inputImage.type() != CV_8UC1)
    {
        throw std::runtime_error(
            "DCT requires an 8-bit grayscale image."
        );
    }
}


void validateRoiMask(
    const cv::Mat& inputImage,
    const cv::Mat& roiMask
)
{
    if (roiMask.empty())
    {
        throw std::runtime_error(
            "ROI mask is empty."
        );
    }


    if (roiMask.type() != CV_8UC1)
    {
        throw std::runtime_error(
            "ROI mask must be CV_8UC1."
        );
    }


    if (
        roiMask.rows != inputImage.rows
        ||
        roiMask.cols != inputImage.cols
    )
    {
        throw std::runtime_error(
            "ROI mask size does not match input image."
        );
    }
}


// =========================================================
// 图像补齐：复制边缘像素
// =========================================================

cv::Mat createPaddedImage(
    const cv::Mat& image
)
{
    const int paddedRows =
        ((image.rows + 7) / 8) * 8;


    const int paddedCols =
        ((image.cols + 7) / 8) * 8;


    cv::Mat paddedImage;


    cv::copyMakeBorder(
        image,
        paddedImage,

        0,
        paddedRows - image.rows,

        0,
        paddedCols - image.cols,

        cv::BORDER_REPLICATE
    );


    return paddedImage;
}


// =========================================================
// ROI mask 补齐：补0
//
// 补出来的像素不属于真实图像，因此不算ROI。
// =========================================================

cv::Mat createPaddedMask(
    const cv::Mat& mask
)
{
    const int paddedRows =
        ((mask.rows + 7) / 8) * 8;


    const int paddedCols =
        ((mask.cols + 7) / 8) * 8;


    cv::Mat paddedMask;


    cv::copyMakeBorder(
        mask,
        paddedMask,

        0,
        paddedRows - mask.rows,

        0,
        paddedCols - mask.cols,

        cv::BORDER_CONSTANT,
        cv::Scalar(0)
    );


    return paddedMask;
}


// =========================================================
// 一个8×8 block中的ROI占比
//
// roiWeight = ROI像素数 / 64
// =========================================================

double calculateBlockRoiWeight(
    const cv::Mat& paddedMask,
    int blockRow,
    int blockCol
)
{
    int roiPixelCount = 0;


    for (int row = 0;
         row < 8;
         ++row)
    {
        for (int col = 0;
             col < 8;
             ++col)
        {
            if (
                paddedMask.at<unsigned char>(
                    blockRow + row,
                    blockCol + col
                )
                != 0
            )
            {
                ++roiPixelCount;
            }
        }
    }


    return
        static_cast<double>(
            roiPixelCount
        )
        /
        64.0;
}


// =========================================================
// 将一次8点DCT中的32个ADD记录成通用样本
// =========================================================

void appendTraceSamples(
    const Dct8FixedGraph::Trace& trace,
    double roiWeight,
    std::vector<core::AddSample>& samples
)
{
    for (int nodeId = 0;
         nodeId < Dct8FixedGraph::kAddNodeCount;
         ++nodeId)
    {
        const auto& item =
            trace[nodeId];


        samples.push_back(
            core::AddSample{
                item.nodeId,
                item.input1,
                item.input2,
                item.output,
                roiWeight
            }
        );
    }
}


// =========================================================
// 运行一次8点DCT
//
// configs == nullptr:
// 精确运行
//
// configs != nullptr:
// 近似运行
//
// samples != nullptr:
// 记录32个ADD中间数据
// =========================================================

Dct8FixedGraph::Vector runVector(
    const Dct8FixedGraph& graph,
    const Dct8FixedGraph::Vector& input,
    const std::vector<core::AttackConfig>* configs,
    double roiWeight,
    std::vector<core::AddSample>* samples
)
{
    Dct8FixedGraph::Trace trace{};


    Dct8FixedGraph::Trace* tracePointer =
        samples == nullptr
        ?
        nullptr
        :
        &trace;


    Dct8FixedGraph::Vector output{};


    if (configs == nullptr)
    {
        output =
            graph.runExact(
                input,
                tracePointer
            );
    }
    else
    {
        output =
            graph.runApprox(
                input,
                *configs,
                tracePointer
            );
    }


    if (samples != nullptr)
    {
        appendTraceSamples(
            trace,
            roiWeight,
            *samples
        );
    }


    return output;
}


// =========================================================
// 一个8×8 block的二维DCT
//
// 8次行DCT + 8次列DCT
// =========================================================

cv::Mat runForwardBlock(
    const cv::Mat& paddedImage,
    int blockRow,
    int blockCol,
    const std::vector<core::AttackConfig>* configs,
    double roiWeight,
    std::vector<core::AddSample>* samples
)
{
    Dct8FixedGraph graph;


    std::array<
        std::array<std::int32_t, 8>,
        8
    > rowTransformed{};


    // =====================================================
    // 行 DCT
    // =====================================================

    for (int row = 0;
         row < 8;
         ++row)
    {
        Dct8FixedGraph::Vector inputVector{};


        for (int col = 0;
             col < 8;
             ++col)
        {
            const int pixel =
                paddedImage.at<unsigned char>(
                    blockRow + row,
                    blockCol + col
                );


            inputVector[col] =
                pixel - 128;
        }


        const auto outputVector =
            runVector(
                graph,
                inputVector,
                configs,
                roiWeight,
                samples
            );


        for (int k = 0;
             k < 8;
             ++k)
        {
            rowTransformed[row][k] =
                outputVector[k];
        }
    }


    // =====================================================
    // 列 DCT
    // =====================================================

    cv::Mat dctBlock(
        8,
        8,
        CV_64F
    );


    for (int col = 0;
         col < 8;
         ++col)
    {
        Dct8FixedGraph::Vector inputVector{};


        for (int row = 0;
             row < 8;
             ++row)
        {
            inputVector[row] =
                rowTransformed[row][col];
        }


        const auto outputVector =
            runVector(
                graph,
                inputVector,
                configs,
                roiWeight,
                samples
            );


        for (int k = 0;
             k < 8;
             ++k)
        {
            dctBlock.at<double>(
                k,
                col
            ) =
                static_cast<double>(
                    outputVector[k]
                );
        }
    }


    return dctBlock;
}


// =========================================================
// 完整图像：DCT -> 精确IDCT -> 重建
// =========================================================

cv::Mat runDctImage(
    const cv::Mat& inputImage,
    const std::vector<core::AttackConfig>* configs
)
{
    validateInput(
        inputImage
    );


    const cv::Mat paddedImage =
        createPaddedImage(
            inputImage
        );


    cv::Mat reconstructed =
        cv::Mat::zeros(
            paddedImage.size(),
            CV_64F
        );


    for (int blockRow = 0;
         blockRow < paddedImage.rows;
         blockRow += 8)
    {
        for (int blockCol = 0;
             blockCol < paddedImage.cols;
             blockCol += 8)
        {
            const cv::Mat dctBlock =
                runForwardBlock(
                    paddedImage,
                    blockRow,
                    blockCol,
                    configs,

                    0.0,
                    nullptr
                );


            cv::Mat reconstructedBlock;


            cv::idct(
                dctBlock,
                reconstructedBlock
            );


            for (int row = 0;
                 row < 8;
                 ++row)
            {
                for (int col = 0;
                     col < 8;
                     ++col)
                {
                    reconstructed.at<double>(
                        blockRow + row,
                        blockCol + col
                    ) =
                        reconstructedBlock.at<double>(
                            row,
                            col
                        )
                        +
                        128.0;
                }
            }
        }
    }


    cv::Mat reconstructed8Bit;


    reconstructed.convertTo(
        reconstructed8Bit,
        CV_8UC1
    );


    const cv::Rect originalRegion(
        0,
        0,
        inputImage.cols,
        inputImage.rows
    );


    return
        reconstructed8Bit(
            originalRegion
        ).clone();
}

}


// =========================================================
// DctApplication
// =========================================================

DctApplication::DctApplication()
{
    addNodes_.reserve(
        Dct8FixedGraph::kAddNodeCount
    );


    for (int nodeId = 0;
         nodeId < Dct8FixedGraph::kAddNodeCount;
         ++nodeId)
    {
        addNodes_.push_back(
            core::AddNode{
                nodeId,
                Dct8FixedGraph::nodeName(nodeId)
            }
        );
    }
}


std::string
DctApplication::name() const
{
    return "DCT";
}


const std::vector<core::AddNode>&
DctApplication::addNodes() const
{
    return addNodes_;
}


cv::Mat DctApplication::runExact(
    const cv::Mat& inputImage
) const
{
    return
        runDctImage(
            inputImage,
            nullptr
        );
}


cv::Mat DctApplication::runApprox(
    const cv::Mat& inputImage,
    const std::vector<core::AttackConfig>& configs
) const
{
    return
        runDctImage(
            inputImage,
            &configs
        );
}


// =========================================================
// 精确运行中间数据采集
// =========================================================

void DctApplication::collectExactAddSamples(
    const cv::Mat& inputImage,
    const cv::Mat& roiMask,
    std::vector<core::AddSample>& samples
) const
{
    validateInput(
        inputImage
    );


    validateRoiMask(
        inputImage,
        roiMask
    );


    const cv::Mat paddedImage =
        createPaddedImage(
            inputImage
        );


    const cv::Mat paddedMask =
        createPaddedMask(
            roiMask
        );


    const std::size_t blockCount =
        static_cast<std::size_t>(
            paddedImage.rows / 8
        )
        *
        static_cast<std::size_t>(
            paddedImage.cols / 8
        );


    samples.reserve(
        samples.size()
        +
        blockCount
        *
        16
        *
        Dct8FixedGraph::kAddNodeCount
    );


    for (int blockRow = 0;
         blockRow < paddedImage.rows;
         blockRow += 8)
    {
        for (int blockCol = 0;
             blockCol < paddedImage.cols;
             blockCol += 8)
        {
            const double roiWeight =
                calculateBlockRoiWeight(
                    paddedMask,
                    blockRow,
                    blockCol
                );


            runForwardBlock(
                paddedImage,
                blockRow,
                blockCol,

                nullptr,

                roiWeight,
                &samples
            );
        }
    }
}


// =========================================================
// 正常近似 Baseline 运行中间数据采集
//
// 空 AttackConfig 表示所有节点只使用各自 Baseline。
// 当前默认整套 Baseline 为 5RP。
// =========================================================

void DctApplication::collectBaselineAddSamples(
    const cv::Mat& inputImage,
    const cv::Mat& roiMask,
    std::vector<core::AddSample>& samples
) const
{
    validateInput(
        inputImage
    );


    validateRoiMask(
        inputImage,
        roiMask
    );


    const cv::Mat paddedImage =
        createPaddedImage(
            inputImage
        );


    const cv::Mat paddedMask =
        createPaddedMask(
            roiMask
        );


    const std::size_t blockCount =
        static_cast<std::size_t>(
            paddedImage.rows / 8
        )
        *
        static_cast<std::size_t>(
            paddedImage.cols / 8
        );


    samples.reserve(
        samples.size()
        +
        blockCount
        *
        16
        *
        Dct8FixedGraph::kAddNodeCount
    );


    const std::vector<core::AttackConfig>
        emptyConfigs;


    for (int blockRow = 0;
         blockRow < paddedImage.rows;
         blockRow += 8)
    {
        for (int blockCol = 0;
             blockCol < paddedImage.cols;
             blockCol += 8)
        {
            const double roiWeight =
                calculateBlockRoiWeight(
                    paddedMask,
                    blockRow,
                    blockCol
                );


            runForwardBlock(
                paddedImage,
                blockRow,
                blockCol,

                &emptyConfigs,

                roiWeight,
                &samples
            );
        }
    }
}


// =========================================================
// 当前 AttackConfig 下的近似运行中间数据采集
//
// 与 Baseline 采样不同，这里真实执行传入的 configs。
// 因此上游节点的近似输出变化会继续传播到后级输入；
// 对 DCT 中重复复用的静态节点，后续动态调用也会反映
// 当前配置已经造成的输入变化。
// =========================================================

void DctApplication::collectConfiguredAddSamples(
    const cv::Mat& inputImage,
    const cv::Mat& roiMask,
    const std::vector<core::AttackConfig>& configs,
    std::vector<core::AddSample>& samples
) const
{
    validateInput(
        inputImage
    );


    validateRoiMask(
        inputImage,
        roiMask
    );


    const cv::Mat paddedImage =
        createPaddedImage(
            inputImage
        );


    const cv::Mat paddedMask =
        createPaddedMask(
            roiMask
        );


    const std::size_t blockCount =
        static_cast<std::size_t>(
            paddedImage.rows / 8
        )
        *
        static_cast<std::size_t>(
            paddedImage.cols / 8
        );


    samples.reserve(
        samples.size()
        +
        blockCount
        *
        16
        *
        Dct8FixedGraph::kAddNodeCount
    );


    for (int blockRow = 0;
         blockRow < paddedImage.rows;
         blockRow += 8)
    {
        for (int blockCol = 0;
             blockCol < paddedImage.cols;
             blockCol += 8)
        {
            const double roiWeight =
                calculateBlockRoiWeight(
                    paddedMask,
                    blockRow,
                    blockCol
                );


            runForwardBlock(
                paddedImage,
                blockRow,
                blockCol,

                &configs,

                roiWeight,
                &samples
            );
        }
    }
}

}