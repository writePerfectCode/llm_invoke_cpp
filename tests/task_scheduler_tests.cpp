#include <doctest/doctest.h>

#include <chrono>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>

#include <func_registry/func_registry.hpp>
#include <task_scheduler/request_classifier.hpp>
#include <task_scheduler/task_scheduler_facade.hpp>
#include <task_scheduler/task_scheduler.hpp>

namespace {

using namespace std::chrono_literals;

struct Counter {
    int value{0};

    explicit Counter(int initial = 0)
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

} // namespace

task_scheduler::TaskExecutionPlan makePlan(
    std::string tool_name,
    task_scheduler::SchedulingCategory category,
    std::optional<std::string> scheduling_key = std::nullopt)
{
    task_scheduler::TaskExecutionPlan plan;
    plan.tool_name = std::move(tool_name);
    plan.category = category;
    if (scheduling_key.has_value())
    {
        plan.scheduling_key = task_scheduler::SchedulingKey{std::move(*scheduling_key)};
    }

    return plan;
}

TEST_CASE("task_scheduler request classifier derives scheduling plans from session tool metadata")
{
    json_session_invoke::JsonSessionInvokeAdapterThreadSafe adapter;
    task_scheduler::RequestClassifierThreadSafe classifier(adapter);

    std::string log;
    adapter.registerFunction(
        "sum",
        json_invoke::readOnly([](int left, int right) { return left + right; }),
        func_registry::FunctionMetadata{{"left", "right"}, "Add two integers without session state."});
    adapter.registerFunction(
        "append_log",
        json_invoke::mutating([&log](std::string suffix) {
            log += suffix;
            return log.size();
        }),
        func_registry::FunctionMetadata{{"suffix"}, "Append one suffix to shared state."});
    adapter.registerFunction(
        "reset_session",
        json_invoke::mutating([]() { return true; }),
        func_registry::FunctionMetadata{{}, "Reset cross-session shared state."},
        json_session_invoke::JsonSessionInvokeAdapterThreadSafe::ToolSchedulingScope::session_barrier);

    adapter
        .stateful<Counter>("counter")
        .create(
            "build_counter",
            [](int initial) { return std::make_shared<Counter>(initial); },
            func_registry::FunctionMetadata{{"initial"}, "Create one counter for scheduling tests."})
        .method(
            "counter_read",
            &Counter::current,
            func_registry::FunctionMetadata{{"counter_ref"}, "Read one counter value."})
        .method(
            "counter_add_with_ref",
            &Counter::add,
            func_registry::FunctionMetadata{{"counter_ref", "delta"}, "Mutate one counter value."})
        .destroy(
            "dispose_counter",
            func_registry::FunctionMetadata{{"counter_ref"}, "Destroy one counter."});

    const auto stateless_read_only = classifier.classify(
        json_session_invoke::json{{"name", "sum"}, {"args", {{"left", 2}, {"right", 5}}}});
    CHECK(stateless_read_only.category == task_scheduler::SchedulingCategory::FreeReadOnly);
    CHECK_FALSE(stateless_read_only.scheduling_key.has_value());

    const auto stateless_mutating = classifier.classify(
        json_session_invoke::json{{"name", "append_log"}, {"args", {{"suffix", "!"}}}});
    CHECK(stateless_mutating.category == task_scheduler::SchedulingCategory::ToolExclusive);
    REQUIRE(stateless_mutating.scheduling_key.has_value());
    CHECK(stateless_mutating.scheduling_key->value == "append_log");

    const auto factory_plan = classifier.classify(
        json_session_invoke::json{{"name", "build_counter"}, {"args", {{"initial", 3}}}});
    CHECK(factory_plan.category == task_scheduler::SchedulingCategory::FactoryLane);
    REQUIRE(factory_plan.scheduling_key.has_value());
    CHECK(factory_plan.scheduling_key->value == "counter");

    const auto method_handle_string = classifier.classify(
        json_session_invoke::json{{"name", "counter_read"}, {"args", {{"counter_ref", "obj_7"}}}});
    CHECK(method_handle_string.category == task_scheduler::SchedulingCategory::ObjectExclusive);
    REQUIRE(method_handle_string.scheduling_key.has_value());
    CHECK(method_handle_string.scheduling_key->value == "obj_7");

    const auto method_handle_object = classifier.classify(
        json_session_invoke::json{{"name", "counter_add_with_ref"}, {"args", {{"counter_ref", {{"object_id", "obj_9"}, {"object_type", "counter"}}}, {"delta", 1}}}});
    CHECK(method_handle_object.category == task_scheduler::SchedulingCategory::ObjectExclusive);
    REQUIRE(method_handle_object.scheduling_key.has_value());
    CHECK(method_handle_object.scheduling_key->value == "obj_9");

    CHECK_THROWS_AS(
        classifier.classify(
            json_session_invoke::json{{"name", "counter_read"}, {"args", {{"counter_ref", {{"value", 11}}}}}}),
        json_invoke::JsonInvokeError);

    CHECK_THROWS_AS(
        classifier.classify(
            json_session_invoke::json{{"name", "dispose_counter"}, {"args", {{"counter_ref", {{"object_type", "counter"}}}}}}),
        json_invoke::JsonInvokeError);

    const auto reset_plan = classifier.classify(
        json_session_invoke::json{{"name", "reset_session"}, {"args", json_session_invoke::json::object()}});
    CHECK(reset_plan.category == task_scheduler::SchedulingCategory::SessionBarrier);
    CHECK_FALSE(reset_plan.scheduling_key.has_value());

    CHECK_THROWS_AS(
        classifier.classify(json_session_invoke::json{{"name", "missing_tool"}, {"args", json_session_invoke::json::object()}}),
        json_invoke::JsonInvokeError);
}

TEST_CASE("task_scheduler facade exposes one-step request submission with optional observers")
{
    json_session_invoke::JsonSessionInvokeAdapterThreadSafe adapter;
    task_scheduler::TaskSchedulerFacadeThreadSafe facade(adapter, 2);

    adapter.registerFunction(
        "sum",
        json_invoke::readOnly([](int left, int right) { return left + right; }),
        func_registry::FunctionMetadata{{"left", "right"}, "Add two integers without session state."});

    task_scheduler::SchedulingCategory observed_category = task_scheduler::SchedulingCategory::SessionBarrier;
    bool started = false;
    bool finished = false;

    task_scheduler::RequestExecutionObserver observer;
    observer.on_classified = [&](const task_scheduler::TaskExecutionPlan& plan) {
        observed_category = plan.category;
    };
    observer.on_started = [&]() {
        started = true;
    };
    observer.on_finished = [&](const json_session_invoke::json&) {
        finished = true;
    };

    const auto response = facade.submitRequest(
        json_session_invoke::json{{"name", "sum"}, {"args", {{"left", 2}, {"right", 5}}}},
        std::move(observer)).get();

    CHECK(observed_category == task_scheduler::SchedulingCategory::FreeReadOnly);
    CHECK(started);
    CHECK(finished);
    CHECK(response.at("ok").get<bool>());
    CHECK(response.at("value").get<int>() == 7);
}

TEST_CASE("task_scheduler keyed scheduler serializes tasks for the same object id")
{
    task_scheduler::KeyedTaskScheduler scheduler(2);

    std::promise<void> first_started_promise;
    auto first_started = first_started_promise.get_future();
    std::promise<void> release_first_promise;
    auto release_first = release_first_promise.get_future().share();
    std::promise<void> second_started_promise;
    auto second_started = second_started_promise.get_future();

    auto first = scheduler.submit(task_scheduler::makeScheduledTask(
        makePlan("counter_read", task_scheduler::SchedulingCategory::ObjectExclusive, "obj_1"),
        [release_first = release_first, &first_started_promise]() mutable {
            first_started_promise.set_value();
            release_first.wait();
            return json_session_invoke::json{{"task", 1}};
        }));

    REQUIRE(first_started.wait_for(1s) == std::future_status::ready);

    auto second = scheduler.submit(task_scheduler::makeScheduledTask(
        makePlan("counter_read", task_scheduler::SchedulingCategory::ObjectExclusive, "obj_1"),
        [&second_started_promise]() {
            second_started_promise.set_value();
            return json_session_invoke::json{{"task", 2}};
        }));

    CHECK(second_started.wait_for(100ms) == std::future_status::timeout);

    release_first_promise.set_value();

    CHECK(second_started.wait_for(1s) == std::future_status::ready);
    CHECK(first.get().at("task").get<int>() == 1);
    CHECK(second.get().at("task").get<int>() == 2);
}

TEST_CASE("task_scheduler keyed scheduler serializes tool-exclusive tasks for the same tool key")
{
    task_scheduler::KeyedTaskScheduler scheduler(2);

    std::promise<void> first_started_promise;
    auto first_started = first_started_promise.get_future();
    std::promise<void> release_first_promise;
    auto release_first = release_first_promise.get_future().share();
    std::promise<void> second_started_promise;
    auto second_started = second_started_promise.get_future();

    auto first = scheduler.submit(task_scheduler::makeScheduledTask(
        makePlan("append_log", task_scheduler::SchedulingCategory::ToolExclusive, "append_log"),
        [release_first = release_first, &first_started_promise]() mutable {
            first_started_promise.set_value();
            release_first.wait();
            return json_session_invoke::json{{"task", 1}};
        }));

    REQUIRE(first_started.wait_for(1s) == std::future_status::ready);

    auto second = scheduler.submit(task_scheduler::makeScheduledTask(
        makePlan("append_log", task_scheduler::SchedulingCategory::ToolExclusive, "append_log"),
        [&second_started_promise]() {
            second_started_promise.set_value();
            return json_session_invoke::json{{"task", 2}};
        }));

    CHECK(second_started.wait_for(100ms) == std::future_status::timeout);

    release_first_promise.set_value();

    CHECK(second_started.wait_for(1s) == std::future_status::ready);
    CHECK(first.get().at("task").get<int>() == 1);
    CHECK(second.get().at("task").get<int>() == 2);
}

TEST_CASE("task_scheduler keyed scheduler gives session-barrier tasks global exclusivity")
{
    task_scheduler::KeyedTaskScheduler scheduler(3);

    std::promise<void> object_started_promise;
    auto object_started = object_started_promise.get_future();
    std::promise<void> release_object_promise;
    auto release_object = release_object_promise.get_future().share();

    std::promise<void> free_started_promise;
    auto free_started = free_started_promise.get_future();
    std::promise<void> release_free_promise;
    auto release_free = release_free_promise.get_future().share();

    std::promise<void> session_started_promise;
    auto session_started = session_started_promise.get_future();
    std::promise<void> release_session_promise;
    auto release_session = release_session_promise.get_future().share();

    std::promise<void> later_free_started_promise;
    auto later_free_started = later_free_started_promise.get_future();

    auto object_task = scheduler.submit(task_scheduler::makeScheduledTask(
        makePlan("counter_read", task_scheduler::SchedulingCategory::ObjectExclusive, "obj_1"),
        [release_object = release_object, &object_started_promise]() mutable {
            object_started_promise.set_value();
            release_object.wait();
            return json_session_invoke::json{{"task", "object"}};
        }));

    REQUIRE(object_started.wait_for(1s) == std::future_status::ready);

    auto free_task = scheduler.submit(task_scheduler::makeScheduledTask(
        makePlan("sum", task_scheduler::SchedulingCategory::FreeReadOnly),
        [release_free = release_free, &free_started_promise]() mutable {
            free_started_promise.set_value();
            release_free.wait();
            return json_session_invoke::json{{"task", "free"}};
        }));

    REQUIRE(free_started.wait_for(1s) == std::future_status::ready);

    auto session_task = scheduler.submit(task_scheduler::makeScheduledTask(
        makePlan("reset_session", task_scheduler::SchedulingCategory::SessionBarrier),
        [release_session = release_session, &session_started_promise]() mutable {
            session_started_promise.set_value();
            release_session.wait();
            return json_session_invoke::json{{"task", "session"}};
        }));

    CHECK(session_started.wait_for(100ms) == std::future_status::timeout);

    release_object_promise.set_value();
    release_free_promise.set_value();

    REQUIRE(session_started.wait_for(1s) == std::future_status::ready);

    auto later_free_task = scheduler.submit(task_scheduler::makeScheduledTask(
        makePlan("sum", task_scheduler::SchedulingCategory::FreeReadOnly),
        [&later_free_started_promise]() {
            later_free_started_promise.set_value();
            return json_session_invoke::json{{"task", "later_free"}};
        }));

    CHECK(later_free_started.wait_for(100ms) == std::future_status::timeout);

    release_session_promise.set_value();

    CHECK(later_free_started.wait_for(1s) == std::future_status::ready);
    CHECK(object_task.get().at("task").get<std::string>() == "object");
    CHECK(free_task.get().at("task").get<std::string>() == "free");
    CHECK(session_task.get().at("task").get<std::string>() == "session");
    CHECK(later_free_task.get().at("task").get<std::string>() == "later_free");
}