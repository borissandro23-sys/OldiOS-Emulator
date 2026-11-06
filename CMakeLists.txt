cmake_minimum_required(VERSION 3.16)
project(OldiOS_Emulator LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (WIN32)
    add_executable(oldios-emu WIN32
        src/main.cpp
        src/armv6.cpp
        src/ipa_loader.cpp
        src/macho_loader.cpp
        third_party/miniz.c
        third_party/miniz_tdef.c
        third_party/miniz_tinfl.c
    )
    target_link_libraries(oldios-emu PRIVATE user32 gdi32 comdlg32)
else()
    add_executable(oldios-emu
        src/main.cpp
        src/armv6.cpp
        src/ipa_loader.cpp
        src/macho_loader.cpp
        third_party/miniz.c
        third_party/miniz_tdef.c
        third_party/miniz_tinfl.c
    )
endif()

target_include_directories(oldios-emu PRIVATE include third_party)

if (MSVC)
    target_compile_options(oldios-emu PRIVATE /W4)
else()
    target_compile_options(oldios-emu PRIVATE -Wall -Wextra -Wpedantic)
endif()
