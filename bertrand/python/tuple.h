#ifndef BERTRAND_PYTHON_INCLUDED
#error "This file should not be included directly.  Please include <bertrand/python.h> instead."
#endif

#ifndef BERTRAND_PYTHON_TUPLE_H
#define BERTRAND_PYTHON_TUPLE_H

#include "common.h"


namespace bertrand {
namespace py {


/* Wrapper around pybind11::tuple that allows it to be directly initialized using
std::initializer_list and replicates the Python interface as closely as possible. */
class Tuple :
    public Object,
    public impl::Ops<Tuple>,
    public impl::SequenceOps<Tuple>,
    public impl::ReverseIterable<Tuple>
{
    using Ops = impl::Ops<Tuple>;
    using SequenceOps = impl::SequenceOps<Tuple>;

    static PyObject* convert_to_tuple(PyObject* obj) {
        return PySequence_Tuple(obj);
    }

    template <typename T>
    static inline PyObject* convert_newref(T&& value) {
        if constexpr (detail::is_pyobject<T>::value) {
            PyObject* result = value.ptr();
            if (result == nullptr) {
                result = Py_None;
            }
            return Py_NewRef(result);
        } else {
            PyObject* result = pybind11::cast(std::forward<T>(value)).release().ptr();
            if (result == nullptr) {
                throw error_already_set();
            }
            return result;
        }
    }

public:
    static py::Type Type;

    template <typename T>
    static constexpr bool like = impl::is_tuple_like<T>;

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    BERTRAND_PYTHON_CONSTRUCTORS(Object, Tuple, PyTuple_Check, convert_to_tuple)

    /* Default constructor.  Initializes to empty tuple. */
    Tuple() : Object(PyTuple_New(0), stolen_t{}) {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Pack the contents of a homogenously-typed braced initializer into a new Python
    tuple. */
    template <typename T, std::enable_if_t<!std::is_same_v<impl::Initializer, T>, int> = 0>
    Tuple(const std::initializer_list<T>& contents) :
        Object(PyTuple_New(contents.size()), stolen_t{})
    {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
        try {
            size_t i = 0;
            for (const T& item : contents) {
                PyTuple_SET_ITEM(m_ptr, i++, convert_newref(item));
            }
        } catch (...) {
            Py_DECREF(m_ptr);
            throw;
        }
    }

    /* Pack the contents of a mixed-type braced initializer into a new Python tuple. */
    Tuple(const std::initializer_list<impl::Initializer>& contents) :
        Object(PyTuple_New(contents.size()), stolen_t{})
    {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
        try {
            size_t i = 0;
            for (const impl::Initializer& element : contents) {
                PyTuple_SET_ITEM(
                    m_ptr,
                    i++,
                    const_cast<impl::Initializer&>(element).value.release().ptr()
                );
            }
        } catch (...) {
            Py_DECREF(m_ptr);
            throw;
        }
    }

    /* Explicitly unpack a generic C++ or Python container into a new py::Tuple. */
    template <
        typename T,
        std::enable_if_t<!(impl::is_object<T> && impl::is_list_like<T>), int> = 0
    >
    explicit Tuple(T&& container) {
        if constexpr (detail::is_pyobject<T>::value) {
            m_ptr = PySequence_Tuple(container.ptr());
            if (m_ptr == nullptr) {
                throw error_already_set();
            }
        } else {
            size_t size = 0;
            if constexpr (impl::has_size<T>) {
                size = container.size();
            }
            m_ptr = PyTuple_New(size);
            if (m_ptr == nullptr) {
                throw error_already_set();
            }
            try {
                size_t i = 0;
                for (auto&& item : container) {
                    PyTuple_SET_ITEM(
                        m_ptr,
                        i++,
                        convert_newref(std::forward<decltype(item)>(item))
                    );
                }
            } catch (...) {
                Py_DECREF(m_ptr);
                throw;
            }
        }
    }

    /* Explicitly unpack a Python list into a py::Tuple directly using the C API. */
    template <
        typename T,
        std::enable_if_t<impl::is_object<T> && impl::is_list_like<T>, int> = 0
    >
    explicit Tuple(T&& list) : Object(PyList_AsTuple(list.ptr()), stolen_t{}) {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    ////////////////////////////
    ////    PyTuple_ API    ////
    ////////////////////////////

    /* Get the size of the tuple. */
    inline size_t size() const noexcept {
        return PyTuple_GET_SIZE(this->ptr());
    }

    /* Check if the tuple is empty. */
    inline bool empty() const noexcept {
        return size() == 0;
    }

    /* Get the underlying PyObject* array. */
    inline PyObject** data() const noexcept {
        return PySequence_Fast_ITEMS(this->ptr());
    }

    /* Directly access an item without bounds checking or constructing a proxy. */
    inline Object GET_ITEM(Py_ssize_t index) const {
        return reinterpret_borrow<Object>(PyTuple_GET_ITEM(this->ptr(), index));
    }

    /* Directly set an item without bounds checking or constructing a proxy.
    
    NOTE: This steals a reference to `value` and does not clear the previous
    item if one is present.  This is dangerous, and should only be used to fill in
    a newly-allocated (empty) tuple. */
    template <typename T>
    inline void SET_ITEM(Py_ssize_t index, PyObject* value) {
        PyTuple_SET_ITEM(this->ptr(), index, value);
    }

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    using Object::operator[];

    inline impl::TupleAccessor operator[](size_t index) const {
        return {*this, index};
    }

    detail::tuple_iterator begin() const {
        return {*this, 0};
    }

    detail::tuple_iterator end() const {
        return {*this, PyTuple_GET_SIZE(this->ptr())};
    }

    using Ops::operator<;
    using Ops::operator<=;
    using Ops::operator==;
    using Ops::operator!=;
    using Ops::operator>;
    using Ops::operator>=;

    using SequenceOps::concat;
    using SequenceOps::operator+;
    using SequenceOps::operator+=;
    using SequenceOps::operator*;
    using SequenceOps::operator*=;

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
                PyTuple_SET_ITEM(result, i++, convert_newref(item));
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
