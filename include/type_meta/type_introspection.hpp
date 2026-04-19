#pragma once

#include <array>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <vector>
#include "enum_traits.hpp"
#include "type_schema.hpp"

namespace func_registry {

struct TypeIntrospectionInfo {
    std::string llm_type{"unknown"};
    bool optional{false};
    bool nullable{false};
    std::vector<std::string> enum_values;
    TypeSchema schema;
};

namespace type_introspection_detail {

template<typename T>
using remove_cvref_t = std::remove_cv_t<std::remove_reference_t<T>>;

template<typename T>
struct is_std_vector : std::false_type {};

template<typename T, typename Alloc>
struct is_std_vector<std::vector<T, Alloc>> : std::true_type {};

template<typename T>
inline constexpr bool is_std_vector_v = is_std_vector<T>::value;

template<typename T>
struct is_std_array : std::false_type {};

template<typename T, std::size_t N>
struct is_std_array<std::array<T, N>> : std::true_type {};

template<typename T>
inline constexpr bool is_std_array_v = is_std_array<T>::value;

template<typename T>
struct is_std_tuple : std::false_type {};

template<typename... Ts>
struct is_std_tuple<std::tuple<Ts...>> : std::true_type {};

template<typename T>
inline constexpr bool is_std_tuple_v = is_std_tuple<T>::value;

template<typename T>
struct is_std_map : std::false_type {};

template<typename K, typename V, typename Compare, typename Alloc>
struct is_std_map<std::map<K, V, Compare, Alloc>> : std::true_type {};

template<typename T>
inline constexpr bool is_std_map_v = is_std_map<T>::value;

template<typename T>
struct is_std_unordered_map : std::false_type {};

template<typename K, typename V, typename Hash, typename KeyEqual, typename Alloc>
struct is_std_unordered_map<std::unordered_map<K, V, Hash, KeyEqual, Alloc>> : std::true_type {};

template<typename T>
inline constexpr bool is_std_unordered_map_v = is_std_unordered_map<T>::value;

template<typename T>
struct vector_value_type;

template<typename T, typename Alloc>
struct vector_value_type<std::vector<T, Alloc>> {
    using type = T;
};

template<typename T>
using vector_value_type_t = typename vector_value_type<T>::type;

template<typename T>
struct array_value_type;

template<typename T, std::size_t N>
struct array_value_type<std::array<T, N>> {
    using type = T;
};

template<typename T>
using array_value_type_t = typename array_value_type<T>::type;

template<typename T>
struct map_key_type;

template<typename K, typename V, typename Compare, typename Alloc>
struct map_key_type<std::map<K, V, Compare, Alloc>> {
    using type = K;
};

template<typename K, typename V, typename Hash, typename KeyEqual, typename Alloc>
struct map_key_type<std::unordered_map<K, V, Hash, KeyEqual, Alloc>> {
    using type = K;
};

template<typename T>
using map_key_type_t = typename map_key_type<T>::type;

template<typename T>
struct map_mapped_type;

template<typename K, typename V, typename Compare, typename Alloc>
struct map_mapped_type<std::map<K, V, Compare, Alloc>> {
    using type = V;
};

template<typename K, typename V, typename Hash, typename KeyEqual, typename Alloc>
struct map_mapped_type<std::unordered_map<K, V, Hash, KeyEqual, Alloc>> {
    using type = V;
};

template<typename T>
using map_mapped_type_t = typename map_mapped_type<T>::type;

template<typename T>
struct is_std_optional : std::false_type {};

template<typename U>
struct is_std_optional<std::optional<U>> : std::true_type {};

template<typename T>
inline constexpr bool is_std_optional_v = is_std_optional<T>::value;

template<typename T>
struct optional_value_type {
    using type = T;
};

template<typename U>
struct optional_value_type<std::optional<U>> {
    using type = U;
};

template<typename T>
using optional_value_type_t = typename optional_value_type<T>::type;

template<typename T>
std::string llm_type_name()
{
    using D = remove_cvref_t<T>;
    using Unwrapped = remove_cvref_t<optional_value_type_t<D>>;

    if constexpr (
        std::is_same_v<Unwrapped, std::string> ||
        std::is_same_v<Unwrapped, std::string_view> ||
        std::is_same_v<Unwrapped, const char*> ||
        std::is_same_v<Unwrapped, char*>)
    {
        return "string";
    }
    else if constexpr (std::is_same_v<Unwrapped, bool>)
    {
        return "boolean";
    }
    else if constexpr (has_enum_traits_v<Unwrapped>)
    {
        return "string";
    }
    else if constexpr (std::is_integral_v<Unwrapped>)
    {
        return "integer";
    }
    else if constexpr (std::is_enum_v<Unwrapped>)
    {
        return "integer";
    }
    else if constexpr (std::is_floating_point_v<Unwrapped>)
    {
        return "number";
    }
    else if constexpr (is_std_vector_v<Unwrapped> || is_std_array_v<Unwrapped> || is_std_tuple_v<Unwrapped>)
    {
        return "array";
    }
    else if constexpr (
        is_std_map_v<Unwrapped> ||
        is_std_unordered_map_v<Unwrapped> ||
        std::is_class_v<Unwrapped> ||
        std::is_union_v<Unwrapped>)
    {
        return "object";
    }
    else
    {
        return "unknown";
    }
}

template<typename T>
std::vector<std::string> enum_values()
{
    using D = remove_cvref_t<T>;
    using Unwrapped = remove_cvref_t<optional_value_type_t<D>>;

    if constexpr (has_enum_traits_v<Unwrapped>)
    {
        return func_registry::enum_names<Unwrapped>();
    }
    else
    {
        return {};
    }
}

template<typename T>
TypeSchema type_schema();

template<typename T>
TypeSchema non_optional_type_schema()
{
    using D = remove_cvref_t<T>;

    if constexpr (
        std::is_same_v<D, std::string> ||
        std::is_same_v<D, std::string_view> ||
        std::is_same_v<D, const char*> ||
        std::is_same_v<D, char*>)
    {
        return makeTypeSchema("string");
    }
    else if constexpr (std::is_same_v<D, bool>)
    {
        return makeTypeSchema("boolean");
    }
    else if constexpr (has_enum_traits_v<D>)
    {
        TypeSchema schema = makeTypeSchema("string");
        schema.enum_values = func_registry::enum_names<D>();
        return schema;
    }
    else if constexpr (std::is_integral_v<D>)
    {
        return makeTypeSchema("integer");
    }
    else if constexpr (std::is_enum_v<D>)
    {
        return makeTypeSchema("integer");
    }
    else if constexpr (std::is_floating_point_v<D>)
    {
        return makeTypeSchema("number");
    }
    else if constexpr (is_std_vector_v<D>)
    {
        return makeArraySchema(type_schema<vector_value_type_t<D>>());
    }
    else if constexpr (is_std_array_v<D>)
    {
        return makeArraySchema(type_schema<array_value_type_t<D>>());
    }
    else if constexpr (is_std_map_v<D> || is_std_unordered_map_v<D>)
    {
        using Key = remove_cvref_t<map_key_type_t<D>>;
        using Value = map_mapped_type_t<D>;

        if constexpr (
            std::is_same_v<Key, std::string> ||
            std::is_same_v<Key, std::string_view> ||
            std::is_same_v<Key, const char*> ||
            std::is_same_v<Key, char*>)
        {
            return makeDictionarySchema(type_schema<Value>());
        }
        else
        {
            return makeTypeSchema("object");
        }
    }
    else if constexpr (has_schema_traits_v<D>)
    {
        return schema_traits<D>::schema();
    }
    else if constexpr (std::is_class_v<D> || std::is_union_v<D>)
    {
        return makeTypeSchema("object");
    }
    else if constexpr (is_std_tuple_v<D>)
    {
        return makeTypeSchema("array");
    }
    else
    {
        return makeTypeSchema("string");
    }
}

template<typename T>
TypeSchema type_schema()
{
    using D = remove_cvref_t<T>;
    using Unwrapped = remove_cvref_t<optional_value_type_t<D>>;

    TypeSchema schema = non_optional_type_schema<Unwrapped>();
    if constexpr (is_std_optional_v<D>)
    {
        schema.nullable = true;
    }

    return schema;
}

template<typename T>
TypeIntrospectionInfo make_type_introspection_info()
{
    using D = remove_cvref_t<T>;

    TypeIntrospectionInfo info;
    info.llm_type = llm_type_name<T>();
    info.optional = is_std_optional_v<D>;
    info.nullable = info.optional;
    info.enum_values = enum_values<T>();
    info.schema = type_schema<T>();
    return info;
}

inline std::unordered_map<std::type_index, TypeIntrospectionInfo>& registry()
{
    static std::unordered_map<std::type_index, TypeIntrospectionInfo> entries;
    return entries;
}

inline std::mutex& registry_mutex()
{
    static std::mutex mutex;
    return mutex;
}

} // namespace type_introspection_detail

inline void registerTypeIntrospection(std::type_index type, TypeIntrospectionInfo info)
{
    std::lock_guard<std::mutex> lock(type_introspection_detail::registry_mutex());
    type_introspection_detail::registry().try_emplace(type, std::move(info));
}

inline std::optional<TypeIntrospectionInfo> getTypeIntrospection(std::type_index type)
{
    std::lock_guard<std::mutex> lock(type_introspection_detail::registry_mutex());
    const auto it = type_introspection_detail::registry().find(type);
    if (it == type_introspection_detail::registry().end())
    {
        return std::nullopt;
    }

    return it->second;
}

template<typename StorageT, typename LogicalT = StorageT>
void ensureTypeIntrospectionRegistered()
{
    using Key = std::remove_cvref_t<StorageT>;

    static const bool registered = [] {
        registerTypeIntrospection(std::type_index(typeid(Key)), type_introspection_detail::make_type_introspection_info<LogicalT>());
        return true;
    }();

    (void)registered;
}

} // namespace func_registry