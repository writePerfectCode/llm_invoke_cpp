#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <functional>
#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

#include "json_error.hpp"

namespace json_invoke {

using json = nlohmann::json;

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
    std::string request_id;
    std::string tool_name;
    std::optional<std::chrono::microseconds> duration;
    json payload = json::object();
};

using TraceSink = std::function<void(const TraceEvent&)>;

inline double traceDurationMilliseconds(std::chrono::microseconds duration) noexcept
{
    return static_cast<double>(duration.count()) / 1000.0;
}

inline std::string formatTraceTimestamp(std::chrono::system_clock::time_point timestamp)
{
    const auto timestamp_time = std::chrono::system_clock::to_time_t(timestamp);
    std::tm local_time{};
#if defined(_WIN32)
    localtime_s(&local_time, &timestamp_time);
#else
    localtime_r(&timestamp_time, &local_time);
#endif

    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(timestamp.time_since_epoch()) % 1000;

    std::ostringstream builder;
    builder << std::put_time(&local_time, "%Y-%m-%dT%H:%M:%S")
            << '.'
            << std::setw(3) << std::setfill('0') << millis.count();
    return builder.str();
}

inline json traceEventToJson(const TraceEvent& event)
{
    json value{
        {"event", traceEventKindName(event.kind)},
        {"timestamp", formatTraceTimestamp(event.timestamp)},
        {"payload", event.payload},
    };

    if (!event.request_id.empty())
    {
        value["request_id"] = event.request_id;
    }

    if (!event.tool_name.empty())
    {
        value["tool_name"] = event.tool_name;
    }

    if (event.duration.has_value())
    {
        value["duration_ms"] = traceDurationMilliseconds(*event.duration);
    }

    return value;
}

namespace detail {

struct ActiveTraceContext {
    std::string request_id;
    std::chrono::steady_clock::time_point started_at{};
};

inline ActiveTraceContext*& activeTraceContextSlot() noexcept
{
    static thread_local ActiveTraceContext* active_context = nullptr;
    return active_context;
}

inline const ActiveTraceContext* activeTraceContext() noexcept
{
    return activeTraceContextSlot();
}

class ScopedTraceContext {
public:
    explicit ScopedTraceContext(std::string request_id)
        : context_{std::move(request_id), std::chrono::steady_clock::now()}
        , previous_(activeTraceContextSlot())
    {
        activeTraceContextSlot() = &context_;
    }

    ScopedTraceContext(const ScopedTraceContext&) = delete;
    ScopedTraceContext& operator=(const ScopedTraceContext&) = delete;

    ScopedTraceContext(ScopedTraceContext&& other) noexcept
        : context_(std::move(other.context_))
        , previous_(other.previous_)
        , active_(other.active_)
    {
        if (active_)
        {
            activeTraceContextSlot() = &context_;
            other.active_ = false;
        }
    }

    ScopedTraceContext& operator=(ScopedTraceContext&&) = delete;

    ~ScopedTraceContext() noexcept
    {
        if (active_)
        {
            activeTraceContextSlot() = previous_;
        }
    }

private:
    ActiveTraceContext context_;
    ActiveTraceContext* previous_{nullptr};
    bool active_{true};
};

inline std::string nextTraceRequestId()
{
    static std::atomic<std::uint64_t> next_id{0};
    const auto value = next_id.fetch_add(1, std::memory_order_relaxed) + 1;
    return "req_" + std::to_string(value);
}

} // namespace detail

inline TraceEvent makeTraceEvent(TraceEventKind kind, std::string tool_name = {}, json payload = json::object())
{
    TraceEvent event;
    event.kind = kind;
    event.timestamp = std::chrono::system_clock::now();
    event.tool_name = std::move(tool_name);
    event.payload = std::move(payload);

    if (const auto* context = detail::activeTraceContext(); context != nullptr)
    {
        event.request_id = context->request_id;
        event.duration = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - context->started_at);
    }

    return event;
}

namespace detail {

class RequestTraceScope {
public:
    RequestTraceScope() = default;

    explicit RequestTraceScope(ScopedTraceContext context)
        : context_(std::move(context))
    {
    }

    RequestTraceScope(const RequestTraceScope&) = delete;
    RequestTraceScope& operator=(const RequestTraceScope&) = delete;
    RequestTraceScope(RequestTraceScope&&) noexcept = default;
    RequestTraceScope& operator=(RequestTraceScope&&) noexcept = default;

private:
    std::optional<ScopedTraceContext> context_;
};

} // namespace detail

class TraceDispatcher {
public:
    using RequestScope = detail::RequestTraceScope;

    void setSink(TraceSink sink)
    {
        sink_ = std::move(sink);
    }

    const TraceSink& sink() const noexcept
    {
        return sink_;
    }

    bool enabled() const noexcept
    {
        return static_cast<bool>(sink_);
    }

    RequestScope beginRequest() const
    {
        if (!enabled())
        {
            return RequestScope{};
        }

        return RequestScope{detail::ScopedTraceContext(detail::nextTraceRequestId())};
    }

    template<typename PayloadFactory>
    void emitLazy(TraceEventKind kind, const std::string& tool_name, PayloadFactory&& payload_factory) const
    {
        if (!enabled())
        {
            return;
        }

        emit(kind, tool_name, std::forward<PayloadFactory>(payload_factory)());
    }

    void emit(TraceEventKind kind, const std::string& tool_name, json payload) const
    {
        if (!enabled())
        {
            return;
        }

        sink_(makeTraceEvent(kind, tool_name, std::move(payload)));
    }

    void emit(const TraceEvent& event) const
    {
        if (!enabled())
        {
            return;
        }

        sink_(event);
    }

private:
    TraceSink sink_{};
};

} // namespace json_invoke