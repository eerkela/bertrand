#ifndef BERTRAND_PYTHON_CORE_OPS_H
#define BERTRAND_PYTHON_CORE_OPS_H

#include "declarations.h"
#include "object.h"
#include "except.h"


namespace py {


/// TODO: add assertions where appropriate to ensure that no object is ever null.


namespace impl {

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

    template <StaticStr name>
    Object template_string() {
        PyObject* result = PyUnicode_FromStringAndSize(name, name.size());
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Object>(result);
    }

}


/// TODO: implement the extra overloads for Object, Union, Type, and Tuple of
/// any of the above.


/* Does a compile-time check to see if the derived type inherits from the base type.
Ordinarily, this is equivalent to a `std::derived_from<>` concept, except that custom
logic is allowed by defining a zero-argument call operator in a specialization of
`__issubclass__`, and `Interface<T>` specializations are used to handle Python objects
in a way that allows for multiple inheritance. */
template <typename Derived, typename Base>
    requires (
        !std::is_reference_v<Derived> &&
        !std::is_reference_v<Base> &&
        !std::is_const_v<Derived> &&
        !std::is_const_v<Base> &&
        !std::is_volatile_v<Derived> &&
        !std::is_volatile_v<Base> &&
        impl::inherits<Base, Object> &&
        impl::inherits<Derived, Object> && (
            std::is_invocable_r_v<bool, __issubclass__<Derived, Base>> ||
            !std::is_invocable_v<__issubclass__<Derived, Base>>
        )
    )
[[nodiscard]] constexpr bool issubclass() {
    if constexpr (std::is_invocable_v<__issubclass__<Derived, Base>>) {
        return __issubclass__<Derived, Base>{}();
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
        !std::is_reference_v<Base> &&
        !std::is_const_v<Base> &&
        !std::is_volatile_v<Base> &&
        impl::inherits<Base, Object> &&
        impl::inherits<Derived, Object> &&
        std::is_invocable_r_v<bool, __issubclass__<Derived, Base>, Derived>
    )
[[nodiscard]] constexpr bool issubclass(Derived&& obj) {
    if constexpr (DEBUG) {
        assert_(
            ptr(obj) != nullptr,
            "issubclass() cannot be called on a null object."
        );
    }
    return __issubclass__<Derived, Base>{}(std::forward<Derived>(obj));
}


/* Equivalent to Python `issubclass(obj, base)`.  This overload must be explicitly
enabled by defining a two-argument call operator in a specialization of
`__issubclass__`.  The derived type must be a single type or a dynamic object which can
be narrowed to a single type, and the base must be type-like, a union of types, or a
dynamic object which can be narrowed to such. */
template <typename Derived, typename Base>
    requires (
        impl::inherits<Base, Object> &&
        impl::inherits<Derived, Object> &&
        std::is_invocable_r_v<bool, __issubclass__<Derived, Base>, Derived, Base>
    )
[[nodiscard]] constexpr bool issubclass(Derived&& obj, Base&& base) {
    if constexpr (DEBUG) {
        assert_(
            ptr(obj) != nullptr,
            "left operand to issubclass() cannot be a null object."
        );
        assert_(
            ptr(base) != nullptr,
            "right operand to issubclass() cannot be a null object."
        );
    }
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
        !std::is_reference_v<Base> &&
        !std::is_const_v<Base> &&
        !std::is_volatile_v<Base> &&
        impl::inherits<Base, Object> &&
        impl::inherits<Derived, Object> && (
            std::is_invocable_r_v<bool, __isinstance__<Derived, Base>, Derived> ||
            !std::is_invocable_v<__isinstance__<Derived, Base>>
        )
    )
[[nodiscard]] constexpr bool isinstance(Derived&& obj) {
    if constexpr (DEBUG) {
        assert_(
            ptr(obj) != nullptr,
            "isinstance() cannot be called on a null object."
        );
    }
    if constexpr (std::is_invocable_v<__isinstance__<Derived, Base>, Derived>) {
        return __isinstance__<Derived, Base>{}(std::forward<Derived>(obj));
    } else {
        return issubclass<std::remove_cvref_t<Derived>, Base>();
    }
}


/* Equivalent to Python `isinstance(obj, base)`.  This overload must be explicitly
enabled by defining a two-argument call operator in a specialization of
`__isinstance__`.  By default, this is only done for bases which are type-like, a union
of types, or a dynamic object which can be narrowed to either of the above. */
template <typename Derived, typename Base>
    requires (
        impl::inherits<Base, Object> &&
        impl::inherits<Derived, Object> &&
        std::is_invocable_r_v<bool, __isinstance__<Derived, Base>, Derived, Base>
    )
[[nodiscard]] constexpr bool isinstance(Derived&& obj, Base&& base) {
    if constexpr (DEBUG) {
        assert_(
            ptr(obj) != nullptr,
            "left operand to isinstance() cannot be a null object."
        );
        assert_(
            ptr(base) != nullptr,
            "right operand to isinstance() cannot be a null object."
        );
    }
    return __isinstance__<Derived, Base>{}(
        std::forward<Derived>(obj),
        std::forward<Base>(base)
    );
}


/* Implicitly convert a Python object into one of its superclasses. */
template <impl::inherits<Object> From, std::derived_from<Object> To>
    requires (
        !impl::is<From, To> &&
        issubclass<std::remove_cvref_t<From>, To>()
    )
struct __cast__<From, To>                                   : Returns<To> {
    static To operator()(From from) {
        if constexpr (std::is_lvalue_reference_v<From>) {
            return reinterpret_borrow<To>(ptr(from));
        } else {
            return reinterpret_steal<To>(release(from));
        }
    }
};


/* Implicitly convert a Python object into one of its subclasses by applying an
`isinstance<Derived>()` check. */
template <impl::inherits<Object> From, std::derived_from<Object> To>
    requires (
        !impl::is<From, To> &&
        issubclass<To, std::remove_cvref_t<From>>()
    )
struct __cast__<From, To>                                   : Returns<To> {
    template <size_t I>
    static size_t find_union_type(std::add_lvalue_reference_t<From> obj) {
        if constexpr (I < To::n) {
            if (isinstance<To::template at<I>>(obj)) {
                return I;
            }
            return find_union_type<I + 1>(obj);
        } else {
            return To::n;
        }
    }

    static auto operator()(From from) {
        if constexpr (impl::inherits<To, impl::UnionTag>) {
            size_t index = find_union_type<0>(from);
            if (index == To::n) {
                throw TypeError(
                    "cannot convert Python object from type '" +
                    repr(Type<From>()) + "' to type '" +
                    repr(Type<To>()) + "'"
                );
            }
            if constexpr (std::is_lvalue_reference_v<From>) {
                To result = reinterpret_borrow<To>(ptr(from));
                result.m_index = index;
                return result;
            } else {
                To result = reinterpret_steal<To>(release(from));
                result.m_index = index;
                return result;
            }

        } else {
            if (isinstance<To>(from)) {
                if constexpr (std::is_lvalue_reference_v<From>) {
                    return reinterpret_borrow<To>(ptr(from));
                } else {
                    return reinterpret_steal<To>(release(from));
                }
            } else {
                throw TypeError(
                    "cannot convert Python object from type '" + repr(Type<From>()) +
                    "' to type '" + repr(Type<To>()) + "'"
                );
            }
        }
    }
};


/* Equivalent to Python `hasattr(obj, name)` with a static attribute name. */
template <impl::python Self, StaticStr Name>
[[nodiscard]] constexpr bool hasattr() {
    return __getattr__<Self, Name>::enable;
}


/* Equivalent to Python `hasattr(obj, name)` with a static attribute name. */
template <StaticStr Name, impl::python Self>
[[nodiscard]] constexpr bool hasattr(Self&& obj) {
    return __getattr__<Self, Name>::enable;
}


/* Equivalent to Python `getattr(obj, name)` with a static attribute name. */
template <StaticStr Name, impl::python Self>
    requires (
        __getattr__<Self, Name>::enable &&
        std::derived_from<typename __getattr__<Self, Name>::type, Object> && (
            !std::is_invocable_v<__getattr__<Self, Name>, Self> ||
            std::is_invocable_r_v<
                typename __getattr__<Self, Name>::type,
                __getattr__<Self, Name>,
                Self
            >
        )
    )
[[nodiscard]] auto getattr(Self&& self) -> __getattr__<Self, Name>::type {
    if constexpr (DEBUG) {
        assert_(
            ptr(self) != nullptr,
            "Cannot get attribute '" + Name + "' from a null object."
        );
    }
    if constexpr (std::is_invocable_v<__getattr__<Self, Name>, Self>) {
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
template <StaticStr Name, impl::python Self>
    requires (
        __getattr__<Self, Name>::enable &&
        std::derived_from<typename __getattr__<Self, Name>::type, Object> && (
            !std::is_invocable_v<
                __getattr__<Self, Name>,
                Self,
                const typename __getattr__<Self, Name>::type&
            > || std::is_invocable_r_v<
                typename __getattr__<Self, Name>::type,
                __getattr__<Self, Name>,
                Self,
                const typename __getattr__<Self, Name>::type&
            >
        )
    )
[[nodiscard]] auto getattr(
    Self&& self,
    const typename __getattr__<Self, Name>::type& default_value
) -> __getattr__<Self, Name>::type {
    if constexpr (DEBUG) {
        assert_(
            ptr(self) != nullptr,
            "Cannot get attribute '" + Name + "' from a null object."
        );
    }
    using Return = __getattr__<Self, Name>::type;
    if constexpr (std::is_invocable_v<__getattr__<Self, Name>, Self, const Return&>) {
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
template <StaticStr Name, impl::python Self, typename Value>
    requires (
        __setattr__<Self, Name, Value>::enable &&
        std::is_void_v<typename __setattr__<Self, Name, Value>::type> && (
            std::is_invocable_r_v<void, __setattr__<Self, Name, Value>, Self, Value> || (
                !std::is_invocable_v<__setattr__<Self, Name, Value>, Self, Value> &&
                impl::has_cpp<typename std::remove_cvref_t<typename __getattr__<Self, Name>::type>> &&
                std::is_assignable_v<typename std::remove_cvref_t<typename __getattr__<Self, Name>::type>&, Value>
            ) || (
                !std::is_invocable_v<__setattr__<Self, Name, Value>, Self, Value> &&
                !impl::has_cpp<typename std::remove_cvref_t<typename __getattr__<Self, Name>::type>>
            )
        )
    )
void setattr(Self&& self, Value&& value) {
    if constexpr (DEBUG) {
        assert_(
            ptr(self) != nullptr,
            "Cannot assign attribute '" + Name + "' on a null object."
        );
    }
    if constexpr (std::is_invocable_v<__setattr__<Self, Name, Value>, Self, Value>) {
        __setattr__<Self, Name, Value>{}(
            std::forward<Self>(self),
            std::forward<Value>(value)
        );

    } else {
        auto obj = to_python(std::forward<Value>(value));
        if constexpr (DEBUG) {
            assert_(
                ptr(obj) != nullptr,
                "Cannot assign attribute '" + Name + "' to a null object."
            );
        }
        PyObject* name = PyUnicode_FromStringAndSize(Name, Name.size());
        if (name == nullptr) {
            Exception::from_python();
        }
        int rc = PyObject_SetAttr(
            ptr(to_python(std::forward<Self>(self))),
            name,
            ptr(obj)
        );
        Py_DECREF(name);
        if (rc) {
            Exception::from_python();
        }
    }
}


/* Equivalent to Python `delattr(obj, name)` with a static attribute name. */
template <StaticStr Name, impl::python Self>
    requires (
        __delattr__<Self, Name>::enable && 
        std::is_void_v<typename __delattr__<Self, Name>::type> && (
            std::is_invocable_r_v<void, __delattr__<Self, Name>, Self> ||
            !std::is_invocable_v<__delattr__<Self, Name>, Self>
        )
    )
void delattr(Self&& self) {
    if constexpr (DEBUG) {
        assert_(
            ptr(self) != nullptr,
            "Cannot delete attribute '" + Name + "' on a null object."
        );
    }
    if constexpr (std::is_invocable_v<__delattr__<Self, Name>, Self>) {
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


/* Contains operator.  Equivalent to Python's `in` keyword, but as a freestanding
non-member function (i.e. `x in y` -> `py::in(x, y)`).  A member equivalent is defined
for all subclasses of `Object` (i.e. `x.in(y)`), which delegates to this function. */
template <typename Key, typename Container>
    requires (
        __contains__<Container, Key>::enable &&
        std::same_as<typename __contains__<Container, Key>::type, bool> && (
            std::is_invocable_r_v<bool, __contains__<Container, Key>, Container, Key> || (
                !std::is_invocable_v<__contains__<Container, Key>, Container, Key> &&
                impl::has_cpp<Container> &&
                impl::has_contains<impl::cpp_type<Container>, impl::cpp_type<Key>>
            ) || (
                !std::is_invocable_v<__contains__<Container, Key>, Container, Key> &&
                !impl::has_cpp<Container>
            )
        )
    )
[[nodiscard]] bool in(Key&& key, Container&& container) {
    if constexpr (std::is_invocable_v<__contains__<Container, Key>, Container, Key>) {
        return __contains__<Container, Key>{}(
            std::forward<Container>(container),
            std::forward<Key>(key)
        );

    } else if constexpr (impl::has_cpp<Container>) {
        return from_python(std::forward<Container>(container)).contains(
            from_python(std::forward<Key>(key))
        );

    } else {
        int result = PySequence_Contains(
            ptr(to_python(std::forward<Container>(container))),
            ptr(to_python(std::forward<Key>(key)))
        );
        if (result == -1) {
            Exception::from_python();
        }
        return result;
    }
}


/* Member equivalent for `py::in()` function, which simplifies the syntax if the
key is already a Python object */
template <typename Self, typename Key>
    requires (
        __contains__<Self, Key>::enable &&
        std::same_as<typename __contains__<Self, Key>::type, bool> && (
            std::is_invocable_r_v<bool, __contains__<Self, Key>, Self, Key> || (
                !std::is_invocable_v<__contains__<Self, Key>, Self, Key> &&
                impl::has_cpp<Self> &&
                impl::has_contains<impl::cpp_type<Self>, impl::cpp_type<Key>>
            ) || (
                !std::is_invocable_v<__contains__<Self, Key>, Self, Key> &&
                !impl::has_cpp<Self>
            )
        )
    )
[[nodiscard]] inline bool Object::in(this Self&& self, Key&& key) {
    return py::in(std::forward<Key>(key), std::forward<Self>(self));
}


/* Equivalent to Python `repr(obj)`, but returns a std::string and attempts to
represent C++ types using the stream insertion operator (<<) or std::to_string.  If all
else fails, falls back to demangling the result of typeid(obj).name(). */
template <typename Self>
    requires (!__repr__<Self>::enable || (
        std::convertible_to<typename __repr__<Self>::type, std::string> && (
            !std::is_invocable_v<__repr__<Self>> ||
            std::is_invocable_r_v<std::string, __repr__<Self>, Self>
        )
    ))
[[nodiscard]] std::string repr(Self&& obj) {
    if constexpr (std::is_invocable_r_v<std::string, __repr__<Self>, Self>) {
        return __repr__<Self>{}(std::forward<Self>(obj));

    } else if constexpr (impl::has_python<Self>) {
        PyObject* str = PyObject_Repr(
            ptr(to_python(std::forward<Self>(obj)))
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

    } else if constexpr (impl::has_to_string<Self>) {
        return std::to_string(std::forward<Self>(obj));

    } else if constexpr (impl::has_stream_insertion<Self>) {
        std::ostringstream stream;
        stream << std::forward<Self>(obj);
        return stream.str();

    } else {
        return
            "<" + type_name<Self> + " at " + std::to_string(
                reinterpret_cast<size_t>(&obj)
            ) + ">";
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
    requires (
        __len__<Self>::enable &&
        std::convertible_to<typename __len__<Self>::type, size_t> && (
            std::is_invocable_r_v<size_t, __len__<Self>, Self> || (
                !std::is_invocable_v<__len__<Self>, Self> &&
                impl::has_cpp<Self> &&
                impl::has_size<impl::cpp_type<Self>>
            ) || (
                !std::is_invocable_v<__len__<Self>, Self> &&
                !impl::has_cpp<Self>
            )
        )
    )
[[nodiscard]] size_t len(Self&& obj) {
    if constexpr (std::is_invocable_v<__len__<Self>, Self>) {
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
template <typename Self> requires (!__len__<Self>::enable && impl::has_size<Self>)
[[nodiscard]] size_t len(Self&& obj) {
    return std::ranges::size(std::forward<Self>(obj));
}


/* An alias for `py::len(obj)`, but triggers ADL for constructs that expect a
free-floating size() function. */
template <typename Self>
    requires (
        __len__<Self>::enable &&
        std::convertible_to<typename __len__<Self>::type, size_t> && (
            std::is_invocable_r_v<size_t , __len__<Self>, Self> || (
                !std::is_invocable_v<__len__<Self>, Self> &&
                impl::has_cpp<Self> &&
                impl::has_size<impl::cpp_type<Self>>
            ) || (
                !std::is_invocable_v<__len__<Self>, Self> &&
                !impl::has_cpp<Self>
            )
        )
    )
[[nodiscard]] size_t size(Self&& obj) {
    return len(std::forward<Self>(obj));
}


/* An alias for `py::len(obj)`, but triggers ADL for constructs that expect a
free-floating size() function. */
template <typename Self> requires (!__len__<Self>::enable && impl::has_size<Self>)
[[nodiscard]] size_t size(Self&& obj) {
    return len(std::forward<Self>(obj));
}


/* Equivalent to Python `abs(obj)` for any object that specializes the __abs__ control
struct. */
template <typename Self>
    requires (
        __abs__<Self>::enable &&
        std::convertible_to<typename __abs__<Self>::type, Object> && (
            std::is_invocable_r_v<typename __abs__<Self>::type, __abs__<Self>, Self> || (
                !std::is_invocable_v<__abs__<Self>, Self> &&
                impl::has_cpp<Self> &&
                impl::abs_returns<impl::cpp_type<Self>, typename __abs__<Self>::type>
            ) || (
                !std::is_invocable_v<__abs__<Self>, Self> &&
                !impl::has_cpp<Self> &&
                std::derived_from<typename __abs__<Self>::type, Object>
            )
        )
    )
[[nodiscard]] decltype(auto) abs(Self&& self) {
    if constexpr (std::is_invocable_v<__abs__<Self>, Self>) {
        return __abs__<Self>{}(std::forward<Self>(self));

    } else if constexpr (impl::has_cpp<Self>) {
        return std::abs(from_python(std::forward<Self>(self)));

    } else {
        using Return = std::remove_cvref_t<typename __abs__<Self>::type>;
        PyObject* result = PyNumber_Absolute(
            ptr(to_python(std::forward<Self>(self)))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Return>(result);
    }
}


/* Equivalent to Python `abs(obj)`, except that it takes a C++ value and applies
std::abs() for identical semantics. */
template <impl::has_abs Self>
    requires (!__abs__<Self>::enable && impl::abs_returns<Self, Object>)
[[nodiscard]] decltype(auto) abs(Self&& value) {
    return std::abs(std::forward<Self>(value));
}


template <impl::python Self> requires (!__invert__<Self>::enable)
decltype(auto) operator~(Self&& self) = delete;
template <impl::python Self>
    requires (
        __invert__<Self>::enable &&
        std::convertible_to<typename __invert__<Self>::type, Object> && (
            std::is_invocable_r_v<typename __invert__<Self>::type, __invert__<Self>, Self> || (
                !std::is_invocable_v<__invert__<Self>, Self> &&
                impl::has_cpp<Self> &&
                impl::invert_returns<impl::cpp_type<Self>, typename __invert__<Self>::type>
            ) || (
                !std::is_invocable_v<__invert__<Self>, Self> &&
                !impl::has_cpp<Self> &&
                std::derived_from<typename __invert__<Self>::type, Object>
            )
        )
    )
decltype(auto) operator~(Self&& self) {
    if constexpr (std::is_invocable_v<__invert__<Self>, Self>) {
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
    requires (
        __pos__<Self>::enable &&
        std::convertible_to<typename __pos__<Self>::type, Object> && (
            std::is_invocable_r_v<typename __pos__<Self>::type, __pos__<Self>, Self> || (
                !std::is_invocable_v<__pos__<Self>, Self> &&
                impl::has_cpp<Self> &&
                impl::pos_returns<impl::cpp_type<Self>, typename __pos__<Self>::type>
            ) || (
                !std::is_invocable_v<__pos__<Self>, Self> &&
                !impl::has_cpp<Self> &&
                std::derived_from<typename __pos__<Self>::type, Object>
            )
        )
    )
decltype(auto) operator+(Self&& self) {
    if constexpr (std::is_invocable_v<__pos__<Self>, Self>) {
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
    requires (
        __neg__<Self>::enable &&
        std::convertible_to<typename __neg__<Self>::type, Object> && (
            std::is_invocable_r_v<typename __neg__<Self>::type, __neg__<Self>, Self> || (
                !std::is_invocable_v<__neg__<Self>, Self> &&
                impl::has_cpp<Self> &&
                impl::neg_returns<impl::cpp_type<Self>, typename __neg__<Self>::type>
            ) || (
                !std::is_invocable_v<__neg__<Self>, Self> &&
                !impl::has_cpp<Self> &&
                std::derived_from<typename __neg__<Self>::type, Object>
            )
        )
    )
decltype(auto) operator-(Self&& self) {
    if constexpr (std::is_invocable_v<__neg__<Self>, Self>) {
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
decltype(auto) operator++(Self&& self, int) = delete;  // post-increment is not valid
template <impl::python Self> requires (!__increment__<Self>::enable)
decltype(auto) operator++(Self&& self) = delete;
template <impl::python Self>
    requires (
        __increment__<Self>::enable &&
        std::convertible_to<typename __increment__<Self>::type, Object> && (
            std::is_invocable_r_v<typename __increment__<Self>::type, __increment__<Self>, Self> || (
                !std::is_invocable_v<__increment__<Self>, Self> &&
                impl::has_cpp<Self> &&
                impl::preincrement_returns<impl::cpp_type<Self>, typename __increment__<Self>::type>
            ) || (
                !std::is_invocable_v<__increment__<Self>, Self> &&
                !impl::has_cpp<Self> &&
                std::same_as<typename __increment__<Self>::type, Self>
            )
        )
    )
decltype(auto) operator++(Self&& self) {
    if constexpr (std::is_invocable_v<__increment__<Self>, Self>) {
        return __increment__<Self>{}(std::forward<Self>(self));

    } else if constexpr (impl::has_cpp<Self>) {
        return ++from_python(std::forward<Self>(self));

    } else {
        using Return = std::remove_cvref_t<typename __increment__<Self>::type>;
        PyObject* one = PyLong_FromLong(1);
        if (one == nullptr) {
            Exception::from_python();
        }
        PyObject* result = PyNumber_InPlaceAdd(ptr(self), one);
        Py_DECREF(one);
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == ptr(self)) {
            Py_DECREF(result);
        } else {
            self = reinterpret_steal<Return>(result);
        }
        return std::forward<Self>(self);
    }
}


template <impl::python Self>
decltype(auto) operator--(Self& self, int) = delete;  // post-decrement is not valid
template <impl::python Self> requires (!__decrement__<Self>::enable)
decltype(auto) operator--(Self& self) = delete;
template <impl::python Self>
    requires (
        __decrement__<Self>::enable &&
        std::convertible_to<typename __decrement__<Self>::type, Object> && (
            std::is_invocable_r_v<typename __decrement__<Self>::type, __decrement__<Self>, Self> || (
                !std::is_invocable_v<__decrement__<Self>, Self> &&
                impl::has_cpp<Self> &&
                impl::predecrement_returns<impl::cpp_type<Self>, typename __decrement__<Self>::type>
            ) || (
                !std::is_invocable_v<__decrement__<Self>, Self> &&
                !impl::has_cpp<Self> &&
                std::same_as<typename __decrement__<Self>::type, Self>
            )
        )
    )
decltype(auto) operator--(Self&& self) {
    if constexpr (std::is_invocable_v<__decrement__<Self>, Self>) {
        return __decrement__<Self>{}(std::forward<Self>(self));

    } else if constexpr (impl::has_cpp<Self>) {
        return --from_python(std::forward<Self>(self));

    } else {
        using Return = std::remove_cvref_t<typename __decrement__<Self>::type>;
        PyObject* one = PyLong_FromLong(1);
        if (one == nullptr) {
            Exception::from_python();
        }
        PyObject* result = PyNumber_InPlaceSubtract(ptr(self), one);
        Py_DECREF(one);
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == ptr(self)) {
            Py_DECREF(result);
        } else {
            self = reinterpret_steal<Return>(result);
        }
        return std::forward<Self>(self);
    }
}


template <typename L, typename R>
    requires ((impl::python<L> || impl::python<R>) && !__lt__<L, R>::enable)
decltype(auto) operator<(L&& lhs, R&& rhs) = delete;
template <typename L, typename R>
    requires (
        __lt__<L, R>::enable &&
        std::convertible_to<typename __lt__<L, R>::type, Object> && (
            std::is_invocable_r_v<typename __lt__<L, R>::type, __lt__<L, R>, L, R> || (
                !std::is_invocable_v<__lt__<L, R>, L, R> &&
                (impl::has_cpp<L> && impl::has_cpp<R>) &&
                impl::lt_returns<impl::cpp_type<L>, impl::cpp_type<R>, typename __lt__<L, R>::type>
            ) || (
                !std::is_invocable_v<__lt__<L, R>, L, R> &&
                !(impl::has_cpp<L> && impl::has_cpp<R>) &&
                std::derived_from<typename __lt__<L, R>::type, Object>
            )
        )
    )
decltype(auto) operator<(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__lt__<L, R>, L, R>) {
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
    requires (
        __le__<L, R>::enable &&
        std::convertible_to<typename __le__<L, R>::type, Object> && (
            std::is_invocable_r_v<typename __le__<L, R>::type, __le__<L, R>, L, R> || (
                !std::is_invocable_v<__le__<L, R>, L, R> &&
                (impl::has_cpp<L> && impl::has_cpp<R>) &&
                impl::le_returns<impl::cpp_type<L>, impl::cpp_type<R>, typename __le__<L, R>::type>
            ) || (
                !std::is_invocable_v<__le__<L, R>, L, R> &&
                !(impl::has_cpp<L> && impl::has_cpp<R>) &&
                std::derived_from<typename __le__<L, R>::type, Object>
            )
        )
    )
decltype(auto) operator<=(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__le__<L, R>, L, R>) {
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
    requires (
        __eq__<L, R>::enable &&
        std::convertible_to<typename __eq__<L, R>::type, Object> && (
            std::is_invocable_r_v<typename __eq__<L, R>::type, __eq__<L, R>, L, R> || (
                !std::is_invocable_v<__eq__<L, R>, L, R> &&
                (impl::has_cpp<L> && impl::has_cpp<R>) &&
                impl::eq_returns<impl::cpp_type<L>, impl::cpp_type<R>, typename __eq__<L, R>::type>
            ) || (
                !std::is_invocable_v<__eq__<L, R>, L, R> &&
                !(impl::has_cpp<L> && impl::has_cpp<R>) &&
                std::derived_from<typename __eq__<L, R>::type, Object>
            )
        )
    )
decltype(auto) operator==(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__eq__<L, R>, L, R>) {
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
    requires (
        __ne__<L, R>::enable &&
        std::convertible_to<typename __ne__<L, R>::type, Object> && (
            std::is_invocable_r_v<typename __ne__<L, R>::type, __ne__<L, R>, L, R> || (
                !std::is_invocable_v<__ne__<L, R>, L, R> &&
                (impl::has_cpp<L> && impl::has_cpp<R>) &&
                impl::ne_returns<impl::cpp_type<L>, impl::cpp_type<R>, typename __ne__<L, R>::type>
            ) || (
                !std::is_invocable_v<__ne__<L, R>, L, R> &&
                !(impl::has_cpp<L> && impl::has_cpp<R>) &&
                std::derived_from<typename __ne__<L, R>::type, Object>
            )
        )
    )
decltype(auto) operator!=(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__ne__<L, R>, L, R>) {
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
    requires (
        __ge__<L, R>::enable &&
        std::convertible_to<typename __ge__<L, R>::type, Object> && (
            std::is_invocable_r_v<typename __ge__<L, R>::type, __ge__<L, R>, L, R> || (
                !std::is_invocable_v<__ge__<L, R>, L, R> &&
                (impl::has_cpp<L> && impl::has_cpp<R>) &&
                impl::ge_returns<impl::cpp_type<L>, impl::cpp_type<R>, typename __ge__<L, R>::type>
            ) || (
                !std::is_invocable_v<__ge__<L, R>, L, R> &&
                !(impl::has_cpp<L> && impl::has_cpp<R>) &&
                std::derived_from<typename __ge__<L, R>::type, Object>
            )
        )
    )
decltype(auto) operator>=(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__ge__<L, R>, L, R>) {
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
    requires (
        __gt__<L, R>::enable &&
        std::convertible_to<typename __gt__<L, R>::type, Object> && (
            std::is_invocable_r_v<typename __gt__<L, R>::type, __gt__<L, R>, L, R> || (
                !std::is_invocable_v<__gt__<L, R>, L, R> &&
                (impl::has_cpp<L> && impl::has_cpp<R>) &&
                impl::gt_returns<impl::cpp_type<L>, impl::cpp_type<R>, typename __gt__<L, R>::type>
            ) || (
                !std::is_invocable_v<__gt__<L, R>, L, R> &&
                !(impl::has_cpp<L> && impl::has_cpp<R>) &&
                std::derived_from<typename __gt__<L, R>::type, Object>
            )
        )
    )
decltype(auto) operator>(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__gt__<L, R>, L, R>) {
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
    requires (
        __add__<L, R>::enable &&
        std::convertible_to<typename __add__<L, R>::type, Object> && (
            std::is_invocable_r_v<typename __add__<L, R>::type, __add__<L, R>, L, R> || (
                !std::is_invocable_v<__add__<L, R>, L, R> &&
                (impl::has_cpp<L> && impl::has_cpp<R>) &&
                impl::add_returns<impl::cpp_type<L>, impl::cpp_type<R>, typename __add__<L, R>::type>
            ) || (
                !std::is_invocable_v<__add__<L, R>, L, R> &&
                !(impl::has_cpp<L> && impl::has_cpp<R>) &&
                std::derived_from<typename __add__<L, R>::type, Object>
            )
        )
    )
decltype(auto) operator+(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__add__<L, R>, L, R>) {
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
    requires (
        __iadd__<L, R>::enable &&
        !std::is_const_v<std::remove_reference_t<L>> &&
        std::convertible_to<typename __iadd__<L, R>::type, Object> && (
            std::is_invocable_r_v<typename __iadd__<L, R>::type, __iadd__<L, R>, L, R> || (
                !std::is_invocable_v<__iadd__<L, R>, L, R> &&
                (impl::has_cpp<L> && impl::has_cpp<R>) &&
                impl::iadd_returns<impl::cpp_type<L>, impl::cpp_type<R>, typename __iadd__<L, R>::type>
            ) || (
                !std::is_invocable_v<__iadd__<L, R>, L, R> &&
                !(impl::has_cpp<L> && impl::has_cpp<R>) &&
                impl::inherits<typename __iadd__<L, R>::type, L>
            )
        )
    )
decltype(auto) operator+=(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__iadd__<L, R>, L, R>) {
        return __iadd__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (impl::has_cpp<L> && impl::has_cpp<R>) {
        return from_python(std::forward<L>(lhs)) +=
            from_python(std::forward<R>(rhs));

    } else {
        using Return = std::remove_cvref_t<typename __iadd__<L, R>::type>;
        PyObject* result = PyNumber_InPlaceAdd(
            ptr(lhs),
            ptr(to_python(std::forward<R>(rhs)))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        Return out = reinterpret_steal<Return>(result);
        lhs = out;
        return out;
    }
}


template <typename L, typename R>
    requires ((impl::python<L> || impl::python<R>) && !__sub__<L, R>::enable)
decltype(auto) operator-(L&& lhs, R&& rhs) = delete;
template <typename L, typename R>
    requires (
        __sub__<L, R>::enable &&
        std::convertible_to<typename __sub__<L, R>::type, Object> && (
            std::is_invocable_r_v<typename __sub__<L, R>::type, __sub__<L, R>, L, R> || (
                !std::is_invocable_v<__sub__<L, R>, L, R> &&
                (impl::has_cpp<L> && impl::has_cpp<R>) &&
                impl::sub_returns<impl::cpp_type<L>, impl::cpp_type<R>, typename __sub__<L, R>::type>
            ) || (
                !std::is_invocable_v<__sub__<L, R>, L, R> &&
                !(impl::has_cpp<L> && impl::has_cpp<R>) &&
                std::derived_from<typename __sub__<L, R>::type, Object>
            )
        )
    )
decltype(auto) operator-(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__sub__<L, R>, L, R>) {
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
    requires (
        __isub__<L, R>::enable &&
        !std::is_const_v<std::remove_reference_t<L>> &&
        std::convertible_to<typename __isub__<L, R>::type, Object> && (
            std::is_invocable_r_v<typename __isub__<L, R>::type, __isub__<L, R>, L, R> || (
                !std::is_invocable_v<__isub__<L, R>, L, R> &&
                (impl::has_cpp<L> && impl::has_cpp<R>) &&
                impl::isub_returns<impl::cpp_type<L>, impl::cpp_type<R>, typename __isub__<L, R>::type>
            ) || (
                !std::is_invocable_v<__isub__<L, R>, L, R> &&
                !(impl::has_cpp<L> && impl::has_cpp<R>) &&
                impl::inherits<typename __isub__<L, R>::type, L>
            )
        )
    )
decltype(auto) operator-=(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__isub__<L, R>, L, R>) {
        return __isub__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (impl::has_cpp<L> && impl::has_cpp<R>) {
        return from_python(std::forward<L>(lhs)) -=
            from_python(std::forward<R>(rhs));

    } else {
        using Return = std::remove_cvref_t<typename __isub__<L, R>::type>;
        PyObject* result = PyNumber_InPlaceSubtract(
            ptr(lhs),
            ptr(to_python(std::forward<R>(rhs)))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        Return out = reinterpret_steal<Return>(result);
        lhs = out;
        return out;
    }
}


template <typename L, typename R>
    requires ((impl::python<L> || impl::python<R>) && !__mul__<L, R>::enable)
decltype(auto) operator*(L&& lhs, R&& rhs) = delete;
template <typename L, typename R>
    requires (
        __mul__<L, R>::enable &&
        std::convertible_to<typename __mul__<L, R>::type, Object> && (
            std::is_invocable_r_v<typename __mul__<L, R>::type, __mul__<L, R>, L, R> || (
                !std::is_invocable_v<__mul__<L, R>, L, R> &&
                (impl::has_cpp<L> && impl::has_cpp<R>) &&
                impl::mul_returns<impl::cpp_type<L>, impl::cpp_type<R>, typename __mul__<L, R>::type>
            ) || (
                !std::is_invocable_v<__mul__<L, R>, L, R> &&
                !(impl::has_cpp<L> && impl::has_cpp<R>) &&
                std::derived_from<typename __mul__<L, R>::type, Object>
            )
        )
    )
decltype(auto) operator*(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__mul__<L, R>, L, R>) {
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
    requires (
        __imul__<L, R>::enable &&
        !std::is_const_v<std::remove_reference_t<L>> &&
        std::convertible_to<typename __imul__<L, R>::type, Object> && (
            std::is_invocable_r_v<typename __imul__<L, R>::type, __imul__<L, R>, L, R> || (
                !std::is_invocable_v<__imul__<L, R>, L, R> &&
                (impl::has_cpp<L> && impl::has_cpp<R>) &&
                impl::imul_returns<impl::cpp_type<L>, impl::cpp_type<R>, typename __imul__<L, R>::type>
            ) || (
                !std::is_invocable_v<__imul__<L, R>, L, R> &&
                !(impl::has_cpp<L> && impl::has_cpp<R>) &&
                impl::inherits<typename __imul__<L, R>::type, L>
            )
        )
    )
decltype(auto) operator*=(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__imul__<L, R>, L, R>) {
        return __imul__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (impl::has_cpp<L> && impl::has_cpp<R>) {
        return from_python(std::forward<L>(lhs)) *=
            from_python(std::forward<R>(rhs));

    } else {
        using Return = std::remove_cvref_t<typename __imul__<L, R>::type>;
        PyObject* result = PyNumber_InPlaceMultiply(
            ptr(lhs),
            ptr(to_python(std::forward<R>(rhs)))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        Return out = reinterpret_steal<Return>(result);
        lhs = out;
        return out;
    }
}


/* Equivalent to Python `base ** exp` (exponentiation). */
template <typename Base, typename Exp>
    requires (
        __pow__<Base, Exp>::enable &&
        std::convertible_to<typename __pow__<Base, Exp>::type, Object> && (
            std::is_invocable_r_v<typename __pow__<Base, Exp>::type, __pow__<Base, Exp>, Base, Exp> || (
                !std::is_invocable_v<__pow__<Base, Exp>, Base, Exp> &&
                (impl::has_cpp<Base> && impl::has_cpp<Exp>) &&
                impl::pow_returns<impl::cpp_type<Base>, impl::cpp_type<Exp>, typename __pow__<Base, Exp>::type>
            ) && (
                !std::is_invocable_v<__pow__<Base, Exp>, Base, Exp> &&
                !(impl::has_cpp<Base> && impl::has_cpp<Exp>) &&
                std::derived_from<typename __pow__<Base, Exp>::type, Object>
            )
        )
    )
decltype(auto) pow(Base&& base, Exp&& exp) {
    if constexpr (std::is_invocable_v<__pow__<Base, Exp>, Base, Exp>) {
        return __pow__<Base, Exp>{}(std::forward<Base>(base), std::forward<Exp>(exp));

    } else if constexpr (impl::has_cpp<Base> && impl::has_cpp<Exp>) {
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
decltype(auto) pow(Base&& base, Exp&& exp) {
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
template <typename Base, typename Exp, typename Mod>
    requires (
        __pow__<Base, Exp, Mod>::enable &&
        std::convertible_to<typename __pow__<Base, Exp, Mod>::type, Object> && (
            std::is_invocable_r_v<
                typename __pow__<Base, Exp, Mod>::type,
                __pow__<Base, Exp, Mod>,
                Base,
                Exp,
                Mod
            > || (
                !std::is_invocable_v<__pow__<Base, Exp, Mod>, Base, Exp, Mod> &&
                std::derived_from<typename __pow__<Base, Exp, Mod>::type, Object>
            )
        )
    )
decltype(auto) pow(Base&& base, Exp&& exp, Mod&& mod) {
    if constexpr (std::is_invocable_v<__pow__<Base, Exp, Mod>, Base, Exp, Mod>) {
        return __pow__<Base, Exp, Mod>{}(
            std::forward<Base>(base),
            std::forward<Exp>(exp),
            std::forward<Mod>(mod)
        );

    } else {
        PyObject* result = PyNumber_Power(
            ptr(to_python(std::forward<Base>(base))),
            ptr(to_python(std::forward<Exp>(exp))),
            ptr(to_python(std::forward<Mod>(mod)))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<typename __pow__<Base, Exp, Mod>::type>(result);
    }
}


/* Equivalent to Python `pow(base, exp, mod)`, but works on C++ integers with identical
semantics. */
template <std::integral Base, std::integral Exp, std::integral Mod>
auto pow(Base base, Exp exp, Mod mod) {
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
    requires ((impl::python<L> || impl::python<R>) && !__truediv__<L, R>::enable)
decltype(auto) operator/(L&& lhs, R&& rhs) = delete;
template <typename L, typename R>
    requires (
        __truediv__<L, R>::enable &&
        std::convertible_to<typename __truediv__<L, R>::type, Object> && (
            std::is_invocable_r_v<typename __truediv__<L, R>::type, __truediv__<L, R>, L, R> || (
                !std::is_invocable_v<__truediv__<L, R>, L, R> &&
                (impl::has_cpp<L> && impl::has_cpp<R>) &&
                impl::truediv_returns<impl::cpp_type<L>, impl::cpp_type<R>, typename __truediv__<L, R>::type>
            ) || (
                !std::is_invocable_v<__truediv__<L, R>, L, R> &&
                !(impl::has_cpp<L> && impl::has_cpp<R>) &&
                std::derived_from<typename __truediv__<L, R>::type, Object>
            )
        )
    )
decltype(auto) operator/(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__truediv__<L, R>, L, R>) {
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
    requires (
        __itruediv__<L, R>::enable &&
        !std::is_const_v<std::remove_reference_t<L>> &&
        std::convertible_to<typename __itruediv__<L, R>::type, Object> && (
            std::is_invocable_r_v<typename __itruediv__<L, R>::type, __itruediv__<L, R>, L, R> || (
                !std::is_invocable_v<__itruediv__<L, R>, L, R> &&
                (impl::has_cpp<L> && impl::has_cpp<R>) &&
                impl::itruediv_returns<impl::cpp_type<L>, impl::cpp_type<R>, typename __itruediv__<L, R>::type>
            ) || (
                !std::is_invocable_v<__itruediv__<L, R>, L, R> &&
                !(impl::has_cpp<L> && impl::has_cpp<R>) &&
                impl::inherits<typename __itruediv__<L, R>::type, L>
            )
        )
    )
decltype(auto) operator/=(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__itruediv__<L, R>, L, R>) {
        return __itruediv__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (impl::has_cpp<L> && impl::has_cpp<R>) {
        return from_python(std::forward<L>(lhs)) /=
            from_python(std::forward<R>(rhs));

    } else {
        using Return = std::remove_cvref_t<typename __itruediv__<L, R>::type>;
        PyObject* result = PyNumber_InPlaceTrueDivide(
            ptr(lhs),
            ptr(to_python(std::forward<R>(rhs)))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        Return out = reinterpret_steal<Return>(result);
        lhs = out;
        return out;
    }
}


template <typename L, typename R>
    requires (
        __floordiv__<L, R>::enable &&
        std::convertible_to<typename __floordiv__<L, R>::type, Object> && (
            std::is_invocable_r_v<typename __floordiv__<L, R>::type, __floordiv__<L, R>, L, R> || (
                !std::is_invocable_v<__floordiv__<L, R>, L, R> &&
                std::derived_from<typename __floordiv__<L, R>::type, Object>
            )
        )
    )
decltype(auto) floordiv(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__floordiv__<L, R>, L, R>) {
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
    requires (
        __ifloordiv__<L, R>::enable &&
        std::convertible_to<typename __ifloordiv__<L, R>::type, Object> && (
            std::is_invocable_r_v<typename __ifloordiv__<L, R>::type, __ifloordiv__<L, R>, L, R> || (
                !std::is_invocable_v<__ifloordiv__<L, R>, L, R> &&
                std::derived_from<typename __ifloordiv__<L, R>::type, Object>
            )
        )
    )
decltype(auto) ifloordiv(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__ifloordiv__<L, R>, L, R>) {
        return __ifloordiv__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else {
        using Return = std::remove_cvref_t<typename __ifloordiv__<L, R>::type>;
        PyObject* result = PyNumber_InPlaceFloorDivide(
            ptr(lhs),
            ptr(to_python(std::forward<R>(rhs)))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Return>(result);
    }
}


template <typename L, typename R>
    requires ((impl::python<L> || impl::python<R>) && !__mod__<L, R>::enable)
decltype(auto) operator%(L&& lhs, R&& rhs) = delete;
template <typename L, typename R>
    requires (
        __mod__<L, R>::enable &&
        std::convertible_to<typename __mod__<L, R>::type, Object> && (
            std::is_invocable_r_v<typename __mod__<L, R>::type, __mod__<L, R>, L, R> || (
                !std::is_invocable_v<__mod__<L, R>, L, R> &&
                (impl::has_cpp<L> && impl::has_cpp<R>) &&
                impl::mod_returns<impl::cpp_type<L>, impl::cpp_type<R>, typename __mod__<L, R>::type>
            ) || (
                !std::is_invocable_v<__mod__<L, R>, L, R> &&
                !(impl::has_cpp<L> && impl::has_cpp<R>) &&
                std::derived_from<typename __mod__<L, R>::type, Object>
            )
        )
    )
decltype(auto) operator%(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__mod__<L, R>, L, R>) {
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
    requires (
        __imod__<L, R>::enable &&
        !std::is_const_v<std::remove_reference_t<L>> &&
        std::convertible_to<typename __imod__<L, R>::type, Object> && (
            std::is_invocable_r_v<typename __imod__<L, R>::type, __imod__<L, R>, L, R> || (
                !std::is_invocable_v<__imod__<L, R>, L, R> &&
                (impl::has_cpp<L> && impl::has_cpp<R>) &&
                impl::imod_returns<impl::cpp_type<L>, impl::cpp_type<R>, typename __imod__<L, R>::type>
            ) || (
                !std::is_invocable_v<__imod__<L, R>, L, R> &&
                !(impl::has_cpp<L> && impl::has_cpp<R>) &&
                impl::inherits<typename __imod__<L, R>::type, L>
            )
        )
    )
decltype(auto) operator%=(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__imod__<L, R>, L, R>) {
        return __imod__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (impl::has_cpp<L> && impl::has_cpp<R>) {
        return from_python(std::forward<L>(lhs)) %=
            from_python(std::forward<R>(rhs));

    } else {
        using Return = std::remove_cvref_t<typename __imod__<L, R>::type>;
        PyObject* result = PyNumber_InPlaceRemainder(
            ptr(lhs),
            ptr(to_python(std::forward<R>(rhs)))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        Return out = reinterpret_steal<Return>(result);
        lhs = out;
        return out;
    }
}


template <typename L, typename R>
    requires ((impl::python<L> || impl::python<R>) && !__lshift__<L, R>::enable)
decltype(auto) operator<<(L&& lhs, R&& rhs) = delete;
template <typename L, typename R>
    requires (
        __lshift__<L, R>::enable &&
        std::convertible_to<typename __lshift__<L, R>::type, Object> && (
            std::is_invocable_r_v<typename __lshift__<L, R>::type, __lshift__<L, R>, L, R> || (
                !std::is_invocable_v<__lshift__<L, R>, L, R> &&
                (impl::has_cpp<L> && impl::has_cpp<R>) &&
                impl::lshift_returns<impl::cpp_type<L>, impl::cpp_type<R>, typename __lshift__<L, R>::type>
            ) || (
                !std::is_invocable_v<__lshift__<L, R>, L, R> &&
                !(impl::has_cpp<L> && impl::has_cpp<R>) &&
                std::derived_from<typename __lshift__<L, R>::type, Object>
            )
        )
    )
decltype(auto) operator<<(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__lshift__<L, R>, L, R>) {
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
    requires (
        __ilshift__<L, R>::enable &&
        std::convertible_to<typename __ilshift__<L, R>::type, Object> && (
            std::is_invocable_r_v<typename __ilshift__<L, R>::type, __ilshift__<L, R>, L, R> || (
                !std::is_invocable_v<__ilshift__<L, R>, L, R> &&
                (impl::has_cpp<L> && impl::has_cpp<R>) &&
                impl::ilshift_returns<impl::cpp_type<L>, impl::cpp_type<R>, typename __ilshift__<L, R>::type>
            ) || (
                !std::is_invocable_v<__ilshift__<L, R>, L, R> &&
                !(impl::has_cpp<L> && impl::has_cpp<R>) &&
                std::derived_from<typename __ilshift__<L, R>::type, Object>
            )
        )
    )
decltype(auto) operator<<=(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__ilshift__<L, R>, L, R>) {
        return __ilshift__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (impl::has_cpp<L> && impl::has_cpp<R>) {
        return from_python(std::forward<L>(lhs)) <<=
            from_python(std::forward<R>(rhs));

    } else {
        using Return = std::remove_cvref_t<typename __ilshift__<L, R>::type>;
        PyObject* result = PyNumber_InPlaceLshift(
            ptr(lhs),
            ptr(to_python(std::forward<R>(rhs)))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Return>(result);
    }
}


template <typename L, typename R>
    requires ((impl::python<L> || impl::python<R>) && !__rshift__<L, R>::enable)
decltype(auto) operator>>(L&& lhs, R&& rhs) = delete;
template <typename L, typename R>
    requires (
        __rshift__<L, R>::enable &&
        std::convertible_to<typename __rshift__<L, R>::type, Object> && (
            std::is_invocable_r_v<typename __rshift__<L, R>::type, __rshift__<L, R>, L, R> || (
                !std::is_invocable_v<__rshift__<L, R>, L, R> &&
                (impl::has_cpp<L> && impl::has_cpp<R>) &&
                impl::rshift_returns<impl::cpp_type<L>, impl::cpp_type<R>, typename __rshift__<L, R>::type>
            ) || (
                !std::is_invocable_v<__rshift__<L, R>, L, R> &&
                !(impl::has_cpp<L> && impl::has_cpp<R>) &&
                std::derived_from<typename __rshift__<L, R>::type, Object>
            )
        )
    )
decltype(auto) operator>>(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__rshift__<L, R>, L, R>) {
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
    requires (
        __irshift__<L, R>::enable &&
        std::convertible_to<typename __irshift__<L, R>::type, Object> && (
            std::is_invocable_r_v<typename __irshift__<L, R>::type, __irshift__<L, R>, L, R> || (
                !std::is_invocable_v<__irshift__<L, R>, L, R> &&
                (impl::has_cpp<L> && impl::has_cpp<R>) &&
                impl::irshift_returns<impl::cpp_type<L>, impl::cpp_type<R>, typename __irshift__<L, R>::type>
            ) || (
                !std::is_invocable_v<__irshift__<L, R>, L, R> &&
                !(impl::has_cpp<L> && impl::has_cpp<R>) &&
                std::derived_from<typename __irshift__<L, R>::type, Object>
            )
        )
    )
decltype(auto) operator>>=(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__irshift__<L, R>, L, R>) {
        return __irshift__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (impl::has_cpp<L> && impl::has_cpp<R>) {
        return from_python(std::forward<L>(lhs)) >>=
            from_python(std::forward<R>(rhs));

    } else {
        using Return = std::remove_cvref_t<typename __irshift__<L, R>::type>;
        PyObject* result = PyNumber_InPlaceRshift(
            ptr(lhs),
            ptr(to_python(std::forward<R>(rhs)))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Return>(result);
    }
}


template <typename L, typename R>
    requires ((impl::python<L> || impl::python<R>) && !__and__<L, R>::enable)
decltype(auto) operator&(L&& lhs, R&& rhs) = delete;
template <typename L, typename R>
    requires (
        __and__<L, R>::enable &&
        std::convertible_to<typename __and__<L, R>::type, Object> && (
            std::is_invocable_r_v<typename __and__<L, R>::type, __and__<L, R>, L, R> || (
                !std::is_invocable_v<__and__<L, R>, L, R> &&
                (impl::has_cpp<L> && impl::has_cpp<R>) &&
                impl::and_returns<impl::cpp_type<L>, impl::cpp_type<R>, typename __and__<L, R>::type>
            ) || (
                !std::is_invocable_v<__and__<L, R>, L, R> &&
                !(impl::has_cpp<L> && impl::has_cpp<R>) &&
                std::derived_from<typename __and__<L, R>::type, Object>
            )
        )
    )
decltype(auto) operator&(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__and__<L, R>, L, R>) {
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
    requires (
        __iand__<L, R>::enable &&
        !std::is_const_v<std::remove_reference_t<L>> &&
        std::convertible_to<typename __iand__<L, R>::type, Object> && (
            std::is_invocable_r_v<typename __iand__<L, R>::type, __iand__<L, R>, L, R> || (
                !std::is_invocable_v<__iand__<L, R>, L, R> &&
                (impl::has_cpp<L> && impl::has_cpp<R>) &&
                impl::iand_returns<impl::cpp_type<L>, impl::cpp_type<R>, typename __iand__<L, R>::type>
            ) || (
                !std::is_invocable_v<__iand__<L, R>, L, R> &&
                !(impl::has_cpp<L> && impl::has_cpp<R>) &&
                impl::inherits<typename __iand__<L, R>::type, L>
            )
        )
    )
decltype(auto) operator&=(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__iand__<L, R>, L, R>) {
        return __iand__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (impl::has_cpp<L> && impl::has_cpp<R>) {
        return from_python(std::forward<L>(lhs)) &=
            from_python(std::forward<R>(rhs));

    } else {
        using Return = std::remove_cvref_t<typename __iand__<L, R>::type>;
        PyObject* result = PyNumber_InPlaceAnd(
            ptr(lhs),
            ptr(to_python(std::forward<R>(rhs)))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        Return out = reinterpret_steal<Return>(result);
        lhs = out;
        return out;
    }
}


template <typename L, typename R>
    requires ((impl::python<L> || impl::python<R>) && !__or__<L, R>::enable)
decltype(auto) operator|(L&& lhs, R&& rhs) = delete;
template <typename L, typename R>
    requires (
        __or__<L, R>::enable &&
        std::convertible_to<typename __or__<L, R>::type, Object> && (
            std::is_invocable_r_v<typename __or__<L, R>::type, __or__<L, R>, L, R> || (
                !std::is_invocable_v<__or__<L, R>, L, R> &&
                (impl::has_cpp<L> && impl::has_cpp<R>) &&
                impl::or_returns<impl::cpp_type<L>, impl::cpp_type<R>, typename __or__<L, R>::type>
            ) || (
                !std::is_invocable_v<__or__<L, R>, L, R> &&
                !(impl::has_cpp<L> && impl::has_cpp<R>) &&
                std::derived_from<typename __or__<L, R>::type, Object>
            )
        )
    )
decltype(auto) operator|(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__or__<L, R>, L, R>) {
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
    requires (
        __ior__<L, R>::enable &&
        !std::is_const_v<std::remove_reference_t<L>> &&
        std::convertible_to<typename __ior__<L, R>::type, Object> && (
            std::is_invocable_r_v<typename __ior__<L, R>::type, __ior__<L, R>, L, R> || (
                !std::is_invocable_v<__ior__<L, R>, L, R> &&
                (impl::has_cpp<L> && impl::has_cpp<R>) &&
                impl::ior_returns<impl::cpp_type<L>, impl::cpp_type<R>, typename __ior__<L, R>::type>
            ) || (
                !std::is_invocable_v<__ior__<L, R>, L, R> &&
                !(impl::has_cpp<L> && impl::has_cpp<R>) &&
                impl::inherits<typename __ior__<L, R>::type, L>
            )
        )
    )
decltype(auto) operator|=(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__ior__<L, R>, L, R>) {
        return __ior__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (impl::has_cpp<L> && impl::has_cpp<R>) {
        return from_python(std::forward<L>(lhs)) |=
            from_python(std::forward<R>(rhs));

    } else {
        using Return = std::remove_cvref_t<typename __ior__<L, R>::type>;
        PyObject* result = PyNumber_InPlaceOr(
            ptr(lhs),
            ptr(to_python(std::forward<R>(rhs)))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        Return out = reinterpret_steal<Return>(result);
        lhs = out;
        return out;
    }
}


template <typename L, typename R>
    requires ((impl::python<L> || impl::python<R>) && !__xor__<L, R>::enable)
decltype(auto) operator^(L&& lhs, R&& rhs) = delete;
template <typename L, typename R>
    requires (
        __xor__<L, R>::enable &&
        std::convertible_to<typename __xor__<L, R>::type, Object> && (
            std::is_invocable_r_v<typename __xor__<L, R>::type, __xor__<L, R>, L, R> || (
                !std::is_invocable_v<__xor__<L, R>, L, R> &&
                (impl::has_cpp<L> && impl::has_cpp<R>) &&
                impl::xor_returns<impl::cpp_type<L>, impl::cpp_type<R>, typename __xor__<L, R>::type>
            ) || (
                !std::is_invocable_v<__xor__<L, R>, L, R> &&
                !(impl::has_cpp<L> && impl::has_cpp<R>) &&
                std::derived_from<typename __xor__<L, R>::type, Object>
            )
        )
    )
decltype(auto) operator^(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__xor__<L, R>, L, R>) {
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
    requires (
        __ixor__<L, R>::enable &&
        !std::is_const_v<std::remove_reference_t<L>> &&
        std::convertible_to<typename __ixor__<L, R>::type, Object> && (
            std::is_invocable_r_v<typename __ixor__<L, R>::type, __ixor__<L, R>, L, R> || (
                !std::is_invocable_v<__ixor__<L, R>, L, R> &&
                (impl::has_cpp<L> && impl::has_cpp<R>) &&
                impl::ixor_returns<impl::cpp_type<L>, impl::cpp_type<R>, typename __ixor__<L, R>::type>
            ) || (
                !std::is_invocable_v<__ixor__<L, R>, L, R> &&
                !(impl::has_cpp<L> && impl::has_cpp<R>) &&
                impl::inherits<typename __ixor__<L, R>::type, L>
            )
        )
    )
decltype(auto) operator^=(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__ixor__<L, R>, L, R>) {
        return __ixor__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (impl::has_cpp<L> && impl::has_cpp<R>) {
        return from_python(std::forward<L>(lhs)) ^=
            from_python(std::forward<R>(rhs));

    } else {
        using Return = std::remove_cvref_t<typename __ixor__<L, R>::type>;
        PyObject* result = PyNumber_InPlaceXor(
            ptr(lhs),
            ptr(to_python(std::forward<R>(rhs)))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        Return out = reinterpret_steal<Return>(result);
        lhs = out;
        return out;
    }
}


}  // namespace py


namespace std {

    template <py::impl::python T>
        requires (py::__hash__<T>::enable && (
            std::is_invocable_r_v<size_t, py::__hash__<T>, T> ||
            (
                !std::is_invocable_v<py::__hash__<T>, T> &&
                py::impl::has_cpp<T> && py::impl::hashable<py::impl::cpp_type<T>>
            ) || (
                !std::is_invocable_v<py::__hash__<T>, T> &&
                !py::impl::has_cpp<T>
            )
        ))
    struct hash<T> {
        static constexpr size_t operator()(T obj) {
            if constexpr (std::is_invocable_v<py::__hash__<T>, T>) {
                return py::__hash__<T>{}(std::forward<T>(obj));

            } else if constexpr (py::impl::has_cpp<T>) {
                return py::hash(py::from_python(std::forward<T>(obj)));

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
