#if !defined(BERTRAND_PYTHON_INCLUDED) && !defined(LINTER)
#error "This file should not be included directly.  Please include <bertrand/python.h> instead."
#endif

#ifndef BERTRAND_PYTHON_BOOL_H
#define BERTRAND_PYTHON_BOOL_H

#include "common.h"


namespace bertrand {
namespace py {


namespace impl {

    template <std::derived_from<Bool> T>
    struct __pos__<T>                                           : Returns<Int> {};
    template <std::derived_from<Bool> T>
    struct __neg__<T>                                           : Returns<Int> {};
    template <std::derived_from<Bool> T>
    struct __abs__<T>                                           : Returns<Int> {};
    template <std::derived_from<Bool> T>
    struct __invert__<T>                                        : Returns<Int> {};
    template <std::derived_from<Bool> T>
    struct __hash__<T>                                          : Returns<size_t> {};
    // TODO: since py::Bool is implicitly convertible to bool, we technically don't need to
    // define any of the comparison operators on the Python side.  This would improve
    // performance and allow the compiler to catch more detailed syntax errors.
    template <std::derived_from<Bool> L>
    struct __lt__<L, Object>                                    : Returns<bool> {};
    template <std::derived_from<Bool> R>
    struct __lt__<Object, R>                                    : Returns<bool> {};
    template <std::derived_from<Bool> L, bool_like R>
    struct __lt__<L, R>                                         : Returns<bool> {};
    template <bool_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
    struct __lt__<L, R>                                         : Returns<bool> {};
    template <std::derived_from<Bool> L, int_like R>
    struct __lt__<L, R>                                         : Returns<bool> {};
    template <int_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
    struct __lt__<L, R>                                         : Returns<bool> {};
    template <std::derived_from<Bool> L, float_like R>
    struct __lt__<L, R>                                         : Returns<bool> {};
    template <float_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
    struct __lt__<L, R>                                         : Returns<bool> {};
    template <std::derived_from<Bool> L>
    struct __le__<L, Object>                                    : Returns<bool> {};
    template <std::derived_from<Bool> R>
    struct __le__<Object, R>                                    : Returns<bool> {};
    template <std::derived_from<Bool> L, bool_like R>
    struct __le__<L, R>                                         : Returns<bool> {};
    template <bool_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
    struct __le__<L, R>                                         : Returns<bool> {};
    template <std::derived_from<Bool> L, int_like R>
    struct __le__<L, R>                                         : Returns<bool> {};
    template <int_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
    struct __le__<L, R>                                         : Returns<bool> {};
    template <std::derived_from<Bool> L, float_like R>
    struct __le__<L, R>                                         : Returns<bool> {};
    template <float_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
    struct __le__<L, R>                                         : Returns<bool> {};
    template <std::derived_from<Bool> L>
    struct __eq__<L, Object>                                    : Returns<bool> {};
    template <std::derived_from<Bool> R>
    struct __eq__<Object, R>                                    : Returns<bool> {};
    template <std::derived_from<Bool> L, bool_like R>
    struct __eq__<L, R>                                         : Returns<bool> {};
    template <bool_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
    struct __eq__<L, R>                                         : Returns<bool> {};
    template <std::derived_from<Bool> L, int_like R>
    struct __eq__<L, R>                                         : Returns<bool> {};
    template <int_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
    struct __eq__<L, R>                                         : Returns<bool> {};
    template <std::derived_from<Bool> L, float_like R>
    struct __eq__<L, R>                                         : Returns<bool> {};
    template <float_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
    struct __eq__<L, R>                                         : Returns<bool> {};
    template <std::derived_from<Bool> L>
    struct __ne__<L, Object>                                    : Returns<bool> {};
    template <std::derived_from<Bool> R>
    struct __ne__<Object, R>                                    : Returns<bool> {};
    template <std::derived_from<Bool> L, bool_like R>
    struct __ne__<L, R>                                         : Returns<bool> {};
    template <bool_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
    struct __ne__<L, R>                                         : Returns<bool> {};
    template <std::derived_from<Bool> L, int_like R>
    struct __ne__<L, R>                                         : Returns<bool> {};
    template <int_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
    struct __ne__<L, R>                                         : Returns<bool> {};
    template <std::derived_from<Bool> L, float_like R>
    struct __ne__<L, R>                                         : Returns<bool> {};
    template <float_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
    struct __ne__<L, R>                                         : Returns<bool> {};
    template <std::derived_from<Bool> L>
    struct __ge__<L, Object>                                    : Returns<bool> {};
    template <std::derived_from<Bool> R>
    struct __ge__<Object, R>                                    : Returns<bool> {};
    template <std::derived_from<Bool> L, bool_like R>
    struct __ge__<L, R>                                         : Returns<bool> {};
    template <bool_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
    struct __ge__<L, R>                                         : Returns<bool> {};
    template <std::derived_from<Bool> L, int_like R>
    struct __ge__<L, R>                                         : Returns<bool> {};
    template <int_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
    struct __ge__<L, R>                                         : Returns<bool> {};
    template <std::derived_from<Bool> L, float_like R>
    struct __ge__<L, R>                                         : Returns<bool> {};
    template <float_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
    struct __ge__<L, R>                                         : Returns<bool> {};
    template <std::derived_from<Bool> L>
    struct __gt__<L, Object>                                    : Returns<bool> {};
    template <std::derived_from<Bool> R>
    struct __gt__<Object, R>                                    : Returns<bool> {};
    template <std::derived_from<Bool> L, bool_like R>
    struct __gt__<L, R>                                         : Returns<bool> {};
    template <bool_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
    struct __gt__<L, R>                                         : Returns<bool> {};
    template <std::derived_from<Bool> L, int_like R>
    struct __gt__<L, R>                                         : Returns<bool> {};
    template <int_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
    struct __gt__<L, R>                                         : Returns<bool> {};
    template <std::derived_from<Bool> L, float_like R>
    struct __gt__<L, R>                                         : Returns<bool> {};
    template <float_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
    struct __gt__<L, R>                                         : Returns<bool> {};
    template <std::derived_from<Bool> L>
    struct __add__<L, Object>                                   : Returns<Object> {};
    template <std::derived_from<Bool> R>
    struct __add__<Object, R>                                   : Returns<Object> {};
    template <std::derived_from<Bool> L, bool_like R>
    struct __add__<L, R>                                        : Returns<Int> {};
    template <bool_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
    struct __add__<L, R>                                        : Returns<Int> {};
    template <std::derived_from<Bool> L, int_like R>
    struct __add__<L, R>                                        : Returns<Int> {};
    template <int_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
    struct __add__<L, R>                                        : Returns<Int> {};
    template <std::derived_from<Bool> L, float_like R>
    struct __add__<L, R>                                        : Returns<Float> {};
    template <float_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
    struct __add__<L, R>                                        : Returns<Float> {};
    template <std::derived_from<Bool> L, complex_like R>
    struct __add__<L, R>                                        : Returns<Complex> {};
    template <complex_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
    struct __add__<L, R>                                        : Returns<Complex> {};
    template <std::derived_from<Bool> L>
    struct __sub__<L, Object>                                   : Returns<Object> {};
    template <std::derived_from<Bool> R>
    struct __sub__<Object, R>                                   : Returns<Object> {};
    template <std::derived_from<Bool> L, bool_like R>
    struct __sub__<L, R>                                        : Returns<Int> {};
    template <bool_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
    struct __sub__<L, R>                                        : Returns<Int> {};
    template <std::derived_from<Bool> L, int_like R>
    struct __sub__<L, R>                                        : Returns<Int> {};
    template <int_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
    struct __sub__<L, R>                                        : Returns<Int> {};
    template <std::derived_from<Bool> L, float_like R>
    struct __sub__<L, R>                                        : Returns<Float> {};
    template <float_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
    struct __sub__<L, R>                                        : Returns<Float> {};
    template <std::derived_from<Bool> L, complex_like R>
    struct __sub__<L, R>                                        : Returns<Complex> {};
    template <complex_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
    struct __sub__<L, R>                                        : Returns<Complex> {};
    template <std::derived_from<Bool> L>
    struct __mul__<L, Object>                                   : Returns<Object> {};
    template <std::derived_from<Bool> R>
    struct __mul__<Object, R>                                   : Returns<Object> {};
    template <std::derived_from<Bool> L, bool_like R>
    struct __mul__<L, R>                                        : Returns<Int> {};
    template <bool_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
    struct __mul__<L, R>                                        : Returns<Int> {};
    template <std::derived_from<Bool> L, int_like R>
    struct __mul__<L, R>                                        : Returns<Int> {};
    template <int_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
    struct __mul__<L, R>                                        : Returns<Int> {};
    template <std::derived_from<Bool> L, float_like R>
    struct __mul__<L, R>                                        : Returns<Float> {};
    template <float_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
    struct __mul__<L, R>                                        : Returns<Float> {};
    template <std::derived_from<Bool> L, complex_like R>
    struct __mul__<L, R>                                        : Returns<Complex> {};
    template <complex_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
    struct __mul__<L, R>                                        : Returns<Complex> {};
    template <std::derived_from<Bool> L>
    struct __truediv__<L, Object>                               : Returns<Object> {};
    template <std::derived_from<Bool> R>
    struct __truediv__<Object, R>                               : Returns<Object> {};
    template <std::derived_from<Bool> L, bool_like R>
    struct __truediv__<L, R>                                    : Returns<Float> {};
    template <bool_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
    struct __truediv__<L, R>                                    : Returns<Float> {};
    template <std::derived_from<Bool> L, int_like R>
    struct __truediv__<L, R>                                    : Returns<Float> {};
    template <int_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
    struct __truediv__<L, R>                                    : Returns<Float> {};
    template <std::derived_from<Bool> L, float_like R>
    struct __truediv__<L, R>                                    : Returns<Float> {};
    template <float_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
    struct __truediv__<L, R>                                    : Returns<Float> {};
    template <std::derived_from<Bool> L, complex_like R>
    struct __truediv__<L, R>                                    : Returns<Complex> {};
    template <complex_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
    struct __truediv__<L, R>                                    : Returns<Complex> {};
    template <std::derived_from<Bool> L>
    struct __mod__<L, Object>                                   : Returns<Object> {};
    template <std::derived_from<Bool> R>
    struct __mod__<Object, R>                                   : Returns<Object> {};
    template <std::derived_from<Bool> L, bool_like R>
    struct __mod__<L, R>                                        : Returns<Int> {};
    template <bool_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
    struct __mod__<L, R>                                        : Returns<Int> {};
    template <std::derived_from<Bool> L, int_like R>
    struct __mod__<L, R>                                        : Returns<Int> {};
    template <int_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
    struct __mod__<L, R>                                        : Returns<Int> {};
    template <std::derived_from<Bool> L, float_like R>
    struct __mod__<L, R>                                        : Returns<Float> {};
    template <float_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
    struct __mod__<L, R>                                        : Returns<Float> {};
    // template <std::derived_from<Bool> L, complex_like R>    <-- Disabled in Python
    // struct __mod__<L, R>                                     : Returns<Complex> {};
    template <std::derived_from<Bool> L>
    struct __lshift__<L, Object>                                : Returns<Object> {};
    template <std::derived_from<Bool> R>
    struct __lshift__<Object, R>                                : Returns<Object> {};
    template <std::derived_from<Bool> L, bool_like R>
    struct __lshift__<L, R>                                     : Returns<Int> {};
    template <bool_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
    struct __lshift__<L, R>                                     : Returns<Int> {};
    template <std::derived_from<Bool> L, int_like R>
    struct __lshift__<L, R>                                     : Returns<Int> {};
    template <int_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
    struct __lshift__<L, R>                                     : Returns<Int> {};
    template <std::derived_from<Bool> L>
    struct __rshift__<L, Object>                                : Returns<Object> {};
    template <std::derived_from<Bool> R>
    struct __rshift__<Object, R>                                : Returns<Object> {};
    template <std::derived_from<Bool> L, bool_like R>
    struct __rshift__<L, R>                                     : Returns<Int> {};
    template <bool_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
    struct __rshift__<L, R>                                     : Returns<Int> {};
    template <std::derived_from<Bool> L, int_like R>
    struct __rshift__<L, R>                                     : Returns<Int> {};
    template <int_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
    struct __rshift__<L, R>                                     : Returns<Int> {};
    template <std::derived_from<Bool> L>
    struct __and__<L, Object>                                   : Returns<Object> {};
    template <std::derived_from<Bool> R>
    struct __and__<Object, R>                                   : Returns<Object> {};
    template <std::derived_from<Bool> L, bool_like R>
    struct __and__<L, R>                                        : Returns<Bool> {};
    template <bool_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
    struct __and__<L, R>                                        : Returns<Bool> {};
    template <std::derived_from<Bool> L, int_like R>
    struct __and__<L, R>                                        : Returns<Int> {};
    template <int_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
    struct __and__<L, R>                                        : Returns<Int> {};
    template <std::derived_from<Bool> L, bool_like R>
    struct __iand__<L, R>                                       : Returns<Bool&> {};
    template <std::derived_from<Bool> L>
    struct __or__<L, Object>                                    : Returns<Object> {};
    template <std::derived_from<Bool> R>
    struct __or__<Object, R>                                    : Returns<Object> {};
    template <std::derived_from<Bool> L, bool_like R>
    struct __or__<L, R>                                         : Returns<Bool> {};
    template <bool_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
    struct __or__<L, R>                                         : Returns<Bool> {};
    template <std::derived_from<Bool> L, int_like R>
    struct __or__<L, R>                                         : Returns<Int> {};
    template <int_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
    struct __or__<L, R>                                         : Returns<Int> {};
    template <std::derived_from<Bool> L, bool_like R>
    struct __ior__<L, R>                                        : Returns<Bool&> {};
    template <std::derived_from<Bool> L>
    struct __xor__<L, Object>                                   : Returns<Object> {};
    template <std::derived_from<Bool> R>
    struct __xor__<Object, R>                                   : Returns<Object> {};
    template <std::derived_from<Bool> L, bool_like R>
    struct __xor__<L, R>                                        : Returns<Bool> {};
    template <bool_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
    struct __xor__<L, R>                                        : Returns<Bool> {};
    template <std::derived_from<Bool> L, int_like R>
    struct __xor__<L, R>                                        : Returns<Int> {};
    template <int_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
    struct __xor__<L, R>                                        : Returns<Int> {};
    template <std::derived_from<Bool> L, bool_like R>
    struct __ixor__<L, R>                                       : Returns<Bool&> {};

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
            Exception::from_python();
        }
        m_ptr = Py_NewRef(result ? Py_True : Py_False);
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


static const Bool True = reinterpret_borrow<Bool>(Py_True);
static const Bool False = reinterpret_borrow<Bool>(Py_False);


}  // namespace py
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_BOOL_H
