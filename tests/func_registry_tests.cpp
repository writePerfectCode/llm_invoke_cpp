#include <doctest/doctest.h>

#include <any>
#include <stdexcept>
#include <string>
#include <vector>

#include <func_registry/func_registry.hpp>

TEST_CASE("func_registry supports typed invocation and summaries")
{
    func_registry::FuncRegistry registry;

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
    func_registry::FuncRegistry registry;

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
    func_registry::FuncRegistry registry;

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