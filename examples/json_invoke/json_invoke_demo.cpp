#include <iostream>
#include <string>
#include "person_support.hpp"
#include <func_registry/func_registry.hpp>
#include <json_invoke/json_invoke.hpp>

int main()
{
    func_registry::FuncRegistry registry;
    json_invoke::JsonInvokeAdapter adapter(registry);

    adapter.registerFunction("add", [](int x, int y) { return x + y; }, "Add two integers.");
    adapter.registerFunction("getPerson", getPerson, "Construct one person.");
    adapter.registerFunction(
        "describePerson",
        &Person::describe,
        func_registry::FunctionMetadata{{"person"}, "Call Person::describe() for one person."});

    //#1
    std::cout << "--- Invoke add as int ---" << std::endl;
    int sum = adapter.invoke({
        {"name", "add"},
        {"args", {8, 13}},
    });
    std::cout << sum << std::endl;

    //#2
    std::cout << "\n--- Invoke LLM-style payload as int ---" << std::endl;
    json_invoke::json llm_tool_call_request = {
        {"type", "function"},
        {"function", {
            {"name", "add"},
            {"arguments", R"({"arg0":21,"arg1":9})"},
        }},
    };
    int llm_sum = adapter.invoke(llm_tool_call_request);
    std::cout << llm_sum << std::endl;

    //#3
    std::cout << "\n--- Invoke getPerson get Person object ---" << std::endl;
    Person person = adapter.invoke({
        {"name", "getPerson"},
    });
    std::cout << person.describe() << std::endl;

    //#4
    std::cout << "\n--- Invoke describePerson get std::string ---" << std::endl;
    std::string description = adapter.invoke({
        {"name", "describePerson"},
        {"args", {person}},
    });
    std::cout << description << std::endl;

    //#5
    std::cout << "\n--- Invoke getPerson get Person as Json ---" << std::endl;
    json_invoke::json personJson = adapter.invoke({
        {"name", "getPerson"},
    });
    std::cout << personJson.dump(2) << std::endl;

    //#6
    std::cout << "\n--- Invoke describePerson using json object ---" << std::endl;
    json_invoke::json descriptionRequest = {
        {"name", "describePerson"},
        {"args", {personJson}},
    };
    json_invoke::json descriptionJson = adapter.invoke(descriptionRequest);
    std::cout << descriptionJson.dump(2) << std::endl;

    std::cout << "\n--- Tool specs JSON ---" << std::endl;
    std::cout << json_invoke::getAllToolSpecsJson(registry).dump(2) << std::endl;

    std::cout << "\n--- Tool summaries JSON ---" << std::endl;
    std::cout << json_invoke::getAllToolSummariesJson(registry).dump(2) << std::endl;

    std::cout << "\n--- Tool schemas JSON ---" << std::endl;
    std::cout << json_invoke::getAllToolSchemasJson(registry).dump(2) << std::endl;
    
    return 0;
}