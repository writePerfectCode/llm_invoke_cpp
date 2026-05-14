#pragma once

#include <optional>
#include <string>

#include <json_session_invoke/json_session_invoke.hpp>

namespace task_scheduler {

using json_session_invoke::JsonInvokeError;
using json_session_invoke::json;

enum class SchedulingCategory {
    FreeReadOnly,
    ObjectExclusive,
    FactoryLane,
    ToolExclusive,
    SessionBarrier,
};

struct SchedulingKey {
    std::string value;
};

struct TaskExecutionPlan {
    std::string tool_name;
    SchedulingCategory category{SchedulingCategory::SessionBarrier};
    std::optional<SchedulingKey> scheduling_key;
};

template<bool EnableThreadSafety = false>
class BasicRequestClassifier {
public:
    using AdapterType = json_session_invoke::BasicJsonSessionInvokeAdapter<EnableThreadSafety>;
    using ToolSchedulingScope = typename AdapterType::ToolSchedulingScope;
    using ToolMetadataView = typename AdapterType::ToolMetadataView;
    using ToolStatefulKind = typename AdapterType::ToolStatefulKind;

    explicit BasicRequestClassifier(const AdapterType& adapter)
        : adapter_(adapter)
    {
    }

    TaskExecutionPlan classify(const json& request) const
    {
        if (!request.is_object())
        {
            throw JsonInvokeError("invalid_request", "request must be a JSON object");
        }

        const auto name_it = request.find("name");
        if (name_it == request.end() || !name_it->is_string())
        {
            throw JsonInvokeError("invalid_request", "request must contain a string field 'name'");
        }

        const auto args_it = request.find("args");
        const json default_args = json::object();
        const json& args = args_it != request.end() ? *args_it : default_args;
        return classifyParsedRequest(name_it->get_ref<const std::string&>(), args);
    }

private:
    TaskExecutionPlan classifyParsedRequest(const std::string& tool_name, const json& args) const
    {
        const auto metadata = adapter_.findToolMetadata(tool_name);
        if (!metadata.has_value())
        {
            throw JsonInvokeError("function_not_found", "function not found: " + tool_name);
        }

        TaskExecutionPlan plan;
        plan.tool_name = tool_name;

        const auto object_key = extractObjectSchedulingKey(args, *metadata);
        if (metadata->scheduling_scope != ToolSchedulingScope::infer_default)
        {
            return classifyWithSchedulingHints(std::move(plan), *metadata, object_key);
        }

        if (metadata->stateful_kind == ToolStatefulKind::factory)
        {
            plan.category = SchedulingCategory::FactoryLane;
            plan.scheduling_key = makeFactoryKey(*metadata);
            return plan;
        }

        if (object_key.has_value())
        {
            plan.category = SchedulingCategory::ObjectExclusive;
            plan.scheduling_key = std::move(object_key);
            return plan;
        }

        if (metadata->execution_semantics == json_invoke::ToolExecutionSemantics::read_only)
        {
            plan.category = SchedulingCategory::FreeReadOnly;
            return plan;
        }

        if (metadata->stateful_kind == ToolStatefulKind::none)
        {
            plan.category = SchedulingCategory::ToolExclusive;
            plan.scheduling_key = makeToolKey(tool_name);
            return plan;
        }

        return classifyMissingObjectKey(std::move(plan), *metadata, MissingKeyPolicy::fallback_to_tool_exclusive);
    }

    enum class MissingKeyPolicy {
        reject_request,
        fallback_to_tool_exclusive,
        fallback_to_session_barrier,
    };

    TaskExecutionPlan classifyWithSchedulingHints(
        TaskExecutionPlan plan,
        const ToolMetadataView& metadata,
        const std::optional<SchedulingKey>& object_key) const
    {
        switch (metadata.scheduling_scope)
        {
        case ToolSchedulingScope::infer_default:
            return plan;

        case ToolSchedulingScope::free_read_only:
            plan.category = SchedulingCategory::FreeReadOnly;
            return plan;

        case ToolSchedulingScope::object_lane:
            if (object_key.has_value())
            {
                plan.category = SchedulingCategory::ObjectExclusive;
                plan.scheduling_key = *object_key;
                return plan;
            }

            return classifyMissingObjectKey(std::move(plan), metadata, MissingKeyPolicy::reject_request);

        case ToolSchedulingScope::factory_lane:
            plan.category = SchedulingCategory::FactoryLane;
            plan.scheduling_key = makeFactoryKey(metadata);
            return plan;

        case ToolSchedulingScope::tool_exclusive:
            plan.category = SchedulingCategory::ToolExclusive;
            plan.scheduling_key = makeToolKey(plan.tool_name);
            return plan;

        case ToolSchedulingScope::session_barrier:
            plan.category = SchedulingCategory::SessionBarrier;
            return plan;
        }

        return plan;
    }

    TaskExecutionPlan classifyMissingObjectKey(
        TaskExecutionPlan plan,
        const ToolMetadataView& metadata,
        MissingKeyPolicy default_policy) const
    {
        switch (default_policy)
        {
        case MissingKeyPolicy::fallback_to_tool_exclusive:
            plan.category = SchedulingCategory::ToolExclusive;
            plan.scheduling_key = makeToolKey(plan.tool_name);
            return plan;

        case MissingKeyPolicy::fallback_to_session_barrier:
            plan.category = SchedulingCategory::SessionBarrier;
            return plan;

        case MissingKeyPolicy::reject_request:
            throw JsonInvokeError(
                "invalid_request",
                "request must contain a handle with non-empty object_id in field '" + metadata.handle_parameter_name + "'");
        }

        return plan;
    }

    static SchedulingKey makeToolKey(const std::string& tool_name)
    {
        return SchedulingKey{tool_name};
    }

    static SchedulingKey makeFactoryKey(const ToolMetadataView& metadata)
    {
        return SchedulingKey{metadata.object_type_name.empty() ? metadata.tool_name : metadata.object_type_name};
    }

    static std::optional<SchedulingKey> extractObjectSchedulingKey(const json& args, const ToolMetadataView& metadata)
    {
        if (metadata.stateful_kind != ToolStatefulKind::method && metadata.stateful_kind != ToolStatefulKind::destroy)
        {
            return std::nullopt;
        }

        if (!args.is_object())
        {
            return std::nullopt;
        }

        const auto handle_it = args.find(metadata.handle_parameter_name);
        if (handle_it == args.end())
        {
            return std::nullopt;
        }

        if (handle_it->is_string())
        {
            return SchedulingKey{handle_it->template get<std::string>()};
        }

        if (!handle_it->is_object())
        {
            return std::nullopt;
        }

        const auto object_id_it = handle_it->find("object_id");
        if (object_id_it == handle_it->end() || !object_id_it->is_string())
        {
            return std::nullopt;
        }

        const std::string object_id = object_id_it->template get<std::string>();
        if (object_id.empty())
        {
            return std::nullopt;
        }

        return SchedulingKey{object_id};
    }

    const AdapterType& adapter_;
};

using RequestClassifierThreadSafe = BasicRequestClassifier<true>;
using RequestClassifierUnsafe = BasicRequestClassifier<false>;

} // namespace task_scheduler