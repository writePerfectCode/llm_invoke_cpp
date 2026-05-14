#include <chrono>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <json_session_invoke/json_session_invoke.hpp>
#include <task_scheduler/request_classifier.hpp>
#include <task_scheduler/task_scheduler_facade.hpp>
#include <task_scheduler/task_scheduler.hpp>

namespace {

using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

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

std::string categoryName(task_scheduler::SchedulingCategory category)
{
    switch (category)
    {
    case task_scheduler::SchedulingCategory::FreeReadOnly:
        return "FreeReadOnly";
    case task_scheduler::SchedulingCategory::ObjectExclusive:
        return "ObjectExclusive";
    case task_scheduler::SchedulingCategory::FactoryLane:
        return "FactoryLane";
    case task_scheduler::SchedulingCategory::ToolExclusive:
        return "ToolExclusive";
    case task_scheduler::SchedulingCategory::SessionBarrier:
        return "SessionBarrier";
    }

    return "Unknown";
}

std::string threadIdText()
{
    std::ostringstream stream;
    stream << std::this_thread::get_id();
    return stream.str();
}

void printLine(const Clock::time_point& started_at, std::mutex& output_mutex, const std::string& message)
{
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started_at);
    std::lock_guard<std::mutex> lock(output_mutex);
    std::cout << "[" << elapsed.count() << " ms] " << message << std::endl;
}

void printResponse(const std::string& label, const json_session_invoke::json& response)
{
    std::cout << "\n--- " << label << " response ---" << std::endl;
    std::cout << response.dump(2) << std::endl;
}

json_session_invoke::SessionObjectHandle createCounter(
    task_scheduler::TaskSchedulerFacadeThreadSafe& scheduler,
    int initial)
{
    auto response = scheduler.submitRequest(json_session_invoke::json{
        {"name", "create_counter"},
        {"args", {{"initial", initial}}},
    }).get();
    return response.at("value").get<json_session_invoke::SessionObjectHandle>();
}

} // namespace

int main()
{
    json_session_invoke::JsonSessionInvokeAdapterThreadSafe adapter;
    task_scheduler::TaskSchedulerFacadeThreadSafe scheduler(adapter, 3);

    adapter.registerFunction(
        "sum",
        json_invoke::readOnly([](int left, int right) {
            std::this_thread::sleep_for(120ms);
            return left + right;
        }),
        json_invoke::FunctionMetadata{{"left", "right"}, "Add two integers without session state."});

    adapter
        .stateful<Counter>("counter")
        .create(
            [](int initial) {
                std::this_thread::sleep_for(10ms);
                return std::make_shared<Counter>(initial);
            },
            json_invoke::FunctionMetadata{{"initial"}, "Create one in-memory counter object."})
        .method(
            "counter_add",
            [](Counter& counter, int delta) {
                std::this_thread::sleep_for(300ms);
                counter.add(delta);
            },
            json_invoke::FunctionMetadata{{"delta"}, "Add one delta to the counter state."})
        .method(
            "counter_value",
            [](const Counter& counter) {
                std::this_thread::sleep_for(80ms);
                return counter.current();
            },
            "Read the current counter value from one handle.")
        .destroy();

    const auto counter_a = createCounter(scheduler, 5);
    const auto counter_b = createCounter(scheduler, 20);

    std::cout << "Created counter A: " << counter_a.object_id << std::endl;
    std::cout << "Created counter B: " << counter_b.object_id << std::endl;

    const auto started_at = Clock::now();
    std::mutex output_mutex;

    const auto submit_and_log =
        [&](std::string label, json_session_invoke::json request) {
            auto label_text = std::make_shared<std::string>(std::move(label));
            task_scheduler::RequestExecutionObserver observer;
            observer.on_classified = [&, label_text](const task_scheduler::TaskExecutionPlan& plan) {
                const std::string scheduling_key = plan.scheduling_key.has_value() ? plan.scheduling_key->value : "-";
                printLine(
                    started_at,
                    output_mutex,
                    *label_text + " classified as " + categoryName(plan.category) + " (key=" + scheduling_key + ")");
            };
            observer.on_started = [&, label_text]() {
                printLine(
                    started_at,
                    output_mutex,
                    *label_text + " started on worker " + threadIdText());
            };
            observer.on_finished = [&, label_text](const json_session_invoke::json& response) {
                printLine(
                    started_at,
                    output_mutex,
                    *label_text + " finished with ok=" + std::string(response.at("ok").get<bool>() ? "true" : "false"));
            };

            return scheduler.submitRequest(
                std::move(request),
                std::move(observer));
        };

    auto add_a = submit_and_log(
        "A add +7",
        {{"name", "counter_add"}, {"args", {{"handle", counter_a}, {"delta", 7}}}});

    auto read_a = submit_and_log(
        "A read via string handle",
        {{"name", "counter_value"}, {"args", {{"handle", counter_a.object_id}}}});

    auto read_b = submit_and_log(
        "B read",
        {{"name", "counter_value"}, {"args", {{"handle", counter_b.object_id}}}});

    auto sum = submit_and_log(
        "stateless sum",
        {{"name", "sum"}, {"args", {{"left", 2}, {"right", 5}}}});

    auto create_c = submit_and_log(
        "create counter C",
        {{"name", "create_counter"}, {"args", {{"initial", 99}}}});

    const auto add_response = add_a.get();
    const auto read_a_response = read_a.get();
    const auto read_b_response = read_b.get();
    const auto sum_response = sum.get();
    const auto create_c_response = create_c.get();

    printResponse("A add +7", add_response);
    printResponse("A read via string handle", read_a_response);
    printResponse("B read", read_b_response);
    printResponse("stateless sum", sum_response);
    printResponse("create counter C", create_c_response);

    std::cout << "\nExpected ordering to observe:" << std::endl;
    std::cout << "1. A add +7 and A read share the same object_id, so A read starts only after A add +7 finishes." << std::endl;
    std::cout << "2. B read uses a different object_id, so it can run while A add +7 is still active." << std::endl;
    std::cout << "3. create counter C is FactoryLane(counter), so it can run independently from object lanes and free-read work when a worker is available." << std::endl;

    return 0;
}