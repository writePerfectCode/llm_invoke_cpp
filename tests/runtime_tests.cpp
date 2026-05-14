#include <doctest/doctest.h>

#include <func_registry/func_registry.hpp>
#include <runtime/runtime_facade.hpp>

TEST_CASE("runtime facade lists normalized tool descriptors")
{
    json_session_invoke::JsonSessionInvokeAdapterThreadSafe adapter;
    runtime::RuntimeFacadeThreadSafe runtime(adapter, 2);

    adapter.registerFunction(
        "sum",
        json_invoke::readOnly([](int left, int right) { return left + right; }),
        func_registry::FunctionMetadata{{"left", "right"}, "Add two integers without session state."});

    const auto tools = runtime.listTools();

    REQUIRE(tools.size() == 1);
    CHECK(tools.front().name == "sum");
    CHECK(tools.front().description == "Add two integers without session state.");
    CHECK(tools.front().input_schema.at("type") == "object");
    CHECK(tools.front().input_schema.at("properties").contains("left"));
    CHECK(tools.front().input_schema.at("properties").contains("right"));
    CHECK(tools.front().output_schema.at("type") == "integer");
    CHECK(tools.front().raw_schema.at("function").at("name") == "sum");
}

TEST_CASE("runtime facade normalizes successful invocation results")
{
    json_session_invoke::JsonSessionInvokeAdapterThreadSafe adapter;
    runtime::RuntimeFacadeThreadSafe runtime(adapter, 2);

    adapter.registerFunction(
        "sum",
        json_invoke::readOnly([](int left, int right) { return left + right; }),
        func_registry::FunctionMetadata{{"left", "right"}, "Add two integers without session state."});

    const auto result = runtime.invoke(runtime::InvokeRequest{
        "sum",
        json_session_invoke::json{{"left", 2}, {"right", 5}},
    });

    CHECK(result.tool_name == "sum");
    CHECK(result.ok);
    CHECK_FALSE(result.error.has_value());
    CHECK(result.value.get<int>() == 7);
    CHECK(result.raw_response.at("ok").get<bool>());
}

TEST_CASE("runtime facade converts classifier failures into unified errors")
{
    json_session_invoke::JsonSessionInvokeAdapterThreadSafe adapter;
    runtime::RuntimeFacadeThreadSafe runtime(adapter, 2);

    const auto result = runtime.invoke(runtime::InvokeRequest{
        "missing_tool",
        json_session_invoke::json::object(),
    });

    CHECK(result.tool_name == "missing_tool");
    CHECK_FALSE(result.ok);
    REQUIRE(result.error.has_value());
    CHECK(result.error->code == "function_not_found");
    CHECK_FALSE(result.error->message.empty());
    CHECK(result.raw_response.at("error").at("code") == "function_not_found");
}