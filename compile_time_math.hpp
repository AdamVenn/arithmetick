#pragma once
#include <concepts>
#include <cstddef>
#include <cstdint>

namespace compile_time_math
{
template <typename T>
concept arithmetic_concept = std::integral<T> || std::floating_point<T>;

template <arithmetic_concept CastType = double>
constexpr CastType round(double value)
{
    auto truncated = static_cast<std::int64_t>(value);
    double remainder = value - truncated;

    if (remainder >= 0.5)
        return static_cast<CastType>(truncated + 1);
    else if (remainder <= -0.5)
        return static_cast<CastType>(truncated - 1);
    else
        return static_cast<CastType>(truncated);
}

constexpr double floor(double value)
{
    int truncated = static_cast<int>(value);

    if (value >= 0.0 || value == truncated)
        return static_cast<double>(truncated);
    else
        return static_cast<double>(truncated - 1);
}

} // compile_time_math