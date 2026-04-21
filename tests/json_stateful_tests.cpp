#include <doctest/doctest.h>

#include <chrono>
#include <memory>
#include <string>

#include <func_registry/func_registry.hpp>
#include <json_session_invoke/json_session_invoke.hpp>

namespace {

struct Counter {
    int value{0};

    Counter() = default;

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

struct CounterHarness {
    json_session_invoke::JsonSessionInvokeAdapter adapter;

    CounterHarness()
    {
        json_session_invoke::SessionObjectOptions counter_options;
        counter_options.serialized = true;

        adapter
            .stateful<Counter>("counter")
            .options(counter_options)
            .create(
                [](int initial) { return std::make_shared<Counter>(initial); },
                func_registry::FunctionMetadata{{"initial"}, "Create an in-memory counter."})
            .method(
                "counter_add",
                &Counter::add,
                func_registry::FunctionMetadata{{"delta"}, "Add to the counter."})
            .method("counter_value", &Counter::current, "Read the current counter value.")
            .destroy();
    }

    json_session_invoke::SessionObjectHandle create(int initial)
    {
        return adapter.invoke<json_session_invoke::SessionObjectHandle>(
            {{"name", "create_counter"}, {"args", {{"initial", initial}}}});
    }
};

} // namespace

template<>
struct json_invoke::json_traits<Counter> {
    static Counter from_json_value(const json_invoke::json& value)
    {
        return Counter{value.at("value").get<int>()};
    }

    static json_invoke::json to_json_value(const Counter& value)
    {
        return {{"value", value.value}};
    }
};

TEST_CASE("json_stateful preserves object state across repeated tool calls")
{
    CounterHarness harness;

    const json_session_invoke::SessionObjectHandle handle = harness.create(4);
    CHECK(handle.object_type == "counter");
    CHECK(!handle.object_id.empty());

    const auto add_response = harness.adapter.invokeJson(
        {{"name", "counter_add"}, {"args", {{"handle", handle}, {"delta", 3}}}});
    CHECK(add_response.at("ok").get<bool>());
    CHECK(add_response.at("value").is_null());

    const auto value_response = harness.adapter.invokeJson(
        {{"name", "counter_value"}, {"args", {{"handle", handle}}}});
    CHECK(value_response.at("ok").get<bool>());
    CHECK(value_response.at("value").get<int>() == 7);

    const auto shorthand_response = harness.adapter.invokeJson(
        {{"name", "counter_value"}, {"args", {{"handle", handle.object_id}}}});
    CHECK(shorthand_response.at("ok").get<bool>());
    CHECK(shorthand_response.at("value").get<int>() == 7);

    const auto direct_object_response = harness.adapter.invokeJson(
        {{"name", "counter_value"}, {"args", {{"handle", json_session_invoke::json{{"value", 11}}}}}});
    CHECK(direct_object_response.at("ok").get<bool>());
    CHECK(direct_object_response.at("value").get<int>() == 11);

    const auto schema = harness.adapter.getToolSchemaJson("counter_add");
    const auto& handle_schema = schema.at("function").at("parameters").at("properties").at("handle");
    CHECK(handle_schema.at("type").get<std::string>() == "object");
    CHECK(handle_schema.at("properties").contains("object_id"));
    CHECK(handle_schema.at("properties").contains("object_type"));
    CHECK(schema.at("function").at("parameters").at("required") == json_session_invoke::json::array({"handle", "delta"}));
}

TEST_CASE("json_stateful reports invalid handles and destroy lifecycle")
{
    CounterHarness harness;

    const json_session_invoke::SessionObjectHandle handle = harness.create(10);

    const auto destroy_response = harness.adapter.invokeJson(
        {{"name", "destroy_counter"}, {"args", {{"handle", handle}}}});
    CHECK(destroy_response.at("ok").get<bool>());
    CHECK(destroy_response.at("value").get<bool>());

    const auto second_destroy_response = harness.adapter.invokeJson(
        {{"name", "destroy_counter"}, {"args", {{"handle", handle}}}});
    CHECK(second_destroy_response.at("ok").get<bool>());
    CHECK_FALSE(second_destroy_response.at("value").get<bool>());

    const auto missing_response = harness.adapter.invokeJson(
        {{"name", "counter_value"}, {"args", {{"handle", handle}}}});
    CHECK_FALSE(missing_response.at("ok").get<bool>());
    CHECK(missing_response.at("error").at("code").get<std::string>() == "invalid_object");
    CHECK(missing_response.at("error").at("message").get<std::string>().find(handle.object_id) != std::string::npos);

    const auto mismatched_type_response = harness.adapter.invokeJson(
        {{"name", "counter_value"}, {"args", {{"handle", {{"object_id", harness.create(3).object_id}, {"object_type", "other"}}}}}});
    CHECK_FALSE(mismatched_type_response.at("ok").get<bool>());
    CHECK(mismatched_type_response.at("error").at("code").get<std::string>() == "object_type_mismatch");
}

TEST_CASE("json_stateful builder auto registers default destroy when omitted")
{
    json_session_invoke::JsonSessionInvokeAdapter adapter;

    adapter
        .stateful<Counter>("counter")
        .create(
            [](int initial) { return std::make_shared<Counter>(initial); },
            func_registry::FunctionMetadata{{"initial"}, "Create an in-memory counter."})
        .method("counter_value", &Counter::current, "Read the current counter value.");

    const auto create_response = adapter.invokeJson(
        {{"name", "create_counter"}, {"args", {{"initial", 6}}}});
    REQUIRE(create_response.at("ok").get<bool>());

    const auto handle = create_response.at("value").get<json_session_invoke::SessionObjectHandle>();

    const auto destroy_schema = adapter.getToolSchemaJson("destroy_counter");
    CHECK(destroy_schema.at("function").at("name").get<std::string>() == "destroy_counter");
    CHECK(destroy_schema.at("function").at("x-stateful-kind").get<std::string>() == "destroy");
    CHECK(destroy_schema.at("function").at("x-handle-source-tool").get<std::string>() == "create_counter");

    const auto destroy_response = adapter.invokeJson(
        {{"name", "destroy_counter"}, {"args", {{"handle", handle}}}});
    CHECK(destroy_response.at("ok").get<bool>());
    CHECK(destroy_response.at("value").get<bool>());
}

TEST_CASE("json_stateful builder can customize auto destroy defaults")
{
    json_session_invoke::JsonSessionInvokeAdapter adapter;
    adapter.statefulDefaults().destroy_description = "Release one previously created counter handle.";

    adapter
        .stateful<Counter>("counter")
        .create(
            [](int initial) { return std::make_shared<Counter>(initial); },
            func_registry::FunctionMetadata{{"initial"}, "Create an in-memory counter."})
        .method("counter_value", &Counter::current, "Read the current counter value.");

    const auto destroy_schema = adapter.getToolSchemaJson("destroy_counter");
    CHECK(destroy_schema.at("function").at("description").get<std::string>().find("Release one previously created counter handle.") != std::string::npos);
}

TEST_CASE("json_stateful builder can disable auto destroy registration")
{
    json_session_invoke::JsonSessionInvokeAdapter adapter;
    adapter.statefulDefaults().auto_register_destroy = false;

    adapter
        .stateful<Counter>("counter")
        .create(
            [](int initial) { return std::make_shared<Counter>(initial); },
            func_registry::FunctionMetadata{{"initial"}, "Create an in-memory counter."})
        .method("counter_value", &Counter::current, "Read the current counter value.");

    CHECK_FALSE(adapter.isFunctionRegistered("destroy_counter"));
}

TEST_CASE("json_stateful builder does not auto register default destroy after custom destroy")
{
    json_session_invoke::JsonSessionInvokeAdapter adapter;

    adapter
        .stateful<Counter>("counter")
        .create(
            [](int initial) { return std::make_shared<Counter>(initial); },
            func_registry::FunctionMetadata{{"initial"}, "Create an in-memory counter."})
        .method("counter_value", &Counter::current, "Read the current counter value.")
        .destroy("release_counter");

    CHECK(adapter.isFunctionRegistered("release_counter"));
    CHECK_FALSE(adapter.isFunctionRegistered("destroy_counter"));

    const auto create_response = adapter.invokeJson(
        {{"name", "create_counter"}, {"args", {{"initial", 9}}}});
    REQUIRE(create_response.at("ok").get<bool>());

    const auto handle = create_response.at("value").get<json_session_invoke::SessionObjectHandle>();

    const auto destroy_response = adapter.invokeJson(
        {{"name", "release_counter"}, {"args", {{"handle", handle}}}});
    CHECK(destroy_response.at("ok").get<bool>());
    CHECK(destroy_response.at("value").get<bool>());
}

TEST_CASE("json_stateful can lazily clean up expired handles")
{
    json_session_invoke::JsonSessionInvokeAdapter adapter;
    json_session_invoke::SessionObjectOptions expiring_options;
    expiring_options.idle_timeout = std::chrono::milliseconds{0};

    adapter
        .stateful<Counter>("counter")
        .options(expiring_options)
        .create(
            [] { return std::make_shared<Counter>(1); },
            func_registry::FunctionMetadata{{}, "Create an immediately expiring counter."})
        .destroy();

    const json_session_invoke::SessionObjectHandle handle = adapter.invoke<json_session_invoke::SessionObjectHandle>(
        {{"name", "create_counter"}});

    const auto expired_response = adapter.invokeJson(
        {{"name", "create_counter"}, {"args", json_invoke::json::object()}});
    CHECK(expired_response.at("ok").get<bool>());

    const auto stale_lookup_response = adapter.invokeJson(
        {{"name", "destroy_counter"}, {"args", {{"handle", handle}}}});
    CHECK(stale_lookup_response.at("ok").get<bool>());
    CHECK_FALSE(stale_lookup_response.at("value").get<bool>());
}