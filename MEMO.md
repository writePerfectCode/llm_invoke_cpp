# Memo

## Goal

The project goal is not only function lookup. It is to let LLM systems call C++ capabilities safely and predictably.

## The 5 Highest-Priority Capabilities

### 1. Tool Schema Compatibility

The library should export schemas that match real tool-calling APIs.
That means stable support for the shapes expected by OpenAI, Anthropic, Gemini, and later MCP.
Without this, the project remains a local helper instead of an integration layer.

### 2. Stronger Type Mapping

The current path works for basic types and custom JSON traits, but practical tool use needs more.
Priority additions are:
- `std::optional`
- enums
- nested structs
- vectors of custom objects
- clearer nullability handling

If type mapping is weak, every serious user ends up writing one-off glue code.

### 3. Safe Execution Controls

An LLM-facing runtime needs guardrails.
Priority items are:
- explicit tool visibility control
- per-tool allowlists
- execution timeout hooks
- structured failure reporting

This is the difference between a demo and something safe enough to embed in a real app.

### 4. Transport And Hosting Adapters

The current JSON adapter is a good start, but the project will be much more useful once it can be hosted in different ways.
Priority order:
- JSON request loop for local embedding
- MCP server adapter
- optional HTTP wrapper

The core registry should stay transport-agnostic.

### 5. Observability And Test Coverage

LLM integration is hard to debug if the runtime does not explain what happened.
Priority items are:
- clear argument validation errors
- call tracing hooks
- schema snapshot tests
- end-to-end example tests

This will make future iterations much easier and reduce hidden regressions.

## Recommended Implementation Order

1. Normalize public API names and finish the rename.
2. Improve schema export for mainstream tool-calling formats.
3. Expand type support starting with `std::optional` and enums.
4. Add execution policies and structured errors.
5. Add one real external integration, preferably MCP or OpenAI-style tool calling.
