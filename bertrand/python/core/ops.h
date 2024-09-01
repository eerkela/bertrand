#ifndef BERTRAND_PYTHON_CORE_OPS_H
#define BERTRAND_PYTHON_CORE_OPS_H

#include "declarations.h"
#include "object.h"
#include "except.h"


namespace py {


namespace impl {
    static PyObject* one = (Interpreter::init(), PyLong_FromLong(1));  // immortal

    /* Construct a new instance of an inner `Type<Wrapper>::__python__` type using
    Python-based memory allocation and forwarding to its C++ constructor to complete
    initialization. */
    template <typename Wrapper, typename... Args>
        requires (
            std::derived_from<Wrapper, Object> && has_python<Wrapper> &&
            std::constructible_from<typename Wrapper::__python__, Args...>
        )
    static Wrapper construct(Args&&... args) {
        using Self = Wrapper::__python__;
        Type<Wrapper> type;
        PyTypeObject* cls = ptr(type);
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


template <typename Self>
[[nodiscard]] Object::operator bool(this Self&& self) {
    if constexpr (
        impl::has_cpp<Self> &&
        impl::has_operator_bool<impl::cpp_type<Self>>
    ) {
        return static_cast<bool>(unwrap(std::forward<Self>(self)));
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
        return unwrap(std::forward<Self>(self)).contains(
            unwrap(std::forward<Key>(key))
        );

    } else {
        int result = PySequence_Contains(
            ptr(self),
            ptr(as_object(key))
        );
        if (result == -1) {
            Exception::from_python();
        }
        return result;
    }
}


/// TODO: implement the extra overloads for Object, BertrandMeta, Type, and Tuple of
/// types/generic objects


/* Checks if the given object can be safely converted to the specified base type.  This
is automatically called whenever a Python object is narrowed from a parent type to one
of its subclasses. */
template <typename Base, typename Derived>
[[nodiscard]] constexpr bool isinstance(const Derived& obj) {
    if constexpr (std::is_invocable_v<
        __isinstance__<Derived, Base>,
        const Derived&
    >) {
        static_assert(
            std::is_invocable_r_v<bool, __isinstance__<Derived, Base>, const Derived&>,
            "__isinstance__<Derived, Base> must return a boolean value."
        );
        return __isinstance__<Derived, Base>{}(obj);

    } else if constexpr (impl::has_type<Base> && impl::dynamic_type<Derived>) {
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
    requires (std::is_invocable_r_v<
        bool,
        __isinstance__<Derived, Base>,
        const Derived&,
        const Base&
    >)
[[nodiscard]] constexpr bool isinstance(const Derived& obj, const Base& base) {
    return __isinstance__<Derived, Base>{}(obj, base);
}


/* Does a compile-time check to see if the derived type inherits from the base type.
Ordinarily, this is equivalent to a `std::derived_from<>` concept, except that custom
logic is allowed by defining a zero-argument call operator in a specialization of
`__issubclass__`, and `Interface<T>` specializations are used to handle Python objects
in a way that allows for multiple inheritance. */
template <typename Derived, typename Base>
[[nodiscard]] consteval bool issubclass() {
    if constexpr (std::is_invocable_v<__issubclass__<Derived, Base>>) {
        static_assert(
            std::is_invocable_r_v<bool, __issubclass__<Derived, Base>>,
            "__issubclass__<Derived, Base> must return a boolean value."
        );
        return __issubclass__<Derived, Base>{}();

    } else if constexpr (impl::has_interface<Derived> && impl::has_interface<Base>) {
        return std::derived_from<Interface<Derived>, Interface<Base>>;

    } else if constexpr (impl::has_interface<Derived>) {
        return std::derived_from<Interface<Derived>, Base>;

    } else if constexpr (impl::has_interface<Base>) {
        return std::derived_from<Derived, Interface<Base>>;

    } else {
        return std::derived_from<Derived, Base>;
    }
}


/* Devolves to a compile-time `issubclass<Derived, Base>()` check unless the object is
a dynamic object which may be narrowed to a single type, or a one-argument call
operator is defined in a specialization of `__issubclass__`. */
template <std::derived_from<Object> Base, std::derived_from<Object> Derived>
[[nodiscard]] constexpr bool issubclass(const Derived& obj) {
    if constexpr (std::is_invocable_v<__issubclass__<Derived, Base>, const Derived&>) {
        static_assert(
            std::is_invocable_r_v<bool, __issubclass__<Derived, Base>, const Derived&>,
            "__issubclass__<Derived, Base> must return a boolean value."
        );
        return __issubclass__<Derived, Base>{}(obj);

    } else if constexpr (impl::has_type<Base> && impl::dynamic_type<Derived>) {
        return PyType_Check(ptr(obj)) && PyObject_IsSubclass(
            ptr(obj),
            ptr(Type<Base>())
        );

    } else if constexpr (impl::type_like<Derived>) {
        return issubclass<Derived, Base>();

    } else {
        return false;
    }
}


/* Equivalent to Python `issubclass(obj, base)`.  This overload must be explicitly
enabled by defining a two-argument call operator in a specialization of
`__issubclass__`.  The derived type must be a single type or a dynamic object which can
be narrowed to a single type, and the base must be type-like, a union of types, or a
dynamic object which can be narrowed to such. */
template <typename Derived, typename Base>
    requires (std::is_invocable_v<
        __issubclass__<Derived, Base>,
        const Derived&,
        const Base&
    >)
[[nodiscard]] bool issubclass(const Derived& obj, const Base& base) {
    return __issubclass__<Derived, Base>{}(obj, base);
}


/* Equivalent to Python `hasattr(obj, name)` with a static attribute name. */
template <typename T, StaticStr name> requires (__as_object__<T>::enable)
[[nodiscard]] consteval bool hasattr() {
    return __getattr__<T, name>::enable;
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
        PyObject* result = PyObject_GetAttr(ptr(obj), impl::TemplateString<name>::ptr);
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
        PyObject* result = PyObject_GetAttr(ptr(obj), impl::TemplateString<name>::ptr);
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
            ptr(obj),
            impl::TemplateString<name>::ptr,
            ptr(as_object(value))
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
        if (PyObject_DelAttr(ptr(obj), impl::TemplateString<name>::ptr) < 0) {
            Exception::from_python();
        }
    }
}


/* Equivalent to Python `repr(obj)`, but returns a std::string and attempts to
represent C++ types using the stream insertion operator (<<) or std::to_string.  If all
else fails, falls back to demangling the result of typeid(obj).name(). */
template <typename T>
[[nodiscard]] std::string repr(const T& obj) {
    if constexpr (__as_object__<T>::enable) {
        PyObject* str = PyObject_Repr(ptr(as_object(obj)));
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
        return std::to_string(obj);

    } else if constexpr (impl::has_stream_insertion<T>) {
        std::ostringstream stream;
        stream << obj;
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
[[nodiscard]] size_t hash(const T& obj) {
    return std::hash<T>{}(obj);
}


/* Equivalent to Python `len(obj)`. */
template <typename T> requires (__len__<T>::enable)
[[nodiscard]] size_t len(const T& obj) {
    using Return = typename __len__<T>::type;
    static_assert(
        std::same_as<Return, size_t>,
        "len() operator must return a size_t for compatibility with C++ containers.  "
        "Check your specialization of __len__ for these types and ensure the Return "
        "type is set to size_t."
    );
    if constexpr (impl::has_call_operator<__len__<T>>) {
        return __len__<T>{}(obj);

    } else if constexpr (impl::has_cpp<T>) {
        static_assert(
            impl::has_size<impl::cpp_type<T>>,
            "__len__<T> is enabled for a type whose C++ representation does not have "
            "a viable overload for `std::size(T)`.  Did you forget to define a custom "
            "call operator for this type?"
        );
        return std::size(unwrap(obj));

    } else {
        Py_ssize_t size = PyObject_Length(ptr(obj));
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


/* Equivalent to Python `abs(obj)` for any object that specializes the __abs__ control
struct. */
template <std::derived_from<Object> Self> requires (__abs__<Self>::enable)
[[nodiscard]] decltype(auto) abs(const Self& self) {
    if constexpr (impl::has_call_operator<__abs__<Self>>) {
        return __abs__<Self>{}(self);

    } else if (impl::has_cpp<Self>) {
        static_assert(
            impl::has_abs<impl::cpp_type<Self>>,
            "__abs__<T> is enabled for a type whose C++ representation does not have "
            "a viable overload for `std::abs(T)`"
        );
        return std::abs(unwrap(self));

    } else {
        using Return = __abs__<Self>::type;
        static_assert(
            std::derived_from<Return, Object>,
            "Default absolute value operator must return a py::Object subclass.  "
            "Check your specialization of __abs__ for this type and ensure the Return "
            "type derives from py::Object, or define a custom call operator to "
            "override this behavior."
        );
        PyObject* result = PyNumber_Absolute(ptr(self));
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Return>(result);
    }
}


/* Equivalent to Python `abs(obj)`, except that it takes a C++ value and applies
std::abs() for identical semantics. */
template <impl::has_abs T> requires (!__abs__<T>::enable && impl::has_abs<T>)
[[nodiscard]] decltype(auto) abs(const T& value) {
    return std::abs(value);
}


/* Equivalent to Python `base ** exp` (exponentiation). */
template <typename Base, typename Exp> requires (__pow__<Base, Exp>::enable)
[[nodiscard]] decltype(auto) pow(const Base& base, const Exp& exp) {
    if constexpr (impl::has_call_operator<__pow__<Base, Exp>>) {
        return __pow__<Base, Exp>{}(base, exp);

    } else if constexpr (
        (impl::cpp_like<Base> || impl::has_cpp<Base>) &&
        (impl::cpp_like<Exp> || impl::has_cpp<Exp>)
    ) {
        if constexpr (
            impl::complex_like<impl::cpp_type<Base>> &&
            impl::complex_like<impl::cpp_type<Exp>>
        ) {
            return std::common_type_t<impl::cpp_type<Base>, impl::cpp_type<Exp>>(
                pow(unwrap(base).real(), unwrap(exp).real()),
                pow(unwrap(base).imag(), unwrap(exp).imag())
            );
        } else if constexpr (impl::complex_like<impl::cpp_type<Base>>) {
            return Base(
                pow(unwrap(base).real(), unwrap(exp)),
                pow(unwrap(base).real(), unwrap(exp))
            );
        } else if constexpr (impl::complex_like<impl::cpp_type<Exp>>) {
            return Exp(
                pow(unwrap(base), unwrap(exp).real()),
                pow(unwrap(base), unwrap(exp).imag())
            );
        } else {
            static_assert(
                impl::has_pow<impl::cpp_type<Base>, impl::cpp_type<Exp>>,
                "__pow__<Base, Exp> is enabled for operands whose C++ representations "
                "have no viable overload for `std::pow(Base, Exp)`"
            );
            return std::pow(unwrap(base), unwrap(exp));
        }

    } else {
        using Return = typename __pow__<Base, Exp>::type;
        static_assert(
            std::derived_from<Return, Object>,
            "Default pow() operator must return a py::Object subclass.  Check your "
            "specialization of  __pow__ for this type and ensure the Return type "
            "derives from py::Object, or define a custom call operator to override "
            "this behavior."
        );
        PyObject* result = PyNumber_Power(
            ptr(as_object(base)),
            ptr(as_object(exp)),
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
template <typename Base, typename Exp>
    requires (
        !impl::any_are_python_like<Base, Exp> &&
        !__pow__<Base, Exp>::enable &&
        (
            impl::complex_like<Base> ||
            impl::complex_like<Exp> ||
            impl::has_pow<Base, Exp>
        )
    )
[[nodiscard]] decltype(auto) pow(const Base& base, const Exp& exp) {
    if constexpr (impl::complex_like<Base> && impl::complex_like<Exp>) {
        return std::common_type_t<Base, Exp>(
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
[[nodiscard]] decltype(auto) pow(const Base& base, const Exp& exp, const Mod& mod) {
    if constexpr (std::is_invocable_v<
        __pow__<Base, Exp>,
        const Base&,
        const Exp&,
        const Mod&
    >) {
        return __pow__<Base, Exp>{}(base, exp, mod);

    } else if constexpr (
        (impl::cpp_like<Base> || impl::has_cpp<Base>) &&
        (impl::cpp_like<Exp> || impl::has_cpp<Exp>) &&
        (impl::cpp_like<Mod> || impl::has_cpp<Mod>)
    ) {
        return pow(unwrap(base), unwrap(exp), unwrap(mod));

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
            ptr(as_object(base)),
            ptr(as_object(exp)),
            ptr(as_object(mod))
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


template <typename L, typename R> requires (__floordiv__<L, R>::enable)
decltype(auto) floordiv(const L& lhs, const R& rhs) {
    if constexpr (impl::has_call_operator<__floordiv__<L, R>>) {
        return __floordiv__<L, R>{}(lhs, rhs);
    } else {
        using Return = typename __floordiv__<L, R>::type;
        static_assert(
            std::derived_from<Return, Object>,
            "Default floor division operator must return a py::Object subclass.  "
            "Check your specialization of __floordiv__ for these types and ensure the "
            "Return type derives from py::Object, or define a custom call operator to "
            "override this behavior."
        );
        PyObject* result = PyNumber_FloorDivide(
            ptr(as_object(lhs)),
            ptr(as_object(rhs))
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
    if constexpr (impl::has_call_operator<__ifloordiv__<L, R>>) {
        __ifloordiv__<L, R>{}(lhs, rhs);
    } else {
        PyObject* result = PyNumber_InPlaceFloorDivide(
            ptr(lhs),
            ptr(as_object(rhs))
        );
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == ptr(lhs)) {
            Py_DECREF(result);
        } else {
            lhs = reinterpret_steal<Return>(result);
        }
    }
    return lhs;
}


template <std::derived_from<Object> Self> requires (!__invert__<Self>::enable)
decltype(auto) operator~(const Self& self) = delete;
template <std::derived_from<Object> Self> requires (__invert__<Self>::enable)
decltype(auto) operator~(const Self& self) {
    if constexpr (impl::has_call_operator<__invert__<Self>>) {
        return __invert__<Self>{}(self);

    } else if constexpr (impl::has_cpp<Self>) {
        static_assert(
            impl::has_invert<impl::cpp_type<Self>>,
            "__invert__<T> is enabled for a type whose C++ representation does not "
            "have a viable overload for `~T`.  Did you forget to define a custom call "
            "operator for this type?"
        );
        return ~unwrap(self);

    } else {
        using Return = typename __invert__<Self>::type;
        static_assert(
            std::derived_from<Return, Object>,
            "Default bitwise NOT operator must return a py::Object subclass.  Check "
            "your specialization of __invert__ for this type and ensure the Return "
            "type derives from py::Object, or define a custom call operator to "
            "override this behavior."
        );
        PyObject* result = PyNumber_Invert(ptr(self));
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Return>(result);
    }
}


template <std::derived_from<Object> Self> requires (!__pos__<Self>::enable)
decltype(auto) operator+(const Self& self) = delete;
template <std::derived_from<Object> Self> requires (__pos__<Self>::enable)
decltype(auto) operator+(const Self& self) {
    if constexpr (impl::has_call_operator<__pos__<Self>>) {
        return __pos__<Self>{}(self);

    } else if constexpr (impl::has_cpp<Self>) {
        static_assert(
            impl::has_pos<impl::cpp_type<Self>>,
            "__pos__<T> is enabled for a type whose C++ representation does not have "
            "a viable overload for `+T`.  Did you forget to define a custom call "
            "operator for this type?"
        );
        return +unwrap(self);

    } else {
        using Return = typename __pos__<Self>::type;
        static_assert(
            std::derived_from<Return, Object>,
            "Default unary positive operator must return a py::Object subclass.  "
            "Check your specialization of __pos__ for this type and ensure the Return "
            "type derives from py::Object, or define a custom call operator to "
            "override this behavior."
        );
        PyObject* result = PyNumber_Positive(ptr(self));
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Return>(result);
    }
}


template <std::derived_from<Object> Self> requires (!__neg__<Self>::enable)
decltype(auto) operator-(const Self& self) = delete;
template <std::derived_from<Object> Self> requires (__neg__<Self>::enable)
decltype(auto) operator-(const Self& self) {
    if constexpr (impl::has_call_operator<__neg__<Self>>) {
        return __neg__<Self>{}(self);

    } else if constexpr (impl::has_cpp<Self>) {
        static_assert(
            impl::has_neg<impl::cpp_type<Self>>,
            "__neg__<T> is enabled for a type whose C++ representation does not have "
            "a viable overload for `-T`.  Did you forget to define a custom call "
            "operator for this type?"
        );
        return -unwrap(self);

    } else {
        using Return = typename __neg__<Self>::type;
        static_assert(
            std::derived_from<Return, Object>,
            "Default unary negative operator must return a py::Object subclass.  Check "
            "your specialization of __neg__ for this type and ensure the Return type "
            "derives from py::Object, or define a custom call operator to override "
            "this behavior."
        );
        PyObject* result = PyNumber_Negative(ptr(self));
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Return>(result);
    }
}


template <std::derived_from<Object> Self>
Self operator++(Self& self, int) = delete;  // post-increment is not valid Python
template <std::derived_from<Object> Self> requires (!__increment__<Self>::enable)
Self& operator++(Self& self) = delete;
template <std::derived_from<Object> Self> requires (__increment__<Self>::enable)
Self& operator++(Self& self) {
    using Return = typename __increment__<Self>::type;
    static_assert(
        std::same_as<Return, Self&>,
        "Increment operator must return a mutable reference to the derived type.  "
        "Check your specialization of __increment__ for this type and ensure the "
        "Return type is set to the derived type."
    );
    if constexpr (impl::has_call_operator<__increment__<Self>>) {
        __increment__<Self>{}(self);

    } else if constexpr (impl::has_cpp<Self>) {
        static_assert(
            impl::has_preincrement<impl::cpp_type<Self>>,
            "__increment__<T> is enabled for a type whose C++ representation does not "
            "have a viable overload for `++T`.  Did you forget to define a custom "
            "call operator for this type?"
        );
        return ++unwrap(self);

    } else {
        PyObject* result = PyNumber_InPlaceAdd(ptr(self), impl::one);
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == ptr(self)) {
            Py_DECREF(result);
        } else {
            self = reinterpret_steal<Return>(result);
        }
    }
    return self;
}


template <std::derived_from<Object> Self>
Self operator--(Self& self, int) = delete;  // post-decrement is not valid Python
template <std::derived_from<Object> Self> requires (!__decrement__<Self>::enable)
Self& operator--(Self& self) = delete;
template <std::derived_from<Object> Self> requires (__decrement__<Self>::enable)
Self& operator--(Self& self) {
    using Return = typename __decrement__<Self>::type;
    static_assert(
        std::same_as<Return, Self&>,
        "Decrement operator must return a mutable reference to the derived type.  "
        "Check your specialization of __decrement__ for this type and ensure the "
        "Return type is set to the derived type."
    );
    if constexpr (impl::has_call_operator<__decrement__<Self>>) {
        __decrement__<Self>{}(self);

    } else if constexpr (impl::has_cpp<Self>) {
        static_assert(
            impl::has_predecrement<impl::cpp_type<Self>>,
            "__decrement__<T> is enabled for a type whose C++ representation does not "
            "have a viable overload for `--T`.  Did you forget to define a custom "
            "call operator for this type?"
        );
        return --unwrap(self);

    } else {
        PyObject* result = PyNumber_InPlaceSubtract(ptr(self), impl::one);
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == ptr(self)) {
            Py_DECREF(result);
        } else {
            self = reinterpret_steal<Return>(result);
        }
    }
    return self;
}


template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && !__lt__<L, R>::enable)
decltype(auto) operator<(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && __lt__<L, R>::enable)
decltype(auto) operator<(const L& lhs, const R& rhs) {
    if constexpr (impl::has_call_operator<__lt__<L, R>>) {
        return __lt__<L, R>{}(lhs, rhs);

    } else if constexpr (
        (impl::cpp_like<L> || impl::has_cpp<L>) &&
        (impl::cpp_like<R> || impl::has_cpp<R>)
    ) {
        static_assert(
            impl::has_lt<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__lt__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L < R`.  Did you forget to define a custom "
            "call operator for this type?"
        );
        return unwrap(lhs) < unwrap(rhs);

    } else {
        using Return = typename __lt__<L, R>::type;
        static_assert(
            std::same_as<Return, bool>,
            "Default Less-than operator must return a boolean.  Check your "
            "specialization of __lt__ for these types and ensure the Return type "
            "is set to bool, or define a custom call operator to override this "
            "behavior."
        );
        int result = PyObject_RichCompareBool(
            ptr(as_object(lhs)),
            ptr(as_object(rhs)),
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
decltype(auto) operator<=(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && __le__<L, R>::enable)
decltype(auto) operator<=(const L& lhs, const R& rhs) {
    if constexpr (impl::has_call_operator<__le__<L, R>>) {
        return __le__<L, R>{}(lhs, rhs);

    } else if constexpr (
        (impl::cpp_like<L> || impl::has_cpp<L>) &&
        (impl::cpp_like<R> || impl::has_cpp<R>)
    ) {
        static_assert(
            impl::has_le<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__le__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L <= R`.  Did you forget to define a custom "
            "call operator for this type?"
        );
        return unwrap(lhs) <= unwrap(rhs);

    } else {
        using Return = typename __le__<L, R>::type;
        static_assert(
            std::same_as<Return, bool>,
            "Default less-than-or-equal operator must return a boolean value.  Check "
            "your specialization of __le__ for this type and ensure the Return type "
            "is set to bool, or define a custom call operator to override this "
            "behavior."
        );
        int result = PyObject_RichCompareBool(
            ptr(as_object(lhs)),
            ptr(as_object(rhs)),
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
decltype(auto) operator==(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && __eq__<L, R>::enable)
decltype(auto) operator==(const L& lhs, const R& rhs) {
    if constexpr (impl::has_call_operator<__eq__<L, R>>) {
        return __eq__<L, R>{}(lhs, rhs);

    } else if constexpr (
        (impl::cpp_like<L> || impl::has_cpp<L>) &&
        (impl::cpp_like<R> || impl::has_cpp<R>)
    ) {
        static_assert(
            impl::has_eq<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__eq__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L == R`.  Did you forget to define a custom "
            "call operator for this type?"
        );
        return unwrap(lhs) == unwrap(rhs);

    } else {
        using Return = typename __eq__<L, R>::type;
        static_assert(
            std::same_as<Return, bool>,
            "Default equality operator must return a boolean value.  Check your "
            "specialization of __eq__ for this type and ensure the Return type is "
            "set to bool, or define a custom call operator to override this behavior."
        );
        int result = PyObject_RichCompareBool(
            ptr(as_object(lhs)),
            ptr(as_object(rhs)),
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
decltype(auto) operator!=(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && __ne__<L, R>::enable)
decltype(auto) operator!=(const L& lhs, const R& rhs) {
    if constexpr (impl::has_call_operator<__ne__<L, R>>) {
        return __ne__<L, R>{}(lhs, rhs);

    } else if constexpr (
        (impl::cpp_like<L> || impl::has_cpp<L>) &&
        (impl::cpp_like<R> || impl::has_cpp<R>)
    ) {
        static_assert(
            impl::has_ne<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__ne__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L != R`.  Did you forget to define a custom "
            "call operator for this type?"
        );
        return unwrap(lhs) != unwrap(rhs);

    } else {
        using Return = typename __ne__<L, R>::type;
        static_assert(
            std::same_as<Return, bool>,
            "Default inequality operator must return a boolean value.  Check your "
            "specialization of __ne__ for this type and ensure the Return type is "
            "set to bool, or define a custom call operator to override this behavior."
        );
        int result = PyObject_RichCompareBool(
            ptr(as_object(lhs)),
            ptr(as_object(rhs)),
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
decltype(auto) operator>=(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && __ge__<L, R>::enable)
decltype(auto) operator>=(const L& lhs, const R& rhs) {
    if constexpr (impl::has_call_operator<__ge__<L, R>>) {
        return __ge__<L, R>{}(lhs, rhs);

    } else if constexpr (
        (impl::cpp_like<L> || impl::has_cpp<L>) &&
        (impl::cpp_like<R> || impl::has_cpp<R>)
    ) {
        static_assert(
            impl::has_ge<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__ge__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L >= R`.  Did you forget to define a custom "
            "call operator for this type?"
        );
        return unwrap(lhs) >= unwrap(rhs);

    } else {
        using Return = typename __ge__<L, R>::type;
        static_assert(
            std::same_as<Return, bool>,
            "Default greater-than-or-equal operator must return a boolean value.  "
            "Check your specialization of __ge__ for this type and ensure the Return "
            "type is set to bool, or define a custom call operator to override this "
            "behavior."
        );
        int result = PyObject_RichCompareBool(
            ptr(as_object(lhs)),
            ptr(as_object(rhs)),
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
decltype(auto) operator>(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && __gt__<L, R>::enable)
decltype(auto) operator>(const L& lhs, const R& rhs) {
    if constexpr (impl::has_call_operator<__gt__<L, R>>) {
        return __gt__<L, R>{}(lhs, rhs);

    } else if constexpr (
        (impl::cpp_like<L> || impl::has_cpp<L>) &&
        (impl::cpp_like<R> || impl::has_cpp<R>)
    ) {
        static_assert(
            impl::has_gt<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__gt__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L > R`.  Did you forget to define a custom "
            "call operator for this type?"
        );
        return unwrap(lhs) > unwrap(rhs);

    } else {
        using Return = typename __gt__<L, R>::type;
        static_assert(
            std::same_as<Return, bool>,
            "Default greater-than operator must return a boolean value.  Check your "
            "specialization of __gt__ for this type and ensure the Return type is "
            "set to bool, or define a custom call operator to override this behavior."
        );
        int result = PyObject_RichCompareBool(
            ptr(as_object(lhs)),
            ptr(as_object(rhs)),
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
decltype(auto) operator+(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && __add__<L, R>::enable)
decltype(auto) operator+(const L& lhs, const R& rhs) {
    if constexpr (impl::has_call_operator<__add__<L, R>>) {
        return __add__<L, R>{}(lhs, rhs);

    } else if constexpr (
        (impl::cpp_like<L> || impl::has_cpp<L>) &&
        (impl::cpp_like<R> || impl::has_cpp<R>)
    ) {
        static_assert(
            impl::has_add<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__add__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L + R`.  Did you forget to define a custom "
            "call operator for this type?"
        );
        return unwrap(lhs) + unwrap(rhs);

    } else {
        using Return = typename __add__<L, R>::type;
        static_assert(
            std::derived_from<Return, Object>,
            "Default addition operator must return a py::Object subclass.  Check your "
            "specialization of __add__ for this type and ensure the Return type is "
            "derived from py::Object, or define a custom call operator to override "
            "this behavior."
        );
        PyObject* result = PyNumber_Add(
            ptr(as_object(lhs)),
            ptr(as_object(rhs))
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
        "operand.  Check your specialization of __iadd__ for these types and ensure "
        "the Return type is set to the left operand."
    );
    if constexpr (impl::has_call_operator<__iadd__<L, R>>) {
        __iadd__<L, R>{}(lhs, rhs);

    } else if constexpr (
        (impl::cpp_like<L> || impl::has_cpp<L>) &&
        (impl::cpp_like<R> || impl::has_cpp<R>)
    ) {
        static_assert(
            impl::has_iadd<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__iadd__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L += R`.  Did you forget to define a custom "
            "call operator for this type?"
        );
        unwrap(lhs) += unwrap(rhs);

    } else {
        PyObject* result = PyNumber_InPlaceAdd(
            ptr(lhs),
            ptr(as_object(rhs))
        );
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == ptr(lhs)) {
            Py_DECREF(result);
        } else {
            lhs = reinterpret_steal<Return>(result);
        }
    }
    return lhs;
}


template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && !__sub__<L, R>::enable)
decltype(auto) operator-(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && __sub__<L, R>::enable)
decltype(auto) operator-(const L& lhs, const R& rhs) {
    if constexpr (impl::has_call_operator<__sub__<L, R>>) {
        return __sub__<L, R>{}(lhs, rhs);

    } else if constexpr (
        (impl::cpp_like<L> || impl::has_cpp<L>) &&
        (impl::cpp_like<R> || impl::has_cpp<R>)
    ) {
        static_assert(
            impl::has_sub<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__sub__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L - R`.  Did you forget to define a custom "
            "call operator for this type?"
        );
        return unwrap(lhs) - unwrap(rhs);

    } else {
        using Return = typename __sub__<L, R>::type;
        static_assert(
            std::derived_from<Return, Object>,
            "Default subtraction operator must return a py::Object subclass.  Check "
            "your specialization of __sub__ for this type and ensure the Return type "
            "derives from py::Object, or define a custom call operator to override "
            "this behavior."
        );
        PyObject* result = PyNumber_Subtract(
            ptr(as_object(lhs)),
            ptr(as_object(rhs))
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
        "operand.  Check your specialization of __isub__ for these types and ensure "
        "the Return type is set to the left operand."
    );
    if constexpr (impl::has_call_operator<__isub__<L, R>>) {
        __isub__<L, R>{}(lhs, rhs);

    } else if constexpr (
        (impl::cpp_like<L> || impl::has_cpp<L>) &&
        (impl::cpp_like<R> || impl::has_cpp<R>)
    ) {
        static_assert(
            impl::has_isub<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__isub__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L -= R`.  Did you forget to define a custom "
            "call operator for this type?"
        );
        unwrap(lhs) -= unwrap(rhs);

    } else {
        PyObject* result = PyNumber_InPlaceSubtract(
            ptr(lhs),
            ptr(as_object(rhs))
        );
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == ptr(lhs)) {
            Py_DECREF(result);
        } else {
            lhs = reinterpret_steal<Return>(result);
        }
    }
    return lhs;
}


template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && !__mul__<L, R>::enable)
decltype(auto) operator*(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && __mul__<L, R>::enable)
decltype(auto) operator*(const L& lhs, const R& rhs) {
    if constexpr (impl::has_call_operator<__mul__<L, R>>) {
        return __mul__<L, R>{}(lhs, rhs);

    } else if constexpr (
        (impl::cpp_like<L> || impl::has_cpp<L>) &&
        (impl::cpp_like<R> || impl::has_cpp<R>)
    ) {
        static_assert(
            impl::has_mul<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__mul__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L * R`.  Did you forget to define a custom "
            "call operator for this type?"
        );
        return unwrap(lhs) * unwrap(rhs);

    } else {
        using Return = typename __mul__<L, R>::type;
        static_assert(
            std::derived_from<Return, Object>,
            "Default multiplication operator must return a py::Object subclass.  "
            "Check your specialization of __mul__ for this type and ensure the Return "
            "type derives from py::Object, or define a custom call operator to "
            "override this behavior."
        );
        PyObject* result = PyNumber_Multiply(
            ptr(as_object(lhs)),
            ptr(as_object(rhs))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Return>(result);
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
    if constexpr (impl::has_call_operator<__imul__<L, R>>) {
        __imul__<L, R>{}(lhs, rhs);

    } else if constexpr (
        (impl::cpp_like<L> || impl::has_cpp<L>) &&
        (impl::cpp_like<R> || impl::has_cpp<R>)
    ) {
        static_assert(
            impl::has_imul<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__imul__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L *= R`.  Did you forget to define a custom "
            "call operator for this type?"
        );
        unwrap(lhs) *= unwrap(rhs);

    } else {
        PyObject* result = PyNumber_InPlaceMultiply(
            ptr(lhs),
            ptr(as_object(rhs))
        );
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == ptr(lhs)) {
            Py_DECREF(result);
        } else {
            lhs = reinterpret_steal<Return>(result);
        }
    }
    return lhs;
}


template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && !__truediv__<L, R>::enable)
decltype(auto) operator/(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && __truediv__<L, R>::enable)
decltype(auto) operator/(const L& lhs, const R& rhs) {
    if constexpr (impl::has_call_operator<__truediv__<L, R>>) {
        return __truediv__<L, R>{}(lhs, rhs);

    } else if constexpr (
        (impl::cpp_like<L> || impl::has_cpp<L>) &&
        (impl::cpp_like<R> || impl::has_cpp<R>)
    ) {
        static_assert(
            impl::has_truediv<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__truediv__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L / R`.  Did you forget to define a custom "
            "call operator for this type?"
        );
        return unwrap(lhs) / unwrap(rhs);

    } else {
        using Return = typename __truediv__<L, R>::type;
        static_assert(
            std::derived_from<Return, Object>,
            "Default true division operator must return a py::Object subclass.  Check "
            "your specialization of __truediv__ for this type and ensure the Return "
            "type derives from py::Object, or define a custom call operator to "
            "override this behavior."
        );
        PyObject* result = PyNumber_TrueDivide(
            ptr(as_object(lhs)),
            ptr(as_object(rhs))
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
    if constexpr (impl::has_call_operator<__itruediv__<L, R>>) {
        __itruediv__<L, R>{}(lhs, rhs);

    } else if constexpr (
        (impl::cpp_like<L> || impl::has_cpp<L>) &&
        (impl::cpp_like<R> || impl::has_cpp<R>)
    ) {
        static_assert(
            impl::has_itruediv<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__itruediv__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L /= R`.  Did you forget to define a custom "
            "call operator for this type?"
        );
        unwrap(lhs) /= unwrap(rhs);

    } else {
        PyObject* result = PyNumber_InPlaceTrueDivide(
            ptr(lhs),
            ptr(as_object(rhs))
        );
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == ptr(lhs)) {
            Py_DECREF(result);
        } else {
            lhs = reinterpret_steal<Return>(result);
        }
    }
    return lhs;
}


template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && !__mod__<L, R>::enable)
decltype(auto) operator%(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && __mod__<L, R>::enable)
decltype(auto) operator%(const L& lhs, const R& rhs) {
    if constexpr (impl::has_call_operator<__mod__<L, R>>) {
        return __mod__<L, R>{}(lhs, rhs);

    } else if constexpr (
        (impl::cpp_like<L> || impl::has_cpp<L>) &&
        (impl::cpp_like<R> || impl::has_cpp<R>)
    ) {
        static_assert(
            impl::has_mod<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__mod__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L % R`.  Did you forget to define a custom "
            "call operator for this type?"
        );
        return unwrap(lhs) % unwrap(rhs);

    } else {
        using Return = typename __mod__<L, R>::type;
        static_assert(
            std::derived_from<Return, Object>,
            "Default modulus operator must return a py::Object subclass.  Check your "
            "specialization of __mod__ for this type and ensure the Return type is "
            "derived from py::Object, or define a custom call operator to override "
            "this behavior."
        );
        PyObject* result = PyNumber_Remainder(
            ptr(as_object(lhs)),
            ptr(as_object(rhs))
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
    if constexpr (impl::has_call_operator<__imod__<L, R>>) {
        __imod__<L, R>{}(lhs, rhs);

    } else if constexpr (
        (impl::cpp_like<L> || impl::has_cpp<L>) &&
        (impl::cpp_like<R> || impl::has_cpp<R>)
    ) {
        static_assert(
            impl::has_imod<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__imod__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L %= R`.  Did you forget to define a custom "
            "call operator for this type?"
        );
        unwrap(lhs) %= unwrap(rhs);

    } else {
        PyObject* result = PyNumber_InPlaceRemainder(
            ptr(lhs),
            ptr(as_object(rhs))
        );
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == ptr(lhs)) {
            Py_DECREF(result);
        } else {
            lhs = reinterpret_steal<Return>(result);
        }
    }
    return lhs;
}


template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && !__lshift__<L, R>::enable)
decltype(auto) operator<<(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && __lshift__<L, R>::enable)
decltype(auto) operator<<(const L& lhs, const R& rhs) {
    if constexpr (impl::has_call_operator<__lshift__<L, R>>) {
        return __lshift__<L, R>{}(lhs, rhs);

    } else if constexpr (
        (impl::cpp_like<L> || impl::has_cpp<L>) &&
        (impl::cpp_like<R> || impl::has_cpp<R>)
    ) {
        static_assert(
            impl::has_lshift<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__lshift__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L << R`.  Did you forget to define a custom "
            "call operator for this type?"
        );
        return unwrap(lhs) << unwrap(rhs);

    } else {
        using Return = typename __lshift__<L, R>::type;
        static_assert(
            std::derived_from<Return, Object>,
            "Default left shift operator must return a py::Object subclass.  Check "
            "your specialization of __lshift__ for this type and ensure the Return "
            "type derives from py::Object, or define a custom call operator to "
            "override this behavior."
        );
        PyObject* result = PyNumber_Lshift(
            ptr(as_object(lhs)),
            ptr(as_object(rhs))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Return>(result);
    }
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
    if constexpr (impl::has_call_operator<__ilshift__<L, R>>) {
        __ilshift__<L, R>{}(lhs, rhs);

    } else if constexpr (
        (impl::cpp_like<L> || impl::has_cpp<L>) &&
        (impl::cpp_like<R> || impl::has_cpp<R>)
    ) {
        static_assert(
            impl::has_ilshift<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__ilshift__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L <<= R`.  Did you forget to define a custom "
            "call operator for this type?"
        );
        unwrap(lhs) <<= unwrap(rhs);

    } else {
        PyObject* result = PyNumber_InPlaceLshift(
            ptr(lhs),
            ptr(as_object(rhs))
        );
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == ptr(lhs)) {
            Py_DECREF(result);
        } else {
            lhs = reinterpret_steal<Return>(result);
        }
    }
    return lhs;
}


template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && !__rshift__<L, R>::enable)
decltype(auto) operator>>(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && __rshift__<L, R>::enable)
decltype(auto) operator>>(const L& lhs, const R& rhs) {
    if constexpr (impl::has_call_operator<__rshift__<L, R>>) {
        return __rshift__<L, R>{}(lhs, rhs);

    } else if constexpr (
        (impl::cpp_like<L> || impl::has_cpp<L>) &&
        (impl::cpp_like<R> || impl::has_cpp<R>)
    ) {
        static_assert(
            impl::has_rshift<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__rshift__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L >> R`.  Did you forget to define a custom "
            "call operator for this type?"
        );
        return unwrap(lhs) >> unwrap(rhs);

    } else {
        using Return = typename __rshift__<L, R>::type;
        static_assert(
            std::derived_from<Return, Object>,
            "Default right shift operator must return a py::Object subclass.  Check "
            "your specialization of __rshift__ for this type and ensure the Return "
            "type derives from py::Object, or define a custom call operator to "
            "override this behavior."
        );
        PyObject* result = PyNumber_Rshift(
            ptr(as_object(lhs)),
            ptr(as_object(rhs))
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
    if constexpr (impl::has_call_operator<__irshift__<L, R>>) {
        __irshift__<L, R>{}(lhs, rhs);

    } else if constexpr (
        (impl::cpp_like<L> || impl::has_cpp<L>) &&
        (impl::cpp_like<R> || impl::has_cpp<R>)
    ) {
        static_assert(
            impl::has_irshift<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__irshift__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L >>= R`.  Did you forget to define a custom "
            "call operator for this type?"
        );
        unwrap(lhs) >>= unwrap(rhs);

    } else {
        PyObject* result = PyNumber_InPlaceRshift(
            ptr(lhs),
            ptr(as_object(rhs))
        );
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == ptr(lhs)) {
            Py_DECREF(result);
        } else {
            lhs = reinterpret_steal<Return>(result);
        }
    }
    return lhs;
}


template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && !__and__<L, R>::enable)
decltype(auto) operator&(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && __and__<L, R>::enable)
decltype(auto) operator&(const L& lhs, const R& rhs) {
    if constexpr (impl::has_call_operator<__and__<L, R>>) {
        return __and__<L, R>{}(lhs, rhs);

    } else if constexpr (
        (impl::cpp_like<L> || impl::has_cpp<L>) &&
        (impl::cpp_like<R> || impl::has_cpp<R>)
    ) {
        static_assert(
            impl::has_and<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__and__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L & R`.  Did you forget to define a custom "
            "call operator for this type?"
        );
        return unwrap(lhs) & unwrap(rhs);

    } else {
        using Return = typename __and__<L, R>::type;
        static_assert(
            std::derived_from<Return, Object>,
            "Default bitwise AND operator must return a py::Object subclass.  Check "
            "your specialization of __and__ for this type and ensure the Return type "
            "derives from py::Object, or define a custom call operator to override "
            "this behavior."
        );
        PyObject* result = PyNumber_And(
            ptr(as_object(lhs)),
            ptr(as_object(rhs))
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
    if constexpr (impl::has_call_operator<__iand__<L, R>>) {
        __iand__<L, R>{}(lhs, rhs);

    } else if constexpr (
        (impl::cpp_like<L> || impl::has_cpp<L>) &&
        (impl::cpp_like<R> || impl::has_cpp<R>)
    ) {
        static_assert(
            impl::has_iand<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__iand__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L &= R`.  Did you forget to define a custom "
            "call operator for this type?"
        );
        unwrap(lhs) &= unwrap(rhs);

    } else {
        PyObject* result = PyNumber_InPlaceAnd(
            ptr(lhs),
            ptr(as_object(rhs))
        );
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == ptr(lhs)) {
            Py_DECREF(result);
        } else {
            lhs = reinterpret_steal<Return>(result);
        }
    }
    return lhs;
}


template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && !__or__<L, R>::enable)
decltype(auto) operator|(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && __or__<L, R>::enable)
decltype(auto) operator|(const L& lhs, const R& rhs) {
    if constexpr (impl::has_call_operator<__or__<L, R>>) {
        return __or__<L, R>{}(lhs, rhs);

    } else if constexpr (
        (impl::cpp_like<L> || impl::has_cpp<L>) &&
        (impl::cpp_like<R> || impl::has_cpp<R>)
    ) {
        static_assert(
            impl::has_or<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__or__<L, R> is enabled for operands whose C++ representations have no "
            "viable overload for `L | R`.  Did you forget to define a custom call "
            "operator for this type?"
        );
        return unwrap(lhs) | unwrap(rhs);

    } else {
        using Return = typename __or__<L, R>::type;
        static_assert(
            std::derived_from<Return, Object>,
            "Default bitwise OR operator must return a py::Object subclass.  Check "
            "your specialization of __or__ for this type and ensure the Return type "
            "derives from py::Object, or define a custom call operator to override "
            "this behavior."
        );
        PyObject* result = PyNumber_Or(
            ptr(as_object(lhs)),
            ptr(as_object(rhs))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Return>(result);
    }
}


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
    if constexpr (impl::has_call_operator<__ior__<L, R>>) {
        __ior__<L, R>{}(lhs, rhs);

    } else if constexpr (
        (impl::cpp_like<L> || impl::has_cpp<L>) &&
        (impl::cpp_like<R> || impl::has_cpp<R>)
    ) {
        static_assert(
            impl::has_ior<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__ior__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L |= R`.  Did you forget to define a custom "
            "call operator for this type?"
        );
        unwrap(lhs) |= unwrap(rhs);

    } else {
        PyObject* result = PyNumber_InPlaceOr(
            ptr(lhs),
            ptr(as_object(rhs))
        );
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == ptr(lhs)) {
            Py_DECREF(result);
        } else {
            lhs = reinterpret_steal<Return>(result);
        }
    }
    return lhs;
}


template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && !__xor__<L, R>::enable)
decltype(auto) operator^(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && __xor__<L, R>::enable)
decltype(auto) operator^(const L& lhs, const R& rhs) {
    if constexpr (impl::has_call_operator<__xor__<L, R>>) {
        return __xor__<L, R>{}(lhs, rhs);

    } else if constexpr (
        (impl::cpp_like<L> || impl::has_cpp<L>) &&
        (impl::cpp_like<R> || impl::has_cpp<R>)
    ) {
        static_assert(
            impl::has_xor<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__xor__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L ^ R`.  Did you forget to define a custom "
            "call operator for this type?"
        );
        return unwrap(lhs) ^ unwrap(rhs);

    } else {
        using Return = typename __xor__<L, R>::type;
        static_assert(
            std::derived_from<Return, Object>,
            "Default bitwise XOR operator must return a py::Object subclass.  Check "
            "your specialization of __xor__ for this type and ensure the Return type "
            "derives from py::Object, or define a custom call operator to override "
            "this behavior."
        );
        PyObject* result = PyNumber_Xor(
            ptr(as_object(lhs)),
            ptr(as_object(rhs))
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
    if constexpr (impl::has_call_operator<__ixor__<L, R>>) {
        __ixor__<L, R>{}(lhs, rhs);

    } else if constexpr (
        (impl::cpp_like<L> || impl::has_cpp<L>) &&
        (impl::cpp_like<R> || impl::has_cpp<R>)
    ) {
        static_assert(
            impl::has_ixor<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__ixor__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L ^= R`.  Did you forget to define a custom "
            "call operator for this type?"
        );
        unwrap(lhs) ^= unwrap(rhs);

    } else {
        PyObject* result = PyNumber_InPlaceXor(
            ptr(lhs),
            ptr(as_object(rhs))
        );
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == ptr(lhs)) {
            Py_DECREF(result);
        } else {
            lhs = reinterpret_steal<Return>(result);
        }
    }
    return lhs;
}


//////////////////////
////    OBJECT    ////
//////////////////////


template <typename T>
    requires (
        __as_object__<T>::enable &&
        impl::has_cpp<typename __as_object__<T>::type> &&
        std::same_as<T, impl::cpp_type<typename __as_object__<T>::type>>
    )
[[nodiscard]] auto wrap(T& obj) -> __as_object__<T>::type {
    using Wrapper = __as_object__<T>::type;
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


template <typename T>
    requires (
        __as_object__<T>::enable &&
        impl::has_cpp<typename __as_object__<T>::type> &&
        std::same_as<T, impl::cpp_type<typename __as_object__<T>::type>>
    )
[[nodiscard]] auto wrap(const T& obj) -> __as_object__<T>::type {
    using Wrapper = __as_object__<T>::type;
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


template <typename T>
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
            ptr(obj)->m_cpp
        );
    } else {
        return obj;
    }
}


template <typename T>
[[nodiscard]] const auto& unwrap(const T& obj) {
    if constexpr (impl::has_cpp<T>) {
        using CppType = impl::cpp_type<T>;
        return std::visit(
            Object::Visitor{
                [](const CppType& cpp) -> const CppType& { return cpp; },
                [](const CppType* cpp) -> const CppType& { return *cpp; }
            },
            ptr(obj)->m_cpp
        );
    } else {
        return obj;
    }
}


template <typename T, impl::is<Object> Base>
constexpr bool __isinstance__<T, Base>::operator()(T&& obj, Base&& cls) {
    if constexpr (impl::python_like<T>) {
        int result = PyObject_IsInstance(
            ptr(obj),
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
        ptr(as_object(obj)),
        ptr(cls)
    );
    if (result == -1) {
        Exception::from_python();
    }
    return result;
}


template <impl::inherits<Object> From, impl::inherits<From> To>
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
        throw TypeError(
            "cannot convert Python object from type '" + repr(Type<From>()) +
            "' to type '" + repr(Type<To>()) + "'"
        );
    }
}


template <impl::inherits<Object> From, impl::cpp_like To>
    requires (__as_object__<To>::enable && std::integral<To>)
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


template <impl::inherits<Object> From, impl::cpp_like To>
    requires (__as_object__<To>::enable && std::floating_point<To>)
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
    PyObject* str = PyObject_Str(ptr(from));
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
Stream& __lshift__<Stream, Self>::operator()(Stream& stream, Self&& self) {
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
auto __init__<Code, Source>::operator()(const std::string& source) {
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
Frame __explicit_init__<Frame, T>::operator()(int skip) {
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
inline auto __call__<Self>::operator()(Frame&& frame) {
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


[[nodiscard]] inline std::optional<Code> Interface<Frame>::_code(
    this const auto& self
) {
    PyCodeObject* code = PyFrame_GetCode(ptr(self));
    if (code == nullptr) {
        return std::nullopt;
    }
    return reinterpret_steal<Code>(reinterpret_cast<PyObject*>(code));
}


[[nodiscard]] inline std::optional<Frame> Interface<Frame>::_back(
    this const auto& self
) {
    PyFrameObject* result = PyFrame_GetBack(ptr(self));
    if (result == nullptr) {
        return std::nullopt;
    }
    return reinterpret_steal<Frame>(reinterpret_cast<PyObject*>(result));
}


[[nodiscard]] inline size_t Interface<Frame>::_line_number(
    this const auto& self
) {
    return PyFrame_GetLineNumber(ptr(self));
}


[[nodiscard]] inline size_t Interface<Frame>::_last_instruction(
    this const auto& self
) {
    int result = PyFrame_GetLasti(ptr(self));
    if (result < 0) {
        throw RuntimeError("frame is not currently executing");
    }
    return result;
}


[[nodiscard]] inline std::optional<Object> Interface<Frame>::_generator(
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

    template <typename T> requires (py::__hash__<T>::enable)
    struct hash<T> {
        static_assert(
            std::same_as<typename py::__hash__<T>::type, size_t>,
            "std::hash<> must return size_t for compatibility with other C++ types.  "
            "Check your specialization of __hash__ for this type and ensure the "
            "Return type is set to size_t."
        );
        static constexpr size_t operator()(const T& obj) {
            if constexpr (py::impl::has_call_operator<py::__hash__<T>>) {
                return py::__hash__<T>{}(obj);
            } else if constexpr (py::impl::has_cpp<T>) {
                static_assert(
                    py::impl::hashable<py::impl::cpp_type<T>>,
                    "__hash__<T> is enabled for a type whose C++ representation does "
                    "not have a viable overload for `std::hash<T>{}`.  Did you forget "
                    "to define a custom call operator for this type?"
                );
                return std::hash<T>{}(py::unwrap(obj));
            } else {
                Py_ssize_t result = PyObject_Hash(py::ptr(obj));
                if (result == -1 && PyErr_Occurred()) {
                    py::Exception::from_python();
                }
                return static_cast<size_t>(result);
            }
        }
    };

};  // namespace std


#endif
