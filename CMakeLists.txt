cmake_minimum_required(VERSION 3.20)
project(ForceUnfreeze VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(ForceUnfreeze WIN32
    src/main.cpp
    res/ForceUnfreeze.rc
)

target_compile_definitions(ForceUnfreeze PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_link_libraries(ForceUnfreeze PRIVATE
    user32
    shell32
    advapi32
    psapi
    dwmapi
)

if(MSVC)
    target_compile_options(ForceUnfreeze PRIVATE /W4 /permissive-)
    set_property(TARGET ForceUnfreeze PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_link_options(ForceUnfreeze PRIVATE /MANIFEST:NO)
endif()
