#pragma once

#include <string_view>

namespace json_invoke {

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

} // namespace json_invoke