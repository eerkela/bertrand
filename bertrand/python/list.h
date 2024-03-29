#if !defined(BERTRAND_PYTHON_INCLUDED) && !defined(LINTER)
#error "This file should not be included directly.  Please include <bertrand/python.h> instead."
#endif

#ifndef BERTRAND_PYTHON_LIST_H
#define BERTRAND_PYTHON_LIST_H

#include "common.h"
#include "bool.h"


namespace bertrand {
namespace py {



namespace impl {

template <>
struct __len__<List>                                        : Returns<size_t> {};
template <>
struct __iter__<List>                                       : Returns<Object> {};
template <>
struct __reversed__<List>                                   : Returns<Object> {};
template <typename T>
struct __contains__<List, T>                                : Returns<bool> {};
template <int_like T>
struct __getitem__<List, T>                                 : Returns<Object> {};
template <>
struct __getitem__<List, Slice>                             : Returns<List> {};
template <int_like Key, typename Value>
struct __setitem__<List, Key, Value>                        : Returns<void> {};
template <list_like Value>
struct __setitem__<List, Slice, Value>                      : Returns<void> {};
template <int_like Key>
struct __delitem__<List, Key>                               : Returns<void> {};
template <>
struct __delitem__<List, Slice>                             : Returns<void> {};
template <>
struct __lt__<List, Object>                                 : Returns<bool> {};
template <list_like T>
struct __lt__<List, T>                                      : Returns<bool> {};
template <>
struct __le__<List, Object>                                 : Returns<bool> {};
template <list_like T>
struct __le__<List, T>                                      : Returns<bool> {};
template <>
struct __ge__<List, Object>                                 : Returns<bool> {};
template <list_like T>
struct __ge__<List, T>                                      : Returns<bool> {};
template <>
struct __gt__<List, Object>                                 : Returns<bool> {};
template <list_like T>
struct __gt__<List, T>                                      : Returns<bool> {};
template <>
struct __add__<List, Object>                                : Returns<List> {};
template <list_like T>
struct __add__<List, T>                                     : Returns<List> {};
template <>
struct __iadd__<List, Object>                               : Returns<List&> {};
template <list_like T>
struct __iadd__<List, T>                                    : Returns<List&> {};
template <>
struct __mul__<List, Object>                                : Returns<List> {};
template <int_like T>
struct __mul__<List, T>                                     : Returns<List> {};
template <>
struct __imul__<List, Object>                               : Returns<List&> {};
template <int_like T>
struct __imul__<List, T>                                    : Returns<List&> {};

}


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
            PyObject* result = pybind11::cast(value).release().ptr();
            if (result == nullptr) {
                throw error_already_set();
            }
            return result;
        }
    }

    template <typename T>
    static constexpr bool py_unpacking_constructor =
        !impl::list_like<T> && impl::python_like<T> && impl::is_iterable<T>;
    template <typename T>
    static constexpr bool cpp_unpacking_constructor =
        !impl::python_like<T> && impl::is_iterable<T>;

public:
    static Type type;

    template <typename T>
    static constexpr bool check() { return impl::list_like<T>; }

    BERTRAND_OBJECT_COMMON(Base, List, PyList_Check)

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Default constructor.  Initializes to an empty list. */
    List() : Base(PyList_New(0), stolen_t{}) {
        if (m_ptr == nullptr) {
            throw error_already_set();
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
            throw error_already_set();
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

    /* Pack the contents of a braced initializer list into a new Python list. */
    template <typename T, std::enable_if_t<impl::is_initializer<T>, int> = 0>
    List(const std::initializer_list<T>& contents) :
        Base(PyList_New(contents.size()), stolen_t{})
    {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
        try {
            size_t i = 0;
            for (const T& item : contents) {
                PyList_SET_ITEM(m_ptr, i++, const_cast<T&>(item).first.release().ptr());
            }
        } catch (...) {
            Py_DECREF(m_ptr);
            throw;
        }
    }

    /* Explicitly unpack an arbitrary Python container into a new py::List. */
    template <typename T> requires (py_unpacking_constructor<T>)
    explicit List(const T& contents) :
        Base(PySequence_List(contents.ptr()), stolen_t{})
    {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Explicitly unpack a generic C++ container into a new py::List. */
    template <typename T> requires (cpp_unpacking_constructor<T>)
    explicit List(const T& contents) {
        size_t size = 0;
        if constexpr (impl::has_size<T>) {
            size = contents.size();
        }
        m_ptr = PyList_New(size);
        if (m_ptr == nullptr) {
            throw error_already_set();
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
    }

    /* Explicitly unpack a std::pair into a py::List. */
    template <typename First, typename Second>
    explicit List(const std::pair<First, Second>& pair) :
        Base(PyList_New(2), stolen_t{})
    {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
        try {
            PyList_SET_ITEM(m_ptr, 0, convert_newref(pair.first));
            PyList_SET_ITEM(m_ptr, 1, convert_newref(pair.second));
        } catch (...) {
            Py_DECREF(m_ptr);
            throw;
        }
    }

private:

    template <typename... Args, size_t... N>
    inline static void unpack_tuple(
        PyObject* result,
        const std::tuple<Args...>& tuple,
        std::index_sequence<N...>
    ) {
        (PyList_SET_ITEM(result, N, convert_newref(std::get<N>(tuple))), ...);
    }

public:

    /* Explicitly unpack a std::tuple into a py::List. */
    template <typename... Args>
    explicit List(const std::tuple<Args...>& tuple) :
        Base(PyList_New(sizeof...(Args)), stolen_t{})
    {
        if (m_ptr == nullptr) {
            throw error_already_set();
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
            result.push_back(item.template cast<typename T::value_type>());
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
            throw error_already_set();
        }
    }

    /* Equivalent to Python `list.extend(items)`. */
    template <impl::is_iterable T>
    inline void extend(const T& items) {
        if constexpr (impl::python_like<T>) {
            attr<"extend">()(detail::object_or_cast(items));
        } else {
            for (auto&& item : items) {
                append(std::forward<decltype(item)>(item));
            }
        }
    }

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
    inline void remove(const T& value) {
        attr<"remove">()(detail::object_or_cast(value));
    }

    /* Equivalent to Python `list.pop([index])`. */
    inline Object pop(Py_ssize_t index = -1) {
        return attr<"pop">()(index);
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
        attr<"sort">()(py::arg("reverse") = reverse);
    }

    /* Equivalent to Python `list.sort(key=key[, reverse=reverse])`.  The key function
    can be given as any C++ function-like object, but users should note that pybind11
    has a hard time parsing generic arguments, so templates and the `auto` keyword
    should be avoided. */
    inline void sort(const Function& key, const Bool& reverse = false);

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    inline List operator+(const std::initializer_list<impl::Initializer>& items) const {
        return concat(items);
    }

    inline friend List operator+(
        const std::initializer_list<impl::Initializer>& items,
        const List& list
    ) {
        return list.concat(items);
    }

    inline List& operator+=(const std::initializer_list<impl::Initializer>& items) {
        extend(items);
        return *this;
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
        return {obj, 0};
    }

    template <typename Return, typename T>
    inline static auto operator_end(const T& obj)
        -> impl::Iterator<impl::ListIter<Return>>
    {
        return {PyList_GET_SIZE(obj.ptr())};
    }

    template <typename Return, typename T>
    inline static auto operator_rbegin(const T& obj)
        -> impl::ReverseIterator<impl::ListIter<Return>>
    {
        return {obj, PyList_GET_SIZE(obj.ptr()) - 1};
    }

    template <typename Return, typename T>
    inline static auto operator_rend(const T& obj)
        -> impl::ReverseIterator<impl::ListIter<Return>>
    {
        return {-1};
    }

};


}  // namespace py
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_LIST_H
