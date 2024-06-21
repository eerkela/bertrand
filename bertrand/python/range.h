#if !defined(BERTRAND_PYTHON_INCLUDED) && !defined(LINTER)
#error "This file should not be included directly.  Please include <bertrand/python.h> instead."
#endif

#ifndef BERTRAND_PYTHON_RANGE_H
#define BERTRAND_PYTHON_RANGE_H

#include "common.h"
#include "int.h"


namespace bertrand {
namespace py {


/* Represents a statically-typed Python `range` object in C++. */
class Range : public Object {
    using Base = Object;
    using Self = Range;

public:
    static const Type type;

    Range(Handle h, borrowed_t t) : Base(h, t) {}
    Range(Handle h, stolen_t t) : Base(h, t) {}

    template <typename... Args>
        requires (
            std::is_invocable_r_v<Range, __init__<Range, std::remove_cvref_t<Args>...>, Args...> &&
            __init__<Range, std::remove_cvref_t<Args>...>::enable
        )
    Range(Args&&... args) : Base(
        __init__<Range, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    ) {}

    template <typename... Args>
        requires (
            !__init__<Range, std::remove_cvref_t<Args>...>::enable &&
            std::is_invocable_r_v<Range, __explicit_init__<Range, std::remove_cvref_t<Args>...>, Args...> &&
            __explicit_init__<Range, std::remove_cvref_t<Args>...>::enable
        )
    explicit Range(Args&&... args) : Base(
        __explicit_init__<Range, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    ) {}

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
    [[nodiscard]] auto start() const {
        return attr<"start">().value();
    }

    /* Get the stop index of the Range sequence. */
    [[nodiscard]] auto stop() const {
        return attr<"stop">().value();
    }

    /* Get the step size of the Range sequence. */
    [[nodiscard]] auto step() const {
        return attr<"step">().value();
    }

};


template <typename T>
struct __issubclass__<T, Range>                             : Returns<bool> {
    static consteval bool operator()() {
        return impl::range_like<T>;
    }
    static consteval bool operator()(const T& obj) {
        return operator()();
    }
};


template <typename T>
struct __isinstance__<T, Range>                             : Returns<bool> {
    static constexpr bool operator()(const T& obj) {
        if constexpr (impl::cpp_like<T>) {
            return issubclass<T, Range>();
        } else if constexpr (issubclass<T, Range>()) {
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
};


template <impl::int_like Start, impl::int_like Stop, impl::int_like Step>
struct __init__<Range, Start, Stop, Step>                     : Returns<Range> {
    static auto operator()(const Int& start, const Int& stop, const Int& step) {
        PyObject* result = PyObject_CallFunctionObjArgs(
            (PyObject*) &PyRange_Type,
            start.ptr(),
            stop.ptr(),
            step.ptr(),
            nullptr
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Range>(result);
    }
};
template <impl::int_like Start, impl::int_like Stop>
struct __init__<Range, Start, Stop>                           : Returns<Range> {
    static auto operator()(const Int& start, const Int& stop) {
        return Range(start, stop, Int::one);
    }
};
template <impl::int_like T>
struct __init__<Range, T>                                   : Returns<Range> {
    static auto operator()(const Int& stop) {
        PyObject* result = PyObject_CallOneArg(
            (PyObject*) &PyRange_Type,
            stop.ptr()
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Range>(result);
    }
};
template <>
struct __init__<Range>                                      : Returns<Range> {
    static auto operator()() {
        return Range(Int::zero);
    }
};


}  // namespace py
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_RANGE_H
