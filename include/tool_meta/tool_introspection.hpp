#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
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

template<typename Registry, typename Fn>
void registerToolFunction(Registry& registry, const std::string& name, Fn&& fn)
{
    registerCallableTypeIntrospection<Fn>();
    registry.registerFunction(name, std::forward<Fn>(fn));
}

template<typename Registry, typename Fn>
void registerToolFunction(Registry& registry, const std::string& name, Fn&& fn, FunctionMetadata metadata)
{
    registerCallableTypeIntrospection<Fn>();
    registry.registerFunction(name, std::forward<Fn>(fn), std::move(metadata));
}

template<typename Registry, typename Fn>
void registerToolFunction(Registry& registry, const std::string& name, Fn&& fn, std::string description)
{
    registerCallableTypeIntrospection<Fn>();
    registry.registerFunction(name, std::forward<Fn>(fn), std::move(description));
}

template<typename R, typename... Args, typename Registry, typename Fn>
void registerToolFunctionAs(Registry& registry, const std::string& name, Fn&& fn)
{
    registerSignatureTypeIntrospection<R(Args...)>();
    registry.template registerFunctionAs<R, Args...>(name, std::forward<Fn>(fn));
}

template<typename R, typename... Args, typename Registry, typename Fn>
void registerToolFunctionAs(Registry& registry, const std::string& name, Fn&& fn, FunctionMetadata metadata)
{
    registerSignatureTypeIntrospection<R(Args...)>();
    registry.template registerFunctionAs<R, Args...>(name, std::forward<Fn>(fn), std::move(metadata));
}

template<typename R, typename... Args, typename Registry, typename Fn>
void registerToolFunctionAs(Registry& registry, const std::string& name, Fn&& fn, std::string description)
{
    registerSignatureTypeIntrospection<R(Args...)>();
    registry.template registerFunctionAs<R, Args...>(name, std::forward<Fn>(fn), std::move(description));
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

inline ToolParameterSpec makeToolParameterSpec(const FunctionInfo& info, std::size_t index)
{
    ToolParameterSpec spec;
    const TypeIntrospectionInfo type_info = index < info.arg_types.size()
        ? getTypeIntrospectionOrFallback(info.arg_types[index])
        : fallbackTypeIntrospection();

    spec.name = (index < info.param_names.size() && !info.param_names[index].empty())
        ? info.param_names[index]
        : "arg" + std::to_string(index);
    spec.cpp_type_name = index < info.arg_type_names.size() ? info.arg_type_names[index] : std::string("<unknown>");
    spec.llm_type = type_info.llm_type;
    spec.required = !type_info.optional;
    spec.nullable = type_info.nullable;
    spec.enum_values = type_info.enum_values;
    spec.schema = type_info.schema;
    return spec;
}

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
        spec.parameters.push_back(makeToolParameterSpec(info, i));
    }

    return spec;
}

inline ToolSpec makeToolSpec(const FunctionInfo& info)
{
    return makeToolSpec(info, info.name);
}

inline std::vector<ToolSpec> makeToolSpecs(const std::vector<FunctionInfo>& infos)
{
    std::vector<ToolSpec> specs;
    specs.reserve(infos.size());

    for (const auto& info : infos)
    {
        specs.push_back(makeToolSpec(info));
    }

    return specs;
}

inline std::string formatToolSpec(const ToolSpec& tool)
{
    std::string out = tool.tool_name;
    out += " => ";

    if (tool.parameters.empty())
    {
        out += "(no args)";
    }
    else
    {
        for (std::size_t i = 0; i < tool.parameters.size(); ++i)
        {
            if (i != 0)
            {
                out += ", ";
            }

            out += tool.parameters[i].name;
            if (!tool.parameters[i].required)
            {
                out += "?";
            }
            out += ": ";
            out += tool.parameters[i].llm_type;
            out += " (";
            out += tool.parameters[i].cpp_type_name;
            out += ")";
        }
    }

    out += " -> ";
    out += tool.return_llm_type;
    out += " (";
    out += tool.return_cpp_type_name;
    out += ")";

    if (!tool.description.empty())
    {
        out += " : ";
        out += tool.description;
    }

    return out;
}

inline std::vector<std::string> formatToolSpecs(const std::vector<ToolSpec>& tools)
{
    std::vector<std::string> lines;
    lines.reserve(tools.size());
    for (const auto& tool : tools)
    {
        lines.push_back(formatToolSpec(tool));
    }
    return lines;
}

inline std::string joinLines(const std::vector<std::string>& lines, std::string_view separator)
{
    std::string out;
    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        if (i != 0)
        {
            out += separator;
        }
        out += lines[i];
    }
    return out;
}

template<typename Registry>
ToolSpec getToolSpec(const Registry& registry, const std::string& name)
{
    return makeToolSpec(getFunctionInfo(registry, name));
}

template<typename Registry>
std::vector<ToolSpec> getAllToolSpecs(const Registry& registry)
{
    return makeToolSpecs(getAllFunctionInfos(registry));
}

template<typename Registry>
std::string renderAllToolSpecs(const Registry& registry, std::string_view separator = "\n")
{
    return joinLines(formatToolSpecs(getAllToolSpecs(registry)), separator);
}

} // namespace func_registry