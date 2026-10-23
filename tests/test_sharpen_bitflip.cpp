#include "applications/sharpen.hpp"
#include <opencv2/core.hpp>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class F> void mustThrow(F action) {
    bool thrown = false;
    try { action(); } catch (const std::runtime_error&) { thrown = true; }
    require(thrown, "Expected invalid bit-flip configuration to be rejected");
}
core::AttackConfig flip(int node, int bit, core::MonitorSignal monitor, int lo, int hi) {
    core::AttackConfig config{node, approximate::ApproxUnitId::Add12se5Z0, monitor, lo, hi};
    config.payload = core::AttackPayload::BitFlip;
    config.bitIndex = bit;
    return config;
}
}

int main() {
    try {
        applications::SharpenApplication exact(
            applications::SharpenApplication::createAllExactBaselineConfig());
        const cv::Mat image(5, 5, CV_8UC1, cv::Scalar(40));
        const cv::Mat mask(5, 5, CV_8UC1, cv::Scalar(255));
        const auto baseline = exact.runApprox(image, {});
        for (auto signal : {core::MonitorSignal::Input1, core::MonitorSignal::Input2,
                            core::MonitorSignal::BaselineOutput}) {
            const int value = signal == core::MonitorSignal::Input1 ? 80 :
                              signal == core::MonitorSignal::Input2 ? -40 : 40;
            auto config = flip(3, 4, signal, value, value);
            auto changed = exact.runApprox(image, {config});
            require(changed.at<unsigned char>(2, 2) == 56, "Inclusive hit must flip output bit 4");
            require(changed.at<unsigned char>(0, 2) == 40, "Border must remain unchanged");
            config.lower = value + 1; config.upper = value + 5;
            require(cv::countNonZero(exact.runApprox(image, {config}) != baseline) == 0,
                    "Miss must be exactly baseline");
            config.lower = value - 5; config.upper = value;
            require(exact.runApprox(image, {config}).at<unsigned char>(2, 2) == 56,
                    "Upper endpoint must be included");
        }
        require(exact.runApprox(image, {flip(3, 5, core::MonitorSignal::BaselineOutput, 40, 40)})
                    .at<unsigned char>(2, 2) == 8, "Set bit must flip down, not always add");
        std::vector<core::AddSample> samples;
        cv::Mat negative(5, 5, CV_8UC1, cv::Scalar(10));
        negative.at<unsigned char>(2, 2) = 0;
        exact.collectConfiguredAddSamples(negative, mask,
            {flip(3, 4, core::MonitorSignal::BaselineOutput, -40, -40)}, samples);
        require(samples.at(4 * 4 + 3).output == -56, "Signed 13-bit negative output was decoded incorrectly");
        samples.clear();
        exact.collectConfiguredAddSamples(image, mask,
            {flip(0, 4, core::MonitorSignal::BaselineOutput, -4096, 4095)}, samples);
        require(samples[0].baselineOutput == 160 && samples[0].output == 176,
                "Bit flip must use the existing exact baseline, not the alternate FU");
        require(samples[1].input1 == 176, "Downstream node must receive flipped output");
        require(exact.runApprox(image,
            {flip(0, 4, core::MonitorSignal::BaselineOutput, -4096, 4095)})
                    .at<unsigned char>(2, 2) == 56, "Flip must propagate to the application output");
        for (int bit : {-1, 12, 31}) mustThrow([&] {
            exact.runApprox(image, {flip(3, bit, core::MonitorSignal::Input1, -4096, 4095)});
        });
        applications::SharpenApplication approximate;
        mustThrow([&] { approximate.runApprox(image,
            {flip(0, 11, core::MonitorSignal::Input1, -4096, 4095)}); });
        std::cout << "Sharpen bit-flip execution passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
