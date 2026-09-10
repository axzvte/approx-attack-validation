cmake_minimum_required(VERSION 3.16)

project(approx_attack_validation LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenCV REQUIRED)

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

add_executable(
    run_validation

    app/main.cpp

    src/io/image_io.cpp
    src/region/region_mask.cpp

    src/processing/exact_sharpen.cpp
    src/processing/approx_sharpen.cpp
    src/processing/triggered_sharpen.cpp
    src/processing/exact_sobel.cpp

    src/approximate/evoapprox_adapter.cpp

    src/analysis/intermediate_analysis.cpp
    src/analysis/trigger_search.cpp
    src/analysis/sobel_intermediate_analysis.cpp
    src/analysis/interval_attack_search.cpp

    src/metrics/psnr.cpp
)

target_include_directories(
    run_validation
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(
    run_validation
    PRIVATE
        evoapprox_units
        ${OpenCV_LIBS}
)

add_executable(
    baseline_benchmark

    app/baseline_benchmark.cpp

    src/io/image_io.cpp

    src/processing/exact_sharpen.cpp
    src/processing/approx_sharpen.cpp
    
    src/region/region_mask.cpp

    src/approximate/evoapprox_adapter.cpp

    src/metrics/psnr.cpp
)


target_include_directories(
    baseline_benchmark
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${OpenCV_INCLUDE_DIRS}
)


target_link_libraries(
    baseline_benchmark
    PRIVATE
        evoapprox_units
        ${OpenCV_LIBS}
)

add_executable(
    test_fast_psnr

    app/test_fast_psnr.cpp

    src/io/image_io.cpp
    src/region/region_mask.cpp

    src/processing/exact_sharpen.cpp

    src/approximate/evoapprox_adapter.cpp

    src/metrics/psnr.cpp
)


target_include_directories(
    test_fast_psnr
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${OpenCV_INCLUDE_DIRS}
)


target_link_libraries(
    test_fast_psnr
    PRIVATE
        evoapprox_units
        ${OpenCV_LIBS}
)


add_executable(
    joint_bruteforce_search

    app/joint_bruteforce_search.cpp

    src/io/image_io.cpp
    src/region/region_mask.cpp

    src/processing/exact_sharpen.cpp

    src/approximate/evoapprox_adapter.cpp

    src/metrics/psnr.cpp
)


target_include_directories(
    joint_bruteforce_search
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${OpenCV_INCLUDE_DIRS}
)


target_link_libraries(
    joint_bruteforce_search
    PRIVATE
        evoapprox_units
        ${OpenCV_LIBS}
)

add_executable(
    all_pair_bruteforce_search

    app/all_pair_bruteforce_search.cpp

    src/io/image_io.cpp
    src/region/region_mask.cpp
    src/processing/exact_sharpen.cpp
    src/approximate/evoapprox_adapter.cpp
    src/metrics/psnr.cpp
)


target_include_directories(
    all_pair_bruteforce_search
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${OpenCV_INCLUDE_DIRS}
)


target_link_libraries(
    all_pair_bruteforce_search
    PRIVATE
        evoapprox_units
        ${OpenCV_LIBS}
)

add_executable(
    all_triple_search

    app/all_triple_search.cpp
)


add_executable(
    triple_A2A3A4_search

    app/triple_A2A3A4_search.cpp

    src/io/image_io.cpp
    src/region/region_mask.cpp

    src/processing/exact_sharpen.cpp

    src/approximate/evoapprox_adapter.cpp

    src/metrics/psnr.cpp
)


target_include_directories(
    triple_A2A3A4_search
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${OpenCV_INCLUDE_DIRS}
)


target_link_libraries(
    triple_A2A3A4_search
    PRIVATE
        evoapprox_units
        ${OpenCV_LIBS}
)

add_executable(
    test_dct_application

    app/test_dct_application.cpp

    src/applications/dct.cpp

    src/io/image_io.cpp
    src/region/region_mask.cpp
    src/metrics/psnr.cpp
)

target_include_directories(
    test_dct_application
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${OpenCV_INCLUDE_DIRS}
)


target_link_libraries(
    test_dct_application
    PRIVATE
        evoapprox_units
        ${OpenCV_LIBS}
)

add_executable(
    test_dct8_graph

    app/test_dct8_graph.cpp

    src/applications/dct8_graph.cpp
)

target_include_directories(
    test_dct8_graph
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(
    test_dct8_graph
    PRIVATE
        ${OpenCV_LIBS}
)

add_executable(
    test_dct8_fixed_graph

    app/test_dct8_fixed_graph.cpp

    src/applications/dct8_graph.cpp
    src/applications/dct8_fixed_graph.cpp
)


target_include_directories(
    test_dct8_fixed_graph
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${OpenCV_INCLUDE_DIRS}
)


target_link_libraries(
    test_dct8_fixed_graph
    PRIVATE
        ${OpenCV_LIBS}
)