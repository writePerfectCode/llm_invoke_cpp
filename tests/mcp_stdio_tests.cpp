#include <doctest/doctest.h>

#include <sstream>
#include <string>

#include <func_registry/func_registry.hpp>
#include <mcp/mcp_stdio_server.hpp>

TEST_CASE("mcp stdio server handles initialize list and call")
{
    json_session_invoke::JsonSessionInvokeAdapterThreadSafe adapter;
    runtime::RuntimeFacadeThreadSafe runtime(adapter, 2);
    mcp::McpStdioServerThreadSafe server(runtime, mcp::ServerInfo{"demo-server", "1.2.3"});

    adapter.registerFunction(
        "sum",
        json_invoke::readOnly([](int left, int right) { return left + right; }),
        func_registry::FunctionMetadata{{"left", "right"}, "Add two integers without session state."});

    const auto initialize_response = server.handleMessage(json_session_invoke::json{
        {"jsonrpc", "2.0"},
        {"id", 1},
        {"method", "initialize"},
        {"params", {
            {"protocolVersion", "2025-03-26"},
            {"capabilities", json_session_invoke::json::object()},
            {"clientInfo", {{"name", "test-client"}, {"version", "0.0.1"}}},
        }},
    });

    REQUIRE(initialize_response.has_value());
    CHECK(initialize_response->at("result").at("protocolVersion") == "2025-03-26");
    CHECK(initialize_response->at("result").at("capabilities").at("tools").at("listChanged") == false);
    CHECK(initialize_response->at("result").at("serverInfo").at("name") == "demo-server");
    CHECK(initialize_response->at("result").at("serverInfo").at("version") == "1.2.3");

    const auto initialized_notification = server.handleMessage(json_session_invoke::json{
        {"jsonrpc", "2.0"},
        {"method", "notifications/initialized"},
    });
    CHECK_FALSE(initialized_notification.has_value());

    const auto list_response = server.handleMessage(json_session_invoke::json{
        {"jsonrpc", "2.0"},
        {"id", 2},
        {"method", "tools/list"},
        {"params", json_session_invoke::json::object()},
    });

    REQUIRE(list_response.has_value());
    REQUIRE(list_response->at("result").at("tools").size() == 1);
    CHECK(list_response->at("result").at("tools").at(0).at("name") == "sum");
    CHECK(list_response->at("result").at("tools").at(0).at("description") == "Add two integers without session state.");
    CHECK(list_response->at("result").at("tools").at(0).at("inputSchema").at("properties").contains("left"));

    const auto call_response = server.handleMessage(json_session_invoke::json{
        {"jsonrpc", "2.0"},
        {"id", 3},
        {"method", "tools/call"},
        {"params", {
            {"name", "sum"},
            {"arguments", {{"left", 2}, {"right", 5}}},
        }},
    });

    REQUIRE(call_response.has_value());
    CHECK(call_response->at("result").at("isError") == false);
    CHECK(call_response->at("result").at("content").at(0).at("type") == "text");
    CHECK(call_response->at("result").at("content").at(0).at("text") == "7");
}

TEST_CASE("mcp stdio server maps runtime validation failures to protocol errors")
{
    json_session_invoke::JsonSessionInvokeAdapterThreadSafe adapter;
    runtime::RuntimeFacadeThreadSafe runtime(adapter, 2);
    mcp::McpStdioServerThreadSafe server(runtime);

    const auto initialize_response = server.handleMessage(json_session_invoke::json{
        {"jsonrpc", "2.0"},
        {"id", 1},
        {"method", "initialize"},
        {"params", {
            {"protocolVersion", "2025-03-26"},
            {"capabilities", json_session_invoke::json::object()},
            {"clientInfo", {{"name", "test-client"}, {"version", "0.0.1"}}},
        }},
    });
    REQUIRE(initialize_response.has_value());

    const auto missing_tool_response = server.handleMessage(json_session_invoke::json{
        {"jsonrpc", "2.0"},
        {"id", 2},
        {"method", "tools/call"},
        {"params", {
            {"name", "missing_tool"},
            {"arguments", json_session_invoke::json::object()},
        }},
    });

    REQUIRE(missing_tool_response.has_value());
    CHECK(missing_tool_response->contains("error"));
    CHECK(missing_tool_response->at("error").at("code") == -32602);
    CHECK(missing_tool_response->at("error").at("message").get<std::string>().find("missing_tool") != std::string::npos);
}

TEST_CASE("mcp stdio server reads and writes Content-Length framed messages")
{
    json_session_invoke::JsonSessionInvokeAdapterThreadSafe adapter;
    runtime::RuntimeFacadeThreadSafe runtime(adapter, 2);
    mcp::McpStdioServerThreadSafe server(runtime);

    std::stringstream input;
    mcp::McpStdioServerThreadSafe::writeMessage(input, json_session_invoke::json{
        {"jsonrpc", "2.0"},
        {"id", 1},
        {"method", "initialize"},
        {"params", {
            {"protocolVersion", "2025-03-26"},
            {"capabilities", json_session_invoke::json::object()},
            {"clientInfo", {{"name", "test-client"}, {"version", "0.0.1"}}},
        }},
    });

    std::stringstream output;
    REQUIRE(server.processNextMessage(input, output));

    const auto response = mcp::McpStdioServerThreadSafe::readMessage(output);
    REQUIRE(response.has_value());
    CHECK(response->at("jsonrpc") == "2.0");
    CHECK(response->at("id") == 1);
    CHECK(response->at("result").at("serverInfo").at("name") == "llm_invoke_cpp");
}

TEST_CASE("mcp stdio server ignores trailing blank lines before EOF")
{
    std::stringstream input("\r\n\r\n");

    const auto message = mcp::McpStdioServerThreadSafe::readMessage(input);

    CHECK_FALSE(message.has_value());
}