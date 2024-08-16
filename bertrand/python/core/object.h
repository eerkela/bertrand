#ifndef BERTRAND_PYTHON_CORE_OBJECT_H
#define BERTRAND_PYTHON_CORE_OBJECT_H

#include "declarations.h"
#include "except.h"
#include "ops.h"


namespace py {


namespace impl {

    struct SliceInitializer;

    /* A proxy for an item in a Python container that is controlled by the
    `__getitem__`, `__setitem__`, and `__delitem__` control structs.

    This is a simple extension of an Object type that intercepts `operator=` and
    assigns the new value back to the container using the appropriate API.  Mutating
    the object in any other way will also modify it in-place within the container. */
    template <typename Container, typename Key>
        requires (__getitem__<Container, Key>::enable)
    struct Item : __getitem__<Container, Key>::type {
    private:
        using Base = __getitem__<Container, Key>::type;

        Container m_container;
        Key m_key;

    public:

        Item(Base&& item, Container container, Key key) :
            Base(std::move(item)), m_container(container), m_key(key)
        {}
        Item(const Item& other) :
            Base(other), m_container(other.m_container), m_key(other.m_key)
        {}
        Item(Item&& other) :
            Base(std::move(other)), m_container(std::move(other.m_container)),
            m_key(std::move(other.m_key))
        {}

        template <typename Value>
            requires (__setitem__<Container, Key, std::remove_cvref_t<Value>>::enable)
        Item& operator=(Value&& value) {
            using setitem = __setitem__<Container, Key, std::remove_cvref_t<Value>>;
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
                impl::cpp_or_originates_from_cpp<Key> &&
                impl::cpp_or_originates_from_cpp<std::remove_cvref_t<Value>>
            ) {
                static_assert(
                    impl::supports_item_assignment<Base, Key, std::remove_cvref_t<Value>>,
                    "__setitem__<Self, Key, Value> is enabled for operands whose C++ "
                    "representations have no viable overload for `Self[Key] = Value`"
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

        template <typename = void> requires (__delitem__<Container, Key>::enable)
        Item& operator=(del value) {
            using delitem = __delitem__<Container, Key>;
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
                !__setitem__<Container, Key, std::remove_cvref_t<Value>>::enable &&
                !__delitem__<Container, Key>::enable
            )
        Item& operator=(Value&& other) = delete;

    };

}


template <>
struct Interface<Handle> {};
template <>
struct Interface<Object> {};


/* A non-owning reference to a raw Python object. */
struct Handle : impl::BertrandTag, Interface<Handle> {
protected:
    PyObject* m_ptr;

    friend inline PyObject* ptr(Handle handle);
    friend inline PyObject* release(Handle handle);

public:

    Handle() = default;
    Handle(const Handle&) = default;
    Handle(Handle&&) = default;
    Handle& operator=(const Handle&) = default;
    Handle& operator=(Handle&&) = default;

    Handle(PyObject* ptr) : m_ptr(ptr) {}

    /* Check for exact pointer identity. */
    [[nodiscard]] bool is(Handle other) const {
        return m_ptr == ptr(other);
    }

    /* Contains operator.  Equivalent to Python's `in` keyword, but with reversed
    operands (i.e. `x in y` -> `y.contains(x)`).  This is consistent with other STL
    container types, and the allowable key types can be specified via the __contains__
    control struct. */
    template <typename Self, typename Key> requires (__contains__<Self, Key>::enable)
    [[nodiscard]] bool contains(this const Self& self, const Key& key) {
        using Return = typename __contains__<Self, Key>::type;
        static_assert(
            std::same_as<Return, bool>,
            "contains() operator must return a boolean value.  Check your "
            "specialization of __contains__ for these types and ensure the Return "
            "type is set to bool."
        );
        if constexpr (impl::has_call_operator<__contains__<Self, Key>>) {
            return __contains__<Self, Key>{}(self, key);

        } else if constexpr (
            impl::originates_from_cpp<Self> &&
            impl::cpp_or_originates_from_cpp<Key>
        ) {
            static_assert(
                impl::has_contains<impl::cpp_type<Self>, impl::cpp_type<Key>>,
                "__contains__<Self, Key> is enabled for operands whose C++ "
                "representations have no viable overload for `Self.contains(Key)`"
            );
            return unwrap(self).contains(unwrap(key));

        } else {
            int result = PySequence_Contains(
                self.m_ptr,
                ptr(as_object(key))
            );
            if (result == -1) {
                Exception::from_python();
            }
            return result;
        }
    }

    /* Contextually convert an Object into a boolean value for use in if/else 
    statements, with the same semantics as in Python. */
    template <typename Self>
    [[nodiscard]] explicit operator bool(this const Self& self) {
        if constexpr (
            impl::originates_from_cpp<Self> &&
            impl::has_operator_bool<impl::cpp_type<Self>>
        ) {
            return static_cast<bool>(unwrap(self));
        } else {
            int result = PyObject_IsTrue(self.m_ptr);
            if (result == -1) {
                Exception::from_python();
            }
            return result;   
        }
    }

    /* Universal implicit conversion operator.  Implemented via the __cast__ control
    struct. */
    template <typename Self, typename T>
        requires (__cast__<Self, T>::enable)
    [[nodiscard]] operator T(this const Self& self) {
        return __cast__<Self, T>{}(self);
    }

    /* Universal explicit conversion operator.  Implemented via the __explicit_cast__
    control struct. */
    template <typename Self, typename T>
        requires (!__cast__<Self, T>::enable && __explicit_cast__<Self, T>::enable)
    [[nodiscard]] explicit operator T(this const Self& self) {
        return __explicit_cast__<Self, T>{}(self);
    }

    /* Call operator.  This can be enabled for specific argument signatures and return
    types via the __call__ control struct, enabling static type safety for Python
    functions in C++. */
    template <typename Self, typename... Args>
        requires (__call__<std::remove_cvref_t<Self>, Args...>::enable)
    auto operator()(this Self&& self, Args&&... args) {
        using call = __call__<std::remove_cvref_t<Self>, Args...>;
        using Return = typename call::type;
        static_assert(
            std::is_void_v<Return> || std::derived_from<Return, Object>,
            "Call operator must return either void or a py::Object subclass.  "
            "Check your specialization of __call__ for the given arguments and "
            "ensure that it is derived from py::Object."
        );
        if constexpr (impl::has_call_operator<call>) {
            return call{}(std::forward<Self>(self), std::forward<Args>(args)...);

        } else if constexpr (
            impl::originates_from_cpp<Self> &&
            (impl::cpp_or_originates_from_cpp<std::decay_t<Args>> && ...)
        ) {
            static_assert(
                std::is_invocable_r_v<Return, impl::cpp_type<Self>, Args...>,
                "__call__<Self, Args...> is enabled for operands whose C++ "
                "representations have no viable overload for `Self(Key, Args...)`"
            );
            if constexpr (std::is_void_v<Return>) {
                unwrap(self)(std::forward<Args>(args)...);
            } else {
                return unwrap(self)(std::forward<Args>(args)...);
            }

        } else {
            if constexpr (std::is_void_v<Return>) {
                Function<Return(Args...)>::template invoke_py<Return>(
                    ptr(self),
                    std::forward<Args>(args)...
                );
            } else {
                return Function<Return(Args...)>::template invoke_py<Return>(
                    ptr(self),
                    std::forward<Args>(args)...
                );
            }
        }
    }

    /* Index operator.  Specific key and element types can be controlled via the
    __getitem__, __setitem__, and __delitem__ control structs. */
    template <typename Self, typename Key> requires (__getitem__<Self, Key>::enable)
    auto operator[](this const Self& self, Key&& key) {
        using Return = typename __getitem__<Self, std::decay_t<Key>>::type;
        static_assert(
            std::derived_from<Return, Object>,
            "index operator must return a subclass of py::Object.  Check your "
            "specialization of __getitem__ for these types and ensure the Return "
            "type is set to a subclass of py::Object."
        );
        if constexpr (impl::has_call_operator<__getitem__<Self, std::decay_t<Key>>>) {
            return impl::Item<Self, Key>(
                __getitem__<Self, Key>{}(self, key),
                self,
                std::forward<Key>(key)
            );

        } else if constexpr (
            impl::originates_from_cpp<Self> &&
            impl::cpp_or_originates_from_cpp<std::decay_t<Key>>
        ) {
            static_assert(
                impl::lookup_yields<impl::cpp_type<Self>, std::decay_t<Key>, Return>,
                "__getitem__<Self, Args...> is enabled for operands whose C++ "
                "representations have no viable overload for `Self[Key]`"
            );
            return unwrap(self)[std::forward<Key>(key)];

        } else {
            PyObject* result = PyObject_GetItem(
                self.m_ptr,
                ptr(as_object(key))
            );
            if (result == nullptr) {
                Exception::from_python();
            }
            return impl::Item<Self, std::decay_t<Key>>(
                reinterpret_steal<Return>(result),
                self,
                std::forward<Key>(key)
            );
        }
    }

    /* Slice operator.  This is just syntactic sugar for the index operator with a
    py::Slice operand, allowing users to specify slices using a condensed initializer
    list. */
    template <typename Self> requires (__getitem__<Self, Slice>::enable)
    auto operator[](
        this const Self& self,
        const std::initializer_list<impl::SliceInitializer>& slice
    );

};


[[nodiscard]] inline PyObject* ptr(Handle obj) {
    return obj.m_ptr;
}


[[nodiscard]] inline PyObject* release(Handle obj) {
    PyObject* temp = obj.m_ptr;
    obj.m_ptr = nullptr;
    return temp;
}


/* An owning reference to a dynamically-typed Python object. */
struct Object : Handle, Interface<Object> {
protected:
    struct borrowed_t {};
    struct stolen_t {};

    template <std::derived_from<Object> T>
    friend T reinterpret_borrow(Handle);
    template <std::derived_from<Object> T>
    friend T reinterpret_steal(Handle);

    template <typename T>
    struct implicit_ctor {
        template <typename... Args>
        static constexpr bool enable =
            __init__<T, std::remove_cvref_t<Args>...>::enable &&
            std::is_invocable_r_v<T, __init__<T, std::remove_cvref_t<Args>...>, Args...>;
    };

    template <typename T>
    struct explicit_ctor {
        template <typename... Args>
        static constexpr bool enable =
            !__init__<T, std::remove_cvref_t<Args>...>::enable &&
            __explicit_init__<T, std::remove_cvref_t<Args>...>::enable &&
            std::is_invocable_r_v<
                T,
                __explicit_init__<T, std::remove_cvref_t<Args>...>,
                Args...
            >;
    };

    template <std::derived_from<Object> T, typename... Args>
    Object(implicit_ctor<T>, Args&&... args) : Handle((
        Interpreter::init(),
        __init__<T, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    )) {}

    template <std::derived_from<Object> T, typename... Args>
    Object(explicit_ctor<T>, Args&&... args) : Handle((
        Interpreter::init(),
        __explicit_init__<T, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    )) {}

public:

    /* Copy constructor.  Borrows a reference to an existing object. */
    Object(const Object& other) : Handle(Py_XNewRef(ptr(other))) {}

    /* Move constructor.  Steals a reference to a temporary object. */
    Object(Object&& other) : Handle(release(other)) {}

    /* reinterpret_borrow() constructor.  Borrows a reference to a raw Python handle. */
    Object(Handle h, borrowed_t) : Handle(Py_XNewRef(ptr(h))) {}

    /* reinterpret_steal() constructor.  Steals a reference to a raw Python handle. */
    Object(Handle h, stolen_t) : Handle(ptr(h)) {}

    /* Universal implicit constructor.  Implemented via the __init__ control struct. */
    template <typename... Args> requires (implicit_ctor<Object>::enable<Args...>)
    Object(Args&&... args) : Handle(
        implicit_ctor<Object>{},
        std::forward<Args>(args)...
    ) {}

    /* Universal explicit constructor.  Implemented via the __explicit_init__ control
    struct. */
    template <typename... Args> requires (explicit_ctor<Object>::enable<Args...>)
    explicit Object(Args&&... args) : Handle(
        explicit_ctor<Object>{},
        std::forward<Args>(args)...
    ) {}

    /* Destructor.  Allows any object to be stored with static duration. */
    ~Object() noexcept {
        if (Py_IsInitialized()) {
            Py_XDECREF(m_ptr);
        }
    }

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

};


template <std::derived_from<Object> T>
[[nodiscard]] T reinterpret_borrow(Handle obj) {
    return T(obj, Object::borrowed_t{});
}


template <std::derived_from<Object> T>
[[nodiscard]] T reinterpret_steal(Handle obj) {
    return T(obj, Object::stolen_t{});
}


template <typename T>
    requires (
        __as_object__<T>::enable &&
        impl::originates_from_cpp<typename __as_object__<T>::type> &&
        std::same_as<T, typename __as_object__<T>::type::__python__::t_cpp>
    )
[[nodiscard]] auto wrap(T& obj) -> __as_object__<T>::type {
    return Type<T>::__python__::_wrap(obj);
}


template <typename T>
    requires (
        __as_object__<T>::enable &&
        impl::originates_from_cpp<typename __as_object__<T>::type> &&
        std::same_as<T, typename __as_object__<T>::type::__python__::t_cpp>
    )
[[nodiscard]] auto wrap(const T& obj) -> __as_object__<T>::type {
    return Type<T>::__python__::_wrap(obj);
}


template <typename T> requires (impl::cpp_or_originates_from_cpp<T>)
[[nodiscard]] auto& unwrap(T& obj) {
    if constexpr (impl::cpp_like<T>) {
        return obj;
    } else {
        return Type<T>::__python__::_unwrap(obj);
    }
}


template <typename T> requires (impl::cpp_or_originates_from_cpp<T>)
[[nodiscard]] const auto& unwrap(const T& obj) {
    if constexpr (impl::cpp_like<T>) {
        return obj;
    } else {
        return Type<T>::__python__::_unwrap(obj);
    }
}


template <typename T>
struct __isinstance__<T, Object> : Returns<bool> {
    static consteval bool operator()(const T& obj) { return std::derived_from<T, Object>; }
    static constexpr bool operator()(const T& obj, const Object& cls) {
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
};


template <typename T>
struct __issubclass__<T, Object> : Returns<bool> {
    static consteval bool operator()() { return std::derived_from<T, Object>; }
    static constexpr bool operator()(const T& obj) {
        if constexpr (impl::dynamic_type<T>) {
            return PyType_Check(ptr(obj));
        } else {
            return impl::type_like<T>;
        }
    }
    static bool operator()(const T& obj, const Object& cls) {
        int result = PyObject_IsSubclass(
            ptr(as_object(obj)),
            ptr(cls)
        );
        if (result == -1) {
            Exception::from_python();
        }
        return result;
    }
};


/* Default initialize py::Object to None. */
template <>
struct __init__<Object> : Returns<Object> {
    static auto operator()() {
        return reinterpret_steal<Object>(Py_NewRef(Py_None));
    }
};


/* Implicitly convert any C++ value into a py::Object by invoking as_object(). */
template <impl::cpp_like T> requires (__as_object__<T>::enable)
struct __init__<Object, T> : Returns<Object> {
    static auto operator()(const T& value) {
        return reinterpret_steal<Object>(release(as_object(value)));
    }
};


/// NOTE: additional delegating constructors for py::Object are defined in common.h


/* Implicitly convert a py::Object (or any of its subclasses) into one of its
subclasses by applying a runtime `isinstance()` check. */
template <std::derived_from<Object> From, std::derived_from<From> To>
struct __cast__<From, To> : Returns<To> {
    static auto operator()(const From& from) {
        if (isinstance<To>(from)) {
            return reinterpret_borrow<To>(ptr(from));
        } else {
            throw TypeError(
                "cannot convert Python object from type '" + repr(Type<From>()) +
                 "' to type '" + repr(Type<To>()) + "'"
            );
        }
    }
    static auto operator()(From&& from) {
        if (isinstance<To>(from)) {
            return reinterpret_steal<To>(release(from));
        } else {
            throw TypeError(
                "cannot convert Python object from type '" + repr(Type<From>()) +
                 "' to type '" + repr(Type<To>()) + "'"
            );
        }
    }
};


/* Implicitly convert a py::Object into any C++ type by checking for an equivalent
Python type via __as_object__, implicitly converting to that type, and then implicitly
converting to the C++ type in a 2-step process. */
template <typename To>
    requires (!impl::bertrand_like<To> && __as_object__<To>::enable)
struct __cast__<Object, To> : Returns<To> {
    static auto operator()(const Object& self) {
        return self.operator typename __as_object__<To>::type().operator To();
    }
};


/* Explicitly convert a py::Object (or any of its subclasses) into a C++ integer by
calling `int(obj)` at the Python level. */
template <std::derived_from<Object> From, std::integral To>
struct __explicit_cast__<From, To> : Returns<To> {
    static To operator()(const From& from) {
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
};


/* Explicitly convert a py::Object (or any of its subclasses) into a C++ floating-point
number by calling `float(obj)` at the Python level. */
template <std::derived_from<Object> From, std::floating_point To>
struct __explicit_cast__<From, To> : Returns<To> {
    static To operator()(const From& from) {
        double result = PyFloat_AsDouble(ptr(from));
        if (result == -1.0 && PyErr_Occurred()) {
            Exception::from_python();
        }
        return result;
    }
};


/* Explicitly convert a py::Object (or any of its subclasses) into a C++ complex number
by calling `complex(obj)` at the Python level. */
template <std::derived_from<Object> From, impl::complex_like To>
    requires (impl::cpp_like<To>)
struct __explicit_cast__<From, To> : Returns<To> {
    static To operator()(const From& from) {
        Py_complex result = PyComplex_AsCComplex(ptr(from));
        if (result.real == -1.0 && PyErr_Occurred()) {
            Exception::from_python();
        }
        return To(result.real, result.imag);
    }
};


/* Explicitly convert a py::Object (or any of its subclasses) into a C++ sd::string
representation by calling `str(obj)` at the Python level. */
template <std::derived_from<Object> From> 
struct __explicit_cast__<From, std::string> : Returns<std::string> {
    static auto operator()(const From& from) {
        PyObject* str = PyObject_Str(ptr(from));
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
};


/* Explicitly convert a py::Object (or any of its subclasses) into any C++ type by
checking for an equivalent Python type via __as_object__, explicitly converting to that
type, and then explicitly converting to the C++ type in a 2-step process. */
template <std::derived_from<Object> From, typename To>
    requires (!impl::bertrand_like<To> && __as_object__<To>::enable)
struct __explicit_cast__<From, To> : Returns<To> {
    static auto operator()(const From& from) {
        return static_cast<To>(static_cast<typename __as_object__<To>::type>(from));
    }
};


}  // namespace py


#endif
