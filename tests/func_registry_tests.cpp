#include <doctest/doctest.h>

#include <atomic>
#include <any>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#include <func_registry/func_registry.hpp>

TEST_CASE("func_registry supports typed invocation and summaries")
{
    func_registry::FuncRegistryThreadSafe registry;

    registry.registerFunction(
        "sum",
        [](int lhs, int rhs) { return lhs + rhs; },
        func_registry::FunctionMetadata{{"lhs", "rhs"}, "Add two integers."});

    CHECK(registry.callByNameAs<int>("sum", 2, 3) == 5);

    const auto wrapped = registry.callByNameWrap("sum", 4, 6);
    CHECK(wrapped.declaredReturnType() == typeid(int));
    CHECK(wrapped.as<int>() == 10);

    const std::string description = func_registry::describeFunction(registry, "sum");
    CHECK(description.find("sum(") == 0);
    CHECK(description.find("lhs") != std::string::npos);
    CHECK(description.find("rhs") != std::string::npos);
    CHECK(description.find("Add two integers.") != std::string::npos);
}

TEST_CASE("func_registry sorts descriptions and validates metadata")
{
    func_registry::FuncRegistryThreadSafe registry;

    registry.registerFunction("beta", [] { return 2; }, "Second function.");
    registry.registerFunction("alpha", [] { return 1; }, "First function.");

    const auto descriptions = func_registry::describeAllFunctions(registry);
    REQUIRE(descriptions.size() == 2);
    CHECK(descriptions[0].find("alpha(") == 0);
    CHECK(descriptions[1].find("beta(") == 0);

    CHECK_THROWS_WITH_AS(
        registry.registerFunction(
            "bad_metadata",
            [](int value) { return value; },
            func_registry::FunctionMetadata{{"value", "extra"}, "Broken metadata."}),
        doctest::Contains("parameter name count mismatch"),
        std::invalid_argument);
}

TEST_CASE("func_registry reports duplicate registration and invocation errors")
{
    func_registry::FuncRegistryThreadSafe registry;

    registry.registerFunction("sum", [](int lhs, int rhs) { return lhs + rhs; });

    CHECK_THROWS_WITH_AS(
        registry.registerFunction("sum", [](int lhs, int rhs) { return lhs - rhs; }),
        doctest::Contains("function already registered"),
        std::runtime_error);

    CHECK_THROWS_WITH_AS(
        registry.callByName("missing", 1, 2),
        doctest::Contains("function not found"),
        std::runtime_error);

    const std::vector<std::any> bad_args{std::string("bad"), 2};
    CHECK_THROWS_WITH_AS(
        registry.callByName("sum", bad_args),
        doctest::Contains("type mismatch at arg[0]"),
        std::runtime_error);
}

TEST_CASE("func_registry exposes explicit thread-safe and unsafe aliases")
{
    static_assert(
        std::is_same_v<
            func_registry::FuncRegistryThreadSafe,
            func_registry::BasicFuncRegistry<true>>);
    static_assert(
        !std::is_same_v<
            func_registry::FuncRegistryThreadSafe,
            func_registry::FuncRegistryUnsafe>);
}

TEST_CASE("func_registry thread-safe alias supports concurrent invocation")
{
    func_registry::FuncRegistryThreadSafe registry;
    registry.registerFunction(
        "sum",
        [](int lhs, int rhs) { return lhs + rhs; },
        func_registry::FunctionMetadata{{"lhs", "rhs"}, "Add two integers."});

    std::atomic<bool> failed{false};
    std::vector<std::thread> workers;
    workers.reserve(8);

    for (int worker = 0; worker < 8; ++worker)
    {
        workers.emplace_back([&registry, &failed]() {
            for (int iteration = 0; iteration < 100; ++iteration)
            {
                if (registry.callByNameAs<int>("sum", 2, 3) != 5)
                {
                    failed.store(true, std::memory_order_relaxed);
                    return;
                }
            }
        });
    }

    for (auto& worker : workers)
    {
        worker.join();
    }

    CHECK_FALSE(failed.load(std::memory_order_relaxed));
}

TEST_CASE("func_registry forEachFunction snapshots entries before invoking the visitor")
{
    func_registry::FuncRegistryThreadSafe registry;
    registry.registerFunction("alpha", [] { return 1; }, "First function.");

    std::size_t visited = 0;
    registry.forEachFunction([&](std::string_view name, const func_registry::AnyCallable&) {
        ++visited;
        if (name == "alpha")
        {
            registry.registerFunction("beta", [] { return 2; }, "Second function.");
        }
    });

    CHECK(visited == 1);
    CHECK(registry.callByNameAs<int>("beta") == 2);

    const auto descriptions = func_registry::describeAllFunctions(registry);
    REQUIRE(descriptions.size() == 2);
}