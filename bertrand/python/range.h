#ifndef BERTRAND_PYTHON_RANGE_H
#define BERTRAND_PYTHON_RANGE_H

#include "common.h"


namespace bertrand {
namespace py {


/* New subclass of pybind11::object that represents a range object at the Python
level. */
class Range : public Object, public impl::Ops<Range> {

    inline static bool range_check(PyObject* obj) {
        int result = PyObject_IsInstance(obj, (PyObject*) &PyRange_Type);
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

    inline static PyObject* convert_to_range(PyObject* obj) {
        PyObject* result = PyObject_CallOneArg((PyObject*) &PyRange_Type, obj);
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }

public:
    static py::Type Type;
    BERTRAND_PYTHON_CONSTRUCTORS(Object, Range, range_check, convert_to_range)

    /* Construct a range from 0 to the given stop index (exclusive). */
    Range(Py_ssize_t stop = 0) {
        m_ptr = PyObject_CallOneArg(
            (PyObject*) &PyRange_Type,
            py::cast(stop).ptr()
        );
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Construct a range from the given start and stop indices (exclusive). */
    Range(Py_ssize_t start, Py_ssize_t stop, Py_ssize_t step = 1) {
        m_ptr = PyObject_CallFunctionObjArgs(
            (PyObject*) &PyRange_Type,
            py::cast(start).ptr(),
            py::cast(stop).ptr(),
            py::cast(step).ptr(),
            nullptr
        );
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    ////////////////////////////////
    ////    PYTHON INTERFACE    ////
    ////////////////////////////////

    /* Get the start index of the Range sequence. */
    inline Py_ssize_t start() const {
        return PyLong_AsSsize_t(this->attr("start").ptr());
    }

    /* Get the stop index of the Range sequence. */
    inline Py_ssize_t stop() const {
        return PyLong_AsSsize_t(this->attr("stop").ptr());
    }

    /* Get the step size of the Range sequence. */
    inline Py_ssize_t step() const {
        return PyLong_AsSsize_t(this->attr("step").ptr());
    }    

    using impl::Ops<Range>::operator==;
    using impl::Ops<Range>::operator!=;
};


}  // namespace python
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_RANGE_H
