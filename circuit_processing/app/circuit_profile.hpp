#pragma once

#include "approximate/evoapprox_adapter.hpp"
#include "core/output_bitflip.hpp"
#include <opencv2/core.hpp>
#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

// The profile is deliberately a small explicit execution format, not a C++ parser.
namespace circuit_profile {
using Value = std::int64_t;
constexpr Value limit = (Value{1} << 30) - 1;
inline void demand(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error("Circuit profile: " + message);
}
inline Value checked(Value value) {
    demand(value >= -limit && value <= limit, "intermediate exceeds supported integer width");
    return value;
}
inline int number(const cv::FileNode& node) {
    demand(node.isInt(), "expected integer");
    const double value = node.real();
    demand(value >= -limit && value <= limit, "integer exceeds supported width");
    return static_cast<int>(value);
}
inline std::string word(const cv::FileNode& node) {
    demand(node.isString(), "expected string");
    return static_cast<std::string>(node);
}
inline int optionalNumber(const cv::FileNode& map, const char* key, int fallback) {
    return map[key].empty() ? fallback : number(map[key]);
}
inline void object(const cv::FileNode& node) { demand(node.isMap(), "expected object"); }
struct BaselineNode {
    bool exact = true;
    approximate::ApproxUnitId unit = approximate::ApproxUnitId::Add12se5RP;
};
inline BaselineNode unit(const std::string& name) {
    using U = approximate::ApproxUnitId;
    static const std::map<std::string,U> units{{"5RP",U::Add12se5RP},{"5QC",U::Add12se5QC},
        {"5L8",U::Add12se5L8},{"5QT",U::Add12se5QT},{"5TE",U::Add12se5TE},
        {"5PD",U::Add12se5PD},{"5PN",U::Add12se5PN},{"5SB",U::Add12se5SB},{"5Z0",U::Add12se5Z0}};
    if (name == "exact") return {};
    const auto it = units.find(name);
    demand(it != units.end(), "unknown arithmetic unit: " + name);
    return {false,it->second};
}
struct Input { int dx, dy, scale, bias; };
struct Operand {
    int index = -1;
    Value constant = 0;
    Value get(const std::vector<Value>& values) const { return index < 0 ? constant : values.at(index); }
};
struct Operation {
    std::string op;
    std::vector<Operand> args;
    int node = -1;
    int factor = 0, bits = 0, minimum = 0, maximum = 255;
};
struct Profile {
    std::string name, application;
    std::vector<BaselineNode> baseline;
    std::vector<Input> inputs;
    std::vector<Operation> operations;
    int output = -1;
    bool reflect = false;

    static Profile load(const std::string& path, const std::string& app) {
        cv::FileStorage storage(path, cv::FileStorage::READ | cv::FileStorage::FORMAT_JSON);
        demand(storage.isOpened(), "cannot open " + path);
        const auto root = storage.root(); object(root);
        demand(number(root["schema"]) == 1, "unsupported schema");
        Profile p;
        p.name = word(root["name"]); demand(!p.name.empty(), "empty name");
        p.application = word(root["application"]);
        demand(p.application == app, "application differs from CLI");
        int count = app == "sharpen" ? 4 : app == "conv" ? 8 : app == "sobel" ? 11 : app == "dct" ? 32 : 0;
        if (app == "graph") {
            const auto graph = root["graph"]; object(graph);
            const auto border = word(graph["border"]);
            demand(border == "replicate" || border == "reflect101", "unsupported border");
            p.reflect = border == "reflect101";
            std::map<std::string,int> ids;
            const auto inputs = graph["inputs"]; object(inputs);
            demand(inputs.size()>0, "inputs must be nonempty");
            for (auto it = inputs.begin(); it != inputs.end(); ++it) {
                const auto input = *it; object(input);
                const auto id = input.name();
                demand(!id.empty() && ids.emplace(id,static_cast<int>(ids.size())).second, "duplicate input ID");
                p.inputs.push_back({optionalNumber(input,"dx",0),optionalNumber(input,"dy",0),
                                    optionalNumber(input,"scale",1),optionalNumber(input,"bias",0)});
            }
            const auto operations = graph["operations"]; demand(operations.isSeq() && operations.size()>0, "operations must be nonempty array");
            for (const auto& entry : operations) {
                object(entry);
                const auto id = word(entry["id"]);
                demand(!id.empty() && !ids.count(id), "duplicate operation ID");
                Operation operation; operation.op = word(entry["op"]);
                const auto& op = operation.op;
                demand(op=="add" || op=="sub" || op=="abs" || op=="scale" || op=="shift" || op=="clamp", "unknown operation");
                const auto args = entry["args"];
                demand(args.isSeq() && args.size() == ((op=="add" || op=="sub") ? 2u : 1u), "wrong argument count");
                for (const auto& arg : args) {
                    if (arg.isString()) {
                        const auto ref = ids.find(word(arg));
                        demand(ref != ids.end(), "operand must reference preceding input or operation");
                        operation.args.push_back({ref->second,0});
                    } else operation.args.push_back({-1,number(arg)});
                }
                if (op=="add" || op=="sub") {
                    demand(id == std::to_string(count), "arithmetic IDs must be consecutive decimal strings from zero");
                    operation.node = count++;
                } else {
                    demand(id.find_first_not_of("0123456789") != std::string::npos, "nonarithmetic IDs must be names");
                }
                if (op=="scale") operation.factor = number(entry["factor"]);
                if (op=="shift") {
                    operation.bits = number(entry["bits"]);
                    demand(operation.bits >= 0 && operation.bits <= 30, "shift bits must be in [0,30]");
                }
                if (op=="clamp") {
                    operation.minimum = number(entry["min"]); operation.maximum = number(entry["max"]);
                    demand(operation.minimum <= operation.maximum, "invalid clamp range");
                }
                ids.emplace(id,static_cast<int>(ids.size()));
                p.operations.push_back(operation);
            }
            demand(count>0, "graph must contain arithmetic nodes");
            const auto out = ids.find(word(graph["output"]));
            demand(out != ids.end(), "unknown output reference"); p.output = out->second;
        } else demand(count != 0, "unknown application");
        const auto baseline = root["baseline"]; demand(baseline.isSeq(), "baseline must be array");
        demand(baseline.size() == static_cast<size_t>(count), "baseline must specify every arithmetic node");
        p.baseline.resize(count);
        std::set<int> seen;
        for (const auto& item : baseline) {
            object(item); const int node = number(item["node"]);
            demand(node >= 0 && node < count && seen.insert(node).second, "invalid or duplicate baseline node");
            p.baseline[node] = unit(word(item["unit"]));
        }
        return p;
    }

    static int coordinate(Value value, int size, bool reflect) {
        if (!reflect) return static_cast<int>(std::max(Value{0},std::min(Value{size-1},value)));
        if (size == 1) return 0;
        const Value period = 2*(Value{size}-1);
        value = (value % period + period) % period;
        return static_cast<int>(value < size ? value : period-value);
    }
    cv::Mat run(const cv::Mat& image, const std::vector<BaselineNode>& config,
                const cv::Rect& roi = {}, int flipNode = -2, int bit = 0) const {
        cv::Mat result(image.size(),CV_8UC1);
        for (int y=0; y<image.rows; ++y) for (int x=0; x<image.cols; ++x) {
            std::vector<Value> values; values.reserve(inputs.size()+operations.size());
            for (const auto& input : inputs) {
                const auto pixel = image.at<unsigned char>(coordinate(Value{y}+input.dy,image.rows,reflect),
                                                            coordinate(Value{x}+input.dx,image.cols,reflect));
                values.push_back(checked(Value{pixel}*input.scale+input.bias));
            }
            for (const auto& operation : operations) {
                const Value a = operation.args[0].get(values);
                Value out = 0;
                if (operation.node >= 0) {
                    const Value b = operation.args[1].get(values) * (operation.op=="sub" ? -1 : 1);
                    const auto& baselineNode = config.at(operation.node);
                    out = baselineNode.exact ? checked(a+b) : approximate::addSigned12(static_cast<int>(a),static_cast<int>(b),baselineNode.unit);
                    if (operation.node == flipNode && roi.contains(cv::Point(x,y))) {
                        demand(out >= -4096 && out <= 4095, "bit-flip output exceeds signed13 width");
                        out = core::flipNonSignOutputBit(static_cast<int>(out),bit);
                    }
                } else if (operation.op=="abs") out = a < 0 ? -a : a;
                else if (operation.op=="scale") out = a*operation.factor;
                else if (operation.op=="shift") {
                    // Arithmetic right shift, including negative values, without implementation-defined C++ behavior.
                    const Value divisor = Value{1} << operation.bits;
                    out = a >= 0 ? a/divisor : -((-a+divisor-1)/divisor);
                } else if (operation.op=="clamp") out = std::clamp(a,Value{operation.minimum},Value{operation.maximum});
                values.push_back(checked(out));
            }
            result.at<unsigned char>(y,x) = static_cast<unsigned char>(std::clamp(values.at(output),Value{0},Value{255}));
        }
        return result;
    }
};
} // namespace circuit_profile
