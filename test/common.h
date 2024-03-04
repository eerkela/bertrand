#ifndef BERTRAND_TEST_COMMON_H
#define BERTRAND_TEST_COMMON_H

#include <gtest/gtest.h>
#include <Python.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <initializer_list>
#include <list>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>


namespace assertions {

    template <typename L, typename R, typename = void>
    constexpr bool is_assignable = false;
    template <typename L, typename R>
    constexpr bool is_assignable<L, R, std::void_t<decltype(
        std::declval<L&>() = std::declval<R>()
    )>> = true;

    template <typename T, typename = void>
    constexpr bool is_indexable = false;
    template <typename T>
    constexpr bool is_indexable<T, std::void_t<decltype(
        std::declval<T>()[0]
    )>> = true;

    template <typename T, typename = void>
    constexpr bool is_iterable = false;
    template <typename T>
    constexpr bool is_iterable<T, std::void_t<decltype(
        std::begin(std::declval<T>()),
        std::end(std::declval<T>())
    )>> = true;

    template <typename T, typename = void>
    constexpr bool is_callable = false;
    template <typename T>
    constexpr bool is_callable<T, std::void_t<decltype(&T::operator())>> = true;

    #define CHECK_UNARY_OPERATOR(name, op)                                              \
        template <typename T, typename = void>                                          \
        constexpr bool name = false;                                                    \
        template <typename T>                                                           \
        constexpr bool name<T, std::void_t<decltype(                                    \
            op std::declval<T>()                                                        \
        )>> = true;                                                                     \

    #define CHECK_BINARY_OPERATOR(name, op)                                             \
        template <typename L, typename R, typename = void>                              \
        constexpr bool name = false;                                                    \
        template <typename L, typename R>                                               \
        constexpr bool name<L, R, std::void_t<decltype(                                 \
            std::declval<L>() op std::declval<R>()                                      \
        )>> = true;                                                                     \

    #define CHECK_INPLACE_OPERATOR(name, op)                                            \
        template <typename L, typename R, typename = void>                              \
        constexpr bool name = false;                                                    \
        template <typename L, typename R>                                               \
        constexpr bool name<L, R, std::void_t<decltype(                                 \
            std::declval<L&>() op std::declval<R>()                                     \
        )>> = true;                                                                     \

    CHECK_UNARY_OPERATOR(has_inverse, ~)
    CHECK_UNARY_OPERATOR(has_positive, +)
    CHECK_UNARY_OPERATOR(has_negative, -)
    CHECK_BINARY_OPERATOR(has_addition, +)
    CHECK_BINARY_OPERATOR(has_subtraction, -)
    CHECK_BINARY_OPERATOR(has_multiplication, *)
    CHECK_BINARY_OPERATOR(has_division, /)
    CHECK_BINARY_OPERATOR(has_modulus, %)
    CHECK_BINARY_OPERATOR(has_left_shift, <<)
    CHECK_BINARY_OPERATOR(has_right_shift, >>)
    CHECK_BINARY_OPERATOR(has_bitwise_and, &)
    CHECK_BINARY_OPERATOR(has_bitwise_or, |)
    CHECK_BINARY_OPERATOR(has_bitwise_xor, ^)
    CHECK_INPLACE_OPERATOR(has_inplace_addition, +=)
    CHECK_INPLACE_OPERATOR(has_inplace_subtraction, -=)
    CHECK_INPLACE_OPERATOR(has_inplace_multiplication, *=)
    CHECK_INPLACE_OPERATOR(has_inplace_division, /=)
    CHECK_INPLACE_OPERATOR(has_inplace_modulus, %=)
    CHECK_INPLACE_OPERATOR(has_inplace_left_shift, <<=)
    CHECK_INPLACE_OPERATOR(has_inplace_right_shift, >>=)
    CHECK_INPLACE_OPERATOR(has_inplace_bitwise_and, &=)
    CHECK_INPLACE_OPERATOR(has_inplace_bitwise_or, |=)
    CHECK_INPLACE_OPERATOR(has_inplace_bitwise_xor, ^=)

    #undef CHECK_UNARY_OPERATOR
    #undef CHECK_BINARY_OPERATOR
    #undef CHECK_INPLACE_OPERATOR
}


#endif  // BERTRAND_TEST_COMMON_H
