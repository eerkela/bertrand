#ifndef BERTRAND_PYTHON_INCLUDED
#error "This file should not be included directly.  Please include <bertrand/python.h> instead."
#endif

#ifndef  BERTRAND_PYTHON_SLICE_H
#define  BERTRAND_PYTHON_SLICE_H

#include "common.h"


namespace bertrand {
namespace py {


/* Wrapper around pybind11::slice that allows it to be instantiated with non-integer
inputs in order to represent denormalized slices at the Python level, and provides more
pythonic access to its members. */
class Slice : public impl::Ops {
    using Base = impl::Ops;

public:
    static py::Type Type;

    template <typename T>
    static constexpr bool check() { return impl::is_slice_like<T>; }

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    BERTRAND_OBJECT_CONSTRUCTORS(Base, Slice, PySlice_Check)

    /* Default constructor.  Initializes to all Nones. */
    Slice() : Base(PySlice_New(nullptr, nullptr, nullptr), stolen_t{}) {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Initializer list constructor. */
    Slice(std::initializer_list<SliceIndex> indices) {
        if (indices.size() > 3) {
            throw ValueError("slices must be of the form {[start[, stop[, step]]]}");
        }
        size_t i = 0;
        std::array<Object, 3> params {None, None, None};
        for (const SliceIndex& index : indices) {
            params[i++] = std::visit(
                [](auto&& arg) -> Object {
                    return detail::object_or_cast(arg);
                },
                index
            );
        }
        m_ptr = PySlice_New(params[0].ptr(), params[1].ptr(), params[2].ptr());
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Explicitly construct a slice from a (possibly denormalized) stop object. */
    template <typename Stop>
    explicit Slice(const Stop& stop) {
        m_ptr = PySlice_New(nullptr, detail::object_or_cast(stop).ptr(), nullptr);
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Explicitly construct a slice from (possibly denormalized) start and stop
    objects. */
    template <typename Start, typename Stop>
    explicit Slice(const Start& start, const Stop& stop) {
        m_ptr = PySlice_New(
            detail::object_or_cast(start).ptr(),
            detail::object_or_cast(stop).ptr(),
            nullptr
        );
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Explicitly construct a slice from (possibly denormalized) start, stop, and step
    objects. */
    template <typename Start, typename Stop, typename Step>
    explicit Slice(const Start& start, const Stop& stop, const Step& step) {
        m_ptr = PySlice_New(
            detail::object_or_cast(start).ptr(),
            detail::object_or_cast(stop).ptr(),
            detail::object_or_cast(step).ptr()
        );
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    ////////////////////////////////
    ////    PYTHON INTERFACE    ////
    ////////////////////////////////

    /* Get the start object of the slice.  Note that this might not be an integer. */
    inline Object start() const {
        return this->attr("start");
    }

    /* Get the stop object of the slice.  Note that this might not be an integer. */
    inline Object stop() const {
        return this->attr("stop");
    }

    /* Get the step object of the slice.  Note that this might not be an integer. */
    inline Object step() const {
        return this->attr("step");
    }

    /* Data struct containing normalized indices obtained from a py::Slice object. */
    struct Indices {
        Py_ssize_t start;
        Py_ssize_t stop;
        Py_ssize_t step;
        Py_ssize_t length;
    };

    /* Normalize the indices of this slice against a container of the given length.
    This accounts for negative indices and clips those that are out of bounds.
    Returns a simple data struct with the following fields:

        * (Py_ssize_t) start: the normalized start index
        * (Py_ssize_t) stop: the normalized stop index
        * (Py_ssize_t) step: the normalized step size
        * (Py_ssize_t) length: the number of indices that are included in the slice

    It can be destructured using structured bindings:

        auto [start, stop, step, length] = slice.indices(size);
    */
    inline Indices indices(size_t size) const {
        Py_ssize_t start, stop, step, length = 0;
        if (PySlice_GetIndicesEx(
            this->ptr(),
            static_cast<Py_ssize_t>(size),
            &start,
            &stop,
            &step,
            &length
        )) {
            throw error_already_set();
        }
        return {start, stop, step, length};
    }

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    using Base::operator<;
    using Base::operator<=;
    using Base::operator==;
    using Base::operator!=;
    using Base::operator>=;
    using Base::operator>;
};


}  // namespace python
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_SLICE_H
