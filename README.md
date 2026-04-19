# llm_invoke_cpp

`llm_invoke_cpp` is a header-only C++ library for exposing native C++ functions as LLM-callable tools.

It is split into four public modules:

- `include/func_registry`: dependency-free function registration, concise function summaries, and runtime dispatch.
- `include/tool_meta`: optional tool-facing metadata and tool spec export helpers built on top of the registry.
- `include/type_meta`: optional enum/schema/type-introspection helpers used by tool export and JSON adaptation.
- `include/json_invoke`: JSON-based invocation on top of the registry, intended for LLM and agent integrations.

Project layout

- `include/func_registry/func_registry.hpp`: core function registry entry point.
- `include/func_registry/function_summary.hpp`: human-readable function summary helpers for registry callables.
- `include/tool_meta/tool_introspection.hpp`: tool-facing metadata and `ToolSpec` export helpers.
- `include/type_meta/type_schema.hpp`: dependency-free structured type schema metadata and custom schema trait hook.
- `include/type_meta/enum_traits.hpp`: string enum mapping hook for human-friendly enum export and conversion.
- `include/type_meta/type_introspection.hpp`: optional type-level LLM/schema metadata helpers used by richer tool export.
- `include/json_invoke/json_invoke.hpp`: JSON invocation adapter.
- `include/json_invoke/json_introspection.hpp`: standalone JSON tool/spec/schema export helpers for registry metadata.
- `include/json_invoke/json_common.hpp`: shared JSON alias and JsonInvokeError definition used by both invoke and introspection layers.
- `include/json_invoke/json_traits.hpp`: trait hook for custom JSON bindings.
- `examples/func_registry/func_registry_demo.cpp`: core-only registry example.
- `examples/json_invoke/json_invoke_demo.cpp`: JSON invocation example.
- `examples/json_invoke/person.hpp`: example-only domain type used by the JSON invocation demo.
- `examples/json_invoke/person_support.hpp`: example-only JSON bindings and helper functions for `Person`.
- `examples/json_invoke/priority_support.hpp`: example-only enum mapping and incident-priority helper logic used by the JSON invocation demo.
- `SCHEMA_TRAITS.md`: focused guide for writing `func_registry::schema_traits<T>` specializations, including LLM-oriented generation rules.
- `SCHEMA_TRAITS_PROMPT.md`: reusable prompt templates for asking an LLM to generate or review `schema_traits<T>` code.
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

- `func_registry::FuncRegistry`: register functions and invoke them by name.
- `func_registry::FunctionMetadata`: attach descriptions and explicit parameter names.
- `registerFunctionAs(...)`: register a callable under an explicit signature.
- `describeFunction(name)` / `describeAllFunctions()`: render concise human-readable C++ function summaries.
- Include `tool_meta/tool_introspection.hpp` for `getToolSpec(name)` / `getAllToolSpecs()` and `renderAllToolSpecs()`.
- Use `registerToolFunction(...)` / `registerToolFunctionAs(...)` when a plain registry should also populate tool/type metadata for `tool_meta` export.
- `json_invoke::JsonInvokeAdapter`: accept JSON tool requests and return a conversion-friendly result wrapper.
- `json_invoke::JsonInvokeAdapter::registerFunction(...)`: register a callable and eagerly auto-register default JSON-capable argument and return types; later `registerType(...)` calls can override those defaults.
- `json_invoke::getToolSpecJson(registry, name)` / `json_invoke::getAllToolSpecsJson(registry)`: export JSON tool metadata as free functions.
- `json_invoke::getAllToolSummariesJson(registry)`: emit concise tool summaries with only tool name and description for low-context LLM tool selection.
- `json_invoke::getToolSchemaJson(registry, name)` / `json_invoke::getAllToolSchemasJson(registry)`: emit JSON schemas from registered tool metadata without triggering invocation-time conversion checks.
- `json_invoke::JsonInvokeAdapter::invoke(...)`: supports `.dump(2)` for raw response viewing and implicit conversion to strong C++ result types.
- `json_invoke::JsonInvokeAdapter::invokeJson(...)`: execute a JSON request and return the full raw JSON response directly.
- `json_invoke::json_traits<T>`: add custom JSON bindings for domain types.
- `func_registry::schema_traits<T>`: optionally describe nested object fields so exported tool schemas can include custom object properties and container item shapes.
- See `SCHEMA_TRAITS.md` for dedicated authoring guidance and LLM-oriented generation rules for `schema_traits<T>`.
- See `SCHEMA_TRAITS_PROMPT.md` for reusable prompt templates when you want an LLM to generate or review `schema_traits<T>` code.
- `func_registry::TypeSchema` can carry field descriptions, example values, and default values; `json_invoke` emits them into exported JSON Schema.
- `std::optional<T>` parameters are exported as non-required nullable schema properties and can be omitted from named JSON arguments.
- enum parameters and return values are supported out of the box through their underlying integer representation.
- specialize `func_registry::enum_traits<T>` to expose string-based enum mappings and emit schema enum values for LLM-friendly calls.
- `std::map<std::string, T>` and `std::unordered_map<std::string, T>` now export as object schemas with `additionalProperties`, so nested dictionary inputs and outputs can carry structured item schemas.

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
