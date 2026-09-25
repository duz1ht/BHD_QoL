cmake_minimum_required(VERSION 3.15)
project(BHD_QoL_DInput8 LANGUAGES CXX)

add_library(dinput8 SHARED src/dinput8.cpp src/logger.cpp src/game_window.cpp
    src/cursor_clip.cpp src/cursor_visibility.cpp src/dpi_awareness.cpp
    src/borderless_fullscreen.cpp src/borderless_gamma.cpp
    src/raw_input.cpp src/free_rate_mouse_poll.cpp src/high_rate_camera_rotation.cpp
    src/mouse_scaling_fix.cpp
    src/camera_fov.cpp src/camera_fov_math.cpp src/visual_camera_math.cpp
    src/visual_diagnostics.cpp
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

add_executable(BHD_QoL_Overlay WIN32 tools/visual_diagnostics_overlay.cpp)
target_include_directories(BHD_QoL_Overlay PRIVATE src)
target_compile_features(BHD_QoL_Overlay PRIVATE cxx_std_17)
target_compile_definitions(BHD_QoL_Overlay PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(BHD_QoL_Overlay PRIVATE gdi32 user32)
