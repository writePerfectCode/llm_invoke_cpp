#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>
#include "any_callable.hpp"

namespace func_registry {

using FunctionInfo = CallableMetadata;

inline FunctionInfo makeFunctionInfo(std::string_view name, const AnyCallable& callable)
{
    FunctionInfo info = static_cast<const CallableMetadata&>(callable);
    if (info.name.empty())
    {
        info.name = std::string(name);
    }
    return info;
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

inline bool compareFunctionInfo(const FunctionInfo& lhs, const FunctionInfo& rhs)
{
    if (lhs.name != rhs.name)
    {
        return lhs.name < rhs.name;
    }
    return lhs.arg_type_names.size() < rhs.arg_type_names.size();
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

} // namespace func_registry