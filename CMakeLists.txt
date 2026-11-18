cmake_minimum_required(VERSION 3.24)
project(NeoFrame VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(NeoFrame WIN32
    src/main.cpp
    src/Core/Logger.cpp
    src/UI/MainWindow.cpp
)

target_include_directories(NeoFrame PRIVATE src)
target_compile_definitions(NeoFrame PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(NeoFrame PRIVATE /W4 /utf-8 /permissive-)
target_link_libraries(NeoFrame PRIVATE d2d1 dwrite d3d11 dxgi dwmapi user32 gdi32 shell32 ole32)
