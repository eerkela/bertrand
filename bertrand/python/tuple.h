#ifndef BERTRAND_PYTHON_TUPLE_H
#define BERTRAND_PYTHON_TUPLE_H

#include "common.h"


namespace bertrand {
namespace py {


/* Wrapper around pybind11::tuple that allows it to be directly initialized using
std::initializer_list and replicates the Python interface as closely as possible. */
class Tuple :
    public pybind11::tuple,
    public impl::SequenceOps<Tuple>,
    public impl::FullCompare<Tuple>,
    public impl::ReverseIterable<Tuple>
{
    using Base = pybind11::tuple;
    using Ops = impl::SequenceOps<Tuple>;
    using Compare = impl::FullCompare<Tuple>;

    static PyObject* convert_to_tuple(PyObject* obj) {
        PyObject* result = PySequence_Tuple(obj);
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }

public:
    CONSTRUCTORS(Tuple, PyTuple_Check, convert_to_tuple);

    /* Default constructor.  Initializes to empty tuple. */
    inline Tuple() : Base([] {
        PyObject* result = PyTuple_New(0);
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }(), stolen_t{}) {}

    /* Pack the contents of a braced initializer into a new Python tuple. */
    template <typename T>
    Tuple(const std::initializer_list<T>& contents) : Base([&contents] {
        PyObject* result = PyTuple_New(contents.size());
        if (result == nullptr) {
            throw error_already_set();
        }
        try {
            size_t i = 0;
            for (const T& item : contents) {
                PyTuple_SET_ITEM(result, i++, impl::convert_newref(item));
            }
            return result;
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
    }(), stolen_t{}) {}

    /* Pack the contents of a braced initializer into a new Python tuple. */
    Tuple(const std::initializer_list<impl::Initializer>& contents) : Base([&contents] {
        PyObject* result = PyTuple_New(contents.size());
        if (result == nullptr) {
            throw error_already_set();
        }
        try {
            size_t i = 0;
            for (const impl::Initializer& element : contents) {
                PyTuple_SET_ITEM(
                    result,
                    i++,
                    const_cast<impl::Initializer&>(element).value.release().ptr()
                );
            }
            return result;
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
    }(), stolen_t{}) {}

    /* Unpack a pybind11::list into a new tuple directly using the C API. */
    template <
        typename T,
        std::enable_if_t<std::is_base_of_v<pybind11::list, T>, int> = 0
    >
    explicit Tuple(T&& list) : Base([&list] {
        PyObject* result = PyList_AsTuple(list.ptr());
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }(), stolen_t{}) {}

    /* Unpack a generic container into a new Python tuple. */
    template <
        typename T,
        std::enable_if_t<!std::is_base_of_v<pybind11::list, T>, int> = 0
    >
    explicit Tuple(T&& container) : Base([&container] {
        if constexpr (detail::is_pyobject<T>::value) {
            PyObject* result = PySequence_Tuple(container.ptr());
            if (result == nullptr) {
                throw error_already_set();
            }
            return result;
        } else {
            size_t size = 0;
            if constexpr (impl::has_size<T>) {
                size = container.size();
            }
            PyObject* result = PyTuple_New(size);
            if (result == nullptr) {
                throw error_already_set();
            }
            try {
                size_t i = 0;
                for (auto&& item : container) {
                    PyTuple_SET_ITEM(
                        result,
                        i++,
                        impl::convert_newref(std::forward<decltype(item)>(item))
                    );
                }
                return result;
            } catch (...) {
                Py_DECREF(result);
                throw;
            }
        }
    }(), stolen_t{}) {}

        /*     PyTuple_ API     */

    /* Get the size of the tuple. */
    inline size_t size() const noexcept {
        return PyTuple_GET_SIZE(this->ptr());
    }

    /* Get the underlying PyObject* array. */
    inline PyObject** data() const noexcept {
        return PySequence_Fast_ITEMS(this->ptr());
    }

    /* Directly access an item without bounds checking or constructing a proxy. */
    inline Object GET_ITEM(Py_ssize_t index) const {
        return reinterpret_borrow<Object>(PyTuple_GET_ITEM(this->ptr(), index));
    }

    /* Directly set an item without bounds checking or constructing a proxy. */
    template <typename T>
    inline void SET_ITEM(Py_ssize_t index, PyObject* value) {
        /* NOTE: This steals a reference to `value` and does not clear the previous */
        /* item if one is present.  This is dangerous, and should only be used to */
        /* fill in a newly-allocated (empty) tuple. */
        PyTuple_SET_ITEM(this->ptr(), index, value);
    }

        /*    OPERATORS    */

    using Base::operator[];
    using Ops::operator[];

    using Compare::operator<;
    using Compare::operator<=;
    using Compare::operator==;
    using Compare::operator!=;
    using Compare::operator>;
    using Compare::operator>=;

    using Ops::concat;
    using Ops::operator+;
    using Ops::operator+=;
    using Ops::operator*;
    using Ops::operator*=;

    /* Overload of concat() that allows the operand to be a braced initializer list. */
    template <typename T>
    inline Tuple concat(const std::initializer_list<T>& items) const {
        PyObject* result = PyTuple_New(size() + items.size());
        if (result == nullptr) {
            throw error_already_set();
        }
        try {
            size_t i = 0;
            size_t length = size();
            PyObject** array = data();
            while (i < length) {
                PyTuple_SET_ITEM(result, i, Py_NewRef(array[i]));
                ++i;
            }
            for (const T& item : items) {
                PyTuple_SET_ITEM(result, i++, impl::convert_newref(item));
            }
            return reinterpret_steal<Tuple>(result);
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
    }

    /* Overload of concat() that allows the operand to be a braced initializer list. */
    inline Tuple concat(const std::initializer_list<impl::Initializer>& items) const {
        PyObject* result = PyTuple_New(size() + items.size());
        if (result == nullptr) {
            throw error_already_set();
        }
        try {
            size_t i = 0;
            size_t length = size();
            PyObject** array = data();
            while (i < length) {
                PyTuple_SET_ITEM(result, i, Py_NewRef(array[i]));
                ++i;
            }
            for (const impl::Initializer& item : items) {
                PyTuple_SET_ITEM(
                    result,
                    i++,
                    const_cast<impl::Initializer&>(item).value.release().ptr()
                );
            }
            return reinterpret_steal<Tuple>(result);
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
    }

    template <typename T>
    inline Tuple operator+(const std::initializer_list<T>& items) const {
        return concat(items);
    }

    inline Tuple operator+(const std::initializer_list<impl::Initializer>& items) const {
        return concat(items);
    }

    template <typename T>
    inline Tuple& operator+=(const std::initializer_list<T>& items) {
        *this = concat(items);
        return *this;
    }

    inline Tuple& operator+=(const std::initializer_list<impl::Initializer>& items) {
        *this = concat(items);
        return *this;
    }

};


}  // namespace python
}  // namespace bertrand


BERTRAND_STD_HASH(bertrand::py::Tuple)


#endif  // BERTRAND_PYTHON_TUPLE_H
