cmake_minimum_required(VERSION 3.24)
project(NeoFrame VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(NeoFrame WIN32
    src/main.cpp
    src/Core/Logger.cpp
    src/Core/Gpu.cpp
    src/Core/Engine.cpp
    src/Settings/Settings.cpp
    src/Capture/CaptureSession.cpp
    src/OpticalFlow/MotionEstimator.cpp
    src/FrameGeneration/FrameGenerator.cpp
    src/Renderer/Overlay.cpp
    src/Renderer/Preview.cpp
    src/Performance/PerfMonitor.cpp
    src/UI/MainWindow.cpp
    src/UI/Pages.cpp
    src/UI/Logo.cpp
    src/UI/RegionPicker.cpp
)

target_include_directories(NeoFrame PRIVATE src)
target_compile_definitions(NeoFrame PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_compile_options(NeoFrame PRIVATE /W3 /utf-8 /permissive- /EHsc /bigobj /Zc:__cplusplus)
target_link_libraries(NeoFrame PRIVATE
    d2d1 dwrite d3d11 dxgi d3dcompiler dwmapi user32 gdi32 shell32 ole32 advapi32
    windowscodecs pdh winmm windowsapp)
