#pragma once

#include <string>
#include "person.hpp"
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

inline Person getPerson()
{
    return Person{"Alice", 30};
}