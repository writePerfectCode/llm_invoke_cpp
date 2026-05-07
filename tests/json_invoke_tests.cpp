#include <doctest/doctest.h>

#include <optional>
#include <string>
#include <vector>

#include <json_invoke/json_invoke.hpp>
#include <tools/trace_recorder.hpp>

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

TEST_CASE("json_invoke exports explicit execution semantics for wrapped stateless tools")
{
    json_invoke::JsonInvokeAdapter adapter;
    std::string log;

    adapter.registerFunction(
        "sum",
        json_invoke::readOnly([](int left, int right) { return left + right; }),
        func_registry::FunctionMetadata{{"left", "right"}, "Add two integers."});

    adapter.registerFunction(
        "append_log",
        json_invoke::mutating([&log](std::string suffix) {
            log += suffix;
            return log.size();
        }),
        func_registry::FunctionMetadata{{"suffix"}, "Append one suffix to the log."});

    const auto sum_schema = adapter.getToolSchemaJson("sum");
    CHECK(sum_schema.at("function").at("x-execution-semantics").get<std::string>() == "read_only");

    const auto summaries = adapter.getAllToolSummariesJson();
    REQUIRE(summaries.size() == 2);
    CHECK(summaries[0].contains("x-execution-semantics"));
    CHECK(summaries[1].contains("x-execution-semantics"));

    const auto response = adapter.invokeJson(
        {{"name", "append_log"}, {"args", {{"suffix", "!"}}}});
    CHECK(response.at("ok").get<bool>());
    CHECK(response.at("value").get<std::size_t>() == 1);
    CHECK(log == "!");
}

TEST_CASE("json_invoke emits tracing events for successful and failed calls")
{
    json_invoke::JsonInvokeAdapter adapter;
    std::vector<json_invoke::TraceEvent> events;

    adapter.setTraceSink([&events](const json_invoke::TraceEvent& event) {
        events.push_back(event);
    });

    adapter.registerFunction(
        "sum",
        json_invoke::readOnly([](int left, int right) { return left + right; }),
        func_registry::FunctionMetadata{{"left", "right"}, "Add two integers."});

    const auto success = adapter.invokeJson(
        {{"name", "sum"}, {"args", {{"left", 2}, {"right", 5}}}});
    REQUIRE(success.at("ok").get<bool>());

    const auto failure = adapter.invokeJson(
        {{"name", "sum"}, {"args", {{"left", 2}}}});
    CHECK_FALSE(failure.at("ok").get<bool>());

    REQUIRE(events.size() == 4);
    CHECK(events[0].kind == json_invoke::TraceEventKind::invoke_started);
    CHECK(events[0].timestamp.time_since_epoch().count() > 0);
    CHECK(!events[0].request_id.empty());
    CHECK(events[0].tool_name == "sum");
    CHECK(events[0].duration.has_value());
    CHECK(events[0].payload.at("request").at("name").get<std::string>() == "sum");

    CHECK(events[1].kind == json_invoke::TraceEventKind::invoke_finished);
    CHECK(events[1].timestamp.time_since_epoch().count() > 0);
    CHECK(events[1].request_id == events[0].request_id);
    REQUIRE(events[1].duration.has_value());
    CHECK(events[1].duration->count() >= events[0].duration->count());
    CHECK(events[1].payload.at("response").at("ok").get<bool>());
    CHECK(events[1].payload.at("response").at("value").get<int>() == 7);

    CHECK(events[2].kind == json_invoke::TraceEventKind::invoke_started);
    CHECK(events[2].request_id != events[0].request_id);
    CHECK(events[3].kind == json_invoke::TraceEventKind::invoke_failed);
    CHECK(events[3].timestamp.time_since_epoch().count() > 0);
    CHECK(events[3].request_id == events[2].request_id);
    REQUIRE(events[3].duration.has_value());
    CHECK(events[3].duration->count() >= events[2].duration->count());
    CHECK(events[3].payload.at("error").at("code").get<std::string>() == "invalid_request");
    CHECK(events[3].payload.at("error").at("message").get<std::string>().find("right") != std::string::npos);
}

TEST_CASE("vector trace recorder captures structured trace events")
{
    json_invoke::JsonInvokeAdapter adapter;
    json_invoke::VectorTraceRecorder recorder;

    adapter.setTraceSink(recorder.sink());
    adapter.registerFunction(
        "sum",
        json_invoke::readOnly([](int left, int right) { return left + right; }),
        func_registry::FunctionMetadata{{"left", "right"}, "Add two integers."});

    const auto response = adapter.invokeJson(
        {{"name", "sum"}, {"args", {{"left", 2}, {"right", 5}}}});
    REQUIRE(response.at("ok").get<bool>());

    REQUIRE(recorder.events().size() == 2);
    const auto trace_json = recorder.toJson();
    REQUIRE(trace_json.size() == 2);
    CHECK(trace_json[0].at("event").get<std::string>() == "invoke_started");
    CHECK(trace_json[0].at("payload").at("request").at("name").get<std::string>() == "sum");
    CHECK(trace_json[1].at("event").get<std::string>() == "invoke_finished");
    CHECK(trace_json[1].at("request_id").get<std::string>() == trace_json[0].at("request_id").get<std::string>());
    CHECK(trace_json[1].at("payload").at("response").at("value").get<int>() == 7);
}

TEST_CASE("json_invoke tool metadata matches JSON snapshots")
{
    const auto adapter = makeSnapshotAdapter();

    test_support::checkSnapshot("tool_schemas_snapshot.json", adapter.getAllToolSchemasJson().dump(2));
}