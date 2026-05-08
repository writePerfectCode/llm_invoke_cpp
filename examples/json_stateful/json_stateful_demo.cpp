#include <iostream>
#include <memory>
#include <string>

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

void printResponse(const std::string& label, const json_invoke::json& response)
{
    std::cout << "\n--- " << label << " ---" << std::endl;
    std::cout << response.dump(2) << std::endl;
}

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

int main()
{
    json_session_invoke::JsonSessionInvokeAdapterThreadSafe adapter;
    json_session_invoke::SessionObjectOptions counter_options;
    counter_options.serialized = true;

    adapter.registerFunction(
        "sum",
        json_invoke::readOnly([](int left, int right) { return left + right; }),
        json_invoke::FunctionMetadata{{"left", "right"}, "Add two integers without session state."});

    adapter
        .stateful<Counter>("counter")
        .options(counter_options)
        .create(
            [](int initial) { return std::make_shared<Counter>(initial); },
            json_invoke::FunctionMetadata{{"initial"}, "Create one in-memory counter object."})
        .method(
            "counter_add",
            &Counter::add,
            json_invoke::FunctionMetadata{{"delta"}, "Add one delta to the counter state."})
        .method(
            "counter_value",
            &Counter::current,
            "Read the current counter value from one handle.")
        .destroy();

    printResponse(
        "Stateless sum",
        adapter.invokeJson({
            {"name", "sum"},
            {"args", {{"left", 2}, {"right", 5}}},
        }));

    const auto create_response = adapter.invokeJson({
        {"name", "create_counter"},
        {"args", {{"initial", 5}}},
    });
    printResponse("Create counter", create_response);

    const json_session_invoke::SessionObjectHandle counter =
        create_response.at("value").get<json_session_invoke::SessionObjectHandle>();

    printResponse(
        "Add by handle object",
        adapter.invokeJson({
            {"name", "counter_add"},
            {"args", {{"handle", counter}, {"delta", 7}}},
        }));

    printResponse(
        "Read counter value",
        adapter.invokeJson({
            {"name", "counter_value"},
            {"args", {{"handle", counter}}},
        }));

    printResponse(
        "Read counter value with string shorthand",
        adapter.invokeJson({
            {"name", "counter_value"},
            {"args", {{"handle", counter.object_id}}},
        }));

    printResponse(
        "Read counter value with direct object JSON",
        adapter.invokeJson({
            {"name", "counter_value"},
            {"args", {{"handle", {{"value", 19}}}}},
        }));

    printResponse(
        "Destroy counter",
        adapter.invokeJson({
            {"name", "destroy_counter"},
            {"args", {{"handle", counter}}},
        }));

    printResponse(
        "Read after destroy",
        adapter.invokeJson({
            {"name", "counter_value"},
            {"args", {{"handle", counter}}},
        }));

    std::cout << "\n--- Stateful tool schemas ---" << std::endl;
    std::cout << adapter.getAllToolSchemasJson().dump(2) << std::endl;

    return 0;
}