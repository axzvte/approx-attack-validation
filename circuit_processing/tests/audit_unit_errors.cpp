// Exhaustive audit of the production adapter, without image inputs.
// error = addSigned12(a, b, unit) - (a + b); both operands cover [-2048, 2047].
// Standalone rerun from circuit_processing (Linux/WSL, gcc/g++/python3):
//   out=$(mktemp -d)
//   for f in third_party/evoapproxlib/*.c; do
//     gcc -O3 -c "$f" -o "$out/$(basename "$f" .c).o"
//   done
//   g++ -std=c++17 -O3 -Iinclude tests/audit_unit_errors.cpp \
//     src/approximate/evoapprox_adapter.cpp "$out"/*.o -o "$out/audit"
//   "$out/audit" > "$out/bounds.json"
//   python3 - "$out/bounds.json" <<'PY'
// import hashlib, json, pathlib, sys
// data = json.loads(pathlib.Path(sys.argv[1]).read_text())
// paths = [pathlib.Path('src/approximate/evoapprox_adapter.cpp')]
// paths += sorted(pathlib.Path('third_party/evoapproxlib').glob('*.c'))
// data['source_hash_algorithm'] = 'sha256-lf-normalized'
// data['sources'] = {p.as_posix(): hashlib.sha256(p.read_bytes().replace(b'\r\n', b'\n')).hexdigest() for p in paths}
// pathlib.Path('configs/unit_error_bounds.json').write_text(json.dumps(data, indent=2) + '\n')
// PY

#include "approximate/evoapprox_adapter.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

int main()
{
    using approximate::ApproxUnitId;
    struct Unit { const char* name; ApproxUnitId id; };
    const Unit units[] = {
        {"5L8", ApproxUnitId::Add12se5L8},
        {"5PD", ApproxUnitId::Add12se5PD},
        {"5PN", ApproxUnitId::Add12se5PN},
        {"5QC", ApproxUnitId::Add12se5QC},
        {"5QT", ApproxUnitId::Add12se5QT},
        {"5RP", ApproxUnitId::Add12se5RP},
        {"5SB", ApproxUnitId::Add12se5SB},
        {"5TE", ApproxUnitId::Add12se5TE},
        {"5Z0", ApproxUnitId::Add12se5Z0},
    };
    constexpr std::uint64_t expectedPairs = 4096ULL * 4096ULL;
    std::cout << "{\n  \"schema\": 1,\n  \"input_min\": -2048,\n"
              << "  \"input_max\": 2047,\n  \"output_bits\": 13,\n"
              << "  \"operand_pairs_per_unit\": " << expectedPairs << ",\n"
              << "  \"error_definition\": \"adapter_output - (a + b)\",\n"
              << "  \"units\": {\n";
    bool first = true;
    for (const auto& unit : units)
    {
        int minError = std::numeric_limits<int>::max();
        int maxError = std::numeric_limits<int>::min();
        std::uint64_t pairs = 0;
        for (int a = -2048; a <= 2047; ++a)
        {
            for (int b = -2048; b <= 2047; ++b)
            {
                const int value = approximate::addSigned12(a, b, unit.id);
                if (value < -4096 || value > 4095) return EXIT_FAILURE;
                const int error = value - (a + b);
                minError = std::min(minError, error);
                maxError = std::max(maxError, error);
                ++pairs;
            }
        }
        if (pairs != expectedPairs) return EXIT_FAILURE;
        if (!first) std::cout << ",\n";
        first = false;
        const int wce = std::max(std::abs(minError), std::abs(maxError));
        std::cout << "    \"" << unit.name << "\": {\"min_error\": " << minError
                  << ", \"max_error\": " << maxError << ", \"wce\": " << wce << "}";
        std::cerr << unit.name << ": " << pairs << " pairs, error [" << minError
                  << ", " << maxError << "], WCE " << wce << '\n';
    }
    std::cout << "\n  }\n}\n";
    return EXIT_SUCCESS;
}
