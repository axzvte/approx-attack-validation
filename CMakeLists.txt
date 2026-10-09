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
    src/applications/sharpen.cpp
    src/applications/sobel.cpp

    src/approximate/evoapprox_adapter.cpp

    src/analysis/add_sample_collector.cpp
    src/analysis/brute_force_search.cpp
    src/analysis/two_stage_search.cpp
    src/analysis/stage1_screening.cpp
    src/analysis/dct_stage1_evaluator.cpp
    src/analysis/dct_node_sensitivity.cpp
    src/analysis/dct_monitor_input_analyzer.cpp
    src/analysis/dct_monitor_signal_analyzer.cpp
    src/analysis/dct_interval_search_space_generator.cpp
    src/analysis/dct_interval_fast_evaluator.cpp
    src/analysis/dct_interval_full_validator.cpp
    src/analysis/dct_multi_node_interval_optimizer.cpp
    src/analysis/dct_multi_state_joint_search.cpp
    src/analysis/search_space_statistics.cpp
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
    test_sharpen_application

    tests/test_sharpen_application.cpp
)


target_link_libraries(
    test_sharpen_application
    PRIVATE
        approx_framework
)


add_test(
    NAME sharpen_application
    COMMAND test_sharpen_application
)


add_executable(
    test_sobel_application

    tests/test_sobel_application.cpp
)


target_link_libraries(
    test_sobel_application
    PRIVATE
        approx_framework
)


add_test(
    NAME sobel_application
    COMMAND test_sobel_application
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


add_executable(
    test_stage1_screening

    tests/test_stage1_screening.cpp
)


target_link_libraries(
    test_stage1_screening
    PRIVATE
        approx_framework
)


add_test(
    NAME stage1_screening
    COMMAND test_stage1_screening
)


add_executable(
    test_dct_stage1_evaluator

    tests/test_dct_stage1_evaluator.cpp
)


target_link_libraries(
    test_dct_stage1_evaluator
    PRIVATE
        approx_framework
)


add_test(
    NAME dct_stage1_evaluator
    COMMAND test_dct_stage1_evaluator
)


add_executable(
    test_search_space_statistics

    tests/test_search_space_statistics.cpp
)


target_link_libraries(
    test_search_space_statistics
    PRIVATE
        approx_framework
)


add_test(
    NAME search_space_statistics
    COMMAND test_search_space_statistics
)

add_executable(
    test_dct_node_sensitivity

    tests/test_dct_node_sensitivity.cpp
)


target_link_libraries(
    test_dct_node_sensitivity
    PRIVATE
        approx_framework
)


add_test(
    NAME dct_node_sensitivity
    COMMAND test_dct_node_sensitivity
)


add_executable(
    test_dct_monitor_input_analyzer

    tests/test_dct_monitor_input_analyzer.cpp
)


target_link_libraries(
    test_dct_monitor_input_analyzer
    PRIVATE
        approx_framework
)


add_test(
    NAME dct_monitor_input_analyzer
    COMMAND test_dct_monitor_input_analyzer
)


add_executable(
    test_dct_interval_search_space_generator

    tests/test_dct_interval_search_space_generator.cpp
)


target_link_libraries(
    test_dct_interval_search_space_generator
    PRIVATE
        approx_framework
)


add_test(
    NAME dct_interval_search_space_generator
    COMMAND test_dct_interval_search_space_generator
)


add_executable(
    test_dct_interval_fast_evaluator

    tests/test_dct_interval_fast_evaluator.cpp
)


target_link_libraries(
    test_dct_interval_fast_evaluator
    PRIVATE
        approx_framework
)


add_test(
    NAME dct_interval_fast_evaluator
    COMMAND test_dct_interval_fast_evaluator
)


add_executable(
    test_dct_interval_full_validator

    tests/test_dct_interval_full_validator.cpp
)


target_link_libraries(
    test_dct_interval_full_validator
    PRIVATE
        approx_framework
)


add_test(
    NAME dct_interval_full_validator
    COMMAND test_dct_interval_full_validator
)


add_executable(
    test_dct_multi_node_interval_optimizer

    tests/test_dct_multi_node_interval_optimizer.cpp
)


target_link_libraries(
    test_dct_multi_node_interval_optimizer
    PRIVATE
        approx_framework
)


add_test(
    NAME dct_multi_node_interval_optimizer
    COMMAND test_dct_multi_node_interval_optimizer
)


add_executable(
    test_dct_monitor_signal_analyzer

    tests/test_dct_monitor_signal_analyzer.cpp
)


target_link_libraries(
    test_dct_monitor_signal_analyzer
    PRIVATE
        approx_framework
)


add_test(
    NAME dct_monitor_signal_analyzer
    COMMAND test_dct_monitor_signal_analyzer
)


add_executable(
    test_dct_multi_state_joint_search

    tests/test_dct_multi_state_joint_search.cpp
)


target_link_libraries(
    test_dct_multi_state_joint_search
    PRIVATE
        approx_framework
)


add_test(
    NAME dct_multi_state_joint_search
    COMMAND test_dct_multi_state_joint_search
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

# =========================================================
# DCT exact brute-force search-space statistics
# =========================================================

add_executable(
    dct_search_space_statistics

    app/dct_search_space_statistics.cpp
)


target_link_libraries(
    dct_search_space_statistics
    PRIVATE
        approx_framework
)


# =========================================================
# DCT single-node sensitivity analysis
# =========================================================

add_executable(
    dct_node_sensitivity_analysis

    app/dct_node_sensitivity_analysis.cpp
)


target_link_libraries(
    dct_node_sensitivity_analysis
    PRIVATE
        approx_framework
)


# =========================================================
# DCT monitor-input regional separability analysis
# =========================================================

add_executable(
    dct_monitor_input_analysis

    app/dct_monitor_input_analysis.cpp
)


target_link_libraries(
    dct_monitor_input_analysis
    PRIVATE
        approx_framework
)


# =========================================================
# DCT interval search-space generation (Module 1)
# =========================================================

add_executable(
    dct_interval_search_space_generation

    app/dct_interval_search_space_generation.cpp
)


target_link_libraries(
    dct_interval_search_space_generation
    PRIVATE
        approx_framework
)


# =========================================================
# DCT fast interval evaluation (Module 2-A core)
# =========================================================

add_executable(
    dct_interval_fast_evaluation

    app/dct_interval_fast_evaluation.cpp
)


target_link_libraries(
    dct_interval_fast_evaluation
    PRIVATE
        approx_framework
)


# =========================================================
# DCT fast-to-full interval validation (Module 2-B)
# =========================================================

add_executable(
    dct_interval_full_validation

    app/dct_interval_full_validation.cpp
)


target_link_libraries(
    dct_interval_full_validation
    PRIVATE
        approx_framework
)


# =========================================================
# DCT cross-image unit / interval validation
# =========================================================

add_executable(
    dct_unit_cross_image_validation

    app/dct_unit_cross_image_validation.cpp
)


target_link_libraries(
    dct_unit_cross_image_validation
    PRIVATE
        approx_framework
)


# =========================================================
# DCT single-image multi-node interval optimization
# =========================================================

add_executable(
    dct_multi_node_interval_optimization

    app/dct_multi_node_interval_optimization.cpp
)


target_link_libraries(
    dct_multi_node_interval_optimization
    PRIVATE
        approx_framework
)


# =========================================================
# DCT monitor-signal comparison:
# input1 / input2 / baseline 5RP output
# =========================================================

add_executable(
    dct_monitor_signal_analysis

    app/dct_monitor_signal_analysis.cpp
)


target_link_libraries(
    dct_monitor_signal_analysis
    PRIVATE
        approx_framework
)


# =========================================================
# DCT sparse baseline validation
# =========================================================

add_executable(
    dct_sparse_baseline_validation

    app/dct_sparse_baseline_validation.cpp
)


target_link_libraries(
    dct_sparse_baseline_validation
    PRIVATE
        approx_framework
)


# =========================================================
# DCT multi-state joint interval search (Module 3-B)
# =========================================================

add_executable(
    dct_multi_state_joint_search

    app/dct_multi_state_joint_search.cpp
)


target_link_libraries(
    dct_multi_state_joint_search
    PRIVATE
        approx_framework
)


# =========================================================
# DCT baseline-only quality sweep
# =========================================================

add_executable(
    dct_baseline_sweep

    app/dct_baseline_sweep.cpp
)


target_link_libraries(
    dct_baseline_sweep
    PRIVATE
        approx_framework
)


# =========================================================
# Image Sharpening through the shared multi-state joint search
# =========================================================

add_executable(
    sharpen_multi_state_joint_search

    app/sharpen_multi_state_joint_search.cpp
)


target_link_libraries(
    sharpen_multi_state_joint_search
    PRIVATE
        approx_framework
)


# =========================================================
# Sobel single-node ROI sensitivity analysis
# =========================================================

add_executable(
    sobel_node_sensitivity_analysis

    app/sobel_node_sensitivity_analysis.cpp
)


target_link_libraries(
    sobel_node_sensitivity_analysis
    PRIVATE
        approx_framework
)


# =========================================================
# Sobel through the shared multi-state joint search
# =========================================================

add_executable(
    sobel_multi_state_joint_search

    app/sobel_multi_state_joint_search.cpp
)


target_link_libraries(
    sobel_multi_state_joint_search
    PRIVATE
        approx_framework
)


# =========================================================
# Sobel provisional baseline validation
# =========================================================

add_executable(
    sobel_baseline_validation

    app/sobel_baseline_validation.cpp
)


target_link_libraries(
    sobel_baseline_validation
    PRIVATE
        approx_framework
)


# =========================================================
# Sobel exhaustive sparse-baseline sweep
# =========================================================

add_executable(
    sobel_baseline_sweep

    app/sobel_baseline_sweep.cpp
)


target_link_libraries(
    sobel_baseline_sweep
    PRIVATE
        approx_framework
)
