// include guard: BERTRAND_STRUCTS_UTIL_MATH_H
#ifndef BERTRAND_STRUCTS_UTIL_MATH_H
#define BERTRAND_STRUCTS_UTIL_MATH_H

#include <cstddef>  // size_t
#include <type_traits>  // std::enable_if_t<>, std::is_unsigned_v<>, etc.


namespace bertrand {
namespace util {


/* Check if a number is a power of two. */
template <typename T, std::enable_if_t<std::is_unsigned_v<T>, int> = 0>
inline bool is_power_of_two(T n) {
    return n && !(n & (n - 1));
}


/* Round a number up to the next power of two. */
template <typename T, std::enable_if_t<std::is_unsigned_v<T>, int> = 0>
inline T next_power_of_two(T n) {
    constexpr size_t bits = sizeof(T) * 8;
    --n;
    for (size_t i = 1; i < bits; i <<= 1) {
        n |= (n >> i);
    }
    return ++n;
}


/* Get the log base 2 of a number. */
template <typename T, std::enable_if_t<std::is_unsigned_v<T>, int> = 0>
inline unsigned char log2(T n) {
    unsigned char count = 0;
    while (n >>= 1) ++count;
    return count;
}


/* A fast modulo operator that works for any b power of two. */
template <typename T, std::enable_if_t<std::is_unsigned_v<T>, int> = 0>
inline static T mod2(T a, T b) {
    return a & (b - 1);
}


/* A Python-style modulo operator (%).

NOTE: Python's `%` operator is defined such that the result has the same sign as the
divisor (b).  This differs from C/C++, where the result has the same sign as the
dividend (a). */
template <typename T>
inline static T py_modulo(T a, T b) {
    return (a % b + b) % b;
}


}  // namespace util
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_UTIL_MATH_H
