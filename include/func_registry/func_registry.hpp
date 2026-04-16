#pragma once

#include <algorithm>
#include <any>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>
#include "any_callable.hpp"
#include "call_result.hpp"

namespace func_registry {

struct NullSharedMutex {
    void lock() noexcept {}
    void unlock() noexcept {}
    void lock_shared() noexcept {}
    void unlock_shared() noexcept {}
};

struct FunctionMetadata {
    std::vector<std::string> param_names;
    std::string description;
};

struct FunctionInfo {
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

struct ToolParameterSpec {
    std::string name;
    std::string cpp_type_name;
    std::string llm_type;
    bool required{true};
};

struct ToolSpec {
    std::string tool_name;
    std::string function_name;
    std::string prototype;
    std::string description;
    std::vector<ToolParameterSpec> parameters;
    std::string return_cpp_type_name;
    std::string return_llm_type;
};

template<typename... Args>
inline constexpr bool is_packed_any_arg_v =
    sizeof...(Args) == 1 &&
    (std::is_same_v<std::remove_cvref_t<Args>, std::vector<std::any>> && ...);

template<bool EnableThreadSafety = false>
class BasicFuncRegistry {
public:
    void registerFunction(const std::string& name, AnyCallable ac) {
        std::unique_lock<mutex_type> lock(mutex_);
        if (tools_.find(name) != tools_.end())
        {
            throw std::runtime_error("function already registered: " + name);
        }
        tools_.emplace(name, std::move(ac));
    }

    void registerFunction(const std::string& name, AnyCallable ac, FunctionMetadata metadata) {
        applyMetadata(ac, std::move(metadata));
        registerFunction(name, std::move(ac));
    }

    void registerFunction(const std::string& name, AnyCallable ac, std::string description) {
        registerFunction(name, std::move(ac), FunctionMetadata{{}, std::move(description)});
    }

    template<typename Fn>
    requires (!std::is_same_v<std::remove_cvref_t<Fn>, AnyCallable>)
    void registerFunction(const std::string& name, Fn&& fn)
    {
        registerFunction(name, makeCallable(std::forward<Fn>(fn)));
    }

    template<typename Fn>
    requires (!std::is_same_v<std::remove_cvref_t<Fn>, AnyCallable>)
    void registerFunction(const std::string& name, Fn&& fn, FunctionMetadata metadata)
    {
        registerFunction(name, makeCallable(std::forward<Fn>(fn)), std::move(metadata));
    }

    template<typename Fn>
    requires (!std::is_same_v<std::remove_cvref_t<Fn>, AnyCallable>)
    void registerFunction(const std::string& name, Fn&& fn, std::string description)
    {
        registerFunction(name, makeCallable(std::forward<Fn>(fn)), std::move(description));
    }

    template<typename R, typename... Args, typename Fn>
    void registerFunctionAs(const std::string& name, Fn&& fn)
    {
        registerFunction(name, makeCallableAs<R(Args...)>(std::forward<Fn>(fn)));
    }

    template<typename R, typename... Args, typename Fn>
    void registerFunctionAs(const std::string& name, Fn&& fn, FunctionMetadata metadata)
    {
        registerFunction(name, makeCallableAs<R(Args...)>(std::forward<Fn>(fn)), std::move(metadata));
    }

    template<typename R, typename... Args, typename Fn>
    void registerFunctionAs(const std::string& name, Fn&& fn, std::string description)
    {
        registerFunction(name, makeCallableAs<R(Args...)>(std::forward<Fn>(fn)), std::move(description));
    }

    AnyCallable getFunction(const std::string& name) const {
        std::shared_lock<mutex_type> lock(mutex_);
        auto it = tools_.find(name);
        if (it == tools_.end())
        {
            throw std::runtime_error("function not found: " + name);
        }
        return it->second;
    }

    FunctionInfo getFunctionInfo(const std::string& name) const {
        std::shared_lock<mutex_type> lock(mutex_);
        auto it = tools_.find(name);
        if (it == tools_.end())
        {
            throw std::runtime_error("function not found: " + name);
        }
        return makeFunctionInfo(name, it->second);
    }

    std::string describeFunction(const std::string& name) const {
        return formatFunctionInfo(getFunctionInfo(name));
    }

    std::vector<FunctionInfo> getAllFunctionInfos() const {
        std::shared_lock<mutex_type> lock(mutex_);

        std::vector<FunctionInfo> infos;
        infos.reserve(tools_.size());
        for (const auto& entry : tools_)
        {
            infos.push_back(makeFunctionInfo(entry.first, entry.second));
        }

        std::stable_sort(infos.begin(), infos.end(), compareFunctionInfo);

        return infos;
    }

    std::vector<std::string> describeAllFunctions() const {
        std::vector<FunctionInfo> infos = getAllFunctionInfos();
        std::vector<std::string> descriptions;
        descriptions.reserve(infos.size());
        for (const auto& info : infos)
        {
            descriptions.push_back(formatFunctionInfo(info));
        }
        return descriptions;
    }

    ToolSpec getToolSpec(const std::string& name) const {
        return makeToolSpec(getFunctionInfo(name));
    }

    std::vector<ToolSpec> getAllToolSpecs() const {
        return makeToolSpecs(getAllFunctionInfos());
    }

    std::string renderAllToolSpecs(std::string_view separator = "\n") const {
        return joinLines(formatToolSpecs(getAllToolSpecs()), separator);
    }

    std::any callByName(const std::string& name, const std::vector<std::any>& args) const
    {
        try
        {
            AnyCallable callable = getFunction(name);
            return callable.fn(args);
        }
        catch (const std::exception& e)
        {
            throw std::runtime_error("call failed: " + name + ": " + e.what());
        }
    }

    FuncCallResult callByNameWrap(const std::string& name, const std::vector<std::any>& args) const
    {
        try
        {
            AnyCallable callable = getFunction(name);
            std::any result = callable.fn(args);
            return FuncCallResult(std::move(result), callable.ret_type);
        }
        catch (const std::exception& e)
        {
            throw std::runtime_error("call failed: " + name + ": " + e.what());
        }
    }

    template<typename R>
    R callByNameAs(const std::string& name, const std::vector<std::any>& args) const
    {
        try
        {
            AnyCallable callable = getFunction(name);
            std::any result = callable.fn(args);
            return cast_result<R>(callable, result);
        }
        catch (const std::exception& e)
        {
            throw std::runtime_error("call failed: " + name + ": " + e.what());
        }
    }

    std::tuple<std::any, std::type_index> callByNameWithRetType(const std::string& name, const std::vector<std::any>& args) const
    {
        try
        {
            AnyCallable callable = getFunction(name);
            std::any result = callable.fn(args);
            return {std::move(result), callable.ret_type};
        }
        catch (const std::exception& e)
        {
            throw std::runtime_error("call failed: " + name + ": " + e.what());
        }
    }

    template<typename... Args>
    requires (!is_packed_any_arg_v<Args...>)
    std::any callByName(const std::string& name, Args&&... args) const
    {
        try
        {
            AnyCallable callable = resolveCallable(name, std::forward<Args>(args)...);
            std::vector<std::any> packed = packArgs(callable.arg_types, std::forward<Args>(args)...);
            return callable.fn(packed);
        }
        catch (const std::exception& e)
        {
            throw std::runtime_error("call failed: " + name + ": " + e.what());
        }
    }

    template<typename... Args>
    requires (!is_packed_any_arg_v<Args...>)
    FuncCallResult callByNameWrap(const std::string& name, Args&&... args) const
    {
        try
        {
            AnyCallable callable = resolveCallable(name, std::forward<Args>(args)...);
            std::vector<std::any> packed = packArgs(callable.arg_types, std::forward<Args>(args)...);
            std::any result = callable.fn(packed);
            return FuncCallResult(std::move(result), callable.ret_type);
        }
        catch (const std::exception& e)
        {
            throw std::runtime_error("call failed: " + name + ": " + e.what());
        }
    }

    template<typename R, typename... Args>
    requires (!is_packed_any_arg_v<Args...>)
    R callByNameAs(const std::string& name, Args&&... args) const
    {
        try
        {
            AnyCallable callable = resolveCallable(name, std::forward<Args>(args)...);
            std::vector<std::any> packed = packArgs(callable.arg_types, std::forward<Args>(args)...);
            std::any result = callable.fn(packed);
            return cast_result<R>(callable, result);
        }
        catch (const std::exception& e)
        {
            throw std::runtime_error("call failed: " + name + ": " + e.what());
        }
    }

    template<typename... Args>
    requires (!is_packed_any_arg_v<Args...>)
    std::tuple<std::any, std::type_index> callByNameWithRetType(const std::string& name, Args&&... args) const
    {
        try
        {
            AnyCallable callable = resolveCallable(name, std::forward<Args>(args)...);
            std::vector<std::any> packed = packArgs(callable.arg_types, std::forward<Args>(args)...);
            std::any result = callable.fn(packed);
            return {std::move(result), callable.ret_type};
        }
        catch (const std::exception& e)
        {
            throw std::runtime_error("call failed: " + name + ": " + e.what());
        }
    }

    bool hasFunction(const std::string& name) const {
        std::shared_lock<mutex_type> lock(mutex_);
        return tools_.find(name) != tools_.end();
    }

private:
    using mutex_type = std::conditional_t<EnableThreadSafety, std::shared_mutex, NullSharedMutex>;

    static void applyMetadata(AnyCallable& callable, FunctionMetadata metadata)
    {
        if (!metadata.param_names.empty() && metadata.param_names.size() != callable.arg_types.size())
        {
            throw std::invalid_argument(
                "parameter name count mismatch: expected " + std::to_string(callable.arg_types.size()) +
                ", got " + std::to_string(metadata.param_names.size()));
        }

        callable.param_names = std::move(metadata.param_names);
        callable.description = std::move(metadata.description);
    }

    static FunctionInfo makeFunctionInfo(const std::string& name, const AnyCallable& callable)
    {
        return FunctionInfo{
            name,
            callable.ret_type,
            callable.ret_type_name,
            callable.ret_llm_type,
            callable.arg_types,
            callable.arg_type_names,
            callable.arg_llm_types,
            callable.param_names,
            callable.description,
        };
    }

    static ToolParameterSpec makeToolParameterSpec(const FunctionInfo& info, std::size_t index)
    {
        ToolParameterSpec spec;
        spec.name = (index < info.param_names.size() && !info.param_names[index].empty())
            ? info.param_names[index]
            : "arg" + std::to_string(index);
        spec.cpp_type_name = index < info.arg_type_names.size() ? info.arg_type_names[index] : std::string("<unknown>");
        spec.llm_type = index < info.arg_llm_types.size() ? info.arg_llm_types[index] : std::string("unknown");
        return spec;
    }

    static ToolSpec makeToolSpec(const FunctionInfo& info, const std::string& tool_name)
    {
        ToolSpec spec;
        spec.tool_name = tool_name;
        spec.function_name = info.name;
        spec.prototype = formatFunctionInfo(info);
        spec.description = info.description;
        spec.return_cpp_type_name = info.ret_type_name;
        spec.return_llm_type = info.ret_llm_type.empty() ? std::string("unknown") : info.ret_llm_type;
        spec.parameters.reserve(info.arg_type_names.size());

        for (std::size_t i = 0; i < info.arg_type_names.size(); ++i)
        {
            spec.parameters.push_back(makeToolParameterSpec(info, i));
        }

        return spec;
    }

    static ToolSpec makeToolSpec(const FunctionInfo& info)
    {
        return makeToolSpec(info, info.name);
    }

    static bool compareFunctionInfo(const FunctionInfo& lhs, const FunctionInfo& rhs)
    {
        if (lhs.name != rhs.name)
        {
            return lhs.name < rhs.name;
        }
        return lhs.arg_type_names.size() < rhs.arg_type_names.size();
    }

    static std::vector<ToolSpec> makeToolSpecs(const std::vector<FunctionInfo>& infos)
    {
        std::vector<ToolSpec> specs;
        specs.reserve(infos.size());

        for (const auto& info : infos)
        {
            specs.push_back(makeToolSpec(info));
        }

        return specs;
    }

    static std::string formatToolSpec(const ToolSpec& tool)
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

    static std::vector<std::string> formatToolSpecs(const std::vector<ToolSpec>& tools)
    {
        std::vector<std::string> lines;
        lines.reserve(tools.size());
        for (const auto& tool : tools)
        {
            lines.push_back(formatToolSpec(tool));
        }
        return lines;
    }

    static std::string joinLines(const std::vector<std::string>& lines, std::string_view separator)
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

    static std::string formatFunctionInfo(const FunctionInfo& info)
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

    static std::string formatTypeList(const std::vector<std::type_index>& types)
    {
        std::string out = "(";
        bool first = true;
        for (const auto& type : types)
        {
            if (!first) out += ", ";
            first = false;
            out += type.name();
        }
        out += ")";
        return out;
    }

    template<typename... Args>
    static std::string formatCallTypeList()
    {
        std::string out = "(";
        if constexpr (sizeof...(Args) > 0) {
            bool first = true;
            auto append = [&](std::type_index type) {
                if (!first) out += ", ";
                first = false;
                out += type.name();
            };
            (append(std::type_index(typeid(std::decay_t<Args>))), ...);
        }
        out += ")";
        return out;
    }

    template<typename Arg>
    static std::any packOneArg(const std::type_index& expected, Arg&& arg)
    {
        using Raw = std::remove_cvref_t<Arg>;
        using Decayed = std::decay_t<Raw>;

        if (expected == typeid(std::string))
        {
            if constexpr (std::is_same_v<Decayed, const char*> || std::is_same_v<Decayed, char*>)
            {
                return std::any(std::string(arg));
            }
            else if constexpr (std::is_same_v<Decayed, std::string_view>)
            {
                return std::any(std::string(arg));
            }
        }

        if constexpr (std::is_lvalue_reference_v<Arg> && !std::is_const_v<std::remove_reference_t<Arg>>)
        {
            using T = std::remove_reference_t<Arg>;
            using RefWrap = std::reference_wrapper<T>;
            if (expected == typeid(RefWrap))
            {
                return std::make_any<RefWrap>(std::ref(arg));
            }
        }

        return std::make_any<std::decay_t<Arg>>(std::forward<Arg>(arg));
    }

    template<typename Arg>
    static bool isArgCompatible(const std::type_index& expected, Arg&&)
    {
        using Raw = std::remove_cvref_t<Arg>;
        using Decayed = std::decay_t<Raw>;

        if (expected == typeid(std::string))
        {
            if constexpr (std::is_same_v<Decayed, const char*> || std::is_same_v<Decayed, char*> || std::is_same_v<Decayed, std::string_view>)
            {
                return true;
            }
        }

        if constexpr (std::is_lvalue_reference_v<Arg> && !std::is_const_v<std::remove_reference_t<Arg>>)
        {
            using T = std::remove_reference_t<Arg>;
            using RefWrap = std::reference_wrapper<T>;
            if (expected == typeid(RefWrap))
            {
                return true;
            }
        }

        return expected == typeid(std::decay_t<Arg>);
    }

    template<typename... Args>
    static bool isCallableCompatible(const std::vector<std::type_index>& expected, Args&&... args)
    {
        if (expected.size() != sizeof...(Args))
        {
            return false;
        }

        std::size_t i = 0;
        return (isArgCompatible(expected[i++], std::forward<Args>(args)) && ...);
    }

    template<typename... Args>
    AnyCallable resolveCallable(const std::string& name, Args&&... args) const
    {
        std::shared_lock<mutex_type> lock(mutex_);

        auto it = tools_.find(name);
        if (it == tools_.end())
        {
            throw std::runtime_error("function not found: " + name);
        }

        const AnyCallable& callable = it->second;
        if (!isCallableCompatible(callable.arg_types, std::forward<Args>(args)...))
        {
            throw std::runtime_error(
                "signature mismatch for function '" + name +
                "', call args " + formatCallTypeList<Args...>() +
                ", expected: " + formatTypeList(callable.arg_types));
        }

        return callable;
    }

    template<typename... Args>
    static std::vector<std::any> packArgs(const std::vector<std::type_index>& expected, Args&&... args)
    {
        if (expected.size() != sizeof...(Args))
        {
            throw std::runtime_error("arity mismatch: expected " + std::to_string(expected.size()) + ", got " + std::to_string(sizeof...(Args)));
        }

        std::vector<std::any> packed;
        packed.reserve(sizeof...(Args));

        std::size_t i = 0;
        (packed.emplace_back(packOneArg(expected[i++], std::forward<Args>(args))), ...);
        return packed;
    }

    std::unordered_map<std::string, AnyCallable> tools_;
    mutable mutex_type mutex_;
};

using FuncRegistry = BasicFuncRegistry<false>;
using FuncRegistryThreadSafe = BasicFuncRegistry<true>;

} // namespace func_registry