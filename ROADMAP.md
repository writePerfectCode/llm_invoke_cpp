# Roadmap

## Direction

`llm_invoke_cpp` is positioned as a C++-native tool runtime for LLM and agent systems.
The long-term goal is to let an LLM discover, validate, and invoke C++ functions with low integration cost and predictable behavior.

## Phase 1: Stabilize The Core

- Finalize the public naming around `func_registry` and `json_invoke`.
- Keep the core registry dependency-free.
- Clean up examples so they demonstrate the intended public API only.
- Verify tool/schema export and JSON invocation behavior on Windows builds.

## Phase 2: Improve LLM Readiness

- Expand tool schema export for OpenAI-style and Anthropic-style tool definitions.
- Improve parameter naming and descriptions so tool prompts are useful without hand-written wrappers.
- Add better errors for missing fields, bad argument types, and unsupported return types.
- Introduce examples that show a real LLM request loop instead of only local JSON payloads.

## Phase 3: Strengthen Type Support

- Support more standard C++ types such as `std::optional`, enums, and nested structs.
- Add a clearer registration path for custom domain objects.
- Improve tuple and container handling so schemas are less ambiguous.
- Define a consistent strategy for nullable values and default arguments.

## Phase 4: Add Runtime Safety

- Add per-tool policies for allowlists, deny-lists, and visibility.
- Add timeout and cancellation hooks around tool execution.
- Add structured error categories so callers can distinguish validation failures from execution failures.
- Prepare for sandboxing or process isolation where direct execution is too risky.

## Phase 5: Multi-Protocol Integration

- Add transport adapters beyond raw JSON calls.
- Prototype an MCP server adapter.
- Evaluate a simple HTTP interface for local orchestration.
- Keep the core registry independent from transport concerns.

## Phase 6: Production Readiness

- Add tests for schema generation, JSON conversion, and runtime dispatch.
- Add CI across at least one Windows and one Linux toolchain.
- Add packaging and install guidance for embedding in larger C++ projects.
- Add benchmark coverage for registration overhead and call overhead.
