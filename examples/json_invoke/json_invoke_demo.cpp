#include <iostream>
#include <optional>
#include <string>
#include "person_support.hpp"
#include "priority_support.hpp"
#include <json_invoke/json_invoke.hpp>

int main()
{
    json_invoke::JsonInvokeAdapter adapter;

    adapter.registerFunction("add", json_invoke::readOnly([](int x, int y) { return x + y; }), "Add two integers.");
    adapter.registerFunction("getPerson", json_invoke::readOnly(getPerson), "Construct one person.");
    adapter.registerFunction(
        "describePerson",
        json_invoke::readOnly(&Person::describe),
        json_invoke::FunctionMetadata{{"person"}, "Call Person::describe() for one person."});
    adapter.registerFunction(
        "countPeopleAtLeastAge",
        json_invoke::readOnly(countPeopleAtLeastAge),
        json_invoke::FunctionMetadata{{"people", "minimum_age"}, "Count how many people in a JSON roster meet a minimum age."});
    adapter.registerFunction(
        "countPeopleAtLeastAgeByTeam",
        json_invoke::readOnly(countPeopleAtLeastAgeByTeam),
        json_invoke::FunctionMetadata{
            {"team_rosters", "minimum_age"},
            "Count how many people in each team roster meet a minimum age."});
    adapter.registerFunction(
        "oldestPersonByQueue",
        json_invoke::readOnly(oldestPersonByQueue),
        json_invoke::FunctionMetadata{
            {"queue_rosters"},
            "Find the oldest person in each support queue."});
    adapter.registerFunction(
        "displayNickname",
        json_invoke::readOnly([](std::optional<std::string> nickname) {
            return nickname.value_or("anonymous");
        }),
        json_invoke::FunctionMetadata{{"nickname"}, "Return the nickname or a default when omitted."});
    adapter.registerFunction(
        "recommendIncidentPriority",
        json_invoke::readOnly(recommendIncidentPriority),
        json_invoke::FunctionMetadata{
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

    std::cout << "\n--- Count people from array of JSON objects ---" << std::endl;
    json_invoke::json rosterRequest = {
        {"name", "countPeopleAtLeastAge"},
        {"args", {
            {"people", json_invoke::json::array({
                personJson,
                json_invoke::json{{"name", "Bob"}, {"age", 17}},
                json_invoke::json{{"name", "Cara"}, {"age", 41}},
            })},
            {"minimum_age", 21},
        }},
    };
    int adultCount = adapter.invoke(rosterRequest);
    std::cout << adultCount << std::endl;

    std::cout << "\n--- Count people by team from dictionary of rosters ---" << std::endl;
    json_invoke::json teamRosterRequest = {
        {"name", "countPeopleAtLeastAgeByTeam"},
        {"args", {
            {"team_rosters", {
                {"support", json_invoke::json::array({
                    personJson,
                    json_invoke::json{{"name", "Bob"}, {"age", 17}},
                })},
                {"platform", json_invoke::json::array({
                    json_invoke::json{{"name", "Cara"}, {"age", 41}},
                    json_invoke::json{{"name", "Dylan"}, {"age", 29}},
                    json_invoke::json{{"name", "Eva"}, {"age", 18}},
                })},
            }},
            {"minimum_age", 21},
        }},
    };
    json_invoke::json teamCounts = adapter.invoke(teamRosterRequest);
    std::cout << teamCounts.dump(2) << std::endl;

    std::cout << "\n--- Find oldest person by queue from unordered dictionary ---" << std::endl;
    json_invoke::json queueRequest = {
        {"name", "oldestPersonByQueue"},
        {"args", {
            {"queue_rosters", {
                {"billing", json_invoke::json::array({
                    json_invoke::json{{"name", "Mia"}, {"age", 26}},
                    json_invoke::json{{"name", "Noah"}, {"age", 31}},
                })},
                {"vip", json_invoke::json::array({
                    personJson,
                    json_invoke::json{{"name", "Olivia"}, {"age", 45}},
                })},
            }},
        }},
    };
    json_invoke::json oldestByQueue = adapter.invoke(queueRequest);
    std::cout << oldestByQueue.dump(2) << std::endl;

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

    //std::cout << "\n--- Tool summaries JSON ---" << std::endl;
    //std::cout << adapter.getAllToolSummariesJson().dump(2) << std::endl;

    std::cout << "\n--- Tool schemas JSON ---" << std::endl;
    std::cout << adapter.getAllToolSchemasJson().dump(2) << std::endl;
    
    return 0;
}