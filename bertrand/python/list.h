#ifndef BERTRAND_PYTHON_INCLUDED
#error "This file should not be included directly.  Please include <bertrand/python.h> instead."
#endif

#ifndef BERTRAND_PYTHON_LIST_H
#define BERTRAND_PYTHON_LIST_H

#include "common.h"


namespace bertrand {
namespace py {


    /* Convert an arbitrary Python/C++ value into a pybind11 object and then return a
    new reference.  This is used in the constructors of list and tuple objects, whose
    SET_ITEM() functions steal a reference.  Converts null pointers into Python None. */
    


/* Wrapper around pybind11::list that allows it to be directly initialized using
std::initializer_list and replicates the Python interface as closely as possible. */
class List :
    public Object,
    public impl::Ops<List>,
    public impl::SequenceOps<List>,
    public impl::ReverseIterable<List>
{
    using Ops = impl::Ops<List>;
    using SequenceOps = impl::SequenceOps<List>;

    static PyObject* convert_to_list(PyObject* obj) {
        return PySequence_List(obj);
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
    static constexpr bool like = impl::is_list_like<T>;

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    BERTRAND_PYTHON_CONSTRUCTORS(Object, List, PyList_Check, convert_to_list);

    /* Default constructor.  Initializes to an empty list. */
    List() : Object(PyList_New(0), stolen_t{}) {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Pack the contents of a homogenously-typed braced initializer into a new Python
    list. */
    template <typename T, std::enable_if_t<!std::is_same_v<impl::Initializer, T>, int> = 0>
    List(const std::initializer_list<T>& contents) :
        Object(PyList_New(contents.size()), stolen_t{})
    {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
        try {
            size_t i = 0;
            for (const T& element : contents) {
                PyList_SET_ITEM(m_ptr, i++, convert_newref(element));
            }
        } catch (...) {
            Py_DECREF(m_ptr);
            throw;
        }
    }

    /* Pack the contents of a mixed-type braced initializer into a new Python list. */
    List(const std::initializer_list<impl::Initializer>& contents) :
        Object(PyList_New(contents.size()), stolen_t{})
    {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
        try {
            size_t i = 0;
            for (const impl::Initializer& element : contents) {
                PyList_SET_ITEM(
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

    /* Explicitly unpack a generic C++ or Python container into a new py::List. */
    template <typename T>
    explicit List(T&& container) {
        if constexpr (detail::is_pyobject<T>::value) {
            m_ptr = PySequence_List(container.ptr());
            if (m_ptr == nullptr) {
                throw error_already_set();
            }
        } else {
            size_t size = 0;
            if constexpr (impl::has_size<T>) {
                size = container.size();
            }
            m_ptr = PyList_New(size);
            if (m_ptr == nullptr) {
                throw error_already_set();
            }
            try {
                size_t i = 0;
                for (auto&& item : container) {
                    PyList_SET_ITEM(
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

    ///////////////////////////
    ////    CONVERSIONS    ////
    ///////////////////////////

    /* Implicitly convert a Python list into a C++ vector, deque, list, or forward
    list. */
    template <
        typename T,
        std::enable_if_t<impl::is_list_like<T> && !impl::is_object<T>, int> = 0
    >
    inline operator T() const {
        T result;
        if constexpr (impl::has_reserve<T>) {
            result.reserve(size());
        }
        for (auto&& item : *this) {
            result.push_back(item.template cast<typename T::value_type>());
        }
        return result;
    }

    ///////////////////////////////////
    ////    PyList* API METHODS    ////
    ///////////////////////////////////

    /* Get the size of the list. */
    inline size_t size() const noexcept {
        return static_cast<size_t>(PyList_GET_SIZE(this->ptr()));
    }

    /* Check if the list is empty. */
    inline bool empty() const noexcept {
        return size() == 0;
    }

    /* Get the underlying PyObject* array. */
    inline PyObject** data() const noexcept {
        return PySequence_Fast_ITEMS(this->ptr());
    }

    ////////////////////////////////
    ////    PYTHON INTERFACE    ////
    ////////////////////////////////

    /* Equivalent to Python `list.append(value)`. */
    template <typename T>
    inline void append(T&& value) {
        if (PyList_Append(
            this->ptr(),
            detail::object_or_cast(std::forward<T>(value)).ptr()
        )) {
            throw error_already_set();
        }
    }

    /* Equivalent to Python `list.extend(items)`. */
    template <typename T, std::enable_if_t<impl::is_iterable<std::decay_t<T>>, int> = 0>
    inline void extend(T&& items) {
        if constexpr (detail::is_pyobject<T>::value) {
            this->attr("extend")(detail::object_or_cast(std::forward<T>(items)));
        } else {
            for (auto&& item : items) {
                append(std::forward<decltype(item)>(item));
            }
        }
    }

    /* Equivalent to Python `list.extend(items)`, where items are given as a
    homogenously-typed braced initializer list. */
    template <typename T>
    inline void extend(const std::initializer_list<T>& items) {
        for (const T& item : items) {
            append(item);
        }
    }

    /* Equivalent to Python `list.extend(items)`, where items are given as a mixed-type
    braced initializer list. */
    inline void extend(const std::initializer_list<impl::Initializer>& items) {
        for (const impl::Initializer& item : items) {
            append(item.value);
        }
    }

    /* Equivalent to Python `list.insert(index, value)`. */
    template <typename T>
    inline void insert(Py_ssize_t index, T&& value) {
        if (PyList_Insert(
            this->ptr(),
            index,
            detail::object_or_cast(std::forward<T>(value)).ptr()
        )) {
            throw error_already_set();
        }
    }

    /* Equivalent to Python `list.copy()`. */
    inline List copy() const {
        PyObject* result = PyList_GetSlice(this->ptr(), 0, size());
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<List>(result);
    }

    /* Equivalent to Python `list.clear()`. */
    inline void clear() {
        if (PyList_SetSlice(this->ptr(), 0, size(), nullptr)) {
            throw error_already_set();
        }
    }

    /* Equivalent to Python `list.remove(value)`. */
    template <typename T>
    inline void remove(T&& value) {
        this->attr("remove")(detail::object_or_cast(std::forward<T>(value)));
    }

    /* Equivalent to Python `list.pop([index])`. */
    inline Object pop(Py_ssize_t index = -1) {
        return this->attr("pop")(index);
    }

    /* Equivalent to Python `list.reverse()`. */
    inline void reverse() {
        if (PyList_Reverse(this->ptr())) {
            throw error_already_set();
        }
    }

    /* Equivalent to Python `list.sort()`. */
    inline void sort() {
        if (PyList_Sort(this->ptr())) {
            throw error_already_set();
        }
    }

    /* Equivalent to Python `list.sort(reverse=reverse)`. */
    inline void sort(const Bool& reverse) {
        this->attr("sort")(py::arg("reverse") = reverse);
    }

    /* Equivalent to Python `list.sort(key=key[, reverse=reverse])`.  The key function
    can be given as any C++ function-like object, but users should note that pybind11
    has a hard time parsing generic arguments, so templates and the `auto` keyword
    should be avoided. */
    inline void sort(const Function& key, const Bool& reverse = false);

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    using Object::operator[];

    inline impl::ListAccessor operator[](size_t index) const {
        return {*this, index};
    }

    detail::list_iterator begin() const {
        return {*this, 0};
    }

    detail::list_iterator end() const {
        return {*this, PyList_GET_SIZE(this->ptr())};
    }

    using Ops::operator<;
    using Ops::operator<=;
    using Ops::operator==;
    using Ops::operator!=;
    using Ops::operator>;
    using Ops::operator>=;

    // TODO: SequenceOps::operator+ should be constrained to only accept List?

    using SequenceOps::concat;
    using SequenceOps::operator+;
    using SequenceOps::operator*;
    using SequenceOps::operator*=;

    /* Overload of concat() that allows the operand to be a braced initializer list. */
    template <typename T>
    inline List concat(const std::initializer_list<T>& items) const {
        PyObject* result = PyList_New(size() + items.size());
        if (result == nullptr) {
            throw error_already_set();
        }
        try {
            size_t i = 0;
            size_t length = size();
            PyObject** array = data();
            while (i < length) {
                PyList_SET_ITEM(result, i, Py_NewRef(array[i]));
                ++i;
            }
            for (const T& item : items) {
                PyList_SET_ITEM(result, i++, convert_newref(item));
            }
            return reinterpret_steal<List>(result);
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
    }

    /* Overload of concat() that allows the operand to be a braced initializer list. */
    inline List concat(const std::initializer_list<impl::Initializer>& items) const {
        PyObject* result = PyList_New(size() + items.size());
        if (result == nullptr) {
            throw error_already_set();
        }
        try {
            size_t i = 0;
            size_t length = size();
            PyObject** array = data();
            while (i < length) {
                PyList_SET_ITEM(result, i, Py_NewRef(array[i]));
                ++i;
            }
            for (const impl::Initializer& item : items) {
                PyList_SET_ITEM(
                    result,
                    i++,
                    const_cast<impl::Initializer&>(item).value.release().ptr()
                );
            }
            return reinterpret_steal<List>(result);
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
    }

    template <typename T>
    inline List operator+(const std::initializer_list<T>& items) const {
        return concat(items);
    }

    inline List operator+(const std::initializer_list<impl::Initializer>& items) const {
        return concat(items);
    }

    template <typename T, std::enable_if_t<impl::is_list_like<T>, int> = 0>
    inline List& operator+=(T&& items) {
        extend(std::forward<T>(items));
        return *this;
    }

    template <typename T>
    inline List& operator+=(const std::initializer_list<T>& items) {
        extend(items);
        return *this;
    }

    inline List& operator+=(const std::initializer_list<impl::Initializer>& items) {
        extend(items);
        return *this;
    }

};


}  // namespace python
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_LIST_H
