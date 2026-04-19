#pragma once

#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace func_registry {

struct TypeSchema;

struct TypeSchemaProperty {
    std::string name;
    std::shared_ptr<TypeSchema> schema;
    bool required{true};
};

struct TypeSchema {
    std::string type{"string"};
    bool nullable{false};
    std::string description;
    std::optional<std::string> default_json;
    std::vector<std::string> examples_json;
    std::vector<std::string> enum_values;
    std::shared_ptr<TypeSchema> items;
    std::shared_ptr<TypeSchema> additional_properties;
    std::vector<TypeSchemaProperty> properties;
    bool additional_properties_allowed{true};
};

template<typename T>
struct schema_traits;

namespace type_schema_detail {

template<typename T, typename = void>
struct has_schema_traits : std::false_type {};

template<typename T>
struct has_schema_traits<T, std::void_t<decltype(schema_traits<T>::schema())>> : std::true_type {};

} // namespace type_schema_detail

template<typename T>
inline constexpr bool has_schema_traits_v = type_schema_detail::has_schema_traits<T>::value;

inline TypeSchema makeTypeSchema(std::string type, bool nullable = false)
{
    TypeSchema schema;
    schema.type = std::move(type);
    schema.nullable = nullable;
    return schema;
}

inline TypeSchema stringSchema(bool nullable = false)
{
    return makeTypeSchema("string", nullable);
}

inline TypeSchema integerSchema(bool nullable = false)
{
    return makeTypeSchema("integer", nullable);
}

inline TypeSchema numberSchema(bool nullable = false)
{
    return makeTypeSchema("number", nullable);
}

inline TypeSchema booleanSchema(bool nullable = false)
{
    return makeTypeSchema("boolean", nullable);
}

inline TypeSchemaProperty makeSchemaProperty(std::string name, TypeSchema schema, bool required = true)
{
    return TypeSchemaProperty{std::move(name), std::make_shared<TypeSchema>(std::move(schema)), required};
}

inline TypeSchemaProperty property(std::string name, TypeSchema schema, bool required = true)
{
    return makeSchemaProperty(std::move(name), std::move(schema), required);
}

inline TypeSchema makeObjectSchema(
    std::vector<TypeSchemaProperty> properties = {},
    bool additional_properties_allowed = false)
{
    TypeSchema schema;
    schema.type = "object";
    schema.properties = std::move(properties);
    schema.additional_properties_allowed = additional_properties_allowed;
    return schema;
}

inline TypeSchema objectSchema(
    std::vector<TypeSchemaProperty> properties = {},
    bool additional_properties_allowed = false)
{
    return makeObjectSchema(std::move(properties), additional_properties_allowed);
}

inline TypeSchema makeArraySchema(TypeSchema items)
{
    TypeSchema schema;
    schema.type = "array";
    schema.items = std::make_shared<TypeSchema>(std::move(items));
    schema.additional_properties_allowed = false;
    return schema;
}

inline TypeSchema arrayOf(TypeSchema items)
{
    return makeArraySchema(std::move(items));
}

inline TypeSchema makeDictionarySchema(TypeSchema value_schema)
{
    TypeSchema schema;
    schema.type = "object";
    schema.additional_properties = std::make_shared<TypeSchema>(std::move(value_schema));
    return schema;
}

inline TypeSchema dictionaryOf(TypeSchema value_schema)
{
    return makeDictionarySchema(std::move(value_schema));
}

inline TypeSchema withDescription(TypeSchema schema, std::string description)
{
    schema.description = std::move(description);
    return schema;
}

inline TypeSchema described(TypeSchema schema, std::string description)
{
    return withDescription(std::move(schema), std::move(description));
}

inline TypeSchema withDefaultJson(TypeSchema schema, std::string default_json)
{
    schema.default_json = std::move(default_json);
    return schema;
}

inline TypeSchema defaulted(TypeSchema schema, std::string default_json)
{
    return withDefaultJson(std::move(schema), std::move(default_json));
}

inline TypeSchema withExamplesJson(TypeSchema schema, std::vector<std::string> examples_json)
{
    schema.examples_json = std::move(examples_json);
    return schema;
}

inline TypeSchema examples(TypeSchema schema, std::vector<std::string> examples_json)
{
    return withExamplesJson(std::move(schema), std::move(examples_json));
}

} // namespace func_registry