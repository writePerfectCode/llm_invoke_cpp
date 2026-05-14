cmake_minimum_required(VERSION 3.16)

project(llm_invoke_cpp LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)
include(CTest)

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

add_library(llm_invoke_cpp_json_session_invoke INTERFACE)
add_library(llm_invoke_cpp::json_session_invoke ALIAS llm_invoke_cpp_json_session_invoke)

target_link_libraries(llm_invoke_cpp_json_session_invoke
    INTERFACE
        llm_invoke_cpp::json_invoke
)

add_library(llm_invoke_cpp_task_scheduler INTERFACE)
add_library(llm_invoke_cpp::task_scheduler ALIAS llm_invoke_cpp_task_scheduler)

target_link_libraries(llm_invoke_cpp_task_scheduler
    INTERFACE
        llm_invoke_cpp::json_session_invoke
)

add_library(llm_invoke_cpp_runtime INTERFACE)
add_library(llm_invoke_cpp::runtime ALIAS llm_invoke_cpp_runtime)

target_link_libraries(llm_invoke_cpp_runtime
    INTERFACE
        llm_invoke_cpp::task_scheduler
)

add_library(llm_invoke_cpp_mcp INTERFACE)
add_library(llm_invoke_cpp::mcp ALIAS llm_invoke_cpp_mcp)

target_link_libraries(llm_invoke_cpp_mcp
    INTERFACE
        llm_invoke_cpp::runtime
)

if(BUILD_TESTING)
    FetchContent_Declare(
        doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG v2.4.12
    )

    FetchContent_MakeAvailable(doctest)

    add_executable(llm_invoke_cpp_tests
        tests/test_main.cpp
        tests/func_registry_tests.cpp
        tests/json_invoke_tests.cpp
        tests/json_stateful_tests.cpp
        tests/mcp_stdio_tests.cpp
        tests/runtime_tests.cpp
        tests/task_scheduler_tests.cpp
    )

    target_compile_features(llm_invoke_cpp_tests PRIVATE cxx_std_23)
    target_compile_definitions(llm_invoke_cpp_tests PRIVATE LLM_INVOKE_CPP_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_include_directories(llm_invoke_cpp_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(llm_invoke_cpp_tests
        PRIVATE
            doctest::doctest
            llm_invoke_cpp::task_scheduler
    )

    add_test(NAME llm_invoke_cpp_tests COMMAND llm_invoke_cpp_tests)
endif()

if(LLM_INVOKE_CPP_BUILD_EXAMPLES)
    add_executable(func_registry_demo examples/func_registry/func_registry_demo.cpp)
    target_link_libraries(func_registry_demo PRIVATE llm_invoke_cpp::func_registry)

    add_executable(mcp_stdio_server_demo examples/mcp_stdio/mcp_stdio_server_demo.cpp)
    target_link_libraries(mcp_stdio_server_demo PRIVATE llm_invoke_cpp::mcp)

    add_executable(json_invoke_demo examples/json_invoke/json_invoke_demo.cpp)
    target_link_libraries(json_invoke_demo PRIVATE llm_invoke_cpp::json_invoke)

    add_executable(json_stateful_demo examples/json_stateful/json_stateful_demo.cpp)
    target_link_libraries(json_stateful_demo PRIVATE llm_invoke_cpp::json_session_invoke)

    add_executable(json_tracing_demo examples/json_tracing/json_tracing_demo.cpp)
    target_link_libraries(json_tracing_demo PRIVATE llm_invoke_cpp::json_session_invoke)

    add_executable(trace_recorder_demo examples/trace_recorder/trace_recorder_demo.cpp)
    target_link_libraries(trace_recorder_demo PRIVATE llm_invoke_cpp::json_invoke)

    add_executable(task_scheduler_demo examples/task_scheduler/task_scheduler_demo.cpp)
    target_link_libraries(task_scheduler_demo PRIVATE llm_invoke_cpp::task_scheduler)
endif()