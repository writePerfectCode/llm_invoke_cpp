#pragma once

#include <any>
#include <array>
#include <functional>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <vector>
#include "function_traits.hpp"

namespace func_registry {

struct CallableMetadata {
    std::string name;
    std::type_index ret_type{typeid(void)};
    std::string ret_type_name;
    std::string ret_llm_type;
    std::vector<std::type_index> arg_types;
    std::vector<std::string> arg_type_names;
    std::vector<std::string> arg_llm_types;
    std::vector<std::string> param_names;
    std::string description;
};

struct AnyCallable : CallableMetadata {
    std::function<std::any(const std::vector<std::any>&)> fn;
};

namespace any_callable_detail {
inline void replaceAll(std::string& value, std::string_view from, std::string_view to)
{
    if (from.empty())
    {
        return;
    }

    std::size_t pos = 0;
    while ((pos = value.find(from.data(), pos, from.size())) != std::string::npos)
    {
        value.replace(pos, from.size(), to.data(), to.size());
        pos += to.size();
    }
}

inline std::string normalizeTypeName(std::string name)
{
    replaceAll(name, "std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >", "std::string");
    replaceAll(name, "std::basic_string<char, std::char_traits<char>, std::allocator<char> >", "std::string");
    replaceAll(name, "std::__cxx11::basic_string<char>", "std::string");
    replaceAll(name, "std::basic_string<char>", "std::string");
    replaceAll(name, "std::basic_string_view<char, std::char_traits<char> >", "std::string_view");
    replaceAll(name, "std::basic_string_view<char>", "std::string_view");
    return name;
}

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
    else if constexpr (std::is_integral_v<Unwrapped>)
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
    else if constexpr (is_std_map_v<Unwrapped> || is_std_unordered_map_v<Unwrapped> || std::is_class_v<Unwrapped> || std::is_union_v<Unwrapped>)
    {
        return "object";
    }
    else
    {
        return "unknown";
    }
}

template<typename T>
std::string type_name()
{
#if defined(__clang__) || defined(__GNUC__)
    std::string_view signature = __PRETTY_FUNCTION__;
    const std::string_view marker = "T = ";
    const std::size_t start = signature.find(marker);
    if (start == std::string_view::npos)
    {
        return "<unknown>";
    }

    const std::size_t type_start = start + marker.size();
    const std::size_t type_end = signature.find_first_of(';', type_start) != std::string_view::npos
        ? signature.find_first_of(';', type_start)
        : signature.find(']', type_start);
    if (type_end == std::string_view::npos)
    {
        return "<unknown>";
    }

    return normalizeTypeName(std::string(signature.substr(type_start, type_end - type_start)));
#elif defined(_MSC_VER)
    std::string_view signature = __FUNCSIG__;
    const std::string_view prefix = "type_name<";
    const std::string_view suffix = ">(void)";
    const std::size_t start = signature.find(prefix);
    if (start == std::string_view::npos)
    {
        return "<unknown>";
    }

    const std::size_t type_start = start + prefix.size();
    const std::size_t type_end = signature.rfind(suffix);
    if (type_end == std::string_view::npos || type_end < type_start)
    {
        return "<unknown>";
    }

    return normalizeTypeName(std::string(signature.substr(type_start, type_end - type_start)));
#else
    return normalizeTypeName(typeid(T).name());
#endif
}

template<typename T>
struct expected_any_type {
    using type = std::decay_t<T>;
};

template<typename T>
struct expected_any_type<T&> {
    using type = std::conditional_t<std::is_const_v<T>, std::decay_t<T>, std::reference_wrapper<T>>;
};

template<typename T>
struct expected_any_type<const T&> {
    using type = std::decay_t<T>;
};

template<typename T>
using expected_any_type_t = typename expected_any_type<T>::type;

template<typename Param>
decltype(auto) any_to_param(const std::any& a)
{
    if constexpr (std::is_lvalue_reference_v<Param> && !std::is_const_v<std::remove_reference_t<Param>>)
    {
        using T = std::remove_reference_t<Param>;
        return std::any_cast<std::reference_wrapper<T>>(a).get();
    }
    else
    {
        return std::any_cast<std::decay_t<Param>>(a);
    }
}
} // namespace any_callable_detail

inline void validate_args(const std::vector<std::type_index>& expected, const std::vector<std::any>& packed)
{
    if (packed.size() != expected.size())
    {
        std::ostringstream oss;
        oss << "arity mismatch: expected " << expected.size() << ", got " << packed.size();
        throw std::runtime_error(oss.str());
    }

    for (std::size_t idx = 0; idx < expected.size(); ++idx)
    {
        const std::type_index actual = packed[idx].has_value() ? std::type_index(packed[idx].type()) : std::type_index(typeid(void));
        if (actual != expected[idx])
        {
            std::ostringstream oss;
            oss << "type mismatch at arg[" << idx << "]: expected " << expected[idx].name() << ", got " << actual.name();
            throw std::runtime_error(oss.str());
        }
    }
}

template<typename Fn, typename Traits, size_t... I>
static AnyCallable makeCallable_impl(Fn&& fn, std::index_sequence<I...>)
{
    using R = typename Traits::return_type;

    AnyCallable ac;
    ac.ret_type = typeid(R);
    ac.ret_type_name = any_callable_detail::type_name<R>();
    ac.ret_llm_type = any_callable_detail::llm_type_name<R>();
    ac.arg_types = { std::type_index(typeid(any_callable_detail::expected_any_type_t<typename Traits::template arg<I>>))... };
    ac.arg_type_names = { any_callable_detail::type_name<typename Traits::template arg<I>>()... };
    ac.arg_llm_types = { any_callable_detail::llm_type_name<typename Traits::template arg<I>>()... };

    using FnStored = std::decay_t<Fn>;
    FnStored stored = std::forward<Fn>(fn);

    const std::vector<std::type_index> expected_arg_types = ac.arg_types;

    ac.fn = [stored = std::move(stored), expected_arg_types](const std::vector<std::any>& packed) mutable -> std::any {
        validate_args(expected_arg_types, packed);
        if constexpr (std::is_void_v<R>)
        {
            std::invoke(stored, any_callable_detail::any_to_param<typename Traits::template arg<I>>(packed[I])...);
            return std::any{};
        }
        else
        {
            return std::any(
                std::invoke(stored, any_callable_detail::any_to_param<typename Traits::template arg<I>>(packed[I])...)
            );
        }
    };

    return ac;
}

template<typename Fn>
AnyCallable makeCallable(Fn&& fn)
{
    using FnD = std::decay_t<Fn>;
    using Traits = std::conditional_t<
        std::is_member_function_pointer_v<FnD>,
        member_function_traits<FnD>,
        function_traits<FnD>>;
    return makeCallable_impl<Fn, Traits>(std::forward<Fn>(fn), std::make_index_sequence<Traits::arity>{});
}

template<typename Signature, typename Fn>
AnyCallable makeCallableAs(Fn&& fn)
{
    using Traits = function_traits<Signature>;
    return makeCallable_impl<Fn, Traits>(std::forward<Fn>(fn), std::make_index_sequence<Traits::arity>{});
}

template<class T>
T cast_result(const AnyCallable& ac, const std::any& r)
{
    if (ac.ret_type != typeid(T))
    {
        throw std::runtime_error("return type mismatch");
    }

    if constexpr (std::is_void_v<T>)
    {
        if (r.has_value())
        {
            throw std::runtime_error("expected void result");
        }
        return;
    }
    else
    {
        return std::any_cast<T>(r);
    }
}

} // namespace func_registry