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

    /* Standardized error message for type narrowing via pybind11 accessors or the
    generic Object wrapper. */
    template <typename Derived>
    TypeError noconvert(PyObject* obj) {
        std::ostringstream msg;
        msg << "cannot convert python object from type '"
            << Py_TYPE(obj)->tp_name
            << "' to type '"
            << reinterpret_cast<PyTypeObject*>(Derived::type.ptr())->tp_name
            << "'";
        return TypeError(msg.str());
    }

    /* Mixin holding operator overloads for types implementing the sequence protocol,
    which makes them both concatenatable and repeatable. */
    template <typename Derived>
    class SequenceOps {

        inline Derived& self() { return static_cast<Derived&>(*this); }
        inline const Derived& self() const { return static_cast<const Derived&>(*this); }

    public:
        /* Equivalent to Python `sequence.count(value)`, but also takes optional
        start/stop indices similar to `sequence.index()`. */
        template <typename T>
        inline Py_ssize_t count(
            const T& value,
            Py_ssize_t start = 0,
            Py_ssize_t stop = -1
        ) const {
            if (start != 0 || stop != -1) {
                PyObject* slice = PySequence_GetSlice(self().ptr(), start, stop);
                if (slice == nullptr) {
                    Exception::from_python();
                }
                Py_ssize_t result = PySequence_Count(
                    slice,
                    detail::object_or_cast(value).ptr()
                );
                Py_DECREF(slice);
                if (result == -1 && PyErr_Occurred()) {
                    Exception::from_python();
                }
                return result;
            } else {
                Py_ssize_t result = PySequence_Count(
                    self().ptr(),
                    detail::object_or_cast(value).ptr()
                );
                if (result == -1 && PyErr_Occurred()) {
                    Exception::from_python();
                }
                return result;
            }
        }

        /* Equivalent to Python `s.index(value[, start[, stop]])`. */
        template <typename T>
        inline Py_ssize_t index(
            const T& value,
            Py_ssize_t start = 0,
            Py_ssize_t stop = -1
        ) const {
            if (start != 0 || stop != -1) {
                PyObject* slice = PySequence_GetSlice(self().ptr(), start, stop);
                if (slice == nullptr) {
                    Exception::from_python();
                }
                Py_ssize_t result = PySequence_Index(
                    slice,
                    detail::object_or_cast(value).ptr()
                );
                Py_DECREF(slice);
                if (result == -1 && PyErr_Occurred()) {
                    Exception::from_python();
                }
                return result;
            } else {
                Py_ssize_t result = PySequence_Index(
                    self().ptr(),
                    detail::object_or_cast(value).ptr()
                );
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


/* All subclasses of py::Object must define these constructors, which are taken
directly from PYBIND11_OBJECT_COMMON and cover the basic object creation and
conversion logic.

The check() function will be called whenever a generic Object wrapper is implicitly
converted to this type.  It should return true if and only if the object has a
compatible type, and it will never be passed a null pointer.  If it returns false,
a TypeError will be raised during the assignment.

Also included are generic copy and move constructors that work with any object type
that is like this one.  It depends on each type implementing the `like` trait
before invoking this macro.

Lastly, a generic assignment operator is provided that triggers implicit
conversions to this type for any inputs that support them.  This is especially
nice for initializer-list syntax, enabling containers to be assigned to like this:

    py::List list = {1, 2, 3, 4, 5};
    list = {5, 4, 3, 2, 1};
*/
#define BERTRAND_OBJECT_COMMON(parent, cls, comptime_check, runtime_check)          \
    /* Implement check() for compile-time C++ types. */                             \
    template <typename T>                                                           \
    static consteval bool check() { return comptime_check<T>; }                     \
                                                                                    \
    /* Implement check() for runtime C++ values. */                                 \
    template <typename T> requires (!impl::python_like<T>)                          \
    static consteval bool check(const T&) {                                         \
        return check<T>();                                                          \
    }                                                                               \
                                                                                    \
    /* Implement check() for runtime Python values. */                              \
    template <typename T> requires (impl::python_like<T>)                           \
    static bool check(const T& obj) {                                               \
        return obj.ptr() != nullptr && runtime_check(obj.ptr());                    \
    }                                                                               \
                                                                                    \
    /* pybind11 expects check_ internally, but the idea is the same. */             \
    template <typename T> requires (impl::python_like<T>)                           \
    static constexpr bool check_(const T& value) { return check(value); }           \
    template <typename T> requires (!impl::python_like<T>)                          \
    static constexpr bool check_(const T& value) { return check(value); }           \
                                                                                    \
    /* Inherit tagged borrow/steal constructors. */                                 \
    cls(Handle h, const borrowed_t& t) : parent(h, t) {}                            \
    cls(Handle h, const stolen_t& t) : parent(h, t) {}                              \
                                                                                    \
    /* Convert a pybind11 accessor into this type. */                               \
    template <typename Policy>                                                      \
    cls(const detail::accessor<Policy>& accessor) {                                 \
        pybind11::object obj(accessor);                                             \
        if (check(obj)) {                                                           \
            m_ptr = obj.release().ptr();                                            \
        } else {                                                                    \
            throw impl::noconvert<cls>(obj.ptr());                                  \
        }                                                                           \
    }                                                                               \
                                                                                    \
    /* Trigger implicit conversions to this type via the assignment operator. */    \
    template <typename T> requires (std::is_convertible_v<T, cls>)                  \
    cls& operator=(T&& value) {                                                     \
        if constexpr (std::is_same_v<cls, std::decay_t<T>>) {                       \
            if (this != &value) {                                                   \
                parent::operator=(std::forward<T>(value));                          \
            }                                                                       \
        } else if constexpr (impl::has_conversion_operator<std::decay_t<T>, cls>) { \
            parent::operator=(value.operator cls());                                \
        } else {                                                                    \
            parent::operator=(cls(std::forward<T>(value)));                         \
        }                                                                           \
        return *this;                                                               \
    }                                                                               \
                                                                                    \
    /* Convert to a pybind11 handle. */                                             \
    inline operator pybind11::handle() const {                                      \
        return pybind11::handle(m_ptr);                                             \
    }                                                                               \
                                                                                    \
    /* Convert to a pybind11 object. */                                             \
    inline operator pybind11::object() const {                                      \
        return pybind11::reinterpret_borrow<pybind11::object>(m_ptr);               \
    }                                                                               \
                                                                                    \
    /* Delete type narrowing operator inherited from Object. */                     \
    template <std::derived_from<Object> T>                                          \
    operator T() const = delete;                                                    \
                                                                                    \
    /* Mark pybind11::cast operator as explicit for subclasses. */                  \
    template <typename T>                                                           \
        requires (!impl::proxy_like<T> && !std::derived_from<T, Object>)            \
    explicit operator T() const {                                                   \
        return parent::operator T();                                                \
    }                                                                               \
                                                                                    \
    template <StaticStr name>                                                       \
    impl::Attr<cls, name> attr() const {                                            \
        return impl::Attr<cls, name>(*this);                                        \
    }                                                                               \


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


// TODO: maybe place unsafe hooks for the protected helpers in the impl:: namespace
// so that they can be used from outside the Object class for optimization purposes
// in rounding algorithms, etc.  The base operators would then call these hooks, and
// the BERTRAND_OPERATORS macro would specify them as friends.  This satisfies the
// visibility issue for both the operators and rounding strategies.


/* A revised pybind11::object interface that allows implicit conversions to subtypes
(applying a type check on the way), explicit conversions to arbitrary C++ types via
static_cast<>, cross-language math operators, and generalized slice/attr syntax. */
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

    template <typename Return, typename T, typename... Args>
    static Return operator_call(const T& obj, Args&&... args) {
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

    template <typename Return, typename T, typename Key>
    static impl::Item<T, std::decay_t<Key>> operator_getitem(
        const T& obj,
        Key&& key
    );

    template <typename Return, typename T>
    static impl::Item<T, Slice> operator_getitem(
        const T& obj,
        std::initializer_list<impl::SliceInitializer> slice
    );

    template <typename Return, typename T>
    static impl::Iterator<impl::GenericIter<Return>> operator_begin(const T& obj);
    template <typename Return, typename T>
    static impl::Iterator<impl::GenericIter<Return>> operator_end(const T& obj);
    template <typename Return, typename T>
    static impl::Iterator<impl::GenericIter<Return>> operator_rbegin(const T& obj);
    template <typename Return, typename T>
    static impl::Iterator<impl::GenericIter<Return>> operator_rend(const T& obj);

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

    template <typename Return, typename T>
    static size_t operator_len(const T& obj) {
        Py_ssize_t size = PyObject_Size(obj.ptr());
        if (size < 0) {
            Exception::from_python();
        }
        return size;
    }

    template <typename T>
    static auto operator_dereference(const T& obj) {
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

    template <typename Return, typename T>
    static auto operator_abs(const T& obj) {
        PyObject* result = PyNumber_Absolute(obj.ptr());
        if (result == nullptr) {
            Exception::from_python();
        }
        return Return(result, stolen_t{});
    }

    template <typename Return, typename T>
    static auto operator_invert(const T& obj) {
        PyObject* result = PyNumber_Invert(detail::object_or_cast(obj).ptr());
        if (result == nullptr) {
            Exception::from_python();
        }
        return Return(result, stolen_t{});
    }

    template <typename Return, typename T>
    static auto operator_pos(const T& obj) {
        PyObject* result = PyNumber_Positive(detail::object_or_cast(obj).ptr());
        if (result == nullptr) {
            Exception::from_python();
        }
        return Return(result, stolen_t{});
    }

    template <typename Return, typename T>
    static void operator_increment(T& obj) {
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

    template <typename Return, typename T>
    static auto operator_neg(const T& obj) {
        PyObject* result = PyNumber_Negative(detail::object_or_cast(obj).ptr());
        if (result == nullptr) {
            Exception::from_python();
        }
        return Return(result, stolen_t{});
    }

    template <typename Return, typename T>
    static void operator_decrement(T& obj) {
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
    static Type type;

    /* Check whether the templated type is considered object-like at compile time. */
    template <typename T>
    static constexpr bool check() {
        return std::is_base_of_v<Object, T> || std::is_base_of_v<pybind11::object, T>;
    }

    /* Check whether a C++ value is considered object-like at compile time. */
    template <typename T> requires (!impl::python_like<T>)
    static constexpr bool check(const T& value) { return check<T>(); }

    /* Check whether a Python value is considered object-like at runtime. */
    template <typename T> requires (impl::python_like<T>)
    static constexpr bool check(const T& value) { return value.ptr() != nullptr; }

    /* Identical to the above, but pybind11 expects a method of this name. */
    template <typename T> requires (!impl::python_like<T>)
    static constexpr bool check_(const T& value) { return check(value); }
    template <typename T> requires (impl::python_like<T>)
    static constexpr bool check_(const T& value) { return check(value); }

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Default constructor.  Initializes to a null object, which should always be
    filled in before being returned to the user. NOTE: this has to be public for
    pybind11's type casters to work as intended. */
    Object() = default;

    /* reinterpret_borrow()/reinterpret_steal() constructors.  The tags themselves are
    protected and only accessible within subclasses of pybind11::object. */
    Object(Handle ptr, const borrowed_t&) : m_ptr(Py_XNewRef(ptr.ptr())) {}
    Object(Handle ptr, const stolen_t&) : m_ptr(ptr.ptr()) {}

    /* Copy constructor.  Borrows a reference to an existing object. */
    Object(const Object& other) : m_ptr(Py_XNewRef(other.m_ptr)) {}
    Object(const pybind11::object& other) : m_ptr(Py_XNewRef(other.ptr())) {}

    /* Move constructor.  Steals a reference to a temporary object. */
    Object(Object&& other) : m_ptr(other.m_ptr) { other.m_ptr = nullptr; }
    Object(pybind11::object&& other) : m_ptr(other.release().ptr()) {}

    /* Convert a pybind11 accessor into a generic Object. */
    template <typename Policy>
    Object(const detail::accessor<Policy>& accessor) {
        pybind11::object obj(accessor);
        if (check(obj)) {
            m_ptr = obj.release().ptr();
        } else {
            throw impl::noconvert<Object>(obj.ptr());
        }
    }

    /* Convert any C++ value into a generic python object. */
    template <typename T> requires (!impl::python_like<T>)
    explicit Object(const T& value) : m_ptr([&value] {
        try {
            return pybind11::cast(value).release().ptr();
        } catch (...) {
            Exception::from_pybind11();
        }
    }()) {}

    /* Copy assignment operator. */
    Object& operator=(const Object& other) {
        if (this != &other) {
            PyObject* temp = m_ptr;
            m_ptr = Py_XNewRef(other.m_ptr);
            Py_XDECREF(temp);
        }
        return *this;
    }

    /* Move assignment operator. */
    Object& operator=(Object&& other) {
        if (this != &other) {
            PyObject* temp = m_ptr;
            m_ptr = other.m_ptr;
            other.m_ptr = nullptr;
            Py_XDECREF(temp);
        }
        return *this;
    }

    /* Trigger implicit conversions to this type via the assignment operator. */
    template <typename T> requires (std::is_convertible_v<T, Object>)
    Object& operator=(T&& value) {
        PyObject* temp = m_ptr;
        if constexpr (impl::has_conversion_operator<std::remove_cvref_t<T>, Object>) {
            m_ptr = value.operator Object().release().ptr();
        } else {
            m_ptr = Object(std::forward<T>(value)).release().ptr();
        }
        Py_XDECREF(temp);
        return *this;
    }

    /* Destructor allows any object to be stored with static duration. */
    ~Object() noexcept {
        if (Py_IsInitialized()) {
            Py_XDECREF(m_ptr);
        }
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

    /* Implicitly convert an Object into a pybind11::handle. */
    inline operator pybind11::handle() const {
        return pybind11::handle(m_ptr);
    }

    /* Implicitly convert an Object into a pybind11::object. */
    inline operator pybind11::object() const {
        return pybind11::reinterpret_borrow<pybind11::object>(m_ptr);
    }

    /* Wrap an Object in a proxy class, which moves it into a managed buffer for
    granular control. */
    template <typename T> requires (impl::proxy_like<T>)
    operator T() const {
        return T(this->operator typename T::Wrapped());
    }

    /* Narrow an Object into one of its subclasses, applying a runtime type check
    against its value. */
    template <std::derived_from<Object> T>
    operator T() const {
        if (!T::check(*this)) {
            throw impl::noconvert<T>(m_ptr);
        }
        return T(m_ptr, borrowed_t{});
    }

    /* Explicitly convert to any other type using pybind11's type casting mechanism. */
    template <typename T>
        requires (!impl::proxy_like<T> && !std::derived_from<T, Object>)
    operator T() const {
        try {
            return Handle(m_ptr).template cast<T>();
        } catch (...) {
            Exception::from_pybind11();
        }
    }

    // TODO: this could bely a more general solution for conversions.  Implicit
    // conversions are always type safe, but static_cast<>() can also be used to
    // perform an explicit conversion no matter the type.  I could, for instance,
    // explicitly convert to any container type by just static_cast<>()ing to it
    // after converting the object to a corresponding Python type (say, by calling)
    // the list() constructor on it.  That would be pretty cool, and would allow
    // conversions to both Python types and C++ types in a single line of readable
    // code.

    /* Implicitly cast to a string representation.  Equivalent to Python `str(obj)`. */
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

    /* Contextually convert an Object into a boolean value for use in if/else 
    statements, with the same semantics as in Python. */
    inline explicit operator bool() const {
        int result = PyObject_IsTrue(m_ptr);
        if (result == -1) {
            Exception::from_python();
        }
        return result;
    }

    ////////////////////////////
    ////    BASE METHODS    ////
    ////////////////////////////

    /* Return the underlying PyObject pointer. */
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

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    BERTRAND_OBJECT_OPERATORS(Object)

    template <StaticStr name>
    impl::Attr<Object, name> attr() const;

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


}  // namespace py
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_COMMON_OBJECT_H
