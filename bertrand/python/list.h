#if !defined(BERTRAND_PYTHON_INCLUDED) && !defined(LINTER)
#error "This file should not be included directly.  Please include <bertrand/python.h> instead."
#endif

#ifndef BERTRAND_PYTHON_LIST_H
#define BERTRAND_PYTHON_LIST_H

#include "common.h"
#include "bool.h"


namespace bertrand {
namespace py {


template <>
struct __getattr__<List, "append">                          : Returns<Function> {};
template <>
struct __getattr__<List, "extend">                          : Returns<Function> {};
template <>
struct __getattr__<List, "insert">                          : Returns<Function> {};
template <>
struct __getattr__<List, "copy">                            : Returns<Function> {};
template <>
struct __getattr__<List, "clear">                           : Returns<Function> {};
template <>
struct __getattr__<List, "remove">                          : Returns<Function> {};
template <>
struct __getattr__<List, "pop">                             : Returns<Function> {};
template <>
struct __getattr__<List, "reverse">                         : Returns<Function> {};
template <>
struct __getattr__<List, "sort">                            : Returns<Function> {};
template <>
struct __getattr__<List, "count">                           : Returns<Function> {};
template <>
struct __getattr__<List, "index">                           : Returns<Function> {};

template <>
struct __len__<List>                                        : Returns<size_t> {};
template <>
struct __iter__<List>                                       : Returns<Object> {};
template <>
struct __reversed__<List>                                   : Returns<Object> {};
template <typename T>
struct __contains__<List, T>                                : Returns<bool> {};
template <impl::int_like T>
struct __getitem__<List, T>                                 : Returns<Object> {};
template <>
struct __getitem__<List, Slice>                             : Returns<List> {};
template <impl::int_like Key, typename Value>
struct __setitem__<List, Key, Value>                        : Returns<void> {};
template <impl::list_like Value>
struct __setitem__<List, Slice, Value>                      : Returns<void> {};
template <impl::int_like Key>
struct __delitem__<List, Key>                               : Returns<void> {};
template <>
struct __delitem__<List, Slice>                             : Returns<void> {};
template <>
struct __lt__<List, Object>                                 : Returns<bool> {};
template <impl::list_like T>
struct __lt__<List, T>                                      : Returns<bool> {};
template <>
struct __le__<List, Object>                                 : Returns<bool> {};
template <impl::list_like T>
struct __le__<List, T>                                      : Returns<bool> {};
template <>
struct __ge__<List, Object>                                 : Returns<bool> {};
template <impl::list_like T>
struct __ge__<List, T>                                      : Returns<bool> {};
template <>
struct __gt__<List, Object>                                 : Returns<bool> {};
template <impl::list_like T>
struct __gt__<List, T>                                      : Returns<bool> {};
template <>
struct __add__<List, Object>                                : Returns<List> {};
template <impl::list_like T>
struct __add__<List, T>                                     : Returns<List> {};
template <>
struct __iadd__<List, Object>                               : Returns<List&> {};
template <impl::list_like T>
struct __iadd__<List, T>                                    : Returns<List&> {};
template <>
struct __mul__<List, Object>                                : Returns<List> {};
template <impl::int_like T>
struct __mul__<List, T>                                     : Returns<List> {};
template <>
struct __imul__<List, Object>                               : Returns<List&> {};
template <impl::int_like T>
struct __imul__<List, T>                                    : Returns<List&> {};


/* Wrapper around pybind11::list that allows it to be directly initialized using
std::initializer_list and replicates the Python interface as closely as possible. */
class List : public Object, public impl::SequenceOps<List> {
    using Base = Object;

    template <typename T>
    static inline PyObject* convert_newref(const T& value) {
        if constexpr (impl::python_like<T>) {
            PyObject* result = value.ptr();
            if (result == nullptr) {
                result = Py_None;
            }
            return Py_NewRef(result);
        } else {
            return Object(value).release().ptr();
        }
    }

public:
    static Type type;

    BERTRAND_OBJECT_COMMON(Base, List, impl::list_like, PyList_Check)
    BERTRAND_OBJECT_OPERATORS(List)

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Default constructor.  Initializes to an empty list. */
    List() : Base(PyList_New(0), stolen_t{}) {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /* Copy/move constructors. */
    template <typename T> requires (check<T>() && impl::python_like<T>)
    List(T&& other) : Base(std::forward<T>(other)) {}

    /* Pack the contents of a braced initializer list into a new Python list. */
    List(const std::initializer_list<impl::Initializer>& contents) :
        Base(PyList_New(contents.size()), stolen_t{})
    {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
        try {
            size_t i = 0;
            for (const impl::Initializer& item : contents) {
                PyList_SET_ITEM(
                    m_ptr,
                    i++,
                    const_cast<impl::Initializer&>(item).first.release().ptr()
                );
            }
        } catch (...) {
            Py_DECREF(m_ptr);
            throw;
        }
    }

    /* Construct a new list from a pair of input iterators. */
    template <typename Iter, std::sentinel_for<Iter> Sentinel>
    explicit List(Iter first, Sentinel last) : Base(PyList_New(0), stolen_t{}) {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
        try {
            while (first != last) {
                append(*first);
                ++first;
            }
        } catch (...) {
            Py_DECREF(m_ptr);
            throw;
        }
    }

    /* Explicitly unpack an arbitrary Python container into a new py::List. */
    template <typename T>
        requires (impl::is_iterable<T> && impl::python_like<T> && !impl::list_like<T>)
    explicit List(const T& contents) :
        Base(PySequence_List(contents.ptr()), stolen_t{})
    {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /* Explicitly unpack a generic C++ container into a new py::List. */
    template <typename T> requires (impl::is_iterable<T> && !impl::python_like<T>)
    explicit List(T&& contents) {
        if constexpr (impl::has_size<T>) {
            size_t size = std::size(contents);
            m_ptr = PyList_New(size);  // TODO: this can potentially create an empty list
            if (m_ptr == nullptr) {
                Exception::from_python();
            }
            try {
                size_t i = 0;
                for (auto&& item : contents) {
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
        } else {
            m_ptr = PyList_New(0);
            if (m_ptr == nullptr) {
                Exception::from_python();
            }
            try {
                for (auto&& item : contents) {
                    append(item);
                }
            } catch (...) {
                Py_DECREF(m_ptr);
                throw;
            }
        }
    }

    /* Explicitly unpack a std::pair into a py::List. */
    template <typename First, typename Second>
    explicit List(const std::pair<First, Second>& pair) :
        Base(PyList_New(2), stolen_t{})
    {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
        try {
            PyList_SET_ITEM(m_ptr, 0, convert_newref(pair.first));
            PyList_SET_ITEM(m_ptr, 1, convert_newref(pair.second));
        } catch (...) {
            Py_DECREF(m_ptr);
            throw;
        }
    }

    /* Explicitly unpack a std::tuple into a py::List. */
    template <typename... Args>
    explicit List(const std::tuple<Args...>& tuple) :
        Base(PyList_New(sizeof...(Args)), stolen_t{})
    {
        auto unpack_tuple = []<typename... As, size_t... Ns>(
            PyObject* result,
            const std::tuple<Args...>& tuple,
            std::index_sequence<Ns...>
        ) {
            (
                PyList_SET_ITEM(
                    result,
                    Ns,
                    convert_newref(std::get<Ns>(tuple))
                ),
                ...
            );
        };

        if (m_ptr == nullptr) {
            Exception::from_python();
        }
        try {
            unpack_tuple(m_ptr, tuple, std::index_sequence_for<Args...>{});
        } catch (...) {
            Py_DECREF(m_ptr);
            throw;
        }
    }

    /////////////////////////////
    ////    C++ INTERFACE    ////
    /////////////////////////////

    /* Implicitly convert to pybind11::list. */
    inline operator pybind11::list() const {
        return reinterpret_borrow<pybind11::list>(m_ptr);
    }

    /* Implicitly convert a py::List into a C++ std::array.  Throws an error if the
    list does not have the expected length. */
    template <typename T, size_t N>
    inline operator std::array<T, N>() const {
        if (size() != N) {
            std::ostringstream msg;
            msg << "conversion to std::array requires list of size " << N << ", not "
                << size();
            throw IndexError(msg.str());
        }
        std::array<T, N> result;
        for (size_t i = 0; i < N; ++i) {
            result[i] = static_cast<T>(GET_ITEM(i));
        }
        return result;
    }

    /* Implicitly convert a Python list into a C++ vector, deque, list, or forward
    list. */
    template <typename T> requires (!impl::python_like<T> && impl::list_like<T>)
    inline operator T() const {
        T result;
        if constexpr (impl::has_reserve<T>) {
            result.reserve(size());
        }
        for (auto&& item : *this) {
            result.push_back(static_cast<typename T::value_type>(item));
        }
        return result;
    }

    /* Get the underlying PyObject* array. */
    inline PyObject** data() const noexcept {
        return PySequence_Fast_ITEMS(this->ptr());
    }

    /* Directly access an item without bounds checking or constructing a proxy. */
    inline Object GET_ITEM(Py_ssize_t index) const {
        return reinterpret_borrow<Object>(PyList_GET_ITEM(this->ptr(), index));
    }

    /* Directly set an item without bounds checking or constructing a proxy.
    
    NOTE: This steals a reference to `value` and does not clear the previous
    item if one is present.  This is dangerous, and should only be used to fill in
    a newly-allocated (empty) list. */
    template <typename T>
    inline void SET_ITEM(Py_ssize_t index, PyObject* value) {
        PyList_SET_ITEM(this->ptr(), index, value);
    }

    ////////////////////////////////
    ////    PYTHON INTERFACE    ////
    ////////////////////////////////

    /* Equivalent to Python `list.append(value)`. */
    template <typename T>
    inline void append(const T& value) {
        if (PyList_Append(this->ptr(), detail::object_or_cast(value).ptr())) {
            Exception::from_python();
        }
    }

    /* Equivalent to Python `list.extend(items)`. */
    template <impl::is_iterable T>
    inline void extend(const T& items);

    /* Equivalent to Python `list.extend(items)`, where items are given as a braced
    initializer list. */
    inline void extend(const std::initializer_list<impl::Initializer>& items) {
        for (const impl::Initializer& item : items) {
            append(item.first);
        }
    }

    /* Equivalent to Python `list.insert(index, value)`. */
    template <typename T>
    inline void insert(Py_ssize_t index, const T& value) {
        if (PyList_Insert(this->ptr(), index, detail::object_or_cast(value).ptr())) {
            Exception::from_python();
        }
    }

    /* Equivalent to Python `list.copy()`. */
    inline List copy() const {
        PyObject* result = PyList_GetSlice(this->ptr(), 0, size());
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<List>(result);
    }

    /* Equivalent to Python `list.clear()`. */
    inline void clear() {
        if (PyList_SetSlice(this->ptr(), 0, size(), nullptr)) {
            Exception::from_python();
        }
    }

    /* Equivalent to Python `list.remove(value)`. */
    template <typename T>
    inline void remove(const T& value);

    /* Equivalent to Python `list.pop([index])`. */
    inline Object pop(Py_ssize_t index = -1);

    /* Equivalent to Python `list.reverse()`. */
    inline void reverse() {
        if (PyList_Reverse(this->ptr())) {
            Exception::from_python();
        }
    }

    /* Equivalent to Python `list.sort()`. */
    inline void sort() {
        if (PyList_Sort(this->ptr())) {
            Exception::from_python();
        }
    }

    /* Equivalent to Python `list.sort(reverse=reverse)`. */
    inline void sort(const Bool& reverse);

    /* Equivalent to Python `list.sort(key=key[, reverse=reverse])`.  The key function
    can be given as any C++ function-like object, but users should note that pybind11
    has a hard time parsing generic arguments, so templates and the `auto` keyword
    should be avoided. */
    inline void sort(const Function& key, const Bool& reverse = false);

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    inline friend List operator+(
        const List& self,
        const std::initializer_list<impl::Initializer>& items
    ) {
        return self.concat(items);
    }

    inline friend List operator+(
        const std::initializer_list<impl::Initializer>& items,
        const List& self
    ) {
        return self.concat(items);
    }

    inline friend List& operator+=(
        List& self,
        const std::initializer_list<impl::Initializer>& items
    ) {
        self.extend(items);
        return self;
    }

protected:

    using impl::SequenceOps<List>::operator_add;
    using impl::SequenceOps<List>::operator_iadd;
    using impl::SequenceOps<List>::operator_mul;
    using impl::SequenceOps<List>::operator_imul;

    template <typename Return, typename T>
    inline static size_t operator_len(const T& self) {
        return static_cast<size_t>(PyList_GET_SIZE(self.ptr()));
    }

    inline List concat(const std::initializer_list<impl::Initializer>& items) const {
        PyObject* result = PyList_New(size() + items.size());
        if (result == nullptr) {
            Exception::from_python();
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
                    const_cast<impl::Initializer&>(item).first.release().ptr()
                );
            }
            return reinterpret_steal<List>(result);
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
    }

    template <typename Return, typename T>
    inline static auto operator_begin(const T& obj)
        -> impl::Iterator<impl::ListIter<Return>>
    {
        return impl::Iterator<impl::ListIter<Return>>(obj, 0);
    }

    template <typename Return, typename T>
    inline static auto operator_end(const T& obj)
        -> impl::Iterator<impl::ListIter<Return>>
    {
        return impl::Iterator<impl::ListIter<Return>>(PyList_GET_SIZE(obj.ptr()));
    }

    template <typename Return, typename T>
    inline static auto operator_rbegin(const T& obj)
        -> impl::ReverseIterator<impl::ListIter<Return>>
    {
        return impl::ReverseIterator<impl::ListIter<Return>>(
            obj,
            PyList_GET_SIZE(obj.ptr()) - 1
        );
    }

    template <typename Return, typename T>
    inline static auto operator_rend(const T& obj)
        -> impl::ReverseIterator<impl::ListIter<Return>>
    {
        return impl::ReverseIterator<impl::ListIter<Return>>(-1);
    }

};


}  // namespace py
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_LIST_H
