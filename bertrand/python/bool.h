#ifndef BERTRAND_PYTHON_BOOL_H
#define BERTRAND_PYTHON_BOOL_H

#include "common.h"


namespace bertrand {
namespace py {


/* Wrapper around pybind11::bool_ that enables math operations with C++ inputs. */
class Bool : public Object, public impl::Ops<Bool> {

    static PyObject* convert_to_bool(PyObject* obj) {
        int result = PyObject_IsTrue(obj);
        if (result == -1) {
            throw error_already_set();
        }
        return Py_NewRef(result ? Py_True : Py_False);
    }

public:
    static py::Type Type;
    BERTRAND_PYTHON_OPERATORS(impl::Ops<Bool>)
    BERTRAND_PYTHON_CONSTRUCTORS(Object, Bool, PyBool_Check, convert_to_bool)

    /* Default constructor.  Initializes to False. */
    Bool() : Object(Py_False, borrowed_t{}) {}

    /* Construct from a C++ bool. */
    Bool(bool value) : Object(value ? Py_True : Py_False, borrowed_t{}) {}

    /* Construct from an integer or floating point value. */
    template <typename T, std::enable_if_t<std::is_arithmetic<T>::value, int> = 0>
    explicit Bool(T value) : Bool(value != 0) {}

    /* Construct from any object that implements a `.size()` method. */
    template <typename T, std::enable_if_t<impl::has_size<T>, int> = 0>
    explicit Bool(const T& obj) : Bool(obj.size() > 0) {}

    /* Construct from any object that implements an `.empty()` method. */
    template <
        typename T,
        std::enable_if_t<!impl::has_size<T> && impl::has_empty<T>, int> = 0
    >
    explicit Bool(const T& obj) : Bool(!obj.empty()) {}

    /* Construct from a string literal. */
    explicit Bool(const char* str) : Bool(std::string(str)) {}

    /* Construct from a std::tuple. */
    template <typename... Args>
    explicit Bool(const std::tuple<Args...>& obj) : Bool(sizeof...(Args) > 0) {}

};


}  // namespace python
}  // namespace bertrand


BERTRAND_STD_HASH(bertrand::py::Bool)


#endif  // BERTRAND_PYTHON_BOOL_H
