#pragma once
#include <concepts>
#include <cstdint>
#include <limits>

namespace compile_time_math
{

#ifdef __cpp_lib_saturation_arithmetic
using std::saturate_cast;
#else
template <std::integral Res, std::integral Tp>
constexpr Res saturate_cast(Tp x) noexcept
{
    if constexpr (std::is_signed_v<Res> == std::is_signed_v<Tp>)
    {
        if constexpr (std::numeric_limits<Res>::digits < std::numeric_limits<Tp>::digits)
        {
            if (x < std::numeric_limits<Res>::min())
                return std::numeric_limits<Res>::min();
            if (x > std::numeric_limits<Res>::max())
                return std::numeric_limits<Res>::max();
        }
    }
    else if constexpr (std::is_signed_v<Tp>)
    {
        if (x < 0)
            return 0;
        if (static_cast<std::make_unsigned_t<Tp>>(x) > std::numeric_limits<Res>::max())
            return std::numeric_limits<Res>::max();
    }
    else
    {
        if (x > static_cast<std::make_unsigned_t<Res>>(std::numeric_limits<Res>::max()))
            return std::numeric_limits<Res>::max();
    }
    return static_cast<Res>(x);
}
#endif

template <typename T>
concept arithmetic_concept = std::integral<T> || std::floating_point<T>;

template <arithmetic_concept CastType = double>
constexpr CastType round(double value)
{
    auto truncated = static_cast<double>(static_cast<std::int64_t>(value));
    double remainder{ value - truncated };

    if (remainder >= 0.5)
        return static_cast<CastType>(truncated + 1);
    else if (remainder <= -0.5)
        return static_cast<CastType>(truncated - 1);
    else
        return static_cast<CastType>(truncated);
}

constexpr double floor(double value)
{
    auto truncated = static_cast<double>(static_cast<std::int64_t>(value));

    if (value >= 0.0 || value == truncated)
        return truncated;
    else
        return truncated - 1.0;
}

} // compile_time_math