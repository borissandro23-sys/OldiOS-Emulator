cmake_minimum_required(VERSION 3.16)
project(OldiOS_Emulator LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Portable emulator core: no Win32 UI or IPA/ZIP front-end dependencies.
add_library(oldios_core STATIC src/armv6.cpp src/macho_loader.cpp)
target_include_directories(oldios_core PUBLIC include)

if (WIN32)
    add_executable(oldios-emu WIN32
        src/main.cpp src/ipa_loader.cpp
        third_party/miniz.c third_party/miniz_tdef.c third_party/miniz_tinfl.c)
    target_link_libraries(oldios-emu PRIVATE oldios_core user32 gdi32 comdlg32)
else()
    add_executable(oldios-emu
        src/main.cpp src/ipa_loader.cpp
        third_party/miniz.c third_party/miniz_tdef.c third_party/miniz_tinfl.c)
    target_link_libraries(oldios-emu PRIVATE oldios_core)
endif()

target_include_directories(oldios-emu PRIVATE include third_party)
if (MSVC)
    target_compile_options(oldios_core PRIVATE /W4)
    target_compile_options(oldios-emu PRIVATE /W4)
else()
    target_compile_options(oldios_core PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(oldios-emu PRIVATE -Wall -Wextra -Wpedantic)
endif()
