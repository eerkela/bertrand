#if !defined(BERTRAND_PYTHON_INCLUDED) && !defined(LINTER)
#error "This file should not be included directly.  Please include <bertrand/python.h> instead."
#endif

#ifndef BERTRAND_PYTHON_BOOL_H
#define BERTRAND_PYTHON_BOOL_H

#include "common.h"


namespace bertrand {
namespace py {


template <std::derived_from<Bool> T>
struct __pos__<T>                                               : Returns<Int> {};
template <std::derived_from<Bool> T>
struct __neg__<T>                                               : Returns<Int> {};
template <std::derived_from<Bool> T>
struct __abs__<T>                                               : Returns<Int> {};
template <std::derived_from<Bool> T>
struct __invert__<T>                                            : Returns<Int> {};
template <std::derived_from<Bool> T>
struct __hash__<T>                                              : Returns<size_t> {};
// TODO: since py::Bool is implicitly convertible to bool, we technically don't need to
// define any of the comparison operators on the Python side.  This would improve
// performance and allow the compiler to catch more detailed syntax errors.
template <std::derived_from<Bool> L>
struct __lt__<L, Object>                                        : Returns<bool> {};
template <std::derived_from<Bool> R>
struct __lt__<Object, R>                                        : Returns<bool> {};
template <std::derived_from<Bool> L, impl::bool_like R>
struct __lt__<L, R>                                             : Returns<bool> {};
template <impl::bool_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
struct __lt__<L, R>                                             : Returns<bool> {};
template <std::derived_from<Bool> L, impl::int_like R>
struct __lt__<L, R>                                             : Returns<bool> {};
template <impl::int_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
struct __lt__<L, R>                                             : Returns<bool> {};
template <std::derived_from<Bool> L, impl::float_like R>
struct __lt__<L, R>                                             : Returns<bool> {};
template <impl::float_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
struct __lt__<L, R>                                             : Returns<bool> {};
template <std::derived_from<Bool> L>
struct __le__<L, Object>                                        : Returns<bool> {};
template <std::derived_from<Bool> R>
struct __le__<Object, R>                                        : Returns<bool> {};
template <std::derived_from<Bool> L, impl::bool_like R>
struct __le__<L, R>                                             : Returns<bool> {};
template <impl::bool_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
struct __le__<L, R>                                             : Returns<bool> {};
template <std::derived_from<Bool> L, impl::int_like R>
struct __le__<L, R>                                             : Returns<bool> {};
template <impl::int_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
struct __le__<L, R>                                             : Returns<bool> {};
template <std::derived_from<Bool> L, impl::float_like R>
struct __le__<L, R>                                             : Returns<bool> {};
template <impl::float_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
struct __le__<L, R>                                             : Returns<bool> {};
template <std::derived_from<Bool> L>
struct __eq__<L, Object>                                        : Returns<bool> {};
template <std::derived_from<Bool> R>
struct __eq__<Object, R>                                        : Returns<bool> {};
template <std::derived_from<Bool> L, impl::bool_like R>
struct __eq__<L, R>                                             : Returns<bool> {};
template <impl::bool_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
struct __eq__<L, R>                                             : Returns<bool> {};
template <std::derived_from<Bool> L, impl::int_like R>
struct __eq__<L, R>                                             : Returns<bool> {};
template <impl::int_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
struct __eq__<L, R>                                             : Returns<bool> {};
template <std::derived_from<Bool> L, impl::float_like R>
struct __eq__<L, R>                                             : Returns<bool> {};
template <impl::float_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
struct __eq__<L, R>                                             : Returns<bool> {};
template <std::derived_from<Bool> L>
struct __ne__<L, Object>                                        : Returns<bool> {};
template <std::derived_from<Bool> R>
struct __ne__<Object, R>                                        : Returns<bool> {};
template <std::derived_from<Bool> L, impl::bool_like R>
struct __ne__<L, R>                                             : Returns<bool> {};
template <impl::bool_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
struct __ne__<L, R>                                             : Returns<bool> {};
template <std::derived_from<Bool> L, impl::int_like R>
struct __ne__<L, R>                                             : Returns<bool> {};
template <impl::int_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
struct __ne__<L, R>                                             : Returns<bool> {};
template <std::derived_from<Bool> L, impl::float_like R>
struct __ne__<L, R>                                             : Returns<bool> {};
template <impl::float_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
struct __ne__<L, R>                                             : Returns<bool> {};
template <std::derived_from<Bool> L>
struct __ge__<L, Object>                                        : Returns<bool> {};
template <std::derived_from<Bool> R>
struct __ge__<Object, R>                                        : Returns<bool> {};
template <std::derived_from<Bool> L, impl::bool_like R>
struct __ge__<L, R>                                             : Returns<bool> {};
template <impl::bool_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
struct __ge__<L, R>                                             : Returns<bool> {};
template <std::derived_from<Bool> L, impl::int_like R>
struct __ge__<L, R>                                             : Returns<bool> {};
template <impl::int_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
struct __ge__<L, R>                                             : Returns<bool> {};
template <std::derived_from<Bool> L, impl::float_like R>
struct __ge__<L, R>                                             : Returns<bool> {};
template <impl::float_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
struct __ge__<L, R>                                             : Returns<bool> {};
template <std::derived_from<Bool> L>
struct __gt__<L, Object>                                        : Returns<bool> {};
template <std::derived_from<Bool> R>
struct __gt__<Object, R>                                        : Returns<bool> {};
template <std::derived_from<Bool> L, impl::bool_like R>
struct __gt__<L, R>                                             : Returns<bool> {};
template <impl::bool_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
struct __gt__<L, R>                                             : Returns<bool> {};
template <std::derived_from<Bool> L, impl::int_like R>
struct __gt__<L, R>                                             : Returns<bool> {};
template <impl::int_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
struct __gt__<L, R>                                             : Returns<bool> {};
template <std::derived_from<Bool> L, impl::float_like R>
struct __gt__<L, R>                                             : Returns<bool> {};
template <impl::float_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
struct __gt__<L, R>                                             : Returns<bool> {};
template <std::derived_from<Bool> L>
struct __add__<L, Object>                                       : Returns<Object> {};
template <std::derived_from<Bool> R>
struct __add__<Object, R>                                       : Returns<Object> {};
template <std::derived_from<Bool> L, impl::bool_like R>
struct __add__<L, R>                                            : Returns<Int> {};
template <impl::bool_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
struct __add__<L, R>                                            : Returns<Int> {};
template <std::derived_from<Bool> L, impl::int_like R>
struct __add__<L, R>                                            : Returns<Int> {};
template <impl::int_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
struct __add__<L, R>                                            : Returns<Int> {};
template <std::derived_from<Bool> L, impl::float_like R>
struct __add__<L, R>                                            : Returns<Float> {};
template <impl::float_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
struct __add__<L, R>                                            : Returns<Float> {};
template <std::derived_from<Bool> L, impl::complex_like R>
struct __add__<L, R>                                            : Returns<Complex> {};
template <impl::complex_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
struct __add__<L, R>                                            : Returns<Complex> {};
template <std::derived_from<Bool> L>
struct __sub__<L, Object>                                       : Returns<Object> {};
template <std::derived_from<Bool> R>
struct __sub__<Object, R>                                       : Returns<Object> {};
template <std::derived_from<Bool> L, impl::bool_like R>
struct __sub__<L, R>                                            : Returns<Int> {};
template <impl::bool_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
struct __sub__<L, R>                                            : Returns<Int> {};
template <std::derived_from<Bool> L, impl::int_like R>
struct __sub__<L, R>                                            : Returns<Int> {};
template <impl::int_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
struct __sub__<L, R>                                            : Returns<Int> {};
template <std::derived_from<Bool> L, impl::float_like R>
struct __sub__<L, R>                                            : Returns<Float> {};
template <impl::float_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
struct __sub__<L, R>                                            : Returns<Float> {};
template <std::derived_from<Bool> L, impl::complex_like R>
struct __sub__<L, R>                                            : Returns<Complex> {};
template <impl::complex_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
struct __sub__<L, R>                                            : Returns<Complex> {};
template <std::derived_from<Bool> L>
struct __mul__<L, Object>                                       : Returns<Object> {};
template <std::derived_from<Bool> R>
struct __mul__<Object, R>                                       : Returns<Object> {};
template <std::derived_from<Bool> L, impl::bool_like R>
struct __mul__<L, R>                                            : Returns<Int> {};
template <impl::bool_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
struct __mul__<L, R>                                            : Returns<Int> {};
template <std::derived_from<Bool> L, impl::int_like R>
struct __mul__<L, R>                                            : Returns<Int> {};
template <impl::int_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
struct __mul__<L, R>                                            : Returns<Int> {};
template <std::derived_from<Bool> L, impl::float_like R>
struct __mul__<L, R>                                            : Returns<Float> {};
template <impl::float_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
struct __mul__<L, R>                                            : Returns<Float> {};
template <std::derived_from<Bool> L, impl::complex_like R>
struct __mul__<L, R>                                            : Returns<Complex> {};
template <impl::complex_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
struct __mul__<L, R>                                            : Returns<Complex> {};
template <std::derived_from<Bool> L>
struct __truediv__<L, Object>                                   : Returns<Object> {};
template <std::derived_from<Bool> R>
struct __truediv__<Object, R>                                   : Returns<Object> {};
template <std::derived_from<Bool> L, impl::bool_like R>
struct __truediv__<L, R>                                        : Returns<Float> {};
template <impl::bool_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
struct __truediv__<L, R>                                        : Returns<Float> {};
template <std::derived_from<Bool> L, impl::int_like R>
struct __truediv__<L, R>                                        : Returns<Float> {};
template <impl::int_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
struct __truediv__<L, R>                                        : Returns<Float> {};
template <std::derived_from<Bool> L, impl::float_like R>
struct __truediv__<L, R>                                        : Returns<Float> {};
template <impl::float_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
struct __truediv__<L, R>                                        : Returns<Float> {};
template <std::derived_from<Bool> L, impl::complex_like R>
struct __truediv__<L, R>                                        : Returns<Complex> {};
template <impl::complex_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
struct __truediv__<L, R>                                        : Returns<Complex> {};
template <std::derived_from<Bool> L>
struct __mod__<L, Object>                                       : Returns<Object> {};
template <std::derived_from<Bool> R>
struct __mod__<Object, R>                                       : Returns<Object> {};
template <std::derived_from<Bool> L, impl::bool_like R>
struct __mod__<L, R>                                            : Returns<Int> {};
template <impl::bool_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
struct __mod__<L, R>                                            : Returns<Int> {};
template <std::derived_from<Bool> L, impl::int_like R>
struct __mod__<L, R>                                            : Returns<Int> {};
template <impl::int_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
struct __mod__<L, R>                                            : Returns<Int> {};
template <std::derived_from<Bool> L, impl::float_like R>
struct __mod__<L, R>                                            : Returns<Float> {};
template <impl::float_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
struct __mod__<L, R>                                            : Returns<Float> {};
// template <std::derived_from<Bool> L, impl::complex_like R>    <-- Disabled in Python
// struct __mod__<L, R>                                         : Returns<Complex> {};
template <std::derived_from<Bool> L>
struct __lshift__<L, Object>                                    : Returns<Object> {};
template <std::derived_from<Bool> R>
struct __lshift__<Object, R>                                    : Returns<Object> {};
template <std::derived_from<Bool> L, impl::bool_like R>
struct __lshift__<L, R>                                         : Returns<Int> {};
template <impl::bool_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
struct __lshift__<L, R>                                         : Returns<Int> {};
template <std::derived_from<Bool> L, impl::int_like R>
struct __lshift__<L, R>                                         : Returns<Int> {};
template <impl::int_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
struct __lshift__<L, R>                                         : Returns<Int> {};
template <std::derived_from<Bool> L>
struct __rshift__<L, Object>                                    : Returns<Object> {};
template <std::derived_from<Bool> R>
struct __rshift__<Object, R>                                    : Returns<Object> {};
template <std::derived_from<Bool> L, impl::bool_like R>
struct __rshift__<L, R>                                         : Returns<Int> {};
template <impl::bool_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
struct __rshift__<L, R>                                         : Returns<Int> {};
template <std::derived_from<Bool> L, impl::int_like R>
struct __rshift__<L, R>                                         : Returns<Int> {};
template <impl::int_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
struct __rshift__<L, R>                                         : Returns<Int> {};
template <std::derived_from<Bool> L>
struct __and__<L, Object>                                       : Returns<Object> {};
template <std::derived_from<Bool> R>
struct __and__<Object, R>                                       : Returns<Object> {};
template <std::derived_from<Bool> L, impl::bool_like R>
struct __and__<L, R>                                            : Returns<Bool> {};
template <impl::bool_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
struct __and__<L, R>                                            : Returns<Bool> {};
template <std::derived_from<Bool> L, impl::int_like R>
struct __and__<L, R>                                            : Returns<Int> {};
template <impl::int_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
struct __and__<L, R>                                            : Returns<Int> {};
template <std::derived_from<Bool> L, impl::bool_like R>
struct __iand__<L, R>                                           : Returns<Bool&> {};
template <std::derived_from<Bool> L>
struct __or__<L, Object>                                        : Returns<Object> {};
template <std::derived_from<Bool> R>
struct __or__<Object, R>                                        : Returns<Object> {};
template <std::derived_from<Bool> L, impl::bool_like R>
struct __or__<L, R>                                             : Returns<Bool> {};
template <impl::bool_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
struct __or__<L, R>                                             : Returns<Bool> {};
template <std::derived_from<Bool> L, impl::int_like R>
struct __or__<L, R>                                             : Returns<Int> {};
template <impl::int_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
struct __or__<L, R>                                             : Returns<Int> {};
template <std::derived_from<Bool> L, impl::bool_like R>
struct __ior__<L, R>                                            : Returns<Bool&> {};
template <std::derived_from<Bool> L>
struct __xor__<L, Object>                                       : Returns<Object> {};
template <std::derived_from<Bool> R>
struct __xor__<Object, R>                                       : Returns<Object> {};
template <std::derived_from<Bool> L, impl::bool_like R>
struct __xor__<L, R>                                            : Returns<Bool> {};
template <impl::bool_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
struct __xor__<L, R>                                            : Returns<Bool> {};
template <std::derived_from<Bool> L, impl::int_like R>
struct __xor__<L, R>                                            : Returns<Int> {};
template <impl::int_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
struct __xor__<L, R>                                            : Returns<Int> {};
template <std::derived_from<Bool> L, impl::bool_like R>
struct __ixor__<L, R>                                           : Returns<Bool&> {};


/* Represents a statically-typed Python boolean in C++. */
class Bool : public Inherits<Bool, Object> {
    using Base = Inherits<Bool, Object>;

public:
    static const Type type;

    template <typename T>
    static consteval bool check() { return impl::bool_like<T>; }

    template <typename T>
    static constexpr bool check(const T& obj) {
        if constexpr (impl::python_like<T>) {
            return obj.ptr() != nullptr && PyBool_Check(obj.ptr());
        } else {
            return check<T>();
        }
    }

    //////////////////////
    ////    COMMON    ////
    //////////////////////

    using Base::Base;

    BERTRAND_OBJECT_OPERATORS(Bool)

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Default constructor.  Initializes to False. */
    Bool() : Base(Py_False, borrowed_t{}) {}

    /* Implicitly convert C++ booleans into py::Bool. */
    template <impl::cpp_like T> requires (impl::bool_like<T>)
    Bool(const T& value) : Base(value ? Py_True : Py_False, borrowed_t{}) {}

    /* Explicitly convert an arbitrary Python object into a boolean. */
    template <impl::python_like T> requires (!impl::bool_like<T>)
    explicit Bool(const T& obj) : Base(nullptr, stolen_t{}) {
        int result = PyObject_IsTrue(obj.ptr());
        if (result == -1) {
            Exception::from_python();
        }
        m_ptr = Py_NewRef(result ? Py_True : Py_False);
    }

    /* Trigger explicit conversion operators to bool. */
    template <impl::cpp_like T>
        requires (!impl::bool_like<T> && impl::explicitly_convertible_to<T, bool>)
    explicit Bool(const T& value) : Bool(static_cast<bool>(value)) {}

    /* Explicitly convert any C++ object that implements a `.size()` method into a
    py::Bool. */
    template <impl::cpp_like T>
        requires (
            !impl::bool_like<T> &&
            !impl::explicitly_convertible_to<T, bool> &&
            impl::has_size<T>
        )
    explicit Bool(const T& obj) : Bool(std::size(obj) > 0) {}

    /* Explicitly convert any C++ object that implements a `.empty()` method into a
    py::Bool. */
    template <impl::cpp_like T>
        requires (
            !impl::bool_like<T> &&
            !impl::explicitly_convertible_to<T, bool> &&
            !impl::has_size<T> &&
            impl::has_empty<T>
        )
    explicit Bool(const T& obj) : Bool(!obj.empty()) {}

    /* Explicitly convert a std::tuple into a py::Bool. */
    template <typename... Args>
    explicit Bool(const std::tuple<Args...>& obj) : Bool(sizeof...(Args) > 0) {}

    // TODO: Bool(const char* str) needs to be templated to avoid implicitly converting
    // py::Str, etc.

    /* Explicitly convert a string literal into a py::Bool. */
    explicit Bool(const char* str) : Bool(std::strcmp(str, "") != 0) {}

    /////////////////////////////
    ////    C++ INTERFACE    ////
    /////////////////////////////

    // /* Implicitly convert to a pybind11::bool. */
    // inline operator pybind11::bool_() const {
    //     return reinterpret_borrow<pybind11::bool_>(m_ptr);
    // }

    /* Implicitly convert to a C++ boolean. */
    inline operator bool() const {
        return Base::operator bool();
    }

};


static const Bool True = reinterpret_borrow<Bool>(Py_True);
static const Bool False = reinterpret_borrow<Bool>(Py_False);


}  // namespace py
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_BOOL_H
