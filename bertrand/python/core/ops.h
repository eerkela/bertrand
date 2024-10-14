#ifndef BERTRAND_PYTHON_CORE_OPS_H
#define BERTRAND_PYTHON_CORE_OPS_H

#include "declarations.h"
#include "object.h"
#include "except.h"


namespace py {


namespace impl {
    static PyObject* one = (Interpreter::init(), PyLong_FromLong(1));  // immortal

    /* Construct a new instance of an inner `Type<Wrapper>::__python__` type using
    Python-based memory allocation and forwarding to the nested type's C++ constructor
    to complete initialization. */
    template <typename Wrapper, typename... Args>
        requires (
            std::derived_from<Wrapper, Object> && has_python<Wrapper> &&
            std::constructible_from<typename Wrapper::__python__, Args...>
        )
    Wrapper construct(Args&&... args) {
        using Self = Wrapper::__python__;
        Type<Wrapper> type;
        PyTypeObject* cls = reinterpret_cast<PyTypeObject*>(ptr(type));
        Self* self = reinterpret_cast<Self*>(cls->tp_alloc(cls, 0));
        if (self == nullptr) {
            Exception::from_python();
        }
        try {
            new (self) Self(std::forward<Args>(args)...);
        } catch (...) {
            cls->tp_free(self);
            throw;
        }
        if (cls->tp_flags & Py_TPFLAGS_HAVE_GC) {
            PyObject_GC_Track(self);
        }
        return reinterpret_steal<Wrapper>(self);
    }

}


/// TODO: remove all remaining static assertions from this file, and update the
/// template constraints for member operators that can't be removed from Object.


/// TODO: implement the extra overloads for Object, BertrandMeta, Type, and Tuple of
/// types/generic objects


/* Does a compile-time check to see if the derived type inherits from the base type.
Ordinarily, this is equivalent to a `std::derived_from<>` concept, except that custom
logic is allowed by defining a zero-argument call operator in a specialization of
`__issubclass__`, and `Interface<T>` specializations are used to handle Python objects
in a way that allows for multiple inheritance. */
template <typename Derived, typename Base>
    requires (
        std::is_invocable_r_v<bool, __issubclass__<Derived, Base>> ||
        !std::is_invocable_v<__issubclass__<Derived, Base>>
    )
[[nodiscard]] constexpr bool issubclass() {
    if constexpr (std::is_invocable_v<__issubclass__<Derived, Base>>) {
        return __issubclass__<Derived, Base>{}();

    } else if constexpr (impl::has_interface<Derived> && impl::has_interface<Base>) {
        return impl::inherits<Interface<Derived>, Interface<Base>>;

    } else if constexpr (impl::has_interface<Derived>) {
        return impl::inherits<Interface<Derived>, Base>;

    } else if constexpr (impl::has_interface<Base>) {
        return impl::inherits<Derived, Interface<Base>>;

    } else {
        return impl::inherits<Derived, Base>;
    }
}


/* Devolves to a compile-time `issubclass<Derived, Base>()` check unless the object is
a dynamic object which may be narrowed to a single type, or a one-argument call
operator is defined in a specialization of `__issubclass__`. */
template <typename Base, typename Derived>
    requires (
        std::is_invocable_r_v<bool, __issubclass__<Derived, Base>, Derived> ||
        !std::is_invocable_v<__issubclass__<Derived, Base>>
    )
[[nodiscard]] constexpr bool issubclass(Derived&& obj) {
    if constexpr (std::is_invocable_v<__issubclass__<Derived, Base>, Derived>) {
        return __issubclass__<Derived, Base>{}(std::forward<Derived>(obj));

    } else if constexpr (impl::has_type<Base> && impl::dynamic<Derived>) {
        return PyType_Check(ptr(obj)) && PyObject_IsSubclass(
            ptr(obj),
            ptr(Type<Base>())
        );

    } else {
        return impl::type_like<Derived> && issubclass<Derived, Base>();
    }
}


/* Equivalent to Python `issubclass(obj, base)`.  This overload must be explicitly
enabled by defining a two-argument call operator in a specialization of
`__issubclass__`.  The derived type must be a single type or a dynamic object which can
be narrowed to a single type, and the base must be type-like, a union of types, or a
dynamic object which can be narrowed to such. */
template <typename Derived, typename Base>
    requires (std::is_invocable_r_v<bool, __issubclass__<Derived, Base>, Derived, Base>)
[[nodiscard]] constexpr bool issubclass(Derived&& obj, Base&& base) {
    return __issubclass__<Derived, Base>{}(
        std::forward<Derived>(obj),
        std::forward<Base>(base)
    );
}


/* Checks if the given object can be safely converted to the specified base type.  This
is automatically called whenever a Python object is narrowed from a parent type to one
of its subclasses. */
template <typename Base, typename Derived>
    requires (
        std::is_invocable_r_v<bool, __isinstance__<Derived, Base>, Derived> ||
        !std::is_invocable_v<__isinstance__<Derived, Base>>
    )
[[nodiscard]] constexpr bool isinstance(Derived&& obj) {
    if constexpr (std::is_invocable_v<__isinstance__<Derived, Base>, Derived>) {
        return __isinstance__<Derived, Base>{}(std::forward<Derived>(obj));

    } else if constexpr (impl::has_type<Base> && impl::dynamic<Derived>) {
        int result = PyObject_IsInstance(
            ptr(obj),
            ptr(Type<Base>())
        );
        if (result < 0) {
            Exception::from_python();
        }
        return result;

    } else {
        return issubclass<Derived, Base>();
    }
}


/* Equivalent to Python `isinstance(obj, base)`.  This overload must be explicitly
enabled by defining a two-argument call operator in a specialization of
`__isinstance__`.  By default, this is only done for bases which are type-like, a union
of types, or a dynamic object which can be narrowed to either of the above. */
template <typename Derived, typename Base>
    requires (std::is_invocable_r_v<bool, __isinstance__<Derived, Base>, Derived, Base>)
[[nodiscard]] constexpr bool isinstance(Derived&& obj, Base&& base) {
    return __isinstance__<Derived, Base>{}(
        std::forward<Derived>(obj),
        std::forward<Base>(base)
    );
}


/* Equivalent to Python `hasattr(obj, name)` with a static attribute name. */
template <typename Self, StaticStr Name>
[[nodiscard]] constexpr bool hasattr() {
    return __getattr__<Self, Name>::enable;
}


/* Equivalent to Python `hasattr(obj, name)` with a static attribute name. */
template <StaticStr Name, typename Self>
[[nodiscard]] constexpr bool hasattr(Self&& obj) {
    return __getattr__<Self, Name>::enable;
}


/* Equivalent to Python `getattr(obj, name)` with a static attribute name. */
template <StaticStr Name, typename Self>
    requires (__getattr__<Self, Name>::enable && (
        std::is_invocable_r_v<
            typename __getattr__<Self, Name>::type,
            __getattr__<Self, Name>,
            Self
        > ||
        std::derived_from<typename __getattr__<Self, Name>::type, Object>
    ))
[[nodiscard]] auto getattr(Self&& self) -> __getattr__<Self, Name>::type {
    if constexpr (impl::has_call_operator<__getattr__<Self, Name>>) {
        return __getattr__<Self, Name>{}(std::forward<Self>(self));

    } else {
        PyObject* name = PyUnicode_FromStringAndSize(Name, Name.size());
        if (name == nullptr) {
            Exception::from_python();
        }
        PyObject* result = PyObject_GetAttr(
            ptr(to_python(std::forward<Self>(self))),
            name
        );
        Py_DECREF(name);
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<typename __getattr__<Self, Name>::type>(result);
    }
}


/* Equivalent to Python `getattr(obj, name, default)` with a static attribute name and
default value. */
template <StaticStr Name, typename Self>
    requires (__getattr__<Self, Name>::enable && (
        std::is_invocable_r_v<
            typename __getattr__<Self, Name>::type,
            __getattr__<Self, Name>,
            Self,
            const typename __getattr__<Self, Name>::type&
        > ||
        std::derived_from<typename __getattr__<Self, Name>::type, Object>
    ))
[[nodiscard]] auto getattr(
    Self&& self,
    const typename __getattr__<Self, Name>::type& default_value
) -> __getattr__<Self, Name>::type {
    if constexpr (impl::has_call_operator<__getattr__<Self, Name>>) {
        return __getattr__<Self, Name>{}(std::forward<Self>(self), default_value);

    } else {
        PyObject* name = PyUnicode_FromStringAndSize(Name, Name.size());
        if (name == nullptr) {
            Exception::from_python();
        }
        PyObject* result = PyObject_GetAttr(
            ptr(to_python(std::forward<Self>(self))),
            name
        );
        Py_DECREF(name);
        if (result == nullptr) {
            PyErr_Clear();
            return default_value;
        }
        return reinterpret_steal<typename __getattr__<Self, Name>::type>(result);
    }
}


/* Equivalent to Python `setattr(obj, name, value)` with a static attribute name. */
template <StaticStr Name, typename Self, typename Value>
    requires (__setattr__<Self, Name, Value>::enable && (
        std::is_void_v<typename __setattr__<Self, Name, Value>::type>
    ))
void setattr(Self&& self, Value&& value) {
    if constexpr (impl::has_call_operator<__setattr__<Self, Name, Value>>) {
        __setattr__<Self, Name, Value>{}(
            std::forward<Self>(self),
            std::forward<Value>(value)
        );

    } else {
        PyObject* name = PyUnicode_FromStringAndSize(Name, Name.size());
        if (name == nullptr) {
            Exception::from_python();
        }
        int rc = PyObject_SetAttr(
            ptr(to_python(std::forward<Self>(self))),
            name,
            ptr(to_python(std::forward<Value>(value)))
        );
        Py_DECREF(name);
        if (rc) {
            Exception::from_python();
        }
    }
}


/* Equivalent to Python `delattr(obj, name)` with a static attribute name. */
template <StaticStr Name, typename Self>
    requires (__delattr__<Self, Name>::enable && (
        std::is_void_v<typename __delattr__<Self, Name>::type>
    ))
void delattr(Self&& self) {
    if constexpr (impl::has_call_operator<__delattr__<Self, Name>>) {
        __delattr__<Self, Name>{}(std::forward<Self>(self));

    } else {
        PyObject* name = PyUnicode_FromStringAndSize(Name, Name.size());
        if (name == nullptr) {
            Exception::from_python();
        }
        int rc = PyObject_DelAttr(
            ptr(to_python(std::forward<Self>(self))),
            name
        );
        Py_DECREF(name);
        if (rc) {
            Exception::from_python();
        }
    }
}


/* Equivalent to Python `repr(obj)`, but returns a std::string and attempts to
represent C++ types using the stream insertion operator (<<) or std::to_string.  If all
else fails, falls back to demangling the result of typeid(obj).name(). */
template <typename T>
[[nodiscard]] decltype(auto) repr(T&& obj) {
    if constexpr (__repr__<T>::enable && impl::has_call_operator<__repr__<T>>) {
        return __repr__<T>{}(std::forward<T>(obj));

    } else if constexpr (__cast__<T>::enable) {
        /// TODO: figure out a way to return a Python string from this.  That will
        /// probably require a forward declaration until after the string type is
        /// defined.
        PyObject* str = PyObject_Repr(
            ptr(to_python(std::forward<T>(obj)))
        );
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

    } else if constexpr (impl::has_to_string<T>) {
        return std::to_string(std::forward<T>(obj));

    } else if constexpr (impl::has_stream_insertion<T>) {
        std::ostringstream stream;
        stream << std::forward<T>(obj);
        return stream.str();

    } else {
        return
            "<" + impl::demangle(typeid(obj).name()) + " at " +
            std::to_string(reinterpret_cast<size_t>(&obj)) + ">";
    }
}


/* Equivalent to Python `hash(obj)`, but delegates to std::hash, which is overloaded
for the relevant Python types.  This promotes hash-not-implemented exceptions into
compile-time equivalents. */
template <impl::hashable T>
[[nodiscard]] size_t hash(T&& obj) {
    return std::hash<T>{}(std::forward<T>(obj));
}


/* Equivalent to Python `len(obj)`. */
template <typename Self>
    requires (__len__<Self>::enable && (
        impl::has_call_operator<__len__<Self>> ||
        (impl::has_cpp<Self> && impl::has_size<impl::cpp_type<Self>>) ||
        (!impl::has_cpp<Self> && std::convertible_to<Py_ssize_t, typename __len__<Self>::type>)
    ))
[[nodiscard]] decltype(auto) len(Self&& obj) {
    if constexpr (impl::has_call_operator<__len__<Self>>) {
        return __len__<Self>{}(std::forward<Self>(obj));

    } else if constexpr (impl::has_cpp<Self>) {
        return std::ranges::size(from_python(std::forward<Self>(obj)));

    } else {
        Py_ssize_t size = PyObject_Length(
            ptr(to_python(std::forward<Self>(obj)))
        );
        if (size < 0) {
            Exception::from_python();
        }
        return size;
    }
}


/* Equivalent to Python `len(obj)`, except that it works on C++ objects implementing a
`size()` method. */
template <typename T> requires (!__len__<T>::enable && impl::has_size<T>)
[[nodiscard]] decltype(auto) len(T&& obj) {
    return std::ranges::size(std::forward<T>(obj));
}


/* An alias for `py::len(obj)`, but triggers ADL for constructs that expect a
free-floating size() function. */
template <typename T>
    requires (__len__<T>::enable && (
        impl::has_call_operator<__len__<T>> ||
        (impl::has_cpp<T> && impl::has_size<impl::cpp_type<T>>) ||
        (!impl::has_cpp<T> && std::convertible_to<Py_ssize_t, typename __len__<T>::type>)
    ))
[[nodiscard]] decltype(auto) size(T&& obj) {
    return len(std::forward<T>(obj));
}


/* An alias for `py::len(obj)`, but triggers ADL for constructs that expect a
free-floating size() function. */
template <typename T> requires (!__len__<T>::enable && impl::has_size<T>)
[[nodiscard]] decltype(auto) size(T&& obj) {
    return len(std::forward<T>(obj));
}


/* Equivalent to Python `abs(obj)` for any object that specializes the __abs__ control
struct. */
template <typename Self>
    requires (__abs__<Self>::enable && (
        impl::has_call_operator<__abs__<Self>> ||
        (impl::has_cpp<Self> && impl::has_abs<impl::cpp_type<Self>>) ||
        (!impl::has_cpp<Self> && std::derived_from<typename __abs__<Self>::type, Object>)
    ))
[[nodiscard]] decltype(auto) abs(Self&& self) {
    if constexpr (impl::has_call_operator<__abs__<Self>>) {
        return __abs__<Self>{}(std::forward<Self>(self));

    } else if (impl::has_cpp<Self>) {
        return std::abs(from_python(std::forward<Self>(self)));

    } else {
        PyObject* result = PyNumber_Absolute(
            ptr(to_python(std::forward<Self>(self)))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<typename __abs__<Self>::type>(result);
    }
}


/* Equivalent to Python `abs(obj)`, except that it takes a C++ value and applies
std::abs() for identical semantics. */
template <impl::has_abs T> requires (!__abs__<T>::enable && impl::has_abs<T>)
[[nodiscard]] decltype(auto) abs(T&& value) {
    return std::abs(std::forward<T>(value));
}


/* Equivalent to Python `base ** exp` (exponentiation). */
template <typename Base, typename Exp>
    requires (__pow__<Base, Exp>::enable && (
        impl::has_call_operator<__pow__<Base, Exp>> ||
        (
            (impl::has_cpp<Base> && impl::has_cpp<Exp>) &&
            impl::has_pow<impl::cpp_type<Base>, impl::cpp_type<Exp>>
        ) || (
            !(impl::has_cpp<Base> && impl::has_cpp<Exp>) &&
            std::derived_from<typename __pow__<Base, Exp>::type, Object>
        )
    ))
[[nodiscard]] decltype(auto) pow(Base&& base, Exp&& exp) {
    if constexpr (impl::has_call_operator<__pow__<Base, Exp>>) {
        return __pow__<Base, Exp>{}(
            std::forward<Base>(base),
            std::forward<Exp>(exp)
        );

    } else if constexpr (
        (impl::cpp<Base> || impl::has_cpp<Base>) &&
        (impl::cpp<Exp> || impl::has_cpp<Exp>)
    ) {
        if constexpr (
            impl::complex_like<impl::cpp_type<Base>> &&
            impl::complex_like<impl::cpp_type<Exp>>
        ) {
            return std::common_type_t<impl::cpp_type<Base>, impl::cpp_type<Exp>>(
                pow(from_python(base).real(), from_python(exp).real()),
                pow(from_python(base).imag(), from_python(exp).imag())
            );
        } else if constexpr (impl::complex_like<impl::cpp_type<Base>>) {
            return Base(
                pow(from_python(base).real(), from_python(exp)),
                pow(from_python(base).real(), from_python(exp))
            );
        } else if constexpr (impl::complex_like<impl::cpp_type<Exp>>) {
            return Exp(
                pow(from_python(base), from_python(exp).real()),
                pow(from_python(base), from_python(exp).imag())
            );
        } else {
            return std::pow(
                from_python(std::forward<Base>(base)),
                from_python(std::forward<Exp>(exp))
            );
        }

    } else {
        PyObject* result = PyNumber_Power(
            ptr(to_python(std::forward<Base>(base))),
            ptr(to_python(std::forward<Exp>(exp))),
            Py_None
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<typename __pow__<Base, Exp>::type>(result);
    }
}


/* Equivalent to Python `pow(base, exp)`, except that it takes a C++ value and applies
std::pow() for identical semantics. */
template <impl::cpp Base, impl::cpp Exp>
    requires (!__pow__<Base, Exp>::enable && (
        impl::complex_like<Base> ||
        impl::complex_like<Exp> ||
        impl::has_pow<Base, Exp>
    ))
[[nodiscard]] decltype(auto) pow(Base&& base, Exp&& exp) {
    if constexpr (impl::complex_like<Base> && impl::complex_like<Exp>) {
        return std::common_type_t<std::remove_cvref_t<Base>, std::remove_cvref_t<Exp>>(
            pow(base.real(), exp.real()),
            pow(base.imag(), exp.imag())
        );
    } else if constexpr (impl::complex_like<Base>) {
        return Base(
            pow(base.real(), exp),
            pow(base.imag(), exp)
        );
    } else if constexpr (impl::complex_like<Exp>) {
        return Exp(
            pow(base, exp.real()),
            pow(base, exp.imag())
        );
    } else {
        return std::pow(base, exp);
    }
}


/* Equivalent to Python `pow(base, exp, mod)`. */
template <impl::int_like Base, impl::int_like Exp, impl::int_like Mod>
    requires (__pow__<Base, Exp>::enable)
decltype(auto) pow(Base&& base, Exp&& exp, Mod&& mod) {
    if constexpr (std::is_invocable_v<__pow__<Base, Exp>, Base, Exp, Mod>) {
        return __pow__<Base, Exp>{}(
            std::forward<Base>(base),
            std::forward<Exp>(exp),
            std::forward<Mod>(mod)
        );

    } else if constexpr (impl::has_cpp<Base> && impl::has_cpp<Exp> && impl::has_cpp<Mod>) {
        return pow(
            from_python(std::forward<Base>(base)),
            from_python(std::forward<Exp>(exp)),
            from_python(std::forward<Mod>(mod))
        );

    } else {
        using Return = typename __pow__<Base, Exp>::type;
        static_assert(
            std::derived_from<Return, Object>,
            "Default pow() operator must return a py::Object subclass.  Check your "
            "specialization of __pow__ for this type and ensure the Return type "
            "derives from py::Object, or define a custom call operator to override "
            "this behavior."
        );
        PyObject* result = PyNumber_Power(
            ptr(to_python(std::forward<Base>(base))),
            ptr(to_python(std::forward<Exp>(exp))),
            ptr(to_python(std::forward<Mod>(mod)))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Return>(result);
    }
}


/* Equivalent to Python `pow(base, exp, mod)`, but works on C++ integers with identical
semantics. */
template <std::integral Base, std::integral Exp, std::integral Mod>
[[nodiscard]] decltype(auto) pow(Base base, Exp exp, Mod mod) {
    std::common_type_t<Base, Exp, Mod> result = 1;
    base = base % mod;
    while (exp > 0) {
        if (exp % 2) {
            result = (result * base) % mod;
        }
        exp >>= 1;
        base = (base * base) % mod;
    }
    return result;
}


template <typename L, typename R>
    requires (__floordiv__<L, R>::enable && (
        impl::has_call_operator<__floordiv__<L, R>> ||
        std::derived_from<typename __floordiv__<L, R>::type, Object>
    ))
decltype(auto) floordiv(L&& lhs, R&& rhs) {
    if constexpr (impl::has_call_operator<__floordiv__<L, R>>) {
        return __floordiv__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));
    } else {
        PyObject* result = PyNumber_FloorDivide(
            ptr(to_python(std::forward<L>(lhs))),
            ptr(to_python(std::forward<R>(lhs)))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<typename __floordiv__<L, R>::type>(result);
    }
}


template <impl::python L, typename R>
    requires (__ifloordiv__<L, R>::enable && (
        impl::has_call_operator<__ifloordiv__<L, R>> ||
        std::same_as<typename __ifloordiv__<L, R>::type, L&>
    ))
decltype(auto) ifloordiv(L& lhs, R&& rhs) {
    if constexpr (impl::has_call_operator<__ifloordiv__<L, R>>) {
        return __ifloordiv__<L, R>{}(lhs, std::forward<R>(rhs));
    } else {
        PyObject* result = PyNumber_InPlaceFloorDivide(
            ptr(lhs),
            ptr(to_python(std::forward<R>(rhs)))
        );
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == ptr(lhs)) {
            Py_DECREF(result);
        } else {
            lhs = reinterpret_steal<typename __ifloordiv__<L, R>::type>(result);
        }
        return lhs;
    }
}


template <impl::python Self> requires (!__invert__<Self>::enable)
decltype(auto) operator~(Self&& self) = delete;
template <impl::python Self>
    requires (__invert__<Self>::enable && (
        impl::has_call_operator<__invert__<Self>> ||
        (impl::has_cpp<Self> && impl::has_invert<impl::cpp_type<Self>>) ||
        (!impl::has_cpp<Self> && std::derived_from<typename __invert__<Self>::type, Object>)
    ))
decltype(auto) operator~(Self&& self) {
    if constexpr (impl::has_call_operator<__invert__<Self>>) {
        return __invert__<Self>{}(std::forward<Self>(self));

    } else if constexpr (impl::has_cpp<Self>) {
        return ~from_python(std::forward<Self>(self));

    } else {
        PyObject* result = PyNumber_Invert(
            ptr(to_python(std::forward<Self>(self)))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<typename __invert__<Self>::type>(result);
    }
}


template <impl::python Self> requires (!__pos__<Self>::enable)
decltype(auto) operator+(Self&& self) = delete;
template <impl::python Self>
    requires (__pos__<Self>::enable && (
        impl::has_call_operator<__pos__<Self>> ||
        (impl::has_cpp<Self> && impl::has_pos<impl::cpp_type<Self>>) ||
        (!impl::has_cpp<Self> && std::derived_from<typename __pos__<Self>::type, Object>)
    ))
decltype(auto) operator+(Self&& self) {
    if constexpr (impl::has_call_operator<__pos__<Self>>) {
        return __pos__<Self>{}(std::forward<Self>(self));

    } else if constexpr (impl::has_cpp<Self>) {
        return +from_python(std::forward<Self>(self));

    } else {
        PyObject* result = PyNumber_Positive(
            ptr(to_python(std::forward<Self>(self)))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<typename __pos__<Self>::type>(result);
    }
}


template <impl::python Self> requires (!__neg__<Self>::enable)
decltype(auto) operator-(Self&& self) = delete;
template <impl::python Self>
    requires (__neg__<Self>::enable && (
        impl::has_call_operator<__neg__<Self>> ||
        (impl::has_cpp<Self> && impl::has_neg<impl::cpp_type<Self>>) ||
        (!impl::has_cpp<Self> && std::derived_from<typename __neg__<Self>::type, Object>)
    ))
decltype(auto) operator-(Self&& self) {
    if constexpr (impl::has_call_operator<__neg__<Self>>) {
        return __neg__<Self>{}(std::forward<Self>(self));

    } else if constexpr (impl::has_cpp<Self>) {
        return -from_python(std::forward<Self>(self));

    } else {
        PyObject* result = PyNumber_Negative(
            ptr(to_python(std::forward<Self>(self)))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<typename __neg__<Self>::type>(result);
    }
}


template <impl::python Self>
decltype(auto) operator++(Self& self, int) = delete;  // post-increment is not valid Python
template <impl::python Self> requires (!__increment__<Self>::enable)
decltype(auto) operator++(Self& self) = delete;
template <impl::python Self>
    requires (__increment__<Self>::enable && (
        impl::has_call_operator<__increment__<Self>> ||
        (impl::has_cpp<Self> && impl::has_preincrement<impl::cpp_type<Self>>) ||
        (!impl::has_cpp<Self> && std::same_as<typename __increment__<Self>::type, Self&>)
    ))
decltype(auto) operator++(Self& self) {
    if constexpr (impl::has_call_operator<__increment__<Self>>) {
        return __increment__<Self>{}(self);

    } else if constexpr (impl::has_cpp<Self>) {
        return ++from_python(self);

    } else {
        PyObject* result = PyNumber_InPlaceAdd(ptr(self), impl::one);
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == ptr(self)) {
            Py_DECREF(result);
        } else {
            self = reinterpret_steal<typename __increment__<Self>::type>(result);
        }
        return self;
    }
}


template <impl::python Self>
decltype(auto) operator--(Self& self, int) = delete;  // post-decrement is not valid Python
template <impl::python Self> requires (!__decrement__<Self>::enable)
decltype(auto) operator--(Self& self) = delete;
template <impl::python Self>
    requires (__decrement__<Self>::enable && (
        impl::has_call_operator<__decrement__<Self>> ||
        (impl::has_cpp<Self> && impl::has_predecrement<impl::cpp_type<Self>>) ||
        (!impl::has_cpp<Self> && std::same_as<typename __decrement__<Self>::type, Self&>)
    ))
decltype(auto) operator--(Self& self) {
    if constexpr (impl::has_call_operator<__decrement__<Self>>) {
        return __decrement__<Self>{}(self);

    } else if constexpr (impl::has_cpp<Self>) {
        return --from_python(self);

    } else {
        PyObject* result = PyNumber_InPlaceSubtract(ptr(self), impl::one);
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == ptr(self)) {
            Py_DECREF(result);
        } else {
            self = reinterpret_steal<typename __decrement__<Self>::type>(result);
        }
        return self;
    }
}


template <typename L, typename R>
    requires ((impl::python<L> || impl::python<R>) && !__lt__<L, R>::enable)
decltype(auto) operator<(L&& lhs, R&& rhs) = delete;
template <typename L, typename R>
    requires (__lt__<L, R>::enable && (
        impl::has_call_operator<__lt__<L, R>> || (
            (impl::has_cpp<L> && impl::has_cpp<R>) &&
            impl::has_lt<impl::cpp_type<L>, impl::cpp_type<R>>
        ) || (
            !(impl::has_cpp<L> && impl::has_cpp<R>) &&
            std::derived_from<typename __lt__<L, R>::type, Object>
        )
    ))
decltype(auto) operator<(L&& lhs, R&& rhs) {
    if constexpr (impl::has_call_operator<__lt__<L, R>>) {
        return __lt__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (impl::has_cpp<L> && impl::has_cpp<R>) {
        return from_python(std::forward<L>(lhs)) < from_python(std::forward<R>(rhs));

    } else {
        PyObject* result = PyObject_RichCompare(
            ptr(to_python(std::forward<L>(lhs))),
            ptr(to_python(std::forward<R>(rhs))),
            Py_LT
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<typename __lt__<L, R>::type>(result);
    }
}


template <typename L, typename R>
    requires ((impl::python<L> || impl::python<R>) && !__le__<L, R>::enable)
decltype(auto) operator<=(L&& lhs, R&& rhs) = delete;
template <typename L, typename R>
    requires (__le__<L, R>::enable && (
        impl::has_call_operator<__le__<L, R>> || (
            (impl::has_cpp<L> && impl::has_cpp<R>) &&
            impl::has_le<impl::cpp_type<L>, impl::cpp_type<R>>
        ) || (
            !(impl::has_cpp<L> && impl::has_cpp<R>) &&
            std::derived_from<typename __le__<L, R>::type, Object>
        )
    ))
decltype(auto) operator<=(L&& lhs, R&& rhs) {
    if constexpr (impl::has_call_operator<__le__<L, R>>) {
        return __le__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (impl::has_cpp<L> && impl::has_cpp<R>) {
        return from_python(std::forward<L>(lhs)) <= from_python(std::forward<R>(rhs));

    } else {
        PyObject* result = PyObject_RichCompare(
            ptr(to_python(std::forward<L>(lhs))),
            ptr(to_python(std::forward<R>(rhs))),
            Py_LE
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<typename __le__<L, R>::type>(result);
    }
}


template <typename L, typename R>
    requires ((impl::python<L> || impl::python<R>) && !__eq__<L, R>::enable)
decltype(auto) operator==(L&& lhs, R&& rhs) = delete;
template <typename L, typename R>
    requires (__eq__<L, R>::enable && (
        impl::has_call_operator<__eq__<L, R>> || (
            (impl::has_cpp<L> && impl::has_cpp<R>) &&
            impl::has_eq<impl::cpp_type<L>, impl::cpp_type<R>>
        ) || (
            !(impl::has_cpp<L> && impl::has_cpp<R>) &&
            std::derived_from<typename __eq__<L, R>::type, Object>
        )
    ))
decltype(auto) operator==(L&& lhs, R&& rhs) {
    if constexpr (impl::has_call_operator<__eq__<L, R>>) {
        return __eq__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (impl::has_cpp<L> && impl::has_cpp<R>) {
        return from_python(std::forward<L>(lhs)) == from_python(std::forward<R>(rhs));

    } else {
        PyObject* result = PyObject_RichCompare(
            ptr(to_python(std::forward<L>(lhs))),
            ptr(to_python(std::forward<R>(rhs))),
            Py_EQ
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<typename __eq__<L, R>::type>(result);
    }
}


template <typename L, typename R>
    requires ((impl::python<L> || impl::python<R>) && !__ne__<L, R>::enable)
decltype(auto) operator!=(L&& lhs, R&& rhs) = delete;
template <typename L, typename R>
    requires (__ne__<L, R>::enable && (
        impl::has_call_operator<__ne__<L, R>> || (
            (impl::has_cpp<L> && impl::has_cpp<R>) &&
            impl::has_ne<impl::cpp_type<L>, impl::cpp_type<R>>
        ) || (
            !(impl::has_cpp<L> && impl::has_cpp<R>) &&
            std::derived_from<typename __ne__<L, R>::type, Object>
        )
    ))
decltype(auto) operator!=(L&& lhs, R&& rhs) {
    if constexpr (impl::has_call_operator<__ne__<L, R>>) {
        return __ne__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (impl::has_cpp<L> && impl::has_cpp<R>) {
        return from_python(std::forward<L>(lhs)) != from_python(std::forward<R>(rhs));

    } else {
        PyObject* result = PyObject_RichCompare(
            ptr(to_python(std::forward<L>(lhs))),
            ptr(to_python(std::forward<R>(rhs))),
            Py_NE
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<typename __ne__<L, R>::type>(result);
    }
}


template <typename L, typename R>
    requires ((impl::python<L> || impl::python<R>) && !__ge__<L, R>::enable)
decltype(auto) operator>=(L&& lhs, R&& rhs) = delete;
template <typename L, typename R>
    requires (__ge__<L, R>::enable && (
        impl::has_call_operator<__ge__<L, R>> || (
            (impl::has_cpp<L> && impl::has_cpp<R>) &&
            impl::has_ge<impl::cpp_type<L>, impl::cpp_type<R>>
        ) || (
            !(impl::has_cpp<L> && impl::has_cpp<R>) &&
            std::derived_from<typename __ge__<L, R>::type, Object>
        )
    ))
decltype(auto) operator>=(L&& lhs, R&& rhs) {
    if constexpr (impl::has_call_operator<__ge__<L, R>>) {
        return __ge__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (impl::has_cpp<L> && impl::has_cpp<R>) {
        return from_python(std::forward<L>(lhs)) >= from_python(std::forward<R>(rhs));

    } else {
        PyObject* result = PyObject_RichCompare(
            ptr(to_python(std::forward<L>(lhs))),
            ptr(to_python(std::forward<R>(rhs))),
            Py_GE
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<typename __ge__<L, R>::type>(result);
    }
}


template <typename L, typename R>
    requires ((impl::python<L> || impl::python<R>) && !__gt__<L, R>::enable)
decltype(auto) operator>(L&& lhs, R&& rhs) = delete;
template <typename L, typename R>
    requires (__gt__<L, R>::enable && (
        impl::has_call_operator<__gt__<L, R>> || (
            (impl::has_cpp<L> && impl::has_cpp<R>) &&
            impl::has_gt<impl::cpp_type<L>, impl::cpp_type<R>>
        ) || (
            !(impl::has_cpp<L> && impl::has_cpp<R>) &&
            std::derived_from<typename __gt__<L, R>::type, Object>
        )
    ))
decltype(auto) operator>(L&& lhs, R&& rhs) {
    if constexpr (impl::has_call_operator<__gt__<L, R>>) {
        return __gt__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (impl::has_cpp<L> && impl::has_cpp<R>) {
        return from_python(std::forward<L>(lhs)) > from_python(std::forward<R>(rhs));

    } else {
        PyObject* result = PyObject_RichCompare(
            ptr(to_python(std::forward<L>(lhs))),
            ptr(to_python(std::forward<R>(rhs))),
            Py_GT
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<typename __gt__<L, R>::type>(result);
    }
}


template <typename L, typename R>
    requires ((impl::python<L> || impl::python<R>) && !__add__<L, R>::enable)
decltype(auto) operator+(L&& lhs, R&& rhs) = delete;
template <typename L, typename R>
    requires (__add__<L, R>::enable && (
        impl::has_call_operator<__add__<L, R>> || (
            (impl::has_cpp<L> && impl::has_cpp<R>) &&
            impl::has_add<impl::cpp_type<L>, impl::cpp_type<R>>
        ) || (
            !(impl::has_cpp<L> && impl::has_cpp<R>) &&
            std::derived_from<typename __add__<L, R>::type, Object>
        )
    ))
decltype(auto) operator+(L&& lhs, R&& rhs) {
    if constexpr (impl::has_call_operator<__add__<L, R>>) {
        return __add__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (impl::has_cpp<L> && impl::has_cpp<R>) {
        return from_python(std::forward<L>(lhs)) + from_python(std::forward<R>(rhs));

    } else {
        PyObject* result = PyNumber_Add(
            ptr(to_python(std::forward<L>(lhs))),
            ptr(to_python(std::forward<R>(rhs)))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<typename __add__<L, R>::type>(result);
    }
}


template <impl::python L, typename R> requires (!__iadd__<L, R>::enable)
decltype(auto) operator+=(L& lhs, R&& rhs) = delete;
template <impl::python L, typename R>
    requires (__iadd__<L, R>::enable && (
        impl::has_call_operator<__iadd__<L, R>> || (
            (impl::has_cpp<L> && impl::has_cpp<R>) &&
            impl::has_iadd<impl::cpp_type<L>, impl::cpp_type<R>>
        ) || (
            !(impl::has_cpp<L> && impl::has_cpp<R>) &&
            std::same_as<typename __iadd__<L, R>::type, L&>
        )
    ))
decltype(auto) operator+=(L& lhs, R&& rhs) {
    if constexpr (impl::has_call_operator<__iadd__<L, R>>) {
        return __iadd__<L, R>{}(lhs, std::forward<R>(rhs));

    } else if constexpr (impl::has_cpp<L> && impl::has_cpp<R>) {
        return from_python(lhs) += from_python(std::forward<R>(rhs));

    } else {
        using Return = std::remove_reference_t<typename __iadd__<L, R>::type>;
        PyObject* result = PyNumber_InPlaceAdd(
            ptr(lhs),
            ptr(to_python(std::forward<R>(rhs)))
        );
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == ptr(lhs)) {
            Py_DECREF(result);
        } else {
            lhs = reinterpret_steal<Return>(result);
        }
        return lhs;
    }
}


template <typename L, typename R>
    requires ((impl::python<L> || impl::python<R>) && !__sub__<L, R>::enable)
decltype(auto) operator-(L&& lhs, R&& rhs) = delete;
template <typename L, typename R>
    requires (__sub__<L, R>::enable && (
        impl::has_call_operator<__sub__<L, R>> || (
            (impl::has_cpp<L> && impl::has_cpp<R>) &&
            impl::has_sub<impl::cpp_type<L>, impl::cpp_type<R>>
        ) || (
            !(impl::has_cpp<L> && impl::has_cpp<R>) &&
            std::derived_from<typename __sub__<L, R>::type, Object>
        )
    ))
decltype(auto) operator-(L&& lhs, R&& rhs) {
    if constexpr (impl::has_call_operator<__sub__<L, R>>) {
        return __sub__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (impl::has_cpp<L> && impl::has_cpp<R>) {
        return from_python(std::forward<L>(lhs)) - from_python(std::forward<R>(rhs));

    } else {
        PyObject* result = PyNumber_Subtract(
            ptr(to_python(std::forward<L>(lhs))),
            ptr(to_python(std::forward<R>(rhs)))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<typename __sub__<L, R>::type>(result);
    }
}


template <impl::python L, typename R> requires (!__isub__<L, R>::enable)
decltype(auto) operator-=(L& lhs, R&& rhs) = delete;
template <impl::python L, typename R>
    requires (__isub__<L, R>::enable && (
        impl::has_call_operator<__isub__<L, R>> || (
            (impl::has_cpp<L> && impl::has_cpp<R>) &&
            impl::has_isub<impl::cpp_type<L>, impl::cpp_type<R>>
        ) || (
            !(impl::has_cpp<L> && impl::has_cpp<R>) &&
            std::same_as<typename __isub__<L, R>::type, L&>
        )
    ))
decltype(auto) operator-=(L& lhs, R&& rhs) {
    if constexpr (impl::has_call_operator<__isub__<L, R>>) {
        return __isub__<L, R>{}(lhs, std::forward<R>(rhs));

    } else if constexpr (impl::has_cpp<L> && impl::has_cpp<R>) {
        return from_python(lhs) -= from_python(std::forward<R>(rhs));

    } else {
        PyObject* result = PyNumber_InPlaceSubtract(
            ptr(lhs),
            ptr(to_python(std::forward<R>(rhs)))
        );
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == ptr(lhs)) {
            Py_DECREF(result);
        } else {
            lhs = reinterpret_steal<typename __isub__<L, R>::type>(result);
        }
        return lhs;
    }
}


template <typename L, typename R>
    requires ((impl::python<L> || impl::python<R>) && !__mul__<L, R>::enable)
decltype(auto) operator*(L&& lhs, R&& rhs) = delete;
template <typename L, typename R>
    requires (__mul__<L, R>::enable && (
        impl::has_call_operator<__mul__<L, R>> || (
            (impl::has_cpp<L> && impl::has_cpp<R>) &&
            impl::has_mul<impl::cpp_type<L>, impl::cpp_type<R>>
        ) || (
            !(impl::has_cpp<L> && impl::has_cpp<R>) &&
            std::derived_from<typename __mul__<L, R>::type, Object>
        )
    ))
decltype(auto) operator*(L&& lhs, R&& rhs) {
    if constexpr (impl::has_call_operator<__mul__<L, R>>) {
        return __mul__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (impl::has_cpp<L> && impl::has_cpp<R>) {
        return from_python(std::forward<L>(lhs)) * from_python(std::forward<R>(rhs));

    } else {
        PyObject* result = PyNumber_Multiply(
            ptr(to_python(std::forward<L>(lhs))),
            ptr(to_python(std::forward<R>(rhs)))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<typename __mul__<L, R>::type>(result);
    }
}


template <impl::python L, typename R> requires (!__imul__<L, R>::enable)
decltype(auto) operator*=(L& lhs, R&& rhs) = delete;
template <impl::python L, typename R>
    requires (__imul__<L, R>::enable && (
        impl::has_call_operator<__imul__<L, R>> || (
            (impl::has_cpp<L> && impl::has_cpp<R>) &&
            impl::has_imul<impl::cpp_type<L>, impl::cpp_type<R>>
        ) || (
            !(impl::has_cpp<L> && impl::has_cpp<R>) &&
            std::same_as<typename __imul__<L, R>::type, L&>
        )
    ))
decltype(auto) operator*=(L& lhs, R&& rhs) {
    if constexpr (impl::has_call_operator<__imul__<L, R>>) {
        return __imul__<L, R>{}(lhs, std::forward<R>(rhs));

    } else if constexpr (impl::has_cpp<L> && impl::has_cpp<R>) {
        return from_python(lhs) *= from_python(std::forward<R>(rhs));

    } else {
        PyObject* result = PyNumber_InPlaceMultiply(
            ptr(lhs),
            ptr(to_python(std::forward<R>(rhs)))
        );
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == ptr(lhs)) {
            Py_DECREF(result);
        } else {
            lhs = reinterpret_steal<typename __imul__<L, R>::type>(result);
        }
        return lhs;
    }
}


template <typename L, typename R>
    requires ((impl::python<L> || impl::python<R>) && !__truediv__<L, R>::enable)
decltype(auto) operator/(L&& lhs, R&& rhs) = delete;
template <typename L, typename R>
    requires (__truediv__<L, R>::enable && (
        impl::has_call_operator<__truediv__<L, R>> || (
            (impl::has_cpp<L> && impl::has_cpp<R>) &&
            impl::has_truediv<impl::cpp_type<L>, impl::cpp_type<R>>
        ) || (
            !(impl::has_cpp<L> && impl::has_cpp<R>) &&
            std::derived_from<typename __truediv__<L, R>::type, Object>
        )
    ))
decltype(auto) operator/(L&& lhs, R&& rhs) {
    if constexpr (impl::has_call_operator<__truediv__<L, R>>) {
        return __truediv__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (impl::has_cpp<L> && impl::has_cpp<R>) {
        return from_python(std::forward<L>(lhs)) / from_python(std::forward<R>(rhs));

    } else {
        PyObject* result = PyNumber_TrueDivide(
            ptr(to_python(std::forward<L>(lhs))),
            ptr(to_python(std::forward<R>(rhs)))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<typename __truediv__<L, R>::type>(result);
    }
}


template <impl::python L, typename R> requires (!__itruediv__<L, R>::enable)
decltype(auto) operator/=(L& lhs, R&& rhs) = delete;
template <impl::python L, typename R>
    requires (__itruediv__<L, R>::enable && (
        impl::has_call_operator<__itruediv__<L, R>> || (
            (impl::has_cpp<L> && impl::has_cpp<R>) &&
            impl::has_itruediv<impl::cpp_type<L>, impl::cpp_type<R>>
        ) || (
            !(impl::has_cpp<L> && impl::has_cpp<R>) &&
            std::same_as<typename __itruediv__<L, R>::type, L&>
        )
    ))
decltype(auto) operator/=(L& lhs, R&& rhs) {
    if constexpr (impl::has_call_operator<__itruediv__<L, R>>) {
        return __itruediv__<L, R>{}(lhs, std::forward<R>(rhs));

    } else if constexpr (impl::has_cpp<L> && impl::has_cpp<R>) {
        return from_python(lhs) /= from_python(std::forward<R>(rhs));

    } else {
        PyObject* result = PyNumber_InPlaceTrueDivide(
            ptr(lhs),
            ptr(to_python(std::forward<R>(rhs)))
        );
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == ptr(lhs)) {
            Py_DECREF(result);
        } else {
            lhs = reinterpret_steal<typename __itruediv__<L, R>::type>(result);
        }
        return lhs;
    }
}


template <typename L, typename R>
    requires ((impl::python<L> || impl::python<R>) && !__mod__<L, R>::enable)
decltype(auto) operator%(L&& lhs, R&& rhs) = delete;
template <typename L, typename R>
    requires (__mod__<L, R>::enable && (
        impl::has_call_operator<__mod__<L, R>> || (
            (impl::has_cpp<L> && impl::has_cpp<R>) &&
            impl::has_mod<impl::cpp_type<L>, impl::cpp_type<R>>
        ) || (
            !(impl::has_cpp<L> && impl::has_cpp<R>) &&
            std::derived_from<typename __mod__<L, R>::type, Object>
        )
    ))
decltype(auto) operator%(L&& lhs, R&& rhs) {
    if constexpr (impl::has_call_operator<__mod__<L, R>>) {
        return __mod__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (impl::has_cpp<L> && impl::has_cpp<R>) {
        return from_python(std::forward<L>(lhs)) % from_python(std::forward<R>(rhs));

    } else {
        PyObject* result = PyNumber_Remainder(
            ptr(to_python(std::forward<L>(lhs))),
            ptr(to_python(std::forward<R>(rhs)))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<typename __mod__<L, R>::type>(result);
    }
}


template <impl::python L, typename R> requires (!__imod__<L, R>::enable)
decltype(auto) operator%=(L& lhs, R&& rhs) = delete;
template <impl::python L, typename R>
    requires (__imod__<L, R>::enable && (
        impl::has_call_operator<__imod__<L, R>> || (
            (impl::has_cpp<L> && impl::has_cpp<R>) &&
            impl::has_imod<impl::cpp_type<L>, impl::cpp_type<R>>
        ) || (
            !(impl::has_cpp<L> && impl::has_cpp<R>) &&
            std::same_as<typename __imod__<L, R>::type, L&>
        )
    ))
decltype(auto) operator%=(L& lhs, R&& rhs) {
    if constexpr (impl::has_call_operator<__imod__<L, R>>) {
        return __imod__<L, R>{}(lhs, std::forward<R>(rhs));

    } else if constexpr (impl::has_cpp<L> && impl::has_cpp<R>) {
        return from_python(lhs) %= from_python(std::forward<R>(rhs));

    } else {
        PyObject* result = PyNumber_InPlaceRemainder(
            ptr(lhs),
            ptr(to_python(std::forward<R>(rhs)))
        );
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == ptr(lhs)) {
            Py_DECREF(result);
        } else {
            lhs = reinterpret_steal<typename __imod__<L, R>::type>(result);
        }
        return lhs;
    }
}


template <typename L, typename R>
    requires ((impl::python<L> || impl::python<R>) && !__lshift__<L, R>::enable)
decltype(auto) operator<<(L&& lhs, R&& rhs) = delete;
template <typename L, typename R>
    requires (__lshift__<L, R>::enable && (
        impl::has_call_operator<__lshift__<L, R>> || (
            (impl::has_cpp<L> && impl::has_cpp<R>) &&
            impl::has_lshift<impl::cpp_type<L>, impl::cpp_type<R>>
        ) || (
            !(impl::has_cpp<L> && impl::has_cpp<R>) &&
            std::derived_from<typename __lshift__<L, R>::type, Object>
        )
    ))
decltype(auto) operator<<(L&& lhs, R&& rhs) {
    if constexpr (impl::has_call_operator<__lshift__<L, R>>) {
        return __lshift__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (impl::has_cpp<L> && impl::has_cpp<R>) {
        return from_python(std::forward<L>(lhs)) << from_python(std::forward<R>(rhs));

    } else {
        PyObject* result = PyNumber_Lshift(
            ptr(to_python(std::forward<L>(lhs))),
            ptr(to_python(std::forward<R>(rhs)))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<typename __lshift__<L, R>::type>(result);
    }
}


template <impl::python L, typename R> requires (!__ilshift__<L, R>::enable)
decltype(auto) operator<<=(L& lhs, R&& rhs) = delete;
template <impl::python L, typename R>
    requires (__ilshift__<L, R>::enable && (
        impl::has_call_operator<__ilshift__<L, R>> || (
            (impl::has_cpp<L> && impl::has_cpp<R>) &&
            impl::has_ilshift<impl::cpp_type<L>, impl::cpp_type<R>>
        ) || (
            !(impl::has_cpp<L> && impl::has_cpp<R>) &&
            std::same_as<typename __ilshift__<L, R>::type, L&>
        )
    ))
decltype(auto) operator<<=(L& lhs, R&& rhs) {
    if constexpr (impl::has_call_operator<__ilshift__<L, R>>) {
        return __ilshift__<L, R>{}(lhs, std::forward<R>(rhs));

    } else if constexpr (impl::has_cpp<L> && impl::has_cpp<R>) {
        return from_python(lhs) <<= from_python(std::forward<R>(rhs));

    } else {
        PyObject* result = PyNumber_InPlaceLshift(
            ptr(lhs),
            ptr(to_python(std::forward<R>(rhs)))
        );
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == ptr(lhs)) {
            Py_DECREF(result);
        } else {
            lhs = reinterpret_steal<typename __ilshift__<L, R>::type>(result);
        }
        return lhs;
    }
}


template <typename L, typename R>
    requires ((impl::python<L> || impl::python<R>) && !__rshift__<L, R>::enable)
decltype(auto) operator>>(L&& lhs, R&& rhs) = delete;
template <typename L, typename R>
    requires (__rshift__<L, R>::enable && (
        impl::has_call_operator<__rshift__<L, R>> || (
            (impl::has_cpp<L> && impl::has_cpp<R>) &&
            impl::has_rshift<impl::cpp_type<L>, impl::cpp_type<R>>
        ) || (
            !(impl::has_cpp<L> && impl::has_cpp<R>) &&
            std::derived_from<typename __rshift__<L, R>::type, Object>
        )
    ))
decltype(auto) operator>>(L&& lhs, R&& rhs) {
    if constexpr (impl::has_call_operator<__rshift__<L, R>>) {
        return __rshift__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (impl::has_cpp<L> && impl::has_cpp<R>) {
        return from_python(std::forward<L>(lhs)) >> from_python(std::forward<R>(rhs));

    } else {
        PyObject* result = PyNumber_Rshift(
            ptr(to_python(std::forward<L>(lhs))),
            ptr(to_python(std::forward<R>(rhs)))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<typename __rshift__<L, R>::type>(result);
    }
}


template <impl::python L, typename R> requires (!__irshift__<L, R>::enable)
decltype(auto) operator>>=(L& lhs, R&& rhs) = delete;
template <impl::python L, typename R>
    requires (__irshift__<L, R>::enable && (
        impl::has_call_operator<__irshift__<L, R>> || (
            (impl::has_cpp<L> && impl::has_cpp<R>) &&
            impl::has_irshift<impl::cpp_type<L>, impl::cpp_type<R>>
        ) || (
            !(impl::has_cpp<L> && impl::has_cpp<R>) &&
            std::same_as<typename __irshift__<L, R>::type, L&>
        )
    ))
decltype(auto) operator>>=(L& lhs, R&& rhs) {
    if constexpr (impl::has_call_operator<__irshift__<L, R>>) {
        return __irshift__<L, R>{}(lhs, std::forward<R>(rhs));

    } else if constexpr (impl::has_cpp<L> && impl::has_cpp<R>) {
        return from_python(lhs) >>= from_python(std::forward<R>(rhs));

    } else {
        PyObject* result = PyNumber_InPlaceRshift(
            ptr(lhs),
            ptr(to_python(std::forward<R>(rhs)))
        );
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == ptr(lhs)) {
            Py_DECREF(result);
        } else {
            lhs = reinterpret_steal<typename __irshift__<L, R>::type>(result);
        }
        return lhs;
    }
}


template <typename L, typename R>
    requires ((impl::python<L> || impl::python<R>) && !__and__<L, R>::enable)
decltype(auto) operator&(L&& lhs, R&& rhs) = delete;
template <typename L, typename R>
    requires (__and__<L, R>::enable && (
        impl::has_call_operator<__and__<L, R>> || (
            (impl::has_cpp<L> && impl::has_cpp<R>) &&
            impl::has_and<impl::cpp_type<L>, impl::cpp_type<R>>
        ) || (
            !(impl::has_cpp<L> && impl::has_cpp<R>) &&
            std::derived_from<typename __and__<L, R>::type, Object>
        )
    ))
decltype(auto) operator&(L&& lhs, R&& rhs) {
    if constexpr (impl::has_call_operator<__and__<L, R>>) {
        return __and__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (impl::has_cpp<L> && impl::has_cpp<R>) {
        return from_python(std::forward<L>(lhs)) & from_python(std::forward<R>(rhs));

    } else {
        PyObject* result = PyNumber_And(
            ptr(to_python(std::forward<L>(lhs))),
            ptr(to_python(std::forward<R>(rhs)))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<typename __and__<L, R>::type>(result);
    }
}


template <impl::python L, typename R> requires (!__iand__<L, R>::enable)
decltype(auto) operator&=(L& lhs, R&& rhs) = delete;
template <impl::python L, typename R>
    requires (__iand__<L, R>::enable && (
        impl::has_call_operator<__iand__<L, R>> || (
            (impl::has_cpp<L> && impl::has_cpp<R>) &&
            impl::has_iand<impl::cpp_type<L>, impl::cpp_type<R>>
        ) || (
            !(impl::has_cpp<L> && impl::has_cpp<R>) &&
            std::same_as<typename __iand__<L, R>::type, L&>
        )
    ))
decltype(auto) operator&=(L& lhs, R&& rhs) {
    if constexpr (impl::has_call_operator<__iand__<L, R>>) {
        return __iand__<L, R>{}(lhs, std::forward<R>(rhs));

    } else if constexpr (impl::has_cpp<L> && impl::has_cpp<R>) {
        return from_python(lhs) &= from_python(std::forward<R>(rhs));

    } else {
        PyObject* result = PyNumber_InPlaceAnd(
            ptr(lhs),
            ptr(to_python(std::forward<R>(rhs)))
        );
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == ptr(lhs)) {
            Py_DECREF(result);
        } else {
            lhs = reinterpret_steal<typename __iand__<L, R>::type>(result);
        }
        return lhs;
    }
}


template <typename L, typename R>
    requires ((impl::python<L> || impl::python<R>) && !__or__<L, R>::enable)
decltype(auto) operator|(L&& lhs, R&& rhs) = delete;
template <typename L, typename R>
    requires (__or__<L, R>::enable && (
        impl::has_call_operator<__or__<L, R>> || (
            (impl::has_cpp<L> && impl::has_cpp<R>) &&
            impl::has_or<impl::cpp_type<L>, impl::cpp_type<R>>
        ) || (
            !(impl::has_cpp<L> && impl::has_cpp<R>) &&
            std::derived_from<typename __or__<L, R>::type, Object>
        )
    ))
decltype(auto) operator|(L&& lhs, R&& rhs) {
    if constexpr (impl::has_call_operator<__or__<L, R>>) {
        return __or__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (impl::has_cpp<L> && impl::has_cpp<R>) {
        return from_python(std::forward<L>(lhs)) | from_python(std::forward<R>(rhs));

    } else {
        PyObject* result = PyNumber_Or(
            ptr(to_python(std::forward<L>(lhs))),
            ptr(to_python(std::forward<R>(rhs)))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<typename __or__<L, R>::type>(result);
    }
}


template <impl::python L, typename R> requires (!__ior__<L, R>::enable)
decltype(auto) operator|=(L& lhs, R&& rhs) = delete;
template <impl::python L, typename R>
    requires (__ior__<L, R>::enable && (
        impl::has_call_operator<__ior__<L, R>> || (
            (impl::has_cpp<L> && impl::has_cpp<R>) &&
            impl::has_ior<impl::cpp_type<L>, impl::cpp_type<R>>
        ) || (
            !(impl::has_cpp<L> && impl::has_cpp<R>) &&
            std::same_as<typename __ior__<L, R>::type, L&>
        )
    ))
decltype(auto) operator|=(L& lhs, R&& rhs) {
    if constexpr (impl::has_call_operator<__ior__<L, R>>) {
        return __ior__<L, R>{}(lhs, std::forward<R>(rhs));

    } else if constexpr (impl::has_cpp<L> && impl::has_cpp<R>) {
        return from_python(lhs) |= from_python(std::forward<R>(rhs));

    } else {
        PyObject* result = PyNumber_InPlaceOr(
            ptr(lhs),
            ptr(to_python(std::forward<R>(rhs)))
        );
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == ptr(lhs)) {
            Py_DECREF(result);
        } else {
            lhs = reinterpret_steal<typename __ior__<L, R>::type>(result);
        }
        return lhs;
    }
}


template <typename L, typename R>
    requires ((impl::python<L> || impl::python<R>) && !__xor__<L, R>::enable)
decltype(auto) operator^(L&& lhs, R&& rhs) = delete;
template <typename L, typename R>
    requires (__xor__<L, R>::enable && (
        impl::has_call_operator<__xor__<L, R>> || (
            (impl::has_cpp<L> && impl::has_cpp<R>) &&
            impl::has_xor<impl::cpp_type<L>, impl::cpp_type<R>>
        ) || (
            !(impl::has_cpp<L> && impl::has_cpp<R>) &&
            std::derived_from<typename __xor__<L, R>::type, Object>
        )
    ))
decltype(auto) operator^(L&& lhs, R&& rhs) {
    if constexpr (impl::has_call_operator<__xor__<L, R>>) {
        return __xor__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (impl::has_cpp<L> && impl::has_cpp<R>) {
        return from_python(std::forward<L>(lhs)) ^ from_python(std::forward<R>(rhs));

    } else {
        PyObject* result = PyNumber_Xor(
            ptr(to_python(std::forward<L>(lhs))),
            ptr(to_python(std::forward<R>(rhs)))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<typename __xor__<L, R>::type>(result);
    }
}


template <impl::python L, typename R> requires (!__ixor__<L, R>::enable)
decltype(auto) operator^=(L& lhs, R&& rhs) = delete;
template <impl::python L, typename R>
    requires (__ixor__<L, R>::enable && (
        impl::has_call_operator<__ixor__<L, R>> || (
            (impl::has_cpp<L> && impl::has_cpp<R>) &&
            impl::has_ixor<impl::cpp_type<L>, impl::cpp_type<R>>
        ) || (
            !(impl::has_cpp<L> && impl::has_cpp<R>) &&
            std::same_as<typename __ixor__<L, R>::type, L&>
        )
    ))
decltype(auto) operator^=(L& lhs, R&& rhs) {
    if constexpr (impl::has_call_operator<__ixor__<L, R>>) {
        return __ixor__<L, R>{}(lhs, std::forward<R>(rhs));

    } else if constexpr (impl::has_cpp<L> && impl::has_cpp<R>) {
        return from_python(lhs) ^= from_python(std::forward<R>(rhs));

    } else {
        PyObject* result = PyNumber_InPlaceXor(
            ptr(lhs),
            ptr(to_python(std::forward<R>(rhs)))
        );
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == ptr(lhs)) {
            Py_DECREF(result);
        } else {
            lhs = reinterpret_steal<typename __ixor__<L, R>::type>(result);
        }
        return lhs;
    }
}


//////////////////////
////    OBJECT    ////
//////////////////////


/// TODO: all of these forward declarations should be filled in at the top level
/// core.h file instead of here.


namespace impl {

    template <cpp T>
        requires (
            has_python<T> &&
            has_cpp<python_type<T>> &&
            is<T, cpp_type<python_type<T>>>
        )
    [[nodiscard]] auto wrap(T& obj) -> python_type<T> {
        using Wrapper = __cast__<T>::type;
        using Variant = decltype(ptr(std::declval<Wrapper>())->m_cpp);
        Type<Wrapper> type;
        PyTypeObject* type_ptr = ptr(type);
        PyObject* self = type_ptr->tp_alloc(type_ptr, 0);
        if (self == nullptr) {
            Exception::from_python();
        }
        new (&reinterpret_cast<typename Wrapper::__python__*>(self)->m_cpp) Variant(&obj);
        return reinterpret_steal<Wrapper>(self);
    }


    template <cpp T>
        requires (
            has_python<T> &&
            has_cpp<python_type<T>> &&
            is<T, cpp_type<python_type<T>>>
        )
    [[nodiscard]] auto wrap(const T& obj) -> python_type<T> {
        using Wrapper = __cast__<T>::type;
        using Variant = decltype(ptr(std::declval<Wrapper>())->m_cpp);
        Type<Wrapper> type;
        PyTypeObject* type_ptr = ptr(type);
        PyObject* self = type_ptr->tp_alloc(type_ptr, 0);
        if (self == nullptr) {
            Exception::from_python();
        }
        new (&reinterpret_cast<typename Wrapper::__python__*>(self)->m_cpp) Variant(&obj);
        return reinterpret_steal<Wrapper>(self);
    }


    template <python T> requires (has_cpp<T>)
    [[nodiscard]] auto& unwrap(T& obj) {
        if constexpr (impl::has_cpp<T>) {
            using CppType = impl::cpp_type<T>;
            return std::visit(
                Object::Visitor{
                    [](CppType& cpp) -> CppType& { return cpp; },
                    [](CppType* cpp) -> CppType& { return *cpp; },
                    [&obj](const CppType* cpp) -> CppType& {
                        throw TypeError(
                            "requested a mutable reference to const object: " +
                            repr(obj)
                        );
                    }
                },
                obj->m_cpp
            );
        } else {
            return obj;
        }
    }


    template <python T> requires (has_cpp<T>)
    [[nodiscard]] const auto& unwrap(const T& obj) {
        if constexpr (impl::has_cpp<T>) {
            using CppType = impl::cpp_type<T>;
            return std::visit(
                Object::Visitor{
                    [](const CppType& cpp) -> const CppType& { return cpp; },
                    [](const CppType* cpp) -> const CppType& { return *cpp; }
                },
                obj->m_cpp
            );
        } else {
            return obj;
        }
    }

}


template <typename Self>
[[nodiscard]] Object::operator bool(this Self&& self) {
    if constexpr (
        impl::has_cpp<Self> &&
        impl::has_operator_bool<impl::cpp_type<Self>>
    ) {
        return static_cast<bool>(from_python(std::forward<Self>(self)));
    } else {
        int result = PyObject_IsTrue(ptr(self));
        if (result == -1) {
            Exception::from_python();
        }
        return result;   
    }
}


template <typename Self, typename Key> requires (__contains__<Self, Key>::enable)
[[nodiscard]] bool Object::contains(this Self&& self, Key&& key) {
    using Return = typename __contains__<Self, Key>::type;
    static_assert(
        std::same_as<Return, bool>,
        "contains() operator must return a boolean value.  Check your "
        "specialization of __contains__ for these types and ensure the Return "
        "type is set to bool."
    );
    if constexpr (impl::has_call_operator<__contains__<Self, Key>>) {
        return __contains__<Self, Key>{}(
            std::forward<Self>(self),
            std::forward<Key>(key)
        );

    } else if constexpr (impl::has_cpp<Self>) {
        static_assert(
            impl::has_contains<impl::cpp_type<Self>, impl::cpp_type<Key>>,
            "__contains__<Self, Key> is enabled for operands whose C++ "
            "representations have no viable overload for `Self.contains(Key)`"
        );
        return from_python(std::forward<Self>(self)).contains(
            from_python(std::forward<Key>(key))
        );

    } else {
        int result = PySequence_Contains(
            ptr(self),
            ptr(to_python(std::forward<Key>(key)))
        );
        if (result == -1) {
            Exception::from_python();
        }
        return result;
    }
}


template <typename T, impl::is<Object> Base>
constexpr bool __isinstance__<T, Base>::operator()(T&& obj, Base&& cls) {
    if constexpr (impl::python<T>) {
        int result = PyObject_IsInstance(
            ptr(to_python(std::forward<T>(obj))),
            ptr(cls)
        );
        if (result < 0) {
            Exception::from_python();
        }
        return result;
    } else {
        return false;
    }
}


template <typename T, impl::is<Object> Base>
bool __issubclass__<T, Base>::operator()(T&& obj, Base&& cls) {
    int result = PyObject_IsSubclass(
        ptr(to_python(std::forward<T>(obj))),
        ptr(cls)
    );
    if (result == -1) {
        Exception::from_python();
    }
    return result;
}


template <impl::inherits<Object> From, impl::inherits<From> To>
    requires (!impl::is<From, To>)
auto __cast__<From, To>::operator()(From&& from) {
    if (isinstance<To>(from)) {
        if constexpr (std::is_lvalue_reference_v<From>) {
            return reinterpret_borrow<To>(ptr(from));
        } else {
            return reinterpret_steal<To>(release(from));
        }
    } else {
        /// TODO: Type<From> and Type<To> must apply std::remove_cvref_t<>?  Maybe that
        /// can be rolled into the Type<> class itself?
        /// -> The only way this can be handled universally is if these forward
        /// declarations are filled after type.h is included
        throw TypeError(
            "cannot convert Python object from type '" + repr(Type<From>()) +
            "' to type '" + repr(Type<To>()) + "'"
        );
    }
}


template <impl::inherits<Object> From, impl::cpp To>
    requires (__cast__<To>::enable && std::integral<To>)
To __explicit_cast__<From, To>::operator()(From&& from) {
    long long result = PyLong_AsLongLong(ptr(from));
    if (result == -1 && PyErr_Occurred()) {
        Exception::from_python();
    } else if (
        result < std::numeric_limits<To>::min() ||
        result > std::numeric_limits<To>::max()
    ) {
        throw OverflowError(
            "integer out of range for " + impl::demangle(typeid(To).name()) +
            ": " + std::to_string(result)
        );
    }
    return result;
}


template <impl::inherits<Object> From, impl::cpp To>
    requires (__cast__<To>::enable && std::floating_point<To>)
To __explicit_cast__<From, To>::operator()(From&& from) {
    double result = PyFloat_AsDouble(ptr(from));
    if (result == -1.0 && PyErr_Occurred()) {
        Exception::from_python();
    }
    return result;
}


template <impl::inherits<Object> From, typename Float>
auto __explicit_cast__<From, std::complex<Float>>::operator()(From&& from) {
    Py_complex result = PyComplex_AsCComplex(ptr(from));
    if (result.real == -1.0 && PyErr_Occurred()) {
        Exception::from_python();
    }
    return std::complex<Float>(result.real, result.imag);
}


/// TODO: this same logic should carry over for strings, bytes, and byte arrays to
/// allow conversion to any kind of basic string type.


template <impl::inherits<Object> From, typename Char>
auto __explicit_cast__<From, std::basic_string<Char>>::operator()(From&& from) {
    PyObject* str = PyObject_Str(reinterpret_cast<PyObject*>(ptr(from)));
    if (str == nullptr) {
        Exception::from_python();
    }
    if constexpr (sizeof(Char) == 1) {
        Py_ssize_t size;
        const char* data = PyUnicode_AsUTF8AndSize(str, &size);
        if (data == nullptr) {
            Py_DECREF(str);
            Exception::from_python();
        }
        std::basic_string<Char> result(data, size);
        Py_DECREF(str);
        return result;

    } else if constexpr (sizeof(Char) == 2) {
        PyObject* bytes = PyUnicode_AsUTF16String(str);
        Py_DECREF(str);
        if (bytes == nullptr) {
            Exception::from_python();
        }
        std::basic_string<Char> result(
            reinterpret_cast<const Char*>(PyBytes_AsString(bytes)) + 1,  // skip BOM marker
            (PyBytes_GET_SIZE(bytes) / sizeof(Char)) - 1
        );
        Py_DECREF(bytes);
        return result;

    } else if constexpr (sizeof(Char) == 4) {
        PyObject* bytes = PyUnicode_AsUTF32String(str);
        Py_DECREF(str);
        if (bytes == nullptr) {
            Exception::from_python();
        }
        std::basic_string<Char> result(
            reinterpret_cast<const Char*>(PyBytes_AsString(bytes)) + 1,  // skip BOM marker
            (PyBytes_GET_SIZE(bytes) / sizeof(Char)) - 1
        );
        Py_DECREF(bytes);
        return result;

    } else {
        static_assert(
            sizeof(Char) == 1 || sizeof(Char) == 2 || sizeof(Char) == 4,
            "unsupported character size for string conversion"
        );
    }
}


template <std::derived_from<std::ostream> Stream, impl::inherits<Object> Self>
Stream& __lshift__<Stream, Self>::operator()(Stream& stream, Self self) {
    PyObject* repr = PyObject_Str(ptr(self));
    if (repr == nullptr) {
        Exception::from_python();
    }
    Py_ssize_t size;
    const char* data = PyUnicode_AsUTF8AndSize(repr, &size);
    if (data == nullptr) {
        Py_DECREF(repr);
        Exception::from_python();
    }
    stream.write(data, size);
    Py_DECREF(repr);
    return stream;
}


////////////////////
////    CODE    ////
////////////////////


template <std::convertible_to<std::string> Source>
auto __cast__<Source, Code>::operator()(const std::string& source) {
    std::string line;
    std::string parsed;
    std::istringstream stream(source);
    size_t min_indent = std::numeric_limits<size_t>::max();

    // find minimum indentation
    while (std::getline(stream, line)) {
        if (line.empty()) {
            continue;
        }
        size_t indent = line.find_first_not_of(" \t");
        if (indent != std::string::npos) {
            min_indent = std::min(min_indent, indent);
        }
    }

    // dedent if necessary
    if (min_indent != std::numeric_limits<size_t>::max()) {
        std::string temp;
        std::istringstream stream2(source);
        while (std::getline(stream2, line)) {
            if (line.empty() || line.find_first_not_of(" \t") == std::string::npos) {
                temp += '\n';
            } else {
                temp += line.substr(min_indent) + '\n';
            }
        }
        parsed = temp;
    } else {
        parsed = source;
    }

    PyObject* result = Py_CompileString(
        parsed.c_str(),
        "<embedded Python script>",
        Py_file_input
    );
    if (result == nullptr) {
        Exception::from_python();
    }
    return reinterpret_steal<Code>(result);
}


/* Parse and compile a source file into a Python code object. */
[[nodiscard]] inline Code Interface<Code>::compile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw FileNotFoundError(std::string("'") + path + "'");
    }
    std::istreambuf_iterator<char> begin(file), end;
    PyObject* result = Py_CompileString(
        std::string(begin, end).c_str(),
        path.c_str(),
        Py_file_input
    );
    if (result == nullptr) {
        Exception::from_python();
    }
    return reinterpret_steal<Code>(result);
}


/////////////////////
////    FRAME    ////
/////////////////////


inline auto __init__<Frame>::operator()() {
    PyFrameObject* frame = PyEval_GetFrame();
    if (frame == nullptr) {
        throw RuntimeError("no frame is currently executing");
    }
    return reinterpret_borrow<Frame>(reinterpret_cast<PyObject*>(frame));
}


template <std::convertible_to<int> T>
Frame __init__<Frame, T>::operator()(int skip) {
    PyFrameObject* frame = reinterpret_cast<PyFrameObject*>(
        Py_XNewRef(PyEval_GetFrame())
    );
    if (frame == nullptr) {
        throw RuntimeError("no frame is currently executing");
    }

    // negative indexing offsets from the most recent frame
    if (skip < 0) {
        for (int i = 0; i > skip; --i) {
            PyFrameObject* temp = PyFrame_GetBack(frame);
            if (temp == nullptr) {
                return reinterpret_steal<Frame>(reinterpret_cast<PyObject*>(frame));
            }
            Py_DECREF(frame);
            frame = temp;
        }
        return reinterpret_steal<Frame>(reinterpret_cast<PyObject*>(frame));
    }

    // positive indexing counts from the least recent frame
    std::vector<Frame> frames;
    while (frame != nullptr) {
        frames.push_back(reinterpret_steal<Frame>(
            reinterpret_cast<PyObject*>(frame))
        );
        frame = PyFrame_GetBack(frame);
    }
    if (skip >= frames.size()) {
        return frames.front();
    }
    return frames[skip];
}


template <impl::is<Frame> Self>
inline auto __call__<Self>::operator()(Self&& frame) {
    PyObject* result = PyEval_EvalFrame(ptr(frame));
    if (result == nullptr) {
        Exception::from_python();
    }
    return reinterpret_steal<Object>(result);
}


[[nodiscard]] inline std::string Interface<Frame>::to_string(
    this const auto& self
) {
    PyFrameObject* frame = ptr(self);
    PyCodeObject* code = PyFrame_GetCode(frame);

    std::string out;
    if (code != nullptr) {
        Py_ssize_t len;
        const char* name = PyUnicode_AsUTF8AndSize(code->co_filename, &len);
        if (name == nullptr) {
            Py_DECREF(code);
            Exception::from_python();
        }
        out += "File \"" + std::string(name, len) + "\", line ";
        out += std::to_string(PyFrame_GetLineNumber(frame)) + ", in ";
        name = PyUnicode_AsUTF8AndSize(code->co_name, &len);
        if (name == nullptr) {
            Py_DECREF(code);
            Exception::from_python();
        }
        out += std::string(name, len);
        Py_DECREF(code);
    } else {
        out += "File \"<unknown>\", line 0, in <unknown>";
    }

    return out;
}


[[nodiscard]] inline std::optional<Code> Interface<Frame>::_get_code(
    this const auto& self
) {
    PyCodeObject* code = PyFrame_GetCode(ptr(self));
    if (code == nullptr) {
        return std::nullopt;
    }
    return reinterpret_steal<Code>(reinterpret_cast<PyObject*>(code));
}


[[nodiscard]] inline std::optional<Frame> Interface<Frame>::_get_back(
    this const auto& self
) {
    PyFrameObject* result = PyFrame_GetBack(ptr(self));
    if (result == nullptr) {
        return std::nullopt;
    }
    return reinterpret_steal<Frame>(reinterpret_cast<PyObject*>(result));
}


[[nodiscard]] inline size_t Interface<Frame>::_get_line_number(
    this const auto& self
) {
    return PyFrame_GetLineNumber(ptr(self));
}


[[nodiscard]] inline size_t Interface<Frame>::_get_last_instruction(
    this const auto& self
) {
    int result = PyFrame_GetLasti(ptr(self));
    if (result < 0) {
        throw RuntimeError("frame is not currently executing");
    }
    return result;
}


[[nodiscard]] inline std::optional<Object> Interface<Frame>::_get_generator(
    this const auto& self
) {
    PyObject* result = PyFrame_GetGenerator(ptr(self));
    if (result == nullptr) {
        return std::nullopt;
    }
    return reinterpret_steal<Object>(result);
}


/////////////////////////
////    TRACEBACK    ////
/////////////////////////


template <impl::is<Traceback> Self>
[[nodiscard]] inline auto __iter__<Self>::operator*() const -> value_type {
    if (curr == nullptr) {
        throw StopIteration();
    }
    return reinterpret_borrow<Frame>(
        reinterpret_cast<PyObject*>(curr->tb_frame)
    );
}


template <impl::is<Traceback> Self>
[[nodiscard]] inline auto __reversed__<Self>::operator*() const -> value_type {
    if (index < 0) {
        throw StopIteration();
    }
    return reinterpret_borrow<Frame>(
        reinterpret_cast<PyObject*>(frames[index]->tb_frame)
    );
}


[[nodiscard]] inline std::string Interface<Traceback>::to_string(
    this const auto& self
) {
    std::string out = "Traceback (most recent call last):";
    PyTracebackObject* tb = ptr(self);
    while (tb != nullptr) {
        out += "\n  ";
        out += reinterpret_borrow<Frame>(
            reinterpret_cast<PyObject*>(tb->tb_frame)
        ).to_string();
        tb = tb->tb_next;
    }
    return out;
}


}  // namespace py


namespace std {

    template <typename T>
        requires (py::__hash__<T>::enable && (
            py::impl::has_call_operator<py::__hash__<T>> ||
            (py::impl::has_cpp<T> && py::impl::hashable<py::impl::cpp_type<T>>) ||
            (!py::impl::has_cpp<T> && std::convertible_to<Py_hash_t, typename py::__hash__<T>::type>)
        ))
    struct hash<T> {
        static constexpr py::__hash__<T>::type operator()(T obj) {
            if constexpr (py::impl::has_call_operator<py::__hash__<T>>) {
                return py::__hash__<T>{}(std::forward<T>(obj));

            } else if constexpr (py::impl::has_cpp<T>) {
                using U = decltype(py::from_python(std::forward<T>(obj)));
                return std::hash<U>{}(py::from_python(std::forward<T>(obj)));

            } else {
                Py_hash_t result = PyObject_Hash(
                    py::ptr(py::to_python(std::forward<T>(obj)))
                );
                if (result == -1 && PyErr_Occurred()) {
                    py::Exception::from_python();
                }
                return result;
            }
        }
    };

};  // namespace std


#endif
