#include "applications/conv.hpp"
#include "applications/dct.hpp"
#include "applications/sharpen.hpp"
#include "applications/sobel.hpp"
#include "circuit_profile.hpp"
#include "circuit_ir.hpp"

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
            int node, int bit, const std::vector<int>& replacements,
            const circuit_profile::Profile* profile = nullptr) {
    auto initial = Application{}.baselineConfig();
    if (profile) {
        for (std::size_t i=0; i<initial.size(); ++i) {
            initial[i].exact = profile->baseline.at(i).exact;
            initial[i].unit = profile->baseline.at(i).unit;
        }
    }
    const Application original(initial);
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
                (!profile && original.baselineConfig()[node].exact)) {
                throw std::runtime_error("Flip must name a valid arithmetic node.");
            }
            const core::AttackConfig flip{
                node, original.baselineConfig()[node].unit, core::MonitorSignal::BaselineOutput,
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

void verifyGraph(const cv::Mat& image, const cv::Rect& region, const std::filesystem::path& directory,
                 int node, int bit, const std::vector<int>& replacements,
                 const circuit_profile::Profile& profile) {
    auto compensated = profile.baseline;
    for (const int replacement : replacements) {
        if (replacement < 0 || static_cast<size_t>(replacement) >= compensated.size() || compensated[replacement].exact)
            throw std::runtime_error("Replacement must name an original approximate node once.");
        compensated[replacement].exact = true;
    }
    if (node < -1 || (node >= 0 && static_cast<size_t>(node) >= compensated.size()))
        throw std::runtime_error("Flip must name a valid arithmetic node.");
    if (node == -1) {
        if (bit < 0 || bit > 7) throw std::runtime_error("Output-pixel bit must be between 0 and 7.");
    } else core::flipNonSignOutputBit(0,bit);
    auto allExact = profile.baseline;
    for (auto& item : allExact) item.exact = true;
    const auto reference = profile.run(image,allExact);
    const auto baseline = profile.run(image,profile.baseline);
    const auto noFlip = profile.run(image,compensated);
    auto processed = node == -1 ? noFlip.clone() : profile.run(image,compensated,region,node,bit);
    if (node == -1) for (int y=region.y; y<region.y+region.height; ++y)
        for (int x=region.x; x<region.x+region.width; ++x) processed.at<unsigned char>(y,x) ^= 1u << bit;
    save(directory,"exact.png",reference);
    save(directory,"baseline.png",baseline);
    save(directory,"compensated.png",noFlip);
    save(directory,"processed.png",processed);
}
}

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--capabilities") {
            std::cout << "{\"circuit_ir\":1,\"legacy\":true}\n";
            return 0;
        }
        if (argc < 10) {
            throw std::runtime_error(
                "Usage: fixed_roi_verify APP IMAGE OUT LEFT TOP RIGHT BOTTOM NODE BIT [EXACT_NODE ...]");
        }
        const std::string app(argv[1]);
        std::string profilePath;
        if (argc >= 12 && std::string(argv[argc-2]) == "--circuit") {
            profilePath = argv[argc-1];
            argc -= 2;
        }
        circuit_profile::Profile profile;
        const circuit_profile::Profile* selectedProfile = nullptr;
        if (!profilePath.empty()) {
            if (app != "ir") {
                profile = circuit_profile::Profile::load(profilePath,app);
                selectedProfile = &profile;
            }
        }
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
        if (app == "ir") {
            circuit_ir::need(!profilePath.empty(),"--circuit profile is required");
            const auto ir=circuit_ir::Profile::load(profilePath);
            const std::string site(argv[8]); const int bit=integer(argv[9]);
            std::vector<int> replacements;
            for(int i=10;i<argc;++i) replacements.push_back(integer(argv[i]));
            const auto compensated=ir.compensatedConfig(replacements);
            const auto exact=ir.run(image,ir.exactConfig());
            const auto baseline=ir.run(image,ir.baseline);
            const auto noFlip=ir.run(image,compensated);
            cv::Mat processed;
            if(site=="-1") {
                processed=ir.flipOutput(image,noFlip,region,bit);
            } else processed=ir.run(image,compensated,region,site,bit);
            save(argv[3],"exact.png",exact); save(argv[3],"baseline.png",baseline);
            save(argv[3],"compensated.png",noFlip); save(argv[3],"processed.png",processed);
            return 0;
        }
        const int node = integer(argv[8]), bit = integer(argv[9]);
        std::vector<int> replacements;
        for (int i = 10; i < argc; ++i) {
            replacements.push_back(integer(argv[i]));
        }
        if (app == "sharpen") {
            verify<applications::SharpenApplication>(image, region, argv[3], node, bit, replacements, selectedProfile);
        } else if (app == "conv") {
            verify<applications::ConvApplication>(image, region, argv[3], node, bit, replacements, selectedProfile);
        } else if (app == "sobel") {
            verify<applications::SobelApplication>(image, region, argv[3], node, bit, replacements, selectedProfile);
        } else if (app == "dct") {
            verify<applications::DctApplication>(image, region, argv[3], node, bit, replacements, selectedProfile);
        } else if (app == "graph" && selectedProfile) {
            verifyGraph(image,region,argv[3],node,bit,replacements,profile);
        } else {
            throw std::runtime_error("Unknown application: " + app);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
