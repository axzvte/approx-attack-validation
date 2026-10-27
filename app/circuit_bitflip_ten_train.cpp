// Offline fixed-hardware selection; per-image ROI is validated at the I/O boundary.
#include "analysis/circuit_bitflip_ten_train.hpp"
#include "applications/conv.hpp"
#include "applications/dct.hpp"
#include "applications/sobel.hpp"
#include "applications/sharpen.hpp"
#include "io/image_io.hpp"
#include "region/region_mask.hpp"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
namespace fs = std::filesystem;
using Row = std::map<std::string, std::string>;
using Candidate = analysis::TenTrainCandidate;
using Evaluation = analysis::TenTrainEvaluation;
using Configuration = analysis::JointBitflipConfiguration;
using Quality = analysis::BitflipQuality;

std::string number(double value) {
    std::ostringstream stream;
    stream << std::setprecision(17) << value;
    return stream.str();
}
std::ofstream open(const fs::path& path) {
    std::ofstream stream;
    stream.exceptions(std::ios::failbit | std::ios::badbit);
    stream.open(path);
    return stream;
}
std::string escaped(const std::string& value) {
    if (value.find_first_of(",\"\r\n") == std::string::npos) return value;
    std::string result = "\"";
    for (const char c : value) {
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
        for (std::size_t i = 0; i < fields.size(); ++i) stream << (i ? "," : "") << escaped(fields[i]);
        stream << '\n';
    }
    void row(const Row& values) {
        for (std::size_t i = 0; i < fields.size(); ++i) {
            if (i) stream << ',';
            const auto found = values.find(fields[i]);
            if (found != values.end()) stream << escaped(found->second);
        }
        stream << '\n';
    }
    void close() { stream.close(); }
};
const char* signal(core::MonitorSignal monitor) {
    if (monitor == core::MonitorSignal::Input1) return "input1";
    if (monitor == core::MonitorSignal::Input2) return "input2";
    if (monitor == core::MonitorSignal::BaselineOutput) return "baseline_output";
    throw std::runtime_error("Unknown monitor enum.");
}
std::string lower(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}
bool contains(const fs::path& parent, const fs::path& child) {
    auto p = parent.begin(), c = child.begin();
    for (; p != parent.end(); ++p, ++c) {
        if (c == child.end()) return false;
#ifdef _WIN32
        if (lower(p->string()) != lower(c->string())) return false;
#else
        if (*p != *c) return false;
#endif
    }
    return true;
}
void outputPath(const fs::path& data, const fs::path& output) {
    if (!fs::is_directory(data)) throw std::runtime_error("Data directory does not exist.");
    if (contains(data, output) || contains(output, data))
        throw std::runtime_error("Output must not overlap the reference data directory.");
    for (const auto& part : output)
        if (lower(part.string()) == "sources")
            throw std::runtime_error("Output must not be inside synced reference sources.");
    if (fs::exists(output)) throw std::runtime_error("Output exists; use a new directory.");
}
std::size_t integer(const std::string& text, std::size_t minimum, std::size_t maximum,
                    const std::string& flag) {
    if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos)
        throw std::runtime_error("Invalid " + flag);
    std::size_t used = 0;
    const auto value = std::stoull(text, &used);
    if (used != text.size() || value < minimum || value > maximum)
        throw std::runtime_error("Invalid " + flag);
    return static_cast<std::size_t>(value);
}
double decimal(const std::string& text, double minimum, double maximum, const std::string& flag) {
    std::size_t used = 0;
    const double value = std::stod(text, &used);
    if (used != text.size() || !std::isfinite(value) || value < minimum || value > maximum)
        throw std::runtime_error("Invalid " + flag);
    return value;
}
void quality(Row& row, const Quality& value) {
    row["global_mse"] = number(value.globalMse);
    row["roi_mse"] = number(value.roiMse);
    row["non_roi_mse"] = number(value.nonRoiMse);
    row["global_psnr"] = number(value.globalPsnr());
    row["roi_psnr"] = number(value.roiPsnr());
    row["non_roi_psnr"] = number(value.nonRoiPsnr());
}
void validateQuality(const Quality& value) {
    for (const double metric : {value.globalMse, value.roiMse, value.nonRoiMse})
        if (!std::isfinite(metric) || metric < 0)
            throw std::runtime_error("Solver returned invalid image quality.");
}
double drop(const Evaluation& evaluation) {
    if (!evaluation.numericEligible) return 0;
    return std::max(0.0, std::min(100.0, evaluation.baseline.roiPsnr()) -
                              std::min(100.0, evaluation.selected.metrics.roiPsnr()));
}
bool complete(const Candidate& candidate) { return candidate.evaluations.size() == 10; }
Configuration ordered(Configuration configuration) {
    std::sort(configuration.begin(), configuration.end(), [](const auto& a, const auto& b) { return a.nodeId < b.nodeId; });
    return configuration;
}
std::string configKey(const Configuration& configuration) {
    std::ostringstream stream;
    for (const auto& node : ordered(configuration))
        stream << node.nodeId << ':' << signal(node.monitorInput) << ':' << node.bitIndex << ':'
               << node.lower << ':' << node.upper << ';';
    return stream.str();
}
Row configRow(const core::AttackConfig& node) {
    return {{"node_id", std::to_string(node.nodeId)}, {"monitor", signal(node.monitorInput)},
            {"bit", std::to_string(node.bitIndex)}, {"lower", std::to_string(node.lower)},
            {"upper", std::to_string(node.upper)}};
}
Row scheduleRow(const analysis::TenTrainResult& result, const analysis::TenTrainOptions& options) {
    std::ostringstream order;
    for (std::size_t i = 0; i < result.evaluationImageOrder.size(); ++i)
        order << (i ? "," : "") << result.evaluationImageOrder[i];
    return {{"scheduling", result.scheduling}, {"initial_batch", std::to_string(options.initialBatch)},
        {"completion_reserve", std::to_string(options.completionReserve)},
        {"effective_completion_reserve", std::to_string(result.effectiveCompletionReserve)},
        {"reference_evaluations", std::to_string(result.referenceEvaluations)},
        {"exploration_evaluations", std::to_string(result.explorationEvaluations)},
        {"completion_evaluations", std::to_string(result.completionEvaluations)},
        {"completion_candidates", std::to_string(result.completionCandidates)},
        {"evaluation_image_order", order.str()}};
}
void validateResult(const analysis::TenTrainResult& result, std::size_t nativeCount,
                    const analysis::TenTrainOptions& options) {
    if (result.baselines.size() != 10 || result.actualEvaluations > options.maxEvaluations ||
        result.candidates.size() > options.maxCandidates)
        throw std::runtime_error("Solver violated the training image or search budget contract.");
    auto imageOrder = result.evaluationImageOrder;
    std::sort(imageOrder.begin(), imageOrder.end());
    if (result.scheduling != options.scheduling || imageOrder != std::vector<int>({1,2,3,4,5,6,7,8,9,10}) ||
        result.referenceEvaluations + result.explorationEvaluations + result.completionEvaluations != result.actualEvaluations)
        throw std::runtime_error("Solver returned inconsistent scheduling diagnostics.");
    if (result.referenceBuilds != 10 || !std::isfinite(result.preparationSeconds) || result.preparationSeconds < 0 ||
        !std::isfinite(result.searchSeconds) || result.searchSeconds < 0 || result.stopReason.empty())
        throw std::runtime_error("Solver returned invalid preparation or search statistics.");
    for (const auto& baseline : result.baselines) validateQuality(baseline);
    std::set<std::string> hardwareIds;
    std::size_t evaluated = 0;
    for (const auto& candidate : result.candidates) {
        if (candidate.hardwareId.empty() || !hardwareIds.insert(candidate.hardwareId).second ||
            candidate.hardware.size() < 2 || candidate.hardware.size() > nativeCount)
            throw std::runtime_error("Invalid candidate hardware identity or installed node count.");
        std::set<int> nodes, images;
        for (const auto& node : candidate.hardware) {
            signal(node.monitorInput);
            if (node.nodeId < 0 || static_cast<std::size_t>(node.nodeId) >= nativeCount ||
                node.bitIndex < 0 || node.bitIndex > 11 || !nodes.insert(node.nodeId).second)
                throw std::runtime_error("Invalid candidate hardware node or bit.");
        }
        std::size_t successes = 0;
        for (const auto& evaluation : candidate.evaluations) {
            ++evaluated;
            if ((options.scheduling == "sequential" && evaluation.phase != "sequential") ||
                (options.scheduling == "interleaved" && evaluation.phase != "reference" &&
                 evaluation.phase != "exploration" && evaluation.phase != "completion"))
                throw std::runtime_error("Solver returned an invalid evaluation phase.");
            if (evaluation.imageId < 1 || evaluation.imageId > 10 || !images.insert(evaluation.imageId).second)
                throw std::runtime_error("Solver returned an unexpected or duplicate training image.");
            if (!std::isfinite(evaluation.elapsedSeconds) || evaluation.elapsedSeconds < 0)
                throw std::runtime_error("Invalid evaluation timing.");
            if (!evaluation.legal) {
                if (evaluation.numericEligible) throw std::runtime_error("Illegal interval state marked eligible.");
                continue;
            }
            validateQuality(evaluation.baseline);
            validateQuality(evaluation.selected.metrics);
            const auto& baseline = result.baselines[static_cast<std::size_t>(evaluation.imageId - 1)];
            if (evaluation.baseline.globalMse != baseline.globalMse || evaluation.baseline.roiMse != baseline.roiMse ||
                evaluation.baseline.nonRoiMse != baseline.nonRoiMse)
                throw std::runtime_error("Evaluation baseline differs from the original prepared baseline.");
            if (!analysis::matchesCircuitFixedBitflipHardware(evaluation.selected.configuration, candidate.hardware, nativeCount))
                throw std::runtime_error("Solver configuration changed installed hardware.");
            for (const auto& node : evaluation.selected.configuration) {
                const bool output = node.monitorInput == core::MonitorSignal::BaselineOutput;
                if (node.lower > node.upper || node.lower < (output ? -4096 : -2048) ||
                    node.upper > (output ? 4095 : 2047))
                    throw std::runtime_error("Solver interval exceeds signed monitor register domain.");
            }
            const auto& q = evaluation.selected.metrics;
            const bool numeric = q.globalPsnr() + 1e-10 >= options.intervals.globalPsnrThreshold &&
                                 q.roiMse > evaluation.baseline.roiMse + 1e-10;
            if (numeric != evaluation.numericEligible) throw std::runtime_error("Solver numeric status disagrees with image quality.");
            successes += evaluation.numericEligible;
        }
        if (candidate.numericSuccesses != successes ||
            (candidate.status != "complete" && candidate.status != "bound_eliminated" && candidate.status != "budget_incomplete") ||
            (complete(candidate) != (candidate.status == "complete")) ||
            !std::isfinite(candidate.worstRoiDrop) || candidate.worstRoiDrop < 0 ||
            !std::isfinite(candidate.meanRoiDrop) || candidate.meanRoiDrop < 0)
            throw std::runtime_error("Solver candidate summary disagrees with measured outcomes.");
        std::vector<const Evaluation*> sorted;
        for (const auto& evaluation : candidate.evaluations) sorted.push_back(&evaluation);
        std::sort(sorted.begin(), sorted.end(), [](auto a, auto b) { return a->imageId < b->imageId; });
        double worst = sorted.empty() ? 0 : 100, sum = 0;
        for (const auto* evaluation : sorted) {
            const double score = drop(*evaluation);
            worst = std::min(worst, score); sum += score;
        }
        const double mean = sum / result.baselines.size();
        if (std::abs(candidate.worstRoiDrop - worst) > 1e-9 || std::abs(candidate.meanRoiDrop - mean) > 1e-9)
            throw std::runtime_error("Candidate scores do not use the declared training image denominator.");
    }
    if (evaluated != result.actualEvaluations) throw std::runtime_error("Pair evaluation total is inconsistent.");
    if (result.bestIndex && *result.bestIndex >= result.candidates.size())
        throw std::runtime_error("Solver best candidate index is invalid.");
}
}

int main(int argc, char** argv) {
    try {
        if (argc < 4) throw std::runtime_error(
            "Usage: circuit_bitflip_ten_train sharpen|sobel|conv|dct DATA NEW_OUTPUT [--train 1:10] "
            "[--max-evaluations 80] [--max-candidates 24] [--initial-candidates 3] "
            "[--proposals-per-round 3] [--max-rounds 6] [--patience 2] [--workers 4] "
            "[--scheduling interleaved|sequential] [--initial-batch 4] [--completion-reserve 20] "
            "[--context-budget 64] [--beam 4] [--refinement 1] [--restarts 2] "
            "[--boundary-budget 24] [--max-full-evals 256] [--full-candidates 8] "
            "[--global-psnr 30] [--convergence-tolerance 0.001] [--seed-limit 2]");
        const std::string application = argv[1];
        std::unique_ptr<core::Application> app;
        // Use the original default baseline, exact reference, widths and graph.
        if (application == "sobel") app = std::make_unique<applications::SobelApplication>();
        else if (application == "sharpen") app = std::make_unique<applications::SharpenApplication>();
        else if (application == "conv") app = std::make_unique<applications::ConvApplication>();
        else if (application == "dct") app = std::make_unique<applications::DctApplication>();
        else throw std::runtime_error("Unknown circuit: " + application);
        const std::set<std::string> allowed{"--train", "--max-evaluations", "--max-candidates", "--initial-candidates",
            "--proposals-per-round", "--max-rounds", "--patience", "--workers", "--context-budget", "--beam",
            "--refinement", "--restarts", "--boundary-budget", "--max-full-evals", "--full-candidates",
            "--global-psnr", "--convergence-tolerance", "--seed-limit", "--scheduling", "--initial-batch", "--completion-reserve", "--roi-dir"};
        std::map<std::string, std::string> flags;
        for (int i = 4; i < argc; i += 2) {
            const std::string flag = argv[i];
            if (!allowed.count(flag) || i + 1 >= argc || !flags.emplace(flag, argv[i + 1]).second)
                throw std::runtime_error("Invalid, duplicate or incomplete option: " + flag);
        }
        if (flags.count("--train") && flags.at("--train") != "1:10")
            throw std::runtime_error("Training images must be exactly --train 1:10.");
        const auto option = [&](const std::string& flag, std::size_t fallback, std::size_t minimum, std::size_t maximum) {
            return flags.count(flag) ? integer(flags.at(flag), minimum, maximum, flag) : fallback;
        };
        analysis::TenTrainOptions options;
        options.maxEvaluations = option("--max-evaluations", 80, 1, 10000000);
        options.maxCandidates = option("--max-candidates", 24, 1, 1000000);
        options.initialCandidates = option("--initial-candidates", 3, 1, 1000000);
        options.proposalsPerRound = option("--proposals-per-round", 3, 1, 1000000);
        options.maxRounds = option("--max-rounds", 6, 0, 1000000);
        options.patience = option("--patience", 2, 1, 1000000);
        options.workers = option("--workers", 4, 1, 256);
        if (flags.count("--scheduling")) options.scheduling = flags.at("--scheduling");
        if (options.scheduling != "interleaved" && options.scheduling != "sequential")
            throw std::runtime_error("Scheduling must be interleaved or sequential.");
        options.initialBatch = option("--initial-batch", 4, 1, 10);
        options.completionReserve = option("--completion-reserve", 20, 0, 10000000);
        options.intervals.maxContexts = option("--context-budget", 64, 1, 1000000);
        options.intervals.beamWidth = option("--beam", 4, 1, 64);
        options.intervals.refinementRounds = option("--refinement", 1, 0, 10);
        options.intervals.restarts = option("--restarts", 2, 1, 8);
        options.intervals.boundaryBudget = option("--boundary-budget", 24, 0, 8192);
        if (options.intervals.boundaryBudget == 1) throw std::runtime_error("Boundary budget must be 0 or >=2.");
        options.intervals.maxFullEvaluations = option("--max-full-evals", 256, 1, 10000000);
        options.intervals.fullCandidatesPerContext = option("--full-candidates", 8, 1, 1024);
        options.intervals.continuous = true;
        if (flags.count("--global-psnr"))
            options.intervals.globalPsnrThreshold = decimal(flags.at("--global-psnr"), 0, 1000, "--global-psnr");
        if (flags.count("--convergence-tolerance"))
            options.intervals.convergenceTolerance = decimal(flags.at("--convergence-tolerance"), 0,
                std::numeric_limits<double>::max(), "--convergence-tolerance");
        const auto seedLimit = option("--seed-limit", 2, 1, 10);
        const auto data = fs::weakly_canonical(fs::absolute(argv[2]));
        const auto output = fs::weakly_canonical(fs::absolute(argv[3]));
        outputPath(data, output);
        const bool perImageRoi = flags.count("--roi-dir") != 0;
        const auto roiDir = perImageRoi ? fs::weakly_canonical(fs::absolute(flags.at("--roi-dir"))) : data / "mask";
        if(perImageRoi && (contains(roiDir,output) || contains(output,roiDir)))
            throw std::runtime_error("Output must not overlap the per-image ROI directory.");
        fs::create_directories(output.parent_path());
        if (!fs::create_directory(output)) throw std::runtime_error("Output exists; use a new directory.");
        const std::string trainImages = "1,2,3,4,5,6,7,8,9,10";
        {
            auto run = open(output / "RUN.txt");
            run << "completed=0\napplication=" << application << "\ntrain_images=" << trainImages << '\n';
            run.close();
        }
        // The resolver is called only for the fixed training IDs. No held-out
        // path is constructed, inspected or decoded by this training entry.
        const auto originalMask = perImageRoi ? cv::Mat{} : image_io::loadGrayImage((data / "mask/roi_mask.jpg").string());
        Csv roiInputs(output / "roi_inputs.csv", {"image","input_path","roi_path","width","height","roi_pixels"});
        std::vector<analysis::TenTrainImage> images;
        for (int id = 1; id <= 10; ++id) {
            const auto inputPath = image_io::resolveStagedImagePath(data.string(), id);
            const auto roiPath = perImageRoi ? image_io::resolveRoiMaskPath(roiDir.string(),id) : (data / "mask/roi_mask.jpg").string();
            auto image = image_io::loadGrayImage(inputPath,perImageRoi);
            auto mask = perImageRoi ? image_io::loadBinaryRoiMask(roiPath,image) : region_mask::resizeMaskToImage(originalMask, image);
            cv::Mat binary; cv::compare(mask,cv::Scalar(128),binary,cv::CMP_GE);
            roiInputs.row({{"image",std::to_string(id)},{"input_path",inputPath},{"roi_path",roiPath},
                {"width",std::to_string(image.cols)},{"height",std::to_string(image.rows)},
                {"roi_pixels",std::to_string(cv::countNonZero(binary))}});
            images.push_back({id, std::move(image), std::move(mask)});
        }
        roiInputs.close();
        const auto result = analysis::searchCircuitTenTrain(*app, images, options, [&](const analysis::TenTrainProgress& progress) {
            std::cout << application << " training candidates=" << progress.candidates << " evaluations="
                      << progress.evaluations << " hardware=" << progress.hardwareId << " image=" << progress.imageId << std::endl;
        });
        validateResult(result, app->addNodes().size(), options);
        std::vector<std::size_t> ranking(result.candidates.size());
        std::iota(ranking.begin(), ranking.end(), 0);
        std::stable_sort(ranking.begin(), ranking.end(), [&](std::size_t a, std::size_t b) {
            const auto& left = result.candidates[a];
            const auto& right = result.candidates[b];
            if (complete(left) != complete(right)) return complete(left);
            return analysis::betterTenTrainCandidate(left, right);
        });
        const Candidate* selected = nullptr;
        if (!result.bestIndex && !ranking.empty() && complete(result.candidates[ranking.front()]))
            throw std::runtime_error("Solver omitted a complete incumbent.");
        if (result.bestIndex) {
            const auto& best = result.candidates[*result.bestIndex];
            if (!complete(best)) throw std::runtime_error("Solver returned a partial incumbent.");
            if (!ranking.empty() && ranking.front() != *result.bestIndex)
                throw std::runtime_error("Solver incumbent disagrees with deterministic ranking.");
            if (best.numericSuccesses > 0) selected = &best;
        }
        Csv baselines(output / "training_baselines.csv", {"application", "image", "global_mse", "roi_mse", "non_roi_mse", "global_psnr", "roi_psnr", "non_roi_psnr"});
        Csv hardware(output / "candidate_hardware.csv", {"hardware_id", "node_id", "monitor", "bit"});
        Csv ranks(output / "training_ranking.csv", {"hardware_id", "rank", "status", "complete", "evaluated_images", "train_images", "numeric_successes", "worst_roi_drop_score", "mean_roi_drop_score", "installed_nodes", "reason"});
        Csv evaluations(output / "training_evaluations.csv", {"application", "hardware_id", "image", "status", "global_mse", "roi_mse", "non_roi_mse", "baseline_roi_mse", "global_psnr", "roi_psnr", "non_roi_psnr", "roi_drop_score", "elapsed_seconds", "contexts_built", "full_evaluations", "completed_sweeps", "stopped_for_convergence", "reference_cache_hit", "baseline_cache_hit", "phase"});
        Csv configurations(output / "training_configurations.csv", {"hardware_id", "image", "node_id", "monitor", "bit", "lower", "upper"});
        Csv stats(output / "search_stats.csv", {"actual_evaluations", "duplicate_structures_skipped", "reference_builds", "preparation_seconds", "search_seconds", "stop_reason", "scheduling", "initial_batch", "completion_reserve", "effective_completion_reserve", "reference_evaluations", "exploration_evaluations", "completion_evaluations", "completion_candidates", "evaluation_image_order"});
        for (std::size_t i = 0; i < images.size(); ++i) {
            Row row{{"application", application}, {"image", std::to_string(images[i].imageId)}};
            quality(row, result.baselines[i]);
            baselines.row(row);
        }
        for (std::size_t rank = 0; rank < ranking.size(); ++rank) {
            const auto& candidate = result.candidates[ranking[rank]];
            ranks.row({{"hardware_id", candidate.hardwareId}, {"rank", std::to_string(rank + 1)},
                {"status", candidate.status}, {"complete", complete(candidate) ? "1" : "0"},
                {"evaluated_images", std::to_string(candidate.evaluations.size())}, {"train_images", "10"},
                {"numeric_successes", std::to_string(candidate.numericSuccesses)},
                {"worst_roi_drop_score", number(candidate.worstRoiDrop)}, {"mean_roi_drop_score", number(candidate.meanRoiDrop)},
                {"installed_nodes", std::to_string(candidate.hardware.size())}, {"reason", candidate.reason}});
            auto nodes = candidate.hardware;
            std::sort(nodes.begin(), nodes.end(), [](const auto& a, const auto& b) { return a.nodeId < b.nodeId; });
            for (const auto& node : nodes)
                hardware.row({{"hardware_id", candidate.hardwareId}, {"node_id", std::to_string(node.nodeId)},
                              {"monitor", signal(node.monitorInput)}, {"bit", std::to_string(node.bitIndex)}});
            std::vector<const Evaluation*> orderedEvaluations;
            for (const auto& evaluation : candidate.evaluations) orderedEvaluations.push_back(&evaluation);
            std::sort(orderedEvaluations.begin(), orderedEvaluations.end(), [](auto a, auto b) { return a->imageId < b->imageId; });
            for (const auto* evaluation : orderedEvaluations) {
                const auto& e = *evaluation;
                Row row{{"application", application}, {"hardware_id", candidate.hardwareId}, {"image", std::to_string(e.imageId)},
                    {"status", !e.legal ? "no_legal_interval_state" : e.numericEligible ? "numeric_eligible" : "quality_not_met"},
                    {"elapsed_seconds", number(e.elapsedSeconds)}, {"contexts_built", std::to_string(e.stats.contextsBuilt)},
                    {"full_evaluations", std::to_string(e.stats.fullEvaluations)}, {"completed_sweeps", std::to_string(e.stats.completedSweeps)},
                    {"stopped_for_convergence", e.stats.stoppedForConvergence ? "1" : "0"},
                    {"reference_cache_hit", e.stats.referenceCacheHit ? "1" : "0"},
                    {"baseline_cache_hit", e.stats.baselineCacheHit ? "1" : "0"}, {"phase", e.phase}};
                if (e.legal) {
                    quality(row, e.selected.metrics);
                    row["baseline_roi_mse"] = number(e.baseline.roiMse);
                    row["roi_drop_score"] = number(drop(e));
                    for (const auto& node : ordered(e.selected.configuration)) {
                        auto config = configRow(node);
                        config["hardware_id"] = candidate.hardwareId;
                        config["image"] = std::to_string(e.imageId);
                        configurations.row(config);
                    }
                }
                evaluations.row(row);
            }
        }
        Row statistics{{"actual_evaluations", std::to_string(result.actualEvaluations)},
            {"duplicate_structures_skipped", std::to_string(result.duplicateStructuresSkipped)},
            {"reference_builds", std::to_string(result.referenceBuilds)}, {"preparation_seconds", number(result.preparationSeconds)},
            {"search_seconds", number(result.searchSeconds)}, {"stop_reason", result.stopReason}};
        const auto scheduling = scheduleRow(result, options);
        statistics.insert(scheduling.begin(), scheduling.end());
        stats.row(statistics);
        baselines.close(); hardware.close(); ranks.close(); evaluations.close(); configurations.close(); stats.close();
        if (selected) {
            Csv frozenHardware(output / "frozen_hardware.csv", {"node_id", "monitor", "bit"});
            Csv frozenSeeds(output / "frozen_seeds.csv", {"seed_id", "node_id", "monitor", "bit", "lower", "upper"});
            auto nodes = selected->hardware;
            std::sort(nodes.begin(), nodes.end(), [](const auto& a, const auto& b) { return a.nodeId < b.nodeId; });
            for (const auto& node : nodes)
                frozenHardware.row({{"node_id", std::to_string(node.nodeId)}, {"monitor", signal(node.monitorInput)}, {"bit", std::to_string(node.bitIndex)}});
            std::vector<const Evaluation*> eligible;
            for (const auto& evaluation : selected->evaluations)
                if (evaluation.legal && evaluation.numericEligible) eligible.push_back(&evaluation);
            std::sort(eligible.begin(), eligible.end(), [](auto a, auto b) {
                if (drop(*a) != drop(*b)) return drop(*a) > drop(*b);
                return a->imageId < b->imageId;
            });
            std::set<std::string> snapshots;
            for (const auto* evaluation : eligible) {
                if (snapshots.size() == seedLimit) break;
                if (!snapshots.insert(configKey(evaluation->selected.configuration)).second) continue;
                std::ostringstream id;
                id << "train_" << std::setw(2) << std::setfill('0') << evaluation->imageId << "_ten";
                for (const auto& node : ordered(evaluation->selected.configuration)) {
                    auto row = configRow(node);
                    row["seed_id"] = id.str();
                    frozenSeeds.row(row);
                }
            }
            if (snapshots.empty()) throw std::runtime_error("Selected hardware has no eligible training seed.");
            frozenHardware.close(); frozenSeeds.close();
        }
        // Successful completion is committed only after every report closes.
        auto run = open(output / "RUN.txt");
        run << "completed=1\napplication=" << application << "\ntrain_images=" << trainImages
            << "\nroi_mode=" << (perImageRoi ? "per_image" : "legacy_fixed") << "\nroi_dir=" << roiDir.string()
            << "\nselection_status=" << (selected ? "selected" : "no_numeric_hardware")
            << "\nselected_hardware_id=" << (selected ? selected->hardwareId : "")
            << "\nactual_evaluations=" << result.actualEvaluations << "\nstop_reason=" << result.stopReason
            << "\nreference_builds=" << result.referenceBuilds << "\npreparation_seconds=" << number(result.preparationSeconds)
            << "\nsearch_seconds=" << number(result.searchSeconds) << '\n';
        for (const auto& field : scheduling) run << field.first << '=' << field.second << '\n';
        run.close();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "circuit_bitflip_ten_train: " << error.what() << '\n';
        return 1;
    }
}
