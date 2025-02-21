cmake_minimum_required(VERSION 3.19)
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")

option(STATIC_CRT "Link with the static CRT (/MT)" OFF)

if (STATIC_CRT)
    cmake_policy(SET CMP0091 NEW)
    set(STATIC_CRT ON)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif ()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # clangd

project(Minifier LANGUAGES CXX)

add_subdirectory(luau)
add_subdirectory(RE-flex)
add_subdirectory(unordered_dense)

add_library(Minifier STATIC)
add_executable(Minifier.CLI)

target_sources(Minifier PRIVATE
    src/minifier.h
    src/syntax.h
    src/tracking.h

    src/graph/rtti.hpp
    src/graph/block.hpp
    src/graph/statement.hpp

    src/graph/rtti.cpp
    src/graph/block.cpp
    src/graph/statement.cpp

    src/minifier.cpp
    src/syntax.cpp
    src/tracking.cpp
)

target_sources(Minifier.CLI PRIVATE
    src/main.cpp
)

if (MSVC)
    list(APPEND OPTIONS /W3 /WX /D_CRT_SECURE_NO_WARNINGS)
    list(APPEND OPTIONS /MP) # Distribute compilation across multiple cores
else ()
    list(APPEND OPTIONS -Wall)
endif ()

target_compile_features(Minifier PUBLIC cxx_std_20)
target_compile_options(Minifier PRIVATE ${OPTIONS})
target_link_libraries(Minifier PRIVATE ReflexLibStatic)
target_link_libraries(Minifier PUBLIC Luau.Ast unordered_dense)

target_compile_features(Minifier.CLI PUBLIC cxx_std_20)
target_compile_options(Minifier.CLI PRIVATE ${OPTIONS})
target_link_libraries(Minifier.CLI PRIVATE Minifier)
set_target_properties(Minifier.CLI PROPERTIES OUTPUT_NAME luau-minify)
