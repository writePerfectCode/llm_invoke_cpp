#pragma once

#include <type_traits>
#include <utility>
#include <nlohmann/json.hpp>

namespace json_invoke {

template<typename T>
struct json_traits;

namespace detail {

template<typename T>
concept has_json_traits = requires(const nlohmann::json& value, const T& object) {
    json_traits<T>::from_json_value(value);
    json_traits<T>::to_json_value(object);
};

template<typename T>
inline constexpr bool has_json_traits_v = has_json_traits<T>;

} // namespace detail

} // namespace json_invoke

namespace nlohmann {

template<typename T>
    requires json_invoke::detail::has_json_traits<T>
struct adl_serializer<T, void> {
    static void to_json(json& value, const T& object)
    {
        value = json_invoke::json_traits<T>::to_json_value(object);
    }

    static T from_json(const json& value)
    {
        return json_invoke::json_traits<T>::from_json_value(value);
    }
};

} // namespace nlohmann