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
#include "registry_introspection.hpp"
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
        ac.name = name;
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

    // Use this overload when arguments are already packed, such as JSON or other dynamic bridges.
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

    // Use this overload for direct C++ calls; the registry validates and packs native arguments for you.
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

    // Returns FuncCallResult when callers need both the value and the declared return type.
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

    // Native-argument variant of callByNameWrap for typed C++ call sites that still need runtime type metadata.
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

    // Use this overload when arguments are already packed and the caller expects a concrete return type R.
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

    // Native-argument variant of callByNameAs; useful for direct C++ calls that want checked typed results.
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

    template<typename Visitor>
    void forEachFunction(Visitor&& visitor) const
    {
        std::shared_lock<mutex_type> lock(mutex_);
        for (const auto& entry : tools_)
        {
            visitor(entry.first, entry.second);
        }
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