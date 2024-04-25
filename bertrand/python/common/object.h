#if !defined(BERTRAND_PYTHON_COMMON_INCLUDED) && !defined(LINTER)
#error "This file should not be included directly.  Please include <bertrand/common.h> instead."
#endif

#ifndef BERTRAND_PYTHON_COMMON_OBJECT_H
#define BERTRAND_PYTHON_COMMON_OBJECT_H

#include "declarations.h"
#include "concepts.h"
#include "exceptions.h"
#include "operators.h"


namespace bertrand {
namespace py {


namespace impl {

    template <typename Obj, typename Key> requires (__getitem__<Obj, Key>::enable)
    class Item;
    template <typename Obj, StaticStr name> requires (__getattr__<Obj, name>::enable)
    class Attr;
    template <typename Policy>
    class Iterator;
    template <typename Policy>
    class ReverseIterator;
    template <typename Deref>
    class GenericIter;

    // TODO: this could maybe necessitate a CRTP intermediate class to correctly
    // resolve conversion operators.  It could also account for member operator
    // overloads at the same time.

    // class Tuple : public impl::Inherits<Tuple, Object> {}


    #define BERTRAND_OBJECT_COMMON(Base, cls, comptime_check, runtime_check)            \
        template <typename T>                                                           \
        static consteval bool check() { return comptime_check<T>; }                     \
                                                                                        \
        template <typename T>                                                           \
        static constexpr bool check(const T& obj) {                                     \
            if constexpr (impl::python_like<T>) {                                       \
                return obj.ptr() != nullptr && runtime_check(obj.ptr());                \
            } else {                                                                    \
                return check<T>();                                                      \
            }                                                                           \
        }                                                                               \
                                                                                        \
        cls(Handle h, const borrowed_t& t) : Base(h, t) {}                              \
        cls(Handle h, const stolen_t& t) : Base(h, t) {}                                \
                                                                                        \
        template <typename Policy>                                                      \
        cls(const detail::accessor<Policy>& accessor) : Base(nullptr, stolen_t{}) {     \
            pybind11::object obj(accessor);                                             \
            if (check(obj)) {                                                           \
                m_ptr = obj.release().ptr();                                            \
            } else {                                                                    \
                throw impl::noconvert<cls>(obj.ptr());                                  \
            }                                                                           \
        }                                                                               \

}


// NOTE: Object implicitly allows all operators, but will defer to a subclass if
// combined with one in a binary operation.  This reduces the need to treat Object
// as a special case in the operator overloads.
template <typename ... Args>
struct __call__<Object, Args...>                            : Returns<Object> {};
template <>
struct __len__<Object>                                      : Returns<size_t> {};
template <typename T>
struct __contains__<Object, T>                              : Returns<bool> {};
template <>
struct __iter__<Object>                                     : Returns<Object> {};
template <>
struct __reversed__<Object>                                 : Returns<Object> {};
template <typename Key>
struct __getitem__<Object, Key>                             : Returns<Object> {};
template <typename Key, typename Value>
struct __setitem__<Object, Key, Value>                      : Returns<void> {};
template <typename Key>
struct __delitem__<Object, Key>                             : Returns<void> {};
template <StaticStr name> requires (!impl::getattr_helper<name>::enable)
struct __getattr__<Object, name>                            : Returns<Object> {};
template <StaticStr name, typename Value> requires (!impl::setattr_helper<name>::enable)
struct __setattr__<Object, name, Value>                     : Returns<void> {};
template <StaticStr name> requires (!impl::delattr_helper<name>::enable)
struct __delattr__<Object, name>                            : Returns<void> {};
template <>
struct __pos__<Object>                                      : Returns<Object> {};
template <>
struct __neg__<Object>                                      : Returns<Object> {};
template <>
struct __abs__<Object>                                      : Returns<Object> {};
template <>
struct __invert__<Object>                                   : Returns<Object> {};
template <>
struct __increment__<Object>                                : Returns<Object> {};
template <>
struct __decrement__<Object>                                : Returns<Object> {};
template <>
struct __hash__<Object>                                     : Returns<size_t> {};
template <std::same_as<Object> L, std::convertible_to<Object> R>
struct __lt__<L, R>                                         : Returns<bool> {};
template <std::convertible_to<Object> L, std::same_as<Object> R> requires (!std::same_as<L, Object>)
struct __lt__<L, R>                                         : Returns<bool> {};
template <std::same_as<Object> L, std::convertible_to<Object> R>
struct __le__<L, R>                                         : Returns<bool> {};
template <std::convertible_to<Object> L, std::same_as<Object> R> requires (!std::same_as<L, Object>)
struct __le__<L, R>                                         : Returns<bool> {};
template <std::same_as<Object> L, std::convertible_to<Object> R>
struct __eq__<L, R>                                         : Returns<bool> {};
template <std::convertible_to<Object> L, std::same_as<Object> R> requires (!std::same_as<L, Object>)
struct __eq__<L, R>                                         : Returns<bool> {};
template <std::same_as<Object> L, std::convertible_to<Object> R>
struct __ne__<L, R>                                         : Returns<bool> {};
template <std::convertible_to<Object> L, std::same_as<Object> R> requires (!std::same_as<L, Object>)
struct __ne__<L, R>                                         : Returns<bool> {};
template <std::same_as<Object> L, std::convertible_to<Object> R>
struct __ge__<L, R>                                         : Returns<bool> {};
template <std::convertible_to<Object> L, std::same_as<Object> R> requires (!std::same_as<L, Object>)
struct __ge__<L, R>                                         : Returns<bool> {};
template <std::same_as<Object> L, std::convertible_to<Object> R>
struct __gt__<L, R>                                         : Returns<bool> {};
template <std::convertible_to<Object> L, std::same_as<Object> R> requires (!std::same_as<L, Object>)
struct __gt__<L, R>                                         : Returns<bool> {};
template <std::same_as<Object> L, std::convertible_to<Object> R>
struct __add__<L, R>                                        : Returns<Object> {};
template <std::convertible_to<Object> L, std::same_as<Object> R> requires (!std::same_as<L, Object>)
struct __add__<L, R>                                        : Returns<Object> {};
template <std::convertible_to<Object> R>
struct __iadd__<Object, R>                                  : Returns<Object&> {};
template <std::same_as<Object> L, std::convertible_to<Object> R>
struct __sub__<L, R>                                        : Returns<Object> {};
template <std::convertible_to<Object> L, std::same_as<Object> R> requires (!std::same_as<L, Object>)
struct __sub__<L, R>                                        : Returns<Object> {};
template <std::convertible_to<Object> R>
struct __isub__<Object, R>                                  : Returns<Object&> {};
template <std::same_as<Object> L, std::convertible_to<Object> R>
struct __mul__<L, R>                                        : Returns<Object> {};
template <std::convertible_to<Object> L, std::same_as<Object> R> requires (!std::same_as<L, Object>)
struct __mul__<L, R>                                        : Returns<Object> {};
template <std::convertible_to<Object> R>
struct __imul__<Object, R>                                  : Returns<Object&> {};
template <std::same_as<Object> L, std::convertible_to<Object> R>
struct __truediv__<L, R>                                    : Returns<Object> {};
template <std::convertible_to<Object> L, std::same_as<Object> R> requires (!std::same_as<L, Object>)
struct __truediv__<L, R>                                    : Returns<Object> {};
template <std::convertible_to<Object> R>
struct __itruediv__<Object, R>                              : Returns<Object&> {};
template <std::same_as<Object> L, std::convertible_to<Object> R>
struct __mod__<L, R>                                        : Returns<Object> {};
template <std::convertible_to<Object> L, std::same_as<Object> R> requires (!std::same_as<L, Object>)
struct __mod__<L, R>                                        : Returns<Object> {};
template <std::convertible_to<Object> R>
struct __imod__<Object, R>                                  : Returns<Object&> {};
template <std::same_as<Object> L, std::convertible_to<Object> R>
struct __lshift__<L, R>                                     : Returns<Object> {};
template <std::convertible_to<Object> L, std::same_as<Object> R> requires (!std::same_as<L, Object>)
struct __lshift__<L, R>                                     : Returns<Object> {};
template <std::convertible_to<Object> R>
struct __ilshift__<Object, R>                               : Returns<Object&> {};
template <std::same_as<Object> L, std::convertible_to<Object> R>
struct __rshift__<L, R>                                     : Returns<Object> {};
template <std::convertible_to<Object> L, std::same_as<Object> R> requires (!std::same_as<L, Object>)
struct __rshift__<L, R>                                     : Returns<Object> {};
template <std::convertible_to<Object> R>
struct __irshift__<Object, R>                               : Returns<Object&> {};
template <std::same_as<Object> L, std::convertible_to<Object> R>
struct __and__<L, R>                                        : Returns<Object> {};
template <std::convertible_to<Object> L, std::same_as<Object> R> requires (!std::same_as<L, Object>)
struct __and__<L, R>                                        : Returns<Object> {};
template <std::convertible_to<Object> R>
struct __iand__<Object, R>                                  : Returns<Object&> {};
template <std::same_as<Object> L, std::convertible_to<Object> R>
struct __or__<L, R>                                         : Returns<Object> {};
template <std::convertible_to<Object> L, std::same_as<Object> R> requires (!std::same_as<L, Object>)
struct __or__<L, R>                                         : Returns<Object> {};
template <std::convertible_to<Object> R>
struct __ior__<Object, R>                                   : Returns<Object&> {};
template <std::same_as<Object> L, std::convertible_to<Object> R>
struct __xor__<L, R>                                        : Returns<Object> {};
template <std::convertible_to<Object> L, std::same_as<Object> R> requires (!std::same_as<L, Object>)
struct __xor__<L, R>                                        : Returns<Object> {};
template <std::convertible_to<Object> R>
struct __ixor__<Object, R>                                  : Returns<Object&> {};


// TODO: maybe check() should not check for nullptr?  This might make them more
// composable?


/* A revised Python object interface that allows implicit conversions to subtypes
(applying a type check on the way), explicit conversions to arbitrary C++ types,
type-safe operators, and generalized slice/attr syntax. */
class Object {
protected:
    PyObject* m_ptr;

    /* Protected tags mirror pybind11::object and allow for the use of
    reinterpret_borrow<>, reinterpret_steal<> for bertrand types. */
    struct borrowed_t {};
    struct stolen_t {};

    template <std::derived_from<Object> T>
    friend T reinterpret_borrow(Handle);
    template <std::derived_from<Object> T>
    friend T reinterpret_steal(Handle);

    template <typename Return, typename Self, typename... Args>
    static Return operator_call(const Self& obj, Args&&... args) {
        try {
            if constexpr (std::is_void_v<Return>) {
                Handle(obj.ptr())(std::forward<Args>(args)...);
            } else {
                return Return(
                    Handle(obj.ptr())(std::forward<Args>(args)...).release(),
                    stolen_t{}
                );
            }
        } catch (...) {
            Exception::from_pybind11();
        }
    }

    template <typename Return, typename Self, typename Key>
    static impl::Item<Self, std::decay_t<Key>> operator_getitem(
        const Self& obj,
        Key&& key
    );

    template <typename Return, typename Self>
    static impl::Item<Self, Slice> operator_getitem(
        const Self& obj,
        std::initializer_list<impl::SliceInitializer> slice
    );

    template <typename Return, typename Self>
    static impl::Iterator<impl::GenericIter<Return>> operator_begin(const Self& obj);
    template <typename Return, typename Self>
    static impl::Iterator<impl::GenericIter<Return>> operator_end(const Self& obj);
    template <typename Return, typename Self>
    static impl::Iterator<impl::GenericIter<Return>> operator_rbegin(const Self& obj);
    template <typename Return, typename Self>
    static impl::Iterator<impl::GenericIter<Return>> operator_rend(const Self& obj);

    template <typename Return, typename L, typename R>
    static bool operator_contains(const L& lhs, const R& rhs) {
        int result = PySequence_Contains(
            lhs.ptr(),
            detail::object_or_cast(rhs).ptr()
        );
        if (result == -1) {
            Exception::from_python();
        }
        return result;
    }

    template <typename Return, typename Self>
    static size_t operator_len(const Self& obj) {
        Py_ssize_t size = PyObject_Size(obj.ptr());
        if (size < 0) {
            Exception::from_python();
        }
        return size;
    }

    template <typename Self>
    static auto operator_dereference(const Self& obj) {
        try {
            return *Handle(obj.ptr());
        } catch (...) {
            Exception::from_pybind11();
        }
    }

    template <typename Return, typename L, typename R>
    static bool operator_lt(const L& lhs, const R& rhs) {
        int result = PyObject_RichCompareBool(
            detail::object_or_cast(lhs).ptr(),
            detail::object_or_cast(rhs).ptr(),
            Py_LT
        );
        if (result == -1) {
            Exception::from_python();
        }
        return result;
    }

    template <typename Return, typename L, typename R>
    static bool operator_le(const L& lhs, const R& rhs) {
        int result = PyObject_RichCompareBool(
            detail::object_or_cast(lhs).ptr(),
            detail::object_or_cast(rhs).ptr(),
            Py_LE
        );
        if (result == -1) {
            Exception::from_python();
        }
        return result;
    }

    template <typename Return, typename L, typename R>
    static bool operator_eq(const L& lhs, const R& rhs) {
        int result = PyObject_RichCompareBool(
            detail::object_or_cast(lhs).ptr(),
            detail::object_or_cast(rhs).ptr(),
            Py_EQ
        );
        if (result == -1) {
            Exception::from_python();
        }
        return result;
    }

    template <typename Return, typename L, typename R>
    static bool operator_ne(const L& lhs, const R& rhs) {
        int result = PyObject_RichCompareBool(
            detail::object_or_cast(lhs).ptr(),
            detail::object_or_cast(rhs).ptr(),
            Py_NE
        );
        if (result == -1) {
            Exception::from_python();
        }
        return result;
    }

    template <typename Return, typename L, typename R>
    static bool operator_ge(const L& lhs, const R& rhs) {
        int result = PyObject_RichCompareBool(
            detail::object_or_cast(lhs).ptr(),
            detail::object_or_cast(rhs).ptr(),
            Py_GE
        );
        if (result == -1) {
            Exception::from_python();
        }
        return result;
    }

    template <typename Return, typename L, typename R>
    static bool operator_gt(const L& lhs, const R& rhs) {
        int result = PyObject_RichCompareBool(
            detail::object_or_cast(lhs).ptr(),
            detail::object_or_cast(rhs).ptr(),
            Py_GT
        );
        if (result == -1) {
            Exception::from_python();
        }
        return result;
    }

    template <typename Return, typename Self>
    static auto operator_abs(const Self& obj) {
        PyObject* result = PyNumber_Absolute(obj.ptr());
        if (result == nullptr) {
            Exception::from_python();
        }
        return Return(result, stolen_t{});
    }

    template <typename Return, typename Self>
    static auto operator_invert(const Self& obj) {
        PyObject* result = PyNumber_Invert(detail::object_or_cast(obj).ptr());
        if (result == nullptr) {
            Exception::from_python();
        }
        return Return(result, stolen_t{});
    }

    template <typename Return, typename Self>
    static auto operator_pos(const Self& obj) {
        PyObject* result = PyNumber_Positive(detail::object_or_cast(obj).ptr());
        if (result == nullptr) {
            Exception::from_python();
        }
        return Return(result, stolen_t{});
    }

    template <typename Return, typename Self>
    static void operator_increment(Self& obj) {
        static const pybind11::int_ one = 1;
        PyObject* result = PyNumber_InPlaceAdd(
            detail::object_or_cast(obj).ptr(),
            one.ptr()
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        if (result == obj.ptr()) {
            Py_DECREF(result);
        } else {
            obj = Return(result, stolen_t{});
        }
    }

    template <typename Return, typename L, typename R>
    static auto operator_add(const L& lhs, const R& rhs) {
        PyObject* result = PyNumber_Add(
            detail::object_or_cast(lhs).ptr(),
            detail::object_or_cast(rhs).ptr()
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return Return(result, stolen_t{});
    }

    template <typename Return, typename L, typename R>
    static void operator_iadd(L& lhs, const R& rhs) {
        PyObject* result = PyNumber_InPlaceAdd(
            lhs.ptr(),
            detail::object_or_cast(rhs).ptr()
        );
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == lhs.ptr()) {
            Py_DECREF(result);
        } else {
            lhs = L(result, stolen_t{});
        }
    }

    template <typename Return, typename Self>
    static auto operator_neg(const Self& obj) {
        PyObject* result = PyNumber_Negative(detail::object_or_cast(obj).ptr());
        if (result == nullptr) {
            Exception::from_python();
        }
        return Return(result, stolen_t{});
    }

    template <typename Return, typename Self>
    static void operator_decrement(Self& obj) {
        static const pybind11::int_ one = 1;
        PyObject* result = PyNumber_InPlaceSubtract(
            detail::object_or_cast(obj).ptr(),
            one.ptr()
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        if (result == obj.ptr()) {
            Py_DECREF(result);
        } else {
            obj = Return(result, stolen_t{});
        }
    }

    template <typename Return, typename L, typename R>
    static auto operator_sub(const L& lhs, const R& rhs) {
        PyObject* result = PyNumber_Subtract(
            detail::object_or_cast(lhs).ptr(),
            detail::object_or_cast(rhs).ptr()
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return Return(result, stolen_t{});
    }

    template <typename Return, typename L, typename R>
    static void operator_isub(L& lhs, const R& rhs) {
        PyObject* result = PyNumber_InPlaceAdd(
            lhs.ptr(),
            detail::object_or_cast(rhs).ptr()
        );
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == lhs.ptr()) {
            Py_DECREF(result);
        } else {
            lhs = L(result, stolen_t{});
        }
    }

    template <typename Return, typename L, typename R>
    static auto operator_mul(const L& lhs, const R& rhs) {
        PyObject* result = PyNumber_Multiply(
            detail::object_or_cast(lhs).ptr(),
            detail::object_or_cast(rhs).ptr()
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return Return(result, stolen_t{});
    }

    template <typename Return, typename L, typename R>
    static void operator_imul(L& lhs, const R& rhs) {
        PyObject* result = PyNumber_InPlaceMultiply(
            lhs.ptr(),
            detail::object_or_cast(rhs).ptr()
        );
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == lhs.ptr()) {
            Py_DECREF(result);
        } else {
            lhs = L(result, stolen_t{});
        }
    }

    template <typename Return, typename L, typename R>
    static auto operator_truediv(const L& lhs, const R& rhs) {
        PyObject* result = PyNumber_TrueDivide(
            detail::object_or_cast(lhs).ptr(),
            detail::object_or_cast(rhs).ptr()
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return Return(result, stolen_t{});
    }

    template <typename Return, typename L, typename R>
    static void operator_itruediv(L& lhs, const R& rhs) {
        PyObject* result = PyNumber_InPlaceTrueDivide(
            lhs.ptr(),
            detail::object_or_cast(rhs).ptr()
        );
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == lhs.ptr()) {
            Py_DECREF(result);
        } else {
            lhs = L(result, stolen_t{});
        }
    }

    template <typename Return, typename L, typename R>
    static auto operator_floordiv(const L& lhs, const R& rhs) {
        PyObject* result = PyNumber_FloorDivide(
            detail::object_or_cast(lhs).ptr(),
            detail::object_or_cast(rhs).ptr()
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return Return(result, stolen_t{});
    }

    template <typename Return, typename L, typename R>
    static void operator_ifloordiv(L& lhs, const R& rhs) {
        PyObject* result = PyNumber_InPlaceFloorDivide(
            lhs.ptr(),
            detail::object_or_cast(rhs).ptr()
        );
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == lhs.ptr()) {
            Py_DECREF(result);
        } else {
            lhs = L(result, stolen_t{});
        }
    }

    template <typename Return, typename L, typename R>
    static auto operator_mod(const L& lhs, const R& rhs) {
        PyObject* result = PyNumber_Remainder(
            detail::object_or_cast(lhs).ptr(),
            detail::object_or_cast(rhs).ptr()
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return Return(result, stolen_t{});
    }

    template <typename Return, typename L, typename R>
    static void operator_imod(L& lhs, const R& rhs) {
        PyObject* result = PyNumber_InPlaceRemainder(
            lhs.ptr(),
            detail::object_or_cast(rhs).ptr()
        );
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == lhs.ptr()) {
            Py_DECREF(result);
        } else {
            lhs = L(result, stolen_t{});
        }
    }

    // TODO: overload operator_pow() on Int to check for negative integer exponents
    // and convert to Float instead.  This just calls the parent implementation and
    // replaces the Return type with py::Float.

    template <typename Return, typename Base, typename Exp>
    static auto operator_pow(const Base& base, const Exp& exp) {
        PyObject* result = PyNumber_Power(
            detail::object_or_cast(base).ptr(),
            detail::object_or_cast(exp).ptr(),
            Py_None
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return Return(result, stolen_t{});
    }

    template <typename Return, typename Base, typename Exp, typename Mod>
    static auto operator_pow(const Base& base, const Exp& exp, const Mod& mod) {
        PyObject* result = PyNumber_Power(
            detail::object_or_cast(base).ptr(),
            detail::object_or_cast(exp).ptr(),
            detail::object_or_cast(mod).ptr()
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return Return(result, stolen_t{});
    }

    template <typename Return, typename Base, typename Exp>
    static void operator_ipow(Base& base, const Exp& exp) {
        PyObject* result = PyNumber_InPlacePower(
            base.ptr(),
            detail::object_or_cast(exp).ptr(),
            Py_None
        );
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == base.ptr()) {
            Py_DECREF(result);
        } else {
            base = Base(result, stolen_t{});
        }
    }

    template <typename Return, typename Base, typename Exp, typename Mod>
    static void operator_ipow(Base& base, const Exp& exp, const Mod& mod) {
        PyObject* result = PyNumber_InPlacePower(
            base.ptr(),
            detail::object_or_cast(exp).ptr(),
            detail::object_or_cast(mod).ptr()
        );
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == base.ptr()) {
            Py_DECREF(result);
        } else {
            base = Base(result, stolen_t{});
        }
    }

    template <typename Return, typename L, typename R>
    static auto operator_lshift(const L& lhs, const R& rhs) {
        PyObject* result = PyNumber_Lshift(
            detail::object_or_cast(lhs).ptr(),
            detail::object_or_cast(rhs).ptr()
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return Return(result, stolen_t{});
    }

    template <typename Return, typename L, typename R>
    static void operator_ilshift(L& lhs, const R& rhs) {
        PyObject* result = PyNumber_InPlaceLshift(
            lhs.ptr(),
            detail::object_or_cast(rhs).ptr()
        );
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == lhs.ptr()) {
            Py_DECREF(result);
        } else {
            lhs = L(result, stolen_t{});
        }
    }

    template <typename Return, typename L, typename R>
    static auto operator_rshift(const L& lhs, const R& rhs) {
        PyObject* result = PyNumber_Rshift(
            detail::object_or_cast(lhs).ptr(),
            detail::object_or_cast(rhs).ptr()
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return Return(result, stolen_t{});
    }

    template <typename Return, typename L, typename R>
    static void operator_irshift(L& lhs, const R& rhs) {
        PyObject* result = PyNumber_InPlaceRshift(
            lhs.ptr(),
            detail::object_or_cast(rhs).ptr()
        );
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == lhs.ptr()) {
            Py_DECREF(result);
        } else {
            lhs = L(result, stolen_t{});
        }
    }

    template <typename Return, typename L, typename R>
    static auto operator_and(const L& lhs, const R& rhs) {
        PyObject* result = PyNumber_And(
            detail::object_or_cast(lhs).ptr(),
            detail::object_or_cast(rhs).ptr()
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return Return(result, stolen_t{});
    }

    template <typename Return, typename L, typename R>
    static void operator_iand(L& lhs, const R& rhs) {
        PyObject* result = PyNumber_InPlaceAnd(
            lhs.ptr(),
            detail::object_or_cast(rhs).ptr()
        );
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == lhs.ptr()) {
            Py_DECREF(result);
        } else {
            lhs = L(result, stolen_t{});
        }
    }

    template <typename Return, typename L, typename R>
    static auto operator_or(const L& lhs, const R& rhs) {
        PyObject* result = PyNumber_Or(
            detail::object_or_cast(lhs).ptr(),
            detail::object_or_cast(rhs).ptr()
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return Return(result, stolen_t{});
    }

    template <typename Return, typename L, typename R>
    static void operator_ior(L& lhs, const R& rhs) {
        PyObject* result = PyNumber_InPlaceOr(
            lhs.ptr(),
            detail::object_or_cast(rhs).ptr()
        );
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == lhs.ptr()) {
            Py_DECREF(result);
        } else {
            lhs = L(result, stolen_t{});
        }
    }

    template <typename Return, typename L, typename R>
    static auto operator_xor(const L& lhs, const R& rhs) {
        PyObject* result = PyNumber_Xor(
            detail::object_or_cast(lhs).ptr(),
            detail::object_or_cast(rhs).ptr()
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return Return(result, stolen_t{});
    }

    template <typename Return, typename L, typename R>
    static void operator_ixor(L& lhs, const R& rhs) {
        PyObject* result = PyNumber_InPlaceXor(
            lhs.ptr(),
            detail::object_or_cast(rhs).ptr()
        );
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == lhs.ptr()) {
            Py_DECREF(result);
        } else {
            lhs = L(result, stolen_t{});
        }
    }

public:
    static const Type type;

    /* Check whether the templated type is considered object-like at compile time. */
    template <typename T>
    static constexpr bool check() {
        return std::derived_from<T, Object> || std::derived_from<T, pybind11::object>;
    }

    /* Check whether a Python/C++ value is considered object-like at compile time. */
    template <typename T>
    static constexpr bool check(const T& value) {
        if constexpr (impl::python_like<T>) {
            return value.ptr() != nullptr;
        } else {
            return check<T>();
        }
    }

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Default constructor.  Initializes to None. */
    Object() : m_ptr(Py_NewRef(Py_None)) {}

    /* reinterpret_borrow() constructor.  Borrows a reference to a raw Python handle. */
    Object(Handle ptr, const borrowed_t&) : m_ptr(Py_XNewRef(ptr.ptr())) {}

    /* reinterpret_steal() constructor.  Steals a reference to a raw Python handle. */
    Object(Handle ptr, const stolen_t&) : m_ptr(ptr.ptr()) {}

    /* Convert a pybind11 accessor into a generic Object. */
    template <typename Policy>
    Object(const detail::accessor<Policy>& accessor) : m_ptr(nullptr) {
        pybind11::object obj(accessor);
        if (check(obj)) {
            m_ptr = obj.release().ptr();
        } else {
            throw impl::noconvert<Object>(obj.ptr());
        }
    }

    /* Copy constructor.  Borrows a reference to an existing object. */
    Object(const Object& other) : m_ptr(Py_XNewRef(other.m_ptr)) {}

    /* Move constructor.  Steals a reference to a temporary object. */
    Object(Object&& other) : m_ptr(other.m_ptr) { other.m_ptr = nullptr; }

    /* Copy constructor from equivalent pybind11 type. */
    Object(const pybind11::object& other) : m_ptr(Py_XNewRef(other.ptr())) {}

    /* Move constructor from equivalent pybind11 type. */
    Object(pybind11::object&& other) : m_ptr(other.release().ptr()) {}

    /* Convert any C++ value into a generic python object. */
    template <impl::cpp_like T>
    Object(const T& value) : m_ptr([&value] {
        try {
            return pybind11::cast(value).release().ptr();
        } catch (...) {
            Exception::from_pybind11();
        }
    }()) {}

    /* Copy assignment operator. */
    Object& operator=(const Object& other) {
        std::cout << "object copy assignment\n";
        if (this != &other) {
            PyObject* temp = m_ptr;
            m_ptr = Py_XNewRef(other.m_ptr);
            Py_XDECREF(temp);
        }
        return *this;
    }

    /* Move assignment operator. */
    Object& operator=(Object&& other) {
        std::cout << "object move assignment\n";
        if (this != &other) {
            PyObject* temp = m_ptr;
            m_ptr = other.m_ptr;
            other.m_ptr = nullptr;
            Py_XDECREF(temp);
        }
        return *this;
    }

    /* Destructor allows any object to be stored with static duration. */
    ~Object() noexcept {
        if (Py_IsInitialized()) {
            Py_XDECREF(m_ptr);
        }
    }

    ////////////////////////////
    ////    BASE METHODS    ////
    ////////////////////////////

    /* Return the underlying PyObject* pointer. */
    inline PyObject* ptr() const {
        return m_ptr;
    }

    /* Relinquish ownership over the object and return it as a raw handle. */
    inline Handle release() {
        PyObject* temp = m_ptr;
        m_ptr = nullptr;
        return Handle(temp);
    }

    /* Check for exact pointer identity. */
    inline bool is(const Handle& other) const {
        return m_ptr == other.ptr();
    }

    /* Check for exact pointer identity. */
    inline bool is(const Object& other) const {
        return m_ptr == other.ptr();
    }

    ///////////////////////////
    ////    CONVERSIONS    ////
    ///////////////////////////

    /* NOTE: the Object wrapper can be implicitly converted to any of its subclasses by
     * applying a runtime type check as part of the assignment.  This allows us to
     * safely convert from a generic object to a more specialized type without worrying
     * about type mismatches or triggering arbitrary conversion logic.  It allows us to
     * write code like this:
     *
     *      py::Object obj = true;
     *      py::Bool b = obj;
     *
     * But not like this:
     *
     *      py::Object obj = true;
     *      py::Str s = obj;  // throws a TypeError
     *
     * While simultaneously preserving the ability to explicitly convert using a normal
     * constructor call:
     *
     *      py::Object obj = true;
     *      py::Str s(obj);
     *
     * Which is identical to calling `str()` at the python level.  Note that the
     * implicit conversion operator is only enabled for Object itself, and is deleted
     * in all of its subclasses.  This prevents implicit conversions between subclasses
     * and promotes any attempt to do so into a compile-time error.  For instance:
     *
     *      py::Bool b = true;
     *      py::Str s = b;  // fails to compile, calls a deleted function
     *
     * In general, this makes assignment via the `=` operator type-safe by default,
     * while explicit constructors are reserved for non-trivial conversions and/or
     * packing in the case of containers.
     */

    /* Contextually convert an Object into a boolean value for use in if/else 
    statements, with the same semantics as in Python. */
    inline explicit operator bool() const {
        int result = PyObject_IsTrue(m_ptr);
        if (result == -1) {
            Exception::from_python();
        }
        return result;
    }

    /* Explicitly cast to a string representation.  Equivalent to Python `str(obj)`. */
    inline explicit operator std::string() const {
        PyObject* str = PyObject_Str(m_ptr);
        if (str == nullptr) {
            Exception::from_python();
        }
        Py_ssize_t size;
        const char* data = PyUnicode_AsUTF8AndSize(str, &size);
        if (data == nullptr) {
            Py_DECREF(str);
            Exception::from_python();
        }
        std::string result(data, size);
        Py_DECREF(str);
        return result;
    }

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    BERTRAND_OBJECT_OPERATORS(Object)

};


template <impl::not_proxy_like T>
    requires (
        !std::is_pointer_v<T> &&
        !std::is_reference_v<T> &&
        !std::same_as<T, pybind11::handle> &&
        !std::same_as<T, pybind11::object> &&
        !std::derived_from<T, Object> &&
        !std::derived_from<T, pybind11::arg>
    )
struct __cast__<Object, T> {
    static constexpr bool enable = true;
    static T cast(const Object& self) {
        try {
            return Handle(self.ptr()).template cast<T>();
        } catch (...) {
            Exception::from_pybind11();
        }
    }
};


/* Borrow a reference to a raw Python handle. */
template <std::derived_from<Object> T>
T reinterpret_borrow(Handle obj) {
    return T(obj, Object::borrowed_t{});
}


/* Borrow a reference to a raw Python handle. */
template <std::derived_from<pybind11::object> T>
T reinterpret_borrow(Handle obj) {
    return pybind11::reinterpret_borrow<T>(obj);
}


/* Steal a reference to a raw Python handle. */
template <std::derived_from<Object> T>
T reinterpret_steal(Handle obj) {
    return T(obj, Object::stolen_t{});
}


/* Steal a reference to a raw Python handle. */
template <std::derived_from<pybind11::object> T>
T reinterpret_steal(Handle obj) {
    return pybind11::reinterpret_steal<T>(obj);
}


namespace impl {

    // TODO: make this accept an extra template argument for the value stored within
    // the container.  index() and count() should account for this

    // TODO: this could be a subclass of Inherits<> that adds in sequence protocol
    // operators automatically.
    // -> Get count()/index() type from __iter__?  Also maybe assert that the type
    // implements __iter__?

    /* Mixin holding operator overloads for types implementing the sequence protocol,
    which makes them both concatenatable and repeatable. */
    template <typename Derived>
    class SequenceOps {

        inline Derived& self() { return static_cast<Derived&>(*this); }
        inline const Derived& self() const { return static_cast<const Derived&>(*this); }

    public:
        /* Equivalent to Python `sequence.count(value)`, but also takes optional
        start/stop indices similar to `sequence.index()`. */
        inline Py_ssize_t count(
            const Object& value,
            Py_ssize_t start = 0,
            Py_ssize_t stop = -1
        ) const {
            if (start != 0 || stop != -1) {
                PyObject* slice = PySequence_GetSlice(self().ptr(), start, stop);
                if (slice == nullptr) {
                    Exception::from_python();
                }
                Py_ssize_t result = PySequence_Count(slice, value.ptr());
                Py_DECREF(slice);
                if (result == -1 && PyErr_Occurred()) {
                    Exception::from_python();
                }
                return result;
            } else {
                Py_ssize_t result = PySequence_Count(self().ptr(), value.ptr());
                if (result == -1 && PyErr_Occurred()) {
                    Exception::from_python();
                }
                return result;
            }
        }

        /* Equivalent to Python `s.index(value[, start[, stop]])`. */
        inline Py_ssize_t index(
            const Object& value,
            Py_ssize_t start = 0,
            Py_ssize_t stop = -1
        ) const {
            if (start != 0 || stop != -1) {
                PyObject* slice = PySequence_GetSlice(self().ptr(), start, stop);
                if (slice == nullptr) {
                    Exception::from_python();
                }
                Py_ssize_t result = PySequence_Index(slice, value.ptr());
                Py_DECREF(slice);
                if (result == -1 && PyErr_Occurred()) {
                    Exception::from_python();
                }
                return result;
            } else {
                Py_ssize_t result = PySequence_Index(self().ptr(), value.ptr());
                if (result == -1 && PyErr_Occurred()) {
                    Exception::from_python();
                }
                return result;
            }
        }

    protected:

        template <typename Return, typename L, typename R>
        static auto operator_add(const L& lhs, const R& rhs) {
            PyObject* result = PySequence_Concat(
                detail::object_or_cast(lhs).ptr(),
                detail::object_or_cast(rhs).ptr()
            );
            if (result == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Return>(result);
        }

        template <typename Return, typename L, typename R>
        static void operator_iadd(L& lhs, const R& rhs) {
            PyObject* result = PySequence_InPlaceConcat(
                lhs.ptr(),
                detail::object_or_cast(rhs).ptr()
            );
            if (result == nullptr) {
                Exception::from_python();
            } else if (result == lhs.ptr()) {
                Py_DECREF(result);
            } else {
                lhs = reinterpret_steal<L>(result);
            }
        }

        template <typename Return, typename L>
        static auto operator_mul(const L& lhs, Py_ssize_t repetitions) {
            PyObject* result = PySequence_Repeat(
                detail::object_or_cast(lhs).ptr(),
                repetitions
            );
            if (result == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Return>(result);
        }

        template <typename Return, typename L>
        static void operator_imul(L& lhs, Py_ssize_t repetitions) {
            PyObject* result = PySequence_InPlaceRepeat(
                lhs.ptr(),
                repetitions
            );
            if (result == nullptr) {
                Exception::from_python();
            } else if (result == lhs.ptr()) {
                Py_DECREF(result);
            } else {
                lhs = reinterpret_steal<L>(result);
            }
        }

    };

}


/* Helper struct for defining a subclass of py::Object.  This ensures correct template
 * deduction for operator overloads and conversions, and separates internal pybind11
 * integrations from the subclass definition.  It includes:
 *
 *      1. Internal constructors for reinterpret_borrow/steal.
 *      2. Copy/move constructors and assignment operators.
 *      3. Implicit conversion from internal pybind11 types, like accessor.
 *      4. Implicit conversion operators to bool, std::string, equivalent pybind11
 *         types, and subclasses of this type.
 *      5. Explicit conversion operators to any other type via pybind11::cast().
 *      6. Operator redefinitions to apply correct template constraints for control
 *         structs and their associated return types.
 *
 * Some of this may become obsolete when C++23's "deducing this" feature is more
 * widespread, but for now it's the only way to ensure correct template deduction
 * across the board without resorting to preprocessor macros. */
template <typename Derived, typename Base>
struct Inherits : public Base {

    // TODO: note that for generic containers, I have to filter the output of
    // check<T>() based on the value type.  pybind11 types should only be considered
    // if the value type is exactly py::Object.

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Inherit reinterpret_borrow, reinterpret_steal constructors. */
    Inherits(Handle h, const Object::borrowed_t& t) : Base(h, t) {}
    Inherits(Handle h, const Object::stolen_t& t) : Base(h, t) {}

    /* Copy/move from equivalent pybind11 types. */
    template <impl::pybind11_like T> requires (Derived::template check<T>())
    Inherits(T&& other) : Base(std::forward<T>(other)) {}

    /* Inherit implicit conversion from pybind11 accessor. */
    template <typename Policy>
    Inherits(const detail::accessor<Policy>& accessor) :
        Base(nullptr, Object::stolen_t{})
    {
        pybind11::object obj(accessor);
        if (Derived::check(obj)) {
            Base::m_ptr = obj.release().ptr();
        } else {
            throw impl::noconvert<Derived>(obj.ptr());
        }
    }

    ///////////////////////////
    ////    CONVERSIONS    ////
    ///////////////////////////

    // NOTE: there's just no way to make sure that these are inherited correctly
    // without explicitly defining them in the subclass.  Thankfully, the control
    // structs can make this much more robust and succinct, so this shouldn't be
    // so difficult.  In fact, I can probably insert them into OBJECT_OPERATORS.

    /* Inherit explicit conversion to bool. */
    inline explicit operator bool() const {
        return Base::operator bool();
    }

    /* Inherit explicit conversion to std::string. */
    inline explicit operator std::string() const {
        return Base::operator std::string();
    }

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////


    // TODO: implement operator overloads here and reduce BERTRAND_OPERATORS down to
    // friend declarations that can be replicated in each subclass.

    // TODO: this means replicating the operators in Object, but this isn't necessarily
    // a bad thing, since it gives us the opportunity to fully document them.

    // TODO: After this refactor, BERTRAND_OPERATORS should only need to be invoked if
    // you're changing the low-level API calls that are used for the object, which
    // should never happen.  It could actually not even escape the python.h header,
    // which would mean that no macros are exported from the python.h header besides
    // the ones exposed by PYBIND11 and bertrand/common.h.

};


}  // namespace py
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_COMMON_OBJECT_H
