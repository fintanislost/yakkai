#pragma once

#include <concepts>
#include <cstdint>
#include <random>
#include <type_traits>

namespace wallpaper::Random
{

inline std::mt19937_64& engine()
{
    thread_local std::mt19937_64 generator { std::random_device{}() };
    return generator;
}

template<typename Distribution, std::floating_point T>
inline T get(T min, T max)
{
    if (max < min) {
        std::swap(min, max);
    }
    Distribution distribution(min, max);
    return distribution(engine());
}

template<std::floating_point T>
inline T get(T min, T max)
{
    return get<std::uniform_real_distribution<T>>(min, max);
}

template<std::integral T>
inline T get(T min, T max)
{
    if (max < min) {
        std::swap(min, max);
    }
    std::uniform_int_distribution<T> distribution(min, max);
    return distribution(engine());
}

template<typename T, typename U>
inline auto get(T min, U max)
{
    using value_type = std::common_type_t<T, U>;

    if constexpr (std::integral<value_type>) {
        return get<value_type>(static_cast<value_type>(min), static_cast<value_type>(max));
    } else {
        using real_type = std::common_type_t<value_type, double>;
        return get(static_cast<real_type>(min), static_cast<real_type>(max));
    }
}

} // namespace wallpaper::Random
