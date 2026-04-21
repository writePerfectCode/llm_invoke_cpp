#pragma once

#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <utility>
#include <vector>

#include <json_invoke/json_invoke.hpp>
#include <json_session_invoke/detail/stateful_runtime.hpp>
#include <json_session_invoke/json_session_support.hpp>

namespace json_session_invoke {

using json_invoke::JsonInvokeError;
using json_invoke::JsonInvokeResult;
using json_invoke::json;

template<bool EnableThreadSafety = false>
class BasicJsonSessionInvokeAdapter {
public:
    using UnderlyingAdapter = json_invoke::BasicJsonInvokeAdapter<EnableThreadSafety>;
    using MapType = typename UnderlyingAdapter::MapType;
    using Handle = ObjectHandle;
    using Options = ObjectOptions;

    struct StatefulDefaults {
        bool auto_register_destroy{true};
        std::string destroy_description{defaultDestroyDescriptionText()};
    };

    template<typename T>
    class StatefulRegistrationBuilder {
    public:
        StatefulRegistrationBuilder(BasicJsonSessionInvokeAdapter& adapter, std::string object_type_name, Options options)
            : adapter_(adapter)
            , object_type_name_(std::move(object_type_name))
            , options_(std::move(options))
        {
        }

        StatefulRegistrationBuilder(const StatefulRegistrationBuilder&) = delete;
        StatefulRegistrationBuilder& operator=(const StatefulRegistrationBuilder&) = delete;

        StatefulRegistrationBuilder(StatefulRegistrationBuilder&& other) noexcept
            : adapter_(other.adapter_)
            , object_type_name_(std::move(other.object_type_name_))
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

            object_type_name_ = std::move(other.object_type_name_);
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
        StatefulRegistrationBuilder& create(const std::string& name, Fn&& fn)
        {
            adapter_.template registerFactory<T>(name, std::forward<Fn>(fn), object_type_name_, options_);
            created_ = true;
            return *this;
        }

        template<typename Fn>
        StatefulRegistrationBuilder& create(const std::string& name, Fn&& fn, func_registry::FunctionMetadata metadata)
        {
            adapter_.template registerFactory<T>(
                name,
                std::forward<Fn>(fn),
                std::move(metadata),
                object_type_name_,
                options_);
            created_ = true;
            return *this;
        }

        template<typename Fn>
        StatefulRegistrationBuilder& create(const std::string& name, Fn&& fn, std::string description)
        {
            adapter_.template registerFactory<T>(
                name,
                std::forward<Fn>(fn),
                std::move(description),
                options_);
            created_ = true;
            return *this;
        }

        template<typename Fn>
        StatefulRegistrationBuilder& create(Fn&& fn)
        {
            return create(defaultFactoryToolName(), std::forward<Fn>(fn));
        }

        template<typename Fn>
        StatefulRegistrationBuilder& create(Fn&& fn, func_registry::FunctionMetadata metadata)
        {
            return create(defaultFactoryToolName(), std::forward<Fn>(fn), std::move(metadata));
        }

        template<typename Fn>
        StatefulRegistrationBuilder& create(Fn&& fn, std::string description)
        {
            return create(defaultFactoryToolName(), std::forward<Fn>(fn), std::move(description));
        }

        template<typename Fn>
        StatefulRegistrationBuilder& method(const std::string& name, Fn&& fn)
        {
            validateMethodType<Fn>();
            adapter_.registerFunction(name, std::forward<Fn>(fn));
            return *this;
        }

        template<typename Fn>
        StatefulRegistrationBuilder& method(const std::string& name, Fn&& fn, func_registry::FunctionMetadata metadata)
        {
            validateMethodType<Fn>();
            adapter_.registerFunction(name, std::forward<Fn>(fn), std::move(metadata));
            return *this;
        }

        template<typename Fn>
        StatefulRegistrationBuilder& method(const std::string& name, Fn&& fn, std::string description)
        {
            validateMethodType<Fn>();
            adapter_.registerFunction(name, std::forward<Fn>(fn), std::move(description));
            return *this;
        }

        StatefulRegistrationBuilder& destroy()
        {
            adapter_.template registerDestroy<T>(defaultDestroyToolName());
            destroy_registered_ = true;
            return *this;
        }

        StatefulRegistrationBuilder& destroy(const std::string& name)
        {
            adapter_.template registerDestroy<T>(name);
            destroy_registered_ = true;
            return *this;
        }

        StatefulRegistrationBuilder& destroy(const std::string& name, func_registry::FunctionMetadata metadata)
        {
            adapter_.template registerDestroy<T>(name, std::move(metadata));
            destroy_registered_ = true;
            return *this;
        }

        StatefulRegistrationBuilder& destroy(const std::string& name, std::string description)
        {
            adapter_.template registerDestroy<T>(name, std::move(description));
            destroy_registered_ = true;
            return *this;
        }

        StatefulRegistrationBuilder& options(Options options)
        {
            options_ = std::move(options);
            return *this;
        }

        const std::string& objectTypeName() const noexcept
        {
            return object_type_name_;
        }

        const Options& objectOptions() const noexcept
        {
            return options_;
        }

    private:
        void ensureDefaultDestroyRegistered() noexcept
        {
            if (!active_ || !created_ || destroy_registered_ || !adapter_.statefulDefaults().auto_register_destroy)
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
                adapter_.template registerDestroy<T>(destroy_tool_name);
            }
            catch (...)
            {
            }
        }

        template<typename Fn>
        static void validateMethodType()
        {
            static_assert(
                std::is_member_function_pointer_v<std::decay_t<Fn>>,
                "stateful<T>().method(...) requires a member function pointer");

            using Traits = json_session_invoke::detail::callable_traits_t<Fn>;
            using ObjectArg = typename Traits::template arg<0>;
            using ObjectType = std::remove_cv_t<std::remove_reference_t<ObjectArg>>;
            static_assert(
                std::is_same_v<ObjectType, T>,
                "stateful<T>().method(...) requires a member function belonging to T");
        }

        std::string defaultFactoryToolName() const
        {
            return BasicJsonSessionInvokeAdapter::defaultFactoryToolName(object_type_name_);
        }

        std::string defaultDestroyToolName() const
        {
            return BasicJsonSessionInvokeAdapter::defaultDestroyToolName(object_type_name_);
        }

        BasicJsonSessionInvokeAdapter& adapter_;
        std::string object_type_name_;
        Options options_;
        bool created_{false};
        bool destroy_registered_{false};
        bool active_{true};
    };

    static std::string defaultFactoryToolName(std::string_view object_type_name)
    {
        return makeDefaultSessionFactoryToolName(object_type_name);
    }

    static std::string defaultDestroyToolName(std::string_view object_type_name)
    {
        return makeDefaultSessionDestroyToolName(object_type_name);
    }

    template<typename T>
    StatefulRegistrationBuilder<T> stateful(std::string object_type_name, Options options = {})
    {
        return StatefulRegistrationBuilder<T>(*this, std::move(object_type_name), std::move(options));
    }

    BasicJsonSessionInvokeAdapter() = default;

    explicit BasicJsonSessionInvokeAdapter(MapType& func_registry)
        : invoke_adapter_(func_registry)
    {
    }

    UnderlyingAdapter& jsonInvokeAdapter() noexcept
    {
        return invoke_adapter_;
    }

    StatefulDefaults& statefulDefaults() noexcept
    {
        return stateful_defaults_;
    }

    const StatefulDefaults& statefulDefaults() const noexcept
    {
        return stateful_defaults_;
    }

    void setStatefulDefaults(StatefulDefaults defaults)
    {
        stateful_defaults_ = std::move(defaults);
    }

    const UnderlyingAdapter& jsonInvokeAdapter() const noexcept
    {
        return invoke_adapter_;
    }

    UnderlyingAdapter& invokeAdapter() noexcept
    {
        return jsonInvokeAdapter();
    }

    const UnderlyingAdapter& invokeAdapter() const noexcept
    {
        return jsonInvokeAdapter();
    }

    MapType& functionRegistry() noexcept
    {
        return invoke_adapter_.functionRegistry();
    }

    const MapType& functionRegistry() const noexcept
    {
        return invoke_adapter_.functionRegistry();
    }

    auto& registry() noexcept
    {
        return invoke_adapter_.registry();
    }

    const auto& registry() const noexcept
    {
        return invoke_adapter_.registry();
    }

    bool isFunctionRegistered(const std::string& name) const noexcept
    {
        try
        {
            static_cast<void>(functionRegistry().getFunction(name));
            return true;
        }
        catch (...)
        {
            return false;
        }
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
    void registerFunction(const std::string& name, Fn&& fn)
    {
        if constexpr (std::is_member_function_pointer_v<std::decay_t<Fn>>)
        {
            registerFunction(name, std::forward<Fn>(fn), makeDefaultMemberMetadata<Fn>(std::string{}));
            return;
        }

        invoke_adapter_.registerFunction(name, std::forward<Fn>(fn));
    }

    template<typename Fn>
    void registerFunction(const std::string& name, Fn&& fn, func_registry::FunctionMetadata metadata)
    {
        if constexpr (std::is_member_function_pointer_v<std::decay_t<Fn>>)
        {
            ensureDirectObjectTypeRegistered<Fn>();
            runtime_.template registerMethod<Fn>(
                name,
                std::forward<Fn>(fn),
                normalizeMemberMetadata<Fn>(std::move(metadata)),
                [this](const std::string& function_name, auto&& callable, func_registry::FunctionMetadata function_metadata) {
                    invoke_adapter_.registerFunction(function_name, std::forward<decltype(callable)>(callable), std::move(function_metadata));
                },
                [this](const json& value, std::type_index expected_type) {
                    return invoke_adapter_.registry().fromJson(value, expected_type);
                });
            return;
        }

        invoke_adapter_.registerFunction(name, std::forward<Fn>(fn), std::move(metadata));
    }

    template<typename Fn>
    void registerFunction(const std::string& name, Fn&& fn, std::string description)
    {
        if constexpr (std::is_member_function_pointer_v<std::decay_t<Fn>>)
        {
            registerFunction(name, std::forward<Fn>(fn), makeDefaultMemberMetadata<Fn>(std::move(description)));
            return;
        }

        invoke_adapter_.registerFunction(name, std::forward<Fn>(fn), std::move(description));
    }

    template<typename R, typename... Args, typename Fn>
    void registerFunctionAs(const std::string& name, Fn&& fn)
    {
        invoke_adapter_.template registerFunctionAs<R, Args...>(name, std::forward<Fn>(fn));
    }

    template<typename R, typename... Args, typename Fn>
    void registerFunctionAs(const std::string& name, Fn&& fn, func_registry::FunctionMetadata metadata)
    {
        invoke_adapter_.template registerFunctionAs<R, Args...>(name, std::forward<Fn>(fn), std::move(metadata));
    }

    template<typename R, typename... Args, typename Fn>
    void registerFunctionAs(const std::string& name, Fn&& fn, std::string description)
    {
        invoke_adapter_.template registerFunctionAs<R, Args...>(name, std::forward<Fn>(fn), std::move(description));
    }

    template<typename T, typename Fn>
    void registerFactory(const std::string& name, Fn&& fn)
    {
        registerFactory<T>(name, std::forward<Fn>(fn), func_registry::FunctionMetadata{});
    }

    template<typename T, typename Fn>
    void registerFactory(const std::string& name, Fn&& fn, ObjectOptions options)
    {
        registerFactory<T>(name, std::forward<Fn>(fn), func_registry::FunctionMetadata{}, std::string{}, std::move(options));
    }

    template<typename T, typename Fn>
    void registerFactory(const std::string& name, Fn&& fn, func_registry::FunctionMetadata metadata)
    {
        registerFactory<T>(name, std::forward<Fn>(fn), std::move(metadata), std::string{}, Options{});
    }

    template<typename T, typename Fn>
    void registerFactory(const std::string& name, Fn&& fn, func_registry::FunctionMetadata metadata, ObjectOptions options)
    {
        registerFactory<T>(name, std::forward<Fn>(fn), std::move(metadata), std::string{}, std::move(options));
    }

    template<typename T, typename Fn>
    void registerFactory(const std::string& name, Fn&& fn, std::string description)
    {
        registerFactory<T>(name, std::forward<Fn>(fn), func_registry::FunctionMetadata{{}, std::move(description)}, std::string{}, Options{});
    }

    template<typename T, typename Fn>
    void registerFactory(const std::string& name, Fn&& fn, std::string description, ObjectOptions options)
    {
        registerFactory<T>(
            name,
            std::forward<Fn>(fn),
            func_registry::FunctionMetadata{{}, std::move(description)},
            std::string{},
            std::move(options));
    }

    template<typename T, typename Fn>
    void registerFactory(
        const std::string& name,
        Fn&& fn,
        func_registry::FunctionMetadata metadata,
        std::string object_type_name,
        ObjectOptions options = {})
    {
        invoke_adapter_.template registerType<Handle>();
        runtime_.template registerFactory<T>(
            name,
            std::forward<Fn>(fn),
            std::move(metadata),
            std::move(object_type_name),
            std::move(options),
            [this](const std::string& function_name, auto&& callable, func_registry::FunctionMetadata function_metadata) {
                invoke_adapter_.registerFunction(function_name, std::forward<decltype(callable)>(callable), std::move(function_metadata));
            });
    }

    template<typename T>
    void registerDestroy()
    {
        registerDestroy<T>(runtime_.template defaultDestroyToolName<T>(), makeDestroyMetadata(defaultDestroyDescription()));
    }

    template<typename T>
    void registerDestroy(const std::string& name)
    {
        registerDestroy<T>(name, makeDestroyMetadata(defaultDestroyDescription()));
    }

    template<typename T>
    void registerDestroy(const std::string& name, func_registry::FunctionMetadata metadata)
    {
        invoke_adapter_.template registerType<Handle>();
        runtime_.template registerDestroy<T>(
            name,
            std::move(metadata),
            [this](const std::string& function_name, auto&& callable, func_registry::FunctionMetadata function_metadata) {
                invoke_adapter_.registerFunction(function_name, std::forward<decltype(callable)>(callable), std::move(function_metadata));
            });
    }

    template<typename T>
    void registerDestroy(const std::string& name, std::string description)
    {
        registerDestroy<T>(name, makeDestroyMetadata(std::move(description)));
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

    json getToolSpecJson(const std::string& name) const
    {
        return runtime_.applyToolSpecOverlay(name, invoke_adapter_.getToolSpecJson(name));
    }

    json getAllToolSpecsJson() const
    {
        json tools = invoke_adapter_.getAllToolSpecsJson();
        for (auto& tool : tools)
        {
            const std::string tool_name = tool.value("tool_name", "");
            tool = runtime_.applyToolSpecOverlay(tool_name, std::move(tool));
        }
        return tools;
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

    json getToolSchemaJson(const std::string& name) const
    {
        return runtime_.applyToolSchemaOverlay(name, invoke_adapter_.getToolSchemaJson(name));
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

private:
    template<typename Fn>
    void ensureDirectObjectTypeRegistered()
    {
        using Traits = json_session_invoke::detail::callable_traits_t<Fn>;
        using ObjectArg = typename Traits::template arg<0>;
        using ObjectType = std::remove_cv_t<std::remove_reference_t<ObjectArg>>;

        if constexpr (json_invoke::detail::can_auto_register_type_v<ObjectType>)
        {
            if (!invoke_adapter_.registry().canRead(typeid(ObjectType)))
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
};

using JsonSessionInvokeAdapter = BasicJsonSessionInvokeAdapter<false>;
using JsonSessionInvokeAdapterThreadSafe = BasicJsonSessionInvokeAdapter<true>;

} // namespace json_session_invoke