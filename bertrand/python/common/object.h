#if !defined(BERTRAND_PYTHON_COMMON_INCLUDED) && !defined(LINTER)
#error "This file should not be included directly.  Please include <bertrand/common.h> instead."
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
        ) {}

}


/* A dynamically-typed Python object that allows implicit and explicit conversions to
to arbitrary types via pybind11::cast(), as well as type-safe attribute access and
operator overloads.  This is the base class for all Bertrand-enabled Python objects. */
class Object {
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
        if (Self::typecheck(obj)) {
            return obj;
        } else {
            throw impl::noconvert<Self>(obj.ptr());
        }
    }

public:
    static const Type type;

    /* Check whether the templated type is considered object-like at compile time. */
    template <typename T>
    [[nodiscard]] static constexpr bool typecheck() {
        return std::derived_from<T, Object> || std::derived_from<T, pybind11::object>;
    }

    /* Check whether a Python/C++ value is considered object-like at compile time. */
    template <typename T>
    [[nodiscard]] static constexpr bool typecheck(const T& value) {
        if constexpr (impl::cpp_like<T>) {
            return typecheck<T>();
        } else {
            return value.ptr() != nullptr;
        }
    }

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Default constructor.  Initializes to None. */
    Object() : m_ptr(Py_NewRef(Py_None)) {}

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

    /* Copy assignment operator. */
    Object& operator=(const Object& other) {
        std::cout << "object copy assignment\n";  // TODO: remove print statement
        if (this != &other) {
            PyObject* temp = m_ptr;
            m_ptr = Py_XNewRef(other.m_ptr);
            Py_XDECREF(temp);
        }
        return *this;
    }

    /* Move assignment operator. */
    Object& operator=(Object&& other) {
        std::cout << "object move assignment\n";  // TODO: remove print statement
        if (this != &other) {
            PyObject* temp = m_ptr;
            m_ptr = other.m_ptr;
            other.m_ptr = nullptr;
            Py_XDECREF(temp);
        }
        return *this;
    }

    /* Destructor.  Allows any object to be stored with static duration. */
    ~Object() noexcept {
        if (Py_IsInitialized()) {
            Py_XDECREF(m_ptr);
        }
    }

    ////////////////////////////
    ////    BASE METHODS    ////
    ////////////////////////////

    /* NOTE: the Object wrapper can be implicitly converted to any of its subclasses by
     * applying a runtime type check as part of the assignment.  This allows us to
     * safely convert from a generic object to a more specialized type without worrying
     * about type mismatches or triggering arbitrary conversion logic.  It allows us to
     * write code like this:
     *
     *      py::Object obj = true;
     *      py::Bool b = obj;
     *
     * But not like this:
     *
     *      py::Object obj = true;
     *      py::Str s = obj;  // throws a TypeError
     *
     * While simultaneously preserving the ability to explicitly convert using a normal
     * constructor call:
     *
     *      py::Object obj = true;
     *      py::Str s(obj);
     *
     * Which is identical to calling `str()` at the python level.  Note that the
     * implicit conversion operator is only enabled for Object itself, and is deleted
     * in all of its subclasses.  This prevents implicit conversions between subclasses
     * and promotes any attempt to do so into a compile-time error.  For instance:
     *
     *      py::Bool b = true;
     *      py::Str s = b;  // fails to compile, calls a deleted function
     *
     * In general, this makes assignment via the `=` operator type-safe by default,
     * while explicit constructors are reserved for non-trivial conversions and/or
     * packing in the case of containers.
     */

    /* Contextually convert an Object into a boolean value for use in if/else 
    statements, with the same semantics as in Python. */
    [[nodiscard]] explicit operator bool() const {
        int result = PyObject_IsTrue(m_ptr);
        if (result == -1) {
            Exception::from_python();
        }
        return result;
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

    /* Check for exact pointer identity. */
    [[nodiscard]] bool is(const Object& other) const {
        return m_ptr == other.ptr();
    }

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    /* Attribute access operator.  Takes a template string for type safety using the
    __getattr__, __setattr__, and __delattr__ control structs. */
    template <StaticStr name, typename Self>
    [[nodiscard]] impl::Attr<Self, name> attr(this const Self& self) {
        return impl::Attr<Self, name>(self);
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
        using Return = typename __call__<std::remove_cvref_t<Self>, Args...>::Return;
        static_assert(
            std::is_void_v<Return> || std::derived_from<Return, Object>,
            "Call operator must return either void or a py::Object subclass.  "
            "Check your specialization of __call__ for the given arguments and "
            "ensure that it is derived from py::Object."
        );
        return ops::call<Return, std::remove_cvref_t<Self>, Args...>{}(
            self, std::forward<Args>(args)...
        );
    }

    /* Index operator.  Specific key and element types can be controlled via the
    __getitem__, __setitem__, and __delitem__ control structs. */
    template <typename Self, typename Key> requires (__getitem__<Self, Key>::enable)
    auto operator[](this const Self& self, const Key& key) {
        using Return = typename __getitem__<Self, Key>::Return;
        if constexpr (impl::proxy_like<Key>) {
            return self[key.value()];
        } else {
            return ops::getitem<Return, Self, Key>{}(self, key);
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

    /* Contains operator.  Equivalent to Python's `in` keyword, but with reversed
    operands (i.e. `x in y` -> `y.contains(x)`).  This is consistent with other STL
    container types, and the allowable key types can be specified via the __contains__
    control struct. */
    template <typename Self, typename Key> requires (__contains__<Self, Key>::enable)
    [[nodiscard]] bool contains(this const Self& self, const Key& key) {
        using Return = typename __contains__<Self, Key>::Return;
        static_assert(
            std::same_as<Return, bool>,
            "contains() operator must return a boolean value.  Check your "
            "specialization of __contains__ for these types and ensure the Return "
            "type is set to bool."
        );
        if constexpr (impl::proxy_like<Key>) {
            return self.contains(key.value());
        } else {
            return ops::contains<Return, Self, Key>{}(self, key);
        }
    }

    /* Length operator.  Equivalent to Python's `len()` function.  This can be enabled
    via the __len__ control struct. */
    template <typename Self> requires (__len__<Self>::enable)
    [[nodiscard]] size_t size(this const Self& self) {
        using Return = typename __len__<Self>::Return;
        static_assert(
            std::same_as<Return, size_t>,
            "size() operator must return a size_t for compatibility with C++ "
            "containers.  Check your specialization of __len__ for these types "
            "and ensure the Return type is set to size_t."
        );
        return ops::len<Return, Self>{}(self);
    }

    /* Begin iteration operator.  Both this and the end iteration operator are
    controlled by the __iter__ control struct, whose return type dictates the
    iterator's dereference type. */
    template <typename Self> requires (__iter__<Self>::enable)
    [[nodiscard]] auto begin(this const Self& self) {
        using Return = typename __iter__<Self>::Return;
        static_assert(
            std::derived_from<Return, Object>,
            "iterator must dereference to a subclass of Object.  Check your "
            "specialization of __iter__ for this types and ensure the Return type "
            "is a subclass of py::Object."
        );
        return ops::begin<Return, Self>{}(self);
    }

    /* Const iteration operator.  Python has no distinction between mutable and
    immutable iterators, so this is fundamentally the same as the ordinary begin()
    method.  Some libraries assume the existence of this method. */
    template <typename Self> requires (__iter__<Self>::enable)
    [[nodiscard]] auto cbegin(this const Self& self) {
        return self.begin();
    }

    /* End iteration operator.  This terminates the iteration and is controlled by the
    __iter__ control struct. */
    template <typename Self> requires (__iter__<Self>::enable)
    [[nodiscard]] auto end(this const Self& self) {
        using Return = typename __iter__<Self>::Return;
        static_assert(
            std::derived_from<Return, Object>,
            "iterator must dereference to a subclass of Object.  Check your "
            "specialization of __iter__ for this types and ensure the Return type "
            "is a subclass of py::Object."
        );
        return ops::end<Return, Self>{}(self);
    }

    /* Const end operator.  Similar to `cbegin()`, this is identical to `end()`. */
    template <typename Self> requires (__iter__<Self>::enable)
    [[nodiscard]] auto cend(this const Self& self) {
        return self.end();
    }

    /* Reverse iteration operator.  Both this and the reverse end operator are
    controlled by the __reversed__ control struct, whose return type dictates the
    iterator's dereference type. */
    template <typename Self> requires (__reversed__<Self>::enable)
    [[nodiscard]] auto rbegin(this const Self& self) {
        using Return = typename __reversed__<Self>::Return;
        static_assert(
            std::derived_from<Return, Object>,
            "iterator must dereference to a subclass of Object.  Check your "
            "specialization of __reversed__ for this types and ensure the Return "
            "type is a subclass of py::Object."
        );
        return ops::rbegin<Return, Self>{}(self);
    }

    /* Const reverse iteration operator.  Python has no distinction between mutable
    and immutable iterators, so this is fundamentally the same as the ordinary
    rbegin() method.  Some libraries assume the existence of this method. */
    template <typename Self> requires (__reversed__<Self>::enable)
    [[nodiscard]] auto crbegin(this const Self& self) {
        return self.rbegin();
    }

    /* Reverse end operator.  This terminates the reverse iteration and is controlled
    by the __reversed__ control struct. */
    template <typename Self> requires (__reversed__<Self>::enable)
    [[nodiscard]] auto rend(this const Self& self) {
        using Return = typename __reversed__<Self>::Return;
        static_assert(
            std::derived_from<Return, Object>,
            "iterator must dereference to a subclass of Object.  Check your "
            "specialization of __reversed__ for this types and ensure the Return "
            "type is a subclass of py::Object."
        );
        return ops::rend<Return, Self>{}(self);
    }

    /* Const reverse end operator.  Similar to `crbegin()`, this is identical to
    `rend()`. */
    template <typename Self> requires (__reversed__<Self>::enable)
    [[nodiscard]] auto crend(this const Self& self) {
        return self.rend();
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


template <impl::cpp_like T>
struct __init__<Object, T>                                  : Returns<Object> {
    static auto operator()(const T& value) {
        try {
            return reinterpret_steal<Object>(pybind11::cast(value).release());
        } catch (...) {
            Exception::from_pybind11();
        }
    }
};


template <std::derived_from<Object> From>
struct __cast__<From, pybind11::handle>                     : Returns<pybind11::handle> {
    static pybind11::handle operator()(const From& from) {
        return from.ptr();
    }
};


template <std::derived_from<Object> From>
struct __cast__<From, pybind11::object>                     : Returns<pybind11::object> {
    static auto operator()(const From& from) {
        return pybind11::reinterpret_borrow<pybind11::object>(from.ptr());
    }
    static auto operator()(From&& from) {
        return pybind11::reinterpret_steal<pybind11::object>(from.release().ptr());
    }
};


template <std::derived_from<Object> From, impl::pybind11_like To>
    requires (
        !std::same_as<To, pybind11::handle> &&
        !std::same_as<To, pybind11::object> &&
        From::template typecheck<To>()
        // issubclass<To, From>()  // TODO: replace typecheck with this
    )
struct __cast__<From, To>                                   : Returns<To> {
    static auto operator()(const From& from) {
        return pybind11::reinterpret_borrow<To>(from.ptr());
    }
    static auto operator()(From&& from) {
        return pybind11::reinterpret_steal<To>(from.release().ptr());
    }
};


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


template <std::derived_from<Object> From, impl::proxy_like To>
    requires (__cast__<From, impl::unwrap_proxy<To>>::enable)
struct __cast__<From, To>                                   : Returns<To> {
    static auto operator()(const From& from) {
        return To(impl::implicit_cast<impl::unwrap_proxy<To>>(from));
    }
};


template <impl::not_proxy_like To>
    requires (
        !std::is_pointer_v<To> &&
        !std::is_reference_v<To> &&
        !std::derived_from<To, pybind11::handle> &&
        !std::derived_from<To, pybind11::arg> &&
        !std::derived_from<To, Object>  // TODO: all internal objects should inherit from a shared tag
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



// TODO: default explicit conversion to integer, float, complex, void*, etc. ??



template <std::derived_from<Object> From, typename To>
struct __explicit_cast__<From, To>                          : Returns<To> {
    static auto operator()(const From& from) {
        try {
            return Handle(from.ptr()).template cast<To>();
        } catch (...) {
            Exception::from_pybind11();
        }
    }
};


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


}  // namespace py
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_COMMON_OBJECT_H
