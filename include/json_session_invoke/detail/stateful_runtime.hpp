#pragma once

#include <any>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

#include <json_invoke/json_common.hpp>
#include <func_registry/func_registry.hpp>
#include <json_session_invoke/json_session_support.hpp>

namespace json_session_invoke::detail {

template<typename Fn>
using callable_traits_t = std::conditional_t<
    std::is_member_function_pointer_v<std::decay_t<Fn>>,
    func_registry::member_function_traits<std::decay_t<Fn>>,
    func_registry::function_traits<std::decay_t<Fn>>>;

inline bool looksLikeObjectHandle(const json_invoke::json& value)
{
    if (value.is_string())
    {
        return true;
    }

    if (!value.is_object())
    {
        return false;
    }

    const auto object_id_it = value.find("object_id");
    return object_id_it != value.end() && object_id_it->is_string();
}

template<bool EnableThreadSafety>
class BasicStatefulRuntime {
public:
    using Store = json_session_invoke::detail::BasicStatefulObjectStore<EnableThreadSafety>;

    template<typename T>
    std::string defaultDestroyToolName() const
    {
        const auto object_type_name = object_store_.template configuredObjectTypeName<T>();
        return makeDefaultSessionDestroyToolName(object_type_name.value_or("object"));
    }

    enum class StatefulToolKind {
        factory,
        method,
        destroy,
    };

    struct StatefulToolMetadata {
        StatefulToolKind kind{StatefulToolKind::method};
        json_invoke::ToolExecutionSemantics execution_semantics{json_invoke::ToolExecutionSemantics::unknown};
        std::type_index object_cpp_type{typeid(void)};
        std::string object_type_name;
        std::string factory_tool_name;
        std::string handle_parameter_name{std::string(default_handle_parameter_name)};
    };

    template<typename T, typename Fn, typename RegisterFunctionFn>
    void registerFactory(
        const std::string& name,
        Fn&& fn,
        func_registry::FunctionMetadata metadata,
        std::string object_type_name,
        json_session_invoke::ObjectOptions options,
        RegisterFunctionFn&& register_function)
    {
        using Traits = callable_traits_t<Fn>;
        using ReturnType = typename Traits::return_type;

        static_assert(!std::is_void_v<ReturnType>, "object factory must return an object or std::shared_ptr<object>");
        static_assert(
            std::is_convertible_v<json_session_invoke::detail::stateful_remove_cvref_t<ReturnType>, std::shared_ptr<T>> ||
                std::is_constructible_v<T, ReturnType>,
            "object factory must return T or std::shared_ptr<T>");

        if (!object_type_name.empty())
        {
            object_store_.template setObjectTypeName<T>(object_type_name);
        }

        const std::string resolved_object_type_name = object_store_.template objectTypeName<T>();
        std::forward<RegisterFunctionFn>(register_function)(
            name,
            makeFactoryWrapper<T>(
                std::forward<Fn>(fn),
                resolved_object_type_name,
                std::move(options),
                std::make_index_sequence<Traits::arity>{}),
            std::move(metadata));

        recordStatefulFactory<T>(name, resolved_object_type_name);
    }

    template<typename T, typename RegisterFunctionFn>
    void registerDestroy(
        const std::string& name,
        func_registry::FunctionMetadata metadata,
        RegisterFunctionFn&& register_function)
    {
        if (metadata.param_names.empty())
        {
            metadata.param_names = {std::string(default_handle_parameter_name)};
        }

        std::forward<RegisterFunctionFn>(register_function)(
            name,
            [this](json_session_invoke::ObjectHandle handle) {
                return object_store_.destroy(handle);
            },
            std::move(metadata));

        recordStatefulDestroy<T>(name, object_store_.template objectTypeName<T>());
    }

    template<typename Fn, typename RegisterFunctionFn, typename FromJsonFn>
    void registerMethod(
        const std::string& name,
        Fn&& fn,
        func_registry::FunctionMetadata metadata,
        RegisterFunctionFn&& register_function,
        FromJsonFn&& from_json)
    {
        using Traits = callable_traits_t<Fn>;
        using ObjectArg = typename Traits::template arg<0>;
        using ObjectType = std::remove_cv_t<std::remove_reference_t<ObjectArg>>;

        const std::string object_type_name = object_store_.template objectTypeName<ObjectType>();
        const std::string handle_parameter_name = metadata.param_names.empty()
            ? std::string(default_handle_parameter_name)
            : metadata.param_names.front();

        std::forward<RegisterFunctionFn>(register_function)(
            name,
            makeMethodWrapper<Fn>(
                std::forward<Fn>(fn),
                std::forward<FromJsonFn>(from_json),
                std::make_index_sequence<Traits::arity - 1>{}),
            std::move(metadata));

        method_tools_by_object_type_[typeid(ObjectType)].push_back(name);

        StatefulToolMetadata tool_metadata;
        tool_metadata.kind = StatefulToolKind::method;
        tool_metadata.execution_semantics = methodExecutionSemantics<ObjectArg>();
        tool_metadata.object_cpp_type = typeid(ObjectType);
        tool_metadata.object_type_name = object_type_name;
        tool_metadata.handle_parameter_name = handle_parameter_name;

        auto factory_it = factory_tool_by_object_type_.find(typeid(ObjectType));
        if (factory_it != factory_tool_by_object_type_.end())
        {
            tool_metadata.factory_tool_name = factory_it->second;
        }

        stateful_tools_[name] = std::move(tool_metadata);
        refreshRelatedStatefulTools(typeid(ObjectType));
    }

    json_invoke::json applyToolSummaryOverlay(const std::string& name, json_invoke::json summary) const
    {
        const auto it = stateful_tools_.find(name);
        if (it == stateful_tools_.end())
        {
            return summary;
        }

        if (it->second.kind == StatefulToolKind::factory)
        {
            appendDescription(summary["description"], factorySummarySuffix());
        }
        else if (!it->second.factory_tool_name.empty())
        {
            appendDescription(summary["description"], handleUsageSuffix(it->second.factory_tool_name));
        }

        summary["x-execution-semantics"] = executionSemanticsName(it->second.execution_semantics);

        return summary;
    }

    json_invoke::json applyToolSpecOverlay(const std::string& name, json_invoke::json spec) const
    {
        const auto it = stateful_tools_.find(name);
        if (it == stateful_tools_.end())
        {
            return spec;
        }

        if (it->second.kind == StatefulToolKind::factory)
        {
            appendDescription(spec["description"], factorySpecSuffix());
            spec["x-stateful-kind"] = "factory";
            spec["x-object-type"] = it->second.object_type_name;
        }
        else if (it->second.kind == StatefulToolKind::method)
        {
            appendDescription(spec["description"], methodSpecSuffix(it->second.factory_tool_name));
            spec["x-stateful-kind"] = "method";
            spec["x-object-type"] = it->second.object_type_name;
            spec["x-handle-source-tool"] = it->second.factory_tool_name;
        }
        else
        {
            appendDescription(spec["description"], destroySpecSuffix());
            spec["x-stateful-kind"] = "destroy";
            spec["x-object-type"] = it->second.object_type_name;
            spec["x-handle-source-tool"] = it->second.factory_tool_name;
        }

        spec["x-execution-semantics"] = executionSemanticsName(it->second.execution_semantics);

        return spec;
    }

    json_invoke::json applyToolSchemaOverlay(const std::string& name, json_invoke::json schema) const
    {
        const auto it = stateful_tools_.find(name);
        if (it == stateful_tools_.end())
        {
            return schema;
        }

        json_invoke::json& function = schema["function"];
        if (it->second.kind == StatefulToolKind::factory)
        {
            appendDescription(function["description"], factorySchemaSuffix());
            function["x-stateful-kind"] = "factory";
            function["x-object-type"] = it->second.object_type_name;
            function["x-execution-semantics"] = executionSemanticsName(it->second.execution_semantics);

            json_invoke::json related_tools = json_invoke::json::array();
            const auto methods_it = method_tools_by_object_type_.find(it->second.object_cpp_type);
            if (methods_it != method_tools_by_object_type_.end())
            {
                for (const auto& method_name : methods_it->second)
                {
                    related_tools.push_back(method_name);
                }
            }

            const auto destroy_it = destroy_tool_by_object_type_.find(it->second.object_cpp_type);
            if (destroy_it != destroy_tool_by_object_type_.end())
            {
                related_tools.push_back(destroy_it->second);
            }

            if (!related_tools.empty())
            {
                function["x-related-tools"] = std::move(related_tools);
            }

            return schema;
        }

        if (!it->second.factory_tool_name.empty())
        {
            const std::string description = it->second.kind == StatefulToolKind::destroy
                ? destroySchemaSuffix(it->second.factory_tool_name)
                : methodSchemaSuffix(it->second.factory_tool_name);
            appendDescription(function["description"], description);
        }

        function["x-stateful-kind"] = it->second.kind == StatefulToolKind::method ? "method" : "destroy";
        function["x-object-type"] = it->second.object_type_name;
        function["x-execution-semantics"] = executionSemanticsName(it->second.execution_semantics);
        function["x-handle-parameter"] = it->second.handle_parameter_name;
        if (!it->second.factory_tool_name.empty())
        {
            function["x-handle-source-tool"] = it->second.factory_tool_name;
        }

        json_invoke::json& properties = function["parameters"]["properties"];
        properties[it->second.handle_parameter_name] = makeHandleParameterSchema(it->second);
        return schema;
    }

private:
    template<typename ObjectArg>
    static json_invoke::ToolExecutionSemantics methodExecutionSemantics()
    {
        if constexpr (std::is_const_v<std::remove_reference_t<ObjectArg>>)
        {
            return json_invoke::ToolExecutionSemantics::read_only;
        }

        return json_invoke::ToolExecutionSemantics::mutating;
    }

    static std::string_view executionSemanticsName(json_invoke::ToolExecutionSemantics semantics)
    {
        return json_invoke::toolExecutionSemanticsName(semantics);
    }

    template<typename Fn, typename FromJsonFn, std::size_t... I>
    auto makeMethodWrapper(Fn&& fn, FromJsonFn&& from_json, std::index_sequence<I...>)
    {
        using Traits = callable_traits_t<Fn>;
        using ObjectArg = typename Traits::template arg<0>;
        using ObjectType = std::remove_cv_t<std::remove_reference_t<ObjectArg>>;
        using ReturnType = typename Traits::return_type;

        return [this,
                member = std::decay_t<Fn>(std::forward<Fn>(fn)),
                from_json = std::forward<FromJsonFn>(from_json)]
               (json_invoke::json object_or_handle, typename Traits::template arg<I + 1>... args) mutable -> ReturnType {
            if (looksLikeObjectHandle(object_or_handle))
            {
                auto access = object_store_.template checkoutObject<ObjectType>(
                    object_or_handle.get<json_session_invoke::ObjectHandle>(),
                    object_store_.template objectTypeName<ObjectType>());
                std::unique_ptr<std::unique_lock<typename Store::ObjectMutexType>> active_lock;
                if (access.invocation_mutex)
                {
                    active_lock = std::make_unique<std::unique_lock<typename Store::ObjectMutexType>>(*access.invocation_mutex);
                }

                return std::invoke(member, *access.object, std::forward<typename Traits::template arg<I + 1>>(args)...);
            }

            std::any object_any = from_json(object_or_handle, typeid(ObjectType));
            if constexpr (std::is_lvalue_reference_v<ObjectArg> && !std::is_const_v<std::remove_reference_t<ObjectArg>>)
            {
                auto object = std::make_shared<ObjectType>(std::any_cast<ObjectType>(std::move(object_any)));
                return std::invoke(member, *object, std::forward<typename Traits::template arg<I + 1>>(args)...);
            }

            auto object = std::any_cast<ObjectType>(std::move(object_any));
            return std::invoke(member, object, std::forward<typename Traits::template arg<I + 1>>(args)...);
        };
    }

    template<typename T, typename Fn, std::size_t... I>
    auto makeFactoryWrapper(Fn&& fn, std::string object_type_name, json_session_invoke::ObjectOptions options, std::index_sequence<I...>)
    {
        using Traits = callable_traits_t<Fn>;

        return [this,
                factory = std::decay_t<Fn>(std::forward<Fn>(fn)),
                object_type_name = std::move(object_type_name),
                options = std::move(options)]
               (typename Traits::template arg<I>... args) -> json_session_invoke::ObjectHandle {
            return storeFactoryResult<T>(
                std::invoke(factory, std::forward<typename Traits::template arg<I>>(args)...),
                object_type_name,
                options);
        };
    }

    template<typename T, typename FactoryResult>
    json_session_invoke::ObjectHandle storeFactoryResult(
        FactoryResult&& result,
        std::string object_type_name,
        json_session_invoke::ObjectOptions options)
    {
        if constexpr (std::is_convertible_v<json_session_invoke::detail::stateful_remove_cvref_t<FactoryResult>, std::shared_ptr<T>>)
        {
            return object_store_.template emplace<T>(
                std::shared_ptr<T>(std::forward<FactoryResult>(result)),
                std::move(object_type_name),
                std::move(options));
        }

        return object_store_.template emplace<T>(std::forward<FactoryResult>(result), std::move(object_type_name), std::move(options));
    }

    template<typename T>
    void recordStatefulFactory(const std::string& name, const std::string& object_type_name)
    {
        factory_tool_by_object_type_[typeid(T)] = name;

        StatefulToolMetadata metadata;
        metadata.kind = StatefulToolKind::factory;
        metadata.execution_semantics = json_invoke::ToolExecutionSemantics::mutating;
        metadata.object_cpp_type = typeid(T);
        metadata.object_type_name = object_type_name;
        metadata.factory_tool_name = name;
        stateful_tools_[name] = std::move(metadata);
        refreshRelatedStatefulTools(typeid(T));
    }

    template<typename T>
    void recordStatefulDestroy(const std::string& name, const std::string& object_type_name)
    {
        destroy_tool_by_object_type_[typeid(T)] = name;

        StatefulToolMetadata metadata;
        metadata.kind = StatefulToolKind::destroy;
        metadata.execution_semantics = json_invoke::ToolExecutionSemantics::mutating;
        metadata.object_cpp_type = typeid(T);
        metadata.object_type_name = object_type_name;
        metadata.factory_tool_name = factoryToolName(typeid(T));
        metadata.handle_parameter_name = std::string(default_handle_parameter_name);
        stateful_tools_[name] = std::move(metadata);
        refreshRelatedStatefulTools(typeid(T));
    }

    void refreshRelatedStatefulTools(std::type_index object_cpp_type)
    {
        const std::string factory_name = factoryToolName(object_cpp_type);
        const auto methods_it = method_tools_by_object_type_.find(object_cpp_type);

        if (!factory_name.empty())
        {
            for (const auto& method_name : methods_it != method_tools_by_object_type_.end() ? methods_it->second : std::vector<std::string>{})
            {
                auto tool_it = stateful_tools_.find(method_name);
                if (tool_it != stateful_tools_.end())
                {
                    tool_it->second.factory_tool_name = factory_name;
                }
            }

            const auto destroy_it = destroy_tool_by_object_type_.find(object_cpp_type);
            if (destroy_it != destroy_tool_by_object_type_.end())
            {
                stateful_tools_[destroy_it->second].factory_tool_name = factory_name;
            }
        }
    }

    std::string factoryToolName(std::type_index object_cpp_type) const
    {
        const auto it = factory_tool_by_object_type_.find(object_cpp_type);
        return it == factory_tool_by_object_type_.end() ? std::string{} : it->second;
    }

    static void appendDescription(json_invoke::json& node, std::string text)
    {
        const std::string existing = node.is_string() ? node.get<std::string>() : std::string{};
        if (existing.find(text) != std::string::npos)
        {
            return;
        }

        node = existing.empty() ? std::move(text) : existing + " " + text;
    }

    static std::string factorySummarySuffix()
    {
        return "Creates a session-scoped object handle for later method calls.";
    }

    static std::string handleUsageSuffix(const std::string& factory_tool_name)
    {
        return "Accepts the handle returned by " + factory_tool_name + ".";
    }

    static std::string factorySpecSuffix()
    {
        return "Creates a session-scoped object and returns an object handle.";
    }

    static std::string methodSpecSuffix(const std::string& factory_tool_name)
    {
        return "Invoke this method with the handle returned by " + factory_tool_name + ".";
    }

    static std::string destroySpecSuffix()
    {
        return "Invalidates a previously created session object handle.";
    }

    static std::string factorySchemaSuffix()
    {
        return "Creates a session-scoped object and returns a reusable handle.";
    }

    static std::string methodSchemaSuffix(const std::string& factory_tool_name)
    {
        return "Pass the handle returned by " + factory_tool_name + " to invoke this method.";
    }

    static std::string destroySchemaSuffix(const std::string& factory_tool_name)
    {
        return "Pass the handle returned by " + factory_tool_name + " to invalidate that object.";
    }

    json_invoke::json makeHandleParameterSchema(const StatefulToolMetadata& metadata) const
    {
        json_invoke::json schema = {
            {"type", "object"},
            {"additionalProperties", false},
            {"properties", {
                {"object_id", {
                    {"type", "string"},
                    {"description", "Opaque in-memory object identifier returned by a create tool."},
                }},
                {"object_type", {
                    {"type", "string"},
                    {"description", "Logical object type name used for validation and debugging."},
                }},
            }},
            {"required", json_invoke::json::array({"object_id"})},
        };
        const std::string source_tool = metadata.factory_tool_name.empty()
            ? "the matching create tool"
            : metadata.factory_tool_name;
        schema["description"] = "Use the handle returned by " + source_tool + ".";
        schema["x-cpp-type"] = "json_session_invoke::ObjectHandle";
        schema["x-llm-type"] = "object";
        schema["x-nullable"] = false;
        return schema;
    }

    Store object_store_{};
    std::unordered_map<std::string, StatefulToolMetadata> stateful_tools_;
    std::unordered_map<std::type_index, std::string> factory_tool_by_object_type_;
    std::unordered_map<std::type_index, std::string> destroy_tool_by_object_type_;
    std::unordered_map<std::type_index, std::vector<std::string>> method_tools_by_object_type_;
};

} // namespace json_session_invoke::detail