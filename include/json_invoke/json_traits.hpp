#pragma once

#include <type_traits>
#include <utility>
#include <nlohmann/json.hpp>

namespace json_invoke {

template<typename T>
struct json_traits;

namespace detail {

template<typename T, typename = void>
struct has_json_traits : std::false_type {};

template<typename T>
struct has_json_traits<T, std::void_t<
    decltype(json_traits<T>::from_json_value(std::declval<const nlohmann::json&>())),
    decltype(json_traits<T>::to_json_value(std::declval<const T&>()))>> : std::true_type {};

template<typename T>
inline constexpr bool has_json_traits_v = has_json_traits<T>::value;

} // namespace detail

} // namespace json_invoke

namespace nlohmann {

template<typename T>
struct adl_serializer<T, std::enable_if_t<json_invoke::detail::has_json_traits_v<T>, void>> {
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