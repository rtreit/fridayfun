cmake_minimum_required(VERSION 3.20)
project(DebugHook LANGUAGES CXX)

add_library(DebugHook SHARED DebugHook.cpp)

# Export GetTargetProcessHandle without name mangling.
set_target_properties(DebugHook PROPERTIES
    PREFIX ""
    OUTPUT_NAME "DebugHook"
)
