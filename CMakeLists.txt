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

    src/approximate/evoapprox_adapter.cpp

    src/io/image_io.cpp
    src/region/region_mask.cpp
    src/metrics/psnr.cpp
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