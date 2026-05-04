#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
#include "../func_registry/func_registry.hpp"
#include "../func_registry/function_summary.hpp"
#include "../type_meta/type_introspection.hpp"

namespace func_registry {

namespace tool_introspection_detail {

template<typename T>
struct stored_argument_type {
    using type = std::decay_t<T>;
};

template<typename T>
struct stored_argument_type<T&> {
    using type = std::conditional_t<std::is_const_v<T>, std::decay_t<T>, std::reference_wrapper<T>>;
};

template<typename T>
struct stored_argument_type<const T&> {
    using type = std::decay_t<T>;
};

template<typename T>
using stored_argument_type_t = typename stored_argument_type<T>::type;

template<typename Fn>
using callable_traits_t = std::conditional_t<
    std::is_member_function_pointer_v<std::decay_t<Fn>>,
    member_function_traits<std::decay_t<Fn>>,
    function_traits<std::decay_t<Fn>>>;

template<typename Traits, std::size_t... I>
void registerCallableTypeIntrospectionImpl(std::index_sequence<I...>)
{
    ensureTypeIntrospectionRegistered<typename Traits::return_type>();
    (ensureTypeIntrospectionRegistered<stored_argument_type_t<typename Traits::template arg<I>>, typename Traits::template arg<I>>(), ...);
}

} // namespace tool_introspection_detail

template<typename Fn>
void registerCallableTypeIntrospection()
{
    using Traits = tool_introspection_detail::callable_traits_t<Fn>;
    tool_introspection_detail::registerCallableTypeIntrospectionImpl<Traits>(std::make_index_sequence<Traits::arity>{});
}

template<typename Signature>
void registerSignatureTypeIntrospection()
{
    using Traits = function_traits<Signature>;
    tool_introspection_detail::registerCallableTypeIntrospectionImpl<Traits>(std::make_index_sequence<Traits::arity>{});
}

inline TypeIntrospectionInfo fallbackTypeIntrospection()
{
    TypeIntrospectionInfo info;
    info.schema = makeTypeSchema("string");
    return info;
}

inline TypeIntrospectionInfo getTypeIntrospectionOrFallback(std::type_index type)
{
    if (auto info = getTypeIntrospection(type))
    {
        return *info;
    }

    return fallbackTypeIntrospection();
}

struct ToolParameterSpec {
    std::string name;
    std::string cpp_type_name;
    std::string llm_type;
    bool required{true};
    bool nullable{false};
    std::vector<std::string> enum_values;
    TypeSchema schema;
};

struct ToolSpec {
    std::string tool_name;
    std::string function_name;
    std::string prototype;
    std::string description;
    std::vector<ToolParameterSpec> parameters;
    std::string return_cpp_type_name;
    std::string return_llm_type;
    bool return_nullable{false};
    std::vector<std::string> return_enum_values;
    TypeSchema return_schema;
};

inline ToolSpec makeToolSpec(const FunctionInfo& info, const std::string& tool_name)
{
    ToolSpec spec;
    const TypeIntrospectionInfo ret_type_info = getTypeIntrospectionOrFallback(info.ret_type);

    spec.tool_name = tool_name;
    spec.function_name = info.name;
    spec.prototype = formatFunctionInfo(info);
    spec.description = info.description;
    spec.return_cpp_type_name = info.ret_type_name;
    spec.return_llm_type = ret_type_info.llm_type;
    spec.return_nullable = ret_type_info.nullable;
    spec.return_enum_values = ret_type_info.enum_values;
    spec.return_schema = ret_type_info.schema;
    spec.parameters.reserve(info.arg_type_names.size());

    for (std::size_t i = 0; i < info.arg_type_names.size(); ++i)
    {
        ToolParameterSpec parameter_spec;
        const TypeIntrospectionInfo type_info = i < info.arg_types.size()
            ? getTypeIntrospectionOrFallback(info.arg_types[i])
            : fallbackTypeIntrospection();

        parameter_spec.name = (i < info.param_names.size() && !info.param_names[i].empty())
            ? info.param_names[i]
            : "arg" + std::to_string(i);
        parameter_spec.cpp_type_name = i < info.arg_type_names.size() ? info.arg_type_names[i] : std::string("<unknown>");
        parameter_spec.llm_type = type_info.llm_type;
        parameter_spec.required = !type_info.optional;
        parameter_spec.nullable = type_info.nullable;
        parameter_spec.enum_values = type_info.enum_values;
        parameter_spec.schema = type_info.schema;
        spec.parameters.push_back(std::move(parameter_spec));
    }

    return spec;
}

inline ToolSpec makeToolSpec(const FunctionInfo& info)
{
    return makeToolSpec(info, info.name);
}

template<typename Registry>
ToolSpec getToolSpec(const Registry& registry, const std::string& name)
{
    return makeToolSpec(getFunctionInfo(registry, name));
}

template<typename Registry>
std::vector<ToolSpec> getAllToolSpecs(const Registry& registry)
{
    const auto infos = getAllFunctionInfos(registry);
    std::vector<ToolSpec> specs;
    specs.reserve(infos.size());

    for (const auto& info : infos)
    {
        specs.push_back(makeToolSpec(info));
    }

    return specs;
}

} // namespace func_registry