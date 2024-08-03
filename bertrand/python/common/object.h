#ifndef BERTRAND_PYTHON_COMMON_OBJECT_H
#define BERTRAND_PYTHON_COMMON_OBJECT_H

#include "declarations.h"
#include "except.h"
#include "ops.h"


namespace py {


/* A non-owning reference to a raw Python object. */
class Handle : public impl::BertrandTag {
protected:
    PyObject* m_ptr;

    friend inline PyObject* ptr(Handle handle);
    friend inline PyObject* release(Handle handle);

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
    Handle(implicit_ctor<T>, Args&&... args) : Handle((
        Interpreter::init(),
        __init__<T, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    )) {}

    template <std::derived_from<Object> T, typename... Args>
    Handle(explicit_ctor<T>, Args&&... args) : Handle((
        Interpreter::init(),
        __explicit_init__<T, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    )) {}

public:

    Handle() = default;
    Handle(const Handle&) = default;
    Handle(Handle&&) = default;
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
        if constexpr (impl::proxy_like<Key>) {
            return self.contains(key.value());
        } else if constexpr (impl::has_call_operator<__contains__<Self, Key>>) {
            return __contains__<Self, Key>{}(self, key);
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
    [[nodiscard]] explicit operator bool() const {
        int result = PyObject_IsTrue(m_ptr);
        if (result == -1) {
            Exception::from_python();
        }
        return result;
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
        } else if constexpr (std::is_void_v<Return>) {
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

    /* Index operator.  Specific key and element types can be controlled via the
    __getitem__, __setitem__, and __delitem__ control structs. */
    template <typename Self, typename Key> requires (__getitem__<Self, Key>::enable)
    auto operator[](this const Self& self, const Key& key) {
        using Return = typename __getitem__<Self, Key>::type;
        if constexpr (impl::proxy_like<Key>) {
            return self[key.value()];
        } else {
            return impl::Item<Self, Key>(self, key);
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
class Object : public Handle {
    using Base = Handle;

protected:
    struct borrowed_t {};
    struct stolen_t {};

    template <std::derived_from<Object> T>
    friend T reinterpret_borrow(Handle);
    template <std::derived_from<Object> T>
    friend T reinterpret_steal(Handle);

public:

    /* Copy constructor.  Borrows a reference to an existing object. */
    Object(const Object& other) : Base(Py_XNewRef(py::ptr(other))) {}

    /* Move constructor.  Steals a reference to a temporary object. */
    Object(Object&& other) : Base(py::release(other)) {}

    /* reinterpret_borrow() constructor.  Borrows a reference to a raw Python handle. */
    Object(Handle h, borrowed_t) : Base(Py_XNewRef(py::ptr(h))) {}

    /* reinterpret_steal() constructor.  Steals a reference to a raw Python handle. */
    Object(Handle h, stolen_t) : Base(py::ptr(h)) {}

    /* Universal implicit constructor.  Implemented via the __init__ control struct. */
    template <typename... Args> requires (implicit_ctor<Object>::enable<Args...>)
    Object(Args&&... args) : Base(
        implicit_ctor<Object>{},
        std::forward<Args>(args)...
    ) {}

    /* Universal explicit constructor.  Implemented via the __explicit_init__ control
    struct. */
    template <typename... Args> requires (explicit_ctor<Object>::enable<Args...>)
    explicit Object(Args&&... args) : Base(
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
        print("object copy assignment");  // TODO: remove debugging print statement
        if (this != &other) {
            PyObject* temp = m_ptr;
            m_ptr = Py_XNewRef(other.m_ptr);
            Py_XDECREF(temp);
        }
        return *this;
    }

    /* Move assignment operator. */
    Object& operator=(Object&& other) {
        print("object move assignment");  // TODO: remove debugging print statement
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
        impl::is_extension<typename __as_object__<T>::type> &&
        std::same_as<T, typename __as_object__<T>::type::__python__::t_cpp>
    )
[[nodiscard]] auto wrap(T& obj) -> __as_object__<T>::type {
    return T::__python__::_wrap(obj);
}


template <typename T>
    requires (
        __as_object__<T>::enable &&
        impl::is_extension<typename __as_object__<T>::type> &&
        std::same_as<T, typename __as_object__<T>::type::__python__::t_cpp>
    )
[[nodiscard]] auto wrap(const T& obj) -> __as_object__<T>::type {
    return T::__python__::_wrap(obj);
}


template <std::derived_from<Object> T> requires (impl::is_extension<T>)
[[nodiscard]] auto& unwrap(T& obj) {
    return T::__python__::_unwrap(obj);
}


template <std::derived_from<Object> T> requires (impl::is_extension<T>)
[[nodiscard]] const auto& unwrap(const T& obj) {
    return T::__python__::_unwrap(obj);
}


template <typename T>
struct __issubclass__<T, Object>                            : Returns<bool> {
    static consteval bool operator()() { return std::derived_from<T, Object>; }
    static constexpr bool operator()(const T& obj) { return operator()(); }
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


template <typename T>
struct __isinstance__<T, Object>                            : Returns<bool> {
    static constexpr bool operator()(const T& obj) {
        if constexpr (impl::python_like<T>) {
            return ptr(obj) != nullptr;
        } else {
            return issubclass<Object>(obj);
        }
    }
    static bool operator()(const T& obj, const Object& cls) {
        int result = PyObject_IsInstance(
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
struct __init__<Object> {
    static auto operator()() {
        return reinterpret_steal<Object>(Py_NewRef(Py_None));
    }
};


/* Implicitly convert any C++ value into a py::Object by invoking as_object(). */
template <impl::cpp_like T> requires (__as_object__<T>::enable)
struct __init__<Object, T>                                  : Returns<Object> {
    static auto operator()(const T& value) {
        return reinterpret_steal<Object>(release(as_object(value)));
    }
};


/// NOTE: additional delegating constructors for py::Object are defined in common.h


/* Implicitly convert a py::Object (or any of its subclasses) into one of its
subclasses by applying a runtime type check. */
template <std::derived_from<Object> From, std::derived_from<From> To>
struct __cast__<From, To>                                   : Returns<To> {
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
struct __cast__<Object, To>                                 : Returns<To> {
    static auto operator()(const Object& self) {
        return self.operator typename __as_object__<To>::type().operator To();
    }
};


/* Explicitly convert a py::Object (or any of its subclasses) into a C++ integer by
calling `int(obj)` at the Python level. */
template <std::derived_from<Object> From, std::integral To>
struct __explicit_cast__<From, To>                          : Returns<To> {
    static To operator()(const From& from) {
        long long result = PyLong_AsLongLong(ptr(from));
        if (result == -1 && PyErr_Occurred()) {
            Exception::from_python();
        }
        constexpr auto min = std::numeric_limits<To>::min();
        constexpr auto max = std::numeric_limits<To>::max();
        if (result < min || result > max) {
            std::string message = "integer out of range for ";
            message += BERTRAND_STRINGIFY(To);
            message += ": ";
            message += std::to_string(result);
            throw OverflowError(message);
        }
        return result;
    }
};


/* Explicitly convert a py::Object (or any of its subclasses) into a C++ floating-point
number by calling `float(obj)` at the Python level. */
template <std::derived_from<Object> From, std::floating_point To>
struct __explicit_cast__<From, To>                          : Returns<To> {
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
template <std::derived_from<Object> From, impl::cpp_like To>
    requires (impl::complex_like<To>)
struct __explicit_cast__<From, To>                          : Returns<To> {
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
struct __explicit_cast__<From, std::string>                 : Returns<std::string> {
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
struct __explicit_cast__<From, To>                          : Returns<To> {
    static auto operator()(const From& from) {
        return static_cast<To>(static_cast<typename __as_object__<To>::type>(from));
    }
};


}  // namespace py


#endif
