cmake_minimum_required(VERSION 3.16)
project(approx_attack_validation LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs)

add_library(evoapprox_units STATIC
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

add_library(approx_framework STATIC
    src/applications/conv.cpp
    src/applications/dct.cpp
    src/applications/dct8_fixed_graph.cpp
    src/applications/sharpen.cpp
    src/applications/sobel.cpp
    src/approximate/evoapprox_adapter.cpp
    src/region/region_mask.cpp
)
target_include_directories(approx_framework PUBLIC include ${OpenCV_INCLUDE_DIRS})
target_link_libraries(approx_framework PUBLIC evoapprox_units ${OpenCV_LIBS})

add_executable(fixed_roi_verify app/fixed_roi_verify.cpp)
target_link_libraries(fixed_roi_verify PRIVATE approx_framework)

include(CTest)
if(BUILD_TESTING)
    add_executable(test_profile_cli tests/test_profile_cli.cpp)
    target_link_libraries(test_profile_cli PRIVATE approx_framework)
    add_test(NAME profile_cli COMMAND test_profile_cli $<TARGET_FILE:fixed_roi_verify>)
    add_executable(test_circuit_bitflip tests/test_circuit_bitflip.cpp)
    target_link_libraries(test_circuit_bitflip PRIVATE approx_framework)
    add_test(NAME circuit_bitflip COMMAND test_circuit_bitflip)
endif()
