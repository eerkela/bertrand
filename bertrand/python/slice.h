#ifndef  BERTRAND_PYTHON_SLICE_H
#define  BERTRAND_PYTHON_SLICE_H

#include "common.h"


namespace bertrand {
namespace py {


/* Wrapper around pybind11::slice that allows it to be instantiated with non-integer
inputs in order to represent denormalized slices at the Python level, and provides more
pythonic access to its members. */
class Slice : public Object, public impl::Ops<Slice> {

    static PyObject* convert_to_slice(PyObject* obj) {
        PyObject* result = PySlice_New(nullptr, obj, nullptr);
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }

public:
    static py::Type Type;
    BERTRAND_PYTHON_CONSTRUCTORS(Object, Slice, PySlice_Check, convert_to_slice);

    /* Default constructor.  Initializes to all Nones. */
    Slice() : Object(PySlice_New(nullptr, nullptr, nullptr), stolen_t{}) {}

    /* Construct a slice from a (possibly denormalized) stop object. */
    template <typename Stop>
    Slice(Stop&& stop) {
        m_ptr = PySlice_New(
            nullptr,
            detail::object_or_cast(std::forward<Stop>(stop)).ptr(),
            nullptr
        );
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Construct a slice from (possibly denormalized) start and stop objects. */
    template <typename Start, typename Stop>
    Slice(Start&& start, Stop&& stop) {
        m_ptr = PySlice_New(
            detail::object_or_cast(std::forward<Start>(start)).ptr(),
            detail::object_or_cast(std::forward<Stop>(stop)).ptr(),
            nullptr
        );
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Construct a slice from (possibly denormalized) start, stop, and step objects. */
    template <typename Start, typename Stop, typename Step>
    Slice(Start&& start, Stop&& stop, Step&& step) {
        m_ptr = PySlice_New(
            detail::object_or_cast(std::forward<Start>(start)).ptr(),
            detail::object_or_cast(std::forward<Stop>(stop)).ptr(),
            detail::object_or_cast(std::forward<Step>(step)).ptr()
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

    using impl::Ops<Slice>::operator<;
    using impl::Ops<Slice>::operator<=;
    using impl::Ops<Slice>::operator==;
    using impl::Ops<Slice>::operator!=;
    using impl::Ops<Slice>::operator>=;
    using impl::Ops<Slice>::operator>;
};


}  // namespace python
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_SLICE_H
