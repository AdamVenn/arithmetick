#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>

/* Tries to do math, but returns empty optionals if there are errors */
namespace try_to
{

// Only supporting 64-bit ints
static_assert(sizeof(std::int64_t) == sizeof(std::intmax_t));

// Deciding types for intermediate calculations
namespace next_signed
{

template <typename T>
struct next_signed_type;

template <>
struct next_signed_type<std::int8_t>
{
    using type = std::int16_t;
};

template <>
struct next_signed_type<std::uint8_t>
{
    using type = std::int16_t;
};

template <>
struct next_signed_type<std::int16_t>
{
    using type = std::int32_t;
};

template <>
struct next_signed_type<std::uint16_t>
{
    using type = std::int32_t;
};

template <>
struct next_signed_type<std::int32_t>
{
    using type = std::int64_t;
};

template <>
struct next_signed_type<std::uint32_t>
{
    using type = std::int64_t;
};

template <typename T>
using t = typename next_signed_type<T>::type;

}

// Deciding types for intermediate calculations
namespace next_unsigned
{

template <typename T>
struct next_unsigned;

template <>
struct next_unsigned<std::uint8_t>
{
    using type = std::uint16_t;
};

template <>
struct next_unsigned<std::uint16_t>
{
    using type = std::uint32_t;
};

template <>
struct next_unsigned<std::uint32_t>
{
    using type = std::int64_t;
};

template <typename T>
using t = typename next_unsigned<T>::type;

}

namespace larger_of
{
// Get the larger of two types
// If sizes are equal, prefer the signed type
template <typename T, typename U>
struct larger_of
{
    using type = std::conditional_t<
        (sizeof(T) > sizeof(U)),
        T,
        std::conditional_t<
            (sizeof(U) > sizeof(T)),
            U,
            std::conditional_t<std::is_signed_v<T>, T, U>>>;
};

template <typename T, typename U>
using t = typename larger_of<T, U>::type;

// Branch 1: T is strictly larger
static_assert(std::is_same_v<t<std::int64_t, std::uint32_t>, std::int64_t>);

// Branch 2: U is strictly larger
static_assert(std::is_same_v<t<uint32_t, int64_t>, int64_t>);

// Branch 3: Sizes equal, T is signed
static_assert(std::is_same_v<t<int32_t, uint32_t>, int32_t>);

// Branch 3: Sizes equal, U is signed
static_assert(std::is_same_v<t<uint32_t, int32_t>, int32_t>);

// Branch 3: Sizes equal, both signed
static_assert(std::is_same_v<t<int32_t, int32_t>, int32_t>);

// Branch 3: Sizes equal, both unsigned
static_assert(std::is_same_v<t<uint32_t, uint32_t>, uint32_t>);

} // namespace larger_of

// Check if an integer type is not bigger than another
template <typename T, typename U>
constexpr bool smaller_or_equal = sizeof(T) <= sizeof(U);

// Perform intermediate calculations.
// Use try_to:: instead, which figures out the safe intermediate type.
namespace intermediate
{
template <std::unsigned_integral IntermediateType>
constexpr std::optional<IntermediateType> subtract(IntermediateType lhs, IntermediateType rhs)
{
    // Underflow
    if (lhs < rhs)
        return {};

    return lhs - rhs;
}

template <std::signed_integral IntermediateType>
constexpr std::optional<IntermediateType> subtract(IntermediateType lhs, IntermediateType rhs)
{
    if (rhs > 0)
    {
        // Underflow
        const auto min_val = std::numeric_limits<IntermediateType>::lowest() + rhs;
        if (lhs < min_val)
            return {};
    }
    else
    {
        // Overflow
        const auto max_val = std::numeric_limits<IntermediateType>::max() + rhs;
        if (lhs > max_val)
            return {};
    }

    return lhs - rhs;
}
} // namespace intermediate

namespace detail
{
// Empty type used to allow deduction of return type
struct not_provided_t
{
};

template <std::integral ReturnType, std::integral L, std::integral R>
constexpr std::optional<ReturnType> subtract(L lhs, R rhs)
{
    auto try_cast_to_return = [](auto diff) -> std::optional<ReturnType> {
        if (!std::in_range<ReturnType>(diff))
            return {};
        return static_cast<ReturnType>(diff);
    };

    // An operand type can hold values outside the range of intmax
    // Try intermediate calculations in intmax and uintmax
    if constexpr (sizeof(L) == sizeof(std::uintmax_t) || sizeof(R) == sizeof(std::uintmax_t))
    {
        if (std::in_range<std::intmax_t>(lhs) && std::in_range<std::intmax_t>(rhs))
        {
            using IntermediateType = std::intmax_t;
            return intermediate::subtract<IntermediateType>(lhs, rhs)
                .and_then(try_cast_to_return);
        }
        if (std::in_range<std::uintmax_t>(lhs) && std::in_range<std::uintmax_t>(rhs))
        {
            using IntermediateType = std::uintmax_t;
            return intermediate::subtract<IntermediateType>(lhs, rhs)
                .and_then(try_cast_to_return);
        }
        else
        {
            return {};
        }
    }

    // Optimize for unsigned types
    else if constexpr (std::is_unsigned_v<ReturnType> && std::is_unsigned_v<L> && std::is_unsigned_v<R>)
    {
        if constexpr (smaller_or_equal<next_unsigned::t<larger_of::t<L, R>>, ReturnType>)
        {
            return intermediate::subtract<ReturnType>(lhs, rhs);
        }
        else
        {
            using IntermediateType = std::uintmax_t;
            return intermediate::subtract<IntermediateType>(lhs, rhs)
                .and_then(try_cast_to_return);
        }
    }

    // If we're using intmax anyway, do the intermediate calculations in intmax
    // This branch has to come now, since the next branch can check for an int larger than intmax
    else if constexpr (sizeof(ReturnType) == sizeof(std::intmax_t) || sizeof(L) == sizeof(std::intmax_t) || sizeof(R) == sizeof(std::intmax_t))
    {
        using IntermediateType = std::intmax_t;
        return intermediate::subtract<IntermediateType>(lhs, rhs)
            .and_then(try_cast_to_return);
    }

    // if the next signed type (one size up) after the larger type of L/R
    // is smaller than or equal to the size of the return type,
    // use the return type for intermediate calculations.
    // Intermediate calculations must use signed integer in case the value goes negative
    else if constexpr (smaller_or_equal<next_signed::t<larger_of::t<L, R>>, ReturnType> && std::is_signed_v<ReturnType>)
    {
        return intermediate::subtract<ReturnType>(lhs, rhs);
    }

    // Use smallest intermediate type which won't overflow
    else
    {
        using IntermediateType = next_signed::t<larger_of::t<L, larger_of::t<ReturnType, R>>>;
        return intermediate::subtract<IntermediateType>(lhs, rhs)
            .and_then(try_cast_to_return);
    }
}
} // detail

// Subtract rhs from lhs and return optional on integer overflow
// Choose your desired return type with the template argument
// or use the larger of the operands' types by default
template <typename ReturnType = detail::not_provided_t, std::integral L, std::integral R>
constexpr auto subtract(L lhs, R rhs)
{
    if constexpr (std::same_as<ReturnType, detail::not_provided_t>)
    {
        // eg. subtract(0, 0)
        // default return type: larger of L and R
        return detail::subtract<larger_of::t<L, R>>(lhs, rhs);
    }
    else
    {
        // eg. subtract<std::int16_t>(0, 0)
        // return type: std::int16_t
        return detail::subtract<ReturnType, L, R>(lhs, rhs);
    }
}

// Set to true for unit tests
#if false

/*
Test type combinations:

|           | int8_t | uint8_t | int16_t | uint16_t | int32_t | uint32_t | int64_t | uint64_t |
| --------- | ------ | ------- | ------- | -------- | ------- | -------- | ------- | -------- |
| deduced   |        |         |         |          |         |          |         |          |
| specified |        |         |         |          |         |          |         |          |
| L         |        |         |         |          |         |          |         |          |
| R         |        |         |         |          |         |          |         |          |

Failure branches:
 - Underflow Intermediate
 - Overflow Intermediate
 - Underflow Return Type
 - Overflow Return Type
 - Underflow intmax
 - Overflow intmax

*/

//  -- int8_t deduced, int8_t, int8_t --
static_assert(subtract(std::numeric_limits<std::int8_t>::lowest(), std::int8_t{ 1 }).has_value() == false);
static_assert(subtract(std::numeric_limits<std::int8_t>::max(), std::int8_t{ -1 }).has_value() == false);
static_assert(subtract(std::numeric_limits<std::int8_t>::max(), std::int8_t{ 1 }) == std::numeric_limits<std::int8_t>::max() - 1);
static_assert(subtract(std::int8_t{ 0 }, std::int8_t{ 1 }) == -1);

//  -- int8_t explicit, int8_t, int8_t --
static_assert(subtract<std::int8_t>(std::numeric_limits<std::int8_t>::lowest(), std::int8_t{ 1 }).has_value() == false);
static_assert(subtract<std::int8_t>(std::numeric_limits<std::int8_t>::max(), std::int8_t{ -1 }).has_value() == false);
static_assert(subtract<std::int8_t>(std::numeric_limits<std::int8_t>::max(), std::int8_t{ 1 }) == std::numeric_limits<std::int8_t>::max() - 1);
static_assert(subtract<std::int8_t>(std::int8_t{ 0 }, std::int8_t{ 1 }) == -1);

//  -- uint8_t explicit, int8_t, int8_t --
static_assert(subtract<std::uint8_t>(std::numeric_limits<std::int8_t>::lowest(), std::int8_t{ 1 }).has_value() == false);
static_assert(subtract<std::uint8_t>(std::numeric_limits<std::int8_t>::lowest(), std::int8_t{ -127 }).has_value() == false);
static_assert(subtract<std::uint8_t>(std::numeric_limits<std::int8_t>::lowest(), std::int8_t{ -128 }) == 0);
static_assert(subtract<std::uint8_t>(std::numeric_limits<std::int8_t>::max(), std::int8_t{ -1 }) == std::numeric_limits<std::int8_t>::max() + 1);
static_assert(subtract<std::uint8_t>(std::numeric_limits<std::int8_t>::max(), std::int8_t{ 1 }) == std::numeric_limits<std::int8_t>::max() - 1);
static_assert(subtract<std::uint8_t>(std::int8_t{ 0 }, std::int8_t{ 1 }).has_value() == false);

//  -- uint8_t explicit, uint8_t, int8_t --
static_assert(subtract<std::uint8_t>(std::uint8_t{ 0 }, std::int8_t{ -1 }) == std::uint8_t{ 1 });
static_assert(subtract<std::uint8_t>(std::numeric_limits<std::uint8_t>::max(), std::int8_t{ -1 }).has_value() == false);
static_assert(subtract<std::uint8_t>(std::numeric_limits<std::uint8_t>::max(), std::int8_t{ 1 }) == std::numeric_limits<std::uint8_t>::max() - 1);
static_assert(subtract<std::uint8_t>(std::uint8_t{ 0 }, std::int8_t{ 1 }).has_value() == false);

//  -- uint8_t explicit, int8_t, uint8_t --
static_assert(subtract<std::uint8_t>(std::int8_t{ 0 }, std::uint8_t{ 1 }).has_value() == false);
static_assert(subtract<std::uint8_t>(std::numeric_limits<std::int8_t>::max(), std::uint8_t{ 1 }) == std::numeric_limits<std::int8_t>::max() - 1);
static_assert(subtract<std::uint8_t>(std::numeric_limits<std::int8_t>::lowest(), std::uint8_t{ 1 }).has_value() == false);
static_assert(subtract<std::uint8_t>(std::numeric_limits<std::int8_t>::lowest(), std::uint8_t{ 1 }).has_value() == false);
static_assert(subtract<std::uint8_t>(std::int8_t{ 1 }, std::uint8_t{ 0 }) == std::uint8_t{ 1 });

//  -- uint8_t deduced, uint8_t, uint8_t --
static_assert(subtract(std::numeric_limits<std::uint8_t>::lowest(), std::uint8_t{ 1 }).has_value() == false);
static_assert(subtract(std::numeric_limits<std::uint8_t>::max(), std::uint8_t{ 1 }) == std::numeric_limits<std::uint8_t>::max() - 1);
static_assert(subtract(std::uint8_t{ 0 }, std::uint8_t{ 1 }).has_value() == false);
static_assert(subtract(std::uint8_t{ 5 }, std::uint8_t{ 3 }) == std::uint8_t{ 2 });

//  -- uint8_t explicit, uint8_t, uint8_t --
static_assert(subtract<std::uint8_t>(std::numeric_limits<std::uint8_t>::lowest(), std::uint8_t{ 1 }).has_value() == false);
static_assert(subtract<std::uint8_t>(std::numeric_limits<std::uint8_t>::max(), std::uint8_t{ 1 }) == std::numeric_limits<std::uint8_t>::max() - 1);
static_assert(subtract<std::uint8_t>(std::uint8_t{ 0 }, std::uint8_t{ 1 }).has_value() == false);
static_assert(subtract<std::uint8_t>(std::uint8_t{ 5 }, std::uint8_t{ 3 }) == std::uint8_t{ 2 });

//  -- int16_t deduced, int16_t, int16_t --
static_assert(subtract(std::numeric_limits<std::int16_t>::lowest(), std::int16_t{ 1 }).has_value() == false);
static_assert(subtract(std::numeric_limits<std::int16_t>::max(), std::int16_t{ -1 }).has_value() == false);
static_assert(subtract(std::numeric_limits<std::int16_t>::max(), std::int16_t{ 1 }) == std::numeric_limits<std::int16_t>::max() - 1);
static_assert(subtract(std::int16_t{ 0 }, std::int16_t{ 1 }) == -1);

//  -- int16_t explicit, int16_t, int16_t --
static_assert(subtract<std::int16_t>(std::numeric_limits<std::int16_t>::lowest(), std::int16_t{ 1 }).has_value() == false);
static_assert(subtract<std::int16_t>(std::numeric_limits<std::int16_t>::max(), std::int16_t{ -1 }).has_value() == false);
static_assert(subtract<std::int16_t>(std::numeric_limits<std::int16_t>::max(), std::int16_t{ 1 }) == std::numeric_limits<std::int16_t>::max() - 1);
static_assert(subtract<std::int16_t>(std::int16_t{ 0 }, std::int16_t{ 1 }) == -1);

//  -- uint16_t explicit, int16_t, int16_t --
static_assert(subtract<std::uint16_t>(std::numeric_limits<std::int16_t>::lowest(), std::int16_t{ 1 }).has_value() == false);
static_assert(subtract<std::uint16_t>(std::numeric_limits<std::int16_t>::lowest(), std::int16_t{ -32767 }).has_value() == false);
static_assert(subtract<std::uint16_t>(std::numeric_limits<std::int16_t>::lowest(), std::int16_t{ -32768 }) == 0);
static_assert(subtract<std::uint16_t>(std::numeric_limits<std::int16_t>::max(), std::int16_t{ -1 }) == std::numeric_limits<std::int16_t>::max() + 1);
static_assert(subtract<std::uint16_t>(std::numeric_limits<std::int16_t>::max(), std::int16_t{ 1 }) == std::numeric_limits<std::int16_t>::max() - 1);
static_assert(subtract<std::uint16_t>(std::int16_t{ 0 }, std::int16_t{ 1 }).has_value() == false);

//  -- uint16_t explicit, uint16_t, int16_t --
static_assert(subtract<std::uint16_t>(std::uint16_t{ 0 }, std::int16_t{ -1 }) == std::uint16_t{ 1 });
static_assert(subtract<std::uint16_t>(std::numeric_limits<std::uint16_t>::max(), std::int16_t{ -1 }).has_value() == false);
static_assert(subtract<std::uint16_t>(std::numeric_limits<std::uint16_t>::max(), std::int16_t{ 1 }) == std::numeric_limits<std::uint16_t>::max() - 1);
static_assert(subtract<std::uint16_t>(std::uint16_t{ 0 }, std::int16_t{ 1 }).has_value() == false);

//  -- uint16_t explicit, int16_t, uint16_t --
static_assert(subtract<std::uint16_t>(std::int16_t{ 0 }, std::uint16_t{ 1 }).has_value() == false);
static_assert(subtract<std::uint16_t>(std::numeric_limits<std::int16_t>::max(), std::uint16_t{ 1 }) == std::numeric_limits<std::int16_t>::max() - 1);
static_assert(subtract<std::uint16_t>(std::numeric_limits<std::uint16_t>::max(), std::numeric_limits<std::uint16_t>::max()) == 0u);
static_assert(subtract<std::uint16_t>(std::numeric_limits<std::int16_t>::lowest(), std::uint16_t{ 1 }).has_value() == false);
static_assert(subtract<std::uint16_t>(std::int16_t{ 1 }, std::uint16_t{ 0 }) == std::uint16_t{ 1 });

//  -- uint16_t deduced, uint16_t, uint16_t --
static_assert(subtract(std::numeric_limits<std::uint16_t>::lowest(), std::uint16_t{ 1 }).has_value() == false);
static_assert(subtract(std::numeric_limits<std::uint16_t>::max(), std::uint16_t{ 1 }) == std::numeric_limits<std::uint16_t>::max() - 1);
static_assert(subtract(std::uint16_t{ 0 }, std::uint16_t{ 1 }).has_value() == false);
static_assert(subtract(std::uint16_t{ 1000 }, std::uint16_t{ 500 }) == std::uint16_t{ 500 });

//  -- uint16_t explicit, uint16_t, uint16_t --
static_assert(subtract<std::uint16_t>(std::numeric_limits<std::uint16_t>::lowest(), std::uint16_t{ 1 }).has_value() == false);
static_assert(subtract<std::uint16_t>(std::numeric_limits<std::uint16_t>::max(), std::uint16_t{ 1 }) == std::numeric_limits<std::uint16_t>::max() - 1);
static_assert(subtract<std::uint16_t>(std::uint16_t{ 0 }, std::uint16_t{ 1 }).has_value() == false);
static_assert(subtract<std::uint16_t>(std::uint16_t{ 1000 }, std::uint16_t{ 500 }) == std::uint16_t{ 500 });

//  -- int32_t deduced, int32_t, int32_t --
static_assert(subtract(std::numeric_limits<std::int32_t>::lowest(), std::int32_t{ 1 }).has_value() == false);
static_assert(subtract(std::numeric_limits<std::int32_t>::max(), std::int32_t{ -1 }).has_value() == false);
static_assert(subtract(std::numeric_limits<std::int32_t>::max(), std::int32_t{ 1 }) == std::numeric_limits<std::int32_t>::max() - 1);
static_assert(subtract(std::int32_t{ 0 }, std::int32_t{ 1 }) == -1);

//  -- int32_t explicit, int32_t, int32_t --
static_assert(subtract<std::int32_t>(std::numeric_limits<std::int32_t>::lowest(), std::int32_t{ 1 }).has_value() == false);
static_assert(subtract<std::int32_t>(std::numeric_limits<std::int32_t>::max(), std::int32_t{ -1 }).has_value() == false);
static_assert(subtract<std::int32_t>(std::numeric_limits<std::int32_t>::max(), std::int32_t{ 1 }) == std::numeric_limits<std::int32_t>::max() - 1);
static_assert(subtract<std::int32_t>(std::int32_t{ 0 }, std::int32_t{ 1 }) == -1);

//  -- uint32_t explicit, int32_t, int32_t --
static_assert(subtract<std::uint32_t>(std::numeric_limits<std::int32_t>::lowest(), std::int32_t{ 1 }).has_value() == false);
static_assert(subtract<std::uint32_t>(std::numeric_limits<std::int32_t>::lowest(), std::int32_t{ -2147483647 }).has_value() == false);
static_assert(subtract<std::uint32_t>(std::numeric_limits<std::int32_t>::lowest(), std::int32_t{ -2147483648 }) == 0);
static_assert(subtract<std::uint32_t>(std::numeric_limits<std::int32_t>::max(), std::int32_t{ -1 }) == static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()) + 1);
static_assert(subtract<std::uint32_t>(std::numeric_limits<std::int32_t>::max(), std::int32_t{ 1 }) == std::numeric_limits<std::int32_t>::max() - 1);
static_assert(subtract<std::uint32_t>(std::int32_t{ 0 }, std::int32_t{ 1 }).has_value() == false);

//  -- uint32_t explicit, uint32_t, int32_t --
static_assert(subtract<std::uint32_t>(std::uint32_t{ 0 }, std::int32_t{ -1 }) == std::uint32_t{ 1 });
static_assert(subtract<std::uint32_t>(std::numeric_limits<std::uint32_t>::max(), std::int32_t{ -1 }).has_value() == false);
static_assert(subtract<std::uint32_t>(std::numeric_limits<std::uint32_t>::max(), std::int32_t{ 1 }) == std::numeric_limits<std::uint32_t>::max() - 1);
static_assert(subtract<std::uint32_t>(std::uint32_t{ 0 }, std::int32_t{ 1 }).has_value() == false);

//  -- uint32_t explicit, int32_t, uint32_t --
static_assert(subtract<std::uint32_t>(std::int32_t{ 0 }, std::uint32_t{ 1 }).has_value() == false);
static_assert(subtract<std::uint32_t>(std::numeric_limits<std::int32_t>::max(), std::uint32_t{ 1 }) == std::numeric_limits<std::int32_t>::max() - 1);
static_assert(subtract<std::uint32_t>(std::numeric_limits<std::int32_t>::lowest(), std::uint32_t{ 1 }).has_value() == false);
static_assert(subtract<std::uint32_t>(std::int32_t{ 1 }, std::uint32_t{ 0 }) == std::uint32_t{ 1 });

//  -- uint32_t deduced, uint32_t, uint32_t --
static_assert(subtract(std::numeric_limits<std::uint32_t>::lowest(), std::uint32_t{ 1 }).has_value() == false);
static_assert(subtract(std::numeric_limits<std::uint32_t>::max(), std::uint32_t{ 1 }) == std::numeric_limits<std::uint32_t>::max() - 1);
static_assert(subtract(std::uint32_t{ 0 }, std::uint32_t{ 1 }).has_value() == false);
static_assert(subtract(std::uint32_t{ 1000000 }, std::uint32_t{ 500000 }) == std::uint32_t{ 500000 });

//  -- uint32_t explicit, uint32_t, uint32_t --
static_assert(subtract<std::uint32_t>(std::numeric_limits<std::uint32_t>::lowest(), std::uint32_t{ 1 }).has_value() == false);
static_assert(subtract<std::uint32_t>(std::numeric_limits<std::uint32_t>::max(), std::uint32_t{ 1 }) == std::numeric_limits<std::uint32_t>::max() - 1);
static_assert(subtract<std::uint32_t>(std::uint32_t{ 0 }, std::uint32_t{ 1 }).has_value() == false);
static_assert(subtract<std::uint32_t>(std::uint32_t{ 1000000 }, std::uint32_t{ 500000 }) == std::uint32_t{ 500000 });

//  -- int64_t deduced, int64_t, int64_t --
static_assert(subtract(std::numeric_limits<std::int64_t>::lowest(), std::int64_t{ 1 }).has_value() == false);
static_assert(subtract(std::numeric_limits<std::int64_t>::max(), std::int64_t{ -1 }).has_value() == false);
static_assert(subtract(std::numeric_limits<std::int64_t>::max(), std::int64_t{ 1 }) == std::numeric_limits<std::int64_t>::max() - 1);
static_assert(subtract(std::int64_t{ 0 }, std::int64_t{ 1 }) == -1);

//  -- int64_t explicit, int64_t, int64_t --
static_assert(subtract<std::int64_t>(std::numeric_limits<std::int64_t>::lowest(), std::int64_t{ 1 }).has_value() == false);
static_assert(subtract<std::int64_t>(std::numeric_limits<std::int64_t>::max(), std::int64_t{ -1 }).has_value() == false);
static_assert(subtract<std::int64_t>(std::numeric_limits<std::int64_t>::max(), std::int64_t{ 1 }) == std::numeric_limits<std::int64_t>::max() - 1);
static_assert(subtract<std::int64_t>(std::numeric_limits<std::int64_t>::max(), std::numeric_limits<std::int64_t>::max()) == 0);
static_assert(subtract<std::int64_t>(std::int64_t{ 0 }, std::int64_t{ 1 }) == -1);

//  -- uint64_t explicit, int64_t, int64_t --
static_assert(subtract<std::uint64_t>(std::numeric_limits<std::int64_t>::lowest(), std::int64_t{ 1 }).has_value() == false);
static_assert(subtract<std::uint64_t>(std::numeric_limits<std::int64_t>::lowest(), std::int64_t{ -9223372036854775807 }).has_value() == false);
static_assert(subtract<std::uint64_t>(std::numeric_limits<std::int64_t>::lowest(), std::int64_t{ -9223372036854775807 } - 1) == 0);
static_assert(subtract<std::uint64_t>(std::numeric_limits<std::int64_t>::max(), std::int64_t{ -1 }).has_value() == false);
static_assert(subtract<std::uint64_t>(std::numeric_limits<std::int64_t>::max(), std::int64_t{ 1 }) == std::numeric_limits<std::int64_t>::max() - 1);
static_assert(subtract<std::uint64_t>(std::int64_t{ 0 }, std::int64_t{ 1 }).has_value() == false);

//  -- uint64_t explicit, uint64_t, int64_t --
static_assert(subtract<std::uint64_t>(std::uint64_t{ 0 }, std::int64_t{ -1 }) == std::uint64_t{ 1 });
static_assert(subtract<std::uint64_t>(std::numeric_limits<std::uint64_t>::max(), std::int64_t{ -1 }).has_value() == false);
static_assert(subtract<std::uint64_t>(std::numeric_limits<std::uint64_t>::max(), std::int64_t{ 1 }) == std::numeric_limits<std::uint64_t>::max() - 1);
static_assert(subtract<std::uint64_t>(std::uint64_t{ 0 }, std::int64_t{ 1 }).has_value() == false);

//  -- uint64_t explicit, int64_t, uint64_t --
static_assert(subtract<std::uint64_t>(std::int64_t{ 0 }, std::uint64_t{ 1 }).has_value() == false);
static_assert(subtract<std::uint64_t>(std::numeric_limits<std::int64_t>::max(), std::uint64_t{ 1 }) == std::numeric_limits<std::int64_t>::max() - 1);
static_assert(subtract<std::uint64_t>(std::numeric_limits<std::int64_t>::lowest(), std::uint64_t{ 1 }).has_value() == false);
static_assert(subtract<std::uint64_t>(std::int64_t{ 1 }, std::uint64_t{ 0 }) == std::uint64_t{ 1 });

//  -- uint64_t deduced, uint64_t, uint64_t --
static_assert(subtract(std::numeric_limits<std::uint64_t>::lowest(), std::uint64_t{ 1 }).has_value() == false);
static_assert(subtract(std::numeric_limits<std::uint64_t>::max(), std::uint64_t{ 1 }) == std::numeric_limits<std::uint64_t>::max() - 1);
static_assert(subtract(std::uint64_t{ 0 }, std::uint64_t{ 1 }).has_value() == false);
static_assert(subtract(std::uint64_t{ 1000000000000 }, std::uint64_t{ 500000000000 }) == std::uint64_t{ 500000000000 });

//  -- uint64_t explicit, uint64_t, uint64_t --
static_assert(subtract<std::uint64_t>(std::numeric_limits<std::uint64_t>::lowest(), std::uint64_t{ 1 }).has_value() == false);
static_assert(subtract<std::uint64_t>(std::numeric_limits<std::uint64_t>::max(), std::uint64_t{ 1 }) == std::numeric_limits<std::uint64_t>::max() - 1);
static_assert(subtract<std::uint64_t>(std::uint64_t{ 0 }, std::uint64_t{ 1 }).has_value() == false);
static_assert(subtract<std::uint64_t>(std::uint64_t{ 1000000000000 }, std::uint64_t{ 500000000000 }) == std::uint64_t{ 500000000000 });

// Combinations...

// return=int8_t L=int8_t R=int16_t
// return=int8_t L=int8_t R=uint16_t
// return=int8_t L=int8_t R=int32_t
static_assert(subtract<std::int8_t>(std::numeric_limits<std::int8_t>::lowest(), 0) == std::numeric_limits<std::int8_t>::lowest());

// return=int8_t L=int8_t R=uint32_t

// return=int8_t L=int8_t R=int64_t
// return=int8_t L=int8_t R=uint64_t
// return=int8_t L=uint8_t R=int8_t
// return=int8_t L=uint8_t R=uint8_t
static_assert(subtract<std::int8_t>(std::uint8_t{ 200 }, std::uint8_t{ 0 }).has_value() == false);

// return=int8_t L=uint8_t R=int16_t
// return=int8_t L=uint8_t R=uint16_t
// return=int8_t L=uint8_t R=int32_t
// return=int8_t L=uint8_t R=uint32_t
// return=int8_t L=uint8_t R=int64_t
// return=int8_t L=uint8_t R=uint64_t
// return=int8_t L=int16_t R=int8_t
// return=int8_t L=int16_t R=uint8_t
// return=int8_t L=int16_t R=int16_t
// return=int8_t L=int16_t R=uint16_t
// return=int8_t L=int16_t R=int32_t
// return=int8_t L=int16_t R=uint32_t
// return=int8_t L=int16_t R=int64_t
// return=int8_t L=int16_t R=uint64_t
// return=int8_t L=uint16_t R=int8_t
// return=int8_t L=uint16_t R=uint8_t
// return=int8_t L=uint16_t R=int16_t
// return=int8_t L=uint16_t R=uint16_t
// return=int8_t L=uint16_t R=int32_t
// return=int8_t L=uint16_t R=uint32_t
// return=int8_t L=uint16_t R=int64_t
// return=int8_t L=uint16_t R=uint64_t
// return=int8_t L=int32_t R=int8_t
// return=int8_t L=int32_t R=uint8_t
// return=int8_t L=int32_t R=int16_t
// return=int8_t L=int32_t R=uint16_t
// return=int8_t L=int32_t R=int32_t
// return=int8_t L=int32_t R=uint32_t
// return=int8_t L=int32_t R=int64_t
// return=int8_t L=int32_t R=uint64_t
// return=int8_t L=uint32_t R=int8_t
// return=int8_t L=uint32_t R=uint8_t
// return=int8_t L=uint32_t R=int16_t
// return=int8_t L=uint32_t R=uint16_t
// return=int8_t L=uint32_t R=int32_t

// return=int8_t L=uint32_t R=uint32_t
// return=int8_t L=uint32_t R=int64_t
// return=int8_t L=uint32_t R=uint64_t
// return=int8_t L=int64_t R=int8_t
// return=int8_t L=int64_t R=uint8_t
// return=int8_t L=int64_t R=int16_t
// return=int8_t L=int64_t R=uint16_t
// return=int8_t L=int64_t R=int32_t
// return=int8_t L=int64_t R=uint32_t
// return=int8_t L=int64_t R=int64_t
// return=int8_t L=int64_t R=uint64_t
// return=int8_t L=uint64_t R=int8_t
// return=int8_t L=uint64_t R=uint8_t
// return=int8_t L=uint64_t R=int16_t
// return=int8_t L=uint64_t R=uint16_t
// return=int8_t L=uint64_t R=int32_t
// return=int8_t L=uint64_t R=uint32_t
// return=int8_t L=uint64_t R=int64_t
// return=int8_t L=uint64_t R=uint64_t

// return=uint8_t L=int8_t R=int8_t
static_assert(subtract<std::uint8_t>(std::int8_t{ 100 }, std::int8_t{ 50 }) == 50u);
static_assert(subtract<std::uint8_t>(std::int8_t{ 10 }, std::int8_t{ 20 }).has_value() == false); // negative result

// return=uint8_t L=int8_t R=uint8_t
// return=uint8_t L=int8_t R=int16_t
// return=uint8_t L=int8_t R=uint16_t
// return=uint8_t L=int8_t R=int32_t
// return=uint8_t L=int8_t R=uint32_t
// return=uint8_t L=int8_t R=int64_t
// return=uint8_t L=int8_t R=uint64_t
// return=uint8_t L=uint8_t R=int8_t
// return=uint8_t L=uint8_t R=int16_t
// return=uint8_t L=uint8_t R=uint16_t
// return=uint8_t L=uint8_t R=int32_t
// return=uint8_t L=uint8_t R=uint32_t
// return=uint8_t L=uint8_t R=int64_t
// return=uint8_t L=uint8_t R=uint64_t
// return=uint8_t L=int16_t R=int8_t
// return=uint8_t L=int16_t R=uint8_t
// return=uint8_t L=int16_t R=int16_t
// return=uint8_t L=int16_t R=uint16_t
// return=uint8_t L=int16_t R=int32_t
// return=uint8_t L=int16_t R=uint32_t
// return=uint8_t L=int16_t R=int64_t
// return=uint8_t L=int16_t R=uint64_t
// return=uint8_t L=uint16_t R=int8_t
// return=uint8_t L=uint16_t R=uint8_t
// return=uint8_t L=uint16_t R=int16_t
// return=uint8_t L=uint16_t R=uint16_t
// return=uint8_t L=uint16_t R=int32_t
// return=uint8_t L=uint16_t R=uint32_t
// return=uint8_t L=uint16_t R=int64_t
// return=uint8_t L=uint16_t R=uint64_t
// return=uint8_t L=int32_t R=int8_t
// return=uint8_t L=int32_t R=uint8_t
// return=uint8_t L=int32_t R=int16_t
// return=uint8_t L=int32_t R=uint16_t

// return=uint8_t L=int32_t R=int32_t
static_assert(subtract<std::uint8_t>(0, 128).has_value() == false);

// return=uint8_t L=int32_t R=uint32_t
// return=uint8_t L=int32_t R=int64_t
// return=uint8_t L=int32_t R=uint64_t
// return=uint8_t L=uint32_t R=int8_t
// return=uint8_t L=uint32_t R=uint8_t
// return=uint8_t L=uint32_t R=int16_t
// return=uint8_t L=uint32_t R=uint16_t
// return=uint8_t L=uint32_t R=int32_t
// return=uint8_t L=uint32_t R=uint32_t
static_assert(subtract<std::uint8_t>(0u, 0u) == 0u);

// return=uint8_t L=uint32_t R=int64_t
// return=uint8_t L=uint32_t R=uint64_t
// return=uint8_t L=int64_t R=int8_t
// return=uint8_t L=int64_t R=uint8_t
// return=uint8_t L=int64_t R=int16_t
// return=uint8_t L=int64_t R=uint16_t
// return=uint8_t L=int64_t R=int32_t
// return=uint8_t L=int64_t R=uint32_t
// return=uint8_t L=int64_t R=int64_t
// return=uint8_t L=int64_t R=uint64_t
// return=uint8_t L=uint64_t R=int8_t
// return=uint8_t L=uint64_t R=uint8_t
// return=uint8_t L=uint64_t R=int16_t
// return=uint8_t L=uint64_t R=uint16_t
// return=uint8_t L=uint64_t R=int32_t
// return=uint8_t L=uint64_t R=uint32_t
// return=uint8_t L=uint64_t R=int64_t
// return=uint8_t L=uint64_t R=uint64_t

// return=int16_t L=int8_t R=int8_t
// return=int16_t L=int8_t R=uint8_t
// return=int16_t L=int8_t R=int16_t
// return=int16_t L=int8_t R=uint16_t
// return=int16_t L=int8_t R=int32_t
// return=int16_t L=int8_t R=uint32_t
// return=int16_t L=int8_t R=int64_t
// return=int16_t L=int8_t R=uint64_t
// return=int16_t L=uint8_t R=int8_t
// return=int16_t L=uint8_t R=uint8_t
static_assert(subtract<std::int16_t>(std::uint8_t{ 200 }, std::uint8_t{ 50 }) == 150);

// return=int16_t L=uint8_t R=int16_t
// return=int16_t L=uint8_t R=uint16_t
// return=int16_t L=uint8_t R=int32_t
// return=int16_t L=uint8_t R=uint32_t
// return=int16_t L=uint8_t R=int64_t
// return=int16_t L=uint8_t R=uint64_t
// return=int16_t L=int16_t R=int8_t
// return=int16_t L=int16_t R=uint8_t
// return=int16_t L=int16_t R=uint16_t
// return=int16_t L=int16_t R=int32_t
// return=int16_t L=int16_t R=uint32_t
// return=int16_t L=int16_t R=int64_t
// return=int16_t L=int16_t R=uint64_t
// return=int16_t L=uint16_t R=int8_t
// return=int16_t L=uint16_t R=uint8_t
// return=int16_t L=uint16_t R=int16_t
// return=int16_t L=uint16_t R=uint16_t
// return=int16_t L=uint16_t R=int32_t
// return=int16_t L=uint16_t R=uint32_t
// return=int16_t L=uint16_t R=int64_t
// return=int16_t L=uint16_t R=uint64_t
// return=int16_t L=int32_t R=int8_t
// return=int16_t L=int32_t R=uint8_t
// return=int16_t L=int32_t R=int16_t
// return=int16_t L=int32_t R=uint16_t

// return=int16_t L=int32_t R=int32_t
static_assert(subtract<std::int16_t>(0, 0) == 0);
static_assert(subtract<std::int16_t>(54, 16) == 38);
static_assert(subtract<std::int16_t>(100, 0) == 100);
static_assert(subtract<std::int16_t>(-100, 0) == -100);
static_assert(subtract<std::int16_t>(10, 20) == -10);
static_assert(subtract<std::int16_t>(-10, 20) == -30);
static_assert(subtract<std::int16_t>(-10, -20) == 10);

// return=int16_t L=int32_t R=uint32_t
// return=int16_t L=int32_t R=int64_t
// return=int16_t L=int32_t R=uint64_t
// return=int16_t L=uint32_t R=int8_t
// return=int16_t L=uint32_t R=uint8_t
// return=int16_t L=uint32_t R=int16_t
// return=int16_t L=uint32_t R=uint16_t
// return=int16_t L=uint32_t R=int32_t
// return=int16_t L=uint32_t R=uint32_t
// return=int16_t L=uint32_t R=int64_t
// return=int16_t L=uint32_t R=uint64_t
// return=int16_t L=int64_t R=int8_t
// return=int16_t L=int64_t R=uint8_t
// return=int16_t L=int64_t R=int16_t
// return=int16_t L=int64_t R=uint16_t
// return=int16_t L=int64_t R=int32_t
// return=int16_t L=int64_t R=uint32_t
// return=int16_t L=int64_t R=int64_t
// return=int16_t L=int64_t R=uint64_t
// return=int16_t L=uint64_t R=int8_t
// return=int16_t L=uint64_t R=uint8_t
// return=int16_t L=uint64_t R=int16_t
// return=int16_t L=uint64_t R=uint16_t
// return=int16_t L=uint64_t R=int32_t
// return=int16_t L=uint64_t R=uint32_t
// return=int16_t L=uint64_t R=int64_t
// return=int16_t L=uint64_t R=uint64_t

// return=uint16_t L=int8_t R=int8_t
static_assert(subtract<std::uint16_t>(std::int8_t{ -100 }, std::int8_t{ 50 }).has_value() == false); // negative result

// return=uint16_t L=int8_t R=uint8_t
// return=uint16_t L=int8_t R=int16_t
// return=uint16_t L=int8_t R=uint16_t
// return=uint16_t L=int8_t R=int32_t
// return=uint16_t L=int8_t R=uint32_t
// return=uint16_t L=int8_t R=int64_t
// return=uint16_t L=int8_t R=uint64_t
// return=uint16_t L=uint8_t R=int8_t
// return=uint16_t L=uint8_t R=uint8_t
// return=uint16_t L=uint8_t R=int16_t
// return=uint16_t L=uint8_t R=uint16_t
// return=uint16_t L=uint8_t R=int32_t
// return=uint16_t L=uint8_t R=uint32_t
// return=uint16_t L=uint8_t R=int64_t
// return=uint16_t L=uint8_t R=uint64_t
// return=uint16_t L=int16_t R=int8_t
// return=uint16_t L=int16_t R=uint8_t
// return=uint16_t L=int16_t R=int16_t
// return=uint16_t L=int16_t R=uint16_t
// return=uint16_t L=int16_t R=int32_t
// return=uint16_t L=int16_t R=uint32_t
// return=uint16_t L=int16_t R=int64_t
// return=uint16_t L=int16_t R=uint64_t
// return=uint16_t L=uint16_t R=int8_t
// return=uint16_t L=uint16_t R=uint8_t
// return=uint16_t L=uint16_t R=int16_t
// return=uint16_t L=uint16_t R=int32_t
// return=uint16_t L=uint16_t R=uint32_t
// return=uint16_t L=uint16_t R=int64_t
// return=uint16_t L=uint16_t R=uint64_t
// return=uint16_t L=int32_t R=int8_t
// return=uint16_t L=int32_t R=uint8_t
// return=uint16_t L=int32_t R=int16_t
// return=uint16_t L=int32_t R=uint16_t
// return=uint16_t L=int32_t R=int32_t
// return=uint16_t L=int32_t R=uint32_t
// return=uint16_t L=int32_t R=int64_t
// return=uint16_t L=int32_t R=uint64_t
// return=uint16_t L=uint32_t R=int8_t
// return=uint16_t L=uint32_t R=uint8_t
// return=uint16_t L=uint32_t R=int16_t
// return=uint16_t L=uint32_t R=uint16_t
// return=uint16_t L=uint32_t R=int32_t
// return=uint16_t L=uint32_t R=uint32_t
// return=uint16_t L=uint32_t R=int64_t
// return=uint16_t L=uint32_t R=uint64_t
// return=uint16_t L=int64_t R=int8_t
// return=uint16_t L=int64_t R=uint8_t
// return=uint16_t L=int64_t R=int16_t
// return=uint16_t L=int64_t R=uint16_t
// return=uint16_t L=int64_t R=int32_t
// return=uint16_t L=int64_t R=uint32_t
// return=uint16_t L=int64_t R=int64_t
// return=uint16_t L=int64_t R=uint64_t
// return=uint16_t L=uint64_t R=int8_t
// return=uint16_t L=uint64_t R=uint8_t
// return=uint16_t L=uint64_t R=int16_t
// return=uint16_t L=uint64_t R=uint16_t
// return=uint16_t L=uint64_t R=int32_t
// return=uint16_t L=uint64_t R=uint32_t
// return=uint16_t L=uint64_t R=int64_t
// return=uint16_t L=uint64_t R=uint64_t

// return=int32_t L=int8_t R=int8_t
// return=int32_t L=int8_t R=uint8_t
// return=int32_t L=int8_t R=int16_t
// return=int32_t L=int8_t R=uint16_t
// return=int32_t L=int8_t R=int32_t
// return=int32_t L=int8_t R=uint32_t
// return=int32_t L=int8_t R=int64_t
// return=int32_t L=int8_t R=uint64_t
// return=int32_t L=uint8_t R=int8_t
// return=int32_t L=uint8_t R=uint8_t
// return=int32_t L=uint8_t R=int16_t
// return=int32_t L=uint8_t R=uint16_t
// return=int32_t L=uint8_t R=int32_t
// return=int32_t L=uint8_t R=uint32_t
// return=int32_t L=uint8_t R=int64_t
// return=int32_t L=uint8_t R=uint64_t
// return=int32_t L=int16_t R=int8_t
// return=int32_t L=int16_t R=uint8_t
// return=int32_t L=int16_t R=int16_t
// return=int32_t L=int16_t R=uint16_t
// return=int32_t L=int16_t R=int32_t
// return=int32_t L=int16_t R=uint32_t
// return=int32_t L=int16_t R=int64_t
// return=int32_t L=int16_t R=uint64_t
// return=int32_t L=uint16_t R=int8_t
// return=int32_t L=uint16_t R=uint8_t
// return=int32_t L=uint16_t R=int16_t
// return=int32_t L=uint16_t R=uint16_t
// return=int32_t L=uint16_t R=int32_t
// return=int32_t L=uint16_t R=uint32_t
// return=int32_t L=uint16_t R=int64_t
// return=int32_t L=uint16_t R=uint64_t
// return=int32_t L=int32_t R=int8_t
// return=int32_t L=int32_t R=uint8_t
// return=int32_t L=int32_t R=int16_t
// return=int32_t L=int32_t R=uint16_t
// return=int32_t L=int32_t R=uint32_t
// return=int32_t L=int32_t R=int64_t
// return=int32_t L=int32_t R=uint64_t
// return=int32_t L=uint32_t R=int8_t
// return=int32_t L=uint32_t R=uint8_t
// return=int32_t L=uint32_t R=int16_t
// return=int32_t L=uint32_t R=uint16_t
// return=int32_t L=uint32_t R=int32_t

// return=int32_t L=uint32_t R=uint32_t
static_assert(subtract<std::int32_t>(std::uint32_t{ 4000000000u }, std::uint32_t{ 1u }).has_value() == false); // overflow return type

// return=int32_t L=uint32_t R=int64_t
// return=int32_t L=uint32_t R=uint64_t
// return=int32_t L=int64_t R=int8_t
// return=int32_t L=int64_t R=uint8_t
// return=int32_t L=int64_t R=int16_t
// return=int32_t L=int64_t R=uint16_t

// return=int32_t L=int64_t R=int32_t
static_assert(subtract<std::int32_t>(std::int64_t{ 9223372036853775807 }, 1).has_value() == false);

// return=int32_t L=int64_t R=uint32_t
// return=int32_t L=int64_t R=int64_t
// return=int32_t L=int64_t R=uint64_t
// return=int32_t L=uint64_t R=int8_t
// return=int32_t L=uint64_t R=uint8_t
// return=int32_t L=uint64_t R=int16_t
// return=int32_t L=uint64_t R=uint16_t
// return=int32_t L=uint64_t R=int32_t
// return=int32_t L=uint64_t R=uint32_t
// return=int32_t L=uint64_t R=int64_t
// return=int32_t L=uint64_t R=uint64_t

// return=uint32_t L=int8_t R=int8_t
// return=uint32_t L=int8_t R=uint8_t
// return=uint32_t L=int8_t R=int16_t
// return=uint32_t L=int8_t R=uint16_t
// return=uint32_t L=int8_t R=int32_t
// return=uint32_t L=int8_t R=uint32_t
// return=uint32_t L=int8_t R=int64_t
// return=uint32_t L=int8_t R=uint64_t
// return=uint32_t L=uint8_t R=int8_t
// return=uint32_t L=uint8_t R=uint8_t
// return=uint32_t L=uint8_t R=int16_t
// return=uint32_t L=uint8_t R=uint16_t
// return=uint32_t L=uint8_t R=int32_t
// return=uint32_t L=uint8_t R=uint32_t
// return=uint32_t L=uint8_t R=int64_t
// return=uint32_t L=uint8_t R=uint64_t
// return=uint32_t L=int16_t R=int8_t
// return=uint32_t L=int16_t R=uint8_t
// return=uint32_t L=int16_t R=int16_t
// return=uint32_t L=int16_t R=uint16_t
// return=uint32_t L=int16_t R=int32_t
// return=uint32_t L=int16_t R=uint32_t
// return=uint32_t L=int16_t R=int64_t
// return=uint32_t L=int16_t R=uint64_t
// return=uint32_t L=uint16_t R=int8_t
// return=uint32_t L=uint16_t R=uint8_t
// return=uint32_t L=uint16_t R=int16_t
// return=uint32_t L=uint16_t R=uint16_t
// return=uint32_t L=uint16_t R=int32_t
// return=uint32_t L=uint16_t R=uint32_t
// return=uint32_t L=uint16_t R=int64_t
// return=uint32_t L=uint16_t R=uint64_t
// return=uint32_t L=int32_t R=int8_t
// return=uint32_t L=int32_t R=uint8_t
// return=uint32_t L=int32_t R=int16_t
// return=uint32_t L=int32_t R=uint16_t
// return=uint32_t L=int32_t R=int32_t
// return=uint32_t L=int32_t R=uint32_t
// return=uint32_t L=int32_t R=int64_t
// return=uint32_t L=int32_t R=uint64_t
// return=uint32_t L=uint32_t R=int8_t
// return=uint32_t L=uint32_t R=uint8_t
// return=uint32_t L=uint32_t R=int16_t
// return=uint32_t L=uint32_t R=uint16_t
// return=uint32_t L=uint32_t R=int32_t
// return=uint32_t L=uint32_t R=int64_t
// return=uint32_t L=uint32_t R=uint64_t
// return=uint32_t L=int64_t R=int8_t
// return=uint32_t L=int64_t R=uint8_t
// return=uint32_t L=int64_t R=int16_t
// return=uint32_t L=int64_t R=uint16_t
// return=uint32_t L=int64_t R=int32_t
// return=uint32_t L=int64_t R=uint32_t
// return=uint32_t L=int64_t R=int64_t
// return=uint32_t L=int64_t R=uint64_t
// return=uint32_t L=uint64_t R=int8_t
// return=uint32_t L=uint64_t R=uint8_t
// return=uint32_t L=uint64_t R=int16_t
// return=uint32_t L=uint64_t R=uint16_t
// return=uint32_t L=uint64_t R=int32_t
// return=uint32_t L=uint64_t R=uint32_t
// return=uint32_t L=uint64_t R=int64_t
// return=uint32_t L=uint64_t R=uint64_t

// return=int64_t L=int8_t R=int8_t
// return=int64_t L=int8_t R=uint8_t
// return=int64_t L=int8_t R=int16_t
// return=int64_t L=int8_t R=uint16_t
// return=int64_t L=int8_t R=int32_t
// return=int64_t L=int8_t R=uint32_t
// return=int64_t L=int8_t R=int64_t
// return=int64_t L=int8_t R=uint64_t
// return=int64_t L=uint8_t R=int8_t
// return=int64_t L=uint8_t R=uint8_t
// return=int64_t L=uint8_t R=int16_t
// return=int64_t L=uint8_t R=uint16_t
// return=int64_t L=uint8_t R=int32_t
// return=int64_t L=uint8_t R=uint32_t
// return=int64_t L=uint8_t R=int64_t
// return=int64_t L=uint8_t R=uint64_t
// return=int64_t L=int16_t R=int8_t
// return=int64_t L=int16_t R=uint8_t
// return=int64_t L=int16_t R=int16_t
// return=int64_t L=int16_t R=uint16_t
// return=int64_t L=int16_t R=int32_t
// return=int64_t L=int16_t R=uint32_t
// return=int64_t L=int16_t R=int64_t
// return=int64_t L=int16_t R=uint64_t
// return=int64_t L=uint16_t R=int8_t
// return=int64_t L=uint16_t R=uint8_t
// return=int64_t L=uint16_t R=int16_t
// return=int64_t L=uint16_t R=uint16_t
// return=int64_t L=uint16_t R=int32_t
// return=int64_t L=uint16_t R=uint32_t
// return=int64_t L=uint16_t R=int64_t
// return=int64_t L=uint16_t R=uint64_t
// return=int64_t L=int32_t R=int8_t
// return=int64_t L=int32_t R=uint8_t
// return=int64_t L=int32_t R=int16_t
// return=int64_t L=int32_t R=uint16_t
// return=int64_t L=int32_t R=int32_t
static_assert(subtract<std::int64_t>(std::int32_t{ 2000000000 }, std::int32_t{ 1000000000 }) == 1000000000);

// return=int64_t L=int32_t R=uint32_t
// return=int64_t L=int32_t R=int64_t
static_assert(subtract<std::int64_t>(1, std::numeric_limits<std::int64_t>::lowest()).has_value() == false);
static_assert(subtract<std::int64_t>(-2, std::numeric_limits<std::int64_t>::max()).has_value() == false);
static_assert(subtract<std::int64_t>(0, std::numeric_limits<std::int64_t>::lowest()).has_value() == false);
// return=int64_t L=int32_t R=uint64_t
static_assert(subtract<std::int64_t>(1, std::numeric_limits<std::uint64_t>::max()).has_value() == false);

// return=int64_t L=uint32_t R=int8_t
// return=int64_t L=uint32_t R=uint8_t
// return=int64_t L=uint32_t R=int16_t
// return=int64_t L=uint32_t R=uint16_t
// return=int64_t L=uint32_t R=int32_t
// return=int64_t L=uint32_t R=uint32_t
static_assert(subtract<std::int64_t>(std::uint32_t{ 4000000000 }, std::uint32_t{ 4000000000 }) == 0);

// return=int64_t L=uint32_t R=int64_t
// return=int64_t L=uint32_t R=uint64_t
// return=int64_t L=int64_t R=int8_t
// return=int64_t L=int64_t R=uint8_t
// return=int64_t L=int64_t R=int16_t
// return=int64_t L=int64_t R=uint16_t
// return=int64_t L=int64_t R=int32_t
static_assert(subtract<std::int64_t>(std::numeric_limits<std::int64_t>::lowest(), 0) == std::numeric_limits<std::int64_t>::lowest());

// return=int64_t L=int64_t R=uint32_t
// return=int64_t L=int64_t R=uint64_t
// return=int64_t L=uint64_t R=int8_t
// return=int64_t L=uint64_t R=uint8_t
// return=int64_t L=uint64_t R=int16_t
// return=int64_t L=uint64_t R=uint16_t
// return=int64_t L=uint64_t R=int32_t
static_assert(subtract<std::int64_t>(std::numeric_limits<std::uint64_t>::max(), 1).has_value() == false);

// return=int64_t L=uint64_t R=uint32_t
// return=int64_t L=uint64_t R=int64_t
static_assert(subtract<std::uint64_t>(std::uint64_t{ std::numeric_limits<std::int64_t>::max() } + 1u, std::int64_t{ -1 }).has_value() == false); // No workable intermediate type

// return=int64_t L=uint64_t R=uint64_t
static_assert(subtract<std::int64_t>(std::uint64_t{ std::numeric_limits<std::int64_t>::max() }, std::uint64_t{ 0 }) == std::numeric_limits<std::int64_t>::max());
static_assert(subtract<std::int64_t>(std::uint64_t{ 100 }, std::uint64_t{ 50 }) == 50);
static_assert(subtract<std::int64_t>(std::uint64_t{ std::numeric_limits<std::int64_t>::max() } + 1u, std::uint64_t{ 0 }).has_value() == false);

// return=uint64_t L=int8_t R=int8_t
// return=uint64_t L=int8_t R=uint8_t
// return=uint64_t L=int8_t R=int16_t
// return=uint64_t L=int8_t R=uint16_t
// return=uint64_t L=int8_t R=int32_t
// return=uint64_t L=int8_t R=uint32_t
// return=uint64_t L=int8_t R=int64_t
// return=uint64_t L=int8_t R=uint64_t
// return=uint64_t L=uint8_t R=int8_t
// return=uint64_t L=uint8_t R=uint8_t
// return=uint64_t L=uint8_t R=int16_t
// return=uint64_t L=uint8_t R=uint16_t
// return=uint64_t L=uint8_t R=int32_t
// return=uint64_t L=uint8_t R=uint32_t
// return=uint64_t L=uint8_t R=int64_t
// return=uint64_t L=uint8_t R=uint64_t
// return=uint64_t L=int16_t R=int8_t
// return=uint64_t L=int16_t R=uint8_t
// return=uint64_t L=int16_t R=int16_t
// return=uint64_t L=int16_t R=uint16_t
// return=uint64_t L=int16_t R=int32_t
// return=uint64_t L=int16_t R=uint32_t
// return=uint64_t L=int16_t R=int64_t
// return=uint64_t L=int16_t R=uint64_t
// return=uint64_t L=uint16_t R=int8_t
// return=uint64_t L=uint16_t R=uint8_t
// return=uint64_t L=uint16_t R=int16_t
// return=uint64_t L=uint16_t R=uint16_t
// return=uint64_t L=uint16_t R=int32_t
// return=uint64_t L=uint16_t R=uint32_t
// return=uint64_t L=uint16_t R=int64_t
// return=uint64_t L=uint16_t R=uint64_t
// return=uint64_t L=int32_t R=int8_t
// return=uint64_t L=int32_t R=uint8_t
// return=uint64_t L=int32_t R=int16_t
// return=uint64_t L=int32_t R=uint16_t
// return=uint64_t L=int32_t R=int32_t
// return=uint64_t L=int32_t R=uint32_t
// return=uint64_t L=int32_t R=int64_t
// return=uint64_t L=int32_t R=uint64_t
// return=uint64_t L=uint32_t R=int8_t
// return=uint64_t L=uint32_t R=uint8_t
// return=uint64_t L=uint32_t R=int16_t
// return=uint64_t L=uint32_t R=uint16_t
// return=uint64_t L=uint32_t R=int32_t
// return=uint64_t L=uint32_t R=uint32_t
// return=uint64_t L=uint32_t R=int64_t
// return=uint64_t L=uint32_t R=uint64_t
// return=uint64_t L=int64_t R=int8_t
// return=uint64_t L=int64_t R=uint8_t
// return=uint64_t L=int64_t R=int16_t
// return=uint64_t L=int64_t R=uint16_t
// return=uint64_t L=int64_t R=int32_t
// return=uint64_t L=int64_t R=uint32_t
// return=uint64_t L=int64_t R=int64_t
// return=uint64_t L=int64_t R=uint64_t
// return=uint64_t L=uint64_t R=int8_t
// return=uint64_t L=uint64_t R=uint8_t
// return=uint64_t L=uint64_t R=int16_t
// return=uint64_t L=uint64_t R=uint16_t
// return=uint64_t L=uint64_t R=int32_t
// return=uint64_t L=uint64_t R=uint32_t
// return=uint64_t L=uint64_t R=int64_t

#endif

}