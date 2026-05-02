#pragma once

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