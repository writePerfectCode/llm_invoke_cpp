#pragma once

#include <map>
#include <string>
#include <unordered_map>
#include <vector>
#include "person.hpp"
#include <type_meta/type_schema.hpp>
#include <json_invoke/json_traits.hpp>

template<>
struct json_invoke::json_traits<Person>
{
    static Person from_json_value(const nlohmann::json& value)
    {
        return Person{value.at("name").get<std::string>(), value.at("age").get<int>()};
    }

    static nlohmann::json to_json_value(const Person& person)
    {
        return nlohmann::json{
            {"name", person.getName()},
            {"age", person.getAge()},
        };
    }
};

namespace func_registry {

template<>
struct schema_traits<Person>
{
    static TypeSchema schema()
    {
        return described(
            examples(
                objectSchema({
                    property(
                        "name",
                        examples(
                            defaulted(
                                described(stringSchema(), "Display name shown to users."),
                                "\"Alice\""),
                            {"\"Alice\"", "\"Bob\"", "\"Cara\""})),
                    property(
                        "age",
                        examples(
                            defaulted(
                                described(integerSchema(), "Age in full years."),
                                "30"),
                            {"17", "30", "41"})),
                }),
                {R"({"name":"Alice","age":30})", R"({"name":"Bob","age":17})"}),
            "Person payload used by the JSON invocation demo.");
    }
};

} // namespace func_registry

inline Person getPerson()
{
    return Person{"Alice", 30};
}

inline int countPeopleAtLeastAge(const std::vector<Person>& people, int minimum_age)
{
    int count = 0;
    for (const auto& person : people)
    {
        if (person.getAge() >= minimum_age)
        {
            ++count;
        }
    }

    return count;
}

inline std::map<std::string, int> countPeopleAtLeastAgeByTeam(
    const std::map<std::string, std::vector<Person>>& team_rosters,
    int minimum_age)
{
    std::map<std::string, int> counts;

    for (const auto& [team_name, people] : team_rosters)
    {
        counts[team_name] = countPeopleAtLeastAge(people, minimum_age);
    }

    return counts;
}

inline std::unordered_map<std::string, Person> oldestPersonByQueue(
    const std::unordered_map<std::string, std::vector<Person>>& queue_rosters)
{
    std::unordered_map<std::string, Person> oldest_by_queue;

    for (const auto& [queue_name, people] : queue_rosters)
    {
        if (people.empty())
        {
            continue;
        }

        const Person* oldest = &people.front();
        for (const auto& person : people)
        {
            if (person.getAge() > oldest->getAge())
            {
                oldest = &person;
            }
        }

        oldest_by_queue.emplace(queue_name, *oldest);
    }

    return oldest_by_queue;
}