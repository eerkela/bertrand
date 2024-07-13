#ifndef BERTRAND_PYTHON_MODULE_GUARD
#error "Internal headers should not be included directly.  Import 'bertrand.python' instead."
#endif

#ifndef BERTRAND_PYTHON_COMMON_OBJECT_H
#define BERTRAND_PYTHON_COMMON_OBJECT_H

#include "declarations.h"
#include "except.h"
#include "ops.h"


namespace bertrand {
namespace py {


namespace impl {

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

    /* A convenience macro that defines all of the required constructors for a subclass
    of py::Object.  This allows implicit and explicit constructors to be defined out of
    line by specializing the py::__init__ and py::__explicit_init__ control structures.

    NOTE: this macro can obfuscate error messages to some degree, so it's not used for
    any of the built-in types.  It does, however, present a uniform and
    backwards-compatible entry point for user-defined types, so they don't have to
    concern themselves with the intricacies of the control struct architecture. */
    #define BERTRAND_INIT(cls) \
        cls(::bertrand::py::Handle h, borrowed_t t) : Base(h, t) {} \
        cls(::bertrand::py::Handle h, stolen_t t) : Base(h, t) {} \
        \
        template <typename... Args> \
            requires ( \
                std::is_invocable_r_v< \
                    cls, \
                    ::bertrand::py::__init__<cls, std::remove_cvref_t<Args>...>, \
                    Args... \
                > && \
                ::bertrand::py::__init__<cls, std::remove_cvref_t<Args>...>::enable \
            ) \
        cls(Args&&... args) : Base( \
            ::bertrand::py::__init__<cls, std::remove_cvref_t<Args>...>{}( \
                std::forward<Args>(args)... \
            ) \
        ) {} \
        \
        template <typename... Args> \
            requires ( \
                !::bertrand::py::__init__<cls, std::remove_cvref_t<Args>...>::enable && \
                std::is_invocable_r_v< \
                    cls, \
                    ::bertrand::py::__explicit_init__<cls, std::remove_cvref_t<Args>...>, \
                    Args... \
                > && \
                ::bertrand::py::__explicit_init__<cls, std::remove_cvref_t<Args>...>::enable \
            ) \
        explicit cls(Args&&... args) : Base( \
            ::bertrand::py::__explicit_init__<cls, std::remove_cvref_t<Args>...>{}( \
                std::forward<Args>(args)... \
            ) \
        ) {} \
        \
        template <typename... Args> \
            requires ( \
                !::bertrand::py::__init__<cls, std::remove_cvref_t<Args>...>::enable && \
                !::bertrand::py::__explicit_init__<cls, std::remove_cvref_t<Args>...>::enable && \
                ::bertrand::py::impl::invocable<cls, "__init__", Args...> \
            ) \
        explicit cls(Args&&... args) : Base( \
            ::bertrand::py::__getattr__<cls, "__init__">::type::template set_return<cls>::invoke_py( \
                type.ptr(), \
                std::forward<Args>(args)... \
            ) \
        ) {}

}


/* A dynamically-typed Python object that allows implicit and explicit conversions to
to arbitrary types via pybind11::cast(), as well as type-safe attribute access and
operator overloads.  This is the base class for all Bertrand-enabled Python objects. */
class Object : public impl::BertrandTag {
protected:
    PyObject* m_ptr;

    /* Protected tags mirror those of pybind11::object and allow the use of
    reinterpret_borrow<>, reinterpret_steal<> for bertrand types. */
    struct borrowed_t {};
    struct stolen_t {};

    template <std::derived_from<Object> T>
    friend T reinterpret_borrow(Handle);
    template <std::derived_from<Object> T>
    friend T reinterpret_steal(Handle);

    // TODO: delete from_pybind11_accessor(), since it is superseded by constructor
    // control structures

    template <typename Self, typename Policy>
    static pybind11::object from_pybind11_accessor(
        const pybind11::detail::accessor<Policy>& accessor
    ) {
        pybind11::object obj(accessor);
        if (isinstance<Self>(obj)) {
            return obj;
        } else {
            throw impl::noconvert<Self>(obj.ptr());
        }
    }

public:
    static const Type type;

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Default constructor.  Initializes to None. */
    Object() : m_ptr((Interpreter::init(), Py_NewRef(Py_None))) {}

    /* reinterpret_borrow() constructor.  Borrows a reference to a raw Python handle. */
    Object(Handle h, borrowed_t) : m_ptr(Py_XNewRef(h.ptr())) {}

    /* reinterpret_steal() constructor.  Steals a reference to a raw Python handle. */
    Object(Handle h, stolen_t) : m_ptr(h.ptr()) {}

    /* Copy constructor.  Borrows a reference to an existing object. */
    Object(const Object& other) : m_ptr(Py_XNewRef(other.m_ptr)) {}

    /* Move constructor.  Steals a reference to a temporary object. */
    Object(Object&& other) : m_ptr(other.m_ptr) { other.m_ptr = nullptr; }

    /* Copy constructor from equivalent pybind11 type. */
    Object(const pybind11::object& other) : m_ptr(Py_XNewRef(other.ptr())) {}

    /* Move constructor from equivalent pybind11 type. */
    Object(pybind11::object&& other) : m_ptr(other.release().ptr()) {}

    /* Universal implicit constructor.  Implemented via the __init__ control struct. */
    template <typename... Args>
        requires (
            std::is_invocable_r_v<
                Object,
                __init__<Object, std::remove_cvref_t<Args>...>,
                Args...
            >
        )
    Object(Args&&... args) : Object(
        __init__<Object, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    ) {}

    /* Universal explicit constructor.  Implemented via the __explicit_init__ control
    struct. */
    template <typename... Args>
        requires (
            !__init__<Object, std::remove_cvref_t<Args>...>::enable &&
            std::is_invocable_r_v<
                Object,
                __explicit_init__<Object, std::remove_cvref_t<Args>...>,
                Args...
            >
        )
    explicit Object(Args&&... args) : Object(
        __explicit_init__<Object, std::remove_cvref_t<Args>...>{}(
            std::forward<Args>(args)...
        )
    ) {}

    /* Destructor.  Allows any object to be stored with static duration. */
    ~Object() noexcept {
        if (Py_IsInitialized()) {
            Py_XDECREF(m_ptr);
        }
    }

    /* Copy assignment operator. */
    Object& operator=(const Object& other) {
        print("object copy assignment");  // TODO: remove print statement
        if (this != &other) {
            PyObject* temp = m_ptr;
            m_ptr = Py_XNewRef(other.m_ptr);
            Py_XDECREF(temp);
        }
        return *this;
    }

    /* Move assignment operator. */
    Object& operator=(Object&& other) {
        print("object move assignment");  // TODO: remove print statement
        if (this != &other) {
            PyObject* temp = m_ptr;
            m_ptr = other.m_ptr;
            other.m_ptr = nullptr;
            Py_XDECREF(temp);
        }
        return *this;
    }

    /* Return the underlying PyObject* pointer. */
    [[nodiscard]] PyObject* ptr() const {
        return m_ptr;
    }

    /* Relinquish ownership over the object and return it as a raw handle. */
    [[nodiscard]] Handle release() {
        PyObject* temp = m_ptr;
        m_ptr = nullptr;
        return temp;
    }

    /* Check for exact pointer identity. */
    [[nodiscard]] bool is(const Handle& other) const {
        return m_ptr == other.ptr();
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
                self.ptr(),
                as_object(key).ptr()
            );
            if (result == -1) {
                Exception::from_python();
            }
            return result;
        }
    }

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

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
                self.ptr(),
                std::forward<Args>(args)...
            );
        } else {
            return Function<Return(Args...)>::template invoke_py<Return>(
                self.ptr(),
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


template <std::derived_from<Object> T>
[[nodiscard]] T reinterpret_borrow(Handle obj) {
    return T(obj, Object::borrowed_t{});
}


template <std::derived_from<Object> T>
[[nodiscard]] T reinterpret_steal(Handle obj) {
    return T(obj, Object::stolen_t{});
}


template <typename T>
struct __issubclass__<T, Object>                            : Returns<bool> {
    static consteval bool operator()() {
        return std::derived_from<T, Object> || std::derived_from<T, pybind11::object>;
    }
    static constexpr bool operator()(const T& obj) {
        return operator()();
    }
    static bool operator()(const T& obj, const Object& cls) {
        int result = PyObject_IsSubclass(
            as_object(obj).ptr(),
            cls.ptr()
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
            return obj.ptr() != nullptr;
        } else {
            return issubclass<Object>(obj);
        }
    }
    static bool operator()(const T& obj, const Object& cls) {
        int result = PyObject_IsInstance(
            as_object(obj).ptr(),
            cls.ptr()
        );
        if (result == -1) {
            Exception::from_python();
        }
        return result;
    }
};


/* Initialize a py::Object (or any of its subclasses) from a compatible pybind11
type. */
template <std::derived_from<Object> Self, impl::pybind11_like T>
    requires (issubclass<T, Self>())
struct __init__<Self, T>                                    : Returns<Self> {
    static auto operator()(const T& other) {
        return reinterpret_borrow<Self>(other.ptr());
    }
    static auto operator()(T&& other) {
        return reinterpret_steal<Self>(other.release().ptr());
    }
};


/* Initialize a py::Object (or any of its subclasses) from a compatible pybind11
accessor. */
template <std::derived_from<Object> Self, typename Policy>
struct __init__<Self, pybind11::detail::accessor<Policy>>   : Returns<Self> {
    static auto operator()(const pybind11::detail::accessor<Policy>& accessor) {
        pybind11::object obj(accessor);
        if (isinstance<Self>(obj)) {
            return reinterpret_steal<Self>(obj.release().ptr());
        } else {
            throw impl::noconvert<Self>(obj.ptr());
        }
    }
};


/* Convert any C++ value into a py::Object by invoking pybind11::cast(). */
template <impl::cpp_like T>
struct __init__<Object, T>                                  : Returns<Object> {
    static auto operator()(const T& value) {
        Interpreter::init();  // ensure the interpreter is initialized
        try {
            return reinterpret_steal<Object>(pybind11::cast(value).release());
        } catch (...) {
            Exception::from_pybind11();
        }
    }
};


/* Implicitly convert a py::Object (or any of its subclasses) into a
pybind11::handle. */
template <std::derived_from<Object> From>
struct __cast__<From, pybind11::handle>                     : Returns<pybind11::handle> {
    static pybind11::handle operator()(const From& from) {
        return from.ptr();
    }
};


/* Implicitly convert a py::Object (or any of its subclasses) into a
pybind11::object. */
template <std::derived_from<Object> From>
struct __cast__<From, pybind11::object>                     : Returns<pybind11::object> {
    static auto operator()(const From& from) {
        return pybind11::reinterpret_borrow<pybind11::object>(from.ptr());
    }
    static auto operator()(From&& from) {
        return pybind11::reinterpret_steal<pybind11::object>(from.release().ptr());
    }
};


/* Implicitly convert a py::Object (or any of its subclasses) into an equivalent
pybind11 type. */
template <std::derived_from<Object> From, impl::pybind11_like To>
    requires (
        !std::same_as<To, pybind11::handle> &&
        !std::same_as<To, pybind11::object> &&
        issubclass<To, From>()
    )
struct __cast__<From, To>                                   : Returns<To> {
    static auto operator()(const From& from) {
        return pybind11::reinterpret_borrow<To>(from.ptr());
    }
    static auto operator()(From&& from) {
        return pybind11::reinterpret_steal<To>(from.release().ptr());
    }
};


/* Implicitly convert a py::Object (or any of its subclasses) into one of its
subclasses by applying a runtime type check. */
template <std::derived_from<Object> From, std::derived_from<From> To>
struct __cast__<From, To>                                   : Returns<To> {
    static auto operator()(const From& from) {
        if (isinstance<To>(from)) {
            return reinterpret_borrow<To>(from.ptr());
        } else {
            throw impl::noconvert<To>(from.ptr());
        }
    }
    static auto operator()(From&& from) {
        if (isinstance<To>(from)) {
            return reinterpret_steal<To>(from.release().ptr());
        } else {
            throw impl::noconvert<To>(from.ptr());
        }
    }
};


/* Implicitly convert a py::Object into any C++ type by invoking pybind11::cast(). */
template <typename To>
    requires (
        !std::is_pointer_v<To> &&
        !std::is_reference_v<To> &&
        !impl::pybind11_like<To> &&
        !impl::bertrand_like<To>
    )
struct __cast__<Object, To>                                 : Returns<To> {
    static auto operator()(const Object& self) {
        try {
            return Handle(self.ptr()).template cast<To>();
        } catch (...) {
            Exception::from_pybind11();
        }
    }
};


/* Explicitly convert a py::Object (or any of its subclasses) into a C++ integer by
calling `int(obj)` at the Python level. */
template <std::derived_from<Object> From, std::integral To>
struct __explicit_cast__<From, To>                          : Returns<To> {
    static To operator()(const From& from) {
        long long result = PyLong_AsLongLong(from.ptr());
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
        double result = PyFloat_AsDouble(from.ptr());
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
        Py_complex result = PyComplex_AsCComplex(from.ptr());
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
        PyObject* str = PyObject_Str(from.ptr());
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


/* Explicitly convert a py::Object (or any of its subclasses) into any other C++ type
by invoking pybind11::cast(). */
template <std::derived_from<Object> From, typename To>
    requires (
        !std::is_pointer_v<To> &&
        !std::is_reference_v<To> &&
        !impl::pybind11_like<To> &&
        !impl::bertrand_like<To>
    )
struct __explicit_cast__<From, To>                          : Returns<To> {
    static auto operator()(const From& from) {
        try {
            return Handle(from.ptr()).template cast<To>();
        } catch (...) {
            Exception::from_pybind11();
        }
    }
};


}  // namespace py
}  // namespace bertrand


#endif
