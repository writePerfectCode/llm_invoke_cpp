#include <iostream>
#include <optional>
#include <string>
#include "person_support.hpp"
#include "priority_support.hpp"
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
    adapter.registerFunction(
        "displayNickname",
        [](std::optional<std::string> nickname) {
            return nickname.value_or("anonymous");
        },
        func_registry::FunctionMetadata{{"nickname"}, "Return the nickname or a default when omitted."});
    adapter.registerFunction(
        "recommendIncidentPriority",
        recommendIncidentPriority,
        func_registry::FunctionMetadata{
            {"requested_priority", "customer_blocked", "production_impact", "affected_users"},
            "Recommend an incident priority from request urgency and customer impact."});

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

    std::cout << "\n--- Invoke displayNickname without optional arg ---" << std::endl;
    std::string nickname = adapter.invoke({
        {"name", "displayNickname"},
        {"args", json_invoke::json::object()},
    });
    std::cout << nickname << std::endl;

    std::cout << "\n--- Invoke displayNickname with optional arg ---" << std::endl;
    std::string nicknameWithArg = adapter.invoke({
        {"name", "displayNickname"},
        {"args", {"tom"}},
    });
    std::cout << nicknameWithArg << std::endl;

    std::cout << "\n--- Recommend incident priority from named JSON args ---" << std::endl;
    json_invoke::json recommendedPriorityJson = adapter.invoke({
        {"name", "recommendIncidentPriority"},
        {"args", {
            {"requested_priority", "low"},
            {"customer_blocked", true},
            {"production_impact", false},
            {"affected_users", 120},
        }},
    });
    std::cout << recommendedPriorityJson.dump(2) << std::endl;

    std::cout << "\n--- Recommend incident priority as typed enum ---" << std::endl;
    Priority recommendedPriority = adapter.invoke({
        {"name", "recommendIncidentPriority"},
        {"args", {
            {"requested_priority", "normal"},
            {"customer_blocked", true},
            {"production_impact", true},
            {"affected_users", 320},
        }},
    });
    std::cout << func_registry::enum_name(recommendedPriority) << std::endl;

    std::cout << "\n--- Tool specs JSON ---" << std::endl;
    std::cout << json_invoke::getAllToolSpecsJson(registry).dump(2) << std::endl;

    std::cout << "\n--- Tool summaries JSON ---" << std::endl;
    std::cout << json_invoke::getAllToolSummariesJson(registry).dump(2) << std::endl;

    std::cout << "\n--- Tool schemas JSON ---" << std::endl;
    std::cout << json_invoke::getAllToolSchemasJson(registry).dump(2) << std::endl;
    
    return 0;
}