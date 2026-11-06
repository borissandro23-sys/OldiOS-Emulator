cmake_minimum_required(VERSION 3.16)
project(OldiOS_Emulator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

add_executable(oldios-emu
    src/main.cpp
    src/armv6.cpp
    src/display_window.cpp
)

target_include_directories(oldios-emu PRIVATE include)

if (BUILD_TESTING)
    add_executable(armv6-tests tests/armv6_tests.cpp src/armv6.cpp)
    target_include_directories(armv6-tests PRIVATE include)
    add_test(NAME armv6-smoke COMMAND armv6-tests)
endif()

if (MSVC)
    target_compile_options(oldios-emu PRIVATE /W4)
else()
    target_compile_options(oldios-emu PRIVATE -Wall -Wextra -Wpedantic)
endif()
