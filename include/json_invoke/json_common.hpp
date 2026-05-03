#pragma once

#include <chrono>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <nlohmann/json.hpp>

namespace json_invoke {

using json = nlohmann::json;

enum class ToolExecutionSemantics {
    unknown,
    read_only,
    mutating,
};

inline constexpr std::string_view toolExecutionSemanticsName(ToolExecutionSemantics semantics) noexcept
{
    switch (semantics)
    {
    case ToolExecutionSemantics::read_only:
        return "read_only";
    case ToolExecutionSemantics::mutating:
        return "mutating";
    default:
        return "unknown";
    }
}

enum class TraceEventKind {
    invoke_started,
    invoke_finished,
    invoke_failed,
    object_created,
    object_destroyed,
    object_expired,
};

inline constexpr std::string_view traceEventKindName(TraceEventKind kind) noexcept
{
    switch (kind)
    {
    case TraceEventKind::invoke_started:
        return "invoke_started";
    case TraceEventKind::invoke_finished:
        return "invoke_finished";
    case TraceEventKind::invoke_failed:
        return "invoke_failed";
    case TraceEventKind::object_created:
        return "object_created";
    case TraceEventKind::object_destroyed:
        return "object_destroyed";
    case TraceEventKind::object_expired:
        return "object_expired";
    default:
        return "unknown";
    }
}

struct TraceEvent {
    TraceEventKind kind{TraceEventKind::invoke_started};
    std::chrono::system_clock::time_point timestamp{};
    std::string tool_name;
    json payload = json::object();
};

using TraceSink = std::function<void(const TraceEvent&)>;

class JsonInvokeError : public std::runtime_error {
public:
    JsonInvokeError(std::string code, std::string message)
        : std::runtime_error(std::move(message)), code_(std::move(code))
    {
    }

    const std::string& code() const noexcept
    {
        return code_;
    }

private:
    std::string code_;
};

} // namespace json_invoke