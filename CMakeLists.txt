cmake_minimum_required(VERSION 3.19)
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")

option(BUILD_ASAN "Build with ASAN" OFF)
option(STATIC_CRT "Link with the static CRT (/MT)" OFF)

if (STATIC_CRT)
    cmake_policy(SET CMP0091 NEW)
    set(STATIC_CRT ON)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif ()

# clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(CMAKE_EXPORT_COMPILE_COMMANDS)
  set(CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES
      ${CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES})
endif()

project(Minifier LANGUAGES CXX)
add_subdirectory(luau)
add_subdirectory(RE-flex)
add_executable(Minifier.CLI)

target_sources(Minifier.CLI PRIVATE
    src/main.cpp
)

# TODO: Set Luau.Analysis at O2 to speed up debugging
if (MSVC)
    list(APPEND OPTIONS /W3 /WX /D_CRT_SECURE_NO_WARNINGS)
    list(APPEND OPTIONS /MP) # Distribute compilation across multiple cores
else ()
    list(APPEND OPTIONS -Wall)
endif ()

if (BUILD_ASAN)
    if (MSVC)
        list(APPEND OPTIONS /fsanitize=address)
        target_compile_options(Minifier.CLI PRIVATE /fsanitize=address)
    else ()
        list(APPEND OPTIONS -fsanitize=address)
        target_compile_options(Minifier.CLI PRIVATE -fsanitize=address)
    endif ()
endif ()

target_compile_features(Minifier.CLI PUBLIC cxx_std_17)
target_compile_options(Minifier.CLI PRIVATE ${OPTIONS})
target_link_libraries(Minifier.CLI PRIVATE Luau.Ast ReflexLibStatic)

set_target_properties(Minifier.CLI PROPERTIES OUTPUT_NAME luau-minify)
target_compile_features(Minifier.CLI PUBLIC cxx_std_17)
target_compile_options(Minifier.CLI PRIVATE ${OPTIONS})
