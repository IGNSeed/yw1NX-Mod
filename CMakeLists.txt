cmake_minimum_required(VERSION 3.21)
project(yw1nx_foundation VERSION 0.1.0 LANGUAGES CXX)

add_library(yw1nx_foundation STATIC
    source/game/versions/version_manager.cpp
    source/game/symbols/symbol_registry.cpp
)
target_include_directories(yw1nx_foundation PUBLIC source)
target_compile_features(yw1nx_foundation PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(yw1nx_foundation PRIVATE /W4 /WX /utf-8)
else()
    target_compile_options(yw1nx_foundation PRIVATE
        -Wall -Wextra -Wpedantic -Werror -fno-exceptions -fno-rtti)
endif()
