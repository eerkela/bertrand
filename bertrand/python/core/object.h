#ifndef BERTRAND_PYTHON_CORE_OBJECT_H
#define BERTRAND_PYTHON_CORE_OBJECT_H

#include "declarations.h"
// #include "except.h"
// #include "ops.h"


namespace py {


/* Retrieve the pointer backing a Python object. */
template <std::derived_from<Handle> T>
[[nodiscard]] PyObject* ptr(const T& obj);


/* Cause a Python object to relinquish ownership over its backing pointer, and then
return the raw pointer. */
template <std::derived_from<Handle> T>
[[nodiscard]] PyObject* release(T& obj);


/* Cause a Python object to relinquish ownership over its backing pointer, and then
return the raw pointer. */
template <std::derived_from<Handle> T> requires (!std::is_const_v<T>)
[[nodiscard]] PyObject* release(T&& obj);


/* Steal a reference to a raw Python handle. */
template <std::derived_from<Object> T>
[[nodiscard]] T reinterpret_steal(Handle obj);


/* Borrow a reference to a raw Python handle. */
template <std::derived_from<Object> T>
[[nodiscard]] T reinterpret_borrow(Handle obj);


template <>
struct Interface<Handle> {};
template <>
struct Interface<Object> {};


/* A non-owning reference to a raw Python object. */
struct Handle : Interface<Handle>, impl::BertrandTag {
protected:
    PyObject* m_ptr;

    template <std::derived_from<Handle> T>
    friend PyObject* ptr(const T&);
    template <std::derived_from<Handle> T>
    friend PyObject* release(T&);
    template <std::derived_from<Handle> T> requires (!std::is_const_v<T>)
    friend PyObject* release(T&& obj);

public:

    Handle() = default;
    Handle(const Handle&) = default;
    Handle(Handle&&) = default;
    Handle& operator=(const Handle&) = default;
    Handle& operator=(Handle&&) = default;

    Handle(PyObject* ptr) : m_ptr(ptr) {}

    /* Check for exact pointer identity. */
    template <typename Self>
    [[nodiscard]] bool is(this const Self& self, Handle other) {
        return ptr(self) == ptr(other);
    }

    /* Contains operator.  Equivalent to Python's `in` keyword, but with reversed
    operands (i.e. `x in y` -> `y.contains(x)`).  This is consistent with other STL
    container types, and the allowable key types can be specified via the __contains__
    control struct. */
    template <typename Self, typename Key> requires (__contains__<Self, Key>::enable)
    [[nodiscard]] bool contains(this const Self& self, const Key& key);  // defined in ops.h

    /* Contextually convert an Object into a boolean value for use in if/else 
    statements, with the same semantics as in Python. */
    template <typename Self>
    [[nodiscard]] explicit operator bool(this const Self& self);  // defined in ops.h

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
    decltype(auto) operator()(this Self&& self, Args&&... args) {
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
    template <typename Self, typename... Key>
        requires (__getitem__<Self, Key...>::enable)
    decltype(auto) operator[](this const Self& self, Key&&... key);  // defined in ops.h

};

template <std::derived_from<Handle> T>
[[nodiscard]] inline PyObject* ptr(const T& obj) {
    return obj.m_ptr;
}


template <std::derived_from<Handle> T>
[[nodiscard]] inline PyObject* release(T& obj) {
    PyObject* temp = obj.m_ptr;
    obj.m_ptr = nullptr;
    return temp;
}


template <std::derived_from<Handle> T> requires (!std::is_const_v<T>)
[[nodiscard]] inline PyObject* release(T&& obj) {
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
    static constexpr bool operator()(const T& obj, const Object& cls);  // defined in except.h
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
    static bool operator()(const T& obj, const Object& cls);  // defined in except.h
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


/// NOTE: additional delegating constructors for Python objects are defined in core.h


/* Implicitly convert a Python object into one of its subclasses by applying a runtime
`isinstance()` check. */
template <std::derived_from<Handle> From, std::derived_from<From> To>
struct __cast__<From, To> : Returns<To> {
    static auto operator()(const From& from);  // defined in except.h
    static auto operator()(From&& from);  // defined in except.h
};


/* Implicitly convert a Python object into any C++ type by checking for an equivalent
Python type via __as_object__, implicitly converting to that type, and then implicitly
converting to the C++ type in a 2-step process. */
template <typename To>
    requires (!impl::bertrand_like<To> && __as_object__<To>::enable)
struct __cast__<Object, To> : Returns<To> {
    static auto operator()(const Object& self) {
        return self.operator typename __as_object__<To>::type().operator To();
    }
};


/* Explicitly convert a Python object into a C++ integer by calling `int(obj)` at the
Python level. */
template <std::derived_from<Handle> From, std::integral To>
struct __explicit_cast__<From, To> : Returns<To> {
    static To operator()(const From& from);  // defined in except.h
};


/* Explicitly convert a Python object into a C++ floating-point number by calling
`float(obj)` at the Python level. */
template <std::derived_from<Handle> From, std::floating_point To>
struct __explicit_cast__<From, To> : Returns<To> {
    static To operator()(const From& from);  // defined in except.h
};


/* Explicitly convert a Python object into a C++ complex number by calling
`complex(obj)` at the Python level. */
template <std::derived_from<Handle> From, impl::complex_like To>
    requires (impl::cpp_like<To>)
struct __explicit_cast__<From, To> : Returns<To> {
    static To operator()(const From& from);  // defined in except.h
};


/* Explicitly convert a Python object into a C++ sd::string representation by calling
`str(obj)` at the Python level. */
template <std::derived_from<Handle> From> 
struct __explicit_cast__<From, std::string> : Returns<std::string> {
    static auto operator()(const From& from);  // defined in except.h
};


/* Explicitly convert a Python object into any C++ type by checking for an equivalent
Python type via __as_object__, explicitly converting to that type, and then explicitly
converting to the C++ type in a 2-step process. */
template <std::derived_from<Handle> From, typename To>
    requires (!impl::bertrand_like<To> && __as_object__<To>::enable)
struct __explicit_cast__<From, To> : Returns<To> {
    static auto operator()(const From& from) {
        return static_cast<To>(static_cast<typename __as_object__<To>::type>(from));
    }
};


}  // namespace py


#endif
