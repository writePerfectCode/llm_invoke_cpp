#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>
#include "any_callable.hpp"

namespace func_registry {

using FunctionInfo = CallableMetadata;

struct ToolParameterSpec {
    std::string name;
    std::string cpp_type_name;
    std::string llm_type;
    bool required{true};
    bool nullable{false};
    std::vector<std::string> enum_values;
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
};

inline FunctionInfo makeFunctionInfo(std::string_view name, const AnyCallable& callable)
{
    FunctionInfo info = static_cast<const CallableMetadata&>(callable);
    if (info.name.empty())
    {
        info.name = std::string(name);
    }
    return info;
}

inline ToolParameterSpec makeToolParameterSpec(const FunctionInfo& info, std::size_t index)
{
    ToolParameterSpec spec;
    spec.name = (index < info.param_names.size() && !info.param_names[index].empty())
        ? info.param_names[index]
        : "arg" + std::to_string(index);
    spec.cpp_type_name = index < info.arg_type_names.size() ? info.arg_type_names[index] : std::string("<unknown>");
    spec.llm_type = index < info.arg_llm_types.size() ? info.arg_llm_types[index] : std::string("unknown");
    spec.required = index < info.arg_required.size() ? info.arg_required[index] : true;
    spec.nullable = index < info.arg_nullable.size() ? info.arg_nullable[index] : false;
    spec.enum_values = index < info.arg_enum_values.size() ? info.arg_enum_values[index] : std::vector<std::string>{};
    return spec;
}

inline std::string formatFunctionInfo(const FunctionInfo& info)
{
    std::string out = info.name;
    out += "(";

    for (std::size_t i = 0; i < info.arg_type_names.size(); ++i)
    {
        if (i != 0)
        {
            out += ", ";
        }

        if (i < info.param_names.size() && !info.param_names[i].empty())
        {
            out += info.arg_type_names[i];
            out += " ";
            out += info.param_names[i];
        }
        else
        {
            out += info.arg_type_names[i];
        }
    }

    out += ") -> ";
    out += info.ret_type_name.empty() ? std::string("<unknown>") : info.ret_type_name;

    if (!info.description.empty())
    {
        out += " : ";
        out += info.description;
    }

    return out;
}

inline ToolSpec makeToolSpec(const FunctionInfo& info, const std::string& tool_name)
{
    ToolSpec spec;
    spec.tool_name = tool_name;
    spec.function_name = info.name;
    spec.prototype = formatFunctionInfo(info);
    spec.description = info.description;
    spec.return_cpp_type_name = info.ret_type_name;
    spec.return_llm_type = info.ret_llm_type.empty() ? std::string("unknown") : info.ret_llm_type;
    spec.return_nullable = info.ret_nullable;
    spec.return_enum_values = info.ret_enum_values;
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

inline bool compareFunctionInfo(const FunctionInfo& lhs, const FunctionInfo& rhs)
{
    if (lhs.name != rhs.name)
    {
        return lhs.name < rhs.name;
    }
    return lhs.arg_type_names.size() < rhs.arg_type_names.size();
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
FunctionInfo getFunctionInfo(const Registry& registry, const std::string& name)
{
    return makeFunctionInfo(name, registry.getFunction(name));
}

template<typename Registry>
std::string describeFunction(const Registry& registry, const std::string& name)
{
    return formatFunctionInfo(getFunctionInfo(registry, name));
}

template<typename Registry>
std::vector<FunctionInfo> getAllFunctionInfos(const Registry& registry)
{
    std::vector<FunctionInfo> infos;
    registry.forEachFunction([&infos](std::string_view name, const AnyCallable& callable) {
        infos.push_back(makeFunctionInfo(name, callable));
    });

    std::stable_sort(infos.begin(), infos.end(), compareFunctionInfo);
    return infos;
}

template<typename Registry>
std::vector<std::string> describeAllFunctions(const Registry& registry)
{
    std::vector<FunctionInfo> infos = getAllFunctionInfos(registry);
    std::vector<std::string> descriptions;
    descriptions.reserve(infos.size());
    for (const auto& info : infos)
    {
        descriptions.push_back(formatFunctionInfo(info));
    }
    return descriptions;
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