#pragma once

#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

#include <json_invoke/json_invoke.hpp>
#include <json_session_invoke/detail/stateful_tool_runtime.hpp>
#include <json_session_invoke/session_objects.hpp>

namespace json_session_invoke {

using json_invoke::JsonInvokeError;
using json_invoke::JsonInvokeResult;
using json_invoke::json;

template<bool EnableThreadSafety = false>
class BasicJsonSessionInvokeAdapter {
public:
    using UnderlyingAdapter = json_invoke::BasicJsonInvokeAdapter<EnableThreadSafety>;
    using MapType = typename UnderlyingAdapter::MapType;

    enum class ToolStatefulKind {
        none,
        factory,
        method,
        destroy,
    };

    enum class ToolSchedulingScope {
        infer_default,
        free_read_only,
        object_lane,
        factory_lane,
        tool_exclusive,
        session_barrier,
    };

    struct ToolMetadataView {
        std::string tool_name;
        json_invoke::ToolExecutionSemantics execution_semantics{json_invoke::ToolExecutionSemantics::unknown};
        ToolSchedulingScope scheduling_scope{ToolSchedulingScope::infer_default};
        ToolStatefulKind stateful_kind{ToolStatefulKind::none};
        std::string object_type_name;
        std::string handle_parameter_name;
    };

    struct StatefulDefaults {
        bool auto_register_destroy{true};
        std::string destroy_description{defaultDestroyDescriptionText()};
    };

    template<typename T>
    class StatefulRegistrationBuilder {
    public:
        StatefulRegistrationBuilder(
            BasicJsonSessionInvokeAdapter& adapter,
            std::string configured_object_type_name,
            ObjectOptions options)
            : adapter_(adapter)
            , configured_object_type_name_(std::move(configured_object_type_name))
            , options_(std::move(options))
        {
        }

        StatefulRegistrationBuilder(const StatefulRegistrationBuilder&) = delete;
        StatefulRegistrationBuilder& operator=(const StatefulRegistrationBuilder&) = delete;

        StatefulRegistrationBuilder(StatefulRegistrationBuilder&& other) noexcept
            : adapter_(other.adapter_)
            , configured_object_type_name_(std::move(other.configured_object_type_name_))
            , options_(std::move(other.options_))
            , created_(other.created_)
            , destroy_registered_(other.destroy_registered_)
            , active_(other.active_)
        {
            other.active_ = false;
        }

        StatefulRegistrationBuilder& operator=(StatefulRegistrationBuilder&& other) noexcept
        {
            if (this == &other)
            {
                return *this;
            }

            configured_object_type_name_ = std::move(other.configured_object_type_name_);
            options_ = std::move(other.options_);
            created_ = other.created_;
            destroy_registered_ = other.destroy_registered_;
            active_ = other.active_;
            other.active_ = false;
            return *this;
        }

        ~StatefulRegistrationBuilder() noexcept
        {
            ensureDefaultDestroyRegistered();
        }

        template<typename Fn>
        StatefulRegistrationBuilder& create(const std::string& factory_tool_name, Fn&& fn)
        {
            return create(factory_tool_name, std::forward<Fn>(fn), ToolSchedulingScope::factory_lane);
        }

        template<typename Fn>
        StatefulRegistrationBuilder& create(
            const std::string& factory_tool_name,
            Fn&& fn,
            ToolSchedulingScope scheduling_scope)
        {
            return registerFactoryWithMetadata(
                factory_tool_name,
                std::forward<Fn>(fn),
                func_registry::FunctionMetadata{},
                scheduling_scope);
        }

        template<typename Fn>
        StatefulRegistrationBuilder& create(
            const std::string& factory_tool_name,
            Fn&& fn,
            func_registry::FunctionMetadata metadata)
        {
            return create(factory_tool_name, std::forward<Fn>(fn), std::move(metadata), ToolSchedulingScope::factory_lane);
        }

        template<typename Fn>
        StatefulRegistrationBuilder& create(
            const std::string& factory_tool_name,
            Fn&& fn,
            func_registry::FunctionMetadata metadata,
            ToolSchedulingScope scheduling_scope)
        {
            return registerFactoryWithMetadata(
                factory_tool_name,
                std::forward<Fn>(fn),
                std::move(metadata),
                scheduling_scope);
        }

        template<typename Fn>
        StatefulRegistrationBuilder& create(const std::string& factory_tool_name, Fn&& fn, std::string description)
        {
            return create(factory_tool_name, std::forward<Fn>(fn), std::move(description), ToolSchedulingScope::factory_lane);
        }

        template<typename Fn>
        StatefulRegistrationBuilder& create(
            const std::string& factory_tool_name,
            Fn&& fn,
            std::string description,
            ToolSchedulingScope scheduling_scope)
        {
            return registerFactoryWithMetadata(
                factory_tool_name,
                std::forward<Fn>(fn),
                func_registry::FunctionMetadata{{}, std::move(description)},
                scheduling_scope);
        }

        template<typename Fn>
        StatefulRegistrationBuilder& create(Fn&& fn)
        {
            return create(defaultFactoryToolName(), std::forward<Fn>(fn), ToolSchedulingScope::factory_lane);
        }

        template<typename Fn>
        StatefulRegistrationBuilder& create(Fn&& fn, ToolSchedulingScope scheduling_scope)
        {
            return create(defaultFactoryToolName(), std::forward<Fn>(fn), scheduling_scope);
        }

        template<typename Fn>
        StatefulRegistrationBuilder& create(Fn&& fn, func_registry::FunctionMetadata metadata)
        {
            return create(
                defaultFactoryToolName(),
                std::forward<Fn>(fn),
                std::move(metadata),
                ToolSchedulingScope::factory_lane);
        }

        template<typename Fn>
        StatefulRegistrationBuilder& create(
            Fn&& fn,
            func_registry::FunctionMetadata metadata,
            ToolSchedulingScope scheduling_scope)
        {
            return create(defaultFactoryToolName(), std::forward<Fn>(fn), std::move(metadata), scheduling_scope);
        }

        template<typename Fn>
        StatefulRegistrationBuilder& create(Fn&& fn, std::string description)
        {
            return create(
                defaultFactoryToolName(),
                std::forward<Fn>(fn),
                std::move(description),
                ToolSchedulingScope::factory_lane);
        }

        template<typename Fn>
        StatefulRegistrationBuilder& create(
            Fn&& fn,
            std::string description,
            ToolSchedulingScope scheduling_scope)
        {
            return create(defaultFactoryToolName(), std::forward<Fn>(fn), std::move(description), scheduling_scope);
        }

        template<typename Fn>
        StatefulRegistrationBuilder& method(const std::string& method_tool_name, Fn&& fn)
        {
            return registerMethodWithMetadata(
                method_tool_name,
                std::forward<Fn>(fn),
                makeDefaultMemberMetadata<Fn>(std::string{}),
                ToolSchedulingScope::object_lane);
        }

        template<typename Fn>
        StatefulRegistrationBuilder& method(
            const std::string& method_tool_name,
            Fn&& fn,
            ToolSchedulingScope scheduling_scope)
        {
            return registerMethodWithMetadata(
                method_tool_name,
                std::forward<Fn>(fn),
                makeDefaultMemberMetadata<Fn>(std::string{}),
                scheduling_scope);
        }

        template<typename Fn>
        StatefulRegistrationBuilder& method(
            const std::string& method_tool_name,
            Fn&& fn,
            func_registry::FunctionMetadata metadata)
        {
            return method(method_tool_name, std::forward<Fn>(fn), std::move(metadata), ToolSchedulingScope::object_lane);
        }

        template<typename Fn>
        StatefulRegistrationBuilder& method(
            const std::string& method_tool_name,
            Fn&& fn,
            func_registry::FunctionMetadata metadata,
            ToolSchedulingScope scheduling_scope)
        {
            return registerMethodWithMetadata(
                method_tool_name,
                std::forward<Fn>(fn),
                std::move(metadata),
                scheduling_scope);
        }

        template<typename Fn>
        StatefulRegistrationBuilder& method(const std::string& method_tool_name, Fn&& fn, std::string description)
        {
            return method(method_tool_name, std::forward<Fn>(fn), std::move(description), ToolSchedulingScope::object_lane);
        }

        template<typename Fn>
        StatefulRegistrationBuilder& method(
            const std::string& method_tool_name,
            Fn&& fn,
            std::string description,
            ToolSchedulingScope scheduling_scope)
        {
            return registerMethodWithMetadata(
                method_tool_name,
                std::forward<Fn>(fn),
                makeDefaultMemberMetadata<Fn>(std::move(description)),
                scheduling_scope);
        }

        StatefulRegistrationBuilder& destroy()
        {
            return destroy(defaultDestroyToolName(), ToolSchedulingScope::object_lane);
        }

        StatefulRegistrationBuilder& destroy(ToolSchedulingScope scheduling_scope)
        {
            return destroy(defaultDestroyToolName(), scheduling_scope);
        }

        StatefulRegistrationBuilder& destroy(const std::string& destroy_tool_name)
        {
            return destroy(destroy_tool_name, ToolSchedulingScope::object_lane);
        }

        StatefulRegistrationBuilder& destroy(
            const std::string& destroy_tool_name,
            ToolSchedulingScope scheduling_scope)
        {
            return registerDestroyWithMetadata(
                destroy_tool_name,
                BasicJsonSessionInvokeAdapter::makeDestroyMetadata(adapter_.defaultDestroyDescription()),
                scheduling_scope);
        }

        StatefulRegistrationBuilder& destroy(
            const std::string& destroy_tool_name,
            func_registry::FunctionMetadata metadata)
        {
            return destroy(
                destroy_tool_name,
                std::move(metadata),
                ToolSchedulingScope::object_lane);
        }

        StatefulRegistrationBuilder& destroy(
            const std::string& destroy_tool_name,
            func_registry::FunctionMetadata metadata,
            ToolSchedulingScope scheduling_scope)
        {
            return registerDestroyWithMetadata(destroy_tool_name, std::move(metadata), scheduling_scope);
        }

        StatefulRegistrationBuilder& destroy(const std::string& destroy_tool_name, std::string description)
        {
            return destroy(
                destroy_tool_name,
                std::move(description),
                ToolSchedulingScope::object_lane);
        }

        StatefulRegistrationBuilder& destroy(
            const std::string& destroy_tool_name,
            std::string description,
            ToolSchedulingScope scheduling_scope)
        {
            return registerDestroyWithMetadata(
                destroy_tool_name,
                BasicJsonSessionInvokeAdapter::makeDestroyMetadata(std::move(description)),
                scheduling_scope);
        }

        StatefulRegistrationBuilder& options(ObjectOptions options)
        {
            options_ = std::move(options);
            return *this;
        }

        const std::string& objectTypeName() const noexcept
        {
            return configured_object_type_name_;
        }

        const ObjectOptions& objectOptions() const noexcept
        {
            return options_;
        }

    private:
        template<typename Fn>
        StatefulRegistrationBuilder& registerFactoryWithMetadata(
            const std::string& factory_tool_name,
            Fn&& fn,
            func_registry::FunctionMetadata metadata,
            ToolSchedulingScope scheduling_scope)
        {
            adapter_.template registerFactory<T>(
                factory_tool_name,
                std::forward<Fn>(fn),
                std::move(metadata),
                configured_object_type_name_,
                options_,
                scheduling_scope);
            created_ = true;
            return *this;
        }

        template<typename Fn>
        StatefulRegistrationBuilder& registerMethodWithMetadata(
            const std::string& method_tool_name,
            Fn&& fn,
            func_registry::FunctionMetadata metadata,
            ToolSchedulingScope scheduling_scope)
        {
            validateMethodType<Fn>();
            adapter_.template registerStatefulMethod<T>(
                method_tool_name,
                std::forward<Fn>(fn),
                std::move(metadata),
                scheduling_scope);
            return *this;
        }

        StatefulRegistrationBuilder& registerDestroyWithMetadata(
            const std::string& destroy_tool_name,
            func_registry::FunctionMetadata metadata,
            ToolSchedulingScope scheduling_scope)
        {
            adapter_.template registerDestroy<T>(destroy_tool_name, std::move(metadata), scheduling_scope);
            destroy_registered_ = true;
            return *this;
        }

        void ensureDefaultDestroyRegistered() noexcept
        {
            if (!active_ || !created_ || destroy_registered_ || !adapter_.stateful_defaults_.auto_register_destroy)
            {
                return;
            }

            destroy_registered_ = true;
            const std::string destroy_tool_name = defaultDestroyToolName();
            if (adapter_.isFunctionRegistered(destroy_tool_name))
            {
                return;
            }

            try
            {
                registerDestroyWithMetadata(
                    destroy_tool_name,
                    BasicJsonSessionInvokeAdapter::makeDestroyMetadata(adapter_.defaultDestroyDescription()),
                    ToolSchedulingScope::object_lane);
            }
            catch (...)
            {
            }
        }

        template<typename Fn>
        static void validateMethodType()
        {
            using Traits = json_session_invoke::detail::callable_traits_t<Fn>;

            static_assert(
                Traits::arity >= 1,
                "stateful<T>().method(...) requires a callable whose first parameter is the stateful object");

            using ObjectArg = typename Traits::template arg<0>;
            using ObjectType = std::remove_cv_t<std::remove_reference_t<ObjectArg>>;
            static_assert(
                std::is_same_v<ObjectType, T>,
                "stateful<T>().method(...) requires a callable whose first parameter belongs to T");
        }

        std::string defaultFactoryToolName() const
        {
            return BasicJsonSessionInvokeAdapter::defaultFactoryToolName(configured_object_type_name_);
        }

        std::string defaultDestroyToolName() const
        {
            return BasicJsonSessionInvokeAdapter::defaultDestroyToolName(configured_object_type_name_);
        }

        BasicJsonSessionInvokeAdapter& adapter_;
        std::string configured_object_type_name_;
        ObjectOptions options_;
        bool created_{false};
        bool destroy_registered_{false};
        bool active_{true};
    };

    template<typename T>
    StatefulRegistrationBuilder<T> stateful(std::string configured_object_type_name, ObjectOptions options = {})
    {
        return StatefulRegistrationBuilder<T>(*this, std::move(configured_object_type_name), std::move(options));
    }

    BasicJsonSessionInvokeAdapter()
    {
        emitUnsafeConstructionWarning();
        runtime_.setTraceDispatcher(invoke_adapter_.sharedTraceDispatcher());
    }

    explicit BasicJsonSessionInvokeAdapter(MapType& func_registry)
        : invoke_adapter_(func_registry)
    {
        emitUnsafeConstructionWarning();
        runtime_.setTraceDispatcher(invoke_adapter_.sharedTraceDispatcher());
    }

    void setStatefulDefaults(StatefulDefaults defaults)
    {
        stateful_defaults_ = std::move(defaults);
    }

    void setTraceSink(json_invoke::TraceSink trace_sink)
    {
        invoke_adapter_.setTraceSink(std::move(trace_sink));
    }

    template<typename T>
    void registerType()
    {
        invoke_adapter_.template registerType<T>();
    }

    template<typename T, typename FromJson, typename ToJson>
    void registerType(FromJson&& from_json, ToJson&& to_json)
    {
        invoke_adapter_.template registerType<T>(std::forward<FromJson>(from_json), std::forward<ToJson>(to_json));
    }

    template<typename Fn>
    void registerFunction(const std::string& tool_name, Fn&& fn)
    {
        registerFunction(tool_name, std::forward<Fn>(fn), ToolSchedulingScope::infer_default);
    }

    template<typename Fn>
    void registerFunction(const std::string& tool_name, Fn&& fn, ToolSchedulingScope scheduling_scope)
    {
        registerStatelessFunction(
            tool_name,
            std::forward<Fn>(fn),
            scheduling_scope,
            [this, &tool_name](auto&& callable) {
                invoke_adapter_.registerFunction(tool_name, std::forward<decltype(callable)>(callable));
            });
    }

    template<typename Fn>
    void registerFunction(const std::string& tool_name, Fn&& fn, func_registry::FunctionMetadata metadata)
    {
        registerFunction(tool_name, std::forward<Fn>(fn), std::move(metadata), ToolSchedulingScope::infer_default);
    }

    template<typename Fn>
    void registerFunction(
        const std::string& tool_name,
        Fn&& fn,
        func_registry::FunctionMetadata metadata,
        ToolSchedulingScope scheduling_scope)
    {
        registerStatelessFunction(
            tool_name,
            std::forward<Fn>(fn),
            scheduling_scope,
            [this, &tool_name, metadata = std::move(metadata)](auto&& callable) mutable {
                invoke_adapter_.registerFunction(tool_name, std::forward<decltype(callable)>(callable), std::move(metadata));
            });
    }

    template<typename Fn>
    void registerFunction(const std::string& tool_name, Fn&& fn, std::string description)
    {
        registerFunction(tool_name, std::forward<Fn>(fn), std::move(description), ToolSchedulingScope::infer_default);
    }

    template<typename Fn>
    void registerFunction(
        const std::string& tool_name,
        Fn&& fn,
        std::string description,
        ToolSchedulingScope scheduling_scope)
    {
        registerStatelessFunction(
            tool_name,
            std::forward<Fn>(fn),
            scheduling_scope,
            [this, &tool_name, description = std::move(description)](auto&& callable) mutable {
                invoke_adapter_.registerFunction(
                    tool_name,
                    std::forward<decltype(callable)>(callable),
                    std::move(description));
            });
    }

    template<typename R, typename... Args, typename Fn>
    void registerFunctionAs(const std::string& tool_name, Fn&& fn)
    {
        registerFunctionAs<R, Args...>(tool_name, std::forward<Fn>(fn), ToolSchedulingScope::infer_default);
    }

    template<typename R, typename... Args, typename Fn>
    void registerFunctionAs(
        const std::string& tool_name,
        Fn&& fn,
        ToolSchedulingScope scheduling_scope)
    {
        registerStatelessFunction(
            tool_name,
            std::forward<Fn>(fn),
            scheduling_scope,
            [this, &tool_name](auto&& callable) {
                invoke_adapter_.template registerFunctionAs<R, Args...>(tool_name, std::forward<decltype(callable)>(callable));
            });
    }

    template<typename R, typename... Args, typename Fn>
    void registerFunctionAs(const std::string& tool_name, Fn&& fn, func_registry::FunctionMetadata metadata)
    {
        registerFunctionAs<R, Args...>(
            tool_name,
            std::forward<Fn>(fn),
            std::move(metadata),
            ToolSchedulingScope::infer_default);
    }

    template<typename R, typename... Args, typename Fn>
    void registerFunctionAs(
        const std::string& tool_name,
        Fn&& fn,
        func_registry::FunctionMetadata metadata,
        ToolSchedulingScope scheduling_scope)
    {
        registerStatelessFunction(
            tool_name,
            std::forward<Fn>(fn),
            scheduling_scope,
            [this, &tool_name, metadata = std::move(metadata)](auto&& callable) mutable {
                invoke_adapter_.template registerFunctionAs<R, Args...>(
                    tool_name,
                    std::forward<decltype(callable)>(callable),
                    std::move(metadata));
            });
    }

    template<typename R, typename... Args, typename Fn>
    void registerFunctionAs(const std::string& tool_name, Fn&& fn, std::string description)
    {
        registerFunctionAs<R, Args...>(
            tool_name,
            std::forward<Fn>(fn),
            std::move(description),
            ToolSchedulingScope::infer_default);
    }

    template<typename R, typename... Args, typename Fn>
    void registerFunctionAs(
        const std::string& tool_name,
        Fn&& fn,
        std::string description,
        ToolSchedulingScope scheduling_scope)
    {
        registerStatelessFunction(
            tool_name,
            std::forward<Fn>(fn),
            scheduling_scope,
            [this, &tool_name, description = std::move(description)](auto&& callable) mutable {
                invoke_adapter_.template registerFunctionAs<R, Args...>(
                    tool_name,
                    std::forward<decltype(callable)>(callable),
                    std::move(description));
            });
    }

    json invokeJson(const json& request) const
    {
        return invoke_adapter_.invokeJson(request);
    }

    JsonInvokeResult invoke(const json& request) const
    {
        return invoke_adapter_.invoke(request);
    }

    template<typename T>
    T invoke(const json& request) const
    {
        return invoke_adapter_.template invoke<T>(request);
    }

    std::string invokeText(std::string_view request_text, int indent = 2) const
    {
        return invoke_adapter_.invokeText(request_text, indent);
    }

    json getAllToolSummariesJson() const
    {
        json tools = invoke_adapter_.getAllToolSummariesJson();
        for (auto& tool : tools)
        {
            const std::string tool_name = tool.value("name", "");
            tool = runtime_.applyToolSummaryOverlay(tool_name, std::move(tool));
        }
        return tools;
    }

    json getToolSchemaJson(const std::string& tool_name) const
    {
        return runtime_.applyToolSchemaOverlay(tool_name, invoke_adapter_.getToolSchemaJson(tool_name));
    }

    json getAllToolSchemasJson() const
    {
        json tools = invoke_adapter_.getAllToolSchemasJson();
        for (auto& tool : tools)
        {
            const std::string tool_name = tool.at("function").value("name", "");
            tool = runtime_.applyToolSchemaOverlay(tool_name, std::move(tool));
        }
        return tools;
    }

    std::optional<ToolMetadataView> findToolMetadata(const std::string& tool_name) const
    {
        const ToolSchedulingScope scheduling_scope = findStoredToolSchedulingScope(tool_name).value_or(
            ToolSchedulingScope::infer_default);
        const auto stateful_metadata = runtime_.findToolMetadata(tool_name);
        if (stateful_metadata.has_value())
        {
            ToolMetadataView metadata;
            metadata.tool_name = tool_name;
            metadata.execution_semantics = stateful_metadata->execution_semantics;
            metadata.scheduling_scope = scheduling_scope;
            metadata.stateful_kind = toToolStatefulKind(stateful_metadata->kind);
            metadata.object_type_name = stateful_metadata->object_type_name;
            if (metadata.stateful_kind == ToolStatefulKind::method || metadata.stateful_kind == ToolStatefulKind::destroy)
            {
                metadata.handle_parameter_name = stateful_metadata->handle_parameter_name;
            }
            return metadata;
        }

        const auto execution_semantics = invoke_adapter_.findToolExecutionSemantics(tool_name);
        if (!execution_semantics.has_value())
        {
            return std::nullopt;
        }

        ToolMetadataView metadata;
        metadata.tool_name = tool_name;
        metadata.execution_semantics = *execution_semantics;
        metadata.scheduling_scope = scheduling_scope;
        return metadata;
    }

private:
    static ToolStatefulKind toToolStatefulKind(
        typename json_session_invoke::detail::BasicStatefulRuntime<EnableThreadSafety>::StatefulToolKind kind) noexcept
    {
        switch (kind)
        {
        case json_session_invoke::detail::BasicStatefulRuntime<EnableThreadSafety>::StatefulToolKind::factory:
            return ToolStatefulKind::factory;
        case json_session_invoke::detail::BasicStatefulRuntime<EnableThreadSafety>::StatefulToolKind::method:
            return ToolStatefulKind::method;
        case json_session_invoke::detail::BasicStatefulRuntime<EnableThreadSafety>::StatefulToolKind::destroy:
            return ToolStatefulKind::destroy;
        }

        return ToolStatefulKind::none;
    }

    static void emitUnsafeConstructionWarning()
    {
        if constexpr (EnableThreadSafety)
        {
            return;
        }

        static std::once_flag warning_once;
        std::call_once(warning_once, []() {
            std::clog
                << "warning: json_session_invoke::BasicJsonSessionInvokeAdapter<false> is single-threaded only; "
                << "use JsonSessionInvokeAdapterThreadSafe for shared multi-threaded access.\n";
        });
    }

    bool isFunctionRegistered(const std::string& tool_name) const noexcept
    {
        return invoke_adapter_.hasRegisteredFunction(tool_name);
    }

    void storeToolSchedulingScope(const std::string& tool_name, ToolSchedulingScope scheduling_scope)
    {
        std::lock_guard<std::mutex> lock(tool_scheduling_scopes_mutex_);
        if (scheduling_scope == ToolSchedulingScope::infer_default)
        {
            tool_scheduling_scopes_.erase(tool_name);
            return;
        }

        tool_scheduling_scopes_[tool_name] = scheduling_scope;
    }

    template<typename Fn>
    static void validateStatelessFunctionRegistration()
    {
        if constexpr (std::is_member_function_pointer_v<std::decay_t<Fn>>)
        {
            throwMemberFunctionRegistrationError();
        }
    }

    template<typename Fn, typename RegisterFn>
    void registerStatelessFunction(
        const std::string& tool_name,
        Fn&& fn,
        ToolSchedulingScope scheduling_scope,
        RegisterFn&& register_function)
    {
        validateStatelessFunctionRegistration<Fn>();
        std::forward<RegisterFn>(register_function)(std::forward<Fn>(fn));
        storeToolSchedulingScope(tool_name, scheduling_scope);
    }

    std::optional<ToolSchedulingScope> findStoredToolSchedulingScope(const std::string& tool_name) const
    {
        std::lock_guard<std::mutex> lock(tool_scheduling_scopes_mutex_);
        const auto it = tool_scheduling_scopes_.find(tool_name);
        if (it == tool_scheduling_scopes_.end())
        {
            return std::nullopt;
        }

        return it->second;
    }

    static std::string defaultFactoryToolName(std::string_view object_type_name)
    {
        return makeDefaultSessionFactoryToolName(object_type_name);
    }

    static std::string defaultDestroyToolName(std::string_view object_type_name)
    {
        return makeDefaultSessionDestroyToolName(object_type_name);
    }

    template<typename T, typename Fn>
    void registerFactory(
        const std::string& factory_tool_name,
        Fn&& fn,
        func_registry::FunctionMetadata metadata,
        std::string configured_object_type_name,
        ObjectOptions options,
        ToolSchedulingScope scheduling_scope)
    {
        invoke_adapter_.template registerType<ObjectHandle>();
        runtime_.template registerFactory<T>(
            factory_tool_name,
            std::forward<Fn>(fn),
            std::move(metadata),
            std::move(configured_object_type_name),
            std::move(options),
            [this](const std::string& registered_tool_name, auto&& callable, func_registry::FunctionMetadata function_metadata) {
                invoke_adapter_.registerFunction(
                    registered_tool_name,
                    std::forward<decltype(callable)>(callable),
                    std::move(function_metadata));
            });
        storeToolSchedulingScope(factory_tool_name, scheduling_scope);
    }

    template<typename T>
    void registerDestroy(
        const std::string& destroy_tool_name,
        func_registry::FunctionMetadata metadata,
        ToolSchedulingScope scheduling_scope)
    {
        invoke_adapter_.template registerType<ObjectHandle>();
        runtime_.template registerDestroy<T>(
            destroy_tool_name,
            std::move(metadata),
            [this](const std::string& registered_tool_name, auto&& callable, func_registry::FunctionMetadata function_metadata) {
                invoke_adapter_.registerFunction(
                    registered_tool_name,
                    std::forward<decltype(callable)>(callable),
                    std::move(function_metadata));
            });
        storeToolSchedulingScope(destroy_tool_name, scheduling_scope);
    }

    template<typename T, typename Fn>
    void registerStatefulMethod(
        const std::string& method_tool_name,
        Fn&& fn,
        func_registry::FunctionMetadata metadata,
        ToolSchedulingScope scheduling_scope)
    {
        using Traits = json_session_invoke::detail::callable_traits_t<Fn>;
        using ObjectArg = typename Traits::template arg<0>;
        using ObjectType = std::remove_cv_t<std::remove_reference_t<ObjectArg>>;

        static_assert(Traits::arity >= 1,
            "registerStatefulMethod requires a callable whose first parameter is the stateful object");
        static_assert(std::is_same_v<ObjectType, T>,
            "registerStatefulMethod requires a callable whose first parameter belongs to T");

        ensureDirectObjectTypeRegistered<Fn>();
        runtime_.template registerMethod<Fn>(
            method_tool_name,
            std::forward<Fn>(fn),
            normalizeMemberMetadata<Fn>(std::move(metadata)),
            [this](const std::string& registered_tool_name, auto&& callable, func_registry::FunctionMetadata function_metadata) {
                invoke_adapter_.registerFunction(
                    registered_tool_name,
                    std::forward<decltype(callable)>(callable),
                    std::move(function_metadata));
            },
            [this](const json& value, std::type_index expected_cpp_type) {
                return invoke_adapter_.convertFromJson(value, expected_cpp_type);
            });
            storeToolSchedulingScope(method_tool_name, scheduling_scope);
    }

    [[noreturn]] static void throwMemberFunctionRegistrationError()
    {
        throw std::invalid_argument(
            "JsonSessionInvokeAdapter::registerFunction does not accept member function pointers; use stateful<T>().method(...) instead");
    }

    template<typename Fn>
    void ensureDirectObjectTypeRegistered()
    {
        using Traits = json_session_invoke::detail::callable_traits_t<Fn>;
        using ObjectArg = typename Traits::template arg<0>;
        using ObjectType = std::remove_cv_t<std::remove_reference_t<ObjectArg>>;

        if constexpr (json_invoke::detail::can_auto_register_type_v<ObjectType>)
        {
            if (!invoke_adapter_.canConvertFromJson(typeid(ObjectType)))
            {
                invoke_adapter_.template registerType<ObjectType>();
            }
        }
    }

    template<typename Fn>
    static func_registry::FunctionMetadata normalizeMemberMetadata(func_registry::FunctionMetadata metadata)
    {
        using Traits = json_session_invoke::detail::callable_traits_t<Fn>;

        if (metadata.param_names.size() == Traits::arity - 1)
        {
            std::vector<std::string> wrapped_names;
            wrapped_names.reserve(Traits::arity);
            wrapped_names.push_back(std::string(default_handle_parameter_name));
            wrapped_names.insert(
                wrapped_names.end(),
                std::make_move_iterator(metadata.param_names.begin()),
                std::make_move_iterator(metadata.param_names.end()));
            metadata.param_names = std::move(wrapped_names);
        }
        else if (metadata.param_names.empty())
        {
            metadata = makeDefaultMemberMetadata<Fn>(std::move(metadata.description));
        }

        return metadata;
    }

    template<typename Fn>
    static func_registry::FunctionMetadata makeDefaultMemberMetadata(std::string description)
    {
        using Traits = json_session_invoke::detail::callable_traits_t<Fn>;

        func_registry::FunctionMetadata metadata;
        metadata.description = std::move(description);
        metadata.param_names.reserve(Traits::arity);
        metadata.param_names.push_back(std::string(default_handle_parameter_name));
        for (std::size_t index = 1; index < Traits::arity; ++index)
        {
            metadata.param_names.push_back("arg" + std::to_string(index - 1));
        }

        return metadata;
    }

    static func_registry::FunctionMetadata makeDestroyMetadata(std::string description)
    {
        return func_registry::FunctionMetadata{{std::string(default_handle_parameter_name)}, std::move(description)};
    }

    static std::string defaultDestroyDescriptionText()
    {
        return "Destroy a previously created session object handle.";
    }

    std::string defaultDestroyDescription() const
    {
        return stateful_defaults_.destroy_description;
    }

    UnderlyingAdapter invoke_adapter_;
    json_session_invoke::detail::BasicStatefulRuntime<EnableThreadSafety> runtime_{};
    StatefulDefaults stateful_defaults_{};
    mutable std::mutex tool_scheduling_scopes_mutex_;
    std::unordered_map<std::string, ToolSchedulingScope> tool_scheduling_scopes_;
};

using JsonSessionInvokeAdapterThreadSafe = BasicJsonSessionInvokeAdapter<true>;
using JsonSessionInvokeAdapterUnsafe = BasicJsonSessionInvokeAdapter<false>;

} // namespace json_session_invoke