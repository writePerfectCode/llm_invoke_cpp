# schema_traits Prompt Template

This file contains prompt templates for asking an LLM to generate `func_registry::schema_traits<T>` specializations.

Use this file together with `SCHEMA_TRAITS.md`.

## Recommended Usage

When asking an LLM to write `schema_traits<T>`:

1. Give it the target C++ type definition.
2. Give it the existing `json_traits<T>` if one exists.
3. Give it `SCHEMA_TRAITS.md` as the rule document.
4. Give it one of the prompt templates below.

## Full Prompt Template

```text
You are generating a func_registry::schema_traits<T> specialization for this repository.

Follow the rules in SCHEMA_TRAITS.md strictly.

Requirements:
1. Reuse the actual JSON field names used by runtime conversion.
2. Keep the specialization in the same support header as nearby type support code when possible.
3. Prefer the minimal correct schema first.
4. Use the helper API from include/type_meta/type_schema.hpp.
5. Reuse schema_traits<Nested>::schema() for nested custom types.
6. Use arrayOf(...) for vectors/arrays.
7. Use dictionaryOf(...) only for string-key maps.
8. Mark optional fields with property(name, schema, false).
9. Only add descriptions, defaults, and examples when they are concrete and valid.
10. Do not invent fields that are not supported by the actual runtime JSON shape.

Output requirements:
1. Return only the C++ code for the specialization.
2. Do not explain the code.
3. Do not change unrelated code.
4. Keep the style consistent with the repository.

Target type:
<paste type definition here>

Existing JSON conversion code if available:
<paste json_traits<T> or equivalent conversion code here>
```

## Short Prompt Template

Use this when the LLM already has the repository context loaded.

```text
Write func_registry::schema_traits<T> for the type below.

Follow SCHEMA_TRAITS.md.
Match the runtime JSON shape exactly.
Use the shorter helper API from type_schema.hpp.
Prefer the smallest correct schema.
Reuse nested schema_traits when needed.
Return only the specialization code.

Target type:
<paste type definition here>

Existing JSON conversion code if available:
<paste json_traits<T> or equivalent conversion code here>
```

## Review Prompt Template

Use this when you already have a generated specialization and want the LLM to validate it.

```text
Review this func_registry::schema_traits<T> specialization against SCHEMA_TRAITS.md.

Check:
1. Field names match runtime JSON conversion.
2. Required vs optional is correct.
3. Nullability is correct.
4. Nested custom types reuse nested schema_traits.
5. Defaults and examples are valid JSON literals.
6. No extra fields were invented.
7. The code uses the repository helper API consistently.

If there are problems, list them precisely.
If it is correct, say it is correct.

Type definition:
<paste type definition here>

Runtime JSON conversion code:
<paste json_traits<T> or equivalent conversion code here>

schema_traits code:
<paste specialization here>
```

## Suggested Attachment Set For LLM Workflows

When using an LLM tool or agent, attach these files together:

1. `SCHEMA_TRAITS.md`
2. `SCHEMA_TRAITS_PROMPT.md`
3. The target support header
4. The target type definition header
5. Existing `json_traits<T>` code if present

That bundle is usually enough for the model to generate a correct first draft.