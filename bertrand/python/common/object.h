#if !defined(BERTRAND_PYTHON_COMMON_INCLUDED) && !defined(LINTER)
#error "This file should not be included directly.  Please include <bertrand/common.h> instead."
#endif

#ifndef BERTRAND_PYTHON_COMMON_OBJECT_H
#define BERTRAND_PYTHON_COMMON_OBJECT_H

#include "declarations.h"
#include "concepts.h"
#include "exceptions.h"
#include "operators.h"


namespace bertrand {
namespace py {


namespace impl {

    template <typename Obj, typename Key> requires (__getitem__<Obj, Key>::enable)
    class Item;
    template <typename Obj, StaticStr name> requires (__getattr__<Obj, name>::enable)
    class Attr;
    template <typename Policy>
    class Iterator;
    template <typename Policy>
    class ReverseIterator;
    template <typename Deref>
    class GenericIter;

}


/* A revised Python object interface that allows implicit conversions to subtypes
(applying a type check on the way), explicit conversions to arbitrary C++ types,
type-safe operators, and generalized slice/attr syntax. */
class Object {
protected:
    PyObject* m_ptr;

    /* Protected tags mirror pybind11::object and allow for the use of
    reinterpret_borrow<>, reinterpret_steal<> for bertrand types. */
    struct borrowed_t {};
    struct stolen_t {};

    template <std::derived_from<Object> T>
    friend T reinterpret_borrow(Handle);
    template <std::derived_from<Object> T>
    friend T reinterpret_steal(Handle);

    template <typename Self, typename Policy>
    static pybind11::object from_pybind11_accessor(
        const pybind11::detail::accessor<Policy>& accessor
    ) {
        pybind11::object obj(accessor);
        if (Self::check(obj)) {
            return obj;
        } else {
            throw impl::noconvert<Self>(obj.ptr());
        }
    }

public:
    static const Type type;

    /* Check whether the templated type is considered object-like at compile time. */
    template <typename T>
    static constexpr bool check() {
        return std::derived_from<T, Object> || std::derived_from<T, pybind11::object>;
    }

    /* Check whether a Python/C++ value is considered object-like at compile time. */
    template <typename T>
    static constexpr bool check(const T& value) {
        if constexpr (impl::cpp_like<T>) {
            return check<T>();
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
    Object(Handle ptr, const borrowed_t&) : m_ptr(Py_XNewRef(ptr.ptr())) {}

    /* reinterpret_steal() constructor.  Steals a reference to a raw Python handle. */
    Object(Handle ptr, const stolen_t&) : m_ptr(ptr.ptr()) {}

    /* Convert a pybind11 accessor into a generic Object. */
    template <typename Policy>
    Object(const pybind11::detail::accessor<Policy>& accessor) : m_ptr(nullptr) {
        pybind11::object obj(accessor);
        if (check(obj)) {
            m_ptr = obj.release().ptr();
        } else {
            throw impl::noconvert<Object>(obj.ptr());
        }
    }

    /* Copy constructor.  Borrows a reference to an existing object. */
    Object(const Object& other) : m_ptr(Py_XNewRef(other.m_ptr)) {}

    /* Move constructor.  Steals a reference to a temporary object. */
    Object(Object&& other) : m_ptr(other.m_ptr) { other.m_ptr = nullptr; }

    /* Copy constructor from equivalent pybind11 type. */
    Object(const pybind11::object& other) : m_ptr(Py_XNewRef(other.ptr())) {}

    /* Move constructor from equivalent pybind11 type. */
    Object(pybind11::object&& other) : m_ptr(other.release().ptr()) {}

    /* Convert any C++ value into a generic python object. */
    template <impl::cpp_like T>
    Object(const T& value) : m_ptr([&value] {
        try {
            return pybind11::cast(value).release().ptr();
        } catch (...) {
            Exception::from_pybind11();
        }
    }()) {}

    /* Copy assignment operator. */
    Object& operator=(const Object& other) {
        std::cout << "object copy assignment\n";
        if (this != &other) {
            PyObject* temp = m_ptr;
            m_ptr = Py_XNewRef(other.m_ptr);
            Py_XDECREF(temp);
        }
        return *this;
    }

    /* Move assignment operator. */
    Object& operator=(Object&& other) {
        std::cout << "object move assignment\n";
        if (this != &other) {
            PyObject* temp = m_ptr;
            m_ptr = other.m_ptr;
            other.m_ptr = nullptr;
            Py_XDECREF(temp);
        }
        return *this;
    }

    /* Destructor allows any object to be stored with static duration. */
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
    inline explicit operator bool() const {
        int result = PyObject_IsTrue(m_ptr);
        if (result == -1) {
            Exception::from_python();
        }
        return result;
    }

    /* Explicitly cast to a string representation.  Equivalent to Python `str(obj)`. */
    inline explicit operator std::string() const {
        PyObject* str = PyObject_Str(m_ptr);
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

    /* Return the underlying PyObject* pointer. */
    [[nodiscard]] inline PyObject* ptr() const {
        return m_ptr;
    }

    /* Relinquish ownership over the object and return it as a raw handle. */
    [[nodiscard]] inline Handle release() {
        PyObject* temp = m_ptr;
        m_ptr = nullptr;
        return Handle(temp);
    }

    /* Check for exact pointer identity. */
    [[nodiscard]] inline bool is(const Handle& other) const {
        return m_ptr == other.ptr();
    }

    /* Check for exact pointer identity. */
    [[nodiscard]] inline bool is(const Object& other) const {
        return m_ptr == other.ptr();
    }

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    /* Attribute access operator.  Takes a template string for type safety using the
    __getattr__, __setattr__, and __delattr__ control structs. */
    template <StaticStr name, typename Self>
    impl::Attr<Self, name> attr(this const Self& self) {
        return impl::Attr<Self, name>(self);
    }

    /* Implicit conversion operator.  Implicit conversions can be registered for any
    type via the __cast__ control struct.  Further implicit conversions should not be
    implemented, as it can lead to template ambiguities and unexpected behavior.
    Ambiguities can still arise via the control struct, but they are more predictable
    and avoidable. */
    template <typename Self, typename T>
        requires (__cast__<Self, T>::enable && !impl::initializer_like<T>)
    operator T(this const Self& self) {
        return __cast__<Self, T>::operator()(self);
    }

    /* Explicit conversion operator.  This defers to implicit conversions where
    possible, and forwards all other conversions to pybind11's cast() mechanism.  Along
    with implicit and explicit constructors, this obviates most uses of
    `pybind11::cast<T>()` and replaces it with the native `static_cast<T>()` and
    implicit conversions instead.  */
    template <typename Self, typename T> requires (!__cast__<Self, T>::enable)
    explicit operator T(this const Self& self) {
        try {
            return Handle(self.ptr()).template cast<T>();
        } catch (...) {
            Exception::from_pybind11();
        }
    }

    /* Call operator.  This can be enabled for specific argument signatures and return
    types via the __call__ control struct, enabling static type safety for Python
    functions in C++. */
    template <typename Self, typename... Args> requires (__call__<Self, Args...>::enable)
    auto operator()(this const Self& self, Args&&... args) {
        using Return = typename __call__<Self, Args...>::Return;
        static_assert(
            std::is_void_v<Return> || std::derived_from<Return, Object>,
            "Call operator must return either void or a py::Object subclass.  "
            "Check your specialization of __call__ for the given arguments and "
            "ensure that it is derived from py::Object."
        );
        return impl::ops::call<Return, Self, Args...>::operator()(
            self, std::forward<Args>(args)...
        );
    }

    /* Index operator.  Specific key and element types can be controlled via the
    __getitem__, __setitem__, and __delitem__ control structs. */
    template <typename Self, typename Key> requires (__getitem__<Self, Key>::enable)
    [[nodiscard]] auto operator[](this const Self& self, const Key& key) {
        using Return = typename __getitem__<Self, Key>::Return;
        if constexpr (impl::proxy_like<Key>) {
            return self[key.value()];
        } else {
            return impl::ops::getitem<Return, Self, Key>::operator()(self, key);
        }
    }

    /* Slice operator.  This is just syntactic sugar for the index operator with a
    py::Slice operand, allowing users to specify slices using a condensed initializer
    list. */
    template <typename Self> requires (__getitem__<Self, Slice>::enable)
    [[nodiscard]] auto operator[](
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
            return impl::ops::contains<Return, Self, Key>::operator()(self, key);
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
        return impl::ops::len<Return, Self>::operator()(self);
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
        return impl::ops::begin<Return, Self>::operator()(self);
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
        return impl::ops::end<Return, Self>::operator()(self);
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
        return impl::ops::rbegin<Return, Self>::operator()(self);
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
        return impl::ops::rend<Return, Self>::operator()(self);
    }

    /* Const reverse end operator.  Similar to `crbegin()`, this is identical to
    `rend()`. */
    template <typename Self> requires (__reversed__<Self>::enable)
    [[nodiscard]] auto crend(this const Self& self) {
        return self.rend();
    }

};


// NOTE: Object implicitly allows all operators, but will defer to a subclass if
// combined with one in a binary operation.  This reduces the need to treat Object
// as a special case in the operator overloads.
template <typename ... Args>
struct __call__<Object, Args...>                            : Returns<Object> {};
template <StaticStr name> requires (!impl::getattr_helper<name>::enable)
struct __getattr__<Object, name>                            : Returns<Object> {};
template <StaticStr name, typename Value> requires (!impl::setattr_helper<name>::enable)
struct __setattr__<Object, name, Value>                     : Returns<void> {};
template <StaticStr name> requires (!impl::delattr_helper<name>::enable)
struct __delattr__<Object, name>                            : Returns<void> {};
template <typename Key>
struct __getitem__<Object, Key>                             : Returns<Object> {};
template <typename Key, typename Value>
struct __setitem__<Object, Key, Value>                      : Returns<void> {};
template <typename Key>
struct __delitem__<Object, Key>                             : Returns<void> {};
template <>
struct __len__<Object>                                      : Returns<size_t> {};
template <>
struct __iter__<Object>                                     : Returns<Object> {};
template <>
struct __reversed__<Object>                                 : Returns<Object> {};
template <typename T>
struct __contains__<Object, T>                              : Returns<bool> {};
template <>
struct __hash__<Object>                                     : Returns<size_t> {};
template <>
struct __abs__<Object>                                      : Returns<Object> {};
template <>
struct __invert__<Object>                                   : Returns<Object> {};
template <>
struct __pos__<Object>                                      : Returns<Object> {};
template <>
struct __neg__<Object>                                      : Returns<Object> {};
template <>
struct __increment__<Object>                                : Returns<Object> {};
template <>
struct __decrement__<Object>                                : Returns<Object> {};
template <>
struct __lt__<Object, Object>                               : Returns<bool> {};
template <std::convertible_to<Object> R>
struct __lt__<Object, R>                                    : Returns<bool> {};
template <std::convertible_to<Object> L>
struct __lt__<L, Object>                                    : Returns<bool> {};
template <>
struct __le__<Object, Object>                               : Returns<bool> {};
template <std::convertible_to<Object> R>
struct __le__<Object, R>                                    : Returns<bool> {};
template <std::convertible_to<Object> L>
struct __le__<L, Object>                                    : Returns<bool> {};
template <>
struct __eq__<Object, Object>                               : Returns<bool> {};
template <std::convertible_to<Object> R>
struct __eq__<Object, R>                                    : Returns<bool> {};
template <std::convertible_to<Object> L>
struct __eq__<L, Object>                                    : Returns<bool> {};
template <>
struct __ne__<Object, Object>                               : Returns<bool> {};
template <std::convertible_to<Object> R>
struct __ne__<Object, R>                                    : Returns<bool> {};
template <std::convertible_to<Object> L>
struct __ne__<L, Object>                                    : Returns<bool> {};
template <>
struct __ge__<Object, Object>                               : Returns<bool> {};
template <std::convertible_to<Object> R>
struct __ge__<Object, R>                                    : Returns<bool> {};
template <std::convertible_to<Object> L>
struct __ge__<L, Object>                                    : Returns<bool> {};
template <>
struct __gt__<Object, Object>                               : Returns<bool> {};
template <std::convertible_to<Object> R>
struct __gt__<Object, R>                                    : Returns<bool> {};
template <std::convertible_to<Object> L>
struct __gt__<L, Object>                                    : Returns<bool> {};
template <>
struct __add__<Object, Object>                              : Returns<Object> {};
template <std::convertible_to<Object> R>
struct __add__<Object, R>                                   : Returns<Object> {};
template <std::convertible_to<Object> L>
struct __add__<L, Object>                                   : Returns<Object> {};
template <>
struct __iadd__<Object, Object>                             : Returns<Object&> {};
template <std::convertible_to<Object> R>
struct __iadd__<Object, R>                                  : Returns<Object&> {};
template <>
struct __sub__<Object, Object>                              : Returns<Object> {};
template <std::convertible_to<Object> R>
struct __sub__<Object, R>                                   : Returns<Object> {};
template <std::convertible_to<Object> L>
struct __sub__<L, Object>                                   : Returns<Object> {};
template <>
struct __isub__<Object, Object>                             : Returns<Object&> {};
template <std::convertible_to<Object> R>
struct __isub__<Object, R>                                  : Returns<Object&> {};
template <>
struct __mul__<Object, Object>                              : Returns<Object> {};
template <std::convertible_to<Object> R>
struct __mul__<Object, R>                                   : Returns<Object> {};
template <std::convertible_to<Object> L>
struct __mul__<L, Object>                                   : Returns<Object> {};
template <>
struct __imul__<Object, Object>                             : Returns<Object&> {};
template <std::convertible_to<Object> R>
struct __imul__<Object, R>                                  : Returns<Object&> {};
template <>
struct __truediv__<Object, Object>                          : Returns<Object> {};
template <std::convertible_to<Object> R>
struct __truediv__<Object, R>                               : Returns<Object> {};
template <std::convertible_to<Object> L>
struct __truediv__<L, Object>                               : Returns<Object> {};
template <>
struct __itruediv__<Object, Object>                         : Returns<Object&> {};
template <std::convertible_to<Object> R>
struct __itruediv__<Object, R>                              : Returns<Object&> {};
template <>
struct __floordiv__<Object, Object>                         : Returns<Object> {};
template <std::convertible_to<Object> R>
struct __floordiv__<Object, R>                              : Returns<Object> {};
template <std::convertible_to<Object> L>
struct __floordiv__<L, Object>                              : Returns<Object> {};
template <>
struct __mod__<Object, Object>                              : Returns<Object> {};
template <std::convertible_to<Object> R>
struct __mod__<Object, R>                                   : Returns<Object> {};
template <std::convertible_to<Object> L>
struct __mod__<L, Object>                                   : Returns<Object> {};
template <>
struct __imod__<Object, Object>                             : Returns<Object&> {};
template <std::convertible_to<Object> R>
struct __imod__<Object, R>                                  : Returns<Object&> {};
template <>
struct __pow__<Object, Object>                              : Returns<Object> {};
template <std::convertible_to<Object> R>
struct __pow__<Object, R>                                   : Returns<Object> {};
template <std::convertible_to<Object> L>
struct __pow__<L, Object>                                   : Returns<Object> {};
template <>
struct __lshift__<Object, Object>                           : Returns<Object> {};
template <std::convertible_to<Object> R>
struct __lshift__<Object, R>                                : Returns<Object> {};
template <std::convertible_to<Object> L>
struct __lshift__<L, Object>                                : Returns<Object> {};
template <>
struct __ilshift__<Object, Object>                          : Returns<Object&> {};
template <std::convertible_to<Object> R>
struct __ilshift__<Object, R>                               : Returns<Object&> {};
template <>
struct __rshift__<Object, Object>                           : Returns<Object> {};
template <std::convertible_to<Object> R>
struct __rshift__<Object, R>                                : Returns<Object> {};
template <std::convertible_to<Object> L>
struct __rshift__<L, Object>                                : Returns<Object> {};
template <>
struct __irshift__<Object, Object>                          : Returns<Object&> {};
template <std::convertible_to<Object> R>
struct __irshift__<Object, R>                               : Returns<Object&> {};
template <>
struct __and__<Object, Object>                              : Returns<Object> {};
template <std::convertible_to<Object> R>
struct __and__<Object, R>                                   : Returns<Object> {};
template <std::convertible_to<Object> L>
struct __and__<L, Object>                                   : Returns<Object> {};
template <>
struct __iand__<Object, Object>                             : Returns<Object&> {};
template <std::convertible_to<Object> R>
struct __iand__<Object, R>                                  : Returns<Object&> {};
template <>
struct __or__<Object, Object>                               : Returns<Object> {};
template <std::convertible_to<Object> R>
struct __or__<Object, R>                                    : Returns<Object> {};
template <std::convertible_to<Object> L>
struct __or__<L, Object>                                    : Returns<Object> {};
template <>
struct __ior__<Object, Object>                              : Returns<Object&> {};
template <std::convertible_to<Object> R>
struct __ior__<Object, R>                                   : Returns<Object&> {};
template <>
struct __xor__<Object, Object>                              : Returns<Object> {};
template <std::convertible_to<Object> R>
struct __xor__<Object, R>                                   : Returns<Object> {};
template <std::convertible_to<Object> L>
struct __xor__<L, Object>                                   : Returns<Object> {};
template <>
struct __ixor__<Object, Object>                             : Returns<Object&> {};
template <std::convertible_to<Object> R>
struct __ixor__<Object, R>                                  : Returns<Object&> {};


/* Implicitly convert Object to any type that is not a pointer or reference type, an
internal pybind11 type, or a subclass of Object using pybind11's cast() mechanism. */
template <impl::not_proxy_like T>
    requires (
        !std::is_pointer_v<T> &&
        !std::is_reference_v<T> &&
        !std::same_as<T, pybind11::handle> &&
        !std::derived_from<T, pybind11::object> &&
        !std::derived_from<T, pybind11::arg> &&
        !std::derived_from<T, Object>
    )
struct __cast__<Object, T> : Returns<T> {
    static T operator()(const Object& self) {
        try {
            return Handle(self.ptr()).template cast<T>();
        } catch (...) {
            Exception::from_pybind11();
        }
    }
};


/* Borrow a reference to a raw Python handle. */
template <std::derived_from<Object> T>
T reinterpret_borrow(Handle obj) {
    return T(obj, Object::borrowed_t{});
}


/* Borrow a reference to a raw Python handle. */
template <std::derived_from<pybind11::object> T>
T reinterpret_borrow(Handle obj) {
    return pybind11::reinterpret_borrow<T>(obj);
}


/* Steal a reference to a raw Python handle. */
template <std::derived_from<Object> T>
T reinterpret_steal(Handle obj) {
    return T(obj, Object::stolen_t{});
}


/* Steal a reference to a raw Python handle. */
template <std::derived_from<pybind11::object> T>
T reinterpret_steal(Handle obj) {
    return pybind11::reinterpret_steal<T>(obj);
}


template <typename Return, typename L, typename R>
Return impl::ops::sequence::mul<Return, L, R>::operator()(const L& lhs, const R& rhs) {
    if constexpr (impl::python_like<L>) {
        PyObject* result = PySequence_Repeat(lhs.ptr(), rhs);
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Return>(result);
    } else if constexpr (impl::python_like<R>) {
        PyObject* result = PySequence_Repeat(rhs.ptr(), lhs);
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Return>(result);
    } else {
        PyObject* result = PySequence_Repeat(Object(rhs).ptr(), lhs);
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Return>(result);
    }
}


}  // namespace py
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_COMMON_OBJECT_H
