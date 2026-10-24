cmake_minimum_required(VERSION 3.15)
project(BHD_QoL_DInput8 LANGUAGES CXX)

add_library(dinput8 SHARED src/dinput8.cpp src/game_window.cpp
    src/cursor_clip.cpp src/cursor_visibility.cpp src/dpi_awareness.cpp
    src/borderless_fullscreen.cpp src/borderless_gamma.cpp
    src/raw_input.cpp src/raw_input_math.cpp src/mouse_scaling_fix.cpp src/camera_fov.cpp src/camera_fov_math.cpp
    src/high_frequency_camera.cpp src/high_frequency_camera_math.cpp
    src/high_frequency_viewmodel.cpp src/high_frequency_viewmodel_math.cpp
    src/dinput8.def)
target_compile_features(dinput8 PRIVATE cxx_std_17)

if(NOT WIN32)
    message(FATAL_ERROR "dinput8.dll must be cross-compiled for Windows, for example with a MinGW toolchain.")
endif()

set_target_properties(dinput8 PROPERTIES
    OUTPUT_NAME "dinput8"
    PREFIX ""
)

target_compile_definitions(dinput8 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(dinput8 PRIVATE shlwapi)

include(CTest)
if(BUILD_TESTING)
    add_executable(camera_fov_math_test tests/camera_fov_math_test.cpp src/camera_fov_math.cpp)
    target_include_directories(camera_fov_math_test PRIVATE src)
    target_compile_features(camera_fov_math_test PRIVATE cxx_std_17)
    add_test(NAME camera_fov_math COMMAND camera_fov_math_test)

    add_executable(cursor_clip_math_test tests/cursor_clip_math_test.cpp)
    target_include_directories(cursor_clip_math_test PRIVATE src)
    target_compile_features(cursor_clip_math_test PRIVATE cxx_std_17)
    add_test(NAME cursor_clip_math COMMAND cursor_clip_math_test)

    add_executable(high_frequency_camera_math_test tests/high_frequency_camera_math_test.cpp
        src/high_frequency_camera_math.cpp)
    target_include_directories(high_frequency_camera_math_test PRIVATE src)
    target_compile_features(high_frequency_camera_math_test PRIVATE cxx_std_17)
    add_test(NAME high_frequency_camera_math COMMAND high_frequency_camera_math_test)

    add_executable(raw_input_math_test tests/raw_input_math_test.cpp src/raw_input_math.cpp)
    target_include_directories(raw_input_math_test PRIVATE src)
    target_compile_features(raw_input_math_test PRIVATE cxx_std_17)
    add_test(NAME raw_input_math COMMAND raw_input_math_test)

    add_executable(high_frequency_viewmodel_math_test
        tests/high_frequency_viewmodel_math_test.cpp src/high_frequency_viewmodel_math.cpp)
    target_include_directories(high_frequency_viewmodel_math_test PRIVATE src)
    target_compile_features(high_frequency_viewmodel_math_test PRIVATE cxx_std_17)
    add_test(NAME high_frequency_viewmodel_math COMMAND high_frequency_viewmodel_math_test)
endif()
