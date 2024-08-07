#ifndef BERTRAND_PYTHON_COMMON_OPS_H
#define BERTRAND_PYTHON_COMMON_OPS_H

#include "declarations.h"
#include "except.h"


namespace py {


namespace impl {

    PyObject* one = (Interpreter::init(), PyLong_FromLong(1));

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

    template <typename L, typename R> requires (__floordiv__<L, R>::enable)
    auto floordiv(const L& lhs, const R& rhs) {
        using Return = typename __floordiv__<L, R>::type;
        static_assert(
            std::derived_from<Return, Object>,
            "Floor division operator must return a py::Object subclass.  Check your "
            "specialization of __floordiv__ for these types and ensure the Return "
            "type is derived from py::Object."
        );
        if constexpr (impl::proxy_like<L>) {
            return floordiv(lhs.value(), rhs);
        } else if constexpr (impl::proxy_like<R>) {
            return floordiv(lhs, rhs.value());
        } else if constexpr (impl::has_call_operator<__floordiv__<L, R>>) {
            return __floordiv__<L, R>{}(lhs, rhs);
        } else {
            PyObject* result = PyNumber_FloorDivide(
                as_object(lhs).ptr(),
                as_object(rhs).ptr()
            );
            if (result == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Return>(result);
        }
    }

    template <std::derived_from<Object> L, typename R> requires (__ifloordiv__<L, R>::enable)
    L& ifloordiv(L& lhs, const R& rhs) {
        using Return = typename __ifloordiv__<L, R>::type;
        static_assert(
            std::same_as<Return, L&>,
            "In-place floor division operator must return a mutable reference to the "
            "left operand.  Check your specialization of __ifloordiv__ for these "
            "types and ensure the Return type is set to the left operand."
        );
        if constexpr (impl::proxy_like<L>) {
            ifloordiv(lhs.value(), rhs);
        } else if constexpr (impl::proxy_like<R>) {
            ifloordiv(lhs, rhs.value());
        } else if constexpr (impl::has_call_operator<__ifloordiv__<L, R>>) {
            __ifloordiv__<L, R>{}(lhs, rhs);
        } else {
            PyObject* result = PyNumber_InPlaceFloorDivide(
                lhs.ptr(),
                as_object(rhs).ptr()
            );
            if (result == nullptr) {
                Exception::from_python();
            } else if (result == lhs.ptr()) {
                Py_DECREF(result);
            } else {
                lhs = reinterpret_steal<Return>(result);
            }
        }
        return lhs;
    }

}


/* Convert an arbitrary C++ value to an equivalent Python object if it isn't one
already. */
template <typename T> requires (__as_object__<std::remove_cvref_t<T>>::enable)
[[nodiscard]] auto as_object(T&& value) {
    using U = std::remove_cvref_t<T>;
    if constexpr (impl::has_call_operator<__as_object__<U>>) {
        return __as_object__<U>{}(std::forward<T>(value));
    } else {
        return typename __as_object__<U>::type(std::forward<T>(value));
    }
}


/* Equivalent to Python `isinstance(obj, base)`. */
template <typename Derived, typename Base> requires (
    std::is_invocable_r_v<bool, __isinstance__<Derived, Base>, const Derived&, const Base&>
)
[[nodiscard]] constexpr bool isinstance(const Derived& obj, const Base& base) {
    return __isinstance__<Derived, Base>{}(obj, base);
}


/* Equivalent to Python `isinstance(obj, base)`, except that base is given as a
template parameter for which __isinstance__ has been specialized. */
template <typename Base, typename Derived> requires (
    std::is_invocable_r_v<bool, __isinstance__<Derived, Base>, const Derived&>
)
[[nodiscard]] constexpr bool isinstance(const Derived& obj) {
    // if constexpr (impl::cpp_like<Derived>) {
    //     return issubclass<Derived, Base>();

    // } else if constexpr (issubclass<Derived, Base>()) {
    //     return obj.ptr() != nullptr;

    // } else if constexpr (impl::has_call_operator<__isinstance__<Derived, Base>>) {
    //     return __isinstance__<Derived, Base>{}(obj);

    // } else if (impl::is_object_exact<Derived>) {
    //     if (obj.ptr() == nullptr) {
    //         return false;
    //     }
    //     // int result = PyObject_IsInstance(obj.ptr(), Base::type.ptr());
    //     int result = PyObject_IsInstance(obj.ptr(), py::Type::of<Base>().ptr());
    //     if (result == -1) {
    //         Exception::from_python();
    //     }
    //     return result;

    // } else {
    //     return false;
    // }

    return __isinstance__<Derived, Base>{}(obj);
}


/* py::Type<T> specifically refers to the Python type of the templated C++ wrapper, so:
 *
 * py::Type<Int>() == PyLong_Type
 * py::Type<Float>() == PyFloat_Type
 * py::Type<Str>() == PyUnicode_Type
 * etc.
 *
 * To represent a generic type, use py::Type<Object>() or py::Type<>().  If that is
 * called with a python object `py::Type(obj)`, then it will detect the object's
 * type at runtime.  If the object is statically typed, then a CTAD constructor will
 * resolve to the right type at compile time, which would just borrow a reference to
 * the right type.
 *
 * In order to get the type of a type (i.e. its metaclass), then you would just pass
 * Type<Type<T>>().
 *
 * TODO: this still runs into conflicts with copy/move constructors, so that would need
 * to be decided.  On the plus side, I would potentially be able to work this into the
 * automated bindings such that py::Type<T> is specialized for every C++ class that is
 * generated.  That way, py::Type<T> would always have the same interface as T itself,
 * and would translate correctly to python type(obj) semantics.
 *
 * Creating a new type at runtime would be possible through
 * py::Type<Object>(name, bases, dict).  This is not available from any other
 * specialization.
 *
 * That would allow you to do something like this: py::Type<py::List>().append, which
 * would be identical to the class-level list.append in Python.
 *
 * -> This is probably too complicated.  What if the templated type represented the
 * bases of the type?  That would potentially be a lot simpler, and you could list as
 * many as you wanted to reflect multiple inheritance.
 *
 * py::Type<Object> (the default) would represent a new subclass of object (i.e. a
 * `class MyClass(Object):` statement in Python).  Creating a dynamic type is then as
 * simple as specifying a supertype and the inheriting class's namespace.
 *
 *  py::Type<py::List<py::Int>> MyList(
 *      "MyList",
 *      {
 *          {"append", [](const py::Int& value) {
 *              // do some stuff
 *              py::List<py::Int>::append(value);
 *          }}
 *      }
 *  );
 */



/* Equivalent to Python `issubclass(obj, base)`. */
template <typename Derived, typename Base> requires (
    std::is_invocable_r_v<bool, __issubclass__<Derived, Base>, const Derived&, const Base&>
)
[[nodiscard]] bool issubclass(const Derived& obj, const Base& base) {
    return __issubclass__<Derived, Base>{}(obj, base);
}


/* Equivalent to Python `issubclass(obj, base)`, except that the base is given as a
template parameter, marking the check as `constexpr`. */
template <typename Base, typename Derived> requires (
    std::is_invocable_r_v<bool, __issubclass__<Derived, Base>, const Derived&>
)
[[nodiscard]] constexpr bool issubclass(const Derived& obj) {
    return __issubclass__<Derived, Base>{}(obj);
}


/* Equivalent to Python `issubclass(obj, base)`, except that both arguments are given
as template parameters, marking the check as `consteval`. */
template <typename Derived, typename Base> requires (
    std::is_invocable_r_v<bool, __issubclass__<Derived, Base>>
)
[[nodiscard]] consteval bool issubclass() {
    return __issubclass__<Derived, Base>{}();
}


/* Equivalent to Python `hasattr(obj, name)` with a static attribute name. */
template <StaticStr name, typename T>
[[nodiscard]] consteval bool hasattr(const T& obj) {
    return __getattr__<T, name>::enable;
}


/* Equivalent to Python `getattr(obj, name)` with a static attribute name. */
template <StaticStr name, typename T> requires (__getattr__<T, name>::enable)
[[nodiscard]] auto getattr(const T& obj) -> __getattr__<T, name>::type {
    if constexpr (impl::has_call_operator<__getattr__<T, name>>) {
        return __getattr__<T, name>{}(obj);
    } else {
        PyObject* result = PyObject_GetAttr(obj.ptr(), impl::TemplateString<name>::ptr);
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<typename __getattr__<T, name>::type>(result);
    }
}


/* Equivalent to Python `getattr(obj, name, default)` with a static attribute name and
default value. */
template <StaticStr name, typename T> requires (__getattr__<T, name>::enable)
[[nodiscard]] auto getattr(
    const T& obj,
    const typename __getattr__<T, name>::type& default_value
) -> __getattr__<T, name>::type {
    if constexpr (impl::has_call_operator<__getattr__<T, name>>) {
        return __getattr__<T, name>{}(obj, default_value);
    } else {
        PyObject* result = PyObject_GetAttr(obj.ptr(), impl::TemplateString<name>::ptr);
        if (result == nullptr) {
            PyErr_Clear();
            return default_value;
        }
        return reinterpret_steal<typename __getattr__<T, name>::type>(result);
    }
}


/* Equivalent to Python `setattr(obj, name, value)` with a static attribute name. */
template <StaticStr name, typename T, typename V> requires (__setattr__<T, name, V>::enable)
void setattr(const T& obj, const V& value) {
    if constexpr (impl::has_call_operator<__setattr__<T, name, V>>) {
        __setattr__<T, name, V>{}(obj, value);
    } else {
        if (PyObject_SetAttr(
            obj.ptr(),
            impl::TemplateString<name>::ptr,
            as_object(value).ptr()
        ) < 0) {
            Exception::from_python();
        }
    }
}


/* Equivalent to Python `delattr(obj, name)` with a static attribute name. */
template <StaticStr name, typename T> requires (__delattr__<T, name>::enable)
void delattr(const T& obj) {
    if constexpr (impl::has_call_operator<__delattr__<T, name>>) {
        __delattr__<T, name>{}(obj);
    } else {
        if (PyObject_DelAttr(obj.ptr(), impl::TemplateString<name>::ptr) < 0) {
            Exception::from_python();
        }
    }
}


/* Equivalent to Python `print(args...)`. */
template <typename... Args>
void print(Args&&... args) {
    try {
        pybind11::print(as_object(std::forward<Args>(args))...);
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
            PyObject* str = PyObject_Repr(as_object(obj).ptr());
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
    return std::hash<T>{}(obj);
}


/* Equivalent to Python `len(obj)`. */
template <typename T> requires (__len__<T>::enable)
[[nodiscard]] size_t len(const T& obj) {
    using Return = typename __len__<T>::type;
    static_assert(
        std::same_as<Return, size_t>,
        "size() operator must return a size_t for compatibility with C++ "
        "containers.  Check your specialization of __len__ for these types "
        "and ensure the Return type is set to size_t."
    );
    if constexpr (impl::has_call_operator<__len__<T>>) {
        return __len__<T>{}(obj);
    } else {
        Py_ssize_t size = PyObject_Length(as_object(obj).ptr());
        if (size < 0) {
            Exception::from_python();
        }
        return size;
    }
}


/* Equivalent to Python `len(obj)`, except that it works on C++ objects implementing a
`size()` method. */
template <typename T> requires (!__len__<T>::enable && impl::has_size<T>)
[[nodiscard]] size_t len(const T& obj) {
    return std::size(obj);
}


/* An alias for `py::len(obj)`, but triggers ADL for constructs that expect a
free-floating size() function. */
template <typename T> requires (__len__<T>::enable)
[[nodiscard]] size_t size(const T& obj) {
    return len(obj);
}


/* An alias for `py::len(obj)`, but triggers ADL for constructs that expect a
free-floating size() function. */
template <typename T> requires (!__len__<T>::enable && impl::has_size<T>)
[[nodiscard]] size_t size(const T& obj) {
    return len(obj);
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
    Interpreter::init();
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
    Interpreter::init();
    try {
        return pybind11::make_iterator(
            std::forward<Iter>(begin),
            std::forward<Sentinel>(end)
        );
    } catch (...) {
        Exception::from_pybind11();
    }
}


/* Begin iteration operator.  Both this and the end iteration operator are
controlled by the __iter__ control struct, whose return type dictates the
iterator's dereference type. */
template <typename Self> requires (__iter__<Self>::enable)
[[nodiscard]] auto begin(const Self& self);


/* Const iteration operator.  Python has no distinction between mutable and
immutable iterators, so this is fundamentally the same as the ordinary begin()
method.  Some libraries assume the existence of this method. */
template <typename Self> requires (__iter__<Self>::enable)
[[nodiscard]] auto cbegin(const Self& self) {
    return begin(self);
}


/* End iteration operator.  This terminates the iteration and is controlled by the
__iter__ control struct. */
template <typename Self> requires (__iter__<Self>::enable)
[[nodiscard]] auto end(const Self& self);


/* Const end operator.  Similar to `cbegin()`, this is identical to `end()`. */
template <typename Self> requires (__iter__<Self>::enable)
[[nodiscard]] auto cend(const Self& self) {
    return end(self);
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
    Interpreter::init();
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
    Interpreter::init();
    try {
        return pybind11::make_iterator(
            std::forward<Iter>(begin),
            std::forward<Sentinel>(end)
        );
    } catch (...) {
        Exception::from_pybind11();
    }
}


/* Reverse iteration operator.  Both this and the reverse end operator are
controlled by the __reversed__ control struct, whose return type dictates the
iterator's dereference type. */
template <typename Self> requires (__reversed__<Self>::enable)
[[nodiscard]] auto rbegin(const Self& self);


/* Const reverse iteration operator.  Python has no distinction between mutable
and immutable iterators, so this is fundamentally the same as the ordinary
rbegin() method.  Some libraries assume the existence of this method. */
template <typename Self> requires (__reversed__<Self>::enable)
[[nodiscard]] auto crbegin(const Self& self) {
    return rbegin(self);
}


/* Reverse end operator.  This terminates the reverse iteration and is controlled
by the __reversed__ control struct. */
template <typename Self> requires (__reversed__<Self>::enable)
[[nodiscard]] auto rend(const Self& self);


/* Const reverse end operator.  Similar to `crbegin()`, this is identical to
`rend()`. */
template <typename Self> requires (__reversed__<Self>::enable)
[[nodiscard]] auto crend(const Self& self) {
    return rend(self);
}


/* Equivalent to Python `abs(obj)` for any object that specializes the __abs__ control
struct. */
template <std::derived_from<Object> Self> requires (__abs__<Self>::enable)
[[nodiscard]] auto abs(const Self& self) {
    using Return = __abs__<Self>::type;
    static_assert(
        std::derived_from<Return, Object>,
        "Absolute value operator must return a py::Object subclass.  Check your "
        "specialization of __abs__ for this type and ensure the Return type is set to "
        "a py::Object subclass."
    );
    if constexpr (impl::proxy_like<Self>) {
        return abs(self.value());
    } else if constexpr (impl::has_call_operator<__abs__<Self>>) {
        return __abs__<Self>{}(self);
    } else {
        PyObject* result = PyNumber_Absolute(as_object(self).ptr());
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Return>(result);
    }
}


/* Equivalent to Python `abs(obj)`, except that it takes a C++ value and applies
std::abs() for identical semantics. */
template <impl::has_abs T> requires (!__abs__<T>::enable && impl::has_abs<T>)
[[nodiscard]] auto abs(const T& value) {
    return std::abs(value);
}


/* Equivalent to Python `base ** exp` (exponentiation). */
template <typename Base, typename Exp> requires (__pow__<Base, Exp>::enable)
[[nodiscard]] auto pow(const Base& base, const Exp& exp) {
    using Return = typename __pow__<Base, Exp>::type;
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
    } else if constexpr (impl::has_call_operator<__pow__<Base, Exp>>) {
        return __pow__<Base, Exp>{}(base, exp);
    } else {
        PyObject* result = PyNumber_Power(
            as_object(base).ptr(),
            as_object(exp).ptr(),
            Py_None
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Return>(result);
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
    using Return = typename __pow__<Base, Exp>::type;
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
        PyObject* result = PyNumber_Power(
            as_object(base).ptr(),
            as_object(exp).ptr(),
            as_object(mod).ptr()
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Return>(result);
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
        return impl::dereference<Self>{}(self);
    }
}


template <std::derived_from<Object> Self> requires (!__invert__<Self>::enable)
auto operator~(const Self& self) = delete;
template <std::derived_from<Object> Self> requires (__invert__<Self>::enable)
auto operator~(const Self& self) {
    using Return = typename __invert__<Self>::type;
    static_assert(
        std::derived_from<Return, Object>,
        "Bitwise NOT operator must return a py::Object subclass.  Check your "
        "specialization of __invert__ for this type and ensure the Return type "
        "is set to a py::Object subclass."
    );
    if constexpr (impl::proxy_like<Self>) {
        return ~self.value();
    } else if constexpr (impl::has_call_operator<__invert__<Self>>) {
        return __invert__<Self>{}(self);
    } else {
        PyObject* result = PyNumber_Invert(as_object(self).ptr());
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Return>(result);
    }
}


template <std::derived_from<Object> Self> requires (!__pos__<Self>::enable)
auto operator+(const Self& self) = delete;
template <std::derived_from<Object> Self> requires (__pos__<Self>::enable)
auto operator+(const Self& self) {
    using Return = typename __pos__<Self>::type;
    static_assert(
        std::derived_from<Return, Object>,
        "Unary positive operator must return a py::Object subclass.  Check "
        "your specialization of __pos__ for this type and ensure the Return "
        "type is set to a py::Object subclass."
    );
    if constexpr (impl::proxy_like<Self>) {
        return +self.value();
    } else if constexpr (impl::has_call_operator<__pos__<Self>>) {
        return __pos__<Self>{}(self);
    } else {
        PyObject* result = PyNumber_Positive(as_object(self).ptr());
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Return>(result);
    }
}


template <std::derived_from<Object> Self> requires (!__neg__<Self>::enable)
auto operator-(const Self& self) = delete;
template <std::derived_from<Object> Self> requires (__neg__<Self>::enable)
auto operator-(const Self& self) {
    using Return = typename __neg__<Self>::type;
    static_assert(
        std::derived_from<Return, Object>,
        "Unary negative operator must return a py::Object subclass.  Check "
        "your specialization of __neg__ for this type and ensure the Return "
        "type is set to a py::Object subclass."
    );
    if constexpr (impl::proxy_like<Self>) {
        return -self.value();
    } else if constexpr (impl::has_call_operator<__neg__<Self>>) {
        return __neg__<Self>{}(self);
    } else {
        PyObject* result = PyNumber_Negative(as_object(self).ptr());
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Return>(result);
    }
}


template <std::derived_from<Object> Self> requires (!__increment__<Self>::enable)
Self& operator++(Self& self) = delete;
template <std::derived_from<Object> Self> requires (__increment__<Self>::enable)
Self& operator++(Self& self) {
    using Return = typename __increment__<Self>::type;
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
        if constexpr (impl::has_call_operator<__increment__<Self>>) {
            __increment__<Self>{}(self);
        } else {
            PyObject* result = PyNumber_InPlaceAdd(self.ptr(), impl::one);
            if (result == nullptr) {
                Exception::from_python();
            } else if (result == self.ptr()) {
                Py_DECREF(result);
            } else {
                self = reinterpret_steal<Return>(result);
            }
        }
    }
    return self;
}


template <std::derived_from<Object> Self> requires (!__increment__<Self>::enable)
Self operator++(Self& self, int) = delete;
template <std::derived_from<Object> Self> requires (__increment__<Self>::enable)
Self operator++(Self& self, int) {
    using Return = typename __increment__<Self>::type;
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
        if constexpr (impl::has_call_operator<__increment__<Self>>) {
            __increment__<Self>{}(self);
        } else {
            PyObject* result = PyNumber_InPlaceAdd(self.ptr(), impl::one);
            if (result == nullptr) {
                Exception::from_python();
            } else if (result == self.ptr()) {
                Py_DECREF(result);
            } else {
                self = reinterpret_steal<Return>(result);
            }
        }
    }
    return copy;
}


template <std::derived_from<Object> Self> requires (!__decrement__<Self>::enable)
Self& operator--(Self& self) = delete;
template <std::derived_from<Object> Self> requires (__decrement__<Self>::enable)
Self& operator--(Self& self) {
    using Return = typename __decrement__<Self>::type;
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
        if constexpr (impl::has_call_operator<__decrement__<Self>>) {
            __decrement__<Self>{}(self);
        } else {
            PyObject* result = PyNumber_InPlaceSubtract(self.ptr(), impl::one);
            if (result == nullptr) {
                Exception::from_python();
            } else if (result == self.ptr()) {
                Py_DECREF(result);
            } else {
                self = reinterpret_steal<Return>(result);
            }
        }
    }
    return self;
}


template <std::derived_from<Object> Self> requires (!__decrement__<Self>::enable)
Self operator--(Self& self, int) = delete;
template <std::derived_from<Object> Self> requires (__decrement__<Self>::enable)
Self operator--(Self& self, int) {
    using Return = typename __decrement__<Self>::type;
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
        if constexpr (impl::has_call_operator<__decrement__<Self>>) {
            __decrement__<Self>{}(self);
        } else {
            PyObject* result = PyNumber_InPlaceSubtract(self.ptr(), impl::one);
            if (result == nullptr) {
                Exception::from_python();
            } else if (result == self.ptr()) {
                Py_DECREF(result);
            } else {
                self = reinterpret_steal<Return>(result);
            }
        }
    }
    return copy;
}


template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && !__lt__<L, R>::enable)
auto operator<(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && __lt__<L, R>::enable)
auto operator<(const L& lhs, const R& rhs) {
    using Return = typename __lt__<L, R>::type;
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
    } else if constexpr (impl::has_call_operator<__lt__<L, R>>) {
        return __lt__<L, R>{}(lhs, rhs);
    } else {
        int result = PyObject_RichCompareBool(
            as_object(lhs).ptr(),
            as_object(rhs).ptr(),
            Py_LT
        );
        if (result == -1) {
            Exception::from_python();
        }
        return result;
    }
}


template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && !__le__<L, R>::enable)
auto operator<=(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && __le__<L, R>::enable)
auto operator<=(const L& lhs, const R& rhs) {
    using Return = typename __le__<L, R>::type;
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
    } else if constexpr (impl::has_call_operator<__le__<L, R>>) {
        return __le__<L, R>{}(lhs, rhs);
    } else {
        int result = PyObject_RichCompareBool(
            as_object(lhs).ptr(),
            as_object(rhs).ptr(),
            Py_LE
        );
        if (result == -1) {
            Exception::from_python();
        }
        return result;
    }
}


template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && !__eq__<L, R>::enable)
auto operator==(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && __eq__<L, R>::enable)
auto operator==(const L& lhs, const R& rhs) {
    using Return = typename __eq__<L, R>::type;
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
    } else if constexpr (impl::has_call_operator<__eq__<L, R>>) {
        return __eq__<L, R>{}(lhs, rhs);
    } else {
        int result = PyObject_RichCompareBool(
            as_object(lhs).ptr(),
            as_object(rhs).ptr(),
            Py_EQ
        );
        if (result == -1) {
            Exception::from_python();
        }
        return result;
    }
}


template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && !__ne__<L, R>::enable)
auto operator!=(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && __ne__<L, R>::enable)
auto operator!=(const L& lhs, const R& rhs) {
    using Return = typename __ne__<L, R>::type;
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
    } else if constexpr (impl::has_call_operator<__ne__<L, R>>) {
        return __ne__<L, R>{}(lhs, rhs);
    } else {
        int result = PyObject_RichCompareBool(
            as_object(lhs).ptr(),
            as_object(rhs).ptr(),
            Py_NE
        );
        if (result == -1) {
            Exception::from_python();
        }
        return result;
    }
}


template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && !__ge__<L, R>::enable)
auto operator>=(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && __ge__<L, R>::enable)
auto operator>=(const L& lhs, const R& rhs) {
    using Return = typename __ge__<L, R>::type;
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
    } else if constexpr (impl::has_call_operator<__ge__<L, R>>) {
        return __ge__<L, R>{}(lhs, rhs);
    } else {
        int result = PyObject_RichCompareBool(
            as_object(lhs).ptr(),
            as_object(rhs).ptr(),
            Py_GE
        );
        if (result == -1) {
            Exception::from_python();
        }
        return result;
    }
}


template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && !__gt__<L, R>::enable)
auto operator>(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && __gt__<L, R>::enable)
auto operator>(const L& lhs, const R& rhs) {
    using Return = typename __gt__<L, R>::type;
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
    } else if constexpr (impl::has_call_operator<__gt__<L, R>>) {
        return __gt__<L, R>{}(lhs, rhs);
    } else {
        int result = PyObject_RichCompareBool(
            as_object(lhs).ptr(),
            as_object(rhs).ptr(),
            Py_GT
        );
        if (result == -1) {
            Exception::from_python();
        }
        return result;
    }
}


template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && !__add__<L, R>::enable)
auto operator+(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && __add__<L, R>::enable)
auto operator+(const L& lhs, const R& rhs) {
    using Return = typename __add__<L, R>::type;
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
    } else if constexpr (impl::has_call_operator<__add__<L, R>>) {
        return __add__<L, R>{}(lhs, rhs);
    } else {
        PyObject* result = PyNumber_Add(
            as_object(lhs).ptr(),
            as_object(rhs).ptr()
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Return>(result);
    }
}


template <std::derived_from<Object> L, typename R> requires (!__iadd__<L, R>::enable)
L& operator+=(L& lhs, const R& rhs) = delete;
template <std::derived_from<Object> L, typename R> requires (__iadd__<L, R>::enable)
L& operator+=(L& lhs, const R& rhs) {
    using Return = typename __iadd__<L, R>::type;
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
        if constexpr (impl::has_call_operator<__iadd__<L, R>>) {
            __iadd__<L, R>{}(lhs, rhs);
        } else {
            PyObject* result = PyNumber_InPlaceAdd(
                lhs.ptr(),
                as_object(rhs).ptr()
            );
            if (result == nullptr) {
                Exception::from_python();
            } else if (result == lhs.ptr()) {
                Py_DECREF(result);
            } else {
                lhs = reinterpret_steal<Return>(result);
            }
        }
    }
    return lhs;
}


template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && !__sub__<L, R>::enable)
auto operator-(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && __sub__<L, R>::enable)
auto operator-(const L& lhs, const R& rhs) {
    using Return = typename __sub__<L, R>::type;
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
    } else if constexpr (impl::has_call_operator<__sub__<L, R>>) {
        return __sub__<L, R>{}(lhs, rhs);
    } else {
        PyObject* result = PyNumber_Subtract(
            as_object(lhs).ptr(),
            as_object(rhs).ptr()
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Return>(result);
    }
}


template <std::derived_from<Object> L, typename R> requires (!__isub__<L, R>::enable)
L& operator-=(L& lhs, const R& rhs) = delete;
template <std::derived_from<Object> L, typename R> requires (__isub__<L, R>::enable)
L& operator-=(L& lhs, const R& rhs) {
    using Return = typename __isub__<L, R>::type;
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
        if constexpr (impl::has_call_operator<__isub__<L, R>>) {
            __isub__<L, R>{}(lhs, rhs);
        } else {
            PyObject* result = PyNumber_InPlaceSubtract(
                lhs.ptr(),
                as_object(rhs).ptr()
            );
            if (result == nullptr) {
                Exception::from_python();
            } else if (result == lhs.ptr()) {
                Py_DECREF(result);
            } else {
                lhs = reinterpret_steal<Return>(result);
            }
        }
    }
    return lhs;
}


template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && !__mul__<L, R>::enable)
auto operator*(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && __mul__<L, R>::enable)
auto operator*(const L& lhs, const R& rhs) {
    using Return = typename __mul__<L, R>::type;
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
        if constexpr (impl::has_call_operator<__mul__<L, R>>) {
            return __mul__<L, R>{}(lhs, rhs);
        } else {
            PyObject* result = PyNumber_Multiply(
                as_object(lhs).ptr(),
                as_object(rhs).ptr()
            );
            if (result == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Return>(result);
        }
    }
}


template <std::derived_from<Object> L, typename R> requires (!__imul__<L, R>::enable)
L& operator*=(L& lhs, const R& rhs) = delete;
template <std::derived_from<Object> L, typename R> requires (__imul__<L, R>::enable)
L& operator*=(L& lhs, const R& rhs) {
    using Return = typename __imul__<L, R>::type;
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
        if constexpr (impl::has_call_operator<__imul__<L, R>>) {
            __imul__<L, R>{}(lhs, rhs);
        } else {
            PyObject* result = PyNumber_InPlaceMultiply(
                lhs.ptr(),
                as_object(rhs).ptr()
            );
            if (result == nullptr) {
                Exception::from_python();
            } else if (result == lhs.ptr()) {
                Py_DECREF(result);
            } else {
                lhs = reinterpret_steal<Return>(result);
            }
        }
    }
    return lhs;
}


template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && !__truediv__<L, R>::enable)
auto operator/(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && __truediv__<L, R>::enable)
auto operator/(const L& lhs, const R& rhs) {
    using Return = typename __truediv__<L, R>::type;
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
    } else if constexpr (impl::has_call_operator<__truediv__<L, R>>) {
        return __truediv__<L, R>{}(lhs, rhs);
    } else {
        PyObject* result = PyNumber_TrueDivide(
            as_object(lhs).ptr(),
            as_object(rhs).ptr()
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Return>(result);
    }
}


template <std::derived_from<Object> L, typename R> requires (!__itruediv__<L, R>::enable)
L& operator/=(L& lhs, const R& rhs) = delete;
template <std::derived_from<Object> L, typename R> requires (__itruediv__<L, R>::enable)
L& operator/=(L& lhs, const R& rhs) {
    using Return = typename __itruediv__<L, R>::type;
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
        if constexpr (impl::has_call_operator<__itruediv__<L, R>>) {
            __itruediv__<L, R>{}(lhs, rhs);
        } else {
            PyObject* result = PyNumber_InPlaceTrueDivide(
                lhs.ptr(),
                as_object(rhs).ptr()
            );
            if (result == nullptr) {
                Exception::from_python();
            } else if (result == lhs.ptr()) {
                Py_DECREF(result);
            } else {
                lhs = reinterpret_steal<Return>(result);
            }
        }
    }
    return lhs;
}


template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && !__mod__<L, R>::enable)
auto operator%(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && __mod__<L, R>::enable)
auto operator%(const L& lhs, const R& rhs) {
    using Return = typename __mod__<L, R>::type;
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
    } else if constexpr (impl::has_call_operator<__mod__<L, R>>) {
        return __mod__<L, R>{}(lhs, rhs);
    } else {
        PyObject* result = PyNumber_Remainder(
            as_object(lhs).ptr(),
            as_object(rhs).ptr()
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Return>(result);
    }
}


template <std::derived_from<Object> L, typename R> requires (!__imod__<L, R>::enable)
L& operator%=(L& lhs, const R& rhs) = delete;
template <std::derived_from<Object> L, typename R> requires (__imod__<L, R>::enable)
L& operator%=(L& lhs, const R& rhs) {
    using Return = typename __imod__<L, R>::type;
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
        if constexpr (impl::has_call_operator<__imod__<L, R>>) {
            __imod__<L, R>{}(lhs, rhs);
        } else {
            PyObject* result = PyNumber_InPlaceRemainder(
                lhs.ptr(),
                as_object(rhs).ptr()
            );
            if (result == nullptr) {
                Exception::from_python();
            } else if (result == lhs.ptr()) {
                Py_DECREF(result);
            } else {
                lhs = reinterpret_steal<Return>(result);
            }
        }
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
    using Return = typename __lshift__<L, R>::type;
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
    } else if constexpr (impl::has_call_operator<__lshift__<L, R>>) {
        return __lshift__<L, R>{}(lhs, rhs);
    } else {
        PyObject* result = PyNumber_Lshift(
            as_object(lhs).ptr(),
            as_object(rhs).ptr()
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Return>(result);
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
    using Return = typename __ilshift__<L, R>::type;
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
        if constexpr (impl::has_call_operator<__ilshift__<L, R>>) {
            __ilshift__<L, R>{}(lhs, rhs);
        } else {
            PyObject* result = PyNumber_InPlaceLshift(
                lhs.ptr(),
                as_object(rhs).ptr()
            );
            if (result == nullptr) {
                Exception::from_python();
            } else if (result == lhs.ptr()) {
                Py_DECREF(result);
            } else {
                lhs = reinterpret_steal<Return>(result);
            }
        }
    }
    return lhs;
}


template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && !__rshift__<L, R>::enable)
auto operator>>(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && __rshift__<L, R>::enable)
auto operator>>(const L& lhs, const R& rhs) {
    using Return = typename __rshift__<L, R>::type;
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
    } else if constexpr (impl::has_call_operator<__rshift__<L, R>>) {
        return __rshift__<L, R>{}(lhs, rhs);
    } else {
        PyObject* result = PyNumber_Rshift(
            as_object(lhs).ptr(),
            as_object(rhs).ptr()
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Return>(result);
    }
}


template <std::derived_from<Object> L, typename R> requires (!__irshift__<L, R>::enable)
L& operator>>=(L& lhs, const R& rhs) = delete;
template <std::derived_from<Object> L, typename R> requires (__irshift__<L, R>::enable)
L& operator>>=(L& lhs, const R& rhs) {
    using Return = typename __irshift__<L, R>::type;
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
        if constexpr (impl::has_call_operator<__irshift__<L, R>>) {
            __irshift__<L, R>{}(lhs, rhs);
        } else {
            PyObject* result = PyNumber_InPlaceRshift(
                lhs.ptr(),
                as_object(rhs).ptr()
            );
            if (result == nullptr) {
                Exception::from_python();
            } else if (result == lhs.ptr()) {
                Py_DECREF(result);
            } else {
                lhs = reinterpret_steal<Return>(result);
            }
        }
    }
    return lhs;
}


template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && !__and__<L, R>::enable)
auto operator&(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && __and__<L, R>::enable)
auto operator&(const L& lhs, const R& rhs) {
    using Return = typename __and__<L, R>::type;
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
    } else if constexpr (impl::has_call_operator<__and__<L, R>>) {
        return __and__<L, R>{}(lhs, rhs);
    } else {
        PyObject* result = PyNumber_And(
            as_object(lhs).ptr(),
            as_object(rhs).ptr()
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Return>(result);
    }
}


template <std::derived_from<Object> L, typename R> requires (!__iand__<L, R>::enable)
L& operator&=(L& lhs, const R& rhs) = delete;
template <std::derived_from<Object> L, typename R> requires (__iand__<L, R>::enable)
L& operator&=(L& lhs, const R& rhs) {
    using Return = typename __iand__<L, R>::type;
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
        if constexpr (impl::has_call_operator<__iand__<L, R>>) {
            __iand__<L, R>{}(lhs, rhs);
        } else {
            PyObject* result = PyNumber_InPlaceAnd(
                lhs.ptr(),
                as_object(rhs).ptr()
            );
            if (result == nullptr) {
                Exception::from_python();
            } else if (result == lhs.ptr()) {
                Py_DECREF(result);
            } else {
                lhs = reinterpret_steal<Return>(result);
            }
        }
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
    using Return = typename __or__<L, R>::type;
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
    } else if constexpr (impl::has_call_operator<__or__<L, R>>) {
        return __or__<L, R>{}(lhs, rhs);
    } else {
        PyObject* result = PyNumber_Or(
            as_object(lhs).ptr(),
            as_object(rhs).ptr()
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Return>(result);
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
    using Return = typename __ior__<L, R>::type;
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
        if constexpr (impl::has_call_operator<__ior__<L, R>>) {
            __ior__<L, R>{}(lhs, rhs);
        } else {
            PyObject* result = PyNumber_InPlaceOr(
                lhs.ptr(),
                as_object(rhs).ptr()
            );
            if (result == nullptr) {
                Exception::from_python();
            } else if (result == lhs.ptr()) {
                Py_DECREF(result);
            } else {
                lhs = reinterpret_steal<Return>(result);
            }
        }
    }
    return lhs;
}


template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && !__xor__<L, R>::enable)
auto operator^(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && __xor__<L, R>::enable)
auto operator^(const L& lhs, const R& rhs) {
    using Return = typename __xor__<L, R>::type;
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
    } else if constexpr (impl::has_call_operator<__xor__<L, R>>) {
        return __xor__<L, R>{}(lhs, rhs);
    } else {
        PyObject* result = PyNumber_Xor(
            as_object(lhs).ptr(),
            as_object(rhs).ptr()
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Return>(result);
    }
}


template <std::derived_from<Object> L, typename R> requires (!__ixor__<L, R>::enable)
L& operator^=(L& lhs, const R& rhs) = delete;
template <std::derived_from<Object> L, typename R> requires (__ixor__<L, R>::enable)
L& operator^=(L& lhs, const R& rhs) {
    using Return = typename __ixor__<L, R>::type;
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
        if constexpr (impl::has_call_operator<__ixor__<L, R>>) {
            __ixor__<L, R>{}(lhs, rhs);
        } else {
            PyObject* result = PyNumber_InPlaceXor(
                lhs.ptr(),
                as_object(rhs).ptr()
            );
            if (result == nullptr) {
                Exception::from_python();
            } else if (result == lhs.ptr()) {
                Py_DECREF(result);
            } else {
                lhs = reinterpret_steal<Return>(result);
            }
        }
    }
    return lhs;
}


}  // namespace py


namespace std {

    template <typename T> requires (py::__hash__<T>::enable)
    struct hash<T> {
        static_assert(
            std::same_as<typename py::__hash__<T>::type, size_t>,
            "std::hash<> must return size_t for compatibility with other C++ types.  "
            "Check your specialization of __hash__ for this type and ensure the "
            "Return type is set to size_t."
        );

        static size_t operator()(const T& obj) {
            if constexpr (py::impl::has_call_operator<py::__hash__<T>>) {
                return py::__hash__<T>{}(obj);
            } else {
                Py_ssize_t result = PyObject_Hash(obj.ptr());
                if (result == -1 && PyErr_Occurred()) {
                    py::Exception::from_python();
                }
                return static_cast<size_t>(result);
            }
        }
    };

    #define BERTRAND_STD_EQUAL_TO(cls)                                                  \
        template <>                                                                     \
        struct equal_to<cls> {                                                          \
            static bool operator()(const cls& a, const cls& b) {                        \
                return a.equal(b);                                                      \
            }                                                                           \
        };                                                                              \

    BERTRAND_STD_EQUAL_TO(py::Handle)
    BERTRAND_STD_EQUAL_TO(py::WeakRef)
    BERTRAND_STD_EQUAL_TO(py::Capsule)


    #undef BERTRAND_STD_EQUAL_TO

};  // namespace std


#endif
