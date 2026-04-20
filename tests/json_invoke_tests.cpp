#include <doctest/doctest.h>

#include <optional>
#include <string>

#include <json_invoke/json_invoke.hpp>

#include "snapshot_test_utils.hpp"
#include "examples/json_invoke/person_support.hpp"
#include "examples/json_invoke/priority_support.hpp"

namespace {

std::optional<int> addOptional(std::optional<int> lhs, int rhs)
{
    if (!lhs.has_value())
    {
        return std::nullopt;
    }

    return *lhs + rhs;
}

json_invoke::JsonInvokeAdapter makeSnapshotAdapter()
{
    json_invoke::JsonInvokeAdapter adapter;
    adapter.registerFunction(
        "add_optional",
        addOptional,
        func_registry::FunctionMetadata{{"lhs", "rhs"}, "Add when the optional input is present."});
    adapter.registerFunction("get_person", getPerson, "Return one person.");
    adapter.registerFunction(
        "recommend_priority",
        recommendIncidentPriority,
        func_registry::FunctionMetadata{
            {"requested_priority", "customer_blocked", "production_impact", "affected_users"},
            "Recommend an incident priority."});
    return adapter;
}

} // namespace

TEST_CASE("json_invoke adapts object results and typed extraction")
{
    json_invoke::JsonInvokeAdapter adapter;
    adapter.registerFunction("get_person", getPerson, "Return one person.");

    const auto response = adapter.invokeJson({{"name", "get_person"}, {"args", json_invoke::json::array()}});
    CHECK(response.at("ok").get<bool>());
    CHECK(response.at("value").at("name").get<std::string>() == "Alice");
    CHECK(response.at("value").at("age").get<int>() == 30);

    const Person person = adapter.invoke<Person>({{"name", "get_person"}, {"args", json_invoke::json::array()}});
    CHECK(person.getName() == "Alice");
    CHECK(person.getAge() == 30);
}

TEST_CASE("json_invoke supports named arguments, optional parameters, and stringified tool arguments")
{
    json_invoke::JsonInvokeAdapter adapter;
    adapter.registerFunction(
        "add_optional",
        addOptional,
        func_registry::FunctionMetadata{{"lhs", "rhs"}, "Add when the optional input is present."});

    const auto schema = adapter.getToolSchemaJson("add_optional");
    const auto& parameters = schema.at("function").at("parameters");
    CHECK(parameters.at("required") == json_invoke::json::array({"rhs"}));
    CHECK(parameters.at("properties").at("lhs").at("x-nullable").get<bool>());
    CHECK(parameters.at("properties").at("lhs").at("type") == json_invoke::json::array({"integer", "null"}));

    const auto success = adapter.invokeJson({
        {"type", "function"},
        {"function", {
            {"name", "add_optional"},
            {"arguments", R"({"lhs":2,"rhs":5})"},
        }},
    });
    CHECK(success.at("ok").get<bool>());
    CHECK(success.at("value").get<int>() == 7);

    const auto missing_optional = adapter.invokeJson({
        {"name", "add_optional"},
        {"args", {{"rhs", 5}}},
    });
    CHECK(missing_optional.at("ok").get<bool>());
    CHECK(missing_optional.at("value").is_null());
}

TEST_CASE("json_invoke exports enum-aware schemas and structured errors")
{
    json_invoke::JsonInvokeAdapter adapter;
    adapter.registerFunction(
        "recommend_priority",
        recommendIncidentPriority,
        func_registry::FunctionMetadata{
            {"requested_priority", "customer_blocked", "production_impact", "affected_users"},
            "Recommend an incident priority."});

    const auto schema = adapter.getToolSchemaJson("recommend_priority");
    const auto& requested_priority = schema.at("function").at("parameters").at("properties").at("requested_priority");
    CHECK(requested_priority.at("enum") == json_invoke::json::array({"low", "normal", "high", "critical"}));
    CHECK(schema.at("function").at("x-return").at("enum_values") == json_invoke::json::array({"low", "normal", "high", "critical"}));

    const auto missing_required = adapter.invokeJson({
        {"name", "recommend_priority"},
        {"args", json_invoke::json::object()},
    });
    CHECK_FALSE(missing_required.at("ok").get<bool>());
    CHECK(missing_required.at("error").at("code").get<std::string>() == "invalid_request");
    CHECK(missing_required.at("error").at("message").get<std::string>().find("requested_priority") != std::string::npos);

    const auto bad_type = adapter.invokeJson({
        {"name", "recommend_priority"},
        {"args", {
            {"requested_priority", "low"},
            {"customer_blocked", false},
            {"production_impact", true},
            {"affected_users", "many"},
        }},
    });
    CHECK_FALSE(bad_type.at("ok").get<bool>());
    CHECK(bad_type.at("error").at("code").get<std::string>() == "conversion_failed");
    CHECK(bad_type.at("error").at("message").get<std::string>().find("affected_users") != std::string::npos);
}

TEST_CASE("json_invoke tool metadata matches JSON snapshots")
{
    const auto adapter = makeSnapshotAdapter();

    test_support::checkSnapshot("tool_specs_snapshot.json", adapter.getAllToolSpecsJson().dump(2));
    test_support::checkSnapshot("tool_schemas_snapshot.json", adapter.getAllToolSchemasJson().dump(2));
}