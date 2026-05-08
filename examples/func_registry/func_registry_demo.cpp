#include <any>
#include <iostream>
#include <string>
#include <func_registry/func_registry.hpp>

static int add(int x, int y)
{
    return x + y;
}

struct Person {
    std::string name;
    int age;
    std::string describe(std::string ending = "") const
    {
        return name + " is " + std::to_string(age) + " years old" + ending;
    }
};

static Person getPerson()
{
    return Person{"Bob", 25};
}

int main()
{
    func_registry::FuncRegistryThreadSafe registry;

    registry.registerFunction("add", add, "Add two integers.");
    registry.registerFunction("getPerson", getPerson, "Return a person payload as Person.");
    registry.registerFunction(
        "describePerson",
        &Person::describe,
        func_registry::FunctionMetadata{{"person", "ending"}, "Call Person::describe() for one person."});

    registry.registerFunction(
        "logMessage",
        [](const std::string& message) {
            std::cout << "[log] " << message << '\n';
        },
        "Log one message and return nothing.");

    registry.registerFunctionAs<std::string, const std::string&, int>(
        "repeatTextAs",
        [](const std::string& text, int count) {
            std::string out;
            for (int i = 0; i < count; ++i)
            {
                out += text;
            }
            return out;
        },
        "Repeat text using an explicit registered signature.");

    //#1
    int sum = registry.callByNameWrap("add", 10, 20);
    std::cout << "Any Sum: " << sum << std::endl;

    //#2
    Person person = registry.callByNameWrap("getPerson");
    std::cout << "Person name: " << person.name << ", age: " << person.age << std::endl;

    //#3
    std::string description = registry.callByNameWrap("describePerson", person, "!!!");
    std::cout << "Person description: " << description << std::endl;

    //#4
    registry.callByNameWrap("logMessage", "Hello from a void lambda.");

    //#5
    auto repeated_text = registry.callByNameWrap("repeatTextAs", "Hi", 3).as<std::string>();
    std::cout << "Repeated text: " << repeated_text << std::endl;

    std::cout << "\n--- Registered prototypes ---" << std::endl;
    for (const auto& summary : func_registry::describeAllFunctions(registry))
    {
        std::cout << summary << std::endl;
    }

    return 0;
}