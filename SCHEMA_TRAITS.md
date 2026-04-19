# schema_traits Guide

## Purpose

This document explains how to write `func_registry::schema_traits<T>` specializations for custom C++ types.

It is written for both humans and LLM-based code generation.
If an LLM needs to generate a new `schema_traits<T>` specialization, it should follow the rules and templates in this file.

## What `schema_traits<T>` Is For

`schema_traits<T>` describes the JSON schema shape of a C++ type.

It affects schema export only.
It does not perform JSON conversion by itself.

Use `json_invoke::json_traits<T>` when a type also needs JSON input and output conversion.
Use `func_registry::schema_traits<T>` when a type needs a richer exported schema.

In many practical cases, custom domain types need both:

1. `json_invoke::json_traits<T>` for runtime conversion.
2. `func_registry::schema_traits<T>` for exported JSON schema.

## When To Write A Specialization

Write `schema_traits<T>` when:

1. `T` is a custom struct or class exposed in tool parameters or return values.
2. The default exported schema of `object` is too vague.
3. You want nested properties, descriptions, defaults, or examples to appear in exported tool schemas.

Do not write `schema_traits<T>` for simple scalar types such as:

1. `int`
2. `double`
3. `bool`
4. `std::string`

Those already map automatically.

## Minimal Template

Use this shape for the smallest useful specialization:

```cpp
struct Point {
    int x;
    int y;
};

template<>
struct func_registry::schema_traits<Point>
{
    static func_registry::TypeSchema schema()
    {
        using namespace func_registry;

        return objectSchema({
            property("x", integerSchema()),
            property("y", integerSchema()),
        });
    }
};
```

This means:

1. The type is exported as a JSON object.
2. It has properties `x` and `y`.
3. Both properties are required by default.

## Common Helper Functions

Use these helpers from `include/type_meta/type_schema.hpp`.

### Scalar Helpers

- `stringSchema()`
- `integerSchema()`
- `numberSchema()`
- `booleanSchema()`

Each helper also accepts `true` to mark the schema as nullable, for example `stringSchema(true)`.

### Structural Helpers

- `objectSchema({...})`
- `arrayOf(item_schema)`
- `dictionaryOf(value_schema)`
- `property(name, schema, required)`

Notes:

1. `property(name, schema)` is required by default.
2. `property(name, schema, false)` makes the property optional.
3. `dictionaryOf(value_schema)` is for string-key JSON objects such as `std::map<std::string, T>`.

### Metadata Helpers

- `described(schema, text)`
- `defaulted(schema, json_literal_text)`
- `examples(schema, {json_literal_texts...})`

Examples:

```cpp
described(stringSchema(), "User display name")
defaulted(integerSchema(), "30")
examples(stringSchema(), {"\"Alice\"", "\"Bob\""})
```

Important:

1. `defaulted` and `examples` accept JSON literals as strings.
2. Use `"30"` for a JSON number.
3. Use `"\"Alice\""` for a JSON string value.
4. Use raw string literals like `R"({"name":"Alice","age":30})"` for object examples.

## Recommended Writing Style

For readability, prefer this pattern:

1. Put `using namespace func_registry;` inside `schema()`.
2. Start with the smallest correct structure.
3. Add descriptions next.
4. Add defaults and examples only when they add real value.

Prefer concise field definitions like this:

```cpp
template<>
struct func_registry::schema_traits<Person>
{
    static func_registry::TypeSchema schema()
    {
        using namespace func_registry;

        return objectSchema({
            property("name", described(stringSchema(), "Display name shown to users.")),
            property("age", described(integerSchema(), "Age in full years.")),
        });
    }
};
```

If richer metadata is useful, extend it like this:

```cpp
template<>
struct func_registry::schema_traits<Person>
{
    static func_registry::TypeSchema schema()
    {
        using namespace func_registry;

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
```

## Nested Object Example

If a type contains another custom type, reuse the nested schema through `schema_traits<Nested>::schema()`.

```cpp
struct Address {
    std::string city;
    std::string country;
};

struct UserProfile {
    std::string name;
    Address address;
};

template<>
struct func_registry::schema_traits<Address>
{
    static func_registry::TypeSchema schema()
    {
        using namespace func_registry;

        return objectSchema({
            property("city", stringSchema()),
            property("country", stringSchema()),
        });
    }
};

template<>
struct func_registry::schema_traits<UserProfile>
{
    static func_registry::TypeSchema schema()
    {
        using namespace func_registry;

        return objectSchema({
            property("name", stringSchema()),
            property("address", schema_traits<Address>::schema()),
        });
    }
};
```

## Container Examples

### Array Of Custom Objects

```cpp
template<>
struct func_registry::schema_traits<Team>
{
    static func_registry::TypeSchema schema()
    {
        using namespace func_registry;

        return objectSchema({
            property("members", arrayOf(schema_traits<Person>::schema())),
        });
    }
};
```

### Dictionary Of Custom Objects

```cpp
template<>
struct func_registry::schema_traits<Directory>
{
    static func_registry::TypeSchema schema()
    {
        using namespace func_registry;

        return objectSchema({
            property("people_by_id", dictionaryOf(schema_traits<Person>::schema())),
        });
    }
};
```

## Common Type Mapping Table

Use this table when deciding which helper to apply.

| C++ shape | Typical schema helper | Notes |
| --- | --- | --- |
| `std::string` | `stringSchema()` | Use for names, ids, labels, free text. |
| `bool` | `booleanSchema()` | Use for flags and toggles. |
| `int`, `long`, integer enums without enum traits | `integerSchema()` | Integer enums become string schemas only when `enum_traits<T>` provides string mappings. |
| `float`, `double` | `numberSchema()` | Use for non-integer numeric values. |
| `std::optional<T>` field | `property(name, <schema for T>, false)` and usually nullable schema | Optional means not required; nullable means `null` is allowed. Set both intentionally. |
| `std::vector<T>` | `arrayOf(<schema for T>)` | Use for ordered lists. |
| `std::array<T, N>` | `arrayOf(<schema for T>)` | Fixed length is not currently modeled separately. |
| `std::map<std::string, T>` | `dictionaryOf(<schema for T>)` | Exports as JSON object with `additionalProperties`. |
| `std::unordered_map<std::string, T>` | `dictionaryOf(<schema for T>)` | Same export shape as string-key `std::map`. |
| Nested custom object `Address` | `schema_traits<Address>::schema()` | Reuse the nested specialization instead of rebuilding inline. |
| String enum with `enum_traits<T>` | usually reuse nested type schema or rely on automatic enum export | Do not duplicate enum values manually unless necessary. |

Practical guidance:

1. Use `property("field", stringSchema())` for the simplest scalar field.
2. Use `property("items", arrayOf(schema_traits<Item>::schema()))` for object arrays.
3. Use `property("by_id", dictionaryOf(schema_traits<Item>::schema()))` for string-key dictionaries.
4. Use `property("nickname", stringSchema(true), false)` only when the field is both optional and nullable.

## Optional Fields

If a field is not required in JSON, mark it as optional with the third argument to `property`.

```cpp
property("nickname", stringSchema(true), false)
```

This means:

1. The field is nullable.
2. The field is not required.

Use both settings intentionally.
Do not mark a field nullable unless `null` is a valid JSON input.

## Relationship To Enum Support

String-based enums are usually handled through `func_registry::enum_traits<T>`.

If a field type already resolves to a string enum schema automatically, do not manually duplicate the enum values inside `schema_traits<T>` unless you have a strong reason.

Prefer reusing the nested type schema when possible.

## Rules For LLM Code Generation

If an LLM is asked to generate `schema_traits<T>`, it should follow these rules:

1. Generate the specialization in the same support header that already holds nearby type support code.
2. Use the minimal template first.
3. Use `objectSchema` for structs and classes that serialize as JSON objects.
4. Add one `property(...)` per JSON field.
5. Use `arrayOf(...)` for vectors and arrays.
6. Use `dictionaryOf(...)` only for string-key maps.
7. Reuse `schema_traits<Nested>::schema()` for nested custom types.
8. Add descriptions only when they are concrete and useful.
9. Add defaults and examples only when they are valid JSON literals.
10. Keep field names aligned with `json_traits<T>` or runtime JSON conversion behavior.

## Prompt Template Reference

If you want an LLM to generate code directly, pair this guide with `SCHEMA_TRAITS_PROMPT.md`.

Recommended flow:

1. Attach this file as the rule document.
2. Attach `SCHEMA_TRAITS_PROMPT.md` as the prompt template source.
3. Attach the target type definition and any existing `json_traits<T>` code.
4. Ask for only the `schema_traits<T>` specialization.

## Rules For Humans Reviewing LLM Output

When reviewing generated code, check these items:

1. Field names exactly match the JSON payload shape.
2. Required vs optional is correct.
3. Nullability is correct.
4. Nested custom types reuse their own `schema_traits`.
5. `defaulted(...)` values are valid JSON literals.
6. `examples(...)` values are valid JSON literals.
7. The schema does not invent fields that runtime conversion does not support.

## Preferred Placement In This Repository

In this repository, keep `schema_traits<T>` next to related type support code.

Examples:

1. `examples/json_invoke/person_support.hpp` holds both `json_traits<Person>` and `schema_traits<Person>`.
2. Enum mappings live in `examples/json_invoke/priority_support.hpp`.

That organization is easier for both humans and LLMs to follow than scattering traits across many unrelated files.

## Quick Checklist

Before finishing a `schema_traits<T>` specialization, verify:

1. The type is actually exposed in tool parameters or return values.
2. The field list matches runtime JSON conversion.
3. Nested objects and containers are represented structurally.
4. Optional and nullable semantics are intentional.
5. Descriptions, defaults, and examples are concrete and valid.
6. The result is readable enough that another person or LLM can extend it later.