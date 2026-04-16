#pragma once

#include <string>
#include <utility>

class Person {
public:
    Person(std::string name_value, int age_value)
        : name(std::move(name_value)), age(age_value)
    {
    }

    std::string describe() const
    {
        return name + " is " + std::to_string(age) + " years old.";
    }

    const std::string& getName() const noexcept
    {
        return name;
    }

    int getAge() const noexcept
    {
        return age;
    }

private:
    std::string name;
    int age;
};