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
struct __dereference__<Range>                               : Returns<detail::args_proxy> {};
template <>
struct __len__<Range>                                       : Returns<size_t> {};
template <>
struct __iter__<Range>                                      : Returns<Int> {};
template <>
struct __reversed__<Range>                                  : Returns<Int> {};
template <int_like T>
struct __contains__<Range, T>                               : Returns<bool> {};
template <int_like T>
struct __getitem__<Range, T>                                : Returns<Int> {};
template <>
struct __getitem__<Range, Slice>                            : Returns<Range> {};

}


/* New subclass of pybind11::object that represents a range object at the Python
level. */
class Range : public Object {
    using Base = Object;

    inline static bool range_check(PyObject* obj) {
        int result = PyObject_IsInstance(obj, (PyObject*) &PyRange_Type);
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

public:
    static Type type;

    template <typename T>
    static constexpr bool like = impl::range_like<T>;

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    BERTRAND_OBJECT_COMMON(Base, Range, range_check)

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
        static const pybind11::str method = "start";
        Py_ssize_t result = PyLong_AsSsize_t(attr(method)->ptr());
        if (result == -1 && PyErr_Occurred()) {
            throw error_already_set();
        }
        return result;
    }

    /* Get the stop index of the Range sequence. */
    inline Py_ssize_t stop() const {
        static const pybind11::str method = "stop";
        Py_ssize_t result = PyLong_AsSsize_t(attr(method)->ptr());
        if (result == -1 && PyErr_Occurred()) {
            throw error_already_set();
        }
        return result;
    }

    /* Get the step size of the Range sequence. */
    inline Py_ssize_t step() const {
        static const pybind11::str method = "step";
        Py_ssize_t result = PyLong_AsSsize_t(attr(method)->ptr());
        if (result == -1 && PyErr_Occurred()) {
            throw error_already_set();
        }
        return result;
    }

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    inline Int operator[](size_t index) const {
        PyObject* result = PySequence_GetItem(this->ptr(), index);
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Int>(result);
    }

    inline Range operator[](const Slice& slice) const {
        PyObject* result = PyObject_GetItem(this->ptr(), slice.ptr());
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Range>(result);
    }

    inline Range operator[](
        const std::initializer_list<impl::SliceInitializer>& slice
    ) const {
        return (*this)[Slice(slice)];
    }

    template <impl::int_like T>
    inline bool contains(const T& item) const {
        int result = PySequence_Contains(this->ptr(), detail::object_or_cast(item).ptr());
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

};


}  // namespace py
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_RANGE_H
