#ifndef BERTRAND_PYTHON_INCLUDED
#error "This file should not be included directly.  Please include <bertrand/python.h> instead."
#endif

#ifndef BERTRAND_PYTHON_BOOL_H
#define BERTRAND_PYTHON_BOOL_H

#include "common.h"


namespace bertrand {
namespace py {


/* Wrapper around pybind11::bool_ that enables math operations with C++ inputs. */
class Bool : public Object, public impl::Ops<Bool> {
    using Ops = impl::Ops<Bool>;

    static PyObject* convert_to_bool(PyObject* obj) {
        int result = PyObject_IsTrue(obj);
        if (result == -1) {
            throw error_already_set();
        }
        return Py_NewRef(result ? Py_True : Py_False);
    }

    template <typename T>
    static constexpr bool constructor1 = impl::is_bool_like<T> && !impl::is_object<T>;
    template <typename T>
    static constexpr bool constructor2 = impl::is_bool_like<T> && impl::is_object<T>;
    template <typename T>
    static constexpr bool constructor3 = 
        !impl::is_bool_like<T> && std::is_convertible_v<T, bool>;
    template <typename T>
    static constexpr bool constructor4 = !constructor3<T> && impl::has_empty<T>;
    template <typename T>
    static constexpr bool constructor5 = !constructor4<T> && impl::has_size<T>;

public:
    static py::Type Type;

    template <typename T>
    static constexpr bool like = impl::is_bool_like<T>;

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    BERTRAND_PYTHON_CONSTRUCTORS(Object, Bool, PyBool_Check, convert_to_bool)

    /* Default constructor.  Initializes to False. */
    Bool() : Object(Py_False, borrowed_t{}) {}

    /* Implicitly convert C++ booleans into py::Bool. */
    template <typename T, std::enable_if_t<constructor1<T>, int> = 0>
    Bool(const T& value) : Object(value ? Py_True : Py_False, borrowed_t{}) {}

    /* Implicitly convert Python booleans into py::Bool.  Borrows a reference. */
    template <typename T, std::enable_if_t<constructor2<T>, int> = 0>
    Bool(const T& value) : Object(value.ptr(), borrowed_t{}) {}

    /* Implicitly convert Python booleans into py::Bool.  Steals a reference. */
    template <typename T, std::enable_if_t<constructor2<std::decay_t<T>>, int> = 0>
    Bool(T&& value) : Object(value.release(), stolen_t{}) {}

    /* Trigger explicit conversions to bool. */
    template <typename T, std::enable_if_t<constructor3<T>, int> = 0>
    explicit Bool(const T& value) : Bool(static_cast<bool>(value)) {}

    /* Explicitly convert any C++ or Python object that implements a `.empty()` method
    into a py::Bool. */
    template <typename T, std::enable_if_t<constructor4<T>, int> = 0>
    explicit Bool(const T& obj) : Bool(!obj.empty()) {}

    /* Explicitly convert and C++ or Python object that implements a `.size()` method
    into a py::Bool. */
    template <typename T, std::enable_if_t<constructor5<T>, int> = 0>
    explicit Bool(const T& obj) : Bool(obj.size() > 0) {}

    /* Explicitly convert a std::tuple into a py::Bool. */
    template <typename... Args>
    explicit Bool(const std::tuple<Args...>& obj) : Bool(sizeof...(Args) > 0) {}

    /* Explicitly convert a string literal into a py::Bool. */
    explicit Bool(const char* str) : Bool(std::string(str)) {}

    ///////////////////////////
    ////    CONVERSIONS    ////
    ///////////////////////////

    /* Implicitly convert a py::Bool into a C++ boolean. */
    inline operator bool() const {
        return Object::operator bool();
    }

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    using Ops::operator<;
    using Ops::operator<=;
    using Ops::operator==;
    using Ops::operator!=;
    using Ops::operator>=;
    using Ops::operator>;
    using Ops::operator~;
    using Ops::operator+;
    using Ops::operator-;
    using Ops::operator*;
    using Ops::operator/;
    using Ops::operator%;
    using Ops::operator<<;
    using Ops::operator>>;
    using Ops::operator&;
    using Ops::operator|;
    using Ops::operator^;

    template <typename T, std::enable_if_t<impl::is_bool_like<T>, int> = 0>
    inline Bool& operator&=(const T& other) {
        return Ops::operator&=(other);
    }

    template <typename T, std::enable_if_t<impl::is_bool_like<T>, int> = 0>
    inline Bool& operator|=(const T& other) {
        return Ops::operator|=(other);
    }

    template <typename T, std::enable_if_t<impl::is_bool_like<T>, int> = 0>
    inline Bool& operator^=(const T& other) {
        return Ops::operator^=(other);
    }

};


}  // namespace python
}  // namespace bertrand


BERTRAND_STD_HASH(bertrand::py::Bool)


#endif  // BERTRAND_PYTHON_BOOL_H
