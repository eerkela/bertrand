#if !defined(BERTRAND_PYTHON_COMMON_INCLUDED) && !defined(LINTER)
#error "This file should not be included directly.  Please include <bertrand/common.h> instead."
#endif

#ifndef BERTRAND_PYTHON_COMMON_OPERATORS_H
#define BERTRAND_PYTHON_COMMON_OPERATORS_H

#include "declarations.h"
#include "concepts.h"
#include "exceptions.h"


namespace bertrand {
namespace py {


/* By default, all generic operators are disabled for subclasses of py::Object.
* This means we have to specifically enable them for each type we want to support,
* which promotes explicitness and type safety by design.  The following structs
* allow users to easily assign static types to any of these operators, which will
* automatically be preferred when operands of those types are detected at compile
* time.  By using template specialization, we allow users to do this from outside
* the class itself, allowing the type system to grow as needed to cover any
* environment.  Here's an example:
*
*      template <>
*      struct py::__add__<py::Bool, int> : py::Returns<py::Int> {};
*
* It's that simple.  Now, whenever we call `py::Bool + int`, it will successfully
* compile and interpret the result as a strict `py::Int` type, eliminating runtime
* overhead and granting static type safety.  It is also possible to apply C++20
* template constraints to these types using an optional second template parameter,
* which allows users to enable or disable whole categories of types at once.
* Here's another example:
*
*      template <py::impl::int_like T>
*      struct py::__add__<py::Bool, T> : py::Returns<py::Int> {};
*
* As long as the constraint does not conflict with any other existing template
* overloads, this will compile and work as expected.  Note that specific overloads
* will always take precedence over generic ones, and any ambiguities between
* templates will result in compile errors when used.
*/

#define BERTRAND_OBJECT_OPERATORS(cls)                                                  \
    template <typename... Args> requires (__call__<cls, Args...>::enable)               \
    auto operator()(Args&&... args) const {                                             \
        using Return = typename __call__<cls, Args...>::Return;                         \
        static_assert(                                                                  \
            std::is_same_v<Return, void> || std::is_base_of_v<Return, Object>,          \
            "Call operator must return either void or a py::Object subclass.  "         \
            "Check your specialization of __call__ for the given arguments and "        \
            "ensure that it is derived from py::Object."                                \
        );                                                                              \
        return operator_call<Return>(*this, std::forward<Args>(args)...);               \
    }                                                                                   \
                                                                                        \
    template <typename Key> requires (__getitem__<cls, Key>::enable)                    \
    auto operator[](const Key& key) const {                                             \
        using Return = typename __getitem__<cls, Key>::Return;                          \
        if constexpr (impl::proxy_like<Key>) {                                          \
            return (*this)[key.value()];                                                \
        } else {                                                                        \
            return operator_getitem<Return>(*this, key);                                \
        }                                                                               \
    }                                                                                   \
                                                                                        \
    template <typename Self = cls> requires (__getitem__<Self, Slice>::enable)          \
    auto operator[](std::initializer_list<impl::SliceInitializer> slice) const {        \
        using Return = typename __getitem__<Self, Slice>::Return;                       \
        return operator_getitem<Return>(*this, slice);                                  \
    }                                                                                   \
                                                                                        \
    template <typename Self = cls> requires (__iter__<Self>::enable)                    \
    auto begin() const {                                                                \
        using Return = typename __iter__<Self>::Return;                                 \
        static_assert(                                                                  \
            std::is_base_of_v<Object, Return>,                                          \
            "iterator must dereference to a subclass of Object.  Check your "           \
            "specialization of __iter__ for this types and ensure the Return type "     \
            "is a subclass of py::Object."                                              \
        );                                                                              \
        return operator_begin<Return>(*this);                                           \
    }                                                                                   \
                                                                                        \
    template <typename Self = cls> requires (__iter__<Self>::enable)                    \
    auto cbegin() const { return begin<Self>(); }                                       \
                                                                                        \
    template <typename Self = cls> requires (__iter__<Self>::enable)                    \
    auto end() const {                                                                  \
        using Return = typename __iter__<Self>::Return;                                 \
        static_assert(                                                                  \
            std::is_base_of_v<Object, Return>,                                          \
            "iterator must dereference to a subclass of Object.  Check your "           \
            "specialization of __iter__ for this types and ensure the Return type "     \
            "is a subclass of py::Object."                                              \
        );                                                                              \
        return operator_end<Return>(*this);                                             \
    }                                                                                   \
                                                                                        \
    template <typename Self = cls> requires (__iter__<Self>::enable)                    \
    auto cend() const { return end<Self>(); }                                           \
                                                                                        \
    template <typename Self = cls> requires (__reversed__<Self>::enable)                \
    auto rbegin() const {                                                               \
        using Return = typename __reversed__<Self>::Return;                             \
        static_assert(                                                                  \
            std::is_base_of_v<Object, Return>,                                          \
            "iterator must dereference to a subclass of Object.  Check your "           \
            "specialization of __reversed__ for this types and ensure the Return "      \
            "type is a subclass of py::Object."                                         \
        );                                                                              \
        return operator_rbegin<Return>(*this);                                          \
    }                                                                                   \
                                                                                        \
    template <typename Self = cls> requires (__reversed__<Self>::enable)                \
    auto crbegin() const { return rbegin<Self>(); }                                     \
                                                                                        \
    template <typename Self = cls> requires (__reversed__<Self>::enable)                \
    auto rend() const {                                                                 \
        using Return = typename __reversed__<Self>::Return;                             \
        static_assert(                                                                  \
            std::is_base_of_v<Object, Return>,                                          \
            "iterator must dereference to a subclass of Object.  Check your "           \
            "specialization of __reversed__ for this types and ensure the Return "      \
            "type is a subclass of py::Object."                                         \
        );                                                                              \
        return operator_rend<Return>(*this);                                            \
    }                                                                                   \
                                                                                        \
    template <typename Self = cls> requires (__reversed__<Self>::enable)                \
    auto crend() const { return rend<Self>(); }                                         \
                                                                                        \
    template <typename Self> requires (__contains__<cls, Self>::enable)                 \
    bool contains(const Self& key) const {                                              \
        using Return = typename __contains__<cls, Self>::Return;                        \
        static_assert(                                                                  \
            std::is_same_v<Return, bool>,                                               \
            "contains() operator must return a boolean value.  Check your "             \
            "specialization of __contains__ for these types and ensure the Return "     \
            "type is set to bool."                                                      \
        );                                                                              \
        if constexpr (impl::proxy_like<Self>) {                                         \
            return this->contains(key.value());                                         \
        } else {                                                                        \
            return operator_contains<Return>(*this, key);                               \
        }                                                                               \
    }                                                                                   \
                                                                                        \
    template <typename Self = cls> requires (__len__<Self>::enable)                     \
    size_t size() const {                                                               \
        using Return = typename __len__<Self>::Return;                                  \
        static_assert(                                                                  \
            std::is_same_v<Return, size_t>,                                             \
            "size() operator must return a size_t for compatibility with C++ "          \
            "containers.  Check your specialization of __len__ for these types "        \
            "and ensure the Return type is set to size_t."                              \
        );                                                                              \
        return operator_len<Return>(*this);                                             \
    }                                                                                   \
                                                                                        \
protected:                                                                              \
                                                                                        \
    template <typename Return, typename Self, typename... Args>                         \
    friend auto impl::ops::operator_call(const Self& self, Args&&... args);             \
    template <typename Return, typename Self, typename Key>                             \
    friend auto impl::ops::operator_getitem(const Self& self, const Key& key);          \
    template <typename Return, typename Self>                                           \
    friend auto impl::ops::operator_getitem(                                            \
        const Self& self,                                                               \
        std::initializer_list<impl::SliceInitializer> slice                             \
    );                                                                                  \
    template <typename Return, typename Self>                                           \
    friend auto impl::ops::operator_begin(const Self& obj);                             \
    template <typename Return, typename Self>                                           \
    friend auto impl::ops::operator_end(const Self& obj);                               \
    template <typename Return, typename Self>                                           \
    friend auto impl::ops::operator_rbegin(const Self& obj);                            \
    template <typename Return, typename Self>                                           \
    friend auto impl::ops::operator_rend(const Self& obj);                              \
    template <typename Return, typename Self, typename Key>                             \
    friend auto impl::ops::operator_contains(const Self& self, const Key& key);         \
    template <typename Return, typename Self>                                           \
    friend auto impl::ops::operator_len(const Self& self);                              \
    template <typename Self>                                                            \
    friend auto impl::ops::operator_dereference(const Self& self);                      \
    template <typename Return, typename L, typename R>                                  \
    friend auto impl::ops::operator_lt(const L& lhs, const R& rhs);                     \
    template <typename Return, typename L, typename R>                                  \
    friend auto impl::ops::operator_le(const L& lhs, const R& rhs);                     \
    template <typename Return, typename L, typename R>                                  \
    friend auto impl::ops::operator_eq(const L& lhs, const R& rhs);                     \
    template <typename Return, typename L, typename R>                                  \
    friend auto impl::ops::operator_ne(const L& lhs, const R& rhs);                     \
    template <typename Return, typename L, typename R>                                  \
    friend auto impl::ops::operator_ge(const L& lhs, const R& rhs);                     \
    template <typename Return, typename L, typename R>                                  \
    friend auto impl::ops::operator_gt(const L& lhs, const R& rhs);                     \
    template <typename Return, typename Self>                                           \
    friend auto impl::ops::operator_abs(const Self& self);                              \
    template <typename Return, typename Self>                                           \
    friend auto impl::ops::operator_invert(const Self& self);                           \
    template <typename Return, typename Self>                                           \
    friend auto impl::ops::operator_pos(const Self& self);                              \
    template <typename Return, typename Self>                                           \
    friend auto impl::ops::operator_increment(Self& self);                              \
    template <typename Return, typename L, typename R>                                  \
    friend auto impl::ops::operator_add(const L& lhs, const R& rhs);                    \
    template <typename Return, typename L, typename R>                                  \
    friend auto impl::ops::operator_iadd(L& lhs, const R& rhs);                         \
    template <typename Return, typename Self>                                           \
    friend auto impl::ops::operator_neg(const Self& self);                              \
    template <typename Return, typename Self>                                           \
    friend auto impl::ops::operator_decrement(Self& self);                              \
    template <typename Return, typename L, typename R>                                  \
    friend auto impl::ops::operator_sub(const L& lhs, const R& rhs);                    \
    template <typename Return, typename L, typename R>                                  \
    friend auto impl::ops::operator_isub(L& lhs, const R& rhs);                         \
    template <typename Return, typename L, typename R>                                  \
    friend auto impl::ops::operator_mul(const L& lhs, const R& rhs);                    \
    template <typename Return, typename L, typename R>                                  \
    friend auto impl::ops::operator_imul(L& lhs, const R& rhs);                         \
    template <typename Return, typename L, typename R>                                  \
    friend auto impl::ops::operator_truediv(const L& lhs, const R& rhs);                \
    template <typename Return, typename L, typename R>                                  \
    friend auto impl::ops::operator_itruediv(L& lhs, const R& rhs);                     \
    template <typename Return, typename L, typename R>                                  \
    friend auto impl::ops::operator_floordiv(const L& lhs, const R& rhs);               \
    template <typename Return, typename L, typename R>                                  \
    friend auto impl::ops::operator_mod(const L& lhs, const R& rhs);                    \
    template <typename Return, typename L, typename R>                                  \
    friend auto impl::ops::operator_imod(L& lhs, const R& rhs);                         \
    template <typename Return, typename Base, typename Exp>                             \
    friend auto impl::ops::operator_pow(const Base& base, const Exp& exp);              \
    template <typename Return, typename Base, typename Exp, typename Mod>               \
    friend auto impl::ops::operator_pow(const Base& base, const Exp& exp, const Mod& mod);\
    template <typename Return, typename L, typename R>                                  \
    friend auto impl::ops::operator_lshift(const L& lhs, const R& rhs);                 \
    template <typename Return, typename L, typename R>                                  \
    friend auto impl::ops::operator_ilshift(L& lhs, const R& rhs);                      \
    template <typename Return, typename L, typename R>                                  \
    friend auto impl::ops::operator_rshift(const L& lhs, const R& rhs);                 \
    template <typename Return, typename L, typename R>                                  \
    friend auto impl::ops::operator_irshift(L& lhs, const R& rhs);                      \
    template <typename Return, typename L, typename R>                                  \
    friend auto impl::ops::operator_and(const L& lhs, const R& rhs);                    \
    template <typename Return, typename L, typename R>                                  \
    friend auto impl::ops::operator_iand(L& lhs, const R& rhs);                         \
    template <typename Return, typename L, typename R>                                  \
    friend auto impl::ops::operator_or(const L& lhs, const R& rhs);                     \
    template <typename Return, typename L, typename R>                                  \
    friend auto impl::ops::operator_ior(L& lhs, const R& rhs);                          \
    template <typename Return, typename L, typename R>                                  \
    friend auto impl::ops::operator_xor(const L& lhs, const R& rhs);                    \
    template <typename Return, typename L, typename R>                                  \
    friend auto impl::ops::operator_ixor(L& lhs, const R& rhs);                         \
                                                                                        \
public:                                                                                 \


/* Base class for enabled operators.  Encodes the return type as a template
parameter. */
template <typename T>
struct Returns {
    static constexpr bool enable = true;
    using Return = T;
};


namespace impl {

    namespace ops {

        template <typename Return, typename Self, typename... Args>
        auto operator_call(const Self& self, Args&&... args) {
            return Self::template operator_call<Return>(
                self,
                std::forward<Args>(args)...
            );
        }

        template <typename Return, typename Self, typename Key>
        auto operator_getitem(const Self& self, const Key& key) {
            return Self::template operator_getitem<Return>(self, key);
        }

        template <typename Return, typename Self>
        auto operator_getitem(
            const Self& self,
            std::initializer_list<impl::SliceInitializer> slice
        ) {
            return Self::template operator_getitem<Return>(self, slice);
        }

        template <typename Return, typename Self>
        auto operator_begin(const Self& obj) {
            return Self::template operator_begin<Return>(obj);
        }

        template <typename Return, typename Self>
        auto operator_end(const Self& obj) {
            return Self::template operator_end<Return>(obj);
        }

        template <typename Return, typename Self>
        auto operator_rbegin(const Self& obj) {
            return Self::template operator_rbegin<Return>(obj);
        }

        template <typename Return, typename Self>
        auto operator_rend(const Self& obj) {
            return Self::template operator_rend<Return>(obj);
        }

        template <typename Return, typename Self, typename Key>
        auto operator_contains(const Self& self, const Key& key) {
            return Self::template operator_contains<Return>(self, key);
        }

        template <typename Return, typename Self>
        auto operator_len(const Self& self) {
            return Self::template operator_len<Return>(self);
        }

        template <typename Self>
        auto operator_dereference(const Self& self) {
            return Self::template operator_dereference(self);
        }

        template <typename Return, typename L, typename R>
        auto operator_lt(const L& lhs, const R& rhs) {
            if constexpr (std::is_base_of_v<Object, L>) {
                return L::template operator_lt<Return>(lhs, rhs);
            } else {
                return R::template operator_lt<Return>(lhs, rhs);
            }
        }

        template <typename Return, typename L, typename R>
        auto operator_le(const L& lhs, const R& rhs) {
            if constexpr (std::is_base_of_v<Object, L>) {
                return L::template operator_le<Return>(lhs, rhs);
            } else {
                return R::template operator_le<Return>(lhs, rhs);
            }
        }

        template <typename Return, typename L, typename R>
        auto operator_eq(const L& lhs, const R& rhs) {
            if constexpr (std::is_base_of_v<Object, L>) {
                return L::template operator_eq<Return>(lhs, rhs);
            } else {
                return R::template operator_eq<Return>(lhs, rhs);
            }
        }

        template <typename Return, typename L, typename R>
        auto operator_ne(const L& lhs, const R& rhs) {
            if constexpr (std::is_base_of_v<Object, L>) {
                return L::template operator_ne<Return>(lhs, rhs);
            } else {
                return R::template operator_ne<Return>(lhs, rhs);
            }
        }

        template <typename Return, typename L, typename R>
        auto operator_ge(const L& lhs, const R& rhs) {
            if constexpr (std::is_base_of_v<Object, L>) {
                return L::template operator_ge<Return>(lhs, rhs);
            } else {
                return R::template operator_ge<Return>(lhs, rhs);
            }
        }

        template <typename Return, typename L, typename R>
        auto operator_gt(const L& lhs, const R& rhs) {
            if constexpr (std::is_base_of_v<Object, L>) {
                return L::template operator_gt<Return>(lhs, rhs);
            } else {
                return R::template operator_gt<Return>(lhs, rhs);
            }
        }

        template <typename Return, typename Self>
        auto operator_abs(const Self& self) {
            return Self::template operator_abs<Return>(self);
        }

        template <typename Return, typename Self>
        auto operator_invert(const Self& self) {
            return Self::template operator_invert<Return>(self);
        }

        template <typename Return, typename Self>
        auto operator_pos(const Self& self) {
            return Self::template operator_pos<Return>(self);
        }

        template <typename Return, typename Self>
        auto operator_increment(Self& self) {
            return Self::template operator_increment<Return>(self);
        }

        template <typename Return, typename L, typename R>
        auto operator_add(const L& lhs, const R& rhs) {
            if constexpr (std::is_base_of_v<Object, L>) {
                return L::template operator_add<Return>(lhs, rhs);
            } else {
                return R::template operator_add<Return>(lhs, rhs);
            }
        }

        template <typename Return, typename L, typename R>
        auto operator_iadd(L& lhs, const R& rhs) {
            return L::template operator_iadd<Return>(lhs, rhs);
        }

        template <typename Return, typename Self>
        auto operator_neg(const Self& self) {
            return Self::template operator_neg<Return>(self);
        }

        template <typename Return, typename Self>
        auto operator_decrement(Self& self) {
            return Self::template operator_decrement<Return>(self);
        }

        template <typename Return, typename L, typename R>
        auto operator_sub(const L& lhs, const R& rhs) {
            if constexpr (std::is_base_of_v<Object, L>) {
                return L::template operator_sub<Return>(lhs, rhs);
            } else {
                return R::template operator_sub<Return>(lhs, rhs);
            }
        }

        template <typename Return, typename L, typename R>
        auto operator_isub(L& lhs, const R& rhs) {
            return L::template operator_isub<Return>(lhs, rhs);
        }

        template <typename Return, typename L, typename R>
        auto operator_mul(const L& lhs, const R& rhs) {
            if constexpr (std::is_base_of_v<Object, L>) {
                return L::template operator_mul<Return>(lhs, rhs);
            } else {
                return R::template operator_mul<Return>(lhs, rhs);
            }
        }

        template <typename Return, typename L, typename R>
        auto operator_imul(L& lhs, const R& rhs) {
            return L::template operator_imul<Return>(lhs, rhs);
        }

        template <typename Return, typename L, typename R>
        auto operator_truediv(const L& lhs, const R& rhs) {
            if constexpr (std::is_base_of_v<Object, L>) {
                return L::template operator_truediv<Return>(lhs, rhs);
            } else {
                return R::template operator_truediv<Return>(lhs, rhs);
            }
        }

        template <typename Return, typename L, typename R>
        auto operator_itruediv(L& lhs, const R& rhs) {
            return L::template operator_itruediv<Return>(lhs, rhs);
        }

        template <typename Return, typename L, typename R>
        auto operator_floordiv(const L& lhs, const R& rhs) {
            if constexpr (std::is_base_of_v<Object, L>) {
                return L::template operator_floordiv<Return>(lhs, rhs);
            } else {
                return R::template operator_floordiv<Return>(lhs, rhs);
            }
        }

        template <typename Return, typename L, typename R>
        auto operator_ifloordiv(L& lhs, const R& rhs) {
            return L::template operator_ifloordiv<Return>(lhs, rhs);
        }

        template <typename Return, typename L, typename R>
        auto operator_mod(const L& lhs, const R& rhs) {
            if constexpr (std::is_base_of_v<Object, L>) {
                return L::template operator_mod<Return>(lhs, rhs);
            } else {
                return R::template operator_mod<Return>(lhs, rhs);
            }
        }

        template <typename Return, typename L, typename R>
        auto operator_imod(L& lhs, const R& rhs) {
            return L::template operator_imod<Return>(lhs, rhs);
        }

        template <typename Return, typename Base, typename Exp>
        auto operator_pow(const Base& base, const Exp& exp) {
            if constexpr (std::is_base_of_v<Object, Base>) {
                return Base::template operator_pow<Return>(base, exp);
            } else {
                return Exp::template operator_pow<Return>(base, exp);
            }
        }

        template <typename Return, typename Base, typename Exp, typename Mod>
        auto operator_pow(const Base& base, const Exp& exp, const Mod& mod) {
            if constexpr (std::is_base_of_v<Object, Base>) {
                return Base::template operator_pow<Return>(base, exp, mod);
            } else {
                return Exp::template operator_pow<Return>(base, exp, mod);
            }
        }

        template <typename Return, typename Base, typename Exp>
        auto operator_ipow(Base& base, const Exp& exp) {
            return Base::template operator_ipow<Return>(base, exp);
        }

        template <typename Return, typename Base, typename Exp, typename Mod>
        auto operator_ipow(Base& base, const Exp& exp, const Mod& mod) {
            return Base::template operator_ipow<Return>(base, exp, mod);
        }

        template <typename Return, typename L, typename R>
        auto operator_lshift(const L& lhs, const R& rhs) {
            if constexpr (std::is_base_of_v<Object, L>) {
                return L::template operator_lshift<Return>(lhs, rhs);
            } else {
                return R::template operator_lshift<Return>(lhs, rhs);
            }
        }

        template <typename Return, typename L, typename R>
        auto operator_ilshift(L& lhs, const R& rhs) {
            return L::template operator_ilshift<Return>(lhs, rhs);
        }

        template <typename Return, typename L, typename R>
        auto operator_rshift(const L& lhs, const R& rhs) {
            if constexpr (std::is_base_of_v<Object, L>) {
                return L::template operator_rshift<Return>(lhs, rhs);
            } else {
                return R::template operator_rshift<Return>(lhs, rhs);
            }
        }

        template <typename Return, typename L, typename R>
        auto operator_irshift(L& lhs, const R& rhs) {
            return L::template operator_irshift<Return>(lhs, rhs);
        }

        template <typename Return, typename L, typename R>
        auto operator_and(const L& lhs, const R& rhs) {
            if constexpr (std::is_base_of_v<Object, L>) {
                return L::template operator_and<Return>(lhs, rhs);
            } else {
                return R::template operator_and<Return>(lhs, rhs);
            }
        }

        template <typename Return, typename L, typename R>
        auto operator_iand(L& lhs, const R& rhs) {
            return L::template operator_iand<Return>(lhs, rhs);
        }

        template <typename Return, typename L, typename R>
        auto operator_or(const L& lhs, const R& rhs) {
            if constexpr (std::is_base_of_v<Object, L>) {
                return L::template operator_or<Return>(lhs, rhs);
            } else {
                return R::template operator_or<Return>(lhs, rhs);
            }
        }

        template <typename Return, typename L, typename R>
        auto operator_ior(L& lhs, const R& rhs) {
            return L::template operator_ior<Return>(lhs, rhs);
        }

        template <typename Return, typename L, typename R>
        auto operator_xor(const L& lhs, const R& rhs) {
            if constexpr (std::is_base_of_v<Object, L>) {
                return L::template operator_xor<Return>(lhs, rhs);
            } else {
                return R::template operator_xor<Return>(lhs, rhs);
            }
        }

        template <typename Return, typename L, typename R>
        auto operator_ixor(L& lhs, const R& rhs) {
            return L::template operator_ixor<Return>(lhs, rhs);
        }

    }

    // NOTE: using a secondary helper struct to handle double underscore attributes
    // delays template instantiation enough to prevent ambiguities with specializations
    // in subclasses, which may be generic on either the object type and/or attribute
    // name.  Effectively, getters and setters for all underscore methods are made
    // available for free on all subclasses of py::Object.

    // TODO: setattr_helper has to account for Value type?

    template <StaticStr name>
    struct getattr_helper {
        static constexpr bool enable = false;
    };

    template <StaticStr name>
    struct setattr_helper {
        static constexpr bool enable = false;
    };

    template <StaticStr name>
    struct delattr_helper {
        static constexpr bool enable = false;
    };

    template <> struct getattr_helper<"__dict__">           : Returns<Dict> {};
    template <> struct setattr_helper<"__dict__">           : Returns<void> {};
    template <> struct delattr_helper<"__dict__">           : Returns<void> {};
    template <> struct getattr_helper<"__class__">          : Returns<Type> {};
    template <> struct setattr_helper<"__class__">          : Returns<void> {};
    template <> struct delattr_helper<"__class__">          : Returns<void> {};
    template <> struct getattr_helper<"__bases__">          : Returns<Tuple> {};
    template <> struct setattr_helper<"__bases__">          : Returns<void> {};
    template <> struct delattr_helper<"__bases__">          : Returns<void> {};
    template <> struct getattr_helper<"__name__">           : Returns<Str> {};
    template <> struct setattr_helper<"__name__">           : Returns<void> {};
    template <> struct delattr_helper<"__name__">           : Returns<void> {};
    template <> struct getattr_helper<"__qualname__">       : Returns<Str> {};
    template <> struct setattr_helper<"__qualname__">       : Returns<void> {};
    template <> struct delattr_helper<"__qualname__">       : Returns<void> {};
    template <> struct getattr_helper<"__type_params__">    : Returns<Object> {};  // type?
    template <> struct setattr_helper<"__type_params__">    : Returns<void> {};
    template <> struct delattr_helper<"__type_params__">    : Returns<void> {};
    template <> struct getattr_helper<"__mro__">            : Returns<Tuple> {};
    template <> struct setattr_helper<"__mro__">            : Returns<void> {};
    template <> struct delattr_helper<"__mro__">            : Returns<void> {};
    template <> struct getattr_helper<"__subclasses__">     : Returns<Function> {};
    template <> struct setattr_helper<"__subclasses__">     : Returns<void> {};
    template <> struct delattr_helper<"__subclasses__">     : Returns<void> {};
    template <> struct getattr_helper<"__doc__">            : Returns<Str> {};
    template <> struct setattr_helper<"__doc__">            : Returns<void> {};
    template <> struct delattr_helper<"__doc__">            : Returns<void> {};
    template <> struct getattr_helper<"__module__">         : Returns<Str> {};
    template <> struct setattr_helper<"__module__">         : Returns<void> {};
    template <> struct delattr_helper<"__module__">         : Returns<void> {};
    template <> struct getattr_helper<"__new__">            : Returns<Function> {};
    template <> struct setattr_helper<"__new__">            : Returns<void> {};
    template <> struct delattr_helper<"__new__">            : Returns<void> {};
    template <> struct getattr_helper<"__init__">           : Returns<Function> {};
    template <> struct setattr_helper<"__init__">           : Returns<void> {};
    template <> struct delattr_helper<"__init__">           : Returns<void> {};
    template <> struct getattr_helper<"__del__">            : Returns<Function> {};
    template <> struct setattr_helper<"__del__">            : Returns<void> {};
    template <> struct delattr_helper<"__del__">            : Returns<void> {};
    template <> struct getattr_helper<"__repr__">           : Returns<Function> {};
    template <> struct setattr_helper<"__repr__">           : Returns<void> {};
    template <> struct delattr_helper<"__repr__">           : Returns<void> {};
    template <> struct getattr_helper<"__str__">            : Returns<Function> {};
    template <> struct setattr_helper<"__str__">            : Returns<void> {};
    template <> struct delattr_helper<"__str__">            : Returns<void> {};
    template <> struct getattr_helper<"__bytes__">          : Returns<Function> {};
    template <> struct setattr_helper<"__bytes__">          : Returns<void> {};
    template <> struct delattr_helper<"__bytes__">          : Returns<void> {};
    template <> struct getattr_helper<"__format__">         : Returns<Function> {};
    template <> struct setattr_helper<"__format__">         : Returns<void> {};
    template <> struct delattr_helper<"__format__">         : Returns<void> {};
    template <> struct getattr_helper<"__bool__">           : Returns<Function> {};
    template <> struct setattr_helper<"__bool__">           : Returns<void> {};
    template <> struct delattr_helper<"__bool__">           : Returns<void> {};
    template <> struct getattr_helper<"__dir__">            : Returns<Function> {};
    template <> struct setattr_helper<"__dir__">            : Returns<void> {};
    template <> struct delattr_helper<"__dir__">            : Returns<void> {};
    template <> struct getattr_helper<"__get__">            : Returns<Function> {};
    template <> struct setattr_helper<"__get__">            : Returns<void> {};
    template <> struct delattr_helper<"__get__">            : Returns<void> {};
    template <> struct getattr_helper<"__set__">            : Returns<Function> {};
    template <> struct setattr_helper<"__set__">            : Returns<void> {};
    template <> struct delattr_helper<"__set__">            : Returns<void> {};
    template <> struct getattr_helper<"__delete__">         : Returns<Function> {};
    template <> struct setattr_helper<"__delete__">         : Returns<void> {};
    template <> struct delattr_helper<"__delete__">         : Returns<void> {};
    template <> struct getattr_helper<"__self__">           : Returns<Object> {};
    template <> struct setattr_helper<"__self__">           : Returns<void> {};
    template <> struct delattr_helper<"__self__">           : Returns<void> {};
    template <> struct getattr_helper<"__wrapped__">        : Returns<Object> {};
    template <> struct setattr_helper<"__wrapped__">        : Returns<void> {};
    template <> struct delattr_helper<"__wrapped__">        : Returns<void> {};
    template <> struct getattr_helper<"__objclass__">       : Returns<Object> {};
    template <> struct setattr_helper<"__objclass__">       : Returns<void> {};
    template <> struct delattr_helper<"__objclass__">       : Returns<void> {};
    template <> struct getattr_helper<"__slots__">          : Returns<Object> {};
    template <> struct setattr_helper<"__slots__">          : Returns<void> {};
    template <> struct delattr_helper<"__slots__">          : Returns<void> {};
    template <> struct getattr_helper<"__init_subclass__">  : Returns<Function> {};
    template <> struct setattr_helper<"__init_subclass__">  : Returns<void> {};
    template <> struct delattr_helper<"__init_subclass__">  : Returns<void> {};
    template <> struct getattr_helper<"__set_name__">       : Returns<Function> {};
    template <> struct setattr_helper<"__set_name__">       : Returns<void> {};
    template <> struct delattr_helper<"__set_name__">       : Returns<void> {};
    template <> struct getattr_helper<"__instancecheck__">  : Returns<Function> {};
    template <> struct setattr_helper<"__instancecheck__">  : Returns<void> {};
    template <> struct delattr_helper<"__instancecheck__">  : Returns<void> {};
    template <> struct getattr_helper<"__subclasscheck__">  : Returns<Function> {};
    template <> struct setattr_helper<"__subclasscheck__">  : Returns<void> {};
    template <> struct delattr_helper<"__subclasscheck__">  : Returns<void> {};
    template <> struct getattr_helper<"__class_getitem__">  : Returns<Function> {};
    template <> struct setattr_helper<"__class_getitem__">  : Returns<void> {};
    template <> struct delattr_helper<"__class_getitem__">  : Returns<void> {};
    template <> struct getattr_helper<"__complex__">        : Returns<Function> {};
    template <> struct setattr_helper<"__complex__">        : Returns<void> {};
    template <> struct delattr_helper<"__complex__">        : Returns<void> {};
    template <> struct getattr_helper<"__int__">            : Returns<Function> {};
    template <> struct setattr_helper<"__int__">            : Returns<void> {};
    template <> struct delattr_helper<"__int__">            : Returns<void> {};
    template <> struct getattr_helper<"__float__">          : Returns<Function> {};
    template <> struct setattr_helper<"__float__">          : Returns<void> {};
    template <> struct delattr_helper<"__float__">          : Returns<void> {};
    template <> struct getattr_helper<"__index__">          : Returns<Function> {};
    template <> struct setattr_helper<"__index__">          : Returns<void> {};
    template <> struct delattr_helper<"__index__">          : Returns<void> {};
    template <> struct getattr_helper<"__enter__">          : Returns<Function> {};
    template <> struct setattr_helper<"__enter__">          : Returns<void> {};
    template <> struct delattr_helper<"__enter__">          : Returns<void> {};
    template <> struct getattr_helper<"__exit__">           : Returns<Function> {};
    template <> struct setattr_helper<"__exit__">           : Returns<void> {};
    template <> struct delattr_helper<"__exit__">           : Returns<void> {};
    template <> struct getattr_helper<"__match_args__">     : Returns<Tuple> {};
    template <> struct setattr_helper<"__match_args__">     : Returns<void> {};
    template <> struct delattr_helper<"__match_args__">     : Returns<void> {};
    template <> struct getattr_helper<"__buffer__">         : Returns<Function> {};
    template <> struct setattr_helper<"__buffer__">         : Returns<void> {};
    template <> struct delattr_helper<"__buffer__">         : Returns<void> {};
    template <> struct getattr_helper<"__release_buffer__"> : Returns<Function> {};
    template <> struct setattr_helper<"__release_buffer__"> : Returns<void> {};
    template <> struct delattr_helper<"__release_buffer__"> : Returns<void> {};
    template <> struct getattr_helper<"__await__">          : Returns<Function> {};
    template <> struct setattr_helper<"__await__">          : Returns<void> {};
    template <> struct delattr_helper<"__await__">          : Returns<void> {};
    template <> struct getattr_helper<"__aiter__">          : Returns<Function> {};
    template <> struct setattr_helper<"__aiter__">          : Returns<void> {};
    template <> struct delattr_helper<"__aiter__">          : Returns<void> {};
    template <> struct getattr_helper<"__anext__">          : Returns<Function> {};
    template <> struct setattr_helper<"__anext__">          : Returns<void> {};
    template <> struct delattr_helper<"__anext__">          : Returns<void> {};
    template <> struct getattr_helper<"__aenter__">         : Returns<Function> {};
    template <> struct setattr_helper<"__aenter__">         : Returns<void> {};
    template <> struct delattr_helper<"__aenter__">         : Returns<void> {};
    template <> struct getattr_helper<"__aexit__">          : Returns<Function> {};
    template <> struct setattr_helper<"__aexit__">          : Returns<void> {};
    template <> struct delattr_helper<"__aexit__">          : Returns<void> {};

    template <typename L, typename R>
    concept object_operand =
        std::derived_from<L, Object> || std::derived_from<R, Object>;

}


////////////////////
////    CALL    ////
////////////////////


namespace impl {
    template <> struct getattr_helper<"__call__">           : Returns<Function> {};
    template <> struct setattr_helper<"__call__">           : Returns<void> {};
    template <> struct delattr_helper<"__call__">           : Returns<void> {};
}


template <typename T, typename... Args>
struct __call__ { static constexpr bool enable = false; };
template <impl::proxy_like T, typename... Args>
struct __call__<T, Args...> : __call__<typename T::Wrapped, Args...> {};


////////////////////
////    ATTR    ////
////////////////////


namespace impl {
    template <> struct getattr_helper<"__getattr__">        : Returns<Function> {};
    template <> struct setattr_helper<"__getattr__">        : Returns<void> {};
    template <> struct delattr_helper<"__getattr__">        : Returns<void> {};

    template <> struct getattr_helper<"__getattribute__">   : Returns<Function> {};
    template <> struct setattr_helper<"__getattribute__">   : Returns<void> {};
    template <> struct delattr_helper<"__getattribute__">   : Returns<void> {};

    template <> struct getattr_helper<"__setattr__">        : Returns<Function> {};
    template <> struct setattr_helper<"__setattr__">        : Returns<void> {};
    template <> struct delattr_helper<"__setattr__">        : Returns<void> {};

    template <> struct getattr_helper<"__delattr__">        : Returns<Function> {};
    template <> struct setattr_helper<"__delattr__">        : Returns<void> {};
    template <> struct delattr_helper<"__delattr__">        : Returns<void> {};
}


template <typename T, StaticStr name>
struct __getattr__ { static constexpr bool enable = false; };
template <impl::proxy_like T, StaticStr name>
struct __getattr__<T, name> : __getattr__<typename T::Wrapped, name> {};
template <std::derived_from<Object> T, StaticStr name>
    requires (impl::getattr_helper<name>::enable)
struct __getattr__<T, name> : Returns<typename impl::getattr_helper<name>::Return> {};

template <typename T, StaticStr name, typename Value>
struct __setattr__ { static constexpr bool enable = false; };
template <impl::proxy_like T, StaticStr name, typename Value>
struct __setattr__<T, name, Value> : __setattr__<typename T::Wrapped, name, Value> {};
template <std::derived_from<Object> T, StaticStr name, typename Value>
    requires (impl::setattr_helper<name>::enable)
struct __setattr__<T, name, Value> : Returns<typename impl::setattr_helper<name>::Return> {};

template <typename T, StaticStr name>
struct __delattr__ { static constexpr bool enable = false; };
template <impl::proxy_like T, StaticStr name>
struct __delattr__<T, name> : __delattr__<typename T::Wrapped, name> {};
template <std::derived_from<Object> T, StaticStr name>
    requires (impl::delattr_helper<name>::enable)
struct __delattr__<T, name> : Returns<typename impl::delattr_helper<name>::Return> {};


////////////////////
////    ITEM    ////
////////////////////


namespace impl {
    template <> struct getattr_helper<"__getitem__">        : Returns<Function> {};
    template <> struct setattr_helper<"__getitem__">        : Returns<void> {};
    template <> struct delattr_helper<"__getitem__">        : Returns<void> {};

    template <> struct getattr_helper<"__setitem__">        : Returns<Function> {};
    template <> struct setattr_helper<"__setitem__">        : Returns<void> {};
    template <> struct delattr_helper<"__setitem__">        : Returns<void> {};

    template <> struct getattr_helper<"__delitem__">        : Returns<Function> {};
    template <> struct setattr_helper<"__delitem__">        : Returns<void> {};
    template <> struct delattr_helper<"__delitem__">        : Returns<void> {};

    template <> struct getattr_helper<"__missing__">        : Returns<Function> {};
    template <> struct setattr_helper<"__missing__">        : Returns<void> {};
    template <> struct delattr_helper<"__missing__">        : Returns<void> {};
}


template <typename T, typename Key>
struct __getitem__ { static constexpr bool enable = false; };
template <impl::proxy_like T, typename Key>
struct __getitem__<T, Key> : __getitem__<typename T::Wrapped, Key> {};

template <typename T, typename Key, typename Value>
struct __setitem__ { static constexpr bool enable = false; };
template <impl::proxy_like T, typename Key, typename Value>
struct __setitem__<T, Key, Value> : __setitem__<typename T::Wrapped, Key, Value> {};

template <typename T, typename Key>
struct __delitem__ { static constexpr bool enable = false; };
template <impl::proxy_like T, typename Key>
struct __delitem__<T, Key> : __delitem__<typename T::Wrapped, Key> {};


////////////////////
////    SIZE    ////
////////////////////


namespace impl {
    template <> struct getattr_helper<"__len__">            : Returns<Function> {};
    template <> struct setattr_helper<"__len__">            : Returns<void> {};
    template <> struct delattr_helper<"__len__">            : Returns<void> {};

    template <> struct getattr_helper<"__length_hint__">    : Returns<Function> {};
    template <> struct setattr_helper<"__length_hint__">    : Returns<void> {};
    template <> struct delattr_helper<"__length_hint__">    : Returns<void> {};
}


template <typename T>
struct __len__ { static constexpr bool enable = false; };
template <impl::proxy_like T>
struct __len__<T> : __len__<typename T::Wrapped> {};


////////////////////
////    ITER    ////
////////////////////


namespace impl {
    template <> struct getattr_helper<"__iter__">           : Returns<Function> {};
    template <> struct setattr_helper<"__iter__">           : Returns<void> {};
    template <> struct delattr_helper<"__iter__">           : Returns<void> {};

    template <> struct getattr_helper<"__next__">           : Returns<Function> {};
    template <> struct setattr_helper<"__next__">           : Returns<void> {};
    template <> struct delattr_helper<"__next__">           : Returns<void> {};

    template <> struct getattr_helper<"__reversed__">       : Returns<Function> {};
    template <> struct setattr_helper<"__reversed__">       : Returns<void> {};
    template <> struct delattr_helper<"__reversed__">       : Returns<void> {};
}


template <typename T>
struct __iter__ { static constexpr bool enable = false; };
template <impl::proxy_like T>
struct __iter__<T> : __iter__<typename T::Wrapped> {};

template <typename T>
struct __reversed__ { static constexpr bool enable = false; };
template <impl::proxy_like T>
struct __reversed__<T> : __reversed__<typename T::Wrapped> {};


////////////////////////
////    CONTAINS    ////
////////////////////////


namespace impl {
    template <> struct getattr_helper<"__contains__">       : Returns<Function> {};
    template <> struct setattr_helper<"__contains__">       : Returns<void> {};
    template <> struct delattr_helper<"__contains__">       : Returns<void> {};
}


template <typename T, typename Key>
struct __contains__ { static constexpr bool enable = false; };
template <impl::proxy_like T, typename Key>
struct __contains__<T, Key> : __contains__<typename T::Wrapped, Key> {};


///////////////////////////
////    DEREFERENCE    ////
///////////////////////////


template <typename T> requires (__iter__<T>::enable)
auto operator*(const T& obj) {
    if constexpr (impl::proxy_like<T>) {
        return *obj.value();
    } else {
        return impl::ops::operator_dereference(obj);
    }
}


////////////////////
////    HASH    ////
////////////////////


namespace impl {
    template <> struct getattr_helper<"__hash__">           : Returns<Function> {};
    template <> struct setattr_helper<"__hash__">           : Returns<void> {};
    template <> struct delattr_helper<"__hash__">           : Returns<void> {};
}


template <typename T>
struct __hash__ { static constexpr bool enable = false; };
template <impl::proxy_like T>
struct __hash__<T> : __hash__<typename T::Wrapped> {};


/////////////////////////
////    LESS-THAN    ////
/////////////////////////


namespace impl {
    template <> struct getattr_helper<"__lt__">             : Returns<Function> {};
    template <> struct setattr_helper<"__lt__">             : Returns<void> {};
    template <> struct delattr_helper<"__lt__">             : Returns<void> {};
}


template <typename L, typename R>
struct __lt__ { static constexpr bool enable = false; };
template <impl::proxy_like L, typename R> requires (!impl::proxy_like<R>)
struct __lt__<L, R> : __lt__<typename L::Wrapped, R> {};
template <typename L, impl::proxy_like R> requires (!impl::proxy_like<L>)
struct __lt__<L, R> : __lt__<L, typename R::Wrapped> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __lt__<L, R> : __lt__<typename L::Wrapped, typename R::Wrapped> {};


template <typename L, typename R> requires (__lt__<L, R>::enable)
auto operator<(const L& lhs, const R& rhs) {
    using Return = typename __lt__<L, R>::Return;
    static_assert(
        std::is_same_v<Return, bool>,
        "Less-than operator must return a boolean value.  Check your "
        "specialization of __lt__ for these types and ensure the Return type "
        "is set to bool."
    );
    if constexpr (impl::proxy_like<L>) {
        return lhs.value() < rhs;
    } else if constexpr (impl::proxy_like<R>) {
        return lhs < rhs.value();
    } else {
        return impl::ops::operator_lt<Return>(lhs, rhs);
    }
}


template <typename L, typename R>
    requires (impl::object_operand<L, R> && !__lt__<L, R>::enable)
auto operator<(const L& lhs, const R& rhs) = delete;


//////////////////////////////////
////    LESS-THAN-OR-EQUAL    ////
//////////////////////////////////


namespace impl {
    template <> struct getattr_helper<"__le__">             : Returns<Function> {};
    template <> struct setattr_helper<"__le__">             : Returns<void> {};
    template <> struct delattr_helper<"__le__">             : Returns<void> {};
}


template <typename L, typename R>
struct __le__ { static constexpr bool enable = false; };
template <impl::proxy_like L, typename R> requires (!impl::proxy_like<R>)
struct __le__<L, R> : __le__<typename L::Wrapped, R> {};
template <typename L, impl::proxy_like R> requires (!impl::proxy_like<L>)
struct __le__<L, R> : __le__<L, typename R::Wrapped> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __le__<L, R> : __le__<typename L::Wrapped, typename R::Wrapped> {};


template <typename L, typename R> requires (__le__<L, R>::enable)
auto operator<=(const L& lhs, const R& rhs) {
    using Return = typename __le__<L, R>::Return;
    static_assert(
        std::is_same_v<Return, bool>,
        "Less-than-or-equal operator must return a boolean value.  Check your "
        "specialization of __le__ for this type and ensure the Return type is "
        "set to bool."
    );
    if constexpr (impl::proxy_like<L>) {
        return lhs.value() <= rhs;
    } else if constexpr (impl::proxy_like<R>) {
        return lhs <= rhs.value();
    } else {
        return impl::ops::operator_le<Return>(lhs, rhs);
    }
}


template <typename L, typename R>
    requires (impl::object_operand<L, R> && !__le__<L, R>::enable)
auto operator<=(const L& lhs, const R& rhs) = delete;


/////////////////////
////    EQUAL    ////
/////////////////////


namespace impl {
    template <> struct getattr_helper<"__eq__">             : Returns<Function> {};
    template <> struct setattr_helper<"__eq__">             : Returns<void> {};
    template <> struct delattr_helper<"__eq__">             : Returns<void> {};
}


template <typename L, typename R>
struct __eq__ { static constexpr bool enable = false; };
template <impl::proxy_like L, typename R> requires (!impl::proxy_like<R>)
struct __eq__<L, R> : __eq__<typename L::Wrapped, R> {};
template <typename L, impl::proxy_like R> requires (!impl::proxy_like<L>)
struct __eq__<L, R> : __eq__<L, typename R::Wrapped> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __eq__<L, R> : __eq__<typename L::Wrapped, typename R::Wrapped> {};


template <typename L, typename R> requires (__eq__<L, R>::enable)
auto operator==(const L& lhs, const R& rhs) {
    using Return = typename __eq__<L, R>::Return;
    static_assert(
        std::is_same_v<Return, bool>,
        "Equality operator must return a boolean value.  Check your "
        "specialization of __eq__ for this type and ensure the Return type is "
        "set to bool."
    );
    if constexpr (impl::proxy_like<L>) {
        return lhs.value() == rhs;
    } else if constexpr (impl::proxy_like<R>) {
        return lhs == rhs.value();
    } else {
        return impl::ops::operator_eq<Return>(lhs, rhs);
    }
}


template <typename L, typename R>
    requires (impl::object_operand<L, R> && !__eq__<L, R>::enable)
auto operator==(const L& lhs, const R& rhs) = delete;


////////////////////////
////   NOT-EQUAL    ////
////////////////////////


namespace impl {
    template <> struct getattr_helper<"__ne__">             : Returns<Function> {};
    template <> struct setattr_helper<"__ne__">             : Returns<void> {};
    template <> struct delattr_helper<"__ne__">             : Returns<void> {};
}


template <typename L, typename R>
struct __ne__ { static constexpr bool enable = false; };
template <impl::proxy_like L, typename R> requires (!impl::proxy_like<R>)
struct __ne__<L, R> : __ne__<typename L::Wrapped, R> {};
template <typename L, impl::proxy_like R> requires (!impl::proxy_like<L>)
struct __ne__<L, R> : __ne__<L, typename R::Wrapped> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __ne__<L, R> : __ne__<typename L::Wrapped, typename R::Wrapped> {};


template <typename L, typename R> requires (__ne__<L, R>::enable)
auto operator!=(const L& lhs, const R& rhs) {
    using Return = typename __ne__<L, R>::Return;
    static_assert(
        std::is_same_v<Return, bool>,
        "Inequality operator must return a boolean value.  Check your "
        "specialization of __ne__ for this type and ensure the Return type is "
        "set to bool."
    );
    if constexpr (impl::proxy_like<L>) {
        return lhs.value() != rhs;
    } else if constexpr (impl::proxy_like<R>) {
        return lhs != rhs.value();
    } else {
        return impl::ops::operator_ne<Return>(lhs, rhs);
    }
}


template <typename L, typename R>
    requires (impl::object_operand<L, R> && !__ne__<L, R>::enable)
auto operator!=(const L& lhs, const R& rhs) = delete;


/////////////////////////////////////
////    GREATER-THAN-OR-EQUAL    ////
/////////////////////////////////////


namespace impl {
    template <> struct getattr_helper<"__ge__">             : Returns<Function> {};
    template <> struct setattr_helper<"__ge__">             : Returns<void> {};
    template <> struct delattr_helper<"__ge__">             : Returns<void> {};
}


template <typename L, typename R>
struct __ge__ { static constexpr bool enable = false; };
template <impl::proxy_like L, typename R> requires (!impl::proxy_like<R>)
struct __ge__<L, R> : __ge__<typename L::Wrapped, R> {};
template <typename L, impl::proxy_like R> requires (!impl::proxy_like<L>)
struct __ge__<L, R> : __ge__<L, typename R::Wrapped> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __ge__<L, R> : __ge__<typename L::Wrapped, typename R::Wrapped> {};


template <typename L, typename R> requires (__ge__<L, R>::enable)
auto operator>=(const L& lhs, const R& rhs) {
    using Return = typename __ge__<L, R>::Return;
    static_assert(
        std::is_same_v<Return, bool>,
        "Greater-than-or-equal operator must return a boolean value.  Check "
        "your specialization of __ge__ for this type and ensure the Return "
        "type is set to bool."
    );
    if constexpr (impl::proxy_like<L>) {
        return lhs.value() >= rhs;
    } else if constexpr (impl::proxy_like<R>) {
        return lhs >= rhs.value();
    } else {
        return impl::ops::operator_ge<Return>(lhs, rhs);
    }
}


template <typename L, typename R>
    requires (impl::object_operand<L, R> && !__ge__<L, R>::enable)
auto operator>=(const L& lhs, const R& rhs) = delete;


////////////////////////////
////    GREATER-THAN    ////
////////////////////////////


namespace impl {
    template <> struct getattr_helper<"__gt__">             : Returns<Function> {};
    template <> struct setattr_helper<"__gt__">             : Returns<void> {};
    template <> struct delattr_helper<"__gt__">             : Returns<void> {};
}


template <typename L, typename R>
struct __gt__ { static constexpr bool enable = false; };
template <impl::proxy_like L, typename R> requires (!impl::proxy_like<R>)
struct __gt__<L, R> : __gt__<typename L::Wrapped, R> {};
template <typename L, impl::proxy_like R> requires (!impl::proxy_like<L>)
struct __gt__<L, R> : __gt__<L, typename R::Wrapped> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __gt__<L, R> : __gt__<typename L::Wrapped, typename R::Wrapped> {};


template <typename L, typename R> requires (__gt__<L, R>::enable)
auto operator>(const L& lhs, const R& rhs) {
    using Return = typename __gt__<L, R>::Return;
    static_assert(
        std::is_same_v<Return, bool>,
        "Greater-than operator must return a boolean value.  Check your "
        "specialization of __gt__ for this type and ensure the Return type is "
        "set to bool."
    );
    if constexpr (impl::proxy_like<L>) {
        return lhs.value() > rhs;
    } else if constexpr (impl::proxy_like<R>) {
        return lhs > rhs.value();
    } else {
        return impl::ops::operator_gt<Return>(lhs, rhs);
    }
}


template <typename L, typename R>
    requires (impl::object_operand<L, R> && !__gt__<L, R>::enable)
auto operator>(const L& lhs, const R& rhs) = delete;


///////////////////
////    ABS    ////
///////////////////


namespace impl {
    template <> struct getattr_helper<"__abs__">            : Returns<Function> {};
    template <> struct setattr_helper<"__abs__">            : Returns<void> {};
    template <> struct delattr_helper<"__abs__">            : Returns<void> {};
}


template <typename T>
struct __abs__ { static constexpr bool enable = false; };
template <impl::proxy_like T>
struct __abs__<T> : __abs__<typename T::Wrapped> {};


/* Equivalent to Python `abs(obj)` for any object that specializes the __abs__ control
struct. */
template <typename T> requires (__abs__<T>::enable)
auto abs(const T& obj) {
    using Return = __abs__<T>::Return;
    static_assert(
        std::is_base_of_v<Object, Return>,
        "Absolute value operator must return a py::Object subclass.  Check your "
        "specialization of __abs__ for this type and ensure the Return type is set to "
        "a py::Object subclass."
    );
    if constexpr (impl::proxy_like<T>) {
        return abs(obj.value());
    } else {
        return impl::ops::operator_abs<Return>(obj);
    }
}


/* Equivalent to Python `abs(obj)`, except that it takes a C++ value and applies
std::abs() for identical semantics. */
template <typename T> requires (!impl::python_like<T>)
auto abs(const T& value) {
    return std::abs(value);
}


//////////////////////
////    INVERT    ////
//////////////////////


namespace impl {
    template <> struct getattr_helper<"__invert__">         : Returns<Function> {};
    template <> struct setattr_helper<"__invert__">         : Returns<void> {};
    template <> struct delattr_helper<"__invert__">         : Returns<void> {};
}


template <typename T>
struct __invert__ { static constexpr bool enable = false; };
template <impl::proxy_like T>
struct __invert__<T> : __invert__<typename T::Wrapped> {};


template <typename T> requires (__invert__<T>::enable)
auto operator~(const T& self) {
    using Return = typename __invert__<T>::Return;
    static_assert(
        std::is_base_of_v<Object, Return>,
        "Bitwise NOT operator must return a py::Object subclass.  Check your "
        "specialization of __invert__ for this type and ensure the Return type "
        "is set to a py::Object subclass."
    );
    if constexpr (impl::proxy_like<T>) {
        return ~self.value();
    } else {
        return impl::ops::operator_invert<Return>(self);
    }
}


template <std::derived_from<Object> T> requires (!__invert__<T>::enable)
auto operator~(const T& self) = delete;


////////////////////////
////    POSITIVE    ////
////////////////////////


namespace impl {
    template <> struct getattr_helper<"__pos__">            : Returns<Function> {};
    template <> struct setattr_helper<"__pos__">            : Returns<void> {};
    template <> struct delattr_helper<"__pos__">            : Returns<void> {};
}


template <typename T>
struct __pos__ { static constexpr bool enable = false; };
template <impl::proxy_like T>
struct __pos__<T> : __pos__<typename T::Wrapped> {};


template <typename T> requires (__pos__<T>::enable)
auto operator+(const T& self) {
    using Return = typename __pos__<T>::Return;
    static_assert(
        std::is_base_of_v<Object, Return>,
        "Unary positive operator must return a py::Object subclass.  Check "
        "your specialization of __pos__ for this type and ensure the Return "
        "type is set to a py::Object subclass."
    );
    if constexpr (impl::proxy_like<T>) {
        return +self.value();
    } else {
        return impl::ops::operator_pos<Return>(self);
    }
}


template <std::derived_from<Object> T> requires (!__pos__<T>::enable)
auto operator+(const T& self) = delete;


////////////////////////
////    NEGATIVE    ////
////////////////////////


namespace impl {
    template <> struct getattr_helper<"__neg__">            : Returns<Function> {};
    template <> struct setattr_helper<"__neg__">            : Returns<void> {};
    template <> struct delattr_helper<"__neg__">            : Returns<void> {};
}


template <typename T>
struct __neg__ { static constexpr bool enable = false; };
template <impl::proxy_like T>
struct __neg__<T> : __neg__<typename T::Wrapped> {};


template <typename T> requires (__neg__<T>::enable)
auto operator-(const T& self) {
    using Return = typename __neg__<T>::Return;
    static_assert(
        std::is_base_of_v<Object, Return>,
        "Unary negative operator must return a py::Object subclass.  Check "
        "your specialization of __neg__ for this type and ensure the Return "
        "type is set to a py::Object subclass."
    );
    if constexpr (impl::proxy_like<T>) {
        return -self.value();
    } else {
        return impl::ops::operator_neg<Return>(self);
    }
}


template <std::derived_from<Object> T> requires (!__neg__<T>::enable)
auto operator-(const T& self) = delete;


/////////////////////////
////    INCREMENT    ////
/////////////////////////


template <typename T>
struct __increment__ { static constexpr bool enable = false; };
template <impl::proxy_like T>
struct __increment__<T> : __increment__<typename T::Wrapped> {};


template <typename T> requires (__increment__<T>::enable)
T& operator++(T& self) {
    using Return = typename __increment__<T>::Return;
    static_assert(
        std::is_same_v<Return, T>,
        "Increment operator must return a reference to the derived type.  "
        "Check your specialization of __increment__ for this type and ensure "
        "the Return type is set to the derived type."
    );
    if constexpr (impl::proxy_like<T>) {
        ++self.value();
    } else {
        impl::ops::operator_increment<Return>(self);
    }
    return self;
}


template <typename T> requires (__increment__<T>::enable)
T operator++(T& self, int) {
    using Return = typename __increment__<T>::Return;
    static_assert(
        std::is_same_v<Return, T>,
        "Increment operator must return a reference to the derived type.  "
        "Check your specialization of __increment__ for this type and ensure "
        "the Return type is set to the derived type."
    );
    T copy = self;
    if constexpr (impl::proxy_like<T>) {
        ++self.value();
    } else {
        impl::ops::operator_increment<Return>(self);
    }
    return copy;
}


template <std::derived_from<Object> T> requires (!__increment__<T>::enable)
T& operator++(T& self) = delete;


template <std::derived_from<Object> T> requires (!__increment__<T>::enable)
T operator++(T& self, int) = delete;


/////////////////////////
////    DECREMENT    ////
/////////////////////////


template <typename T>
struct __decrement__ { static constexpr bool enable = false; };
template <impl::proxy_like T>
struct __decrement__<T> : __decrement__<typename T::Wrapped> {};


template <typename T> requires (__decrement__<T>::enable)
T& operator--(T& self) {
    using Return = typename __decrement__<T>::Return;
    static_assert(
        std::is_same_v<Return, T>,
        "Decrement operator must return a reference to the derived type.  "
        "Check your specialization of __decrement__ for this type and ensure "
        "the Return type is set to the derived type."
    );
    if constexpr (impl::proxy_like<T>) {
        --self.value();
    } else {
        impl::ops::operator_decrement<Return>(self);
    }
    return self;
}


template <typename T> requires (__decrement__<T>::enable)
T operator--(T& self, int) {
    using Return = typename __decrement__<T>::Return;
    static_assert(
        std::is_same_v<Return, T>,
        "Decrement operator must return a reference to the derived type.  "
        "Check your specialization of __decrement__ for this type and ensure "
        "the Return type is set to the derived type."
    );
    T copy = self;
    if constexpr (impl::proxy_like<T>) {
        --self.value();
    } else {
        impl::ops::operator_decrement<Return>(self);
    }
    return copy;
}


template <std::derived_from<Object> T> requires (!__decrement__<T>::enable)
T& operator--(T& self) = delete;


template <std::derived_from<Object> T> requires (!__decrement__<T>::enable)
T operator--(T& self, int) = delete;


///////////////////
////    ADD    ////
///////////////////


namespace impl {
    template <> struct getattr_helper<"__add__">            : Returns<Function> {};
    template <> struct setattr_helper<"__add__">            : Returns<void> {};
    template <> struct delattr_helper<"__add__">            : Returns<void> {};

    template <> struct getattr_helper<"__radd__">           : Returns<Function> {};
    template <> struct setattr_helper<"__radd__">           : Returns<void> {};
    template <> struct delattr_helper<"__radd__">           : Returns<void> {};

    template <> struct getattr_helper<"__iadd__">           : Returns<Function> {};
    template <> struct setattr_helper<"__iadd__">           : Returns<void> {};
    template <> struct delattr_helper<"__iadd__">           : Returns<void> {};
}


template <typename L, typename R>
struct __add__ { static constexpr bool enable = false; };
template <impl::proxy_like L, typename R> requires (!impl::proxy_like<R>)
struct __add__<L, R> : __add__<typename L::Wrapped, R> {};
template <typename L, impl::proxy_like R> requires (!impl::proxy_like<L>)
struct __add__<L, R> : __add__<L, typename R::Wrapped> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __add__<L, R> : __add__<typename L::Wrapped, typename R::Wrapped> {};

template <typename L, typename R>
struct __iadd__ { static constexpr bool enable = false; };
template <impl::proxy_like L, typename R> requires (!impl::proxy_like<R>)
struct __iadd__<L, R> : __iadd__<typename L::Wrapped, R> {};
template <typename L, impl::proxy_like R> requires (!impl::proxy_like<L>)
struct __iadd__<L, R> : __iadd__<L, typename R::Wrapped> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __iadd__<L, R> : __iadd__<typename L::Wrapped, typename R::Wrapped> {};


template <typename L, typename R> requires (__add__<L, R>::enable)
auto operator+(const L& lhs, const R& rhs) {
    using Return = typename __add__<L, R>::Return;
    static_assert(
        std::is_base_of_v<Object, Return>,
        "Addition operator must return a py::Object subclass.  Check your "
        "specialization of __add__ for this type and ensure the Return type is "
        "derived from py::Object."
    );
    if constexpr (impl::proxy_like<L>) {
        return lhs.value() + rhs;
    } else if constexpr (impl::proxy_like<R>) {
        return lhs + rhs.value();
    } else {
        return impl::ops::operator_add<Return>(lhs, rhs);
    }
}


template <typename L, typename R> requires (__iadd__<L, R>::enable)
L& operator+=(L& lhs, const R& rhs) {
    using Return = typename __iadd__<L, R>::Return;
    static_assert(
        std::is_same_v<Return, L&>,
        "In-place addition operator must return a mutable reference to the left "
        "operand.  Check your specialization of __iadd__ for these types and "
        "ensure the Return type is set to the left operand."
    );
    if constexpr (impl::proxy_like<L>) {
        lhs.value() += rhs;
    } else if constexpr (impl::proxy_like<R>) {
        lhs += rhs.value();
    } else {
        impl::ops::operator_iadd<Return>(lhs, rhs);
    }
    return lhs;
}


template <typename L, typename R>
    requires (impl::object_operand<L, R> && !__add__<L, R>::enable)
auto operator+(const L& lhs, const R& rhs) = delete;


template <std::derived_from<Object> L, typename R> requires (!__iadd__<L, R>::enable)
auto operator+=(const L& lhs, const R& rhs) = delete;


////////////////////////
////    SUBTRACT    ////
////////////////////////


namespace impl {
    template <> struct getattr_helper<"__sub__">            : Returns<Function> {};
    template <> struct setattr_helper<"__sub__">            : Returns<void> {};
    template <> struct delattr_helper<"__sub__">            : Returns<void> {};

    template <> struct getattr_helper<"__rsub__">           : Returns<Function> {};
    template <> struct setattr_helper<"__rsub__">           : Returns<void> {};
    template <> struct delattr_helper<"__rsub__">           : Returns<void> {};

    template <> struct getattr_helper<"__isub__">           : Returns<Function> {};
    template <> struct setattr_helper<"__isub__">           : Returns<void> {};
    template <> struct delattr_helper<"__isub__">           : Returns<void> {};
}


template <typename L, typename R>
struct __sub__ { static constexpr bool enable = false; };
template <impl::proxy_like L, typename R> requires (!impl::proxy_like<R>)
struct __sub__<L, R> : __sub__<typename L::Wrapped, R> {};
template <typename L, impl::proxy_like R> requires (!impl::proxy_like<L>)
struct __sub__<L, R> : __sub__<L, typename R::Wrapped> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __sub__<L, R> : __sub__<typename L::Wrapped, typename R::Wrapped> {};

template <typename L, typename R>
struct __isub__ { static constexpr bool enable = false; };
template <impl::proxy_like L, typename R> requires (!impl::proxy_like<R>)
struct __isub__<L, R> : __isub__<typename L::Wrapped, R> {};
template <typename L, impl::proxy_like R> requires (!impl::proxy_like<L>)
struct __isub__<L, R> : __isub__<L, typename R::Wrapped> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __isub__<L, R> : __isub__<typename L::Wrapped, typename R::Wrapped> {};


template <typename L, typename R> requires (__sub__<L, R>::enable)
auto operator-(const L& lhs, const R& rhs) {
    using Return = typename __sub__<L, R>::Return;
    static_assert(
        std::is_base_of_v<Object, Return>,
        "Subtraction operator must return a py::Object subclass.  Check your "
        "specialization of __sub__ for this type and ensure the Return type is "
        "derived from py::Object."
    );
    if constexpr (impl::proxy_like<L>) {
        return lhs.value() - rhs;
    } else if constexpr (impl::proxy_like<R>) {
        return lhs - rhs.value();
    } else {
        return impl::ops::operator_sub<Return>(lhs, rhs);
    }
}


template <typename L, typename R> requires (__isub__<L, R>::enable)
L& operator-=(L& lhs, const R& rhs) {
    using Return = typename __isub__<L, R>::Return;
    static_assert(
        std::is_same_v<Return, L&>,
        "In-place addition operator must return a mutable reference to the left "
        "operand.  Check your specialization of __isub__ for these types and "
        "ensure the Return type is set to the left operand."
    );
    if constexpr (impl::proxy_like<L>) {
        lhs.value() -= rhs;
    } else if constexpr (impl::proxy_like<R>) {
        lhs -= rhs.value();
    } else {
        impl::ops::operator_isub<Return>(lhs, rhs);
    }
    return lhs;
}


template <typename L, typename R>
    requires (impl::object_operand<L, R> && !__sub__<L, R>::enable)
auto operator-(const L& lhs, const R& rhs) = delete;


template <std::derived_from<Object> L, typename R> requires (!__isub__<L, R>::enable)
auto operator-=(const L& lhs, const R& rhs) = delete;


////////////////////////
////    MULTIPLY    ////
////////////////////////


namespace impl {
    template <> struct getattr_helper<"__mul__">            : Returns<Function> {};
    template <> struct setattr_helper<"__mul__">            : Returns<void> {};
    template <> struct delattr_helper<"__mul__">            : Returns<void> {};

    template <> struct getattr_helper<"__matmul__">         : Returns<Function> {};
    template <> struct setattr_helper<"__matmul__">         : Returns<void> {};
    template <> struct delattr_helper<"__matmul__">         : Returns<void> {};

    template <> struct getattr_helper<"__rmul__">           : Returns<Function> {};
    template <> struct setattr_helper<"__rmul__">           : Returns<void> {};
    template <> struct delattr_helper<"__rmul__">           : Returns<void> {};

    template <> struct getattr_helper<"__rmatmul__">        : Returns<Function> {};
    template <> struct setattr_helper<"__rmatmul__">        : Returns<void> {};
    template <> struct delattr_helper<"__rmatmul__">        : Returns<void> {};

    template <> struct getattr_helper<"__imul__">           : Returns<Function> {};
    template <> struct setattr_helper<"__imul__">           : Returns<void> {};
    template <> struct delattr_helper<"__imul__">           : Returns<void> {};

    template <> struct getattr_helper<"__imatmul__">        : Returns<Function> {};
    template <> struct setattr_helper<"__imatmul__">        : Returns<void> {};
    template <> struct delattr_helper<"__imatmul__">        : Returns<void> {};
}

template <typename L, typename R>
struct __mul__ { static constexpr bool enable = false; };
template <impl::proxy_like L, typename R> requires (!impl::proxy_like<R>)
struct __mul__<L, R> : __mul__<typename L::Wrapped, R> {};
template <typename L, impl::proxy_like R> requires (!impl::proxy_like<L>)
struct __mul__<L, R> : __mul__<L, typename R::Wrapped> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __mul__<L, R> : __mul__<typename L::Wrapped, typename R::Wrapped> {};

template <typename L, typename R>
struct __imul__ { static constexpr bool enable = false; };
template <impl::proxy_like L, typename R> requires (!impl::proxy_like<R>)
struct __imul__<L, R> : __imul__<typename L::Wrapped, R> {};
template <typename L, impl::proxy_like R> requires (!impl::proxy_like<L>)
struct __imul__<L, R> : __imul__<L, typename R::Wrapped> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __imul__<L, R> : __imul__<typename L::Wrapped, typename R::Wrapped> {};


template <typename L, typename R> requires (__mul__<L, R>::enable)
auto operator*(const L& lhs, const R& rhs) {
    using Return = typename __mul__<L, R>::Return;
    static_assert(
        std::is_base_of_v<Object, Return>,
        "Multiplication operator must return a py::Object subclass.  Check "
        "your specialization of __mul__ for this type and ensure the Return "
        "type is derived from py::Object."
    );
    if constexpr (impl::proxy_like<L>) {
        return lhs.value() * rhs;
    } else if constexpr (impl::proxy_like<R>) {
        return lhs * rhs.value();
    } else {
        return impl::ops::operator_mul<Return>(lhs, rhs);
    }
}


template <typename L, typename R> requires (__imul__<L, R>::enable)
L& operator*=(L& lhs, const R& rhs) {
    using Return = typename __imul__<L, R>::Return;
    static_assert(
        std::is_same_v<Return, L&>,
        "In-place multiplication operator must return a mutable reference to the "
        "left operand.  Check your specialization of __imul__ for these types "
        "and ensure the Return type is set to the left operand."
    );
    if constexpr (impl::proxy_like<L>) {
        lhs.value() *= rhs;
    } else if constexpr (impl::proxy_like<R>) {
        lhs *= rhs.value();
    } else {
        impl::ops::operator_imul<Return>(lhs, rhs);
    }
    return lhs;
}


template <typename L, typename R>
    requires (impl::object_operand<L, R> && !__mul__<L, R>::enable)
auto operator*(const L& lhs, const R& rhs) = delete;


template <std::derived_from<Object> L, typename R> requires (!__imul__<L, R>::enable)
auto operator*=(const L& lhs, const R& rhs) = delete;


//////////////////////
////    DIVIDE    ////
//////////////////////


namespace impl {
    template <> struct getattr_helper<"__truediv__">        : Returns<Function> {};
    template <> struct setattr_helper<"__truediv__">        : Returns<void> {};
    template <> struct delattr_helper<"__truediv__">        : Returns<void> {};

    template <> struct getattr_helper<"__floordiv__">       : Returns<Function> {};
    template <> struct setattr_helper<"__floordiv__">       : Returns<void> {};
    template <> struct delattr_helper<"__floordiv__">       : Returns<void> {};

    template <> struct getattr_helper<"__rtruediv__">       : Returns<Function> {};
    template <> struct setattr_helper<"__rtruediv__">       : Returns<void> {};
    template <> struct delattr_helper<"__rtruediv__">       : Returns<void> {};

    template <> struct getattr_helper<"__rfloordiv__">      : Returns<Function> {};
    template <> struct setattr_helper<"__rfloordiv__">      : Returns<void> {};
    template <> struct delattr_helper<"__rfloordiv__">      : Returns<void> {};

    template <> struct getattr_helper<"__itruediv__">       : Returns<Function> {};
    template <> struct setattr_helper<"__itruediv__">       : Returns<void> {};
    template <> struct delattr_helper<"__itruediv__">       : Returns<void> {};

    template <> struct getattr_helper<"__ifloordiv__">      : Returns<Function> {};
    template <> struct setattr_helper<"__ifloordiv__">      : Returns<void> {};
    template <> struct delattr_helper<"__ifloordiv__">      : Returns<void> {};
}


template <typename L, typename R>
struct __truediv__ { static constexpr bool enable = false; };
template <impl::proxy_like L, typename R> requires (!impl::proxy_like<R>)
struct __truediv__<L, R> : __truediv__<typename L::Wrapped, R> {};
template <typename L, impl::proxy_like R> requires (!impl::proxy_like<L>)
struct __truediv__<L, R> : __truediv__<L, typename R::Wrapped> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __truediv__<L, R> : __truediv__<typename L::Wrapped, typename R::Wrapped> {};

template <typename L, typename R>
struct __floordiv__ { static constexpr bool enable = false; };
template <impl::proxy_like L, typename R> requires (!impl::proxy_like<R>)
struct __floordiv__<L, R> : __floordiv__<typename L::Wrapped, R> {};
template <typename L, impl::proxy_like R> requires (!impl::proxy_like<L>)
struct __floordiv__<L, R> : __floordiv__<L, typename R::Wrapped> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __floordiv__<L, R> : __floordiv__<typename L::Wrapped, typename R::Wrapped> {};

template <typename L, typename R>
struct __itruediv__ { static constexpr bool enable = false; };
template <impl::proxy_like L, typename R> requires (!impl::proxy_like<R>)
struct __itruediv__<L, R> : __itruediv__<typename L::Wrapped, R> {};
template <typename L, impl::proxy_like R> requires (!impl::proxy_like<L>)
struct __itruediv__<L, R> : __itruediv__<L, typename R::Wrapped> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __itruediv__<L, R> : __itruediv__<typename L::Wrapped, typename R::Wrapped> {};


template <typename L, typename R> requires (__truediv__<L, R>::enable)
auto operator/(const L& lhs, const R& rhs) {
    using Return = typename __truediv__<L, R>::Return;
    static_assert(
        std::is_base_of_v<Object, Return>,
        "True division operator must return a py::Object subclass.  Check "
        "your specialization of __truediv__ for this type and ensure the "
        "Return type is derived from py::Object."
    );
    if constexpr (impl::proxy_like<L>) {
        return lhs.value() / rhs;
    } else if constexpr (impl::proxy_like<R>) {
        return lhs / rhs.value();
    } else {
        return impl::ops::operator_truediv<Return>(lhs, rhs);
    }
}


template <typename L, typename R> requires (__itruediv__<L, R>::enable)
L& operator/=(L& lhs, const R& rhs) {
    using Return = typename __itruediv__<L, R>::Return;
    static_assert(
        std::is_same_v<Return, L&>,
        "In-place true division operator must return a mutable reference to the "
        "left operand.  Check your specialization of __itruediv__ for these "
        "types and ensure the Return type is set to the left operand."
    );
    if constexpr (impl::proxy_like<L>) {
        lhs.value() /= rhs;
    } else if constexpr (impl::proxy_like<R>) {
        lhs /= rhs.value();
    } else {
        impl::ops::operator_itruediv<Return>(lhs, rhs);
    }
    return lhs;
}


template <typename L, typename R>
    requires (impl::object_operand<L, R> && !__truediv__<L, R>::enable)
auto operator/(const L& lhs, const R& rhs) = delete;


template <std::derived_from<Object> L, typename R> requires (!__itruediv__<L, R>::enable)
auto operator/=(const L& lhs, const R& rhs) = delete;


///////////////////////
////    MODULUS    ////
///////////////////////


namespace impl {
    template <> struct getattr_helper<"__mod__">            : Returns<Function> {};
    template <> struct setattr_helper<"__mod__">            : Returns<void> {};
    template <> struct delattr_helper<"__mod__">            : Returns<void> {};

    template <> struct getattr_helper<"__divmod__">         : Returns<Function> {};
    template <> struct setattr_helper<"__divmod__">         : Returns<void> {};
    template <> struct delattr_helper<"__divmod__">         : Returns<void> {};

    template <> struct getattr_helper<"__rmod__">           : Returns<Function> {};
    template <> struct setattr_helper<"__rmod__">           : Returns<void> {};
    template <> struct delattr_helper<"__rmod__">           : Returns<void> {};

    template <> struct getattr_helper<"__rdivmod__">        : Returns<Function> {};
    template <> struct setattr_helper<"__rdivmod__">        : Returns<void> {};
    template <> struct delattr_helper<"__rdivmod__">        : Returns<void> {};

    template <> struct getattr_helper<"__imod__">           : Returns<Function> {};
    template <> struct setattr_helper<"__imod__">           : Returns<void> {};
    template <> struct delattr_helper<"__imod__">           : Returns<void> {};

    template <> struct getattr_helper<"__idivmod__">        : Returns<Function> {};
    template <> struct setattr_helper<"__idivmod__">        : Returns<void> {};
    template <> struct delattr_helper<"__idivmod__">        : Returns<void> {};
}


template <typename L, typename R>
struct __mod__ { static constexpr bool enable = false; };
template <impl::proxy_like L, typename R> requires (!impl::proxy_like<R>)
struct __mod__<L, R> : __mod__<typename L::Wrapped, R> {};
template <typename L, impl::proxy_like R> requires (!impl::proxy_like<L>)
struct __mod__<L, R> : __mod__<L, typename R::Wrapped> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __mod__<L, R> : __mod__<typename L::Wrapped, typename R::Wrapped> {};

template <typename L, typename R>
struct __imod__ { static constexpr bool enable = false; };
template <impl::proxy_like L, typename R> requires (!impl::proxy_like<R>)
struct __imod__<L, R> : __imod__<typename L::Wrapped, R> {};
template <typename L, impl::proxy_like R> requires (!impl::proxy_like<L>)
struct __imod__<L, R> : __imod__<L, typename R::Wrapped> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __imod__<L, R> : __imod__<typename L::Wrapped, typename R::Wrapped> {};


template <typename L, typename R> requires (__mod__<L, R>::enable)
auto operator%(const L& lhs, const R& rhs) {
    using Return = typename __mod__<L, R>::Return;
    static_assert(
        std::is_base_of_v<Object, Return>,
        "Modulus operator must return a py::Object subclass.  Check your "
        "specialization of __mod__ for this type and ensure the Return type "
        "is derived from py::Object."
    );
    if constexpr (impl::proxy_like<L>) {
        return lhs.value() % rhs;
    } else if constexpr (impl::proxy_like<R>) {
        return lhs % rhs.value();
    } else {
        return impl::ops::operator_mod<Return>(lhs, rhs);
    }
}


template <typename L, typename R> requires (__imod__<L, R>::enable)
L& operator%=(L& lhs, const R& rhs) {
    using Return = typename __imod__<L, R>::Return;
    static_assert(
        std::is_same_v<Return, L&>,
        "In-place modulus operator must return a mutable reference to the left "
        "operand.  Check your specialization of __imod__ for these types and "
        "ensure the Return type is set to the left operand."
    );
    if constexpr (impl::proxy_like<L>) {
        lhs.value() %= rhs;
    } else if constexpr (impl::proxy_like<R>) {
        lhs %= rhs.value();
    } else {
        impl::ops::operator_imod<Return>(lhs, rhs);
    }
    return lhs;
}


template <typename L, typename R>
    requires (impl::object_operand<L, R> && !__mod__<L, R>::enable)
auto operator%(const L& lhs, const R& rhs) = delete;


template <std::derived_from<Object> L, typename R> requires (!__imod__<L, R>::enable)
auto operator%=(const L& lhs, const R& rhs) = delete;


/////////////////////
////    ROUND    ////
/////////////////////


namespace impl {
    template <> struct getattr_helper<"__round__">          : Returns<Function> {};
    template <> struct setattr_helper<"__round__">          : Returns<void> {};
    template <> struct delattr_helper<"__round__">          : Returns<void> {};

    template <> struct getattr_helper<"__trunc__">          : Returns<Function> {};
    template <> struct setattr_helper<"__trunc__">          : Returns<void> {};
    template <> struct delattr_helper<"__trunc__">          : Returns<void> {};

    template <> struct getattr_helper<"__floor__">          : Returns<Function> {};
    template <> struct setattr_helper<"__floor__">          : Returns<void> {};
    template <> struct delattr_helper<"__floor__">          : Returns<void> {};

    template <> struct getattr_helper<"__ceil__">           : Returns<Function> {};
    template <> struct setattr_helper<"__ceil__">           : Returns<void> {};
    template <> struct delattr_helper<"__ceil__">           : Returns<void> {};
}


/////////////////////
////    POWER    ////
/////////////////////


namespace impl {
    template <> struct getattr_helper<"__pow__">            : Returns<Function> {};
    template <> struct setattr_helper<"__pow__">            : Returns<void> {};
    template <> struct delattr_helper<"__pow__">            : Returns<void> {};

    template <> struct getattr_helper<"__rpow__">           : Returns<Function> {};
    template <> struct setattr_helper<"__rpow__">           : Returns<void> {};
    template <> struct delattr_helper<"__rpow__">           : Returns<void> {};

    template <> struct getattr_helper<"__ipow__">           : Returns<Function> {};
    template <> struct setattr_helper<"__ipow__">           : Returns<void> {};
    template <> struct delattr_helper<"__ipow__">           : Returns<void> {};
}


template <typename base, typename exponent>
struct __pow__ { static constexpr bool enable = false; };
template <impl::proxy_like base, typename exponent> requires (!impl::proxy_like<exponent>)
struct __pow__<base, exponent> : __pow__<typename base::Wrapped, exponent> {};
template <typename base, impl::proxy_like exponent> requires (!impl::proxy_like<base>)
struct __pow__<base, exponent> : __pow__<base, typename exponent::Wrapped> {};
template <impl::proxy_like base, impl::proxy_like exponent>
struct __pow__<base, exponent> : __pow__<typename base::Wrapped, typename exponent::Wrapped> {};


/* Equivalent to Python `base ** exp` (exponentiation). */
template <typename Base, typename Exp> requires (__pow__<Base, Exp>::enable)
auto pow(const Base& base, const Exp& exp) {
    using Return = typename __pow__<Base, Exp>::Return;
    static_assert(
        std::is_base_of_v<Object, Return>,
        "pow() must return a py::Object subclass.  Check your specialization "
        "of __pow__ for this type and ensure the Return type is derived from "
        "py::Object."
    );
    if constexpr (impl::proxy_like<Base>) {
        return pow(base.value(), exp);
    } else if constexpr (impl::proxy_like<Exp>) {
        return pow(base, exp.value());
    } else {
        return impl::ops::operator_pow<Return>(base, exp);
    }
}


/* Equivalent to Python `pow(base, exp)`, except that it takes a C++ value and applies
std::pow() for identical semantics. */
template <typename Base, typename Exp> requires (!impl::object_operand<Base, Exp>)
auto pow(const Base& base, const Exp& exponent) {
    if constexpr (impl::complex_like<Base> && impl::complex_like<Exp>) {
        return std::common_type_t<Base, Exp>(
            std::pow(base.real(), exponent.real()),
            std::pow(base.imag(), exponent.imag())
        );
    } else if constexpr (impl::complex_like<Base>) {
        return Base(
            std::pow(base.real(), exponent),
            std::pow(base.imag(), exponent)
        );
    } else if constexpr (impl::complex_like<Exp>) {
        return Exp(
            std::pow(base, exponent.real()),
            std::pow(base, exponent.imag())
        );
    } else {
        return std::pow(base, exponent);
    }
}


/* Equivalent to Python `pow(base, exp, mod)`. */
template <impl::int_like Base, impl::int_like Exp, impl::int_like Mod>
    requires (__pow__<Base, Exp>::enable)
auto pow(const Base& base, const Exp& exp, const Mod& mod) {
    using Return = typename __pow__<Base, Exp>::Return;
    static_assert(
        std::is_base_of_v<Object, Return>,
        "pow() must return a py::Object subclass.  Check your specialization "
        "of __pow__ for this type and ensure the Return type is derived from "
        "py::Object."
    );
    if constexpr (impl::proxy_like<Base>) {
        return pow(base.value(), exp, mod);
    } else if constexpr (impl::proxy_like<Exp>) {
        return pow(base, exp.value(), mod);
    } else {
        return impl::ops::operator_pow<Return>(base, exp, mod);
    }
}


// /* Equivalent to Python `pow(base, exp, mod)`, but works on C++ integers with identical
// semantics. */
// template <std::integral Base, std::integral Exp, std::integral Mod>
// auto pow(Base base, Exp exp, Mod mod) {
//     std::common_type_t<Base, Exp, Mod> result = 1;
//     base = py::mod(base, mod);
//     while (exp > 0) {
//         if (exp % 2) {
//             result = py::mod(result * base, mod);
//         }
//         exp >>= 1;
//         base = py::mod(base * base, mod);
//     }
//     return result;
// }


//////////////////////
////    LSHIFT    ////
//////////////////////


namespace impl {
    template <> struct getattr_helper<"__lshift__">         : Returns<Function> {};
    template <> struct setattr_helper<"__lshift__">         : Returns<void> {};
    template <> struct delattr_helper<"__lshift__">         : Returns<void> {};

    template <> struct getattr_helper<"__rlshift__">        : Returns<Function> {};
    template <> struct setattr_helper<"__rlshift__">        : Returns<void> {};
    template <> struct delattr_helper<"__rlshift__">        : Returns<void> {};

    template <> struct getattr_helper<"__ilshift__">        : Returns<Function> {};
    template <> struct setattr_helper<"__ilshift__">        : Returns<void> {};
    template <> struct delattr_helper<"__ilshift__">        : Returns<void> {};
}


template <typename L, typename R>
struct __lshift__ { static constexpr bool enable = false; };
template <impl::proxy_like L, typename R> requires (!impl::proxy_like<R>)
struct __lshift__<L, R> : __lshift__<typename L::Wrapped, R> {};
template <typename L, impl::proxy_like R> requires (!impl::proxy_like<L>)
struct __lshift__<L, R> : __lshift__<L, typename R::Wrapped> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __lshift__<L, R> : __lshift__<typename L::Wrapped, typename R::Wrapped> {};

template <typename L, typename R>
struct __ilshift__ { static constexpr bool enable = false; };
template <impl::proxy_like L, typename R> requires (!impl::proxy_like<R>)
struct __ilshift__<L, R> : __ilshift__<typename L::Wrapped, R> {};
template <typename L, impl::proxy_like R> requires (!impl::proxy_like<L>)
struct __ilshift__<L, R> : __ilshift__<L, typename R::Wrapped> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __ilshift__<L, R> : __ilshift__<typename L::Wrapped, typename R::Wrapped> {};


template <typename L, typename R>
    requires (__lshift__<L, R>::enable && !std::derived_from<L, std::ostream>)
auto operator<<(const L& lhs, const R& rhs) {
    using Return = typename __lshift__<L, R>::Return;
    static_assert(
        std::is_base_of_v<Object, Return>,
        "Left shift operator must return a py::Object subclass.  Check your "
        "specialization of __lshift__ for this type and ensure the Return "
        "type is derived from py::Object."
    );
    if constexpr (impl::proxy_like<L>) {
        return lhs.value() << rhs;
    } else if constexpr (impl::proxy_like<R>) {
        return lhs << rhs.value();
    } else {
        return impl::ops::operator_lshift<Return>(lhs, rhs);
    }
}


template <std::derived_from<std::ostream> L, std::derived_from<Object> R>
L& operator<<(L& os, const R& obj) {
    PyObject* repr = PyObject_Repr(obj.ptr());
    if (repr == nullptr) {
        Exception::from_python();
    }
    Py_ssize_t size;
    const char* data = PyUnicode_AsUTF8AndSize(repr, &size);
    if (data == nullptr) {
        Py_DECREF(repr);
        Exception::from_python();
    }
    os.write(data, size);
    Py_DECREF(repr);
    return os;
}


template <std::derived_from<std::ostream> L, impl::proxy_like T>
L& operator<<(L& os, const T& proxy) {
    os << proxy.value();
    return os;
}


template <typename L, typename R> requires (__ilshift__<L, R>::enable)
L& operator<<=(L& lhs, const R& rhs) {
    using Return = typename __ilshift__<L, R>::Return;
    static_assert(
        std::is_same_v<Return, L&>,
        "In-place left shift operator must return a mutable reference to the left "
        "operand.  Check your specialization of __ilshift__ for these types "
        "and ensure the Return type is set to the left operand."
    );
    if constexpr (impl::proxy_like<L>) {
        lhs.value() <<= rhs;
    } else if constexpr (impl::proxy_like<R>) {
        lhs <<= rhs.value();
    } else {
        impl::ops::operator_ilshift<Return>(lhs, rhs);
    }
    return lhs;
}


template <typename L, typename R>
    requires (impl::object_operand<L, R> && !__lshift__<L, R>::enable)
auto operator<<(const L& lhs, const R& rhs) = delete;


template <std::derived_from<Object> L, typename R> requires (!__ilshift__<L, R>::enable)
auto operator<<=(const L& lhs, const R& rhs) = delete;


//////////////////////
////    RSHIFT    ////
//////////////////////


namespace impl {
    template <> struct getattr_helper<"__rshift__">         : Returns<Function> {};
    template <> struct setattr_helper<"__rshift__">         : Returns<void> {};
    template <> struct delattr_helper<"__rshift__">         : Returns<void> {};

    template <> struct getattr_helper<"__rrshift__">        : Returns<Function> {};
    template <> struct setattr_helper<"__rrshift__">        : Returns<void> {};
    template <> struct delattr_helper<"__rrshift__">        : Returns<void> {};

    template <> struct getattr_helper<"__irshift__">        : Returns<Function> {};
    template <> struct setattr_helper<"__irshift__">        : Returns<void> {};
    template <> struct delattr_helper<"__irshift__">        : Returns<void> {};
}


template <typename L, typename R>
struct __rshift__ { static constexpr bool enable = false; };
template <impl::proxy_like L, typename R> requires (!impl::proxy_like<R>)
struct __rshift__<L, R> : __rshift__<typename L::Wrapped, R> {};
template <typename L, impl::proxy_like R> requires (!impl::proxy_like<L>)
struct __rshift__<L, R> : __rshift__<L, typename R::Wrapped> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __rshift__<L, R> : __rshift__<typename L::Wrapped, typename R::Wrapped> {};

template <typename L, typename R>
struct __irshift__ { static constexpr bool enable = false; };
template <impl::proxy_like L, typename R> requires (!impl::proxy_like<R>)
struct __irshift__<L, R> : __irshift__<typename L::Wrapped, R> {};
template <typename L, impl::proxy_like R> requires (!impl::proxy_like<L>)
struct __irshift__<L, R> : __irshift__<L, typename R::Wrapped> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __irshift__<L, R> : __irshift__<typename L::Wrapped, typename R::Wrapped> {};


template <typename L, typename R> requires (__rshift__<L, R>::enable)
auto operator>>(const L& lhs, const R& rhs) {
    using Return = typename __rshift__<L, R>::Return;
    static_assert(
        std::is_base_of_v<Object, Return>,
        "Right shift operator must return a py::Object subclass.  Check your "
        "specialization of __rshift__ for this type and ensure the Return "
        "type is derived from py::Object."
    );
    if constexpr (impl::proxy_like<L>) {
        return lhs.value() >> rhs;
    } else if constexpr (impl::proxy_like<R>) {
        return lhs >> rhs.value();
    } else {
        return impl::ops::operator_rshift<Return>(lhs, rhs);
    }
}


template <typename L, typename R> requires (__irshift__<L, R>::enable)
L& operator>>=(L& lhs, const R& rhs) {
    using Return = typename __irshift__<L, R>::Return;
    static_assert(
        std::is_same_v<Return, L&>,
        "In-place right shift operator must return a mutable reference to the left "
        "operand.  Check your specialization of __irshift__ for these types "
        "and ensure the Return type is set to the left operand."
    );
    if constexpr (impl::proxy_like<L>) {
        lhs.value() >>= rhs;
    } else if constexpr (impl::proxy_like<R>) {
        lhs >>= rhs.value();
    } else {
        impl::ops::operator_irshift<Return>(lhs, rhs);
    }
    return lhs;
}


template <typename L, typename R>
    requires (impl::object_operand<L, R> && !__rshift__<L, R>::enable)
auto operator>>(const L& lhs, const R& rhs) = delete;


template <std::derived_from<Object> L, typename R> requires (!__irshift__<L, R>::enable)
auto operator>>=(const L& lhs, const R& rhs) = delete;


///////////////////
////    AND    ////
///////////////////


namespace impl {
    template <> struct getattr_helper<"__and__">            : Returns<Function> {};
    template <> struct setattr_helper<"__and__">            : Returns<void> {};
    template <> struct delattr_helper<"__and__">            : Returns<void> {};

    template <> struct getattr_helper<"__rand__">           : Returns<Function> {};
    template <> struct setattr_helper<"__rand__">           : Returns<void> {};
    template <> struct delattr_helper<"__rand__">           : Returns<void> {};

    template <> struct getattr_helper<"__iand__">           : Returns<Function> {};
    template <> struct setattr_helper<"__iand__">           : Returns<void> {};
    template <> struct delattr_helper<"__iand__">           : Returns<void> {};
}


template <typename L, typename R>
struct __and__ { static constexpr bool enable = false; };
template <impl::proxy_like L, typename R> requires (!impl::proxy_like<R>)
struct __and__<L, R> : __and__<typename L::Wrapped, R> {};
template <typename L, impl::proxy_like R> requires (!impl::proxy_like<L>)
struct __and__<L, R> : __and__<L, typename R::Wrapped> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __and__<L, R> : __and__<typename L::Wrapped, typename R::Wrapped> {};

template <typename L, typename R>
struct __iand__ { static constexpr bool enable = false; };
template <impl::proxy_like L, typename R> requires (!impl::proxy_like<R>)
struct __iand__<L, R> : __iand__<typename L::Wrapped, R> {};
template <typename L, impl::proxy_like R> requires (!impl::proxy_like<L>)
struct __iand__<L, R> : __iand__<L, typename R::Wrapped> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __iand__<L, R> : __iand__<typename L::Wrapped, typename R::Wrapped> {};


template <typename L, typename R> requires (__and__<L, R>::enable)
auto operator&(const L& lhs, const R& rhs) {
    using Return = typename __and__<L, R>::Return;
    static_assert(
        std::is_base_of_v<Object, Return>,
        "Bitwise AND operator must return a py::Object subclass.  Check your "
        "specialization of __and__ for this type and ensure the Return type "
        "is derived from py::Object."
    );
    if constexpr (impl::proxy_like<L>) {
        return lhs.value() & rhs;
    } else if constexpr (impl::proxy_like<R>) {
        return lhs & rhs.value();
    } else {
        return impl::ops::operator_and<Return>(lhs, rhs);
    }
}


template <typename L, typename R> requires (__iand__<L, R>::enable)
L& operator&=(L& lhs, const R& rhs) {
    using Return = typename __iand__<L, R>::Return;
    static_assert(
        std::is_same_v<Return, L&>,
        "In-place bitwise AND operator must return a mutable reference to the left "
        "operand.  Check your specialization of __iand__ for these types and "
        "ensure the Return type is set to the left operand."
    );
    if constexpr (impl::proxy_like<L>) {
        lhs.value() &= rhs;
    } else if constexpr (impl::proxy_like<R>) {
        lhs &= rhs.value();
    } else {
        impl::ops::operator_iand<Return>(lhs, rhs);
    }
    return lhs;
}


template <typename L, typename R>
    requires (impl::object_operand<L, R> && !__and__<L, R>::enable)
auto operator&(const L& lhs, const R& rhs) = delete;


template <std::derived_from<Object> L, typename R> requires (!__iand__<L, R>::enable)
auto operator&=(const L& lhs, const R& rhs) = delete;


//////////////////
////    OR    ////
//////////////////


namespace impl {
    template <> struct getattr_helper<"__or__">             : Returns<Function> {};
    template <> struct setattr_helper<"__or__">             : Returns<void> {};
    template <> struct delattr_helper<"__or__">             : Returns<void> {};

    template <> struct getattr_helper<"__ror__">            : Returns<Function> {};
    template <> struct setattr_helper<"__ror__">            : Returns<void> {};
    template <> struct delattr_helper<"__ror__">            : Returns<void> {};

    template <> struct getattr_helper<"__ior__">            : Returns<Function> {};
    template <> struct setattr_helper<"__ior__">            : Returns<void> {};
    template <> struct delattr_helper<"__ior__">            : Returns<void> {};
}


template <typename L, typename R>
struct __or__ { static constexpr bool enable = false; };
template <impl::proxy_like L, typename R> requires (!impl::proxy_like<R>)
struct __or__<L, R> : __or__<typename L::Wrapped, R> {};
template <typename L, impl::proxy_like R> requires (!impl::proxy_like<L>)
struct __or__<L, R> : __or__<L, typename R::Wrapped> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __or__<L, R> : __or__<typename L::Wrapped, typename R::Wrapped> {};

template <typename L, typename R>
struct __ior__ { static constexpr bool enable = false; };
template <impl::proxy_like L, typename R> requires (!impl::proxy_like<R>)
struct __ior__<L, R> : __ior__<typename L::Wrapped, R> {};
template <typename L, impl::proxy_like R> requires (!impl::proxy_like<L>)
struct __ior__<L, R> : __ior__<L, typename R::Wrapped> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __ior__<L, R> : __ior__<typename L::Wrapped, typename R::Wrapped> {};


template <typename L, typename R>
    requires (__or__<L, R>::enable && !std::ranges::view<R>)
auto operator|(const L& lhs, const R& rhs) {
    using Return = typename __or__<L, R>::Return;
    static_assert(
        std::is_base_of_v<Object, Return>,
        "Bitwise OR operator must return a py::Object subclass.  Check your "
        "specialization of __or__ for this type and ensure the Return type is "
        "derived from py::Object."
    );
    if constexpr (impl::proxy_like<L>) {
        return lhs.value() | rhs;
    } else if constexpr (impl::proxy_like<R>) {
        return lhs | rhs.value();
    } else {
        return impl::ops::operator_or<Return>(lhs, rhs);
    }
}


template <std::derived_from<Object> L, std::ranges::view R>
auto operator|(const L& container, const R& view) {
    return std::views::all(container) | view;
}


// TODO: should this be enabled?  Does it cause a lifetime issue?
// template <impl::proxy_like L, std::ranges::view R>
// auto operator|(const L& container, const R& view) {
//     return container.value() | view;
// }


template <typename L, typename R> requires (__ior__<L, R>::enable)
L& operator|=(L& lhs, const R& rhs) {
    using Return = typename __ior__<L, R>::Return;
    static_assert(
        std::is_same_v<Return, L&>,
        "In-place bitwise OR operator must return a mutable reference to the left "
        "operand.  Check your specialization of __ior__ for these types and "
        "ensure the Return type is set to the left operand."
    );
    if constexpr (impl::proxy_like<L>) {
        lhs.value() |= rhs;
    } else if constexpr (impl::proxy_like<R>) {
        lhs |= rhs.value();
    } else {
        impl::ops::operator_ior<Return>(lhs, rhs);
    }
    return lhs;
}


template <typename L, typename R>
    requires (impl::object_operand<L, R> && !__or__<L, R>::enable)
auto operator|(const L& lhs, const R& rhs) = delete;


template <std::derived_from<Object> L, typename R> requires (!__ior__<L, R>::enable)
auto operator|=(const L& lhs, const R& rhs) = delete;


///////////////////
////    XOR    ////
///////////////////


namespace impl {
    template <> struct getattr_helper<"__xor__">            : Returns<Function> {};
    template <> struct setattr_helper<"__xor__">            : Returns<void> {};
    template <> struct delattr_helper<"__xor__">            : Returns<void> {};

    template <> struct getattr_helper<"__rxor__">           : Returns<Function> {};
    template <> struct setattr_helper<"__rxor__">           : Returns<void> {};
    template <> struct delattr_helper<"__rxor__">           : Returns<void> {};

    template <> struct getattr_helper<"__ixor__">           : Returns<Function> {};
    template <> struct setattr_helper<"__ixor__">           : Returns<void> {};
    template <> struct delattr_helper<"__ixor__">           : Returns<void> {};
}


template <typename L, typename R>
struct __xor__ { static constexpr bool enable = false; };
template <impl::proxy_like L, typename R> requires (!impl::proxy_like<R>)
struct __xor__<L, R> : __xor__<typename L::Wrapped, R> {};
template <typename L, impl::proxy_like R> requires (!impl::proxy_like<L>)
struct __xor__<L, R> : __xor__<L, typename R::Wrapped> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __xor__<L, R> : __xor__<typename L::Wrapped, typename R::Wrapped> {};

template <typename L, typename R>
struct __ixor__ { static constexpr bool enable = false; };
template <impl::proxy_like L, typename R> requires (!impl::proxy_like<R>)
struct __ixor__<L, R> : __ixor__<typename L::Wrapped, R> {};
template <typename L, impl::proxy_like R> requires (!impl::proxy_like<L>)
struct __ixor__<L, R> : __ixor__<L, typename R::Wrapped> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __ixor__<L, R> : __ixor__<typename L::Wrapped, typename R::Wrapped> {};


template <typename L, typename R> requires (__xor__<L, R>::enable)
auto operator^(const L& lhs, const R& rhs) {
    using Return = typename __xor__<L, R>::Return;
    static_assert(
        std::is_base_of_v<Object, Return>,
        "Bitwise XOR operator must return a py::Object subclass.  Check your "
        "specialization of __xor__ for this type and ensure the Return type "
        "is derived from py::Object."
    );
    if constexpr (impl::proxy_like<L>) {
        return lhs.value() ^ rhs;
    } else if constexpr (impl::proxy_like<R>) {
        return lhs ^ rhs.value();
    } else {
        return impl::ops::operator_xor<Return>(lhs, rhs);
    }
}


template <typename L, typename R> requires (__ixor__<L, R>::enable)
L& operator^=(L& lhs, const R& rhs) {
    using Return = typename __ixor__<L, R>::Return;
    static_assert(
        std::is_same_v<Return, L&>,
        "In-place bitwise XOR operator must return a mutable reference to the left "
        "operand.  Check your specialization of __ixor__ for these types and "
        "ensure the Return type is set to the left operand."
    );
    if constexpr (impl::proxy_like<L>) {
        lhs.value() ^= rhs;
    } else if constexpr (impl::proxy_like<R>) {
        lhs ^= rhs.value();
    } else {
        impl::ops::operator_ixor<Return>(lhs, rhs);
    }
    return lhs;
}


template <typename L, typename R>
    requires (impl::object_operand<L, R> && !__xor__<L, R>::enable)
auto operator^(const L& lhs, const R& rhs) = delete;


template <std::derived_from<Object> L, typename R> requires (!__ixor__<L, R>::enable)
auto operator^=(const L& lhs, const R& rhs) = delete;


}  // namespace py
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_COMMON_OPERATORS_H
