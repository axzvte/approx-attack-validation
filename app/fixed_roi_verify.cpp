#include "applications/conv.hpp"
#include "applications/dct.hpp"
#include "applications/sharpen.hpp"
#include "applications/sobel.hpp"

#include <opencv2/imgcodecs.hpp>
#include <type_traits>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
int integer(const char* value) {
    std::size_t end = 0;
    const std::string text(value);
    const int parsed = std::stoi(text, &end);
    if (end != text.size()) {
        throw std::runtime_error("Invalid integer: " + text);
    }
    return parsed;
}

void save(const std::filesystem::path& directory, const char* name, const cv::Mat& image) {
    if (!cv::imwrite((directory / name).string(), image)) {
        throw std::runtime_error(std::string("Cannot write ") + name);
    }
}

template <typename Application>
void verify(const cv::Mat& image, const cv::Rect& region, const std::filesystem::path& directory,
            int node, int bit, const std::vector<int>& replacements) {
    const Application original;
    auto baseline = original.baselineConfig();
    for (int index : replacements) {
        if (index < 0 || static_cast<std::size_t>(index) >= baseline.size() || baseline[index].exact) {
            throw std::runtime_error("Replacement must name an original approximate node once.");
        }
        baseline[index].exact = true;
    }
    const Application compensated(baseline);
    const auto reference = original.runExact(image);
    const auto untreated = original.runApprox(image, {});
    const auto noFlip = compensated.runApprox(image, {});
    cv::Mat processed;
    if (node == -1) {
        if (bit < 0 || bit > 7) {
            throw std::runtime_error("Output-pixel bit must be between 0 and 7.");
        }
        processed = noFlip.clone();
        for (int y = 0; y < image.rows; ++y) {
            for (int x = 0; x < image.cols; ++x) {
                if (region.contains(cv::Point(x, y))) {
                    processed.at<unsigned char>(y, x) ^= 1u << bit;
                }
            }
        }
    } else {
        if constexpr (std::is_same_v<Application, applications::DctApplication>) {
            if (node != 10)
                throw std::runtime_error("Fixed DCT configuration must flip the terminal DC coefficient.");
            processed = compensated.runFixedDcRegion(image, region, bit);
        } else {
            if (node < 0 || static_cast<std::size_t>(node) >= baseline.size() ||
                original.baselineConfig()[node].exact) {
                throw std::runtime_error("Flip must name an original approximate node.");
            }
            const core::AttackConfig flip{
                node, approximate::ApproxUnitId::Add12se5RP, core::MonitorSignal::BaselineOutput,
                std::numeric_limits<int>::min(), std::numeric_limits<int>::max(),
                core::AttackPayload::BitFlip, bit
            };
            cv::Mat mask(image.size(), CV_8UC1, cv::Scalar(0));
            mask(region).setTo(255);
            processed = compensated.runFixedRegion(image, mask, {flip});
        }
    }
    save(directory, "exact.png", reference);
    save(directory, "baseline.png", untreated);
    save(directory, "compensated.png", noFlip);
    save(directory, "processed.png", processed);
}
}

int main(int argc, char** argv) {
    try {
        if (argc < 10) {
            throw std::runtime_error(
                "Usage: fixed_roi_verify APP IMAGE OUT LEFT TOP RIGHT BOTTOM NODE BIT [EXACT_NODE ...]");
        }
        const std::string app(argv[1]);
        const auto image = cv::imread(argv[2], cv::IMREAD_GRAYSCALE | cv::IMREAD_IGNORE_ORIENTATION);
        if (image.empty()) {
            throw std::runtime_error("Cannot read input image.");
        }
        const int left = integer(argv[4]), top = integer(argv[5]);
        const int right = integer(argv[6]), bottom = integer(argv[7]);
        if (left < 0 || top < 0 || right > image.cols || bottom > image.rows ||
            left >= right || top >= bottom) {
            throw std::runtime_error("Invalid nonempty half-open rectangle.");
        }
        const cv::Rect region(left, top, right-left, bottom-top);
        const int node = integer(argv[8]), bit = integer(argv[9]);
        std::vector<int> replacements;
        for (int i = 10; i < argc; ++i) {
            replacements.push_back(integer(argv[i]));
        }
        if (app == "sharpen") {
            verify<applications::SharpenApplication>(image, region, argv[3], node, bit, replacements);
        } else if (app == "conv") {
            verify<applications::ConvApplication>(image, region, argv[3], node, bit, replacements);
        } else if (app == "sobel") {
            verify<applications::SobelApplication>(image, region, argv[3], node, bit, replacements);
        } else if (app == "dct") {
            verify<applications::DctApplication>(image, region, argv[3], node, bit, replacements);
        } else {
            throw std::runtime_error("Unknown application: " + app);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
