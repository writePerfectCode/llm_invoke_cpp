# llm_invoke_cpp

`llm_invoke_cpp` is a header-only C++ library for exposing native C++ functions as LLM-callable tools.

It is split into two public modules:

- `include/func_registry`: dependency-free function registration, metadata export, and runtime dispatch.
- `include/json_invoke`: JSON-based invocation on top of the registry, intended for LLM and agent integrations.

Project layout

- `include/func_registry/func_registry.hpp`: core function registry entry point.
- `include/json_invoke/json_invoke.hpp`: JSON invocation adapter.
- `include/json_invoke/json_introspection.hpp`: standalone JSON tool/spec/schema export helpers for registry metadata.
- `include/json_invoke/json_common.hpp`: shared JSON alias and JsonInvokeError definition used by both invoke and introspection layers.
- `include/json_invoke/json_traits.hpp`: trait hook for custom JSON bindings.
- `examples/func_registry/func_registry_demo.cpp`: core-only registry example.
- `examples/json_invoke/json_invoke_demo.cpp`: JSON invocation example.
- `examples/json_invoke/person.hpp`: example-only domain type used by the JSON invocation demo.
- `examples/json_invoke/person_support.hpp`: example-only JSON bindings and helper functions for `Person`.
- `ROADMAP.md`: planned milestones for evolving the project.
- `MEMO.md`: prioritized capability memo for the LLM-to-C++ goal.

Quick core example

```cpp
#include <func_registry/func_registry.hpp>

func_registry::FuncRegistry registry;
registry.registerFunction("sum", [](int a, int b) { return a + b; }, "Add two integers.");

int value = registry.callByNameWrap("sum", 2, 3);

for (const auto& line : registry.describeAllFunctions()) {
  std::cout << line << std::endl;
}
```

Quick LLM tool example

```cpp
#include <func_registry/func_registry.hpp>
#include <json_invoke/json_invoke.hpp>
#include <json_invoke/json_traits.hpp>

struct Person {
  std::string name;
  int age;
};

template<>
struct json_invoke::json_traits<Person> {
  static Person from_json_value(const nlohmann::json& value) {
    return Person{value.at("name").get<std::string>(), value.at("age").get<int>()};
  }

  static nlohmann::json to_json_value(const Person& value) {
    return {{"name", value.name}, {"age", value.age}};
  }
};

func_registry::FuncRegistry registry;

json_invoke::JsonInvokeAdapter adapter(registry);
adapter.registerFunction("get_person", [] { return Person{"Alice", 30}; }, "Return one person.");
std::cout << adapter.invoke({{"name", "get_person"}, {"args", json_invoke::json::array()}}).dump(2) << std::endl;
```

Integration

- `func_registry` depends only on the C++ standard library.
- `json_invoke` depends on `nlohmann/json` and the core registry module.
- The bundled `CMakeLists.txt` fetches `nlohmann/json` automatically with `FetchContent`.

Minimal CMake integration

```cmake
include(FetchContent)

FetchContent_Declare(
  llm_invoke_cpp
  GIT_REPOSITORY <your llm_invoke_cpp repository url>
  GIT_TAG <commit-or-tag>
)

FetchContent_MakeAvailable(llm_invoke_cpp)

target_link_libraries(your_target PRIVATE llm_invoke_cpp::func_registry)
target_link_libraries(your_target PRIVATE llm_invoke_cpp::json_invoke)
```

API notes

- `func_registry::FuncRegistry`: register functions, query metadata, and invoke by name.
- `func_registry::FunctionMetadata`: attach descriptions and explicit parameter names.
- `registerFunctionAs(...)`: register a callable under an explicit signature.
- `getToolSpec(name)` / `getAllToolSpecs()`: export LLM-friendly tool metadata.
- `renderAllToolSpecs()`: render prompt-ready tool descriptions.
- `json_invoke::JsonInvokeAdapter`: accept JSON tool requests and return a conversion-friendly result wrapper.
- `json_invoke::JsonInvokeAdapter::registerFunction(...)`: register a callable and lazily auto-register JSON-capable argument and return types.
- `json_invoke::getToolSpecJson(registry, name)` / `json_invoke::getAllToolSpecsJson(registry)`: export JSON tool metadata as free functions.
- `json_invoke::getAllToolSummariesJson(registry)`: emit concise tool summaries with only tool name and description for low-context LLM tool selection.
- `json_invoke::getToolSchemaJson(registry, name)` / `json_invoke::getAllToolSchemasJson(registry)`: emit JSON schemas from registered tool metadata without triggering invocation-time conversion checks.
- `json_invoke::JsonInvokeAdapter::invoke(...)`: supports `.dump(2)` for raw response viewing and implicit conversion to strong C++ result types.
- `json_invoke::JsonInvokeAdapter::invokeJson(...)`: execute a JSON request and return the full raw JSON response directly.
- `json_invoke::json_traits<T>`: add custom JSON bindings for domain types.

Supported request shapes

```json
{
  "name": "add",
  "args": [2, 3]
}
```

```json
{
  "type": "function",
  "function": {
    "name": "add",
    "arguments": "{\"arg0\":2,\"arg1\":3}"
  }
}
```

Build and run examples

```powershell
cmake -S . -B build
cmake --build build
.\build\func_registry_demo.exe
.\build\json_invoke_demo.exe
```

The initial configure step downloads `nlohmann/json` into `build/_deps/` through `FetchContent`.
