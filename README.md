# llm_invoke_cpp

`llm_invoke_cpp` is a header-only C++ library for exposing native C++ functions as LLM-callable tools.

It is split into eight public modules:

- `include/func_registry`: dependency-free function registration, concise function summaries, and runtime dispatch.
- `include/tool_meta`: optional tool-facing metadata and tool spec export helpers built on top of the registry.
- `include/type_meta`: optional enum/schema/type-introspection helpers used by tool export and JSON adaptation.
- `include/json_invoke`: JSON-based invocation on top of the registry, intended for LLM and agent integrations.
- `include/json_session_invoke`: higher-level session/runtime APIs built on top of `json_invoke` for stateful create/call/destroy flows.
- `include/task_scheduler`: task classification and scheduling helpers built on top of `json_session_invoke`.
- `include/runtime`: protocol-neutral runtime facade built on top of `task_scheduler` for normalized tool listing and unary invocation.
- `include/mcp`: MCP-specific adapters built on top of `runtime` for protocol-facing transports such as stdio.

Project layout

- `include/func_registry/func_registry.hpp`: core function registry entry point.
- `include/func_registry/function_summary.hpp`: human-readable function summary helpers for registry callables.
- `include/tool_meta/tool_introspection.hpp`: tool-facing metadata and `ToolSpec` export helpers.
- `include/type_meta/type_schema.hpp`: dependency-free structured type schema metadata and custom schema trait hook.
- `include/type_meta/enum_traits.hpp`: string enum mapping hook for human-friendly enum export and conversion.
- `include/type_meta/type_introspection.hpp`: optional type-level LLM/schema metadata helpers used by richer tool export.
- `include/tools/trace_recorder.hpp`: reusable helpers for collecting and serializing structured trace events.
- `include/json_invoke/json_invoke.hpp`: JSON invocation adapter for pure function registration and JSON-based calls.
- `include/json_invoke/json_introspection.hpp`: standalone JSON tool/spec/schema export helpers for registry metadata.
- `include/json_invoke/json_error.hpp`: `JsonInvokeError` definition for request/response conversion and invocation failures.
- `include/json_invoke/json_tool_execution_semantics.hpp`: execution semantics enum and naming helpers for exported tool metadata.
- `include/json_invoke/json_trace.hpp`: tracing event model, request context helpers, and JSON serialization helpers.
- `include/json_invoke/json_traits.hpp`: trait hook for custom JSON bindings.
- `include/json_session_invoke/json_session_invoke.hpp`: session-oriented adapter that composes `json_invoke` and exposes stateful factory APIs as the high-level entry point.
- `include/json_session_invoke/session_objects.hpp`: session object handles, object options, JSON/schema bindings, and in-memory object store support.
- `include/task_scheduler/request_classifier.hpp`: readonly request classification helpers that turn session tool metadata into scheduling categories and object keys.
- `include/task_scheduler/task_scheduler_facade.hpp`: recommended facade entry point for callers that want to submit one JSON request and let task_scheduler hide classification, queueing, and invocation details.
- `include/task_scheduler/task_scheduler.hpp`: scheduler-side task abstractions, including a minimal `ITaskScheduler` interface and a keyed scheduler that enforces session-wide and per-object exclusivity.
- `include/runtime/runtime_facade.hpp`: protocol-neutral facade that projects exported tool schemas into normalized descriptors and turns scheduler-backed JSON requests into normalized invoke results.
- `include/mcp/mcp_stdio_server.hpp`: minimal MCP stdio server that handles JSON-RPC framing plus `initialize`, `ping`, `tools/list`, and `tools/call` on top of the runtime facade.
- `examples/func_registry/func_registry_demo.cpp`: core-only registry example.
- `examples/mcp_stdio/mcp_stdio_server_demo.cpp`: standalone MCP stdio server process that exposes demo tools and can be launched directly by an MCP host.
- `examples/json_invoke/json_invoke_demo.cpp`: JSON invocation example.
- `examples/json_stateful/json_stateful_demo.cpp`: stateful object-handle example for create/call/destroy flows.
- `examples/json_tracing/json_tracing_demo.cpp`: tracing example for invoke and object lifecycle events.
- `examples/trace_recorder/trace_recorder_demo.cpp`: focused `VectorTraceRecorder` example that prints each call's response together with the recorded trace slice for that call.
- `examples/task_scheduler/task_scheduler_demo.cpp`: task scheduler example that prints classification results together with actual worker admission order for free-read, per-object, factory-lane, and barrier-aware tasks.
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

func_registry::FuncRegistryThreadSafe registry;
registry.registerFunction("sum", [](int a, int b) { return a + b; }, "Add two integers.");

int value = registry.callByNameWrap("sum", 2, 3);

for (const auto& line : registry.describeAllFunctions()) {
  std::cout << line << std::endl;
}
```

Quick LLM tool example

```cpp
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

json_invoke::JsonInvokeAdapterThreadSafe adapter;
adapter.registerFunction("get_person", json_invoke::readOnly([] { return Person{"Alice", 30}; }), "Return one person.");
std::cout << adapter.invoke({{"name", "get_person"}, {"args", json_invoke::json::array()}}).dump(2) << std::endl;
```

Tracing demo

- Run `json_tracing_demo` to inspect `TraceSink` output for stateless success/failure, stateful create/destroy, and idle expiration.
- Each emitted `TraceEvent` now carries its own `request_id`, `timestamp`, and `duration_ms`, so the demo prints the original event metadata rather than the sink's current wall clock at print time.
- Run `trace_recorder_demo` when you want a quieter example that records events with `json_invoke::VectorTraceRecorder` and prints each call together with that call's recorded trace JSON.

Integration

- `func_registry` depends only on the C++ standard library.
- `json_invoke` depends on `nlohmann/json` and the core registry module.
- `json_session_invoke` depends on `json_invoke` and re-exports a higher-level session-oriented API.
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
target_link_libraries(your_target PRIVATE llm_invoke_cpp::json_session_invoke)
target_link_libraries(your_target PRIVATE llm_invoke_cpp::task_scheduler)
target_link_libraries(your_target PRIVATE llm_invoke_cpp::runtime)
target_link_libraries(your_target PRIVATE llm_invoke_cpp::mcp)
```

API notes

- `func_registry::FuncRegistryThreadSafe`: register functions and invoke them by name when the registry may be shared across threads.
- `func_registry::FuncRegistryUnsafe`: explicit single-threaded variant for thread-confined ownership.
- `func_registry::FunctionMetadata`: attach descriptions and explicit parameter names.
- `registerFunctionAs(...)`: register a callable under an explicit signature.
- `describeFunction(name)` / `describeAllFunctions()`: render concise human-readable C++ function summaries.
- Include `tool_meta/tool_introspection.hpp` for `getToolSpec(name)` / `getAllToolSpecs()`.
- `json_invoke::JsonInvokeAdapterThreadSafe`: accept JSON tool requests and return a conversion-friendly result wrapper for shared multi-threaded access.
- `json_invoke::JsonInvokeAdapterUnsafe`: explicit single-threaded variant for thread-confined ownership when every registration and invocation stays on one thread.
- `json_invoke::JsonInvokeAdapterThreadSafe()` owns an internal function registry by default; advanced callers can still inject an existing registry instance.
- `json_invoke::JsonInvokeAdapterThreadSafe::registerFunction(...)`: register a callable and eagerly auto-register default JSON-capable argument and return types; later `registerType(...)` calls can override those defaults.
- Wrap stateless tools with `json_invoke::readOnly(...)` or `json_invoke::mutating(...)` when you want exported metadata to include `x-execution-semantics`.
- `json_invoke::TraceEvent` / `json_invoke::TraceSink`: opt-in tracing hooks for invoke and session lifecycle events; stable top-level fields include `event`, `timestamp`, `request_id`, `tool_name`, `duration_ms`, and event-specific `payload`.
- `json_invoke::traceEventToJson(...)`: serialize one `TraceEvent` into the stable JSON shape used by the tracing demo and recorder helpers.
- `json_invoke::VectorTraceRecorder`: lightweight collector from `include/tools/trace_recorder.hpp` that exposes a ready-to-use `TraceSink` and exports recorded events through `toJson()`.
- `json_invoke::JsonInvokeAdapterThreadSafe::getAllToolSummariesJson()` / `getToolSchemaJson(...)` / `getAllToolSchemasJson()`: export lighter summaries or full JSON schemas directly from the adapter.
- `json_invoke::getAllToolSummariesJson(registry)`: emit concise tool summaries with only tool name and description for low-context LLM tool selection.
- `json_invoke::getToolSchemaJson(registry, name)` / `json_invoke::getAllToolSchemasJson(registry)`: emit JSON schemas from registered tool metadata without triggering invocation-time conversion checks.
- `json_invoke::JsonInvokeAdapterThreadSafe::invoke(...)`: supports `.dump(2)` for raw response viewing and implicit conversion to strong C++ result types.
- `json_invoke::JsonInvokeAdapterThreadSafe::invokeJson(...)`: execute a JSON request and return the full raw JSON response directly.
- `json_session_invoke::JsonSessionInvokeAdapterThreadSafe`: higher-level session adapter that composes `json_invoke` and is the recommended thread-safe entry point for stateful object lifecycles.
- `json_session_invoke::JsonSessionInvokeAdapterUnsafe`: explicit single-threaded variant for thread-confined schedulers or actor-style ownership. Constructing it prints a one-time warning so accidental shared use is easier to catch.
- `json_session_invoke::SessionObjectHandle` / `json_session_invoke::SessionObjectOptions`: clearer public aliases for the session-layer handle and options types; `ObjectHandle` / `ObjectOptions` remain supported for compatibility.
- `json_session_invoke::JsonSessionInvokeAdapterThreadSafe::registerFunction(...)`: also supports plain stateless function registration directly, so one session adapter can host both stateless tools and stateful object lifecycles.
- `json_session_invoke::JsonSessionInvokeAdapterThreadSafe::registerFunction(...)` intentionally rejects member function pointers; stateful member methods must be registered through `stateful<T>(...).method(...)` so the session boundary stays explicit.
- `json_session_invoke::JsonSessionInvokeAdapterThreadSafe::stateful<T>(...)`: fluent builder for grouped stateful registration such as `.create(...).method(...).destroy()` while reusing the same underlying session runtime.
- `json_session_invoke::JsonSessionInvokeAdapterThreadSafe::findToolMetadata(...)`: readonly scheduler-facing metadata query that reports execution semantics, the minimal `ToolSchedulingScope` hint, and stateful fields such as `stateful_kind`, `object_type_name`, and `handle_parameter_name` for scheduler-side classification.
- `json_session_invoke::JsonSessionInvokeAdapterThreadSafe::ToolSchedulingScope`: optional registration-time scope hint for exceptional tools such as global `session_barrier`; finer fallback policies stay inside `task_scheduler` classification logic.
- `task_scheduler::RequestClassifierThreadSafe`: classifies a JSON request into `FreeReadOnly`, `ObjectExclusive`, `FactoryLane`, `ToolExclusive`, or `SessionBarrier`; default inference uses tool metadata plus handle/object_type extraction, while explicit scheduling hints override the defaults.
- `task_scheduler::TaskSchedulerFacadeThreadSafe`: recommended one-object facade for higher-level callers; submit one JSON request with `submitRequest(...)`, while classification, keyed queueing, and invocation stay hidden behind the facade.
- `task_scheduler::ITaskScheduler`: scheduler-side execution interface that accepts a classified `ScheduledTask` and returns a `std::future<json>` without pulling execution policy back into `json_session_invoke`.
- `task_scheduler::KeyedTaskScheduler`: fixed-worker scheduler implementation that admits the next runnable task from an internal queue, allows `FreeReadOnly` work to run concurrently, serializes `ObjectExclusive` by `object_id`, serializes `FactoryLane` by `object_type`, serializes `ToolExclusive` by `tool_name`, and treats `SessionBarrier` as a stop-the-world session-wide exclusive operation.
- `runtime::RuntimeFacadeThreadSafe`: protocol-neutral entry point above `task_scheduler`; `listTools()` returns normalized tool descriptors, `submitInvoke(...)` schedules one unary call and returns `std::future<runtime::InvokeResult>`, and `invoke(...)` gives the same normalized result synchronously.
- `runtime::InvokeResult`: normalized invoke envelope with `ok`, `value`, optional `error`, and the original `raw_response`; classification-time failures such as unknown tools are converted into the same error shape instead of leaking exceptions to protocol adapters.
- `mcp::McpStdioServerThreadSafe`: thin MCP adapter over `runtime::RuntimeFacadeThreadSafe`; it reads and writes `Content-Length` framed JSON-RPC messages and serves `initialize`, `ping`, `tools/list`, and `tools/call`.
- `mcp::McpStdioServerThreadSafe::handleMessage(...)`: useful when you want protocol handling without a real stdio loop, for example in tests or when embedding the MCP adapter into another transport shim.

MCP stdio demo

- Build `mcp_stdio_server_demo` when you want a real process that an MCP host can launch over stdio.
- The demo server registers `sum`, `echo_text`, `create_counter`, `counter_add`, `counter_value`, and `destroy_counter`.
- The process writes only framed MCP responses to stdout; fatal startup errors go to stderr.
- `examples/mcp_stdio/test_mcp.ps1` sends a minimal initialize/list/call sequence to the demo executable so you can smoke-test the server without writing MCP frames by hand.
- The fluent builder also supports `.options(...)`, so object type selection and session object options can be expressed separately: `.stateful<T>("counter").options(opts)...`.
- When `.stateful<T>("counter")` uses `.create(...)` without an explicit tool name, the builder defaults to `create_counter`.
- If a stateful builder creates an object but omits `.destroy()`, the adapter auto-registers the default `destroy_<object_type>` tool unless `setStatefulDefaults(...)` disables `auto_register_destroy`.
- `json_session_invoke::JsonSessionInvokeAdapterThreadSafe::setStatefulDefaults(...)`: adapter-level defaults for auto-generated stateful helpers, including `auto_register_destroy` and the default destroy description text.
- `json_session_invoke::JsonSessionInvokeAdapterThreadSafe::registerDestroy<T>()`: when called without a name, defaults to `destroy_<object_type>` if the session object type was explicitly named during factory registration, otherwise falls back to `destroy_object`.
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

Stateful flows use the same request envelope through `json_session_invoke`. A create tool can return a handle like `{ "object_id": "obj_1", "object_type": "counter" }`, and later tools can accept that handle as a regular argument.

You can also mix stateless and stateful tools on the same `JsonSessionInvokeAdapterThreadSafe` instance:

```cpp
json_session_invoke::JsonSessionInvokeAdapterThreadSafe adapter;

adapter.registerFunction(
  "sum",
  json_invoke::readOnly([](int left, int right) { return left + right; }),
  func_registry::FunctionMetadata{{"left", "right"}, "Add two integers."});

adapter
  .stateful<Counter>("counter")
  .create([](int initial) { return std::make_shared<Counter>(initial); })
  .method("counter_add", &Counter::add)
  .method("counter_value", &Counter::current);
```

Stateful tools infer execution semantics automatically: factories and destroy tools export `mutating`, non-const methods export `mutating`, and const methods export `read_only`.

`JsonSessionInvokeAdapterUnsafe` is only appropriate when one thread exclusively owns the adapter for its full lifetime and every registration/invocation path is funneled through that same thread. If an adapter instance may be shared across threads, use `JsonSessionInvokeAdapterThreadSafe`.

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
ctest --test-dir build --output-on-failure
.\build\func_registry_demo.exe
.\build\mcp_stdio_server_demo.exe
.\examples\mcp_stdio\test_mcp.ps1
.\build\json_invoke_demo.exe
.\build\json_stateful_demo.exe
.\build\json_tracing_demo.exe
.\build\trace_recorder_demo.exe
.\build\task_scheduler_demo.exe
```

The initial configure step downloads `nlohmann/json` into `build/_deps/` through `FetchContent`.

GitHub Actions runs the same configure, build, and test flow on Windows for pushes and pull requests.
