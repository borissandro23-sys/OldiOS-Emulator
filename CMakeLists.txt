cmake_minimum_required(VERSION 3.16)
project(OldiOS_Emulator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(oldios-emu
    src/main.cpp
    src/armv6.cpp
    src/display_window.cpp
)

target_include_directories(oldios-emu PRIVATE include)

if (MSVC)
    target_compile_options(oldios-emu PRIVATE /W4)
else()
    target_compile_options(oldios-emu PRIVATE -Wall -Wextra -Wpedantic)
endif()
