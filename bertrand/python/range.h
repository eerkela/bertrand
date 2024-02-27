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
class Range : public Object, public impl::Ops<Range> {
    using Ops = impl::Ops<Range>;

    inline static bool range_check(PyObject* obj) {
        int result = PyObject_IsInstance(obj, (PyObject*) &PyRange_Type);
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

    inline static PyObject* convert_to_range(PyObject* obj) {
        return PyObject_CallOneArg((PyObject*) &PyRange_Type, obj);
    }

    template <typename T>
    static constexpr bool constructor1 = impl::is_range_like<T> && impl::is_object<T>;

public:
    static py::Type Type;

    template <typename T>
    static constexpr bool like = impl::is_range_like<T>;

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    BERTRAND_PYTHON_CONSTRUCTORS(Object, Range, range_check, convert_to_range)

    /* Default constructor.  Initializes to an empty range. */
    Range() {
        m_ptr = PyObject_CallOneArg((PyObject*) &PyRange_Type, Int(0).ptr());
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Implicitly convert a Python range into a py::Range.  Borrows a reference. */
    template <typename T, std::enable_if_t<constructor1<T>, int> = 0>
    Range(const T& value) : Object(value.ptr(), borrowed_t{}) {}

    /* Implicitly convert a Python range into a py::Range.  Steals a reference. */
    template <typename T, std::enable_if_t<constructor1<T>, int> = 0>
    Range(T&& value) : Object(value.release(), stolen_t{}) {}

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

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    using Ops::operator==;
    using Ops::operator!=;
};


}  // namespace python
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_RANGE_H
