#if !defined(BERTRAND_PYTHON_INCLUDED) && !defined(LINTER)
#error "This file should not be included directly.  Please include <bertrand/python.h> instead."
#endif

#ifndef BERTRAND_PYTHON_BOOL_H
#define BERTRAND_PYTHON_BOOL_H

#include "common.h"


namespace bertrand {
namespace py {


/* pybind11::bool_ equivalent with stronger type safety, math operators, etc. */
class Bool : public Object {
    using Base = Object;

    template <typename T>
    static constexpr bool constructor1 = !impl::is_python<T> && impl::bool_like<T>;
    template <typename T>
    static constexpr bool constructor2 = 
        !impl::is_python<T> && !impl::bool_like<T> &&
        impl::explicitly_convertible_to<T, bool>;
    template <typename T>
    static constexpr bool constructor3 = impl::is_python<T> && !impl::bool_like<T>;
    template <typename T>
    static constexpr bool constructor4 =
        !constructor1<T> && !constructor2<T> && !constructor3<T> && impl::has_empty<T>;
    template <typename T>
    static constexpr bool constructor5 =
        !constructor1<T> && !constructor2<T> && !constructor3<T> && !constructor4<T> &&
        impl::has_size<T>;

public:
    static Type type;

    template <typename T>
    static constexpr bool check() { return impl::bool_like<T>; }

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Copy/move constructors from equivalent pybind11 type. */
    Bool(const pybind11::bool_& other) : Base(other.ptr(), borrowed_t{}) {}
    Bool(pybind11::bool_&& other) : Base(other.release(), stolen_t{}) {}

    BERTRAND_OBJECT_COMMON(Base, Bool, PyBool_Check)

    /* Default constructor.  Initializes to False. */
    Bool() : Base(Py_False, borrowed_t{}) {}

    /* Implicitly convert C++ booleans into py::Bool. */
    template <typename T>
        requires constructor1<T>
    Bool(const T& value) : Base(value ? Py_True : Py_False, borrowed_t{}) {}

    /* Trigger explicit conversion operators to bool. */
    template <typename T>
        requires constructor2<T>
    explicit Bool(const T& value) : Bool(static_cast<bool>(value)) {}

    /* Explicitly convert an arbitrary Python object into a boolean. */
    template <typename T>
        requires constructor3<T>
    explicit Bool(const T& obj) {
        int result = PyObject_IsTrue(obj.ptr());
        if (result == -1) {
            throw error_already_set();
        }
        m_ptr = Py_NewRef(result ? Py_True : Py_False);
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Explicitly convert any C++ object that implements a `.empty()` method into a
    py::Bool. */
    template <typename T>
        requires constructor4<T>
    explicit Bool(const T& obj) : Bool(!obj.empty()) {}

    /* Explicitly convert any C++ object that implements a `.size()` method into a
    py::Bool. */
    template <typename T>
        requires constructor5<T>
    explicit Bool(const T& obj) : Bool(obj.size() > 0) {}

    /* Explicitly convert a std::tuple into a py::Bool. */
    template <typename... Args>
    explicit Bool(const std::tuple<Args...>& obj) : Bool(sizeof...(Args) > 0) {}

    /* Explicitly convert a string literal into a py::Bool. */
    explicit Bool(const char* str) : Bool(std::string(str)) {}

    ///////////////////////////
    ////    CONVERSIONS    ////
    ///////////////////////////

    /* Explicitly convert a py::Bool into a C++ boolean. */
    inline operator bool() const {
        return Base::operator bool();
    }

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    auto operator*() const = delete;

    template <typename... Args>
    auto operator()(Args&&... args) const = delete;

    template <typename T>
    auto operator[](T&& index) const = delete;

    auto begin() const = delete;
    auto end() const = delete;

    template <typename T>
    auto contains(const T& value) const = delete;

};


///////////////////////////////
////    UNARY OPERATORS    ////
///////////////////////////////


// TODO: include __abs__()

template <>
struct Bool::__pos__<> : impl::Returns<Int> {};
template <>
struct Bool::__neg__<> : impl::Returns<Int> {};
template <>
struct Bool::__invert__<> : impl::Returns<Int> {};


///////////////////////////
////    COMPARISONS    ////
///////////////////////////


template <>
struct Bool::__lt__<Object> : impl::Returns<bool> {};
template <typename T>
struct Bool::__lt__<T, std::enable_if_t<impl::bool_like<T>>> : impl::Returns<bool> {};
template <typename T>
struct Bool::__lt__<T, std::enable_if_t<impl::int_like<T>>> : impl::Returns<bool> {};
template <typename T>
struct Bool::__lt__<T, std::enable_if_t<impl::float_like<T>>> : impl::Returns<bool> {};

template <>
struct Bool::__le__<Object> : impl::Returns<bool> {};
template <typename T>
struct Bool::__le__<T, std::enable_if_t<impl::bool_like<T>>> : impl::Returns<bool> {};
template <typename T>
struct Bool::__le__<T, std::enable_if_t<impl::int_like<T>>> : impl::Returns<bool> {};
template <typename T>
struct Bool::__le__<T, std::enable_if_t<impl::float_like<T>>> : impl::Returns<bool> {};

template <>
struct Bool::__ge__<Object> : impl::Returns<bool> {};
template <typename T>
struct Bool::__ge__<T, std::enable_if_t<impl::bool_like<T>>> : impl::Returns<bool> {};
template <typename T>
struct Bool::__ge__<T, std::enable_if_t<impl::int_like<T>>> : impl::Returns<bool> {};
template <typename T>
struct Bool::__ge__<T, std::enable_if_t<impl::float_like<T>>> : impl::Returns<bool> {};

template <>
struct Bool::__gt__<Object> : impl::Returns<bool> {};
template <typename T>
struct Bool::__gt__<T, std::enable_if_t<impl::bool_like<T>>> : impl::Returns<bool> {};
template <typename T>
struct Bool::__gt__<T, std::enable_if_t<impl::int_like<T>>> : impl::Returns<bool> {};
template <typename T>
struct Bool::__gt__<T, std::enable_if_t<impl::float_like<T>>> : impl::Returns<bool> {};


////////////////////////////////
////    BINARY OPERATORS    ////
////////////////////////////////


template <>
struct Bool::__add__<Object> : impl::Returns<Object> {};
template <typename T>
struct Bool::__add__<T, std::enable_if_t<impl::bool_like<T>>> : impl::Returns<Int> {};
template <typename T>
struct Bool::__add__<T, std::enable_if_t<impl::int_like<T>>> : impl::Returns<Int> {};
template <typename T>
struct Bool::__add__<T, std::enable_if_t<impl::float_like<T>>> : impl::Returns<Float> {};
template <typename T>
struct Bool::__add__<T, std::enable_if_t<impl::complex_like<T>>> : impl::Returns<Complex> {};

template <>
struct Bool::__sub__<Object> : impl::Returns<Object> {};
template <typename T>
struct Bool::__sub__<T, std::enable_if_t<impl::bool_like<T>>> : impl::Returns<Int> {};
template <typename T>
struct Bool::__sub__<T, std::enable_if_t<impl::int_like<T>>> : impl::Returns<Int> {};
template <typename T>
struct Bool::__sub__<T, std::enable_if_t<impl::float_like<T>>> : impl::Returns<Float> {};
template <typename T>
struct Bool::__sub__<T, std::enable_if_t<impl::complex_like<T>>> : impl::Returns<Complex> {};

template <>
struct Bool::__mul__<Object> : impl::Returns<Object> {};
template <typename T>
struct Bool::__mul__<T, std::enable_if_t<impl::bool_like<T>>> : impl::Returns<Int> {};
template <typename T>
struct Bool::__mul__<T, std::enable_if_t<impl::int_like<T>>> : impl::Returns<Int> {};
template <typename T>
struct Bool::__mul__<T, std::enable_if_t<impl::float_like<T>>> : impl::Returns<Float> {};
template <typename T>
struct Bool::__mul__<T, std::enable_if_t<impl::complex_like<T>>> : impl::Returns<Complex> {};

template <>
struct Bool::__truediv__<Object> : impl::Returns<Object> {};
template <typename T>
struct Bool::__truediv__<T, std::enable_if_t<impl::bool_like<T>>> : impl::Returns<Float> {};
template <typename T>
struct Bool::__truediv__<T, std::enable_if_t<impl::int_like<T>>> : impl::Returns<Float> {};
template <typename T>
struct Bool::__truediv__<T, std::enable_if_t<impl::float_like<T>>> : impl::Returns<Float> {};
template <typename T>
struct Bool::__truediv__<T, std::enable_if_t<impl::complex_like<T>>> : impl::Returns<Complex> {};

template <>
struct Bool::__mod__<Object> : impl::Returns<Object> {};
template <typename T>
struct Bool::__mod__<T, std::enable_if_t<impl::bool_like<T>>> : impl::Returns<Int> {};
template <typename T>
struct Bool::__mod__<T, std::enable_if_t<impl::int_like<T>>> : impl::Returns<Int> {};
template <typename T>
struct Bool::__mod__<T, std::enable_if_t<impl::float_like<T>>> : impl::Returns<Float> {};
// template <typename T>    <-- Disabled in Python
// struct Bool::__mod__<T, std::enable_if_t<impl::complex_like<T>>> : impl::Returns<Complex> {};

template <>
struct Bool::__lshift__<Object> : impl::Returns<Object> {};
template <typename T>
struct Bool::__lshift__<T, std::enable_if_t<impl::bool_like<T>>> : impl::Returns<Int> {};
template <typename T>
struct Bool::__lshift__<T, std::enable_if_t<impl::int_like<T>>> : impl::Returns<Int> {};

template <>
struct Bool::__rshift__<Object> : impl::Returns<Object> {};
template <typename T>
struct Bool::__rshift__<T, std::enable_if_t<impl::bool_like<T>>> : impl::Returns<Int> {};
template <typename T>
struct Bool::__rshift__<T, std::enable_if_t<impl::int_like<T>>> : impl::Returns<Int> {};

template <>
struct Bool::__and__<Object> : impl::Returns<Object> {};
template <typename T>
struct Bool::__and__<T, std::enable_if_t<impl::bool_like<T>>> : impl::Returns<Bool> {};
template <typename T>
struct Bool::__and__<T, std::enable_if_t<impl::int_like<T>>> : impl::Returns<Int> {};

template <>
struct Bool::__or__<Object> : impl::Returns<Object> {};
template <typename T>
struct Bool::__or__<T, std::enable_if_t<impl::bool_like<T>>> : impl::Returns<Bool> {};
template <typename T>
struct Bool::__or__<T, std::enable_if_t<impl::int_like<T>>> : impl::Returns<Int> {};

template <>
struct Bool::__xor__<Object> : impl::Returns<Object> {};
template <typename T>
struct Bool::__xor__<T, std::enable_if_t<impl::bool_like<T>>> : impl::Returns<Bool> {};
template <typename T>
struct Bool::__xor__<T, std::enable_if_t<impl::int_like<T>>> : impl::Returns<Int> {};


/////////////////////////////////
////    INPLACE OPERATORS    ////
/////////////////////////////////


template <typename T>
struct Bool::__iand__<T, std::enable_if_t<impl::bool_like<T>>> : impl::Returns<Bool> {};

template <typename T>
struct Bool::__ior__<T, std::enable_if_t<impl::bool_like<T>>> : impl::Returns<Bool> {};

template <typename T>
struct Bool::__ixor__<T, std::enable_if_t<impl::bool_like<T>>> : impl::Returns<Bool> {};


}  // namespace python
}  // namespace bertrand


BERTRAND_STD_HASH(bertrand::py::Bool)


#endif  // BERTRAND_PYTHON_BOOL_H
