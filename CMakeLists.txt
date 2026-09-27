cmake_minimum_required(VERSION 3.16)

project(
    approx_attack_validation
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


find_package(OpenCV REQUIRED)


# =========================================================
# EvoApproxLib 12-bit approximate adders
# =========================================================

add_library(
    evoapprox_units
    STATIC

    third_party/evoapproxlib/add12se_5L8.c
    third_party/evoapproxlib/add12se_5PD.c
    third_party/evoapproxlib/add12se_5PN.c
    third_party/evoapproxlib/add12se_5QC.c
    third_party/evoapproxlib/add12se_5QT.c
    third_party/evoapproxlib/add12se_5RP.c
    third_party/evoapproxlib/add12se_5SB.c
    third_party/evoapproxlib/add12se_5TE.c
    third_party/evoapproxlib/add12se_5Z0.c
)


# =========================================================
# Main framework library
# =========================================================

add_library(
    approx_framework
    STATIC

    src/applications/dct.cpp
    src/applications/dct8_fixed_graph.cpp
    src/applications/dct8_dfg_builder.cpp

    src/approximate/evoapprox_adapter.cpp

    src/analysis/add_sample_collector.cpp
    src/analysis/brute_force_search.cpp
    src/analysis/two_stage_search.cpp
    src/analysis/propagation_analyzer.cpp

    src/io/image_io.cpp
    src/region/region_mask.cpp
    src/metrics/psnr.cpp
    src/core/dfg_graph.cpp
)


target_include_directories(
    approx_framework
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
        ${OpenCV_INCLUDE_DIRS}
)


target_link_libraries(
    approx_framework
    PUBLIC
        evoapprox_units
        ${OpenCV_LIBS}
)


# =========================================================
# Tests
# =========================================================

enable_testing()


add_executable(
    test_dct8_fixed_graph

    tests/test_dct8_fixed_graph.cpp
)


target_link_libraries(
    test_dct8_fixed_graph
    PRIVATE
        approx_framework
)


add_test(
    NAME dct8_fixed_graph
    COMMAND test_dct8_fixed_graph
)

add_executable(
    test_dct_application

    tests/test_dct_application.cpp
)


target_link_libraries(
    test_dct_application
    PRIVATE
        approx_framework
)


add_test(
    NAME dct_application
    COMMAND test_dct_application
)

add_executable(
    test_add_sample_collector


    tests/test_add_sample_collector.cpp
)


target_link_libraries(
    test_add_sample_collector
    PRIVATE
        approx_framework
)


add_test(
    NAME add_sample_collector
    COMMAND test_add_sample_collector
)

add_executable(
    test_dfg_graph

    tests/test_dfg_graph.cpp
)


target_link_libraries(
    test_dfg_graph
    PRIVATE
        approx_framework
)


add_test(
    NAME dfg_graph
    COMMAND test_dfg_graph
)


add_executable(
    test_propagation_analyzer

    tests/test_propagation_analyzer.cpp
)


target_link_libraries(
    test_propagation_analyzer
    PRIVATE
        approx_framework
)


add_test(
    NAME propagation_analyzer
    COMMAND test_propagation_analyzer
)

add_executable(
    test_dct8_dfg_builder

    tests/test_dct8_dfg_builder.cpp
)


target_link_libraries(
    test_dct8_dfg_builder
    PRIVATE
        approx_framework
)


add_test(
    NAME dct8_dfg_builder
    COMMAND test_dct8_dfg_builder
)


add_executable(
    test_brute_force_search

    tests/test_brute_force_search.cpp
)


target_link_libraries(
    test_brute_force_search
    PRIVATE
        approx_framework
)


add_test(
    NAME brute_force_search
    COMMAND test_brute_force_search
)


add_executable(
    test_two_stage_search

    tests/test_two_stage_search.cpp
)


target_link_libraries(
    test_two_stage_search
    PRIVATE
        approx_framework
)


add_test(
    NAME two_stage_search
    COMMAND test_two_stage_search
)

# =========================================================
# DCT structural propagation analysis
# =========================================================

add_executable(
    dct_propagation_analysis

    app/dct_propagation_analysis.cpp
)


target_link_libraries(
    dct_propagation_analysis
    PRIVATE
        approx_framework
)