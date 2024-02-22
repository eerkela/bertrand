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

    /* Implicitly convert to a C++ bool. */
    inline operator bool() const {
        int result = PyObject_IsTrue(this->ptr());
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

};


}  // namespace python
}  // namespace bertrand


BERTRAND_STD_HASH(bertrand::py::Bool)


#endif  // BERTRAND_PYTHON_BOOL_H
