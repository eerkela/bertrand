#if !defined(BERTRAND_PYTHON_INCLUDED) && !defined(LINTER)
#error "This file should not be included directly.  Please include <bertrand/python.h> instead."
#endif

#ifndef BERTRAND_PYTHON_RANGE_H
#define BERTRAND_PYTHON_RANGE_H

#include "common.h"
#include "int.h"


namespace bertrand {
namespace py {


template <std::derived_from<Range> T>
struct __getattr__<T, "count">                              : Returns<Function<
    Int(typename Arg<"value", const Int&>::pos)
>> {};
template <std::derived_from<Range> T>
struct __getattr__<T, "index">                              : Returns<Function<
    Int(typename Arg<"value", const Int&>::pos)
>> {};
template <std::derived_from<Range> T>
struct __getattr__<T, "start">                              : Returns<Int> {};
template <std::derived_from<Range> T>
struct __getattr__<T, "stop">                               : Returns<Int> {};
template <std::derived_from<Range> T>
struct __getattr__<T, "step">                               : Returns<Int> {};

template <>
struct __len__<Range>                                       : Returns<size_t> {};
template <>
struct __iter__<Range>                                      : Returns<Int> {};
template <>
struct __reversed__<Range>                                  : Returns<Int> {};
template <>
struct __contains__<Range, Object>                          : Returns<bool> {};
template <impl::int_like T>
struct __contains__<Range, T>                               : Returns<bool> {};
template <>
struct __getitem__<Range, Object>                           : Returns<Object> {};
template <impl::int_like T>
struct __getitem__<Range, T>                                : Returns<Int> {};
template <>
struct __getitem__<Range, Slice>                            : Returns<Range> {};


/* Represents a statically-typed Python `range` object in C++. */
class Range : public Object {
    using Base = Object;

public:
    static const Type type;

    template <typename T>
    static consteval bool typecheck() {
        return impl::range_like<T>;
    }

    template <typename T>
    static constexpr bool typecheck(const T& obj) {
        if constexpr (impl::cpp_like<T>) {
            return typecheck<T>();

        } else if constexpr (typecheck<T>()) {
            return obj.ptr() != nullptr;

        } else if constexpr (impl::is_object_exact<T>) {
            if (obj.ptr() == nullptr) {
                return false;
            }
            int result = PyObject_IsInstance(
                obj.ptr(),
                (PyObject*) &PyRange_Type
            );
            if (result == -1) {
                Exception::from_python();
            }
            return result;

        } else {
            return false;
        }
    }

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    Range(Handle h, const borrowed_t& t) : Base(h, t) {}
    Range(Handle h, const stolen_t& t) : Base(h, t) {}

    template <impl::pybind11_like T> requires (typecheck<T>())
    Range(T&& other) : Base(std::forward<T>(other)) {}

    template <typename Policy>
    Range(const pybind11::detail::accessor<Policy>& accessor) :
        Base(Base::from_pybind11_accessor<Range>(accessor).release(), stolen_t{})
    {}

    /* Default constructor.  Initializes to an empty range. */
    Range() : Range(Int::zero) {}

    /* Explicitly construct a range from 0 to the given stop index (exclusive). */
    explicit Range(const Int& stop) :
        Base(PyObject_CallOneArg((PyObject*) &PyRange_Type, stop.ptr()))
    {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /* Explicitly construct a range from the given start and stop indices
    (exclusive), with an optional step size. */
    explicit Range(const Int& start, const Int& stop, const Int& step = 1) : Base(
        PyObject_CallFunctionObjArgs(
            (PyObject*) &PyRange_Type,
            start.ptr(),
            stop.ptr(),
            step.ptr(),
            nullptr
        ),
        stolen_t{}
    ) {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    ////////////////////////////////
    ////    PYTHON INTERFACE    ////
    ////////////////////////////////

    /* Get the number of occurrences of a given number within the range. */
    [[nodiscard]] Py_ssize_t count(
        Py_ssize_t value,
        Py_ssize_t start = 0,
        Py_ssize_t stop = -1
    ) const {
        if (start != 0 || stop != -1) {
            PyObject* slice = PySequence_GetSlice(this->ptr(), start, stop);
            if (slice == nullptr) {
                Exception::from_python();
            }
            Py_ssize_t result = PySequence_Count(slice, Int(value).ptr());
            Py_DECREF(slice);
            if (result == -1) {
                Exception::from_python();
            }
            return result;
        } else {
            Py_ssize_t result = PySequence_Count(this->ptr(), Int(value).ptr());
            if (result == -1) {
                Exception::from_python();
            }
            return result;
        }

    }

    /* Get the index of a given number within the range. */
    [[nodiscard]] Py_ssize_t index(
        Py_ssize_t value,
        Py_ssize_t start = 0,
        Py_ssize_t stop = -1
    ) const {
        if (start != 0 || stop != -1) {
            PyObject* slice = PySequence_GetSlice(this->ptr(), start, stop);
            if (slice == nullptr) {
                Exception::from_python();
            }
            Py_ssize_t result = PySequence_Index(slice, Int(value).ptr());
            Py_DECREF(slice);
            if (result == -1) {
                Exception::from_python();
            }
            return result;
        } else {
            Py_ssize_t result = PySequence_Index(this->ptr(), Int(value).ptr());
            if (result == -1) {
                Exception::from_python();
            }
            return result;
        }
    }

    /* Get the start index of the Range sequence. */
    [[nodiscard]] Py_ssize_t start() const {
        Py_ssize_t result = PyLong_AsSsize_t(attr<"start">()->ptr());
        if (result == -1 && PyErr_Occurred()) {
            Exception::from_python();
        }
        return result;
    }

    /* Get the stop index of the Range sequence. */
    [[nodiscard]] Py_ssize_t stop() const {
        Py_ssize_t result = PyLong_AsSsize_t(attr<"stop">()->ptr());
        if (result == -1 && PyErr_Occurred()) {
            Exception::from_python();
        }
        return result;
    }

    /* Get the step size of the Range sequence. */
    [[nodiscard]] Py_ssize_t step() const {
        Py_ssize_t result = PyLong_AsSsize_t(attr<"step">()->ptr());
        if (result == -1 && PyErr_Occurred()) {
            Exception::from_python();
        }
        return result;
    }

};


}  // namespace py
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_RANGE_H
