#ifndef BERTRAND_PYTHON_RANGE_H
#define BERTRAND_PYTHON_RANGE_H

#include "common.h"


namespace bertrand {
namespace py {


/* New subclass of pybind11::object that represents a range object at the Python
level. */
class Range :
    public pybind11::object,
    public impl::EqualCompare<Range>
{
    using Base = pybind11::object;
    using Compare = impl::EqualCompare<Range>;

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
    CONSTRUCTORS(Range, range_check, convert_to_range);

    /* Construct a range from 0 to the given stop index (exclusive). */
    explicit Range(Py_ssize_t stop) : Base([&stop] {
        PyObject* range = PyDict_GetItemString(PyEval_GetBuiltins(), "range");
        PyObject* result = PyObject_CallFunction(range, "n", stop);
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }(), stolen_t{}) {}

    /* Construct a range from the given start and stop indices (exclusive). */
    explicit Range(
        Py_ssize_t start,
        Py_ssize_t stop,
        Py_ssize_t step = 1
    ) : Base([&start, &stop, &step] {
        PyObject* range = PyDict_GetItemString(PyEval_GetBuiltins(), "range");
        PyObject* result = PyObject_CallFunction(range, "nnn", start, stop, step);
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }(), stolen_t{}) {}

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

};


/* Equivalent to Python `range(stop)`. */
inline Range range(Py_ssize_t stop) {
    return Range(stop);
}


/* Equivalent to Python `range(start, stop[, step])`. */
inline Range range(Py_ssize_t start, Py_ssize_t stop, Py_ssize_t step = 1) {
    return Range(start, stop, step);
}


}  // namespace python
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_RANGE_H
