#ifndef BERTRAND_PYTHON_INCLUDED
#error "This file should not be included directly.  Please include <bertrand/python.h> instead."
#endif

#ifndef BERTRAND_PYTHON_RANGE_H
#define BERTRAND_PYTHON_RANGE_H

#include "common.h"
#include "int.h"


namespace bertrand {
namespace py {


/* New subclass of pybind11::object that represents a range object at the Python
level. */
class Range : public impl::Ops {
    using Base = impl::Ops;

    inline static bool range_check(PyObject* obj) {
        int result = PyObject_IsInstance(obj, (PyObject*) &PyRange_Type);
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

public:
    static py::Type Type;

    template <typename T>
    static constexpr bool like = impl::is_range_like<T>;

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    BERTRAND_OBJECT_CONSTRUCTORS(Base, Range, range_check)

    /* Default constructor.  Initializes to an empty range. */
    Range() : Range(Int::zero()) {}

    /* Explicitly construct a range from 0 to the given stop index (exclusive). */
    explicit Range(const Int& stop) {
        m_ptr = PyObject_CallOneArg((PyObject*) &PyRange_Type, stop.ptr());
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Explicitly construct a range from the given start and stop indices
    (exclusive), with an optional step size. */
    explicit Range(const Int& start, const Int& stop, const Int& step = 1) {
        m_ptr = PyObject_CallFunctionObjArgs(
            (PyObject*) &PyRange_Type,
            start.ptr(),
            stop.ptr(),
            step.ptr(),
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
        Py_ssize_t result = PyLong_AsSsize_t(this->attr("start").ptr());
        if (result == -1 && PyErr_Occurred()) {
            throw error_already_set();
        }
        return result;
    }

    /* Get the stop index of the Range sequence. */
    inline Py_ssize_t stop() const {
        Py_ssize_t result = PyLong_AsSsize_t(this->attr("stop").ptr());
        if (result == -1 && PyErr_Occurred()) {
            throw error_already_set();
        }
        return result;
    }

    /* Get the step size of the Range sequence. */
    inline Py_ssize_t step() const {
        Py_ssize_t result = PyLong_AsSsize_t(this->attr("step").ptr());
        if (result == -1 && PyErr_Occurred()) {
            throw error_already_set();
        }
        return result;
    }

};


}  // namespace python
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_RANGE_H
