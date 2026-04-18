cmake_minimum_required(VERSION 3.16)

project(llm_invoke_cpp LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

option(LLM_INVOKE_CPP_BUILD_EXAMPLES "Build the example programs" ON)

add_library(llm_invoke_cpp_func_registry INTERFACE)
add_library(llm_invoke_cpp::func_registry ALIAS llm_invoke_cpp_func_registry)

target_compile_features(llm_invoke_cpp_func_registry INTERFACE cxx_std_23)
target_include_directories(llm_invoke_cpp_func_registry
    INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)

FetchContent_MakeAvailable(nlohmann_json)

add_library(llm_invoke_cpp_json_invoke INTERFACE)
add_library(llm_invoke_cpp::json_invoke ALIAS llm_invoke_cpp_json_invoke)

target_link_libraries(llm_invoke_cpp_json_invoke
    INTERFACE
        llm_invoke_cpp::func_registry
        nlohmann_json::nlohmann_json
)

if(LLM_INVOKE_CPP_BUILD_EXAMPLES)
    add_executable(func_registry_demo examples/func_registry/func_registry_demo.cpp)
    target_link_libraries(func_registry_demo PRIVATE llm_invoke_cpp::func_registry)

    add_executable(json_invoke_demo examples/json_invoke/json_invoke_demo.cpp)
    target_link_libraries(json_invoke_demo PRIVATE llm_invoke_cpp::json_invoke)
endif()