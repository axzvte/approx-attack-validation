// UNRUN locally: see AGENTS.md. The original application remains the replay oracle.
#include "analysis/circuit_bitflip_fixed.hpp"
#include "applications/conv.hpp"
#include "applications/sobel.hpp"
#include "applications/sharpen.hpp"
#include "applications/dct.hpp"
#include "io/image_io.hpp"
#include "io/bitflip_comparison.hpp"
#include "region/region_mask.hpp"
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
namespace fs = std::filesystem;
using Config = analysis::JointBitflipConfiguration;
using Hardware = analysis::FixedBitflipHardware;
using Quality = analysis::BitflipQuality;
using Row = std::map<std::string, std::string>;
using Clock = std::chrono::steady_clock;

std::string number(double value) {
    std::ostringstream stream;
    stream << std::setprecision(17) << value;
    return stream.str();
}
double elapsed(Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}
std::ofstream open(const fs::path& path, std::ios::openmode mode = std::ios::out) {
    std::ofstream stream;
    stream.exceptions(std::ios::failbit | std::ios::badbit);
    stream.open(path, mode);
    return stream;
}
std::string quoted(const std::string& value) {
    if (value.find_first_of(",\"\r\n") == std::string::npos) return value;
    std::string result = "\"";
    for (char c : value) {
        if (c == '"') result += '"';
        result += c;
    }
    return result + '"';
}
struct Csv {
    std::ofstream stream;
    std::vector<std::string> fields;
    Csv(const fs::path& path, std::vector<std::string> names)
        : stream(open(path)), fields(std::move(names)) {
        for (std::size_t i = 0; i < fields.size(); ++i) stream << (i ? "," : "") << fields[i];
        stream << '\n';
    }
    void row(const Row& values) {
        for (std::size_t i = 0; i < fields.size(); ++i) {
            if (i) stream << ',';
            const auto found = values.find(fields[i]);
            if (found != values.end()) stream << quoted(found->second);
        }
        stream << '\n';
    }
    void close() { stream.close(); }
};

std::vector<std::string> csvLine(const std::string& line) {
    std::vector<std::string> fields;
    std::string word;
    bool inQuote = false, ended = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (inQuote) {
            if (c != '"') word += c;
            else if (i + 1 < line.size() && line[i + 1] == '"') { word += '"'; ++i; }
            else { inQuote = false; ended = true; }
        } else if (c == ',') {
            fields.push_back(word); word.clear(); ended = false;
        } else if (c == '"') {
            if (!word.empty() || ended) throw std::runtime_error("Malformed CSV quoting.");
            inQuote = true;
        } else {
            if (ended) throw std::runtime_error("Unexpected text after CSV quote.");
            word += c;
        }
    }
    if (inQuote) throw std::runtime_error("Multiline or unclosed CSV field is unsupported.");
    fields.push_back(word);
    return fields;
}
std::vector<Row> readCsv(const fs::path& path, const std::set<std::string>& required) {
    std::ifstream stream(path);
    if (!stream) throw std::runtime_error("Cannot open " + path.string());
    std::string line;
    if (!std::getline(stream, line)) throw std::runtime_error("Empty CSV: " + path.string());
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.rfind("\xEF\xBB\xBF", 0) == 0) line.erase(0, 3);
    const auto fields = csvLine(line);
    const std::set<std::string> names(fields.begin(), fields.end());
    if (names != required || names.size() != fields.size())
        throw std::runtime_error("CSV has missing, duplicate or unknown columns: " + path.string());
    std::vector<Row> result;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        const auto values = csvLine(line);
        if (values.size() != fields.size()) throw std::runtime_error("CSV row width mismatch.");
        Row row;
        for (std::size_t i = 0; i < fields.size(); ++i) row.emplace(fields[i], values[i]);
        result.push_back(std::move(row));
    }
    if (stream.bad()) throw std::runtime_error("CSV read failed.");
    return result;
}
int integer(const std::string& text) {
    std::size_t used = 0;
    const int value = std::stoi(text, &used);
    if (used != text.size()) throw std::runtime_error("Expected complete integer argument.");
    return value;
}
core::MonitorSignal monitor(const std::string& text) {
    if (text == "input1") return core::MonitorSignal::Input1;
    if (text == "input2") return core::MonitorSignal::Input2;
    if (text == "baseline_output") return core::MonitorSignal::BaselineOutput;
    throw std::runtime_error("Unknown monitor: " + text);
}
const char* signal(core::MonitorSignal value) {
    if (value == core::MonitorSignal::Input1) return "input1";
    if (value == core::MonitorSignal::Input2) return "input2";
    if (value == core::MonitorSignal::BaselineOutput) return "baseline_output";
    throw std::runtime_error("Unknown monitor enum.");
}
void intervalDomain(const core::AttackConfig& config) {
    const bool output = config.monitorInput == core::MonitorSignal::BaselineOutput;
    const int lower = output ? -4096 : -2048, upper = output ? 4095 : 2047;
    if (config.lower > config.upper || config.lower < lower || config.upper > upper)
        throw std::runtime_error("Interval is outside the physical monitor register domain.");
}
Hardware hardwareFile(const fs::path& path, std::size_t nativeCount) {
    Hardware result;
    std::set<int> seen;
    for (const auto& row : readCsv(path, {"node_id", "monitor", "bit"})) {
        const int node = integer(row.at("node_id")), bit = integer(row.at("bit"));
        if (node < 0 || static_cast<std::size_t>(node) >= nativeCount || bit < 0 || bit > 11 || !seen.insert(node).second)
            throw std::runtime_error("Invalid fixed hardware node/bit.");
        result.push_back({node, monitor(row.at("monitor")), bit});
    }
    if (result.size() < 2 || result.size() > nativeCount)
        throw std::runtime_error("Hardware must contain 2..native-node-count installed nodes.");
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.nodeId < b.nodeId; });
    return result;
}
std::vector<Config> seedFile(const fs::path& path, const Hardware& hardware, std::size_t nativeCount) {
    std::map<std::string, Config> grouped;
    for (const auto& row : readCsv(path, {"seed_id", "node_id", "monitor", "bit", "lower", "upper"})) {
        if (row.at("seed_id").empty()) throw std::runtime_error("Empty seed identity.");
        core::AttackConfig config{integer(row.at("node_id")), approximate::ApproxUnitId::Add12se5RP,
            monitor(row.at("monitor")), integer(row.at("lower")), integer(row.at("upper")),
            core::AttackPayload::BitFlip, integer(row.at("bit"))};
        intervalDomain(config);
        grouped[row.at("seed_id")].push_back(config);
    }
    std::vector<Config> result;
    for (auto& item : grouped) {
        if (!analysis::matchesCircuitFixedBitflipHardware(item.second, hardware, nativeCount))
            throw std::runtime_error("Warm seed changes installed node, monitor or bit.");
        std::sort(item.second.begin(), item.second.end(), [](const auto& a, const auto& b) { return a.nodeId < b.nodeId; });
        result.push_back(std::move(item.second));
    }
    return result;
}
std::string hardwareKey(const Hardware& hardware) {
    std::ostringstream stream;
    for (const auto& node : hardware) stream << node.nodeId << ':' << signal(node.monitorInput) << ':' << node.bitIndex << ';';
    return stream.str();
}
std::string hardwareKey(const Config& config) {
    Hardware hardware;
    for (const auto& node : config) hardware.push_back({node.nodeId, node.monitorInput, node.bitIndex});
    std::sort(hardware.begin(), hardware.end(), [](const auto& a, const auto& b) { return a.nodeId < b.nodeId; });
    return hardwareKey(hardware);
}
void quality(Row& row, const Quality& value) {
    row["global_psnr"] = number(value.globalPsnr()); row["roi_psnr"] = number(value.roiPsnr());
    row["non_roi_psnr"] = number(value.nonRoiPsnr()); row["global_mse"] = number(value.globalMse);
    row["roi_mse"] = number(value.roiMse); row["non_roi_mse"] = number(value.nonRoiMse);
}
void equalQuality(const Quality& actual, const Quality& expected) {
    for (const auto values : {std::pair<double, double>{actual.globalMse, expected.globalMse},
                             {actual.roiMse, expected.roiMse}, {actual.nonRoiMse, expected.nonRoiMse}}) {
        if (!std::isfinite(values.first) || !std::isfinite(values.second) || values.first < 0 || values.second < 0 ||
            std::abs(values.first - values.second) > 1e-7)
            throw std::runtime_error("Search score disagrees with complete original application replay.");
    }
}
bool eligible(const Quality& value, const Quality& baseline, double threshold) {
    return value.globalPsnr() + 1e-10 >= threshold && value.roiMse > baseline.roiMse + 1e-10;
}
bool better(const Quality& a, const Quality& b) {
    return a.roiMse > b.roiMse || (a.roiMse == b.roiMse && a.globalMse < b.globalMse);
}
struct Hits { std::uint64_t total = 0; double roi = 0, background = 0; };
std::vector<Hits> triggers(const core::Application& app, const cv::Mat& input, const cv::Mat& mask, const Config& config) {
    // DCT's legacy mask reader uses !=0. Threshold before collection so JPEG halos
    // do not become ROI. Block fractions below are diagnostics, never image MSE.
    cv::Mat binary;
    cv::compare(mask, cv::Scalar(128), binary, cv::CMP_GE);
    std::vector<Hits> counts(app.addNodes().size());
    std::vector<const core::AttackConfig*> byNode(counts.size(), nullptr);
    for (const auto& node : config) byNode.at(node.nodeId) = &node;
    std::vector<core::AddSample> trace;
    app.collectConfiguredAddSamples(input, binary, config, trace);
    for (const auto& sample : trace) {
        const auto* node = byNode.at(sample.nodeId);
        if (!node) continue;
        const int value = node->monitorInput == core::MonitorSignal::Input1 ? sample.input1 :
            node->monitorInput == core::MonitorSignal::Input2 ? sample.input2 : sample.baselineOutput;
        if (value >= node->lower && value <= node->upper) {
            if (!std::isfinite(sample.roiWeight) || sample.roiWeight < 0 || sample.roiWeight > 1)
                throw std::runtime_error("Invalid diagnostic trigger region weight.");
            auto& count = counts.at(sample.nodeId);
            ++count.total; count.roi += sample.roiWeight; count.background += 1 - sample.roiWeight;
        }
    }
    return counts;
}
void save(const fs::path& path, const cv::Mat& image) {
    if (!cv::imwrite(path.string(), image)) throw std::runtime_error("Image write failed: " + path.string());
}
void images(const fs::path& output, const cv::Mat& exact, const cv::Mat& baseline,
            const cv::Mat& selected, const cv::Mat& mask, Csv& manifest, const std::string& mode,
            const Quality& baselineQuality, const Quality& selectedQuality, const std::string& status) {
    const double inf=std::numeric_limits<double>::infinity();
    const std::array<image_io::ComparisonPsnr,3> quality{{{inf,inf},
        {baselineQuality.globalPsnr(),baselineQuality.roiPsnr()},
        {selectedQuality.globalPsnr(),selectedQuality.roiPsnr()}}};
    const auto panel = [&](const std::array<cv::Mat,3>& frames) {
        return image_io::bitflipComparisonPanel(frames,quality,status);
    };
    const auto write = [&](const std::string& kind, const cv::Mat& image) {
        const std::string file = "selected_" + kind + ".png";
        save(output / file, image);
        manifest.row({{"candidate_id", "selected"}, {"image_kind", kind}, {"path", file}});
    };
    cv::Mat binary;
    cv::compare(mask, cv::Scalar(128), binary, cv::CMP_GE);
    const auto box = cv::boundingRect(binary);
    std::array<cv::Mat,3> roiFrames;
    const std::array<cv::Mat,3> original{exact,baseline,selected};
    for (int i=0;i<3;++i)
        cv::resize(original[i](box),roiFrames[i],cv::Size(),2,2,cv::INTER_NEAREST);
    cv::Mat roi = panel(roiFrames);
    if (mode == "comparisons") {
        write("comparison", panel({exact, baseline, selected}));
        write("roi_comparison", roi);
        return;
    }
    write("output", selected);
    write("roi_comparison", roi);
    std::array<cv::Mat, 3> background{exact.clone(), baseline.clone(), selected.clone()};
    for (auto& image : background) image.setTo(127, binary);
    write("background_comparison", panel(background));
    if (mode == "full") {
        write("comparison", panel({exact, baseline, selected}));
        cv::Mat difference;
        cv::absdiff(baseline, selected, difference); difference.convertTo(difference, CV_8U, 4);
        write("diff_baseline_x4", difference);
        cv::absdiff(exact, selected, difference); difference.convertTo(difference, CV_8U, 4);
        write("diff_exact_x4", difference);
    }
}
void spatial(Row& row, const cv::Mat& exact, const cv::Mat& baseline, const cv::Mat& image, const cv::Mat& mask) {
    std::array<std::uint64_t, 2> total{}, changed{}, error{}, change{};
    for (int y = 0; y < image.rows; ++y) for (int x = 0; x < image.cols; ++x) {
        const int region = mask.at<unsigned char>(y, x) >= 128;
        const int value = image.at<unsigned char>(y, x), base = baseline.at<unsigned char>(y, x), ref = exact.at<unsigned char>(y, x);
        ++total[region]; changed[region] += value != base;
        error[region] += std::abs(value - ref) >= 16; change[region] += std::abs(value - base) >= 16;
    }
    for (int region = 0; region < 2; ++region) {
        if (!total[region]) throw std::runtime_error("Both ROI and background must contain pixels.");
        const std::string prefix = region ? "roi" : "non_roi";
        row[prefix + "_changed_fraction"] = number(double(changed[region]) / total[region]);
        row[prefix + "_error_ge16_fraction"] = number(double(error[region]) / total[region]);
        row[prefix + "_change_ge16_fraction"] = number(double(change[region]) / total[region]);
    }
}
struct Verified { Config configuration; Quality metrics; cv::Mat image; };
}

int main(int argc, char** argv) {
    try {
        if (argc < 4) throw std::runtime_error(
            "Usage: circuit_bitflip_fixed sharpen|conv|sobel|dct DATA_DIR NEW_OUTPUT_DIR --hardware H.csv "
            "[--seeds seeds.csv] [--image 1..40] [--input-image FILE] [--beam 4] [--refinement 1] [--restarts 2] "
            "[--boundary-budget 24] [--context-budget 128] [--max-full-evals 256] "
            "[--full-candidates 8] [--global-psnr 30] [--image-mode compact|none|full|comparisons] "
            "[--continuous] [--convergence-tolerance 0.001]");
        const std::string application = argv[1];
        std::unique_ptr<core::Application> app;
        // Preserve native baseline units, graph widths and exact/reference behavior.
        if (application == "conv") app = std::make_unique<applications::ConvApplication>();
        else if (application == "sharpen") app = std::make_unique<applications::SharpenApplication>();
        else if (application == "sobel") app = std::make_unique<applications::SobelApplication>();
        else if (application == "dct") app = std::make_unique<applications::DctApplication>();
        else throw std::runtime_error("Unknown circuit: " + application);
        const auto nativeCount = app->addNodes().size();
        const std::set<std::string> allowed{"--hardware", "--seeds", "--image", "--input-image", "--beam", "--refinement",
            "--restarts", "--boundary-budget", "--context-budget", "--max-full-evals", "--full-candidates",
            "--global-psnr", "--image-mode", "--convergence-tolerance", "--roi-dir", "--roi-mask"};
        std::map<std::string, std::string> flags;
        for (int i = 4; i < argc;) {
            const std::string flag = argv[i];
            if (flag == "--continuous") {
                if (!flags.emplace(flag, "1").second) throw std::runtime_error("Duplicate --continuous.");
                ++i;
                continue;
            }
            if (!allowed.count(flag) || i + 1 >= argc || !flags.emplace(flag, argv[i + 1]).second)
                throw std::runtime_error("Invalid, duplicate or incomplete option: " + flag);
            i += 2;
        }
        if (!flags.count("--hardware")) throw std::runtime_error("Supply --hardware.");
        if (flags.count("--roi-dir") && flags.count("--roi-mask")) throw std::runtime_error("Use either --roi-dir or --roi-mask.");
        const auto option = [&](const std::string& flag, int fallback, int lower, int upper) {
            const int value = flags.count(flag) ? integer(flags.at(flag)) : fallback;
            if (value < lower || value > upper) throw std::runtime_error("Invalid " + flag);
            return value;
        };
        // With an explicit input path, --image is a diagnostic identity only.
        // Legacy indexed inputs retain their original1..40 routing contract.
        const int index = option("--image", 1, 1, flags.count("--input-image") ? 1000000 : 40);
        analysis::CircuitFixedBitflipOptions options;
        options.continuous = flags.count("--continuous") != 0;
        if (flags.count("--convergence-tolerance")) {
            std::size_t used = 0;
            options.convergenceTolerance = std::stod(flags.at("--convergence-tolerance"), &used);
            if (used != flags.at("--convergence-tolerance").size()) throw std::runtime_error("Invalid convergence tolerance.");
        }
        if (!std::isfinite(options.convergenceTolerance) || options.convergenceTolerance < 0)
            throw std::runtime_error("Invalid convergence tolerance.");
        options.beamWidth = option("--beam", 4, 1, 64);
        options.refinementRounds = option("--refinement", 1, 0, 10);
        options.restarts = option("--restarts", 2, 1, 8);
        options.boundaryBudget = option("--boundary-budget", 24, 0, 8192);
        options.maxContexts = option("--context-budget", 128, 1, 1000000);
        options.maxFullEvaluations = option("--max-full-evals", 256, 1, 10000000);
        options.fullCandidatesPerContext = option("--full-candidates", 8, 1, 1024);
        if (options.boundaryBudget == 1) throw std::runtime_error("Boundary budget must be 0 or >=2.");
        if (flags.count("--global-psnr")) {
            std::size_t used = 0;
            options.globalPsnrThreshold = std::stod(flags.at("--global-psnr"), &used);
            if (used != flags.at("--global-psnr").size()) throw std::runtime_error("Invalid PSNR threshold.");
        }
        if (!std::isfinite(options.globalPsnrThreshold) || options.globalPsnrThreshold < 0 || options.globalPsnrThreshold > 1000)
            throw std::runtime_error("Invalid PSNR threshold.");
        const auto hardware = hardwareFile(flags.at("--hardware"), nativeCount);
        const auto fixedKey = hardwareKey(hardware);
        if (flags.count("--seeds")) options.seeds = seedFile(flags.at("--seeds"), hardware, nativeCount);
        const std::string mode = flags.count("--image-mode") ? flags.at("--image-mode") : "compact";
        if (mode != "compact" && mode != "none" && mode != "full" && mode != "comparisons")
            throw std::runtime_error("Invalid image mode.");
        const fs::path data = fs::absolute(argv[2]);
        const bool perImageRoi = flags.count("--roi-dir") || flags.count("--roi-mask");
        const fs::path roiPath = flags.count("--roi-mask") ? fs::absolute(flags.at("--roi-mask")) : flags.count("--roi-dir") ?
            fs::path(image_io::resolveRoiMaskPath(fs::absolute(flags.at("--roi-dir")).string(),index)) : data / "mask/roi_mask.jpg";
        fs::path output = fs::absolute(argv[3]);
        while (output.has_relative_path() && (output.filename().empty() || output.filename() == ".")) output = output.parent_path();
        fs::create_directories(output.parent_path());
        if (!fs::create_directory(output)) throw std::runtime_error("Output exists; use a new directory.");
        {
            auto run = open(output / "RUN.txt");
            run << "completed=0\napplication=" << application << "\nimage=" << index
                << "\nhardware_key=" << fixedKey << "\ninstalled_nodes=" << hardware.size() << "\nnative_nodes=" << nativeCount
                << "\nimage_mode=" << mode << "\nbaseline=original_application_default\nmask_threshold=128"
                << "\ninput_bits=signed12\noutput_bits=signed13_non_sign_0..11\nreference=application_exact_output"
                << "\nablations=disabled\nenable_parameter=absent\nbeam=" << options.beamWidth
                << "\nrefinement=" << options.refinementRounds << "\nrestarts=" << options.restarts
                << "\ncontinuous=" << options.continuous << "\nconvergence_tolerance=" << number(options.convergenceTolerance)
                << "\nboundary_budget=" << options.boundaryBudget << "\ncontext_budget=" << options.maxContexts
                << "\nmax_full_evaluations=" << options.maxFullEvaluations << "\nfull_candidates=" << options.fullCandidatesPerContext
                << "\nglobal_psnr_threshold=" << number(options.globalPsnrThreshold) << "\nwarm_seeds=" << options.seeds.size()
                << "\nroi=" << roiPath.string() << "\nroi_mode=" << (perImageRoi ? "per_image" : "legacy_fixed")
                << "\ntrigger_region_basis=" << (application == "dct" ? "block_roi_weight" : "pixel_roi")
                << "\nOnly interval endpoints change. Installed nodes, monitors and bits are invariant."
                << "\nOffline per-image interval calibration uses exact reference and the selected image ROI."
                << "\nNo background hard threshold; visual approval remains manual."
                << "\nDCT quality uses complete reconstructed pixels; block ROI weights describe triggers only."
                << "\nSearch full-evaluation budget excludes required reference/dormant checks and final oracle replays.\n";
            run.close();
        }
        const auto referenceStart = Clock::now();
        const fs::path inputPath = flags.count("--input-image") ? fs::absolute(flags.at("--input-image")) :
            fs::path(image_io::resolveStagedImagePath(data.string(), index));
        {
            auto run = open(output / "RUN.txt", std::ios::app);
            run << "input_image=" << inputPath.string() << '\n';
            run.close();
        }
        const auto input = image_io::loadGrayImage(inputPath.string(),perImageRoi);
        const auto mask = perImageRoi ? image_io::loadBinaryRoiMask(roiPath.string(),input) :
            region_mask::resizeMaskToImage(image_io::loadGrayImage(roiPath.string()), input);
        const auto exact = app->runExact(input), baseline = app->runApprox(input, {});
        const auto baselineQuality = analysis::measureBitflipQuality(exact, baseline, mask);
        const double cliReferenceSeconds = elapsed(referenceStart);
        const auto start = Clock::now();
        const auto result = analysis::searchCircuitFixedBitflipIntervals(*app, input, mask, hardware, options, [&](const auto& progress) {
            std::cout << application << " fixed H image " << index << " restart " << progress.restart + 1
                << " pass " << progress.pass + 1 << " node " << progress.node << " contexts=" << progress.stats.contextsBuilt
                << " cache=" << progress.stats.cacheHits << " full=" << progress.stats.fullEvaluations << std::endl;
        });
        const double calibrationSeconds = elapsed(start);
        equalQuality(baselineQuality, result.baseline);
        std::uint64_t cliReplayEvaluations = 0;
        double cliReplaySeconds = 0;
        const auto replay = [&](const analysis::JointBitflipState& state) {
            const auto replayStart = Clock::now();
            if (!analysis::matchesCircuitFixedBitflipHardware(state.configuration, hardware, nativeCount) || hardwareKey(state.configuration) != fixedKey)
                throw std::runtime_error("Solver changed fixed hardware before replay.");
            for (const auto& node : state.configuration) intervalDomain(node);
            auto image = app->runApprox(input, state.configuration);
            ++cliReplayEvaluations;
            const auto measured = analysis::measureBitflipQuality(exact, image, mask);
            equalQuality(measured, state.metrics);
            if (!analysis::matchesCircuitFixedBitflipHardware(state.configuration, hardware, nativeCount) || hardwareKey(state.configuration) != fixedKey)
                throw std::runtime_error("Fixed hardware changed during original application replay.");
            cliReplaySeconds += elapsed(replayStart);
            return Verified{state.configuration, measured, std::move(image)};
        };
        std::optional<Verified> selected;
        for (const auto& candidate : result.candidates) {
            auto verified = replay(candidate);
            if (!eligible(verified.metrics, baselineQuality, options.globalPsnrThreshold))
                throw std::runtime_error("Solver returned an ineligible numeric candidate.");
            if (!selected || better(verified.metrics, selected->metrics)) selected = std::move(verified);
        }
        if (!selected && result.bestLegal) selected = replay(*result.bestLegal);
        const std::string searchStatus = result.stats.contextBudgetExhausted || result.stats.fullBudgetExhausted ? "budget_exhausted" : "completed";
        Csv summary(output / "summary.csv", {"application", "image", "candidate_id", "status", "installed_nodes", "triggered_nodes",
            "hardware_key", "visual_status", "search_status", "images_exported", "baseline_global_meets_threshold", "global_psnr",
            "roi_psnr", "non_roi_psnr", "global_mse", "roi_mse", "non_roi_mse", "delta_roi_mse", "delta_non_roi_mse",
            "roi_changed_fraction", "non_roi_changed_fraction", "roi_error_ge16_fraction", "non_roi_error_ge16_fraction",
            "roi_change_ge16_fraction", "non_roi_change_ge16_fraction", "calibration_seconds"});
        Csv configs(output / "configurations.csv", {"application", "image", "candidate_id", "node_id", "node_name", "monitor", "bit",
            "lower", "upper", "dynamic_triggers", "roi_triggers", "non_roi_triggers", "trigger_region_basis"});
        Csv manifest(output / "images.csv", {"candidate_id", "image_kind", "path"});
        const Row common{{"application", application}, {"image", std::to_string(index)}, {"hardware_key", fixedKey},
            {"search_status", searchStatus}, {"calibration_seconds", number(calibrationSeconds)},
            {"baseline_global_meets_threshold", baselineQuality.globalPsnr() + 1e-10 >= options.globalPsnrThreshold ? "1" : "0"}};
        auto base = common;
        base.insert({{"candidate_id", "baseline"}, {"status", "reference"}, {"installed_nodes", "0"}, {"triggered_nodes", "0"},
                     {"visual_status", "reference"}, {"images_exported", "0"}});
        quality(base, baselineQuality); summary.row(base);
        std::string selectedStatus = "no_legal_interval_state";
        if (selected) {
            // Count actual downstream triggers after all installed configurations execute together.
            const auto counts = triggers(*app, input, mask, selected->configuration);
            std::size_t triggeredNodes = 0;
            for (const auto& node : hardware) triggeredNodes += counts.at(node.nodeId).total > 0;
            const bool numeric = eligible(selected->metrics, baselineQuality, options.globalPsnrThreshold);
            selectedStatus = numeric ? "numeric_eligible" : "quality_not_met";
            const bool exported = mode == "full" || mode == "comparisons" || (mode == "compact" && numeric);
            auto row = common;
            row.insert({{"candidate_id", "selected"}, {"status", selectedStatus}, {"installed_nodes", std::to_string(hardware.size())},
                {"triggered_nodes", std::to_string(triggeredNodes)}, {"visual_status", numeric ? "pending_user_review" : "not_eligible"},
                {"images_exported", exported ? "1" : "0"}});
            quality(row, selected->metrics);
            row["delta_roi_mse"] = number(selected->metrics.roiMse - baselineQuality.roiMse);
            row["delta_non_roi_mse"] = number(selected->metrics.nonRoiMse - baselineQuality.nonRoiMse);
            spatial(row, exact, baseline, selected->image, mask); summary.row(row);
            if (exported) images(output, exact, baseline, selected->image, mask, manifest, mode,
                                 baselineQuality, selected->metrics, selectedStatus);
            std::sort(selected->configuration.begin(), selected->configuration.end(), [](const auto& a, const auto& b) { return a.nodeId < b.nodeId; });
            for (const auto& node : selected->configuration) {
                const auto& count = counts.at(node.nodeId);
                configs.row({{"application", application}, {"image", std::to_string(index)}, {"candidate_id", "selected"},
                    {"node_id", std::to_string(node.nodeId)}, {"node_name", app->addNodes().at(node.nodeId).name},
                    {"monitor", signal(node.monitorInput)}, {"bit", std::to_string(node.bitIndex)}, {"lower", std::to_string(node.lower)},
                    {"upper", std::to_string(node.upper)}, {"dynamic_triggers", std::to_string(count.total)},
                    {"roi_triggers", number(count.roi)}, {"non_roi_triggers", number(count.background)},
                    {"trigger_region_basis", application == "dct" ? "block_roi_weight" : "pixel_roi"}});
            }
        } else {
            auto row = common;
            row.insert({{"candidate_id", "selected"}, {"status", selectedStatus}, {"installed_nodes", std::to_string(hardware.size())},
                        {"visual_status", "not_available"}, {"images_exported", "0"}});
            summary.row(row);
            // No executable intervals exist. Keep the configuration table empty;
            // installed_nodes/hardware_key still preserve the full requested H.
        }
        if (mode == "full") {
            for (const auto& item : std::vector<std::pair<std::string, cv::Mat>>{{"exact", exact}, {"baseline", baseline}, {"roi_mask", mask}}) {
                const auto file = item.first + ".png";
                save(output / file, item.second);
                manifest.row({{"candidate_id", "reference"}, {"image_kind", item.first}, {"path", file}});
            }
        }
        summary.close(); configs.close(); manifest.close();
        std::vector<std::string> statFields{"application", "image", "contexts_built", "cache_hits", "evaluated_intervals",
            "range_rejected_intervals", "retained_intermediate_quality_failures", "accepted_seeds", "rejected_seeds",
            "context_budget_exhausted", "context_nodes", "max_contexts", "installed_nodes", "native_nodes", "calibration_seconds",
            "search_status", "reference_seconds", "initialization_seconds", "trace_seconds", "profile_seconds", "interval_seconds",
            "final_replay_seconds", "trace_builds", "trace_cache_hits", "full_evaluations", "final_replay_evaluations",
            "full_budget_exhausted", "max_full_evaluations", "full_candidates", "full_evaluation_seconds", "context_node_count", "scoring",
            "continuous", "convergence_tolerance", "stopped_for_convergence", "completed_sweeps"};
        Row statRow{{"application", application}, {"image", std::to_string(index)},
            {"continuous", result.stats.continuous ? "1" : "0"}, {"convergence_tolerance", number(options.convergenceTolerance)},
            {"stopped_for_convergence", result.stats.stoppedForConvergence ? "1" : "0"},
            {"completed_sweeps", std::to_string(result.stats.completedSweeps)},
            {"contexts_built", std::to_string(result.stats.contextsBuilt)}, {"cache_hits", std::to_string(result.stats.cacheHits)},
            {"evaluated_intervals", std::to_string(result.stats.evaluatedIntervals)}, {"range_rejected_intervals", std::to_string(result.stats.rangeRejected)},
            {"retained_intermediate_quality_failures", std::to_string(result.stats.retainedQualityFailures)},
            {"accepted_seeds", std::to_string(result.stats.acceptedSeeds)}, {"rejected_seeds", std::to_string(result.stats.rejectedSeeds)},
            {"context_budget_exhausted", result.stats.contextBudgetExhausted ? "1" : "0"}, {"max_contexts", std::to_string(options.maxContexts)},
            {"installed_nodes", std::to_string(hardware.size())}, {"native_nodes", std::to_string(nativeCount)},
            {"calibration_seconds", number(calibrationSeconds)}, {"search_status", searchStatus},
            {"reference_seconds", number(result.stats.referenceSeconds + cliReferenceSeconds)},
            {"initialization_seconds", number(result.stats.initializationSeconds)}, {"trace_seconds", number(result.stats.traceSeconds)},
            {"profile_seconds", number(result.stats.profileSeconds)}, {"interval_seconds", number(result.stats.intervalSeconds)},
            {"final_replay_seconds", number(result.stats.finalReplaySeconds + cliReplaySeconds)},
            {"trace_builds", std::to_string(result.stats.traceBuilds)}, {"trace_cache_hits", std::to_string(result.stats.traceCacheHits)},
            {"full_evaluations", std::to_string(result.stats.fullEvaluations)},
            {"final_replay_evaluations", std::to_string(result.stats.finalReplayEvaluations + cliReplayEvaluations)},
            {"full_budget_exhausted", result.stats.fullBudgetExhausted ? "1" : "0"},
            {"max_full_evaluations", std::to_string(options.maxFullEvaluations)}, {"full_candidates", std::to_string(options.fullCandidatesPerContext)},
            {"full_evaluation_seconds", number(result.stats.fullEvaluationSeconds)},
            {"scoring", application == "dct" ? "full_joint_image_reconstruction" : "exact_conditional_pixel_replay"}};
        std::ostringstream covered;
        std::size_t coveredCount = 0;
        if (result.stats.contextsByNode.size() != nativeCount) throw std::runtime_error("Solver returned truncated native-node coverage.");
        for (std::size_t node = 0; node < nativeCount; ++node) {
            const std::string field = "contexts_node_" + std::to_string(node);
            statFields.push_back(field); statRow[field] = std::to_string(result.stats.contextsByNode[node]);
            if (result.stats.contextsByNode[node]) { covered << node << ';'; ++coveredCount; }
        }
        statRow["context_nodes"] = covered.str(); statRow["context_node_count"] = std::to_string(coveredCount);
        Csv stats(output / "search_stats.csv", std::move(statFields)); stats.row(statRow); stats.close();
        auto run = open(output / "RUN.txt", std::ios::app);
        run << "search_status=" << searchStatus << "\nselected_status=" << selectedStatus
            << "\nstopped_for_convergence=" << result.stats.stoppedForConvergence
            << "\ncompleted_sweeps=" << result.stats.completedSweeps << "\ncompleted=1\n";
        run.close();
        return 0;
    } catch (const std::exception& error) {
        // Operational/arithmetic failures remain failures; never forge a no-candidate row.
        std::cerr << error.what() << '\n';
        return 1;
    }
}
