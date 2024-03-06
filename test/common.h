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

    /////////////////////////
    ////    operator=    ////
    /////////////////////////

    template <typename T1>
    struct assign {
        // T1& = T2
        template <typename T2, typename = void>
        struct from {
            static void valid() {
                FAIL() << "Expression `L& = R` is invalid [with L = "
                       << typeid(T1).name() << " and R = " << typeid(T2).name() << " ]";
            }
            static void invalid() {}
        };
        template <typename T2>
        struct from<T2, std::void_t<decltype(std::declval<T1&>() = std::declval<T2>() )>> {
            static void valid() {}
            static void invalid() {
                FAIL() << "Expression `L& = R` is valid [with L = "
                       << typeid(T1).name() << " and R = " << typeid(T2).name() << " ]";
            }
        };

        // T2& = T1
        template <typename T2, typename = void>
        struct to {
            static void valid() {
                FAIL() << "Expression `L& = R` is invalid [with L = "
                       << typeid(T2).name() << " and R = " << typeid(T1).name() << " ]";
            }
            static void invalid() {}
        };
        template <typename T2>
        struct to<T2, std::void_t<decltype(std::declval<T2&>() = std::declval<T1>() )>> {
            static void valid() {}
            static void invalid() {
                FAIL() << "Expression `L& = R` is valid [with L = "
                       << typeid(T2).name() << " and R = " << typeid(T1).name() << " ]";
            }
        };
    };

    //////////////////////////
    ////    operator()    ////
    //////////////////////////

    template <typename T, typename = void>
    struct call {
        static void valid() {
            FAIL() << "Type " << typeid(T).name() << " is not callable";
        }
        static void invalid() {}

        template <typename... Args>
        struct with {
            static void valid() { call::valid(); }
            static void invalid() { call::invalid(); }

            template <typename Return>
            struct returns {
                static void valid() { with::valid(); }
                static void invalid() { with::invalid(); }
            };
        };
    };

    template <typename T>
    struct call<T, std::void_t<decltype(&T::operator())>> {
        static void valid() {}
        static void invalid() {
            FAIL() << "Type " << typeid(T).name() << " is callable";
        }

        template <typename... Args>
        struct with {
            static void valid() {
                if constexpr (!std::is_invocable_v<T, Args...>) {
                    FAIL() << "Type " << typeid(T).name()
                           << " is not callable with the given arguments";
                }
            }
            static void invalid() {
                if constexpr (std::is_invocable_v<T, Args...>) {
                    FAIL() << "Type " << typeid(T).name()
                           << " is callable with the given arguments";
                }
            }

            template <typename Return, typename = void>
            struct returns {
                static void valid() { with::valid(); }
                static void invalid() { with::invalid(); }
            };

            template <typename Return>
            struct returns<Return, std::enable_if_t<std::is_invocable_v<T, Args...>>> {
                static void valid() {
                    if constexpr (!std::is_invocable_r_v<Return, T, Args...>) {
                        FAIL() << "Return types do not match.  Expected: "
                               << typeid(Return).name() << "  Got: "
                               << typeid(std::invoke_result_t<T, Args...>).name();
                    }
                }
                static void invalid() {
                    if constexpr (std::is_invocable_r_v<Return, T, Args...>) {
                        FAIL() << "Return types match (expected no match).  Return type: "
                               << typeid(Return).name();
                    }
                }
            };
        };
    };

    /////////////////////////
    ////    std::hash    ////
    /////////////////////////

    template <typename T, typename = void>
    struct hash {
        static void valid() {
            FAIL() << "Type " << typeid(T).name() << " does not specialize std::hash.";
        }
        static void invalid() {}
    };

    template <typename T>
    struct hash<T, std::void_t<decltype(std::hash<T>{})>> {
        static void valid() {}
        static void invalid() {
            FAIL() << "Type " << typeid(T).name() << " specializes std::hash.";
        }
    };

    //////////////////////////
    ////    operator[]    ////
    //////////////////////////

    template <typename T, typename = void>
    struct index {
        static void valid() {
            FAIL() << "Type " << typeid(T).name()
                   << " does not support the [] operator.";
        }
        static void invalid() {}

        template <typename Key>
        struct with {
            static void valid() { index::valid(); }
            static void invalid() { index::invalid(); }

            template <typename Return>
            struct returns {
                static void valid() { with::valid(); }
                static void invalid() { with::invalid(); }
            };
        };
    };

    template <typename T>
    struct index<T, std::void_t<decltype(&T::operator[])>> {
        static void valid() {}
        static void invalid() {
            FAIL() << "Type " << typeid(T).name() << " supports the [] operator.";
        }

        template <typename Key, typename = void>
        struct with {
            static void valid() {
                FAIL() << "Type " << typeid(T).name()
                       << " does not support the [] operator with key of type "
                       << typeid(Key).name();
            }
            static void invalid() {}

            template <typename Return>
            struct returns {
                static void valid() { with::valid(); }
                static void invalid() { with::invalid(); }
            };
        };

        template <typename Key>
        struct with<Key, std::void_t<decltype(std::declval<T>()[std::declval<Key>()])>> {
            using type = decltype(std::declval<T>()[std::declval<Key>()]);
            static void valid() {}
            static void invalid() {
                FAIL() << "Type " << typeid(T).name()
                       << " supports the [] operator with key of type "
                       << typeid(Key).name();
            }

            template <typename Return, typename = void>
            struct returns {
                static void valid() {
                    FAIL() << "Return types do not match.  Expected: "
                           << typeid(Return).name() << "  Got: "
                           << typeid(type).name();
                }
                static void invalid() {}
            };

            template <typename Return>
            struct returns<Return, std::enable_if_t<std::is_same_v<Return, type>>> {
                static void valid() {}
                static void invalid() {
                    FAIL() << "Return types match (expected no match).  Return type: "
                           << typeid(Return).name();
                }
            };
        };
    };

    /////////////////////////////
    ////    begin()/end()    ////
    /////////////////////////////

    template <typename T, typename = void>
    struct iter {
        static void valid() {
            FAIL() << "Type " << typeid(T).name()
                    << " does not support the forward iterator interface.";
        }
        static void invalid() {}
    };

    template <typename T>
    struct iter<T, std::void_t<decltype(
        std::begin(std::declval<T>()),
        std::end(std::declval<T>())
    )>> {
        static void valid() {}
        static void invalid() {
            FAIL() << "Type " << typeid(T).name()
                    << " supports the forward iterator interface.";
        }
    };

    ///////////////////////////////
    ////    rbegin()/rend()    ////
    ///////////////////////////////

    template <typename T, typename = void>
    struct reverse_iter {
        static void valid() {
            FAIL() << "Type " << typeid(T).name()
                    << " does not support the reverse iterator interface.";
        }
        static void invalid() {}
    };

    template <typename T>
    struct reverse_iter<T, std::void_t<decltype(
        std::rbegin(std::declval<T>()),
        std::rend(std::declval<T>())
    )>> {
        static void valid() {}
        static void invalid() {
            FAIL() << "Type " << typeid(T).name()
                    << " supports the reverse iterator interface.";
        }
    };

    //////////////////////////
    ////    contains()    ////
    //////////////////////////

    template <typename T, typename = void>
    struct contains {
        static void valid() {
            FAIL() << "Type " << typeid(T).name()
                   << " does not support the contains() method.";
        }
        static void invalid() {}

        template <typename Key>
        struct with {
            static void valid() { contains::valid(); }
            static void invalid() { contains::invalid(); }

            template <typename Return>
            struct returns {
                static void valid() { with::valid(); }
                static void invalid() { with::invalid(); }
            };
        };
    };

    template <typename T>
    struct contains<T, std::void_t<decltype(&T::contains)>> {
        static void valid() {}
        static void invalid() {
            FAIL() << "Type " << typeid(T).name() << " supports the contains() method.";
        }

        template <typename Key, typename = void>
        struct with {
            static void valid() {
                FAIL() << "Type " << typeid(T).name()
                       << " does not support the contains() method with key of type "
                       << typeid(Key).name();
            }
            static void invalid() {}

            template <typename Return>
            struct returns {
                static void valid() { with::valid(); }
                static void invalid() { with::invalid(); }
            };
        };

        template <typename Key>
        struct with<Key, std::void_t<decltype(std::declval<T>().contains(std::declval<Key>()))>> {
            using type = decltype(std::declval<T>().contains(std::declval<Key>()));
            static void valid() {}
            static void invalid() {
                FAIL() << "Type " << typeid(T).name()
                       << " supports the contains() method with key of type "
                       << typeid(Key).name();
            }

            template <typename Return, typename = void>
            struct returns {
                static void valid() {
                    FAIL() << "Return types do not match.  Expected: "
                           << typeid(Return).name() << "  Got: "
                           << typeid(type).name();
                }
                static void invalid() {}
            };

            template <typename Return>
            struct returns<Return, std::enable_if_t<std::is_same_v<Return, type>>> {
                static void valid() {}
                static void invalid() {
                    FAIL() << "Return types match (expected no match).  Return type: "
                           << typeid(Return).name();
                }
            };
        };
    };

    ///////////////////////////////
    ////    UNARY OPERATORS    ////
    ///////////////////////////////

    #define UNARY_OPERATOR(cls, op)                                                     \
        template <typename T, typename = void>                                          \
        struct cls {                                                                    \
            static void valid() {                                                       \
                FAIL() << "Type " << typeid(T).name() << " does not support the unary " \
                       << #op << " operator.";                                          \
            }                                                                           \
            static void invalid() {}                                                    \
                                                                                        \
            template <typename Return>                                                  \
            struct returns {                                                            \
                static void valid() { cls::valid(); }                                   \
                static void invalid() { cls::invalid(); }                               \
            };                                                                          \
        };                                                                              \
                                                                                        \
        template <typename T>                                                           \
        struct cls<T, std::void_t<decltype(op std::declval<T>())>> {                    \
            using type = decltype(op std::declval<T>());                                \
            static void valid() {}                                                      \
            static void invalid() {                                                     \
                FAIL() << "Type " << typeid(T).name() << " supports the unary "         \
                       << #op << " operator.";                                          \
            }                                                                           \
                                                                                        \
            template <typename Return, typename = void>                                 \
            struct returns {                                                            \
                static void valid() {                                                   \
                    FAIL() << "Return types do not match.  Expected: "                  \
                           << typeid(Return).name() << "  Got: "                        \
                           << typeid(type).name();                                      \
                }                                                                       \
                static void invalid() {}                                                \
            };                                                                          \
                                                                                        \
            template <typename Return>                                                  \
            struct returns<Return, std::enable_if_t<std::is_same_v<Return, type>>> {    \
                static void valid() {}                                                  \
                static void invalid() {                                                 \
                    FAIL() << "Return types match (expected no match).  Return type: "  \
                           << typeid(Return).name();                                    \
                }                                                                       \
            };                                                                          \
        };                                                                              \

    UNARY_OPERATOR(unary_invert, ~)
    UNARY_OPERATOR(unary_plus, +)
    UNARY_OPERATOR(unary_minus, -)
    UNARY_OPERATOR(dereference, *)
    UNARY_OPERATOR(address_of, &)
    UNARY_OPERATOR(unary_not, !)

    #undef UNARY_OPERATOR

    ////////////////////////////////
    ////    BINARY OPERATORS    ////
    ////////////////////////////////

    #define BINARY_OPERATOR(cls, op)                                                    \
        template <typename L, typename R, typename = void>                              \
        struct cls {                                                                    \
            static void valid() {                                                       \
                FAIL() << "Expression `L " << #op << " R` is invalid [with L = "        \
                       << typeid(L).name() << " and R = " << typeid(R).name() << " ]";  \
            }                                                                           \
            static void invalid() {}                                                    \
                                                                                        \
            template <typename Return>                                                  \
            struct returns {                                                            \
                static void valid() { cls::valid(); }                                   \
                static void invalid() { cls::invalid(); }                               \
            };                                                                          \
        };                                                                              \
                                                                                        \
        template <typename L, typename R>                                               \
        struct cls<L, R, std::void_t<decltype(                                          \
            std::declval<L>() op std::declval<R>()                                      \
        )>> {                                                                           \
            using type = decltype(std::declval<L>() op std::declval<R>());              \
            static void valid() {}                                                      \
            static void invalid() {                                                     \
                FAIL() << "Expression `L " << #op << " R` is valid [with L = "          \
                       << typeid(L).name() << " and R = " << typeid(R).name() << " ]";  \
            }                                                                           \
                                                                                        \
            template <typename Return, typename = void>                                 \
            struct returns {                                                            \
                static void valid() {                                                   \
                    FAIL() << "Return types do not match.  Expected: "                  \
                           << typeid(Return).name() << "  Got: "                        \
                           << typeid(type).name();                                      \
                }                                                                       \
                static void invalid() {}                                                \
            };                                                                          \
                                                                                        \
            template <typename Return>                                                  \
            struct returns<Return, std::enable_if_t<std::is_same_v<Return, type>>> {    \
                static void valid() {}                                                  \
                static void invalid() {                                                 \
                    FAIL() << "Return types match (expected no match).  Return type: "  \
                           << typeid(Return).name();                                    \
                }                                                                       \
            };                                                                          \
        };                                                                              \

    BINARY_OPERATOR(less_than, <)
    BINARY_OPERATOR(less_than_or_equal_to, <=)
    BINARY_OPERATOR(equal_to, ==)
    BINARY_OPERATOR(not_equal_to, !=)
    BINARY_OPERATOR(greater_than_or_equal_to, >=)
    BINARY_OPERATOR(greater_than, >)
    BINARY_OPERATOR(binary_plus, +)
    BINARY_OPERATOR(binary_minus, -)
    BINARY_OPERATOR(binary_multiply, *)
    BINARY_OPERATOR(binary_divide, /)
    BINARY_OPERATOR(binary_modulo, %)
    BINARY_OPERATOR(left_shift, <<)
    BINARY_OPERATOR(right_shift, >>)
    BINARY_OPERATOR(bitwise_and, &)
    BINARY_OPERATOR(bitwise_or, |)
    BINARY_OPERATOR(bitwise_xor, ^)

    #undef BINARY_OPERATOR

    /////////////////////////////////
    ////    INPLACE_OPERATORS    ////
    /////////////////////////////////

    #define INPLACE_OPERATOR(cls, op)                                                   \
        template <typename L, typename R, typename = void>                              \
        struct cls {                                                                    \
            static void valid() {                                                       \
                FAIL() << "Expression `L " << #op << " R` is invalid [with L = "        \
                       << typeid(L).name() << " and R = " << typeid(R).name() << " ]";  \
            }                                                                           \
            static void invalid() {}                                                    \
        };                                                                              \
                                                                                        \
        template <typename L, typename R>                                               \
        struct cls<L, R, std::void_t<decltype(                                          \
            std::declval<L&>() op std::declval<R>()                                     \
        )>> {                                                                           \
            static void valid() {}                                                      \
            static void invalid() {                                                     \
                FAIL() << "Expression `L " << #op << " R` is valid [with L = "          \
                       << typeid(L).name() << " and R = " << typeid(R).name() << " ]";  \
            }                                                                           \
        };                                                                              \

    INPLACE_OPERATOR(inplace_plus, +=)
    INPLACE_OPERATOR(inplace_minus, -=)
    INPLACE_OPERATOR(inplace_multiply, *=)
    INPLACE_OPERATOR(inplace_divide, /=)
    INPLACE_OPERATOR(inplace_modulo, %=)
    INPLACE_OPERATOR(inplace_left_shift, <<=)
    INPLACE_OPERATOR(inplace_right_shift, >>=)
    INPLACE_OPERATOR(inplace_bitwise_and, &=)
    INPLACE_OPERATOR(inplace_bitwise_or, |=)
    INPLACE_OPERATOR(inplace_bitwise_xor, ^=)

    #undef INPLACE_OPERATOR

    /////////////////////////
    ////    operator&    ////
    /////////////////////////



    // TODO: implicit conversion operators?


}








#endif  // BERTRAND_TEST_COMMON_H
