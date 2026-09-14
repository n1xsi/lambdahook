cmake_minimum_required(VERSION 3.15)
project(dod-cheat LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# GoldSrc is 32-bit
if(MSVC)
    # Force 32-bit target
    set(CMAKE_GENERATOR_PLATFORM Win32 CACHE STRING "" FORCE)
endif()

add_library(dodcheat SHARED
    src/dllmain.cpp
    src/globals.cpp
    src/hooks.cpp
    src/cvars.cpp
    src/util.cpp
    src/mathutil.cpp
    src/entityutil.cpp
    src/detour.cpp
    src/features/aim.cpp
    src/features/chams.cpp
    src/features/esp.cpp
    src/features/misc.cpp
    src/features/movement.cpp
)

target_include_directories(dodcheat PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/include
)

target_link_libraries(dodcheat PRIVATE
    opengl32
)

# MSVC-specific settings
if(MSVC)
    target_compile_options(dodcheat PRIVATE
        /W3
        /wd4244  # conversion warnings
        /wd4305  # truncation warnings
    )
    target_compile_definitions(dodcheat PRIVATE
        _CRT_SECURE_NO_WARNINGS
        WIN32
        _WINDOWS
    )
else()
    # MinGW
    target_compile_options(dodcheat PRIVATE
        -Wall -Wextra -Wno-write-strings
    )
    # Static link MinGW runtime so DLL has no external dependencies
    target_link_options(dodcheat PRIVATE
        -static-libgcc -static-libstdc++ -static
    )
endif()

set_target_properties(dodcheat PROPERTIES
    OUTPUT_NAME "dodcheat"
    SUFFIX ".dll"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
