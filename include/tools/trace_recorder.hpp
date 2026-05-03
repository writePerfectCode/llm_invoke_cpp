#pragma once

#include <vector>

#include <json_invoke/json_trace.hpp>

namespace json_invoke {

class VectorTraceRecorder {
public:
    void record(const TraceEvent& event)
    {
        events_.push_back(event);
    }

    TraceSink sink()
    {
        return [this](const TraceEvent& event) {
            record(event);
        };
    }

    const std::vector<TraceEvent>& events() const noexcept
    {
        return events_;
    }

    void clear() noexcept
    {
        events_.clear();
    }

    json toJson() const
    {
        json value = json::array();
        for (const auto& event : events_)
        {
            value.push_back(traceEventToJson(event));
        }
        return value;
    }

private:
    std::vector<TraceEvent> events_;
};

} // namespace json_invoke