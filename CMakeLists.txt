cmake_minimum_required(VERSION 3.16)

project(approx_attack_validation LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenCV REQUIRED)

add_library(
    evoapprox_units
    STATIC
        third_party/evoapproxlib/add12se_5QT.c
        third_party/evoapproxlib/add12se_5QC.c
        third_party/evoapproxlib/add12se_5TE.c
        third_party/evoapproxlib/add12se_5PN.c
        third_party/evoapproxlib/add12se_5SB.c
        third_party/evoapproxlib/add12se_5Z0.c
)

add_executable(
    run_validation

    app/main.cpp

    src/io/image_io.cpp
    src/region/region_mask.cpp

    src/processing/exact_sharpen.cpp
    src/processing/approx_sharpen.cpp

    src/approximate/evoapprox_adapter.cpp

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
