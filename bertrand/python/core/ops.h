#ifndef BERTRAND_PYTHON_CORE_OPS_H
#define BERTRAND_PYTHON_CORE_OPS_H

#include "declarations.h"
#include "object.h"
#include "except.h"

// TODO: define this file last?


namespace py {


namespace impl {
    static PyObject* one = (Interpreter::init(), PyLong_FromLong(1));  // immortal


    /// TODO: See if there's a way to merge this with the __getitem__ control structure
    /// similar to __iter__?

    /* A proxy for an item in a Python container that is controlled by the
    `__getitem__`, `__setitem__`, and `__delitem__` control structs.

    This is a simple extension of an Object type that intercepts `operator=` and
    assigns the new value back to the container using the appropriate API.  Mutating
    the object in any other way will also modify it in-place within the container. */
    template <typename Container, typename... Key>
        requires (__getitem__<Container, Key...>::enable)
    struct Item : __getitem__<Container, Key...>::type {
    private:
        using Base = __getitem__<Container, Key...>::type;
        using M_Key = std::conditional_t<
            (sizeof...(Key) > 1),
            std::tuple<Key...>,
            std::tuple_element_t<0, std::tuple<Key...>>
        >;

        Container m_container;
        M_Key m_key;

    public:

        Item(Base&& item, Container container, Key&&... key) :
            Base(std::move(item)), m_container(container),
            m_key(std::forward<Key>(key)...)
        {}
        Item(const Item& other) :
            Base(other), m_container(other.m_container), m_key(other.m_key)
        {}
        Item(Item&& other) :
            Base(std::move(other)), m_container(std::move(other.m_container)),
            m_key(std::move(other.m_key))
        {}

        template <typename Value>
            requires (__setitem__<Container, std::remove_cvref_t<Value>, Key...>::enable)
        Item& operator=(Value&& value) {
            using setitem = __setitem__<Container, std::remove_cvref_t<Value>, Key...>;
            using Return = typename setitem::type;
            static_assert(
                std::is_void_v<Return>,
                "index assignment operator must return void.  Check your "
                "specialization of __setitem__ for these types and ensure the Return "
                "type is set to void."
            );
            Base::operator=(std::forward<Value>(value));
            if constexpr (impl::has_call_operator<setitem>) {
                setitem{}(m_container, m_key, value);

            } else if constexpr (
                impl::originates_from_cpp<Base> &&
                (impl::cpp_or_originates_from_cpp<Key> && ...) &&
                impl::cpp_or_originates_from_cpp<std::remove_cvref_t<Value>>
            ) {
                static_assert(
                    impl::supports_item_assignment<Base, std::remove_cvref_t<Value>, Key...>,
                    "__setitem__<Self, Value, Key...> is enabled for operands whose C++ "
                    "representations have no viable overload for `Self[Key...] = Value`"
                );
                if constexpr (impl::python_like<std::remove_cvref_t<Value>>) {
                    unwrap(*this) = unwrap(std::forward<Value>(value));
                } else {
                    unwrap(*this) = std::forward<Value>(value);
                }

            } else {
                if (PyObject_SetItem(
                    m_container.m_ptr,
                    ptr(as_object(m_key)),
                    ptr(*this)
                )) {
                    Exception::from_python();
                }
            }
            return *this;
        }

        template <typename = void> requires (__delitem__<Container, Key...>::enable)
        Item& operator=(del value) {
            using delitem = __delitem__<Container, Key...>;
            using Return = typename delitem::type;
            static_assert(
                std::is_void_v<Return>,
                "index deletion operator must return void.  Check your specialization "
                "of __delitem__ for these types and ensure the Return type is set to "
                "void."
            );
            if constexpr (impl::has_call_operator<delitem>) {
                delitem{}(m_container, m_key);

            } else {
                if (PyObject_DelItem(m_container.m_ptr, ptr(as_object(m_key)))) {
                    Exception::from_python();
                }
            }
            return *this;
        }

        template <typename Value>
            requires (
                !__setitem__<Container, std::remove_cvref_t<Value>, Key...>::enable &&
                !__delitem__<Container, Key...>::enable
            )
        Item& operator=(Value&& other) = delete;

    };

}


template <typename Self, typename... Key> requires (__getitem__<Self, Key...>::enable)
decltype(auto) Handle::operator[](this const Self& self, Key&&... key) {
    using Return = typename __getitem__<Self, std::decay_t<Key>...>::type;
    static_assert(
        std::derived_from<Return, Object>,
        "index operator must return a subclass of py::Object.  Check your "
        "specialization of __getitem__ for these types and ensure the Return "
        "type is set to a subclass of py::Object."
    );
    if constexpr (impl::has_call_operator<__getitem__<Self, std::decay_t<Key>...>>) {
        return impl::Item<Self, std::decay_t<Key>...>(
            __getitem__<Self, std::decay_t<Key>...>{}(self, key...),
            self,
            std::forward<Key>(key)...
        );

    } else if constexpr (
        impl::originates_from_cpp<Self> &&
        (impl::cpp_or_originates_from_cpp<std::decay_t<Key>> && ...)
    ) {
        static_assert(
            impl::lookup_yields<impl::cpp_type<Self>, Return, std::decay_t<Key>...>,
            "__getitem__<Self, Key...> is enabled for operands whose C++ "
            "representations have no viable overload for `Self[Key...]`"
        );
        return unwrap(self)[std::forward<Key>(key)...];

    } else {
        if constexpr (sizeof...(Key) > 1) {
            PyObject* tuple = PyTuple_Pack(sizeof...(Key), ptr(as_object(key))...);
            if (tuple == nullptr) {
                Exception::from_python();
            }
            PyObject* result = PyObject_GetItem(self.m_ptr, tuple);
            Py_DECREF(tuple);
            if (result == nullptr) {
                Exception::from_python();
            }
            return impl::Item<Self, std::decay_t<Key>...>(
                reinterpret_steal<Return>(result),
                self,
                std::forward<Key>(key)...
            );

        } else {
            PyObject* result = PyObject_GetItem(self.m_ptr, ptr(as_object(key))...);
            if (result == nullptr) {
                Exception::from_python();
            }
            return impl::Item<Self, std::decay_t<Key>...>(
                reinterpret_steal<Return>(result),
                self,
                std::forward<Key>(key)...
            );
        }
    }
}


/* Represents a keyword parameter pack obtained by double-dereferencing a Python
object. */
template <std::derived_from<Object> T> requires (impl::mapping_like<T>)
struct KwargPack {
    using key_type = T::key_type;
    using mapped_type = T::mapped_type;

    T value;

private:

    static constexpr bool can_iterate =
        impl::yields_pairs_with<T, key_type, mapped_type> ||
        impl::has_items<T> ||
        (impl::has_keys<T> && impl::has_values<T>) ||
        (impl::yields<T, key_type> && impl::lookup_yields<T, key_type, mapped_type>) ||
        (impl::has_keys<T> && impl::lookup_yields<T, key_type, mapped_type>);

    auto transform() const {
        if constexpr (impl::yields_pairs_with<T, key_type, mapped_type>) {
            return value;

        } else if constexpr (impl::has_items<T>) {
            return value.items();

        } else if constexpr (impl::has_keys<T> && impl::has_values<T>) {
            return std::ranges::views::zip(value.keys(), value.values());

        } else if constexpr (
            impl::yields<T, key_type> && impl::lookup_yields<T, key_type, mapped_type>
        ) {
            return std::ranges::views::transform(value, [&](const key_type& key) {
                return std::make_pair(key, value[key]);
            });

        } else {
            return std::ranges::views::transform(value.keys(), [&](const key_type& key) {
                return std::make_pair(key, value[key]);
            });
        }
    }

public:

    template <typename U = T> requires (can_iterate)
    auto begin() const { return std::ranges::begin(transform()); }
    template <typename U = T> requires (can_iterate)
    auto cbegin() const { return begin(); }
    template <typename U = T> requires (can_iterate)
    auto end() const { return std::ranges::end(transform()); }
    template <typename U = T> requires (can_iterate)
    auto cend() const { return end(); }

};


/* Represents a positional parameter pack obtained by dereferencing a Python
object. */
template <std::derived_from<Object> T> requires (impl::iterable<T>)
struct ArgPack {
    T value;

    auto begin() const { return std::ranges::begin(value); }
    auto cbegin() const { return begin(); }
    auto end() const { return std::ranges::end(value); }
    auto cend() const { return end(); }

    template <typename U = T> requires (impl::mapping_like<U>)
    auto operator*() const {
        return KwargPack<U>{value};
    }        
};


template <std::derived_from<Object> Container> requires (impl::iterable<Container>)
[[nodiscard]] auto operator*(const Container& self) {
    return ArgPack<Container>{self};
}


template <std::derived_from<Object> Container, std::ranges::view View>
    requires (impl::iterable<Container>)
[[nodiscard]] auto operator->*(const Container& container, const View& view) {
    return std::views::all(container) | view;
}


template <std::derived_from<Object> Container, typename Func>
    requires (
        !std::ranges::view<Func> &&
        impl::iterable<Container> &&
        std::is_invocable_v<Func, impl::iter_type<Container>>
    )
[[nodiscard]] auto operator->*(const Container& container, Func func) {
    using Return = std::invoke_result_t<Func, impl::iter_type<Container>>;
    if constexpr (impl::is_optional<Return>) {
        return (
            std::views::all(container) |
            std::views::transform(func) |
            std::views::filter([](const Return& value) {
                return value.has_value();
            }) | std::views::transform([](const Return& value) {
            return value.value();
            })
        );
    } else {
        return std::views::all(container) | std::views::transform(func);
    }
}


template <std::derived_from<Object> T>
struct __as_object__<T>                                     : Returns<T> {
    static decltype(auto) operator()(T&& value) { return std::forward<T>(value); }
};


/* Convert an arbitrary C++ value to an equivalent Python object if it isn't one
already. */
template <typename T> requires (__as_object__<std::remove_cvref_t<T>>::enable)
[[nodiscard]] decltype(auto) as_object(T&& value) {
    using Obj = __as_object__<std::remove_cvref_t<T>>;
    static_assert(
        !std::same_as<typename Obj::type, Object>,
        "C++ types cannot be converted to py::Object directly.  Check your "
        "specialization of __as_object__ for this type and ensure the Return type "
        "is set to a subclass of py::Object, not py::Object itself."
    );
    if constexpr (impl::has_call_operator<Obj>) {
        return Obj{}(std::forward<T>(value));
    } else {
        return typename Obj::type(std::forward<T>(value));
    }
}


/// TODO: __isinstance__ and __issubclass__ should test for inheritance from a Python
/// object's `Interface<T>` type, not the type itself.  That allows it to correctly
/// model multiple inheritance.


// TODO: implement the extra overloads for Object, BertrandMeta, Type, and Tuple of
// types/generic objects


// TODO: there also might be an issue with infinite recursion during automated
// isinstance() calls when converting Object to one of its subclasses, if that
// conversion occurs within the logic of isinstance() itself.


// TODO: update docs to better reflect the actual behavior of each overload.
//
//  isinstance<Base>(obj): checks if obj can be safely converted to Base when narrowing
//      a dynamic type.
//  isinstance(obj, base): equivalent to a Python-level isinstance() check.  Base must
//      be type-like, a union of types, or a dynamic object which can be narrowed to
//      either of the above.
//  issubclass<Base, Derived>(): does a compile-time check to see if Derived inherits
//      from Base.
//  issubclass<Base>(obj): devolves to an issubclass<Base, Derived>() check unless obj
//      is a dynamic object which may be narrowed to a single type.
//  issubclass(obj, base): equivalent to a Python-level issubclass() check.  Derived
//      must be a single type or a dynamic object which can be narrowed to a single
//      type, and base must be type-like, a union of types, or a dynamic object which
//      can be narrowed to either of the above.



/* Equivalent to Python `isinstance(obj, base)`, except that base is given as a
template parameter.  If a specialization of `__isinstance__` exists and implements a
call operator that takes a single `const Derived&` argument, then it will be called
directly.  Otherwise, if the argument is a dynamic object, it falls back to a
Python-level `isinstance()` check.  In all other cases, it will be evaluated at
compile-time by calling `issubclass<Derived, Base>()`. */
template <typename Base, typename Derived>
[[nodiscard]] constexpr bool isinstance(const Derived& obj) {
    if constexpr (std::is_invocable_v<
        __isinstance__<Derived, Base>,
        const Derived&
    >) {
        return __isinstance__<Derived, Base>{}(obj);

    } else if constexpr (impl::python_like<Base> && impl::dynamic_type<Derived>) {
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
`__issubclass__`.  By default, this is only done for `Type`, `BertrandMeta`, `Object`,
and `Tuple` as right-hand arguments. */
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


// TODO: isinstance() seems to work, but issubclass() is still complicated


/* Equivalent to Python `issubclass(obj, base)`, except that both arguments are given
as template parameters, marking the check as `consteval`.  This is essentially
equivalent to a `std::derived_from<>` check, except that custom logic from the
`__issubclass__` control structure will be used if available. */
template <typename Derived, typename Base>
[[nodiscard]] consteval bool issubclass() {
    if constexpr (std::is_invocable_v<__issubclass__<Derived, Base>>) {
        return __issubclass__<Derived, Base>{}();
    } else {
        return std::derived_from<Derived, Base>;
    }
}


/* Equivalent to Python `issubclass(obj, base)`, except that the base is given as a
template parameter, marking the check as `constexpr`.  This overload must be explicitly
enabled by defining a one-argument call operator in a specialization of the
`__issubclass__` control structure.  By default, this is only done for `Type`,
`BertrandMeta`, and `Object` as left-hand arguments, as well as `Type`, `BertrandMeta`,
`Object`, and `Tuple` as right-hand arguments. */
template <std::derived_from<Object> Base, std::derived_from<Object> Derived>
[[nodiscard]] constexpr bool issubclass(const Derived& obj) {
    if constexpr (std::is_invocable_v<__issubclass__<Derived, Base>, const Derived&>) {
        return __issubclass__<Derived, Base>{}(obj);

    } else if constexpr (impl::dynamic_type<Derived>) {
        return PyType_Check(ptr(obj)) && PyObject_IsSubclass(
            ptr(obj),
            ptr(Type<Base>())  // TODO: correct?
        );

    } else if constexpr (impl::type_like<Derived>) {
        return issubclass<Derived, Base>();

    } else {
        return false;
    }
}


/* Equivalent to Python `issubclass(obj, base)`.  This overload must be explicitly
enabled by defining a two-argument call operator in a specialization of
`__issubclass__`.  By default, this is only done for `Type`, `BertrandMeta`, and
`Object`, as left-hand arguments, as well as `Type`, `BertrandMeta`, `Object`, and
`Tuple` as right-hand arguments. */
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
        } catch (...) {
            return typeid(obj).name();
        }
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
        "size() operator must return a size_t for compatibility with C++ "
        "containers.  Check your specialization of __len__ for these types "
        "and ensure the Return type is set to size_t."
    );
    if constexpr (impl::has_call_operator<__len__<T>>) {
        return __len__<T>{}(obj);

    } else if constexpr (impl::cpp_or_originates_from_cpp<T>) {
        static_assert(
            impl::has_size<impl::cpp_type<T>>,
            "__len__<T> is enabled for a type whose C++ representation does not have "
            "a viable overload for `std::size(T)`"
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
[[nodiscard]] auto abs(const Self& self) {
    using Return = __abs__<Self>::type;
    static_assert(
        std::derived_from<Return, Object>,
        "Absolute value operator must return a py::Object subclass.  Check your "
        "specialization of __abs__ for this type and ensure the Return type is set to "
        "a py::Object subclass."
    );
    if constexpr (impl::has_call_operator<__abs__<Self>>) {
        return __abs__<Self>{}(self);

    } else if (impl::cpp_or_originates_from_cpp<Self>) {
        static_assert(
            impl::has_abs<impl::cpp_type<Self>>,
            "__abs__<T> is enabled for a type whose C++ representation does not have "
            "a viable overload for `std::abs(T)`"
        );
        return std::abs(unwrap(self));

    } else {
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
    if constexpr (impl::has_call_operator<__pow__<Base, Exp>>) {
        return __pow__<Base, Exp>{}(base, exp);

    } else if constexpr (
        impl::cpp_or_originates_from_cpp<Base> &&
        impl::cpp_or_originates_from_cpp<Exp>
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
[[nodiscard]] auto pow(const Base& base, const Exp& exp) {
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
[[nodiscard]] auto pow(const Base& base, const Exp& exp, const Mod& mod) {
    using Return = typename __pow__<Base, Exp>::type;
    static_assert(
        std::derived_from<Return, Object>,
        "pow() must return a py::Object subclass.  Check your specialization "
        "of __pow__ for this type and ensure the Return type is derived from "
        "py::Object."
    );
    if constexpr (std::is_invocable_v<
        __pow__<Base, Exp>,
        const Base&,
        const Exp&,
        const Mod&
    >) {
        return __pow__<Base, Exp>{}(base, exp, mod);

    } else if constexpr (
        impl::cpp_or_originates_from_cpp<Base> &&
        impl::cpp_or_originates_from_cpp<Exp> &&
        impl::cpp_or_originates_from_cpp<Mod>
    ) {
        return pow(unwrap(base), unwrap(exp), unwrap(mod));

    } else {
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
[[nodiscard]] auto pow(Base base, Exp exp, Mod mod) {
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
auto floordiv(const L& lhs, const R& rhs) {
    using Return = typename __floordiv__<L, R>::type;
    static_assert(
        std::derived_from<Return, Object>,
        "Floor division operator must return a py::Object subclass.  Check your "
        "specialization of __floordiv__ for these types and ensure the Return "
        "type is derived from py::Object."
    );
    if constexpr (impl::has_call_operator<__floordiv__<L, R>>) {
        return __floordiv__<L, R>{}(lhs, rhs);
    } else {
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
    if constexpr (impl::has_call_operator<__invert__<Self>>) {
        return __invert__<Self>{}(self);

    } else if constexpr (impl::cpp_or_originates_from_cpp<Self>) {
        static_assert(
            impl::has_invert<impl::cpp_type<Self>>,
            "__invert__<T> is enabled for a type whose C++ representation does not "
            "have a viable overload for `~T`"
        );
        return ~unwrap(self);

    } else {
        PyObject* result = PyNumber_Invert(ptr(self));
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
    if constexpr (impl::has_call_operator<__pos__<Self>>) {
        return __pos__<Self>{}(self);

    } else if constexpr (impl::cpp_or_originates_from_cpp<Self>) {
        static_assert(
            impl::has_pos<impl::cpp_type<Self>>,
            "__pos__<T> is enabled for a type whose C++ representation does not "
            "have a viable overload for `+T`"
        );
        return +unwrap(self);

    } else {
        PyObject* result = PyNumber_Positive(ptr(self));
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
    if constexpr (impl::has_call_operator<__neg__<Self>>) {
        return __neg__<Self>{}(self);

    } else if constexpr (impl::cpp_or_originates_from_cpp<Self>) {
        static_assert(
            impl::has_neg<impl::cpp_type<Self>>,
            "__neg__<T> is enabled for a type whose C++ representation does not "
            "have a viable overload for `-T`"
        );
        return -unwrap(self);

    } else {
        PyObject* result = PyNumber_Negative(ptr(self));
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
    static_assert(
        impl::python_like<Self>,
        "Increment operator requires a Python object."
    );
    using Return = typename __increment__<Self>::type;
    static_assert(
        std::same_as<Return, Self>,
        "Increment operator must return a reference to the derived type.  "
        "Check your specialization of __increment__ for this type and ensure "
        "the Return type is set to the derived type."
    );
    if constexpr (impl::has_call_operator<__increment__<Self>>) {
        __increment__<Self>{}(self);

    } else if constexpr (impl::cpp_or_originates_from_cpp<Self>) {
        static_assert(
            impl::has_preincrement<impl::cpp_type<Self>>,
            "__increment__<T> is enabled for a type whose C++ representation does not "
            "have a viable overload for `++T`"
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


template <std::derived_from<Object> Self> requires (!__increment__<Self>::enable)
Self operator++(Self& self, int) = delete;
template <std::derived_from<Object> Self> requires (__increment__<Self>::enable)
Self operator++(Self& self, int) {
    static_assert(
        impl::python_like<Self>,
        "Increment operator requires a Python object."
    );
    using Return = typename __increment__<Self>::type;
    static_assert(
        std::same_as<Return, Self>,
        "Increment operator must return a reference to the derived type.  "
        "Check your specialization of __increment__ for this type and ensure "
        "the Return type is set to the derived type."
    );
    Self copy = self;
    if constexpr (impl::has_call_operator<__increment__<Self>>) {
        __increment__<Self>{}(self);

    } else if constexpr (impl::cpp_or_originates_from_cpp<Self>) {
        static_assert(
            impl::has_postincrement<impl::cpp_type<Self>>,
            "__increment__<T> is enabled for a type whose C++ representation does not "
            "have a viable overload for `T++`"
        );
        return unwrap(self)++;

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
    return copy;
}


template <std::derived_from<Object> Self> requires (!__decrement__<Self>::enable)
Self& operator--(Self& self) = delete;
template <std::derived_from<Object> Self> requires (__decrement__<Self>::enable)
Self& operator--(Self& self) {
    static_assert(
        impl::python_like<Self>,
        "Decrement operator requires a Python object."
    );
    using Return = typename __decrement__<Self>::type;
    static_assert(
        std::same_as<Return, Self>,
        "Decrement operator must return a reference to the derived type.  "
        "Check your specialization of __decrement__ for this type and ensure "
        "the Return type is set to the derived type."
    );
    if constexpr (impl::has_call_operator<__decrement__<Self>>) {
        __decrement__<Self>{}(self);

    } else if constexpr (impl::cpp_or_originates_from_cpp<Self>) {
        static_assert(
            impl::has_predecrement<impl::cpp_type<Self>>,
            "__decrement__<T> is enabled for a type whose C++ representation does not "
            "have a viable overload for `--T`"
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


template <std::derived_from<Object> Self> requires (!__decrement__<Self>::enable)
Self operator--(Self& self, int) = delete;
template <std::derived_from<Object> Self> requires (__decrement__<Self>::enable)
Self operator--(Self& self, int) {
    static_assert(
        impl::python_like<Self>,
        "Decrement operator requires a Python object."
    );
    using Return = typename __decrement__<Self>::type;
    static_assert(
        std::same_as<Return, Self>,
        "Decrement operator must return a reference to the derived type.  "
        "Check your specialization of __decrement__ for this type and ensure "
        "the Return type is set to the derived type."
    );
    Self copy = self;
    if constexpr (impl::has_call_operator<__decrement__<Self>>) {
        __decrement__<Self>{}(self);

    } else if constexpr (impl::cpp_or_originates_from_cpp<Self>) {
        static_assert(
            impl::has_postdecrement<impl::cpp_type<Self>>,
            "__decrement__<T> is enabled for a type whose C++ representation does not "
            "have a viable overload for `T--`"
        );
        return unwrap(self)--;

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
    if constexpr (impl::has_call_operator<__lt__<L, R>>) {
        return __lt__<L, R>{}(lhs, rhs);

    } else if constexpr (
        impl::cpp_or_originates_from_cpp<L> &&
        impl::cpp_or_originates_from_cpp<R>
    ) {
        static_assert(
            impl::has_lt<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__lt__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L < R`"
        );
        return unwrap(lhs) < unwrap(rhs);

    } else {
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
    if constexpr (impl::has_call_operator<__le__<L, R>>) {
        return __le__<L, R>{}(lhs, rhs);

    } else if constexpr (
        impl::cpp_or_originates_from_cpp<L> &&
        impl::cpp_or_originates_from_cpp<R>
    ) {
        static_assert(
            impl::has_le<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__le__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L <= R`"
        );
        return unwrap(lhs) <= unwrap(rhs);

    } else {
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
    if constexpr (impl::has_call_operator<__eq__<L, R>>) {
        return __eq__<L, R>{}(lhs, rhs);

    } else if constexpr (
        impl::cpp_or_originates_from_cpp<L> &&
        impl::cpp_or_originates_from_cpp<R>
    ) {
        static_assert(
            impl::has_eq<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__eq__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L == R`"
        );
        return unwrap(lhs) == unwrap(rhs);

    } else {
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
    if constexpr (impl::has_call_operator<__ne__<L, R>>) {
        return __ne__<L, R>{}(lhs, rhs);

    } else if constexpr (
        impl::cpp_or_originates_from_cpp<L> &&
        impl::cpp_or_originates_from_cpp<R>
    ) {
        static_assert(
            impl::has_ne<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__ne__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L != R`"
        );
        return unwrap(lhs) != unwrap(rhs);

    } else {
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
    if constexpr (impl::has_call_operator<__ge__<L, R>>) {
        return __ge__<L, R>{}(lhs, rhs);

    } else if constexpr (
        impl::cpp_or_originates_from_cpp<L> &&
        impl::cpp_or_originates_from_cpp<R>
    ) {
        static_assert(
            impl::has_ge<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__ge__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L >= R`"
        );
        return unwrap(lhs) >= unwrap(rhs);

    } else {
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
    if constexpr (impl::has_call_operator<__gt__<L, R>>) {
        return __gt__<L, R>{}(lhs, rhs);

    } else if constexpr (
        impl::cpp_or_originates_from_cpp<L> &&
        impl::cpp_or_originates_from_cpp<R>
    ) {
        static_assert(
            impl::has_gt<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__gt__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L > R`"
        );
        return unwrap(lhs) > unwrap(rhs);

    } else {
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
    if constexpr (impl::has_call_operator<__add__<L, R>>) {
        return __add__<L, R>{}(lhs, rhs);

    } else if constexpr (
        impl::cpp_or_originates_from_cpp<L> &&
        impl::cpp_or_originates_from_cpp<R>
    ) {
        static_assert(
            impl::has_add<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__add__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L + R`"
        );
        return unwrap(lhs) + unwrap(rhs);

    } else {
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
    static_assert(
        impl::python_like<L>,
        "In-place addition operator requires a Python object."
    );
    using Return = typename __iadd__<L, R>::type;
    static_assert(
        std::same_as<Return, L&>,
        "In-place addition operator must return a mutable reference to the left "
        "operand.  Check your specialization of __iadd__ for these types and "
        "ensure the Return type is set to the left operand."
    );
    if constexpr (impl::has_call_operator<__iadd__<L, R>>) {
        __iadd__<L, R>{}(lhs, rhs);

    } else if constexpr (
        impl::cpp_or_originates_from_cpp<L> &&
        impl::cpp_or_originates_from_cpp<R>
    ) {
        static_assert(
            impl::has_iadd<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__iadd__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L += R`"
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
    if constexpr (impl::has_call_operator<__sub__<L, R>>) {
        return __sub__<L, R>{}(lhs, rhs);

    } else if constexpr (
        impl::cpp_or_originates_from_cpp<L> &&
        impl::cpp_or_originates_from_cpp<R>
    ) {
        static_assert(
            impl::has_sub<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__sub__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L - R`"
        );
        return unwrap(lhs) - unwrap(rhs);

    } else {
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
    static_assert(
        impl::python_like<L>,
        "In-place addition operator requires a Python object."
    );
    using Return = typename __isub__<L, R>::type;
    static_assert(
        std::same_as<Return, L&>,
        "In-place addition operator must return a mutable reference to the left "
        "operand.  Check your specialization of __isub__ for these types and "
        "ensure the Return type is set to the left operand."
    );
    if constexpr (impl::has_call_operator<__isub__<L, R>>) {
        __isub__<L, R>{}(lhs, rhs);

    } else if constexpr (
        impl::cpp_or_originates_from_cpp<L> &&
        impl::cpp_or_originates_from_cpp<R>
    ) {
        static_assert(
            impl::has_isub<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__isub__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L -= R`"
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
    if constexpr (impl::has_call_operator<__mul__<L, R>>) {
        return __mul__<L, R>{}(lhs, rhs);

    } else if constexpr (
        impl::cpp_or_originates_from_cpp<L> &&
        impl::cpp_or_originates_from_cpp<R>
    ) {
        static_assert(
            impl::has_mul<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__mul__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L * R`"
        );
        return unwrap(lhs) * unwrap(rhs);

    } else {
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
    static_assert(
        impl::python_like<L>,
        "In-place multiplication operator requires a Python object."
    );
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
        impl::cpp_or_originates_from_cpp<L> &&
        impl::cpp_or_originates_from_cpp<R>
    ) {
        static_assert(
            impl::has_imul<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__imul__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L *= R`"
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
    if constexpr (impl::has_call_operator<__truediv__<L, R>>) {
        return __truediv__<L, R>{}(lhs, rhs);

    } else if constexpr (
        impl::cpp_or_originates_from_cpp<L> &&
        impl::cpp_or_originates_from_cpp<R>
    ) {
        static_assert(
            impl::has_truediv<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__truediv__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L / R`"
        );
        return unwrap(lhs) / unwrap(rhs);

    } else {
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
    static_assert(
        impl::python_like<L>,
        "In-place true division operator requires a Python object."
    );
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
        impl::cpp_or_originates_from_cpp<L> &&
        impl::cpp_or_originates_from_cpp<R>
    ) {
        static_assert(
            impl::has_itruediv<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__itruediv__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L /= R`"
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
    if constexpr (impl::has_call_operator<__mod__<L, R>>) {
        return __mod__<L, R>{}(lhs, rhs);

    } else if constexpr (
        impl::cpp_or_originates_from_cpp<L> &&
        impl::cpp_or_originates_from_cpp<R>
    ) {
        static_assert(
            impl::has_mod<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__mod__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L % R`"
        );
        return unwrap(lhs) % unwrap(rhs);

    } else {
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
    static_assert(
        impl::python_like<L>,
        "In-place modulus operator requires a Python object."
    );
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
        impl::cpp_or_originates_from_cpp<L> &&
        impl::cpp_or_originates_from_cpp<R>
    ) {
        static_assert(
            impl::has_imod<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__imod__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L %= R`"
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
    if constexpr (impl::has_call_operator<__lshift__<L, R>>) {
        return __lshift__<L, R>{}(lhs, rhs);

    } else if constexpr (
        impl::cpp_or_originates_from_cpp<L> &&
        impl::cpp_or_originates_from_cpp<R>
    ) {
        static_assert(
            impl::has_lshift<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__lshift__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L << R`"
        );
        return unwrap(lhs) << unwrap(rhs);

    } else {
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


template <std::derived_from<std::ostream> L, std::derived_from<Object> R>
L& operator<<(L& os, const R& obj) {
    PyObject* repr = PyObject_Repr(ptr(obj));
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


template <std::derived_from<Object> L, typename R> requires (!__ilshift__<L, R>::enable)
L& operator<<=(L& lhs, const R& rhs) = delete;
template <std::derived_from<Object> L, typename R> requires (__ilshift__<L, R>::enable)
L& operator<<=(L& lhs, const R& rhs) {
    static_assert(
        impl::python_like<L>,
        "In-place left shift operator requires a Python object."
    );
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
        impl::cpp_or_originates_from_cpp<L> &&
        impl::cpp_or_originates_from_cpp<R>
    ) {
        static_assert(
            impl::has_ilshift<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__ilshift__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L <<= R`"
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
    if constexpr (impl::has_call_operator<__rshift__<L, R>>) {
        return __rshift__<L, R>{}(lhs, rhs);

    } else if constexpr (
        impl::cpp_or_originates_from_cpp<L> &&
        impl::cpp_or_originates_from_cpp<R>
    ) {
        static_assert(
            impl::has_rshift<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__rshift__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L >> R`"
        );
        return unwrap(lhs) >> unwrap(rhs);

    } else {
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
    static_assert(
        impl::python_like<L>,
        "In-place right shift operator requires a Python object."
    );
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
        impl::cpp_or_originates_from_cpp<L> &&
        impl::cpp_or_originates_from_cpp<R>
    ) {
        static_assert(
            impl::has_irshift<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__irshift__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L >>= R`"
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
    if constexpr (impl::has_call_operator<__and__<L, R>>) {
        return __and__<L, R>{}(lhs, rhs);

    } else if constexpr (
        impl::cpp_or_originates_from_cpp<L> &&
        impl::cpp_or_originates_from_cpp<R>
    ) {
        static_assert(
            impl::has_and<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__and__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L & R`"
        );
        return unwrap(lhs) & unwrap(rhs);

    } else {
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
    static_assert(
        impl::python_like<L>,
        "In-place bitwise AND operator requires a Python object."
    );
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
        impl::cpp_or_originates_from_cpp<L> &&
        impl::cpp_or_originates_from_cpp<R>
    ) {
        static_assert(
            impl::has_iand<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__iand__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L &= R`"
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
auto operator|(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && __or__<L, R>::enable)
auto operator|(const L& lhs, const R& rhs) {
    using Return = typename __or__<L, R>::type;
    static_assert(
        std::derived_from<Return, Object>,
        "Bitwise OR operator must return a py::Object subclass.  Check your "
        "specialization of __or__ for this type and ensure the Return type is "
        "derived from py::Object."
    );
    if constexpr (impl::has_call_operator<__or__<L, R>>) {
        return __or__<L, R>{}(lhs, rhs);

    } else if constexpr (
        impl::cpp_or_originates_from_cpp<L> &&
        impl::cpp_or_originates_from_cpp<R>
    ) {
        static_assert(
            impl::has_or<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__or__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L | R`"
        );
        return unwrap(lhs) | unwrap(rhs);

    } else {
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
    static_assert(
        impl::python_like<L>,
        "In-place bitwise OR operator requires a Python object."
    );
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
        impl::cpp_or_originates_from_cpp<L> &&
        impl::cpp_or_originates_from_cpp<R>
    ) {
        static_assert(
            impl::has_ior<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__ior__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L |= R`"
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
    if constexpr (impl::has_call_operator<__xor__<L, R>>) {
        return __xor__<L, R>{}(lhs, rhs);

    } else if constexpr (
        impl::cpp_or_originates_from_cpp<L> &&
        impl::cpp_or_originates_from_cpp<R>
    ) {
        static_assert(
            impl::has_xor<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__xor__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L ^ R`"
        );
        return unwrap(lhs) ^ unwrap(rhs);

    } else {
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
    static_assert(
        impl::python_like<L>,
        "In-place bitwise XOR operator requires a Python object."
    );
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
        impl::cpp_or_originates_from_cpp<L> &&
        impl::cpp_or_originates_from_cpp<R>
    ) {
        static_assert(
            impl::has_ixor<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__ixor__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L ^= R`"
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
            } else if constexpr (py::impl::cpp_or_originates_from_cpp<T>) {
                static_assert(
                    py::impl::hashable<py::impl::cpp_type<T>>,
                    "__hash__<T> is enabled for a type whose C++ representation does "
                    "not have a viable overload for `std::hash<T>{}`"
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
