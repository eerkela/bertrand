#if !defined(BERTRAND_PYTHON_INCLUDED) && !defined(LINTER)
#error "This file should not be included directly.  Please include <bertrand/python.h> instead."
#endif

#ifndef BERTRAND_PYTHON_RANGE_H
#define BERTRAND_PYTHON_RANGE_H

#include "common.h"
#include "int.h"


namespace bertrand {
namespace py {


namespace impl {

template <>
struct __len__<Range>                                       : Returns<size_t> {};
template <>
struct __iter__<Range>                                      : Returns<Int> {};
template <>
struct __reversed__<Range>                                  : Returns<Int> {};
template <>
struct __contains__<Range, Object>                          : Returns<bool> {};
template <int_like T>
struct __contains__<Range, T>                               : Returns<bool> {};
template <>
struct __getitem__<Range, Object>                           : Returns<Object> {};
template <int_like T>
struct __getitem__<Range, T>                                : Returns<Int> {};
template <>
struct __getitem__<Range, Slice>                            : Returns<Range> {};

}


/* New subclass of pybind11::object that represents a range object at the Python
level. */
class Range : public Object {
    using Base = Object;

    inline static bool runtime_check(PyObject* obj) {
        int result = PyObject_IsInstance(obj, (PyObject*) &PyRange_Type);
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

public:
    static Type type;

    BERTRAND_OBJECT_COMMON(Base, Range, impl::range_like, runtime_check)
    BERTRAND_OBJECT_OPERATORS(Range)

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Default constructor.  Initializes to an empty range. */
    Range() : Range(Int::zero()) {}

    /* Copy/move constructors. */
    template <typename T> requires (check<T>() && impl::python_like<T>)
    Range(T&& other) : Base(std::forward<T>(other)) {}

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
        Py_ssize_t result = PyLong_AsSsize_t(attr<"start">()->ptr());
        if (result == -1 && PyErr_Occurred()) {
            throw error_already_set();
        }
        return result;
    }

    /* Get the stop index of the Range sequence. */
    inline Py_ssize_t stop() const {
        Py_ssize_t result = PyLong_AsSsize_t(attr<"stop">()->ptr());
        if (result == -1 && PyErr_Occurred()) {
            throw error_already_set();
        }
        return result;
    }

    /* Get the step size of the Range sequence. */
    inline Py_ssize_t step() const {
        Py_ssize_t result = PyLong_AsSsize_t(attr<"step">()->ptr());
        if (result == -1 && PyErr_Occurred()) {
            throw error_already_set();
        }
        return result;
    }

};


}  // namespace py
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_RANGE_H
