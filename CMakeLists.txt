cmake_minimum_required(VERSION 3.15)
project(BHD_QoL_WinMM LANGUAGES CXX)

add_library(winmm SHARED src/winmm_proxy.cpp src/bhd_patches.cpp src/game_window.cpp
    src/cursor_clip.cpp src/cursor_visibility.cpp src/dpi_awareness.cpp
    src/borderless_fullscreen.cpp src/borderless_gamma.cpp
    src/raw_input.cpp src/raw_input_math.cpp src/mouse_scaling_fix.cpp src/mouse_scale_query.cpp src/camera_fov.cpp src/camera_fov_math.cpp
    src/high_frequency_camera.cpp src/high_frequency_camera_math.cpp
    src/high_frequency_viewmodel.cpp src/high_frequency_viewmodel_math.cpp
    src/winmm.def)
target_compile_features(winmm PRIVATE cxx_std_17)

if(NOT WIN32)
    message(FATAL_ERROR "winmm.dll must be cross-compiled for Windows, for example with a MinGW toolchain.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "winmm.dll must be built for x86 (32-bit), matching dfbhd.exe.")
endif()

set_target_properties(winmm PROPERTIES
    OUTPUT_NAME "winmm"
    ARCHIVE_OUTPUT_NAME "BHD_QoL_winmm"
    PREFIX ""
)

target_compile_definitions(winmm PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(winmm PRIVATE shlwapi)
configure_file(winmm.ini winmm.ini COPYONLY)

include(CTest)
if(BUILD_TESTING)
    add_executable(winmm_proxy_test tests/winmm_proxy_test.cpp)
    target_include_directories(winmm_proxy_test PRIVATE src)
    target_compile_features(winmm_proxy_test PRIVATE cxx_std_17)
    # Use the SDK import library, rather than linking against the proxy target.
    # This exercises automatic loading via the executable's normal WinMM imports.
    if(MSVC)
        target_link_libraries(winmm_proxy_test PRIVATE winmm.lib)
    else()
        target_link_libraries(winmm_proxy_test PRIVATE -lwinmm)
    endif()
    add_dependencies(winmm_proxy_test winmm)
    add_test(NAME winmm_proxy COMMAND winmm_proxy_test "$<TARGET_FILE:winmm>")
    set_tests_properties(winmm_proxy PROPERTIES TIMEOUT 30)

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

    add_executable(raw_input_packets_test tests/raw_input_packets_test.cpp)
    target_include_directories(raw_input_packets_test PRIVATE src)
    target_compile_features(raw_input_packets_test PRIVATE cxx_std_17)
    add_test(NAME raw_input_packets COMMAND raw_input_packets_test)

    add_executable(high_frequency_viewmodel_math_test
        tests/high_frequency_viewmodel_math_test.cpp src/high_frequency_viewmodel_math.cpp)
    target_include_directories(high_frequency_viewmodel_math_test PRIVATE src)
    target_compile_features(high_frequency_viewmodel_math_test PRIVATE cxx_std_17)
    add_test(NAME high_frequency_viewmodel_math COMMAND high_frequency_viewmodel_math_test)
endif()
