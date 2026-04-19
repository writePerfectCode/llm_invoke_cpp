#pragma once

#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace func_registry {

template<typename T>
struct enum_traits;

namespace enum_traits_detail {

template<typename T, typename = void>
struct has_enum_traits : std::false_type {};

template<typename T>
struct has_enum_traits<T, std::void_t<decltype(enum_traits<T>::entries)>> : std::true_type {};

} // namespace enum_traits_detail

template<typename T>
inline constexpr bool has_enum_traits_v = enum_traits_detail::has_enum_traits<T>::value;

template<typename T>
std::vector<std::string> enum_names()
{
    static_assert(has_enum_traits_v<T>, "enum_traits<T> specialization is required");

    std::vector<std::string> names;
    names.reserve(enum_traits<T>::entries.size());
    for (const auto& entry : enum_traits<T>::entries)
    {
        names.emplace_back(entry.second);
    }

    return names;
}

template<typename T>
std::string enum_name(T value)
{
    static_assert(has_enum_traits_v<T>, "enum_traits<T> specialization is required");

    for (const auto& entry : enum_traits<T>::entries)
    {
        if (entry.first == value)
        {
            return std::string(entry.second);
        }
    }

    throw std::invalid_argument("enum value is not mapped by enum_traits<T>");
}

template<typename T>
T enum_value(std::string_view name)
{
    static_assert(has_enum_traits_v<T>, "enum_traits<T> specialization is required");

    for (const auto& entry : enum_traits<T>::entries)
    {
        if (entry.second == name)
        {
            return entry.first;
        }
    }

    throw std::invalid_argument("enum string is not mapped by enum_traits<T>: " + std::string(name));
}

} // namespace func_registry