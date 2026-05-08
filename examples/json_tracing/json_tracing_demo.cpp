#include <chrono>
#include <iostream>
#include <memory>
#include <string>

#include <tools/trace_recorder.hpp>
#include <json_session_invoke/json_session_invoke.hpp>

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

void printSection(const std::string& label)
{
    std::cout << "\n=== " << label << " ===" << std::endl;
}

void printResponse(const std::string& label, const json_invoke::json& response)
{
    std::cout << "\n[response] " << label << std::endl;
    std::cout << response.dump(2) << std::endl;
}

void installTracePrinter(json_session_invoke::JsonSessionInvokeAdapterThreadSafe& adapter)
{
    adapter.setTraceSink([](const json_invoke::TraceEvent& event) {
        std::cout << "\n[trace]" << std::endl;
        std::cout << json_invoke::traceEventToJson(event).dump(2) << std::endl;
    });
}

void registerDemoTools(
    json_session_invoke::JsonSessionInvokeAdapterThreadSafe& adapter,
    json_session_invoke::SessionObjectOptions counter_options)
{
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
}

} // namespace

int main()
{
    json_session_invoke::JsonSessionInvokeAdapterThreadSafe adapter;
    installTracePrinter(adapter);

    json_session_invoke::SessionObjectOptions counter_options;
    counter_options.serialized = true;
    registerDemoTools(adapter, counter_options);

    printSection("Stateless Success And Failure");
    printResponse(
        "sum",
        adapter.invokeJson({
            {"name", "sum"},
            {"args", {{"left", 2}, {"right", 5}}},
        }));

    printResponse(
        "sum missing argument",
        adapter.invokeJson({
            {"name", "sum"},
            {"args", {{"left", 2}}},
        }));

    printSection("Stateful Lifecycle");
    const auto create_response = adapter.invokeJson({
        {"name", "create_counter"},
        {"args", {{"initial", 10}}},
    });
    printResponse("create_counter", create_response);

    const auto counter = create_response.at("value").get<json_session_invoke::SessionObjectHandle>();

    printResponse(
        "counter_add",
        adapter.invokeJson({
            {"name", "counter_add"},
            {"args", {{"handle", counter}, {"delta", 3}}},
        }));

    printResponse(
        "counter_value",
        adapter.invokeJson({
            {"name", "counter_value"},
            {"args", {{"handle", counter}}},
        }));

    printResponse(
        "destroy_counter",
        adapter.invokeJson({
            {"name", "destroy_counter"},
            {"args", {{"handle", counter}}},
        }));

    printSection("Expiration Path");
    json_session_invoke::JsonSessionInvokeAdapterThreadSafe expiration_adapter;
    installTracePrinter(expiration_adapter);

    json_session_invoke::SessionObjectOptions expiration_options;
    expiration_options.serialized = true;
    expiration_options.idle_timeout = std::chrono::milliseconds{0};
    registerDemoTools(expiration_adapter, expiration_options);

    const auto expiring_counter_response = expiration_adapter.invokeJson({
        {"name", "create_counter"},
        {"args", {{"initial", 1}}},
    });
    const auto expiring_counter = expiring_counter_response.at("value").get<json_session_invoke::SessionObjectHandle>();

    printResponse(
        "counter_value after idle expiration",
        expiration_adapter.invokeJson({
            {"name", "counter_value"},
            {"args", {{"handle", expiring_counter}}},
        }));

    return 0;
}