#if !defined(BERTRAND_PYTHON_INCLUDED) && !defined(LINTER)
#error "This file should not be included directly.  Please include <bertrand/python.h> instead."
#endif

#ifndef BERTRAND_PYTHON_BOOL_H
#define BERTRAND_PYTHON_BOOL_H

#include "common.h"


namespace bertrand {
namespace py {


namespace impl {

template <>
struct __pos__<Bool>                                            : Returns<Int> {};
template <>
struct __neg__<Bool>                                            : Returns<Int> {};
template <>
struct __abs__<Bool>                                            : Returns<Int> {};
template <>
struct __invert__<Bool>                                         : Returns<Int> {};
template <>
struct __increment__<Bool>                                      : Returns<Int> {};
template <>
struct __decrement__<Bool>                                      : Returns<Int> {};
template <>
struct __hash__<Bool>                                           : Returns<size_t> {};
template <>
struct __lt__<Bool, Object>                                     : Returns<bool> {};
template <>
struct __lt__<Object, Bool>                                     : Returns<bool> {};
template <bool_like T>
struct __lt__<Bool, T>                                          : Returns<bool> {};
template <bool_like T> requires (!std::is_same_v<T, Bool>)
struct __lt__<T, Bool>                                          : Returns<bool> {};
template <int_like T>
struct __lt__<Bool, T>                                          : Returns<bool> {};
template <int_like T>
struct __lt__<T, Bool>                                          : Returns<bool> {};
template <float_like T>
struct __lt__<Bool, T>                                          : Returns<bool> {};
template <float_like T>
struct __lt__<T, Bool>                                          : Returns<bool> {};
template <>
struct __le__<Bool, Object>                                     : Returns<bool> {};
template <>
struct __le__<Object, Bool>                                     : Returns<bool> {};
template <bool_like T>
struct __le__<Bool, T>                                          : Returns<bool> {};
template <bool_like T> requires (!std::is_same_v<T, Bool>)
struct __le__<T, Bool>                                          : Returns<bool> {};
template <int_like T>
struct __le__<Bool, T>                                          : Returns<bool> {};
template <int_like T>
struct __le__<T, Bool>                                          : Returns<bool> {};
template <float_like T>
struct __le__<Bool, T>                                          : Returns<bool> {};
template <float_like T>
struct __le__<T, Bool>                                          : Returns<bool> {};
template <>
struct __ge__<Bool, Object>                                     : Returns<bool> {};
template <>
struct __ge__<Object, Bool>                                     : Returns<bool> {};
template <bool_like T>
struct __ge__<Bool, T>                                          : Returns<bool> {};
template <bool_like T> requires (!std::is_same_v<T, Bool>)
struct __ge__<T, Bool>                                          : Returns<bool> {};
template <int_like T>
struct __ge__<Bool, T>                                          : Returns<bool> {};
template <int_like T>
struct __ge__<T, Bool>                                          : Returns<bool> {};
template <float_like T>
struct __ge__<Bool, T>                                          : Returns<bool> {};
template <float_like T>
struct __ge__<T, Bool>                                          : Returns<bool> {};
template <>
struct __gt__<Bool, Object>                                     : Returns<bool> {};
template <>
struct __gt__<Object, Bool>                                     : Returns<bool> {};
template <bool_like T>
struct __gt__<Bool, T>                                          : Returns<bool> {};
template <bool_like T> requires (!std::is_same_v<T, Bool>)
struct __gt__<T, Bool>                                          : Returns<bool> {};
template <int_like T>
struct __gt__<Bool, T>                                          : Returns<bool> {};
template <int_like T>
struct __gt__<T, Bool>                                          : Returns<bool> {};
template <float_like T>
struct __gt__<Bool, T>                                          : Returns<bool> {};
template <float_like T>
struct __gt__<T, Bool>                                          : Returns<bool> {};
template <>
struct __add__<Bool, Object>                                    : Returns<Object> {};
template <>
struct __add__<Object, Bool>                                    : Returns<Object> {};
template <bool_like T>
struct __add__<Bool, T>                                         : Returns<Int> {};
template <bool_like T> requires (!std::is_same_v<T, Bool>)
struct __add__<T, Bool>                                         : Returns<Int> {};
template <int_like T>
struct __add__<Bool, T>                                         : Returns<Int> {};
template <int_like T>
struct __add__<T, Bool>                                         : Returns<Int> {};
template <float_like T>
struct __add__<Bool, T>                                         : Returns<Float> {};
template <float_like T>
struct __add__<T, Bool>                                         : Returns<Float> {};
template <complex_like T>
struct __add__<Bool, T>                                         : Returns<Complex> {};
template <complex_like T>
struct __add__<T, Bool>                                         : Returns<Complex> {};
template <>
struct __sub__<Bool, Object>                                    : Returns<Object> {};
template <>
struct __sub__<Object, Bool>                                    : Returns<Object> {};
template <bool_like T>
struct __sub__<Bool, T>                                         : Returns<Int> {};
template <bool_like T> requires (!std::is_same_v<T, Bool>)
struct __sub__<T, Bool>                                         : Returns<Int> {};
template <int_like T>
struct __sub__<Bool, T>                                         : Returns<Int> {};
template <int_like T>
struct __sub__<T, Bool>                                         : Returns<Int> {};
template <float_like T>
struct __sub__<Bool, T>                                         : Returns<Float> {};
template <float_like T>
struct __sub__<T, Bool>                                         : Returns<Float> {};
template <complex_like T>
struct __sub__<Bool, T>                                         : Returns<Complex> {};
template <complex_like T>
struct __sub__<T, Bool>                                         : Returns<Complex> {};
template <>
struct __mul__<Bool, Object>                                    : Returns<Object> {};
template <>
struct __mul__<Object, Bool>                                    : Returns<Object> {};
template <bool_like T>
struct __mul__<Bool, T>                                         : Returns<Int> {};
template <bool_like T> requires (!std::is_same_v<T, Bool>)
struct __mul__<T, Bool>                                         : Returns<Int> {};
template <int_like T>
struct __mul__<Bool, T>                                         : Returns<Int> {};
template <int_like T>
struct __mul__<T, Bool>                                         : Returns<Int> {};
template <float_like T>
struct __mul__<Bool, T>                                         : Returns<Float> {};
template <float_like T>
struct __mul__<T, Bool>                                         : Returns<Float> {};
template <complex_like T>
struct __mul__<Bool, T>                                         : Returns<Complex> {};
template <complex_like T>
struct __mul__<T, Bool>                                         : Returns<Complex> {};
template <>
struct __truediv__<Bool, Object>                                : Returns<Object> {};
template <>
struct __truediv__<Object, Bool>                                : Returns<Object> {};
template <bool_like T>
struct __truediv__<Bool, T>                                     : Returns<Float> {};
template <bool_like T> requires (!std::is_same_v<T, Bool>)
struct __truediv__<T, Bool>                                     : Returns<Float> {};
template <int_like T>
struct __truediv__<Bool, T>                                     : Returns<Float> {};
template <int_like T>
struct __truediv__<T, Bool>                                     : Returns<Float> {};
template <float_like T>
struct __truediv__<Bool, T>                                     : Returns<Float> {};
template <float_like T>
struct __truediv__<T, Bool>                                     : Returns<Float> {};
template <complex_like T>
struct __truediv__<Bool, T>                                     : Returns<Complex> {};
template <complex_like T>
struct __truediv__<T, Bool>                                     : Returns<Complex> {};
template <>
struct __mod__<Bool, Object>                                    : Returns<Object> {};
template <>
struct __mod__<Object, Bool>                                    : Returns<Object> {};
template <bool_like T>
struct __mod__<Bool, T>                                         : Returns<Int> {};
template <bool_like T> requires (!std::is_same_v<T, Bool>)
struct __mod__<T, Bool>                                         : Returns<Int> {};
template <int_like T>
struct __mod__<Bool, T>                                         : Returns<Int> {};
template <int_like T>
struct __mod__<T, Bool>                                         : Returns<Int> {};
template <float_like T>
struct __mod__<Bool, T>                                         : Returns<Float> {};
template <float_like T>
struct __mod__<T, Bool>                                         : Returns<Float> {};
// template <complex_like T>    <-- Disabled in Python
// struct __mod__<Bool, T>                                      : Returns<Complex> {};
template <>
struct __lshift__<Bool, Object>                                 : Returns<Object> {};
template <>
struct __lshift__<Object, Bool>                                 : Returns<Object> {};
template <bool_like T>
struct __lshift__<Bool, T>                                      : Returns<Int> {};
template <bool_like T> requires (!std::is_same_v<T, Bool>)
struct __lshift__<T, Bool>                                      : Returns<Int> {};
template <int_like T>
struct __lshift__<Bool, T>                                      : Returns<Int> {};
template <int_like T>
struct __lshift__<T, Bool>                                      : Returns<Int> {};
template <>
struct __rshift__<Bool, Object>                                 : Returns<Object> {};
template <>
struct __rshift__<Object, Bool>                                 : Returns<Object> {};
template <bool_like T>
struct __rshift__<Bool, T>                                      : Returns<Int> {};
template <bool_like T> requires (!std::is_same_v<T, Bool>)
struct __rshift__<T, Bool>                                      : Returns<Int> {};
template <int_like T>
struct __rshift__<Bool, T>                                      : Returns<Int> {};
template <int_like T>
struct __rshift__<T, Bool>                                      : Returns<Int> {};
template <>
struct __and__<Bool, Object>                                    : Returns<Object> {};
template <>
struct __and__<Object, Bool>                                    : Returns<Object> {};
template <bool_like T>
struct __and__<Bool, T>                                         : Returns<Bool> {};
template <bool_like T> requires (!std::is_same_v<T, Bool>)
struct __and__<T, Bool>                                         : Returns<Bool> {};
template <int_like T>
struct __and__<Bool, T>                                         : Returns<Int> {};
template <int_like T>
struct __and__<T, Bool>                                         : Returns<Int> {};
template <bool_like T>
struct __iand__<Bool, T>                                        : Returns<Bool&> {};
template <>
struct __or__<Bool, Object>                                     : Returns<Object> {};
template <>
struct __or__<Object, Bool>                                     : Returns<Object> {};
template <bool_like T>
struct __or__<Bool, T>                                          : Returns<Bool> {};
template <bool_like T> requires (!std::is_same_v<T, Bool>)
struct __or__<T, Bool>                                          : Returns<Bool> {};
template <int_like T>
struct __or__<Bool, T>                                          : Returns<Int> {};
template <int_like T>
struct __or__<T, Bool>                                          : Returns<Int> {};
template <bool_like T>
struct __ior__<Bool, T>                                         : Returns<Bool&> {};
template <>
struct __xor__<Bool, Object>                                    : Returns<Object> {};
template <>
struct __xor__<Object, Bool>                                    : Returns<Object> {};
template <bool_like T>
struct __xor__<Bool, T>                                         : Returns<Bool> {};
template <bool_like T> requires (!std::is_same_v<T, Bool>)
struct __xor__<T, Bool>                                         : Returns<Bool> {};
template <int_like T>
struct __xor__<Bool, T>                                         : Returns<Int> {};
template <int_like T>
struct __xor__<T, Bool>                                         : Returns<Int> {};
template <bool_like T>
struct __ixor__<Bool, T>                                        : Returns<Bool&> {};

}


/* pybind11::bool_ equivalent with stronger type safety and cross-language support. */
class Bool : public Object {
    using Base = Object;

    template <typename T>
    static constexpr bool py_constructor = impl::bool_like<T> && impl::python_like<T>;
    template <typename T>
    static constexpr bool cpp_constructor = impl::bool_like<T> && !impl::python_like<T>;
    template <typename T>
    static constexpr bool py_converting_constructor =
        !impl::bool_like<T> && impl::python_like<T>;
    template <typename T>
    static constexpr bool cpp_converting_constructor =
        !impl::bool_like<T> && !impl::python_like<T> &&
        impl::explicitly_convertible_to<T, bool>;
    template <typename T>
    static constexpr bool container_empty_constructor =
        !impl::bool_like<T> && !impl::explicitly_convertible_to<T, bool> &&
        impl::has_empty<T>;
    template <typename T>
    static constexpr bool container_size_constructor =
        !impl::bool_like<T> && !impl::explicitly_convertible_to<T, bool> &&
        !impl::has_empty<T> && impl::has_size<T>;

public:
    static Type type;

    BERTRAND_OBJECT_COMMON(Base, Bool, impl::bool_like, PyBool_Check)
    BERTRAND_OBJECT_OPERATORS(Bool)

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Default constructor.  Initializes to False. */
    Bool() : Base(Py_False, borrowed_t{}) {}

    /* Copy/move constructors. */
    template <typename T> requires (py_constructor<T>)
    Bool(T&& other) : Base(std::forward<T>(other)) {}

    /* Implicitly convert C++ booleans into py::Bool. */
    template <typename T> requires (cpp_constructor<T>)
    Bool(const T& value) : Base(value ? Py_True : Py_False, borrowed_t{}) {}

    /* Explicitly convert an arbitrary Python object into a boolean. */
    template <typename T> requires (py_converting_constructor<T>)
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

    /* Trigger explicit conversion operators to bool. */
    template <typename T> requires (cpp_converting_constructor<T>)
    explicit Bool(const T& value) : Bool(static_cast<bool>(value)) {}

    /* Explicitly convert any C++ object that implements a `.empty()` method into a
    py::Bool. */
    template <typename T> requires (container_empty_constructor<T>)
    explicit Bool(const T& obj) : Bool(!obj.empty()) {}

    /* Explicitly convert any C++ object that implements a `.size()` method into a
    py::Bool. */
    template <typename T> requires (container_size_constructor<T>)
    explicit Bool(const T& obj) : Bool(obj.size() > 0) {}

    /* Explicitly convert a std::tuple into a py::Bool. */
    template <typename... Args>
    explicit Bool(const std::tuple<Args...>& obj) : Bool(sizeof...(Args) > 0) {}

    /* Explicitly convert a string literal into a py::Bool. */
    explicit Bool(const char* str) : Bool(std::string(str)) {}

    /////////////////////////////
    ////    C++ INTERFACE    ////
    /////////////////////////////

    /* Implicitly convert to a pybind11::bool. */
    inline operator pybind11::bool_() const {
        return reinterpret_borrow<pybind11::bool_>(m_ptr);
    }

    /* Implicitly convert to a C++ boolean. */
    inline operator bool() const {
        return Base::operator bool();
    }

};


}  // namespace py
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_BOOL_H
