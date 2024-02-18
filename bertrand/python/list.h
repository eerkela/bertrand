#ifndef BERTRAND_PYTHON_LIST_H
#define BERTRAND_PYTHON_LIST_H

#include "common.h"


namespace bertrand {
namespace py {


/* Wrapper around pybind11::list that allows it to be directly initialized using
std::initializer_list and replicates the Python interface as closely as possible. */
class List :
    public pybind11::list,
    public impl::SequenceOps<List>,
    public impl::FullCompare<List>,
    public impl::ReverseIterable<List>
{
    using Base = pybind11::list;
    using Ops = impl::SequenceOps<List>;
    using Compare = impl::FullCompare<List>;

    static PyObject* convert_to_list(PyObject* obj) {
        PyObject* result = PySequence_List(obj);
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }

public:
    CONSTRUCTORS(List, PyList_Check, convert_to_list);

    /* Default constructor.  Initializes to an empty list. */
    List() : Base([] {
        PyObject* result = PyList_New(0);
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }(), stolen_t{}) {}

    /* Pack the contents of a braced initializer into a new Python list. */
    List(const std::initializer_list<impl::Initializer>& contents) : Base([&contents] {
        PyObject* result = PyList_New(contents.size());
        if (result == nullptr) {
            throw error_already_set();
        }
        try {
            size_t i = 0;
            for (const impl::Initializer& element : contents) {
                PyList_SET_ITEM(
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

    /* Pack the contents of a braced initializer into a new Python list. */
    template <typename T, std::enable_if_t<!std::is_same_v<impl::Initializer, T>, int> = 0>
    List(const std::initializer_list<T>& contents) : Base([&contents] {
        PyObject* result = PyList_New(contents.size());
        if (result == nullptr) {
            throw error_already_set();
        }
        try {
            size_t i = 0;
            for (const T& element : contents) {
                PyList_SET_ITEM(result, i++, impl::convert_newref(element));
            }
            return result;
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
    }(), stolen_t{}) {}

    /* Unpack a generic container into a new List.  Equivalent to Python
    `list(container)`, except that it also works on C++ containers. */
    template <typename T>
    explicit List(T&& container) : Base([&container]{
        if constexpr (detail::is_pyobject<T>::value) {
            PyObject* result = PySequence_List(container.ptr());
            if (result == nullptr) {
                throw error_already_set();
            }
            return result;
        } else {
            size_t size = 0;
            if constexpr (impl::has_size<T>) {
                size = container.size();
            }
            PyObject* result = PyList_New(size);
            if (result == nullptr) {
                throw error_already_set();
            }
            try {
                size_t i = 0;
                for (auto&& item : container) {
                    PyList_SET_ITEM(
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

    ///////////////////////////////////
    ////    PyList* API METHODS    ////
    ///////////////////////////////////

    /* Get the size of the list. */
    inline size_t size() const noexcept {
        return static_cast<size_t>(PyList_GET_SIZE(this->ptr()));
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
    template <typename T>
    inline void extend(T&& items) {
        if constexpr (detail::is_pyobject<T>::value) {
            this->attr("extend")(std::forward<T>(items));
        } else {
            for (auto&& item : items) {
                append(std::forward<decltype(item)>(item));
            }
        }
    }

    /* Equivalent to Python `list.extend(items)`, where items are given as a braced
    initializer list. */
    template <typename T>
    inline void extend(const std::initializer_list<T>& items) {
        for (const T& item : items) {
            append(item);
        }
    }

    /* Equivalent to Python `list.extend(items)`, where items are given as a braced
    initializer list. */
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
    inline void sort(bool reverse) {
        this->attr("sort")(py::arg("reverse") = pybind11::bool_(reverse));
    }

    /* Equivalent to Python `list.sort(key=key[, reverse=reverse])`.  The key function
    can be given as any C++ function-like object, but users should note that pybind11
    has a hard time parsing generic arguments, so templates and the `auto` keyword
    should be avoided. */
    template <typename Func>
    inline void sort(Func&& key, bool reverse = false) {
        py::cpp_function func(std::forward<Func>(key));
        pybind11::bool_ flag(reverse);
        this->attr("sort")(py::arg("key") = func, py::arg("reverse") = flag);
    }

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

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
    using Ops::operator*;
    using Ops::operator*=;

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
                PyList_SET_ITEM(result, i++, impl::convert_newref(item));
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

    template <typename T>
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


/* Equivalent to Python `sorted(obj)`. */
inline List sorted(const pybind11::handle& obj) {
    PyObject* result = PyObject_CallOneArg(
        PyDict_GetItemString(PyEval_GetBuiltins(), "sorted"),
        obj.ptr()
    );
    if (result == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<List>(result);
}


/* Equivalent to Python `dir()` with no arguments.  Returns a list of names in the
current local scope. */
inline List dir() {
    PyObject* result = PyObject_Dir(nullptr);
    if (result == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<List>(result);
}


/* Equivalent to Python `dir(obj)`. */
inline List dir(const pybind11::handle& obj) {
    if (obj.ptr() == nullptr) {
        throw TypeError("cannot call dir() on a null object");
    }
    PyObject* result = PyObject_Dir(obj.ptr());
    if (result == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<List>(result);
}


}  // namespace python
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_LIST_H
