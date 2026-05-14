#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include <json_session_invoke/json_session_invoke.hpp>
#include <mcp/mcp_stdio_server.hpp>

namespace {

struct Counter {
    int value{0};

    explicit Counter(int initial)
        : value(initial)
    {
    }

    void add(int delta)
    {
        value += delta;
    }

    int current() const
    {
        return value;
    }
};

void registerDemoTools(json_session_invoke::JsonSessionInvokeAdapterThreadSafe& adapter)
{
    adapter.registerFunction(
        "sum",
        json_invoke::readOnly([](int left, int right) { return left + right; }),
        json_invoke::FunctionMetadata{{"left", "right"}, "Add two integers without session state."});

    adapter.registerFunction(
        "echo_text",
        json_invoke::readOnly([](std::string text) { return text; }),
        json_invoke::FunctionMetadata{{"text"}, "Echo one text payload back to the caller."});

    adapter
        .stateful<Counter>("counter")
        .create(
            [](int initial) {
                return std::make_shared<Counter>(initial);
            },
            json_invoke::FunctionMetadata{{"initial"}, "Create one in-memory counter object."})
        .method(
            "counter_add",
            [](Counter& counter, int delta) {
                counter.add(delta);
            },
            json_invoke::FunctionMetadata{{"delta"}, "Add one delta to the counter state."})
        .method(
            "counter_value",
            [](const Counter& counter) {
                return counter.current();
            },
            "Read the current counter value from one handle.")
        .destroy();
}

} // namespace

int main()
{
    try
    {
        json_session_invoke::JsonSessionInvokeAdapterThreadSafe adapter;
        registerDemoTools(adapter);

        runtime::RuntimeFacadeThreadSafe runtime(adapter, 4);
        mcp::McpStdioServerThreadSafe server(
            runtime,
            mcp::ServerInfo{
                "llm_invoke_cpp_demo",
                "0.1.0",
                "2025-03-26",
                "Demo MCP stdio server exposing sum, echo_text, and counter create/call/destroy tools.",
                false,
            });

        server.serve(std::cin, std::cout);
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "fatal MCP server error: " << e.what() << std::endl;
        return 1;
    }
}