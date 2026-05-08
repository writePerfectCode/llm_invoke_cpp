#pragma once

#include <any>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>
#include "../func_registry/func_registry.hpp"
#include "../tool_meta/tool_introspection.hpp"
#include "../type_meta/enum_traits.hpp"
#include "json_error.hpp"
#include "json_tool_execution_semantics.hpp"
#include "json_trace.hpp"
#include "json_introspection.hpp"
#include "json_traits.hpp"

namespace json_invoke {

using func_registry::AnyCallable;
using func_registry::BasicFuncRegistry;
using func_registry::FuncCallResult;
using func_registry::FunctionInfo;
using func_registry::FunctionMetadata;
using func_registry::getFunctionInfo;

class JsonTypeRegistry;

template<typename Fn>
struct AnnotatedTool {
    using callable_type = Fn;

    Fn fn;
    ToolExecutionSemantics semantics{ToolExecutionSemantics::unknown};
};

template<typename Fn>
auto readOnly(Fn&& fn)
{
    return AnnotatedTool<std::decay_t<Fn>>{
        std::forward<Fn>(fn),
        ToolExecutionSemantics::read_only,
    };
}

template<typename Fn>
auto mutating(Fn&& fn)
{
    return AnnotatedTool<std::decay_t<Fn>>{
        std::forward<Fn>(fn),
        ToolExecutionSemantics::mutating,
    };
}

namespace detail {

template<typename T>
using remove_cvref_t = std::remove_cv_t<std::remove_reference_t<T>>;

template<typename T>
concept has_json_get = requires(const json& value) {
    value.template get<T>();
};

template<typename T>
concept has_json_constructor = requires(const T& value) {
    json(value);
};

template<typename T>
inline constexpr bool has_nlohmann_json_binding_v = has_json_get<T> && has_json_constructor<T>;

template<typename T>
struct is_std_optional : std::false_type {};

template<typename U>
struct is_std_optional<std::optional<U>> : std::true_type {};

template<typename T>
inline constexpr bool is_std_optional_v = is_std_optional<remove_cvref_t<T>>::value;

template<typename T>
struct optional_value_type {
    using type = T;
};

template<typename U>
struct optional_value_type<std::optional<U>> {
    using type = U;
};

template<typename T>
using optional_value_type_t = typename optional_value_type<remove_cvref_t<T>>::type;

template<typename T>
struct can_auto_register_type
    : std::bool_constant<has_json_traits_v<T> || has_nlohmann_json_binding_v<T> || std::is_enum_v<T>> {};

template<>
struct can_auto_register_type<void> : std::false_type {};

template<typename U>
struct can_auto_register_type<std::optional<U>> : std::bool_constant<can_auto_register_type<remove_cvref_t<U>>::value> {};

template<typename T>
inline constexpr bool can_auto_register_type_v = can_auto_register_type<remove_cvref_t<T>>::value;

template<typename T>
remove_cvref_t<T> defaultFromJsonValue(const json& value)
{
    using D = remove_cvref_t<T>;

    if constexpr (is_std_optional_v<D>)
    {
        using ValueType = remove_cvref_t<optional_value_type_t<D>>;
        if (value.is_null())
        {
            return D{};
        }

        return D{defaultFromJsonValue<ValueType>(value)};
    }
    else if constexpr (std::is_enum_v<D>)
    {
        if constexpr (func_registry::has_enum_traits_v<D>)
        {
            if (value.is_string())
            {
                return func_registry::enum_value<D>(value.get_ref<const std::string&>());
            }
        }

        using Underlying = std::underlying_type_t<D>;
        return static_cast<D>(value.template get<Underlying>());
    }
    else if constexpr (has_json_traits_v<D>)
    {
        return json_traits<D>::from_json_value(value);
    }
    else
    {
        return value.template get<D>();
    }
}

template<typename T>
json defaultToJsonValue(const T& value)
{
    using D = remove_cvref_t<T>;

    if constexpr (is_std_optional_v<D>)
    {
        if (!value.has_value())
        {
            return nullptr;
        }

        return defaultToJsonValue<remove_cvref_t<optional_value_type_t<D>>>(*value);
    }
    else if constexpr (std::is_enum_v<D>)
    {
        if constexpr (func_registry::has_enum_traits_v<D>)
        {
            return json(func_registry::enum_name(value));
        }

        using Underlying = std::underlying_type_t<D>;
        return json(static_cast<Underlying>(value));
    }
    else if constexpr (has_json_traits_v<D>)
    {
        return json_traits<D>::to_json_value(value);
    }
    else
    {
        return json(value);
    }
}

template<typename T>
struct registered_argument_type {
    using type = std::decay_t<T>;
};

template<typename T>
struct registered_argument_type<T&> {
    using type = std::conditional_t<std::is_const_v<T>, std::decay_t<T>, std::reference_wrapper<T>>;
};

template<typename T>
struct registered_argument_type<const T&> {
    using type = std::decay_t<T>;
};

template<typename T>
using registered_argument_type_t = typename registered_argument_type<T>::type;

template<typename T>
using registered_return_type_t = std::remove_cv_t<std::remove_reference_t<T>>;

template<typename T>
void register_type_hook(void* registry);

using TypeRegistrationHook = void(*)(void*);

template<typename T>
constexpr TypeRegistrationHook make_argument_registration_hook()
{
    using RegisteredType = registered_argument_type_t<T>;
    if constexpr (!std::is_void_v<RegisteredType> && can_auto_register_type_v<RegisteredType>)
    {
        return &register_type_hook<RegisteredType>;
    }

    return nullptr;
}

template<typename T>
constexpr TypeRegistrationHook make_return_registration_hook()
{
    using RegisteredType = registered_return_type_t<T>;
    if constexpr (!std::is_void_v<RegisteredType> && can_auto_register_type_v<RegisteredType>)
    {
        return &register_type_hook<RegisteredType>;
    }

    return nullptr;
}

template<typename Fn>
using callable_traits_t = std::conditional_t<
    std::is_member_function_pointer_v<std::decay_t<Fn>>,
    func_registry::member_function_traits<std::decay_t<Fn>>,
    func_registry::function_traits<std::decay_t<Fn>>>;

template<typename T>
struct is_annotated_tool : std::false_type {};

template<typename Fn>
struct is_annotated_tool<AnnotatedTool<Fn>> : std::true_type {};

template<typename T>
inline constexpr bool is_annotated_tool_v = is_annotated_tool<remove_cvref_t<T>>::value;

template<typename T, bool IsAnnotated = is_annotated_tool_v<T>>
struct unwrapped_callable;

template<typename T>
struct unwrapped_callable<T, false> {
    using type = remove_cvref_t<T>;
};

template<typename T>
struct unwrapped_callable<T, true> {
    using type = typename remove_cvref_t<T>::callable_type;
};

template<typename T>
using unwrapped_callable_t = typename unwrapped_callable<T>::type;

template<typename T, bool IsAnnotated = is_annotated_tool_v<T>>
struct unwrap_callable_value;

template<typename T>
struct unwrap_callable_value<T, false> {
    static decltype(auto) apply(T&& value)
    {
        return std::forward<T>(value);
    }
};

template<typename T>
struct unwrap_callable_value<T, true> {
    static decltype(auto) apply(T&& value)
    {
        return std::forward<T>(value).fn;
    }
};

template<typename T>
decltype(auto) unwrapCallable(T&& value)
{
    return unwrap_callable_value<T>::apply(std::forward<T>(value));
}

template<typename T>
std::optional<ToolExecutionSemantics> annotatedExecutionSemantics(const T& value)
{
    if constexpr (is_annotated_tool_v<T>)
    {
        return value.semantics;
    }

    return std::nullopt;
}

inline void ensureSuccessfulResponse(const json& response)
{
    auto ok_it = response.find("ok");
    if (ok_it != response.end() && ok_it->is_boolean() && ok_it->get<bool>())
    {
        return;
    }

    auto error_it = response.find("error");
    if (error_it != response.end() && error_it->is_object())
    {
        const std::string code = error_it->value("code", "unknown_error");
        const std::string message = error_it->value("message", "request failed");
        throw JsonInvokeError(code, message);
    }

    throw JsonInvokeError("invalid_response", "response is missing a successful result state");
}

inline const json& responseValue(const json& response)
{
    ensureSuccessfulResponse(response);

    auto value_it = response.find("value");
    if (value_it == response.end())
    {
        throw JsonInvokeError("invalid_response", "successful response does not contain a 'value' field");
    }

    return *value_it;
}

} // namespace detail

class JsonInvokeResult {
public:
    explicit JsonInvokeResult(json response)
        : response_(std::move(response))
    {
    }

    const json& response() const noexcept
    {
        return response_;
    }

    std::string dump(
        int indent = -1,
        char indent_char = ' ',
        bool ensure_ascii = false,
        json::error_handler_t error_handler = json::error_handler_t::strict) const
    {
        return response_.dump(indent, indent_char, ensure_ascii, error_handler);
    }

    template<typename T>
    T as() const
    {
        try
        {
            if constexpr (std::is_same_v<std::remove_cvref_t<T>, json>)
            {
                return detail::responseValue(response_);
            }

            return detail::defaultFromJsonValue<T>(detail::responseValue(response_));
        }
        catch (const JsonInvokeError&)
        {
            throw;
        }
        catch (const std::exception& e)
        {
            throw JsonInvokeError("conversion_failed", "failed to convert response value: " + std::string(e.what()));
        }
    }

    template<typename T>
    operator T() const
    {
        return as<T>();
    }

    operator json() const
    {
        return as<json>();
    }

private:
    json response_;
};

} // namespace json_invoke

namespace nlohmann {

template<>
struct adl_serializer<json_invoke::JsonInvokeResult> {
    static void to_json(json& value, const json_invoke::JsonInvokeResult& result)
    {
        value = result.as<json_invoke::json>();
    }
};

} // namespace nlohmann

namespace json_invoke {

class JsonTypeRegistry {
public:
    JsonTypeRegistry() = default;

    template<typename T>
    void registerType()
    {
        if constexpr (!std::is_void_v<T>)
        {
            registerType<T>(
                [](const json& value) -> T {
                    return detail::defaultFromJsonValue<T>(value);
                },
                [](const T& value) -> json {
                    return detail::defaultToJsonValue<T>(value);
                });
        }
    }

    template<typename T, typename FromJson, typename ToJson>
    void registerType(FromJson&& from_json, ToJson&& to_json)
    {
        if constexpr (!std::is_void_v<T>)
        {
            std::unique_lock<std::shared_mutex> lock(mutex_);
            from_json_[typeid(T)] = [converter = std::forward<FromJson>(from_json)](const json& value) -> std::any {
                return std::any(converter(value));
            };

            to_json_[typeid(T)] = [converter = std::forward<ToJson>(to_json)](const std::any& value) -> json {
                return converter(std::any_cast<const T&>(value));
            };
        }
    }

    std::any fromJson(const json& value, std::type_index expected_type) const
    {
        if (expected_type == typeid(void))
        {
            if (!value.is_null())
            {
                throw JsonInvokeError("conversion_failed", "void type expects null JSON");
            }
            return std::any{};
        }

        std::shared_lock<std::shared_mutex> lock(mutex_);
        const auto it = from_json_.find(expected_type);
        if (it == from_json_.end())
        {
            throw JsonInvokeError(
                "unsupported_type",
                "JSON input conversion is not registered for C++ type '" + std::string(expected_type.name()) + "'");
        }

        try
        {
            return it->second(value);
        }
        catch (const JsonInvokeError&)
        {
            throw;
        }
        catch (const std::exception& e)
        {
            throw JsonInvokeError(
                "conversion_failed",
                "JSON input conversion failed for C++ type '" + std::string(expected_type.name()) + "': " + e.what());
        }
    }

    json toJson(const std::any& value, std::type_index actual_type) const
    {
        if (actual_type == typeid(void) || !value.has_value())
        {
            return nullptr;
        }

        std::shared_lock<std::shared_mutex> lock(mutex_);
        const auto it = to_json_.find(actual_type);
        if (it == to_json_.end())
        {
            throw JsonInvokeError(
                "unsupported_type",
                "JSON output conversion is not registered for C++ type '" + std::string(actual_type.name()) + "'");
        }

        try
        {
            return it->second(value);
        }
        catch (const JsonInvokeError&)
        {
            throw;
        }
        catch (const std::exception& e)
        {
            throw JsonInvokeError(
                "conversion_failed",
                "JSON output conversion failed for C++ type '" + std::string(actual_type.name()) + "': " + e.what());
        }
    }

    bool canRead(std::type_index expected_type) const
    {
        if (expected_type == typeid(void))
        {
            return true;
        }

        std::shared_lock<std::shared_mutex> lock(mutex_);
        return from_json_.find(expected_type) != from_json_.end();
    }

    bool canWrite(std::type_index actual_type) const
    {
        if (actual_type == typeid(void))
        {
            return true;
        }

        std::shared_lock<std::shared_mutex> lock(mutex_);
        return to_json_.find(actual_type) != to_json_.end();
    }

private:
    using FromJsonFn = std::function<std::any(const json&)>;
    using ToJsonFn = std::function<json(const std::any&)>;

    mutable std::shared_mutex mutex_;
    std::unordered_map<std::type_index, FromJsonFn> from_json_;
    std::unordered_map<std::type_index, ToJsonFn> to_json_;
};

namespace detail {

template<typename T>
void register_type_hook(void* registry)
{
    static_cast<JsonTypeRegistry*>(registry)->template registerType<T>();
}

} // namespace detail

template<bool EnableThreadSafety = false>
class BasicJsonInvokeAdapter {
public:
    using MapType = BasicFuncRegistry<EnableThreadSafety>;
    using MutexType = std::conditional_t<EnableThreadSafety, std::shared_mutex, func_registry::NullSharedMutex>;

    struct AutoRegistrationHooks {
        detail::TypeRegistrationHook ret_type_hook{nullptr};
        std::vector<detail::TypeRegistrationHook> arg_type_hooks;
    };

    BasicJsonInvokeAdapter()
        : func_registry_(owned_func_registry_)
    {
    }

    explicit BasicJsonInvokeAdapter(MapType& func_registry)
        : func_registry_(func_registry)
    {
    }

    template<typename T>
    void registerType()
    {
        json_type_registry_.template registerType<T>();
    }

    template<typename T, typename FromJson, typename ToJson>
    void registerType(FromJson&& from_json, ToJson&& to_json)
    {
        json_type_registry_.template registerType<T>(std::forward<FromJson>(from_json), std::forward<ToJson>(to_json));
    }

    void setTraceSink(TraceSink trace_sink)
    {
        trace_dispatcher_->setSink(std::move(trace_sink));
    }

    std::shared_ptr<TraceDispatcher> sharedTraceDispatcher() const noexcept
    {
        return trace_dispatcher_;
    }

    bool hasRegisteredFunction(const std::string& name) const noexcept
    {
        try
        {
            static_cast<void>(func_registry_.getFunction(name));
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool canConvertFromJson(std::type_index expected_type) const
    {
        return json_type_registry_.canRead(expected_type);
    }

    std::any convertFromJson(const json& value, std::type_index expected_type) const
    {
        return json_type_registry_.fromJson(value, expected_type);
    }

    template<typename Fn>
    requires (!std::is_same_v<std::remove_cvref_t<Fn>, AnyCallable>)
    void registerFunction(const std::string& name, Fn&& fn)
    {
        using Callable = detail::unwrapped_callable_t<Fn>;
        const auto semantics = detail::annotatedExecutionSemantics(fn);
        AnyCallable callable = func_registry::makeCallable(detail::unwrapCallable(std::forward<Fn>(fn)));
        func_registry::registerCallableTypeIntrospection<Callable>();
        autoRegisterTypes(makeCallableTypeHooks<Callable>());
        func_registry_.registerFunction(name, std::move(callable));
        annotateToolExecutionSemantics(name, semantics);
    }

    template<typename Fn>
    requires (!std::is_same_v<std::remove_cvref_t<Fn>, AnyCallable>)
    void registerFunction(const std::string& name, Fn&& fn, FunctionMetadata metadata)
    {
        using Callable = detail::unwrapped_callable_t<Fn>;
        const auto semantics = detail::annotatedExecutionSemantics(fn);
        AnyCallable callable = func_registry::makeCallable(detail::unwrapCallable(std::forward<Fn>(fn)));
        func_registry::registerCallableTypeIntrospection<Callable>();
        autoRegisterTypes(makeCallableTypeHooks<Callable>());
        func_registry_.registerFunction(name, std::move(callable), std::move(metadata));
        annotateToolExecutionSemantics(name, semantics);
    }

    template<typename Fn>
    requires (!std::is_same_v<std::remove_cvref_t<Fn>, AnyCallable>)
    void registerFunction(const std::string& name, Fn&& fn, std::string description)
    {
        using Callable = detail::unwrapped_callable_t<Fn>;
        const auto semantics = detail::annotatedExecutionSemantics(fn);
        AnyCallable callable = func_registry::makeCallable(detail::unwrapCallable(std::forward<Fn>(fn)));
        func_registry::registerCallableTypeIntrospection<Callable>();
        autoRegisterTypes(makeCallableTypeHooks<Callable>());
        func_registry_.registerFunction(name, std::move(callable), std::move(description));
        annotateToolExecutionSemantics(name, semantics);
    }

    template<typename R, typename... Args, typename Fn>
    void registerFunctionAs(const std::string& name, Fn&& fn)
    {
        const auto semantics = detail::annotatedExecutionSemantics(fn);
        AnyCallable callable = func_registry::makeCallableAs<R(Args...)>(detail::unwrapCallable(std::forward<Fn>(fn)));
        func_registry::registerSignatureTypeIntrospection<R(Args...)>();
        autoRegisterTypes(makeSignatureTypeHooks<R(Args...)>());
        func_registry_.registerFunction(name, std::move(callable));
        annotateToolExecutionSemantics(name, semantics);
    }

    template<typename R, typename... Args, typename Fn>
    void registerFunctionAs(const std::string& name, Fn&& fn, FunctionMetadata metadata)
    {
        const auto semantics = detail::annotatedExecutionSemantics(fn);
        AnyCallable callable = func_registry::makeCallableAs<R(Args...)>(detail::unwrapCallable(std::forward<Fn>(fn)));
        func_registry::registerSignatureTypeIntrospection<R(Args...)>();
        autoRegisterTypes(makeSignatureTypeHooks<R(Args...)>());
        func_registry_.registerFunction(name, std::move(callable), std::move(metadata));
        annotateToolExecutionSemantics(name, semantics);
    }

    template<typename R, typename... Args, typename Fn>
    void registerFunctionAs(const std::string& name, Fn&& fn, std::string description)
    {
        const auto semantics = detail::annotatedExecutionSemantics(fn);
        AnyCallable callable = func_registry::makeCallableAs<R(Args...)>(detail::unwrapCallable(std::forward<Fn>(fn)));
        func_registry::registerSignatureTypeIntrospection<R(Args...)>();
        autoRegisterTypes(makeSignatureTypeHooks<R(Args...)>());
        func_registry_.registerFunction(name, std::move(callable), std::move(description));
        annotateToolExecutionSemantics(name, semantics);
    }

    json invokeJson(const json& request) const
    {
        std::string name;
        [[maybe_unused]] auto trace_context = traceDispatcher().beginRequest();

        try
        {
            const json& request_object = requireRequestObject(request);
            name = extractFunctionName(request_object);
            emitTraceEventIfEnabled(TraceEventKind::invoke_started, name, [&request_object]() {
                return json{{"request", request_object}};
            });
            FunctionInfo info = getFunctionInfo(name);
            ensureJsonCallable(info);
            std::vector<std::any> packed_args = packArgsFromJson(info, extractArgsNode(request_object));
            FuncCallResult result = call(name, packed_args);

            json response{
                {"ok", true},
                {"name", name},
                {"return_cpp_type_name", info.ret_type_name},
                {"return_llm_type", func_registry::getTypeIntrospectionOrFallback(info.ret_type).llm_type},
                {"value", json_type_registry_.toJson(result.any(), result.declaredReturnType())},
            };

            emitTraceEventIfEnabled(TraceEventKind::invoke_finished, name, [&response]() {
                return json{{"response", response}};
            });

            return response;
        }
        catch (const JsonInvokeError& e)
        {
            emitTraceEventIfEnabled(TraceEventKind::invoke_failed, name, [&e]() {
                return json{{"error", {{"code", e.code()}, {"message", e.what()}}}};
            });
            return makeErrorResponse(name, e.code(), e.what());
        }
        catch (const std::exception& e)
        {
            emitTraceEventIfEnabled(TraceEventKind::invoke_failed, name, [&e]() {
                return json{{"error", {{"code", "unknown_error"}, {"message", e.what()}}}};
            });
            return makeErrorResponse(name, "unknown_error", e.what());
        }
    }

    JsonInvokeResult invoke(const json& request) const
    {
        return JsonInvokeResult(invokeJson(request));
    }

    template<typename T>
    T invoke(const json& request) const
    {
        return invoke(request).template as<T>();
    }

    std::string invokeText(std::string_view request_text, int indent = 2) const
    {
        try
        {
            json request = json::parse(request_text.begin(), request_text.end());
            return invokeJson(request).dump(indent);
        }
        catch (const json::parse_error& e)
        {
            return makeErrorResponse("", "invalid_json", e.what()).dump(indent);
        }
        catch (const JsonInvokeError& e)
        {
            return makeErrorResponse("", e.code(), e.what()).dump(indent);
        }
        catch (const std::exception& e)
        {
            return makeErrorResponse("", "unknown_error", e.what()).dump(indent);
        }
    }

    json getAllToolSummariesJson() const
    {
        json tools = json_invoke::getAllToolSummariesJson(func_registry_);
        for (auto& tool : tools)
        {
            const std::string tool_name = tool.value("name", "");
            tool = applyToolExecutionSemanticsToToolJson(tool_name, std::move(tool));
        }
        return tools;
    }

    json getToolSchemaJson(const std::string& name) const
    {
        return applyToolExecutionSemanticsToSchemaJson(name, json_invoke::getToolSchemaJson(func_registry_, name));
    }

    json getAllToolSchemasJson() const
    {
        json tools = json_invoke::getAllToolSchemasJson(func_registry_);
        for (auto& tool : tools)
        {
            const std::string tool_name = tool.at("function").value("name", "");
            tool = applyToolExecutionSemanticsToSchemaJson(tool_name, std::move(tool));
        }
        return tools;
    }

private:
    TraceDispatcher& traceDispatcher() noexcept
    {
        return *trace_dispatcher_;
    }

    const TraceDispatcher& traceDispatcher() const noexcept
    {
        return *trace_dispatcher_;
    }

    template<typename PayloadFactory>
    void emitTraceEventIfEnabled(TraceEventKind kind, const std::string& name, PayloadFactory&& payload_factory) const
    {
        traceDispatcher().emitLazy(kind, name, std::forward<PayloadFactory>(payload_factory));
    }

    void emitTraceEvent(TraceEventKind kind, const std::string& name, json payload) const
    {
        traceDispatcher().emit(kind, name, std::move(payload));
    }

    void annotateToolExecutionSemantics(const std::string& name, std::optional<ToolExecutionSemantics> semantics)
    {
        if (semantics.has_value())
        {
            std::unique_lock<MutexType> lock(state_mutex_);
            tool_execution_semantics_[name] = *semantics;
        }
    }

    json applyToolExecutionSemanticsToToolJson(const std::string& name, json tool) const
    {
        std::shared_lock<MutexType> lock(state_mutex_);
        const auto it = tool_execution_semantics_.find(name);
        if (it != tool_execution_semantics_.end())
        {
            tool["x-execution-semantics"] = toolExecutionSemanticsName(it->second);
        }

        return tool;
    }

    json applyToolExecutionSemanticsToSchemaJson(const std::string& name, json schema) const
    {
        std::shared_lock<MutexType> lock(state_mutex_);
        const auto it = tool_execution_semantics_.find(name);
        if (it != tool_execution_semantics_.end())
        {
            schema["function"]["x-execution-semantics"] = toolExecutionSemanticsName(it->second);
        }

        return schema;
    }

    template<typename Traits, std::size_t... I>
    static AutoRegistrationHooks makeTypeHooks(std::index_sequence<I...>)
    {
        AutoRegistrationHooks hooks;
        hooks.ret_type_hook = detail::make_return_registration_hook<typename Traits::return_type>();
        hooks.arg_type_hooks = {detail::make_argument_registration_hook<typename Traits::template arg<I>>()...};
        return hooks;
    }

    template<typename Fn>
    static AutoRegistrationHooks makeCallableTypeHooks()
    {
        using Traits = detail::callable_traits_t<Fn>;
        return makeTypeHooks<Traits>(std::make_index_sequence<Traits::arity>{});
    }

    template<typename Signature>
    static AutoRegistrationHooks makeSignatureTypeHooks()
    {
        using Traits = func_registry::function_traits<Signature>;
        return makeTypeHooks<Traits>(std::make_index_sequence<Traits::arity>{});
    }

    // autoRegisterTypes process:
    // AutoRegistrationHooks collects registration hooks (if available) for all parameter types and return value types. These hooks are automatically generated based on function signatures when registering functions.
    // For each parameter type of the function, if the corresponding JSON converter is not registered in json_type_registry_, invoke the corresponding registration hook to register this type.
    // For the return value type of the function, if the corresponding JSON converter is not registered in json_type_registry_, invoke the corresponding registration hook to register this type.
    // Hooks are essentially instantiations of function templates that can automatically register JSON converters based on types, namely void register_type_hook(void* registry);
    // register_type_hook is a function template. After being instantiated for different types T, it calls the registerType<T>() method of JsonTypeRegistry to register the JSON converter.
    // In turn, registerType implements conversion from JSON to C++ types via defaultFromJsonValue, which further uses from_json_value (for custom types) or json's get<T>() (for basic types/standard library types).
    // It implements conversion from C++ types to JSON via defaultToJsonValue, which further uses to_json_value (for custom types) or the json constructor (json(value)).
    void autoRegisterTypes(const AutoRegistrationHooks& hooks)
    {
        for (std::size_t index = 0; index < hooks.arg_type_hooks.size(); ++index)
        {
            if (hooks.arg_type_hooks[index] == nullptr)
            {
                continue;
            }

            hooks.arg_type_hooks[index](&json_type_registry_);
        }

        if (hooks.ret_type_hook != nullptr)
        {
            hooks.ret_type_hook(&json_type_registry_);
        }
    }

    void autoRegisterTypes(const std::string& name, const AutoRegistrationHooks& hooks)
    {
        FunctionInfo info = getFunctionInfo(name);

        for (std::size_t index = 0; index < info.arg_types.size(); ++index)
        {
            if (json_type_registry_.canRead(info.arg_types[index]))
            {
                continue;
            }

            if (index < hooks.arg_type_hooks.size() && hooks.arg_type_hooks[index] != nullptr)
            {
                hooks.arg_type_hooks[index](&json_type_registry_);
            }
        }

        if (!json_type_registry_.canWrite(info.ret_type) && hooks.ret_type_hook != nullptr)
        {
            hooks.ret_type_hook(&json_type_registry_);
        }
    }

    static const json& requireRequestObject(const json& request)
    {
        if (!request.is_object())
        {
            throw JsonInvokeError("invalid_request", "request must be a JSON object");
        }
        return request;
    }

    static std::string extractFunctionName(const json& request)
    {
        static constexpr std::string_view name_fields[] = {"name", "tool_name", "function_name"};

        for (std::string_view field : name_fields)
        {
            auto it = request.find(std::string(field));
            if (it == request.end())
            {
                continue;
            }

            if (!it->is_string())
            {
                throw JsonInvokeError("invalid_request", "field '" + std::string(field) + "' must be a string");
            }

            return it->get<std::string>();
        }

        auto function_it = request.find("function");
        if (function_it != request.end())
        {
            if (!function_it->is_object())
            {
                throw JsonInvokeError("invalid_request", "field 'function' must be an object");
            }

            auto name_it = function_it->find("name");
            if (name_it == function_it->end())
            {
                throw JsonInvokeError("invalid_request", "field 'function.name' is required");
            }

            if (!name_it->is_string())
            {
                throw JsonInvokeError("invalid_request", "field 'function.name' must be a string");
            }

            return name_it->get<std::string>();
        }

        throw JsonInvokeError("invalid_request", "request must contain a string field 'name'");
    }

    static json parseArgumentsString(std::string_view raw)
    {
        try
        {
            return json::parse(raw.begin(), raw.end());
        }
        catch (const json::parse_error& e)
        {
            throw JsonInvokeError("invalid_request", "tool arguments string is not valid JSON: " + std::string(e.what()));
        }
    }

    static json normalizeArgsNode(const json& value)
    {
        if (value.is_string())
        {
            return parseArgumentsString(value.get_ref<const std::string&>());
        }

        return value;
    }

    static json extractArgsNode(const json& request)
    {
        auto args_it = request.find("args");
        if (args_it != request.end())
        {
            return normalizeArgsNode(*args_it);
        }

        auto arguments_it = request.find("arguments");
        if (arguments_it != request.end())
        {
            return normalizeArgsNode(*arguments_it);
        }

        auto function_it = request.find("function");
        if (function_it != request.end())
        {
            if (!function_it->is_object())
            {
                throw JsonInvokeError("invalid_request", "field 'function' must be an object");
            }

            auto function_args_it = function_it->find("arguments");
            if (function_args_it != function_it->end())
            {
                return normalizeArgsNode(*function_args_it);
            }

            auto function_plain_args_it = function_it->find("args");
            if (function_plain_args_it != function_it->end())
            {
                return normalizeArgsNode(*function_plain_args_it);
            }
        }

        return json::array();
    }

    FunctionInfo getFunctionInfo(const std::string& name) const
    {
        try
        {
            return func_registry::getFunctionInfo(func_registry_, name);
        }
        catch (const std::exception& e)
        {
            throw JsonInvokeError("function_not_found", e.what());
        }
    }

    FuncCallResult call(const std::string& name, const std::vector<std::any>& args) const
    {
        try
        {
            AnyCallable callable = func_registry_.getFunction(name);
            std::any result = callable.fn(args);
            return FuncCallResult(std::move(result), callable.ret_type);
        }
        catch (const JsonInvokeError&)
        {
            throw;
        }
        catch (const std::exception& e)
        {
            throw JsonInvokeError("call_failed", e.what());
        }
    }

    void ensureJsonCallable(const FunctionInfo& info) const
    {
        for (std::size_t index = 0; index < info.arg_types.size(); ++index)
        {
            if (!json_type_registry_.canRead(info.arg_types[index]))
            {
                throw JsonInvokeError(
                    "unsupported_type",
                    "argument " + std::to_string(index) + " ('" + argumentLabel(info, index) + "') uses unsupported C++ type '" +
                    std::string(info.arg_types[index].name()) + "' for JSON input conversion");
            }
        }

        if (!json_type_registry_.canWrite(info.ret_type))
        {
            throw JsonInvokeError(
                "unsupported_type",
                "return type '" + std::string(info.ret_type.name()) + "' is not registered for JSON output conversion");
        }
    }

    std::vector<std::any> packArgsFromJson(const FunctionInfo& info, const json& args) const
    {
        if (args.is_array())
        {
            return packPositionalArgs(info, args);
        }

        if (args.is_object())
        {
            return packNamedArgs(info, args);
        }

        if (args.is_null() && info.arg_types.empty())
        {
            return {};
        }

        throw JsonInvokeError("invalid_request", "field 'args' must be an array or object");
    }

    std::vector<std::any> packPositionalArgs(const FunctionInfo& info, const json& args) const
    {
        if (args.size() > info.arg_types.size())
        {
            throw JsonInvokeError(
                "invalid_request",
                "argument count mismatch: expected " + std::to_string(info.arg_types.size()) +
                ", got " + std::to_string(args.size()));
        }

        for (std::size_t index = args.size(); index < info.arg_types.size(); ++index)
        {
            if (isArgumentRequired(info, index))
            {
                throw JsonInvokeError(
                    "invalid_request",
                    "missing JSON argument for parameter '" + argumentLabel(info, index) + "'");
            }
        }

        std::vector<std::any> packed_args;
        packed_args.reserve(info.arg_types.size());
        for (std::size_t index = 0; index < args.size(); ++index)
        {
            packed_args.push_back(convertArgument(args[index], info.arg_types[index], index, argumentLabel(info, index)));
        }

        for (std::size_t index = args.size(); index < info.arg_types.size(); ++index)
        {
            packed_args.push_back(convertArgument(json(nullptr), info.arg_types[index], index, argumentLabel(info, index)));
        }

        return packed_args;
    }

    std::vector<std::any> packNamedArgs(const FunctionInfo& info, const json& args) const
    {
        if (!args.is_object())
        {
            throw JsonInvokeError("invalid_request", "named JSON arguments must be a JSON object");
        }

        for (auto it = args.begin(); it != args.end(); ++it)
        {
            bool known = false;
            for (std::size_t index = 0; index < info.arg_types.size(); ++index)
            {
                if (it.key() == argumentLabel(info, index))
                {
                    known = true;
                    break;
                }
            }

            if (!known)
            {
                throw JsonInvokeError("invalid_request", "unexpected JSON argument '" + it.key() + "'");
            }
        }

        std::vector<std::any> packed_args;
        packed_args.reserve(info.arg_types.size());
        for (std::size_t index = 0; index < info.arg_types.size(); ++index)
        {
            const std::string parameter_name = argumentLabel(info, index);

            auto it = args.find(parameter_name);
            if (it == args.end())
            {
                if (isArgumentRequired(info, index))
                {
                    throw JsonInvokeError(
                        "invalid_request",
                        "missing JSON argument for parameter '" + parameter_name + "'");
                }

                packed_args.push_back(convertArgument(json(nullptr), info.arg_types[index], index, parameter_name));
                continue;
            }

            packed_args.push_back(convertArgument(*it, info.arg_types[index], index, parameter_name));
        }

        return packed_args;
    }

    std::any convertArgument(
        const json& value,
        std::type_index expected_type,
        std::size_t index,
        const std::string& label) const
    {
        try
        {
            return json_type_registry_.fromJson(value, expected_type);
        }
        catch (const JsonInvokeError& e)
        {
            throw JsonInvokeError(e.code(), "argument " + std::to_string(index) + " ('" + label + "'): " + e.what());
        }
    }

    static std::string argumentLabel(const FunctionInfo& info, std::size_t index)
    {
        if (index < info.param_names.size() && !info.param_names[index].empty())
        {
            return info.param_names[index];
        }
        return "arg" + std::to_string(index);
    }

    static bool isArgumentRequired(const FunctionInfo& info, std::size_t index)
    {
        if (index >= info.arg_types.size())
        {
            return true;
        }

        const auto type_info = func_registry::getTypeIntrospection(info.arg_types[index]);
        return !(type_info && type_info->optional);
    }

    static json makeErrorResponse(std::string_view name, std::string_view code, std::string_view message)
    {
        json response{
            {"ok", false},
            {"error", {
                {"code", code},
                {"message", message},
            }},
        };

        if (!name.empty())
        {
            response["name"] = name;
        }

        return response;
    }

    MapType owned_func_registry_{};
    MapType& func_registry_;
    JsonTypeRegistry json_type_registry_;
    mutable MutexType state_mutex_;
    std::unordered_map<std::string, ToolExecutionSemantics> tool_execution_semantics_;
    std::shared_ptr<TraceDispatcher> trace_dispatcher_{std::make_shared<TraceDispatcher>()};
};

using JsonInvokeAdapterThreadSafe = BasicJsonInvokeAdapter<true>;
using JsonInvokeAdapterUnsafe = BasicJsonInvokeAdapter<false>;

} // namespace json_invoke
