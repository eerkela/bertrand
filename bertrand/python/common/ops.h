#if !defined(BERTRAND_PYTHON_COMMON_INCLUDED) && !defined(LINTER)
#error "This file should not be included directly.  Please include <bertrand/common.h> instead."
#endif

#ifndef BERTRAND_PYTHON_COMMON_OPS_H
#define BERTRAND_PYTHON_COMMON_OPS_H

#include "declarations.h"
#include "except.h"


namespace bertrand {
namespace py {


namespace ops {

    static const pybind11::int_ one = 1;

    namespace sequence {

        template <typename Return, typename L, typename R>
        struct add {
            static Return operator()(const impl::as_object_t<L>& lhs, const impl::as_object_t<R>& rhs) {
                PyObject* result = PySequence_Concat(lhs.ptr(), rhs.ptr());
                if (result == nullptr) {
                    Exception::from_python();
                }
                return reinterpret_steal<Return>(result);
            }
        };

        template <typename Return, typename L, typename R>
        struct iadd {
            static void operator()(L& lhs, const impl::as_object_t<R>& rhs) {
                PyObject* result = PySequence_InPlaceConcat(
                    lhs.ptr(),
                    rhs.ptr()
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

        template <typename Return, typename L, typename R>
        struct mul {
            static Return operator()(const L& lhs, const R& rhs) {
                PyObject* result;
                if constexpr (impl::int_like<L>) {
                    result = PySequence_Repeat(rhs.ptr(), lhs);
                } else {
                    result = PySequence_Repeat(lhs.ptr(), rhs);
                }
                if (result == nullptr) {
                    Exception::from_python();
                }
                return reinterpret_steal<Return>(result);
            }
        };

        template <typename Return, typename L, typename R>
        struct imul {
            static void operator()(L& lhs, Py_ssize_t rhs) {
                PyObject* result = PySequence_InPlaceRepeat(lhs.ptr(), rhs);
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

    template <typename Return, typename Self, typename... Args>
    struct call {
        static Return operator()(const Self& self, Args&&... args) {
            try {
                if constexpr (std::is_void_v<Return>) {
                    Function<Return(Args...)>::template invoke_py<Return>(
                        self.ptr(),
                        std::forward<Args>(args)...
                    );
                } else {
                    return Function<Return(Args...)>::template invoke_py<Return>(
                        self.ptr(),
                        std::forward<Args>(args)...
                    );
                }
            } catch (...) {
                Exception::from_pybind11();
            }
        }
    };

    template <typename Return, typename Self, typename Key>
    struct getitem {
        static auto operator()(const Self& self, auto&& key);
    };

    template <typename Return, typename Self, typename Key>
    struct contains {
        static bool operator()(const Self& self, const impl::as_object_t<Key>& key) {
            int result = PySequence_Contains(self.ptr(), key.ptr());
            if (result == -1) {
                Exception::from_python();
            }
            return result;
        }
    };

    template <typename Return, typename Self>
    struct len {
        static size_t operator()(const Self& self) {
            Py_ssize_t size = PyObject_Size(self.ptr());
            if (size < 0) {
                Exception::from_python();
            }
            return size;
        }
    };

    template <typename Return, typename Self>
    struct begin {
        static auto operator()(const Self& self);
    };

    template <typename Return, typename Self>
    struct end {
        static auto operator()(const Self& self);
    };

    template <typename Return, typename Self>
    struct rbegin {
        static auto operator()(const Self& self);
    };

    template <typename Return, typename Self>
    struct rend {
        static auto operator()(const Self& self);
    };

    // TODO: update dereference to use new unpacking architecture.  It should
    // also be renamed to unpack.

    template <typename Self>
    struct dereference {
        static auto operator()(const Self& self) {
            try {
                return *Handle(self.ptr());
            } catch (...) {
                Exception::from_pybind11();
            }
        }
    };

    template <typename Return, typename L, typename R>
    struct lt {
        static bool operator()(const impl::as_object_t<L>& lhs, const impl::as_object_t<R>& rhs) {
            int result = PyObject_RichCompareBool(lhs.ptr(), rhs.ptr(), Py_LT);
            if (result == -1) {
                Exception::from_python();
            }
            return result;
        }
    };

    template <typename Return, typename L, typename R>
    struct le {
        static bool operator()(const impl::as_object_t<L>& lhs, const impl::as_object_t<R>& rhs) {
            int result = PyObject_RichCompareBool(lhs.ptr(), rhs.ptr(), Py_LE);
            if (result == -1) {
                Exception::from_python();
            }
            return result;
        }
    };

    template <typename Return, typename L, typename R>
    struct eq {
        static bool operator()(const impl::as_object_t<L>& lhs, const impl::as_object_t<R>& rhs) {
            int result = PyObject_RichCompareBool(lhs.ptr(), rhs.ptr(), Py_EQ);
            if (result == -1) {
                Exception::from_python();
            }
            return result;
        }
    };

    template <typename Return, typename L, typename R>
    struct ne {
        static bool operator()(const impl::as_object_t<L>& lhs, const impl::as_object_t<R>& rhs) {
            int result = PyObject_RichCompareBool(lhs.ptr(), rhs.ptr(), Py_NE);
            if (result == -1) {
                Exception::from_python();
            }
            return result;
        }
    };

    template <typename Return, typename L, typename R>
    struct ge {
        static bool operator()(const impl::as_object_t<L>& lhs, const impl::as_object_t<R>& rhs) {
            int result = PyObject_RichCompareBool(lhs.ptr(), rhs.ptr(), Py_GE);
            if (result == -1) {
                Exception::from_python();
            }
            return result;
        }
    };

    template <typename Return, typename L, typename R>
    struct gt {
        static bool operator()(const impl::as_object_t<L>& lhs, const impl::as_object_t<R>& rhs) {
            int result = PyObject_RichCompareBool(lhs.ptr(), rhs.ptr(), Py_GT);
            if (result == -1) {
                Exception::from_python();
            }
            return result;
        }
    };

    template <typename Return, typename Self>
    struct abs {
        static Return operator()(const Self& self) {
            PyObject* result = PyNumber_Absolute(self.ptr());
            if (result == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Return>(result);
        }
    };

    template <typename Return, typename Self>
    struct invert {
        static Return operator()(const Self& self) {
            PyObject* result = PyNumber_Invert(self.ptr());
            if (result == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Return>(result);
        }
    };

    template <typename Return, typename Self>
    struct pos {
        static Return operator()(const Self& self) {
            PyObject* result = PyNumber_Positive(self.ptr());
            if (result == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Return>(result);
        }
    };

    template <typename Return, typename Self>
    struct neg {
        static Return operator()(const Self& self) {
            PyObject* result = PyNumber_Negative(self.ptr());
            if (result == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Return>(result);
        }
    };

    template <typename Return, typename Self>
    struct increment {
        static void operator()(Self& self) {
            PyObject* result = PyNumber_InPlaceAdd(self.ptr(), one.ptr());
            if (result == nullptr) {
                Exception::from_python();
            }
            if (result == self.ptr()) {
                Py_DECREF(result);
            } else {
                self = reinterpret_steal<Return>(result);
            }
        }
    };

    template <typename Return, typename Self>
    struct decrement {
        static void operator()(Self& self) {
            PyObject* result = PyNumber_InPlaceSubtract(self.ptr(), one.ptr());
            if (result == nullptr) {
                Exception::from_python();
            }
            if (result == self.ptr()) {
                Py_DECREF(result);
            } else {
                self = reinterpret_steal<Return>(result);
            }
        }
    };

    template <typename Return, typename L, typename R>
    struct add {
        static Return operator()(const impl::as_object_t<L>& lhs, const impl::as_object_t<R>& rhs) {
            PyObject* result = PyNumber_Add(lhs.ptr(), rhs.ptr());
            if (result == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Return>(result);
        }
    };

    template <typename Return, typename L, typename R>
    struct iadd {
        static void operator()(L& lhs, const impl::as_object_t<R>& rhs) {
            PyObject* result = PyNumber_InPlaceAdd(lhs.ptr(), rhs.ptr());
            if (result == nullptr) {
                Exception::from_python();
            } else if (result == lhs.ptr()) {
                Py_DECREF(result);
            } else {
                lhs = reinterpret_steal<L>(result);
            }
        }
    };

    template <typename Return, typename L, typename R>
    struct sub {
        static Return operator()(const impl::as_object_t<L>& lhs, const impl::as_object_t<R>& rhs) {
            PyObject* result = PyNumber_Subtract(lhs.ptr(), rhs.ptr());
            if (result == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Return>(result);
        }
    };

    template <typename Return, typename L, typename R>
    struct isub {
        static void operator()(L& lhs, const impl::as_object_t<R>& rhs) {
            PyObject* result = PyNumber_InPlaceAdd(lhs.ptr(), rhs.ptr());
            if (result == nullptr) {
                Exception::from_python();
            } else if (result == lhs.ptr()) {
                Py_DECREF(result);
            } else {
                lhs = reinterpret_steal<L>(result);
            }
        }
    };

    template <typename Return, typename L, typename R>
    struct mul {
        static Return operator()(const impl::as_object_t<L>& lhs, const impl::as_object_t<R>& rhs) {
            PyObject* result = PyNumber_Multiply(lhs.ptr(), rhs.ptr());
            if (result == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Return>(result);
        }
    };

    template <typename Return, typename L, typename R>
    struct imul {
        static void operator()(L& lhs, const impl::as_object_t<R>& rhs) {
            PyObject* result = PyNumber_InPlaceMultiply(lhs.ptr(), rhs.ptr());
            if (result == nullptr) {
                Exception::from_python();
            } else if (result == lhs.ptr()) {
                Py_DECREF(result);
            } else {
                lhs = reinterpret_steal<L>(result);
            }
        }
    };

    template <typename Return, typename L, typename R>
    struct truediv {
        static Return operator()(const impl::as_object_t<L>& lhs, const impl::as_object_t<R>& rhs) {
            PyObject* result = PyNumber_TrueDivide(lhs.ptr(), rhs.ptr());
            if (result == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Return>(result);
        }
    };

    template <typename Return, typename L, typename R>
    struct itruediv {
        static void operator()(L& lhs, const impl::as_object_t<R>& rhs) {
            PyObject* result = PyNumber_InPlaceTrueDivide(lhs.ptr(), rhs.ptr());
            if (result == nullptr) {
                Exception::from_python();
            } else if (result == lhs.ptr()) {
                Py_DECREF(result);
            } else {
                lhs = reinterpret_steal<L>(result);
            }
        }
    };

    template <typename Return, typename L, typename R>
    struct floordiv {
        static Return operator()(const impl::as_object_t<L>& lhs, const impl::as_object_t<R>& rhs) {
            PyObject* result = PyNumber_FloorDivide(lhs.ptr(), rhs.ptr());
            if (result == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Return>(result);
        }
    };

    template <typename Return, typename L, typename R>
    struct ifloordiv {
        static void operator()(L& lhs, const impl::as_object_t<R>& rhs) {
            PyObject* result = PyNumber_InPlaceFloorDivide(lhs.ptr(), rhs.ptr());
            if (result == nullptr) {
                Exception::from_python();
            } else if (result == lhs.ptr()) {
                Py_DECREF(result);
            } else {
                lhs = reinterpret_steal<L>(result);
            }
        }
    };

    template <typename Return, typename L, typename R>
    struct mod {
        static Return operator()(const impl::as_object_t<L>& lhs, const impl::as_object_t<R>& rhs) {
            PyObject* result = PyNumber_Remainder(lhs.ptr(), rhs.ptr());
            if (result == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Return>(result);
        }
    };

    template <typename Return, typename L, typename R>
    struct imod {
        static void operator()(L& lhs, const impl::as_object_t<R>& rhs) {
            PyObject* result = PyNumber_InPlaceRemainder(lhs.ptr(), rhs.ptr());
            if (result == nullptr) {
                Exception::from_python();
            } else if (result == lhs.ptr()) {
                Py_DECREF(result);
            } else {
                lhs = reinterpret_steal<L>(result);
            }
        }
    };

    template <typename Return, typename Base, typename Exp>
    struct pow {
        static Return operator()(const impl::as_object_t<Base>& base, const impl::as_object_t<Exp>& exp) {
            PyObject* result = PyNumber_Power(
                base.ptr(),
                exp.ptr(),
                Py_None
            );
            if (result == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Return>(result);
        }
    };

    template <typename Return, typename Base, typename Exp, typename Mod>
    struct powmod {
        static Return operator()(
            const impl::as_object_t<Base>& base,
            const impl::as_object_t<Exp>& exp,
            const impl::as_object_t<Mod>& mod
        ) {
            PyObject* result = PyNumber_Power(
                base.ptr(),
                exp.ptr(),
                mod.ptr()
            );
            if (result == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Return>(result);
        }
    };

    template <typename Return, typename Base, typename Exp>
    struct ipow {
        static void operator()(Base& base, const impl::as_object_t<Exp>& exp) {
            PyObject* result = PyNumber_InPlacePower(
                base.ptr(),
                exp.ptr(),
                Py_None
            );
            if (result == nullptr) {
                Exception::from_python();
            } else if (result == base.ptr()) {
                Py_DECREF(result);
            } else {
                base = reinterpret_steal<Base>(result);
            }
        }
    };

    template <typename Return, typename Base, typename Exp, typename Mod>
    struct ipowmod {
        static void operator()(
            Base& base,
            const impl::as_object_t<Exp>& exp,
            const impl::as_object_t<Mod>& mod
        ) {
            PyObject* result = PyNumber_InPlacePower(
                base.ptr(),
                exp.ptr(),
                mod.ptr()
            );
            if (result == nullptr) {
                Exception::from_python();
            } else if (result == base.ptr()) {
                Py_DECREF(result);
            } else {
                base = reinterpret_steal<Base>(result);
            }
        }
    };

    template <typename Return, typename L, typename R>
    struct lshift {
        static Return operator()(const impl::as_object_t<L>& lhs, const impl::as_object_t<R>& rhs) {
            PyObject* result = PyNumber_Lshift(lhs.ptr(), rhs.ptr());
            if (result == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Return>(result);
        }
    };

    template <typename Return, typename L, typename R>
    struct ilshift {
        static void operator()(L& lhs, const impl::as_object_t<R>& rhs) {
            PyObject* result = PyNumber_InPlaceLshift(lhs.ptr(), rhs.ptr());
            if (result == nullptr) {
                Exception::from_python();
            } else if (result == lhs.ptr()) {
                Py_DECREF(result);
            } else {
                lhs = reinterpret_steal<L>(result);
            }
        }
    };

    template <typename Return, typename L, typename R>
    struct rshift {
        static Return operator()(const impl::as_object_t<L>& lhs, const impl::as_object_t<R>& rhs) {
            PyObject* result = PyNumber_Rshift(lhs.ptr(), rhs.ptr());
            if (result == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Return>(result);
        }
    };

    template <typename Return, typename L, typename R>
    struct irshift {
        static void operator()(L& lhs, const impl::as_object_t<R>& rhs) {
            PyObject* result = PyNumber_InPlaceRshift(lhs.ptr(), rhs.ptr());
            if (result == nullptr) {
                Exception::from_python();
            } else if (result == lhs.ptr()) {
                Py_DECREF(result);
            } else {
                lhs = reinterpret_steal<L>(result);
            }
        }
    };

    template <typename Return, typename L, typename R>
    struct and_ {
        static Return operator()(const impl::as_object_t<L>& lhs, const impl::as_object_t<R>& rhs) {
            PyObject* result = PyNumber_And(lhs.ptr(), rhs.ptr());
            if (result == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Return>(result);
        }
    };

    template <typename Return, typename L, typename R>
    struct iand {
        static void operator()(L& lhs, const impl::as_object_t<R>& rhs) {
            PyObject* result = PyNumber_InPlaceAnd(lhs.ptr(), rhs.ptr());
            if (result == nullptr) {
                Exception::from_python();
            } else if (result == lhs.ptr()) {
                Py_DECREF(result);
            } else {
                lhs = reinterpret_steal<L>(result);
            }
        }
    };

    template <typename Return, typename L, typename R>
    struct or_ {
        static Return operator()(const impl::as_object_t<L>& lhs, const impl::as_object_t<R>& rhs) {
            PyObject* result = PyNumber_Or(lhs.ptr(), rhs.ptr());
            if (result == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Return>(result);
        }
    };

    template <typename Return, typename L, typename R>
    struct ior {
        static void operator()(L& lhs, const impl::as_object_t<R>& rhs) {
            PyObject* result = PyNumber_InPlaceOr(lhs.ptr(), rhs.ptr());
            if (result == nullptr) {
                Exception::from_python();
            } else if (result == lhs.ptr()) {
                Py_DECREF(result);
            } else {
                lhs = reinterpret_steal<L>(result);
            }
        }
    };

    template <typename Return, typename L, typename R>
    struct xor_ {
        static Return operator()(const impl::as_object_t<L>& lhs, const impl::as_object_t<R>& rhs) {
            PyObject* result = PyNumber_Xor(lhs.ptr(), rhs.ptr());
            if (result == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Return>(result);
        }
    };

    template <typename Return, typename L, typename R>
    struct ixor {
        static void operator()(L& lhs, const impl::as_object_t<R>& rhs) {
            PyObject* result = PyNumber_InPlaceXor(lhs.ptr(), rhs.ptr());
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


/* Convert an arbitrary C++ value to an equivalent Python object if it isn't one
already. */
template <typename T> requires (__as_object__<std::decay_t<T>>::enable)
[[nodiscard]] auto as_object(T&& value) -> __as_object__<std::decay_t<T>>::Return {
    return std::forward<T>(value);
}


/* Equivalent to Python `print(args...)`. */
template <typename... Args>
void print(Args&&... args) {
    try {
        pybind11::print(std::forward<Args>(args)...);
    } catch (...) {
        Exception::from_pybind11();
    }
}


/* Equivalent to Python `repr(obj)`, but returns a std::string and attempts to
represent C++ types using std::to_string or the stream insertion operator (<<).  If all
else fails, falls back to typeid(obj).name(). */
template <typename T>
[[nodiscard]] std::string repr(const T& obj) {
    if constexpr (impl::has_stream_insertion<T>) {
        std::ostringstream stream;
        stream << obj;
        return stream.str();

    } else if constexpr (impl::has_to_string<T>) {
        return std::to_string(obj);

    } else {
        try {
            return pybind11::repr(obj).template cast<std::string>();
        } catch (...) {
            return typeid(obj).name();
        }
    }
}


/* Equivalent to Python `hash(obj)`, but delegates to std::hash, which is overloaded
for the relevant Python types.  This promotes hash-not-implemented exceptions into
compile-time equivalents. */
template <impl::is_hashable T>
[[nodiscard]] size_t hash(const T& obj) {
    return std::hash<std::decay_t<T>>{}(std::forward<T>(obj));
}


/* Equivalent to Python `len(obj)`, but also accepts C++ types implementing a .size()
method. */
template <typename T> requires (impl::has_size<T> || std::derived_from<T, pybind11::object>)
[[nodiscard]] size_t len(const T& obj) {
    try {
        if constexpr (impl::has_size<T>) {
            return std::size(obj);
        } else {
            return pybind11::len(obj);
        }
    } catch (...) {
        Exception::from_pybind11();
    }
}


/* Equivalent to Python `iter(obj)` except that it can also accept C++ containers and
generate Python iterators over them.  Note that requesting an iterator over an rvalue
is not allowed, and will trigger a compiler error. */
template <impl::is_iterable T>
[[nodiscard]] pybind11::iterator iter(T&& obj) {
    static_assert(
        !std::is_rvalue_reference_v<T>,
        "passing a temporary container to py::iter() is unsafe"
    );
    try {
        return pybind11::make_iterator(std::begin(obj), std::end(obj));
    } catch (...) {
        Exception::from_pybind11();
    }
}


/* Equivalent to Python `iter(obj)` except that it takes raw C++ iterators and converts
them into a valid Python iterator object. */
template <typename Iter, std::sentinel_for<Iter> Sentinel>
[[nodiscard]] pybind11::iterator iter(Iter&& begin, Sentinel&& end) {
    try {
        return pybind11::make_iterator(
            std::forward<Iter>(begin),
            std::forward<Sentinel>(end)
        );
    } catch (...) {
        Exception::from_pybind11();
    }
}


/* Equivalent to Python `reversed(obj)` except that it can also accept C++ containers
and generate Python iterators over them.  Note that requesting an iterator over an
rvalue is not allowed, and will trigger a compiler error. */
template <impl::is_reverse_iterable T>
[[nodiscard]] pybind11::iterator reversed(T&& obj) {
    static_assert(
        !std::is_rvalue_reference_v<T>,
        "passing a temporary container to py::reversed() is unsafe"
    );
    try {
        if constexpr (std::derived_from<std::decay_t<T>, pybind11::object>) {
            return reinterpret_steal<pybind11::iterator>(
                obj.attr("__reversed__")().release()
            );
        } else {
            return pybind11::make_iterator(std::rbegin(obj), std::rend(obj));
        }
    } catch (...) {
        Exception::from_pybind11();
    }
}


/* Equivalent to Python `reversed(obj)` except that it takes raw C++ iterators and
converts them into a valid Python iterator object. */
template <typename Iter, std::sentinel_for<Iter> Sentinel>
[[nodiscard]] pybind11::iterator reversed(Iter&& begin, Sentinel&& end) {
    try {
        return pybind11::make_iterator(
            std::forward<Iter>(begin),
            std::forward<Sentinel>(end)
        );
    } catch (...) {
        Exception::from_pybind11();
    }
}


/* Equivalent to Python `abs(obj)` for any object that specializes the __abs__ control
struct. */
template <std::derived_from<Object> Self> requires (__abs__<Self>::enable)
[[nodiscard]] auto abs(const Self& self) {
    using Return = __abs__<Self>::Return;
    static_assert(
        std::derived_from<Return, Object>,
        "Absolute value operator must return a py::Object subclass.  Check your "
        "specialization of __abs__ for this type and ensure the Return type is set to "
        "a py::Object subclass."
    );
    if constexpr (impl::proxy_like<Self>) {
        return abs(self.value());
    } else {
        return ops::abs<Return, Self>::operator()(self);
    }
}


/* Equivalent to Python `abs(obj)`, except that it takes a C++ value and applies
std::abs() for identical semantics. */
template <impl::has_abs T> requires (!__abs__<T>::enable)
[[nodiscard]] auto abs(const T& value) {
    return std::abs(value);
}


/* Equivalent to Python `base ** exp` (exponentiation). */
template <typename Base, typename Exp> requires (__pow__<Base, Exp>::enable)
[[nodiscard]] auto pow(const Base& base, const Exp& exp) {
    using Return = typename __pow__<Base, Exp>::Return;
    static_assert(
        std::derived_from<Return, Object>,
        "pow() must return a py::Object subclass.  Check your specialization "
        "of __pow__ for this type and ensure the Return type is derived from "
        "py::Object."
    );
    if constexpr (impl::proxy_like<Base>) {
        return pow(base.value(), exp);
    } else if constexpr (impl::proxy_like<Exp>) {
        return pow(base, exp.value());
    } else {
        return ops::pow<Return, Base, Exp>::operator()(base, exp);
    }
}


/* Equivalent to Python `pow(base, exp)`, except that it takes a C++ value and applies
std::pow() for identical semantics. */
template <typename Base, typename Exp> requires (!impl::any_are_python_like<Base, Exp>)
[[nodiscard]] auto pow(const Base& base, const Exp& exponent) {
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
[[nodiscard]] auto pow(const Base& base, const Exp& exp, const Mod& mod) {
    using Return = typename __pow__<Base, Exp>::Return;
    static_assert(
        std::derived_from<Return, Object>,
        "pow() must return a py::Object subclass.  Check your specialization "
        "of __pow__ for this type and ensure the Return type is derived from "
        "py::Object."
    );
    if constexpr (impl::proxy_like<Base>) {
        return pow(base.value(), exp, mod);
    } else if constexpr (impl::proxy_like<Exp>) {
        return pow(base, exp.value(), mod);
    } else {
        return ops::powmod<Return, Base, Exp, Mod>::operator()(base, exp, mod);
    }
}


// TODO: enable C++ equivalent for modular exponentiation

// /* Equivalent to Python `pow(base, exp, mod)`, but works on C++ integers with identical
// semantics. */
// template <std::integral Base, std::integral Exp, std::integral Mod>
// [[nodiscard]] auto pow(Base base, Exp exp, Mod mod) {
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


template <std::derived_from<Object> Self> requires (__iter__<Self>::enable)
[[nodiscard]] auto operator*(const Self& self) {
    if constexpr (impl::proxy_like<Self>) {
        return *self.value();
    } else {
        return ops::dereference<Self>::operator()(self);
    }
}


template <std::derived_from<Object> Self> requires (!__invert__<Self>::enable)
auto operator~(const Self& self) = delete;
template <std::derived_from<Object> Self> requires (__invert__<Self>::enable)
auto operator~(const Self& self) {
    using Return = typename __invert__<Self>::Return;
    static_assert(
        std::derived_from<Return, Object>,
        "Bitwise NOT operator must return a py::Object subclass.  Check your "
        "specialization of __invert__ for this type and ensure the Return type "
        "is set to a py::Object subclass."
    );
    if constexpr (impl::proxy_like<Self>) {
        return ~self.value();
    } else {
        return ops::invert<Return, Self>::operator()(self);
    }
}


template <std::derived_from<Object> Self> requires (!__pos__<Self>::enable)
auto operator+(const Self& self) = delete;
template <std::derived_from<Object> Self> requires (__pos__<Self>::enable)
auto operator+(const Self& self) {
    using Return = typename __pos__<Self>::Return;
    static_assert(
        std::derived_from<Return, Object>,
        "Unary positive operator must return a py::Object subclass.  Check "
        "your specialization of __pos__ for this type and ensure the Return "
        "type is set to a py::Object subclass."
    );
    if constexpr (impl::proxy_like<Self>) {
        return +self.value();
    } else {
        return ops::pos<Return, Self>::operator()(self);
    }
}


template <std::derived_from<Object> Self> requires (!__neg__<Self>::enable)
auto operator-(const Self& self) = delete;
template <std::derived_from<Object> Self> requires (__neg__<Self>::enable)
auto operator-(const Self& self) {
    using Return = typename __neg__<Self>::Return;
    static_assert(
        std::derived_from<Return, Object>,
        "Unary negative operator must return a py::Object subclass.  Check "
        "your specialization of __neg__ for this type and ensure the Return "
        "type is set to a py::Object subclass."
    );
    if constexpr (impl::proxy_like<Self>) {
        return -self.value();
    } else {
        return ops::neg<Return, Self>::operator()(self);
    }
}


template <std::derived_from<Object> Self> requires (!__increment__<Self>::enable)
Self& operator++(Self& self) = delete;
template <std::derived_from<Object> Self> requires (__increment__<Self>::enable)
Self& operator++(Self& self) {
    using Return = typename __increment__<Self>::Return;
    static_assert(
        std::same_as<Return, Self>,
        "Increment operator must return a reference to the derived type.  "
        "Check your specialization of __increment__ for this type and ensure "
        "the Return type is set to the derived type."
    );
    if constexpr (impl::proxy_like<Self>) {
        ++self.value();
    } else {
        static_assert(
            impl::python_like<Self>,
            "Increment operator requires a Python object."
        );
        ops::increment<Return, Self>::operator()(self);
    }
    return self;
}


template <std::derived_from<Object> Self> requires (!__increment__<Self>::enable)
Self operator++(Self& self, int) = delete;
template <std::derived_from<Object> Self> requires (__increment__<Self>::enable)
Self operator++(Self& self, int) {
    using Return = typename __increment__<Self>::Return;
    static_assert(
        std::same_as<Return, Self>,
        "Increment operator must return a reference to the derived type.  "
        "Check your specialization of __increment__ for this type and ensure "
        "the Return type is set to the derived type."
    );
    Self copy = self;
    if constexpr (impl::proxy_like<Self>) {
        ++self.value();
    } else {
        static_assert(
            impl::python_like<Self>,
            "Increment operator requires a Python object."
        );
        ops::increment<Return, Self>::operator()(self);
    }
    return copy;
}


template <std::derived_from<Object> Self> requires (!__decrement__<Self>::enable)
Self& operator--(Self& self) = delete;
template <std::derived_from<Object> Self> requires (__decrement__<Self>::enable)
Self& operator--(Self& self) {
    using Return = typename __decrement__<Self>::Return;
    static_assert(
        std::same_as<Return, Self>,
        "Decrement operator must return a reference to the derived type.  "
        "Check your specialization of __decrement__ for this type and ensure "
        "the Return type is set to the derived type."
    );
    if constexpr (impl::proxy_like<Self>) {
        --self.value();
    } else {
        static_assert(
            impl::python_like<Self>,
            "Decrement operator requires a Python object."
        );
        ops::decrement<Return, Self>::operator()(self);
    }
    return self;
}


template <std::derived_from<Object> Self> requires (!__decrement__<Self>::enable)
Self operator--(Self& self, int) = delete;
template <std::derived_from<Object> Self> requires (__decrement__<Self>::enable)
Self operator--(Self& self, int) {
    using Return = typename __decrement__<Self>::Return;
    static_assert(
        std::same_as<Return, Self>,
        "Decrement operator must return a reference to the derived type.  "
        "Check your specialization of __decrement__ for this type and ensure "
        "the Return type is set to the derived type."
    );
    Self copy = self;
    if constexpr (impl::proxy_like<Self>) {
        --self.value();
    } else {
        static_assert(
            impl::python_like<Self>,
            "Decrement operator requires a Python object."
        );
        ops::decrement<Return, Self>::operator()(self);
    }
    return copy;
}


template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && !__lt__<L, R>::enable)
auto operator<(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && __lt__<L, R>::enable)
auto operator<(const L& lhs, const R& rhs) {
    using Return = typename __lt__<L, R>::Return;
    static_assert(
        std::same_as<Return, bool>,
        "Less-than operator must return a boolean value.  Check your "
        "specialization of __lt__ for these types and ensure the Return type "
        "is set to bool."
    );
    if constexpr (impl::proxy_like<L>) {
        return lhs.value() < rhs;
    } else if constexpr (impl::proxy_like<R>) {
        return lhs < rhs.value();
    } else {
        return ops::lt<Return, L, R>::operator()(lhs, rhs);
    }
}


template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && !__le__<L, R>::enable)
auto operator<=(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && __le__<L, R>::enable)
auto operator<=(const L& lhs, const R& rhs) {
    using Return = typename __le__<L, R>::Return;
    static_assert(
        std::same_as<Return, bool>,
        "Less-than-or-equal operator must return a boolean value.  Check your "
        "specialization of __le__ for this type and ensure the Return type is "
        "set to bool."
    );
    if constexpr (impl::proxy_like<L>) {
        return lhs.value() <= rhs;
    } else if constexpr (impl::proxy_like<R>) {
        return lhs <= rhs.value();
    } else {
        return ops::le<Return, L, R>::operator()(lhs, rhs);
    }
}


template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && !__eq__<L, R>::enable)
auto operator==(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && __eq__<L, R>::enable)
auto operator==(const L& lhs, const R& rhs) {
    using Return = typename __eq__<L, R>::Return;
    static_assert(
        std::same_as<Return, bool>,
        "Equality operator must return a boolean value.  Check your "
        "specialization of __eq__ for this type and ensure the Return type is "
        "set to bool."
    );
    if constexpr (impl::proxy_like<L>) {
        return lhs.value() == rhs;
    } else if constexpr (impl::proxy_like<R>) {
        return lhs == rhs.value();
    } else {
        return ops::eq<Return, L, R>::operator()(lhs, rhs);
    }
}


template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && !__ne__<L, R>::enable)
auto operator!=(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && __ne__<L, R>::enable)
auto operator!=(const L& lhs, const R& rhs) {
    using Return = typename __ne__<L, R>::Return;
    static_assert(
        std::same_as<Return, bool>,
        "Inequality operator must return a boolean value.  Check your "
        "specialization of __ne__ for this type and ensure the Return type is "
        "set to bool."
    );
    if constexpr (impl::proxy_like<L>) {
        return lhs.value() != rhs;
    } else if constexpr (impl::proxy_like<R>) {
        return lhs != rhs.value();
    } else {
        return ops::ne<Return, L, R>::operator()(lhs, rhs);
    }
}


template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && !__ge__<L, R>::enable)
auto operator>=(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && __ge__<L, R>::enable)
auto operator>=(const L& lhs, const R& rhs) {
    using Return = typename __ge__<L, R>::Return;
    static_assert(
        std::same_as<Return, bool>,
        "Greater-than-or-equal operator must return a boolean value.  Check "
        "your specialization of __ge__ for this type and ensure the Return "
        "type is set to bool."
    );
    if constexpr (impl::proxy_like<L>) {
        return lhs.value() >= rhs;
    } else if constexpr (impl::proxy_like<R>) {
        return lhs >= rhs.value();
    } else {
        return ops::ge<Return, L, R>::operator()(lhs, rhs);
    }
}


template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && !__gt__<L, R>::enable)
auto operator>(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && __gt__<L, R>::enable)
auto operator>(const L& lhs, const R& rhs) {
    using Return = typename __gt__<L, R>::Return;
    static_assert(
        std::same_as<Return, bool>,
        "Greater-than operator must return a boolean value.  Check your "
        "specialization of __gt__ for this type and ensure the Return type is "
        "set to bool."
    );
    if constexpr (impl::proxy_like<L>) {
        return lhs.value() > rhs;
    } else if constexpr (impl::proxy_like<R>) {
        return lhs > rhs.value();
    } else {
        return ops::gt<Return, L, R>::operator()(lhs, rhs);
    }
}


template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && !__add__<L, R>::enable)
auto operator+(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && __add__<L, R>::enable)
auto operator+(const L& lhs, const R& rhs) {
    using Return = typename __add__<L, R>::Return;
    static_assert(
        std::derived_from<Return, Object>,
        "Addition operator must return a py::Object subclass.  Check your "
        "specialization of __add__ for this type and ensure the Return type is "
        "derived from py::Object."
    );
    if constexpr (impl::proxy_like<L>) {
        return lhs.value() + rhs;
    } else if constexpr (impl::proxy_like<R>) {
        return lhs + rhs.value();
    } else {
        return ops::add<Return, L, R>::operator()(lhs, rhs);
    }
}


template <std::derived_from<Object> L, typename R> requires (!__iadd__<L, R>::enable)
L& operator+=(L& lhs, const R& rhs) = delete;
template <std::derived_from<Object> L, typename R> requires (__iadd__<L, R>::enable)
L& operator+=(L& lhs, const R& rhs) {
    using Return = typename __iadd__<L, R>::Return;
    static_assert(
        std::same_as<Return, L&>,
        "In-place addition operator must return a mutable reference to the left "
        "operand.  Check your specialization of __iadd__ for these types and "
        "ensure the Return type is set to the left operand."
    );
    if constexpr (impl::proxy_like<L>) {
        lhs.value() += rhs;
    } else if constexpr (impl::proxy_like<R>) {
        lhs += rhs.value();
    } else {
        static_assert(
            impl::python_like<L>,
            "In-place addition operator requires a Python object."
        );
        ops::iadd<Return, L, R>::operator()(lhs, rhs);
    }
    return lhs;
}


template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && !__sub__<L, R>::enable)
auto operator-(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && __sub__<L, R>::enable)
auto operator-(const L& lhs, const R& rhs) {
    using Return = typename __sub__<L, R>::Return;
    static_assert(
        std::derived_from<Return, Object>,
        "Subtraction operator must return a py::Object subclass.  Check your "
        "specialization of __sub__ for this type and ensure the Return type is "
        "derived from py::Object."
    );
    if constexpr (impl::proxy_like<L>) {
        return lhs.value() - rhs;
    } else if constexpr (impl::proxy_like<R>) {
        return lhs - rhs.value();
    } else {
        return ops::sub<Return, L, R>::operator()(lhs, rhs);
    }
}


template <std::derived_from<Object> L, typename R> requires (!__isub__<L, R>::enable)
L& operator-=(L& lhs, const R& rhs) = delete;
template <std::derived_from<Object> L, typename R> requires (__isub__<L, R>::enable)
L& operator-=(L& lhs, const R& rhs) {
    using Return = typename __isub__<L, R>::Return;
    static_assert(
        std::same_as<Return, L&>,
        "In-place addition operator must return a mutable reference to the left "
        "operand.  Check your specialization of __isub__ for these types and "
        "ensure the Return type is set to the left operand."
    );
    if constexpr (impl::proxy_like<L>) {
        lhs.value() -= rhs;
    } else if constexpr (impl::proxy_like<R>) {
        lhs -= rhs.value();
    } else {
        static_assert(
            impl::python_like<L>,
            "In-place addition operator requires a Python object."
        );
        ops::isub<Return, L, R>::operator()(lhs, rhs);
    }
    return lhs;
}


template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && !__mul__<L, R>::enable)
auto operator*(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && __mul__<L, R>::enable)
auto operator*(const L& lhs, const R& rhs) {
    using Return = typename __mul__<L, R>::Return;
    static_assert(
        std::derived_from<Return, Object>,
        "Multiplication operator must return a py::Object subclass.  Check "
        "your specialization of __mul__ for this type and ensure the Return "
        "type is derived from py::Object."
    );
    if constexpr (impl::proxy_like<L>) {
        return lhs.value() * rhs;
    } else if constexpr (impl::proxy_like<R>) {
        return lhs * rhs.value();
    } else {
        return ops::mul<Return, L, R>::operator()(lhs, rhs);
    }
}


template <std::derived_from<Object> L, typename R> requires (!__imul__<L, R>::enable)
L& operator*=(L& lhs, const R& rhs) = delete;
template <std::derived_from<Object> L, typename R> requires (__imul__<L, R>::enable)
L& operator*=(L& lhs, const R& rhs) {
    using Return = typename __imul__<L, R>::Return;
    static_assert(
        std::same_as<Return, L&>,
        "In-place multiplication operator must return a mutable reference to the "
        "left operand.  Check your specialization of __imul__ for these types "
        "and ensure the Return type is set to the left operand."
    );
    if constexpr (impl::proxy_like<L>) {
        lhs.value() *= rhs;
    } else if constexpr (impl::proxy_like<R>) {
        lhs *= rhs.value();
    } else {
        static_assert(
            impl::python_like<L>,
            "In-place multiplication operator requires a Python object."
        );
        ops::imul<Return, L, R>::operator()(lhs, rhs);
    }
    return lhs;
}


template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && !__truediv__<L, R>::enable)
auto operator/(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && __truediv__<L, R>::enable)
auto operator/(const L& lhs, const R& rhs) {
    using Return = typename __truediv__<L, R>::Return;
    static_assert(
        std::derived_from<Return, Object>,
        "True division operator must return a py::Object subclass.  Check "
        "your specialization of __truediv__ for this type and ensure the "
        "Return type is derived from py::Object."
    );
    if constexpr (impl::proxy_like<L>) {
        return lhs.value() / rhs;
    } else if constexpr (impl::proxy_like<R>) {
        return lhs / rhs.value();
    } else {
        return ops::truediv<Return, L, R>::operator()(lhs, rhs);
    }
}


template <std::derived_from<Object> L, typename R> requires (!__itruediv__<L, R>::enable)
L& operator/=(L& lhs, const R& rhs) = delete;
template <std::derived_from<Object> L, typename R> requires (__itruediv__<L, R>::enable)
L& operator/=(L& lhs, const R& rhs) {
    using Return = typename __itruediv__<L, R>::Return;
    static_assert(
        std::same_as<Return, L&>,
        "In-place true division operator must return a mutable reference to the "
        "left operand.  Check your specialization of __itruediv__ for these "
        "types and ensure the Return type is set to the left operand."
    );
    if constexpr (impl::proxy_like<L>) {
        lhs.value() /= rhs;
    } else if constexpr (impl::proxy_like<R>) {
        lhs /= rhs.value();
    } else {
        static_assert(
            impl::python_like<L>,
            "In-place true division operator requires a Python object."
        );
        ops::itruediv<Return, L, R>::operator()(lhs, rhs);
    }
    return lhs;
}


template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && !__mod__<L, R>::enable)
auto operator%(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && __mod__<L, R>::enable)
auto operator%(const L& lhs, const R& rhs) {
    using Return = typename __mod__<L, R>::Return;
    static_assert(
        std::derived_from<Return, Object>,
        "Modulus operator must return a py::Object subclass.  Check your "
        "specialization of __mod__ for this type and ensure the Return type "
        "is derived from py::Object."
    );
    if constexpr (impl::proxy_like<L>) {
        return lhs.value() % rhs;
    } else if constexpr (impl::proxy_like<R>) {
        return lhs % rhs.value();
    } else {
        return ops::mod<Return, L, R>::operator()(lhs, rhs);
    }
}


template <std::derived_from<Object> L, typename R> requires (!__imod__<L, R>::enable)
L& operator%=(L& lhs, const R& rhs) = delete;
template <std::derived_from<Object> L, typename R> requires (__imod__<L, R>::enable)
L& operator%=(L& lhs, const R& rhs) {
    using Return = typename __imod__<L, R>::Return;
    static_assert(
        std::same_as<Return, L&>,
        "In-place modulus operator must return a mutable reference to the left "
        "operand.  Check your specialization of __imod__ for these types and "
        "ensure the Return type is set to the left operand."
    );
    if constexpr (impl::proxy_like<L>) {
        lhs.value() %= rhs;
    } else if constexpr (impl::proxy_like<R>) {
        lhs %= rhs.value();
    } else {
        static_assert(
            impl::python_like<L>,
            "In-place modulus operator requires a Python object."
        );
        ops::imod<Return, L, R>::operator()(lhs, rhs);
    }
    return lhs;
}


template <typename L, typename R>
    requires (
        impl::any_are_python_like<L, R> &&
        !std::derived_from<L, std::ostream> &&
        !__lshift__<L, R>::enable
    )
auto operator<<(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (
        impl::any_are_python_like<L, R> &&
        !std::derived_from<L, std::ostream> &&
        __lshift__<L, R>::enable
    )
auto operator<<(const L& lhs, const R& rhs) {
    using Return = typename __lshift__<L, R>::Return;
    static_assert(
        std::derived_from<Return, Object>,
        "Left shift operator must return a py::Object subclass.  Check your "
        "specialization of __lshift__ for this type and ensure the Return "
        "type is derived from py::Object."
    );
    if constexpr (impl::proxy_like<L>) {
        return lhs.value() << rhs;
    } else if constexpr (impl::proxy_like<R>) {
        return lhs << rhs.value();
    } else {
        return ops::lshift<Return, L, R>::operator()(lhs, rhs);
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


template <std::derived_from<Object> L, typename R> requires (!__ilshift__<L, R>::enable)
L& operator<<=(L& lhs, const R& rhs) = delete;
template <std::derived_from<Object> L, typename R> requires (__ilshift__<L, R>::enable)
L& operator<<=(L& lhs, const R& rhs) {
    using Return = typename __ilshift__<L, R>::Return;
    static_assert(
        std::same_as<Return, L&>,
        "In-place left shift operator must return a mutable reference to the left "
        "operand.  Check your specialization of __ilshift__ for these types "
        "and ensure the Return type is set to the left operand."
    );
    if constexpr (impl::proxy_like<L>) {
        lhs.value() <<= rhs;
    } else if constexpr (impl::proxy_like<R>) {
        lhs <<= rhs.value();
    } else {
        static_assert(
            impl::python_like<L>,
            "In-place left shift operator requires a Python object."
        );
        ops::ilshift<Return, L, R>::operator()(lhs, rhs);
    }
    return lhs;
}


template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && !__rshift__<L, R>::enable)
auto operator>>(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && __rshift__<L, R>::enable)
auto operator>>(const L& lhs, const R& rhs) {
    using Return = typename __rshift__<L, R>::Return;
    static_assert(
        std::derived_from<Return, Object>,
        "Right shift operator must return a py::Object subclass.  Check your "
        "specialization of __rshift__ for this type and ensure the Return "
        "type is derived from py::Object."
    );
    if constexpr (impl::proxy_like<L>) {
        return lhs.value() >> rhs;
    } else if constexpr (impl::proxy_like<R>) {
        return lhs >> rhs.value();
    } else {
        return ops::rshift<Return, L, R>::operator()(lhs, rhs);
    }
}


template <std::derived_from<Object> L, typename R> requires (!__irshift__<L, R>::enable)
L& operator>>=(L& lhs, const R& rhs) = delete;
template <std::derived_from<Object> L, typename R> requires (__irshift__<L, R>::enable)
L& operator>>=(L& lhs, const R& rhs) {
    using Return = typename __irshift__<L, R>::Return;
    static_assert(
        std::same_as<Return, L&>,
        "In-place right shift operator must return a mutable reference to the left "
        "operand.  Check your specialization of __irshift__ for these types "
        "and ensure the Return type is set to the left operand."
    );
    if constexpr (impl::proxy_like<L>) {
        lhs.value() >>= rhs;
    } else if constexpr (impl::proxy_like<R>) {
        lhs >>= rhs.value();
    } else {
        static_assert(
            impl::python_like<L>,
            "In-place right shift operator requires a Python object."
        );
        ops::irshift<Return, L, R>::operator()(lhs, rhs);
    }
    return lhs;
}


template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && !__and__<L, R>::enable)
auto operator&(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && __and__<L, R>::enable)
auto operator&(const L& lhs, const R& rhs) {
    using Return = typename __and__<L, R>::Return;
    static_assert(
        std::derived_from<Return, Object>,
        "Bitwise AND operator must return a py::Object subclass.  Check your "
        "specialization of __and__ for this type and ensure the Return type "
        "is derived from py::Object."
    );
    if constexpr (impl::proxy_like<L>) {
        return lhs.value() & rhs;
    } else if constexpr (impl::proxy_like<R>) {
        return lhs & rhs.value();
    } else {
        return ops::and_<Return, L, R>::operator()(lhs, rhs);
    }
}


template <std::derived_from<Object> L, typename R> requires (!__iand__<L, R>::enable)
L& operator&=(L& lhs, const R& rhs) = delete;
template <std::derived_from<Object> L, typename R> requires (__iand__<L, R>::enable)
L& operator&=(L& lhs, const R& rhs) {
    using Return = typename __iand__<L, R>::Return;
    static_assert(
        std::same_as<Return, L&>,
        "In-place bitwise AND operator must return a mutable reference to the left "
        "operand.  Check your specialization of __iand__ for these types and "
        "ensure the Return type is set to the left operand."
    );
    if constexpr (impl::proxy_like<L>) {
        lhs.value() &= rhs;
    } else if constexpr (impl::proxy_like<R>) {
        lhs &= rhs.value();
    } else {
        static_assert(
            impl::python_like<L>,
            "In-place bitwise AND operator requires a Python object."
        );
        ops::iand<Return, L, R>::operator()(lhs, rhs);
    }
    return lhs;
}


template <typename L, typename R>
    requires (
        impl::any_are_python_like<L, R> &&
        !std::ranges::view<R> &&
        !__or__<L, R>::enable
    )
auto operator|(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (
        impl::any_are_python_like<L, R> &&
        !std::ranges::view<R> &&
        __or__<L, R>::enable
    )
auto operator|(const L& lhs, const R& rhs) {
    using Return = typename __or__<L, R>::Return;
    static_assert(
        std::derived_from<Return, Object>,
        "Bitwise OR operator must return a py::Object subclass.  Check your "
        "specialization of __or__ for this type and ensure the Return type is "
        "derived from py::Object."
    );
    if constexpr (impl::proxy_like<L>) {
        return lhs.value() | rhs;
    } else if constexpr (impl::proxy_like<R>) {
        return lhs | rhs.value();
    } else {
        return ops::or_<Return, L, R>::operator()(lhs, rhs);
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


template <std::derived_from<Object> L, typename R> requires (!__ior__<L, R>::enable)
L& operator|=(L& lhs, const R& rhs) = delete;
template <std::derived_from<Object> L, typename R> requires (__ior__<L, R>::enable)
L& operator|=(L& lhs, const R& rhs) {
    using Return = typename __ior__<L, R>::Return;
    static_assert(
        std::same_as<Return, L&>,
        "In-place bitwise OR operator must return a mutable reference to the left "
        "operand.  Check your specialization of __ior__ for these types and "
        "ensure the Return type is set to the left operand."
    );
    if constexpr (impl::proxy_like<L>) {
        lhs.value() |= rhs;
    } else if constexpr (impl::proxy_like<R>) {
        lhs |= rhs.value();
    } else {
        static_assert(
            impl::python_like<L>,
            "In-place bitwise OR operator requires a Python object."
        );
        ops::ior<Return, L, R>::operator()(lhs, rhs);
    }
    return lhs;
}


template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && !__xor__<L, R>::enable)
auto operator^(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && __xor__<L, R>::enable)
auto operator^(const L& lhs, const R& rhs) {
    using Return = typename __xor__<L, R>::Return;
    static_assert(
        std::derived_from<Return, Object>,
        "Bitwise XOR operator must return a py::Object subclass.  Check your "
        "specialization of __xor__ for this type and ensure the Return type "
        "is derived from py::Object."
    );
    if constexpr (impl::proxy_like<L>) {
        return lhs.value() ^ rhs;
    } else if constexpr (impl::proxy_like<R>) {
        return lhs ^ rhs.value();
    } else {
        return ops::xor_<Return, L, R>::operator()(lhs, rhs);
    }
}


template <std::derived_from<Object> L, typename R> requires (!__ixor__<L, R>::enable)
L& operator^=(L& lhs, const R& rhs) = delete;
template <std::derived_from<Object> L, typename R> requires (__ixor__<L, R>::enable)
L& operator^=(L& lhs, const R& rhs) {
    using Return = typename __ixor__<L, R>::Return;
    static_assert(
        std::same_as<Return, L&>,
        "In-place bitwise XOR operator must return a mutable reference to the left "
        "operand.  Check your specialization of __ixor__ for these types and "
        "ensure the Return type is set to the left operand."
    );
    if constexpr (impl::proxy_like<L>) {
        lhs.value() ^= rhs;
    } else if constexpr (impl::proxy_like<R>) {
        lhs ^= rhs.value();
    } else {
        static_assert(
            impl::python_like<L>,
            "In-place bitwise XOR operator requires a Python object."
        );
        ops::ixor<Return, L, R>::operator()(lhs, rhs);
    }
    return lhs;
}


}  // namespace py
}  // namespace bertrand


namespace std {

    template <typename T> requires (bertrand::py::__hash__<T>::enable)
    struct hash<T> {
        static_assert(
            std::same_as<typename bertrand::py::__hash__<T>::Return, size_t>,
            "std::hash<> must return size_t for compatibility with other C++ types.  "
            "Check your specialization of __hash__ for this type and ensure the "
            "Return type is set to size_t."
        );

        static size_t operator()(const T& obj) {
            Py_ssize_t result = PyObject_Hash(obj.ptr());
            if (result == -1 && PyErr_Occurred()) {
                bertrand::py::Exception::from_python();
            }
            return static_cast<size_t>(result);
        }
    };

    #define BERTRAND_STD_EQUAL_TO(cls)                                                  \
        template <>                                                                     \
        struct equal_to<cls> {                                                          \
            static bool operator()(const cls& a, const cls& b) {                        \
                return a.equal(b);                                                      \
            }                                                                           \
        };                                                                              \

    BERTRAND_STD_EQUAL_TO(bertrand::py::Handle)
    BERTRAND_STD_EQUAL_TO(bertrand::py::WeakRef)
    BERTRAND_STD_EQUAL_TO(bertrand::py::Capsule)


    #undef BERTRAND_STD_EQUAL_TO

};


#endif  // BERTRAND_PYTHON_COMMON_OPS_H
