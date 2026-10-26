cmake_minimum_required(VERSION 3.16)

project(
    approx_attack_validation
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


find_package(OpenCV REQUIRED)
find_package(Threads REQUIRED)


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
    src/applications/conv.cpp

    src/approximate/evoapprox_adapter.cpp

    src/analysis/add_sample_collector.cpp
    src/analysis/sharpen_bitflip_search.cpp
    src/analysis/sharpen_bitflip_diagnostics.cpp
    src/analysis/sharpen_bitflip_joint.cpp
    src/analysis/sharpen_bitflip_fixed.cpp
    src/analysis/circuit_bitflip_joint.cpp
    src/analysis/circuit_bitflip_ten_train.cpp
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
        Threads::Threads
        ${OpenCV_LIBS}
)


# =========================================================
# Tests
# =========================================================

enable_testing()

add_executable(sharpen_bitflip_fixed app/sharpen_bitflip_fixed.cpp)
target_link_libraries(sharpen_bitflip_fixed PRIVATE approx_framework)
add_executable(test_sharpen_bitflip_fixed tests/test_sharpen_bitflip_fixed.cpp)
target_link_libraries(test_sharpen_bitflip_fixed PRIVATE approx_framework)
add_test(NAME sharpen_bitflip_fixed COMMAND test_sharpen_bitflip_fixed)
add_executable(test_sharpen_bitflip_coverage tests/test_sharpen_bitflip_coverage.cpp)
target_link_libraries(test_sharpen_bitflip_coverage PRIVATE approx_framework)
add_test(NAME sharpen_bitflip_coverage COMMAND test_sharpen_bitflip_coverage)

add_executable(circuit_bitflip_joint app/circuit_bitflip_joint.cpp)
target_link_libraries(circuit_bitflip_joint PRIVATE approx_framework)
add_executable(circuit_bitflip_fixed app/circuit_bitflip_fixed.cpp)
target_link_libraries(circuit_bitflip_fixed PRIVATE approx_framework)
add_executable(test_circuit_bitflip_fixed tests/test_circuit_bitflip_fixed.cpp)
target_link_libraries(test_circuit_bitflip_fixed PRIVATE approx_framework)
add_test(NAME circuit_bitflip_fixed COMMAND test_circuit_bitflip_fixed)
add_executable(test_circuit_bitflip tests/test_circuit_bitflip.cpp)
target_link_libraries(test_circuit_bitflip PRIVATE approx_framework)
add_test(NAME circuit_bitflip COMMAND test_circuit_bitflip)
add_executable(test_circuit_bitflip_joint tests/test_circuit_bitflip_joint.cpp)
target_link_libraries(test_circuit_bitflip_joint PRIVATE approx_framework)
add_test(NAME circuit_bitflip_joint COMMAND test_circuit_bitflip_joint)
add_executable(test_bitflip_sensitivity tests/test_bitflip_sensitivity.cpp)
target_link_libraries(test_bitflip_sensitivity PRIVATE approx_framework)
add_test(NAME bitflip_sensitivity COMMAND test_bitflip_sensitivity)
add_executable(test_circuit_bitflip_continuation tests/test_circuit_bitflip_continuation.cpp)
target_link_libraries(test_circuit_bitflip_continuation PRIVATE approx_framework)
add_test(NAME circuit_bitflip_continuation COMMAND test_circuit_bitflip_continuation)
add_executable(circuit_bitflip_ten_train app/circuit_bitflip_ten_train.cpp)
target_link_libraries(circuit_bitflip_ten_train PRIVATE approx_framework Threads::Threads)
add_executable(test_circuit_bitflip_ten_train tests/test_circuit_bitflip_ten_train.cpp)
target_link_libraries(test_circuit_bitflip_ten_train PRIVATE approx_framework Threads::Threads)
add_test(NAME circuit_bitflip_ten_train COMMAND test_circuit_bitflip_ten_train)

add_executable(sharpen_bitflip_joint app/sharpen_bitflip_joint.cpp)
target_link_libraries(sharpen_bitflip_joint PRIVATE approx_framework)
add_executable(test_sharpen_bitflip_joint tests/test_sharpen_bitflip_joint.cpp)
target_link_libraries(test_sharpen_bitflip_joint PRIVATE approx_framework)
add_test(NAME sharpen_bitflip_joint COMMAND test_sharpen_bitflip_joint)

add_executable(test_sharpen_bitflip_diagnostics tests/test_sharpen_bitflip_diagnostics.cpp)
target_link_libraries(test_sharpen_bitflip_diagnostics PRIVATE approx_framework)
add_test(NAME sharpen_bitflip_diagnostics COMMAND test_sharpen_bitflip_diagnostics)

add_executable(sharpen_bitflip_diagnostics app/sharpen_bitflip_diagnostics.cpp)
target_link_libraries(sharpen_bitflip_diagnostics PRIVATE approx_framework)
if(UNIX)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME circuit_bitflip_fixed_cli
            COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/test_circuit_bitflip_fixed_cli.py
            $<TARGET_FILE:circuit_bitflip_fixed>)
        add_test(NAME circuit_bitflip_hardware_pipeline
            COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/test_circuit_bitflip_hardware_pipeline.py)
        add_test(NAME sharpen_bitflip_hardware_cli
            COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/test_sharpen_bitflip_hardware_cli.py
            $<TARGET_FILE:sharpen_bitflip_fixed>)
        add_test(NAME sharpen_bitflip_hardware_pipeline
            COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/test_sharpen_bitflip_hardware_pipeline.py)
        add_test(NAME bitflip_workflow
            COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/test_bitflip_workflow.py)
        add_test(NAME bitflip_workflow_pipeline
            COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/test_bitflip_workflow_pipeline.py)
        add_test(NAME sobel_continuation_pipeline
            COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/test_sobel_continuation_pipeline.py)
        add_test(NAME ten_train_report
            COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/test_ten_train_report.py)
        add_test(NAME circuit_bitflip_ten_train_pipeline
            COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/test_circuit_bitflip_ten_train_pipeline.py)
        add_test(NAME circuit_bitflip_ten_train_batch
            COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/test_circuit_bitflip_ten_train_batch.py)
        add_test(NAME circuit_bitflip_ten_train_cli
            COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/test_circuit_bitflip_ten_train_cli.py
            $<TARGET_FILE:circuit_bitflip_ten_train>)
        add_test(NAME circuit_bitflip_continuation_cli
            COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/test_circuit_bitflip_continuation_cli.py
            $<TARGET_FILE:circuit_bitflip_joint> $<TARGET_FILE:circuit_bitflip_fixed>)
        add_test(NAME sharpen_bitflip_coverage_cli
            COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/test_sharpen_bitflip_coverage_cli.py
            $<TARGET_FILE:sharpen_bitflip_joint>)
        add_test(NAME circuit_bitflip_cli
            COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/test_circuit_bitflip_cli.py
            $<TARGET_FILE:circuit_bitflip_joint>)
        add_test(NAME circuit_bitflip_batch
            COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/test_circuit_bitflip_batch.py)
        add_test(NAME sharpen_bitflip_batch
            COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/test_sharpen_bitflip_batch.py)
        add_test(NAME sharpen_bitflip_joint_cli
            COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/test_sharpen_bitflip_joint_cli.py
            $<TARGET_FILE:sharpen_bitflip_joint>)
        add_test(NAME bitflip_trigger_intervals
            COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/test_bitflip_trigger_intervals.py)
        add_test(NAME sharpen_bitflip_diagnostics_cli
            COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/test_sharpen_bitflip_diagnostics_cli.py
            $<TARGET_FILE:sharpen_bitflip_diagnostics>)
    endif()
endif()

add_executable(test_sharpen_bitflip tests/test_sharpen_bitflip.cpp)
target_link_libraries(test_sharpen_bitflip PRIVATE approx_framework)
add_test(NAME sharpen_bitflip COMMAND test_sharpen_bitflip)

add_executable(test_sharpen_bitflip_search tests/test_sharpen_bitflip_search.cpp)
target_link_libraries(test_sharpen_bitflip_search PRIVATE approx_framework)
add_test(NAME sharpen_bitflip_search COMMAND test_sharpen_bitflip_search)

add_executable(sharpen_bitflip_validation app/sharpen_bitflip_validation.cpp)
target_link_libraries(sharpen_bitflip_validation PRIVATE approx_framework)
if(UNIX AND EXISTS "/dev/full")
    add_test(NAME sharpen_bitflip_report_failure
        COMMAND bash ${PROJECT_SOURCE_DIR}/tests/test_sharpen_bitflip_report_failure.sh
        $<TARGET_FILE:sharpen_bitflip_validation> ${PROJECT_SOURCE_DIR}/data)
endif()
if(UNIX)
    add_test(NAME sharpen_bitflip_fixed_cli
        COMMAND bash ${PROJECT_SOURCE_DIR}/tests/test_sharpen_bitflip_fixed_cli.sh
        $<TARGET_FILE:sharpen_bitflip_validation> ${PROJECT_SOURCE_DIR}/data)
endif()


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
    test_conv_application

    tests/test_conv_application.cpp
)


target_link_libraries(
    test_conv_application
    PRIVATE
        approx_framework
)


add_test(
    NAME conv_application
    COMMAND test_conv_application
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


# =========================================================
# 3x3 Conv provisional baseline validation
# =========================================================

add_executable(
    conv_baseline_validation

    app/conv_baseline_validation.cpp
)


target_link_libraries(
    conv_baseline_validation
    PRIVATE
        approx_framework
)


# =========================================================
# 3x3 Conv single-node ROI sensitivity analysis
# =========================================================

add_executable(
    conv_node_sensitivity_analysis

    app/conv_node_sensitivity_analysis.cpp
)


target_link_libraries(
    conv_node_sensitivity_analysis
    PRIVATE
        approx_framework
)


# =========================================================
# 3x3 Conv through the shared multi-state joint search
# =========================================================

add_executable(
    conv_multi_state_joint_search

    app/conv_multi_state_joint_search.cpp
)


target_link_libraries(
    conv_multi_state_joint_search
    PRIVATE
        approx_framework
)


# =========================================================
# 3x3 Conv approximate-unit baseline sweep
# =========================================================

add_executable(
    conv_baseline_unit_sweep

    app/conv_baseline_unit_sweep.cpp
)


target_link_libraries(
    conv_baseline_unit_sweep
    PRIVATE
        approx_framework
)


# =========================================================
# 3x3 Conv exhaustive baseline sweep
# =========================================================

add_executable(
    conv_baseline_sweep

    app/conv_baseline_sweep.cpp
)


target_link_libraries(
    conv_baseline_sweep
    PRIVATE
        approx_framework
)


# =========================================================
# Conv fixed-hardware configuration statistics
# =========================================================

add_executable(
    conv_hardware_config_statistics

    app/conv_hardware_config_statistics.cpp
)


target_link_libraries(
    conv_hardware_config_statistics
    PRIVATE
        approx_framework
)


# =========================================================
# Conv Stage2 fixed-hardware screening
# =========================================================

add_executable(
    conv_stage2_hardware_screening

    app/conv_stage2_hardware_screening.cpp
)


target_link_libraries(
    conv_stage2_hardware_screening
    PRIVATE
        approx_framework
)


# =========================================================
# Conv Stage3 final fixed-hardware validation
# =========================================================

add_executable(
    conv_stage3_final_validation

    app/conv_stage3_final_validation.cpp
)


target_link_libraries(
    conv_stage3_final_validation
    PRIVATE
        approx_framework
)


# =========================================================
# Generic fixed-hardware configuration statistics
# =========================================================

add_executable(
    hardware_config_statistics

    app/hardware_config_statistics.cpp
)


target_link_libraries(
    hardware_config_statistics
    PRIVATE
        approx_framework
)


# =========================================================
# Sharpening Stage2 fixed-hardware screening
# =========================================================

add_executable(
    sharpen_stage2_hardware_screening

    app/sharpen_stage2_hardware_screening.cpp
)


target_link_libraries(
    sharpen_stage2_hardware_screening
    PRIVATE
        approx_framework
)


# =========================================================
# Sharpening Stage3 final fixed-hardware validation
# =========================================================

add_executable(
    sharpen_stage3_final_validation

    app/sharpen_stage3_final_validation.cpp
)

target_link_libraries(
    sharpen_stage3_final_validation
    PRIVATE
        approx_framework
)


# =========================================================
# Automatic DCT/Sobel fixed-hardware pipeline
# =========================================================

add_executable(
    automatic_fixed_hardware_pipeline

    app/automatic_fixed_hardware_pipeline.cpp
)


target_link_libraries(
    automatic_fixed_hardware_pipeline
    PRIVATE
        approx_framework
)


# =========================================================
# DCT/Sobel Stage3 old-H* + new-ROI validation
# =========================================================

add_executable(
    fixed_hardware_stage3_validation

    app/fixed_hardware_stage3_validation.cpp
)

target_link_libraries(
    fixed_hardware_stage3_validation
    PRIVATE
        approx_framework
)


# =========================================================
# PPT-ready Stage3 image export
# =========================================================

add_executable(
    ppt_stage3_export

    app/ppt_stage3_export.cpp
)

target_link_libraries(
    ppt_stage3_export
    PRIVATE
        approx_framework
)
