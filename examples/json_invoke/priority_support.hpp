#pragma once

#include <array>
#include <string_view>
#include <utility>
#include <type_meta/enum_traits.hpp>

enum class Priority {
    low = 0,
    normal = 1,
    high = 2,
    critical = 3,
};

inline Priority maxPriority(Priority lhs, Priority rhs) noexcept
{
    return static_cast<int>(lhs) >= static_cast<int>(rhs) ? lhs : rhs;
}

inline Priority recommendIncidentPriority(
    Priority requested_priority,
    bool customer_blocked,
    bool production_impact,
    int affected_users)
{
    Priority recommended = requested_priority;

    if (affected_users >= 20)
    {
        recommended = maxPriority(recommended, Priority::normal);
    }

    if (customer_blocked || affected_users >= 100)
    {
        recommended = maxPriority(recommended, Priority::high);
    }

    if (production_impact || affected_users >= 500)
    {
        recommended = Priority::critical;
    }

    return recommended;
}

namespace func_registry {

template<>
struct enum_traits<Priority> {
    static constexpr std::array<std::pair<Priority, std::string_view>, 4> entries{{
        {Priority::low, "low"},
        {Priority::normal, "normal"},
        {Priority::high, "high"},
        {Priority::critical, "critical"},
    }};
};

} // namespace func_registry