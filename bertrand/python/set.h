#ifndef BERTRAND_PYTHON_INCLUDED
#error "This file should not be included directly.  Please include <bertrand/python.h> instead."
#endif

#ifndef BERTRAND_PYTHON_SET_H
#define BERTRAND_PYTHON_SET_H

#include "common.h"


namespace bertrand {
namespace py {


// TODO: replace ANYSET macro with a base class in the impl:: namespace.


namespace impl {

#define ANYSET_INTERFACE(cls)                                                           \
                                                                                        \
    static PyObject* convert_to_set(PyObject* obj) {                                    \
        PyObject* result = Py##cls##_New(obj);                                          \
        if (result == nullptr) {                                                        \
            throw error_already_set();                                                  \
        }                                                                               \
        return result;                                                                  \
    }                                                                                   \
                                                                                        \
public:                                                                                 \
    static py::Type Type;                                                               \
    BERTRAND_PYTHON_CONSTRUCTORS(Object, cls, Py##cls##_Check, convert_to_set);         \
                                                                                        \
    /* Default constructor.  Initializes to empty set. */                               \
    inline cls() {                                                                      \
        m_ptr = Py##cls##_New(nullptr);                                                 \
        if (m_ptr == nullptr) {                                                         \
            throw error_already_set();                                                  \
        }                                                                               \
    }                                                                                   \
                                                                                        \
    /* Pack the contents of a braced initializer into a new Python set. */              \
    template <typename T>                                                               \
    cls(const std::initializer_list<T>& contents) {                                     \
        m_ptr = Py##cls##_New(nullptr);                                                 \
        if (m_ptr == nullptr) {                                                         \
            throw error_already_set();                                                  \
        }                                                                               \
        try {                                                                           \
            for (const T& element : contents) {                                         \
                if (PySet_Add(m_ptr, detail::object_or_cast(element).ptr())) {          \
                    throw error_already_set();                                          \
                }                                                                       \
            }                                                                           \
        } catch (...) {                                                                 \
            Py_DECREF(m_ptr);                                                           \
            throw;                                                                      \
        }                                                                               \
    }                                                                                   \
                                                                                        \
    /* Pack the contents of a braced initializer into a new Python set. */              \
    cls(const std::initializer_list<impl::Initializer>& contents) {                     \
        m_ptr = Py##cls##_New(nullptr);                                                 \
        if (m_ptr == nullptr) {                                                         \
            throw error_already_set();                                                  \
        }                                                                               \
        try {                                                                           \
            for (const impl::Initializer& element : contents) {                         \
                if (PySet_Add(m_ptr, element.value.ptr())) {                            \
                    throw error_already_set();                                          \
                }                                                                       \
            }                                                                           \
        } catch (...) {                                                                 \
            Py_DECREF(m_ptr);                                                           \
            throw;                                                                      \
        }                                                                               \
    }                                                                                   \
                                                                                        \
    /* Unpack a generic container into a new Python set. */                             \
    template <typename T>                                                               \
    explicit cls(T&& container) {                                                       \
        if constexpr (detail::is_pyobject<T>::value) {                                  \
            m_ptr = Py##cls##_New(container.ptr());                                     \
            if (m_ptr == nullptr) {                                                     \
                throw error_already_set();                                              \
            }                                                                           \
        } else {                                                                        \
            m_ptr = Py##cls##_New(nullptr);                                             \
            if (m_ptr == nullptr) {                                                     \
                throw error_already_set();                                              \
            }                                                                           \
            try {                                                                       \
                for (auto&& item : container) {                                         \
                    if (PySet_Add(                                                      \
                        m_ptr,                                                          \
                        detail::object_or_cast(std::forward<decltype(item)>(item)).ptr()\
                    )) {                                                                \
                        throw error_already_set();                                      \
                    }                                                                   \
                }                                                                       \
            } catch (...) {                                                             \
                Py_DECREF(m_ptr);                                                       \
                throw;                                                                  \
            }                                                                           \
        }                                                                               \
    }                                                                                   \
                                                                                        \
        /*    PySet_ API    */                                                          \
                                                                                        \
    /* Get the size of the set. */                                                      \
    inline size_t size() const noexcept {                                               \
        return static_cast<size_t>(PySet_GET_SIZE(this->ptr()));                        \
    }                                                                                   \
                                                                                        \
    /* Cehcek if the set is empty. */                                                   \
    inline bool empty() const noexcept {                                                \
        return size() == 0;                                                             \
    }                                                                                   \
                                                                                        \
        /*    PYTHON INTERFACE    */                                                    \
                                                                                        \
    /* Equivalent to Python `set.copy()`. */                                            \
    inline cls copy() const {                                                           \
        PyObject* result = Py##cls##_New(this->ptr());                                  \
        if (result == nullptr) {                                                        \
            throw error_already_set();                                                  \
        }                                                                               \
        return reinterpret_steal<cls>(result);                                          \
    }                                                                                   \
                                                                                        \
    /* Equivalent to Python `set.isdisjoint(other)`. */                                 \
    template <typename T>                                                               \
    inline bool isdisjoint(T&& other) const {                                           \
        if constexpr (detail::is_pyobject<std::decay_t<T>>::value) {                    \
            return static_cast<bool>(this->attr("isdisjoint")(other));                  \
        } else {                                                                        \
            for (auto&& item : other) {                                                 \
                if (contains(std::forward<decltype(item)>(item))) {                     \
                    return false;                                                       \
                }                                                                       \
            }                                                                           \
            return true;                                                                \
        }                                                                               \
    }                                                                                   \
                                                                                        \
    /* Equivalent to Python `set.isdisjoint(<braced initializer list>)`. */             \
    template <typename T>                                                               \
    inline bool isdisjoint(const std::initializer_list<T>& other) const {               \
        for (const T& item : other) {                                                   \
            if (contains(item)) {                                                       \
                return false;                                                           \
            }                                                                           \
        }                                                                               \
        return true;                                                                    \
    }                                                                                   \
                                                                                        \
    /* Equivalent to Python `set.isdisjoint(<braced initializer list>)`. */             \
    inline bool isdisjoint(const std::initializer_list<impl::Initializer>& other) const {\
        for (const impl::Initializer& item : other) {                                   \
            if (contains(item)) {                                                       \
                return false;                                                           \
            }                                                                           \
        }                                                                               \
        return true;                                                                    \
    }                                                                                   \
                                                                                        \
    /* Equivalent to Python `set.issubset(other)`. */                                   \
    template <typename T>                                                               \
    inline bool issubset(T&& other) const {                                             \
        return this->attr("issubset")(                                                  \
            detail::object_or_cast(std::forward<T>(other))                              \
        ).template cast<bool>();                                                        \
    }                                                                                   \
                                                                                        \
    /* Equivalent to Python `set.issubset(<braced initializer list>)`. */               \
    template <typename T>                                                               \
    inline bool issubset(const std::initializer_list<T> other) const {                  \
        return static_cast<bool>(this->attr("issubset")(cls(other)));                   \
    }                                                                                   \
                                                                                        \
    /* Equivalent to Python `set.issubset(<braced initializer list>)`. */               \
    inline bool issubset(const std::initializer_list<impl::Initializer>& other) const { \
        return static_cast<bool>(this->attr("issubset")(cls(other)));                   \
    }                                                                                   \
                                                                                        \
    /* Equivalent to Python `set.issuperset(other)`. */                                 \
    template <typename T>                                                               \
    inline bool issuperset(T&& other) const {                                           \
        if constexpr (detail::is_pyobject<std::decay_t<T>>::value) {                    \
            return static_cast<bool>(this->attr("issuperset")(other));                  \
        } else {                                                                        \
            for (auto&& item : other) {                                                 \
                if (!contains(std::forward<decltype(item)>(item))) {                    \
                    return false;                                                       \
                }                                                                       \
            }                                                                           \
            return true;                                                                \
        }                                                                               \
    }                                                                                   \
                                                                                        \
    /* Equivalent to Python `set.issuperset(<braced initializer list>)`. */             \
    template <typename T>                                                               \
    inline bool issuperset(const std::initializer_list<T>& other) const {               \
        for (const T& item : other) {                                                   \
            if (!contains(item)) {                                                      \
                return false;                                                           \
            }                                                                           \
        }                                                                               \
        return true;                                                                    \
    }                                                                                   \
                                                                                        \
    /* Equivalent to Python `set.issuperset(<braced initializer list>)`. */             \
    inline bool issuperset(const std::initializer_list<impl::Initializer>& other) const {\
        for (const impl::Initializer& item : other) {                                   \
            if (!contains(item)) {                                                      \
                return false;                                                           \
            }                                                                           \
        }                                                                               \
        return true;                                                                    \
    }                                                                                   \
                                                                                        \
    /* Equivalent to Python `set.union(*others)`. */                                    \
    template <typename... Args>                                                         \
    inline cls union_(Args&&... others) const {                                         \
        return this->attr("union")(                                                     \
            detail::object_or_cast(std::forward<Args>(others))...                       \
        );                                                                              \
    }                                                                                   \
                                                                                        \
    /* Equivalent to Python `set.union(<braced initializer list>)`. */                  \
    template <typename T>                                                               \
    inline cls union_(const std::initializer_list<T>& other) const {                    \
        PyObject* result = Py##cls##_New(this->ptr());                                  \
        if (result == nullptr) {                                                        \
            throw error_already_set();                                                  \
        }                                                                               \
        try {                                                                           \
            for (const T& item : other) {                                               \
                if (PySet_Add(result, detail::object_or_cast(item).ptr())) {            \
                    throw error_already_set();                                          \
                }                                                                       \
            }                                                                           \
            return reinterpret_steal<cls>(result);                                      \
        } catch (...) {                                                                 \
            Py_DECREF(result);                                                          \
            throw;                                                                      \
        }                                                                               \
    }                                                                                   \
                                                                                        \
    /* Equivalent to Python `set.union(<braced initializer list>)`. */                  \
    inline cls union_(const std::initializer_list<impl::Initializer>& other) const {    \
        PyObject* result = Py##cls##_New(this->ptr());                                  \
        if (result == nullptr) {                                                        \
            throw error_already_set();                                                  \
        }                                                                               \
        try {                                                                           \
            for (const impl::Initializer& item : other) {                               \
                if (PySet_Add(result, item.value.ptr())) {                              \
                    throw error_already_set();                                          \
                }                                                                       \
            }                                                                           \
            return reinterpret_steal<cls>(result);                                      \
        } catch (...) {                                                                 \
            Py_DECREF(result);                                                          \
            throw;                                                                      \
        }                                                                               \
    }                                                                                   \
                                                                                        \
    /* Equivalent to Python `set.intersection(other)`. */                               \
    template <typename... Args>                                                         \
    inline cls intersection(Args&&... others) const {                                   \
        return this->attr("intersection")(                                              \
            detail::object_or_cast(std::forward<Args>(others))...                       \
        );                                                                              \
    }                                                                                   \
                                                                                        \
    /* Equivalent to Python `set.intersection(<braced initializer list>)`. */           \
    template <typename T>                                                               \
    inline cls intersection(const std::initializer_list<T>& other) const {              \
        PyObject* result = Py##cls##_New(nullptr);                                      \
        if (result == nullptr) {                                                        \
            throw error_already_set();                                                  \
        }                                                                               \
        try {                                                                           \
            for (const T& item : other) {                                               \
                Object obj = detail::object_or_cast(item);                              \
                if (contains(obj)) {                                                    \
                    if (PySet_Add(result, obj.ptr())) {                                 \
                        throw error_already_set();                                      \
                    }                                                                   \
                }                                                                       \
            }                                                                           \
            return reinterpret_steal<cls>(result);                                      \
        } catch (...) {                                                                 \
            Py_DECREF(result);                                                          \
            throw;                                                                      \
        }                                                                               \
    }                                                                                   \
                                                                                        \
    /* Equivalent to Python `set.intersection(<braced initializer list>)`. */           \
    inline cls intersection(const std::initializer_list<impl::Initializer>& other) const {\
        PyObject* result = Py##cls##_New(nullptr);                                      \
        if (result == nullptr) {                                                        \
            throw error_already_set();                                                  \
        }                                                                               \
        try {                                                                           \
            for (const impl::Initializer& item : other) {                               \
                if (contains(item.value)) {                                             \
                    if (PySet_Add(result, item.value.ptr())) {                          \
                        throw error_already_set();                                      \
                    }                                                                   \
                }                                                                       \
            }                                                                           \
            return reinterpret_steal<cls>(result);                                      \
        } catch (...) {                                                                 \
            Py_DECREF(result);                                                          \
            throw;                                                                      \
        }                                                                               \
    }                                                                                   \
                                                                                        \
    /* Equivalent to Python `set.difference(other)`. */                                 \
    template <typename... Args>                                                         \
    inline cls difference(Args&&... others) const {                                     \
        return this->attr("difference")(                                                \
            detail::object_or_cast(std::forward<Args>(others))...                       \
        );                                                                              \
    }                                                                                   \
                                                                                        \
    /* Equivalent to Python `set.difference(<braced initializer list>)`. */             \
    template <typename T>                                                               \
    inline cls difference(const std::initializer_list<T>& other) const {                \
        PyObject* result = Py##cls##_New(this->ptr());                                  \
        if (result == nullptr) {                                                        \
            throw error_already_set();                                                  \
        }                                                                               \
        try {                                                                           \
            for (const T& item : other) {                                               \
                if (PySet_Discard(result, detail::object_or_cast(item).ptr()) == -1) {  \
                    throw error_already_set();                                          \
                }                                                                       \
            }                                                                           \
            return reinterpret_steal<cls>(result);                                      \
        } catch (...) {                                                                 \
            Py_DECREF(result);                                                          \
            throw;                                                                      \
        }                                                                               \
    }                                                                                   \
                                                                                        \
    /* Equivalent to Python `set.difference(<braced initializer list>)`. */             \
    inline cls difference(const std::initializer_list<impl::Initializer>& other) const {\
        PyObject* result = Py##cls##_New(this->ptr());                                  \
        if (result == nullptr) {                                                        \
            throw error_already_set();                                                  \
        }                                                                               \
        try {                                                                           \
            for (const impl::Initializer& item : other) {                               \
                if (PySet_Discard(result, item.value.ptr()) == -1) {                    \
                    throw error_already_set();                                          \
                }                                                                       \
            }                                                                           \
            return reinterpret_steal<cls>(result);                                      \
        } catch (...) {                                                                 \
            Py_DECREF(result);                                                          \
            throw;                                                                      \
        }                                                                               \
    }                                                                                   \
                                                                                        \
    /* Equivalent to Python `set.symmetric_difference(other)`. */                       \
    template <typename T>                                                               \
    inline cls symmetric_difference(T&& other) const {                                  \
        return this->attr("symmetric_difference")(                                      \
            detail::object_or_cast(std::forward<T>(other))                              \
        );                                                                              \
    }                                                                                   \
                                                                                        \
    /* Equivalent to Python `set.symmetric_difference(<braced initializer list)`. */    \
    template <typename T>                                                               \
    inline cls symmetric_difference(const std::initializer_list<T>& other) const {      \
        PyObject* result = Py##cls##_New(nullptr);                                      \
        if (result == nullptr) {                                                        \
            throw error_already_set();                                                  \
        }                                                                               \
        try {                                                                           \
            for (const T& item : other) {                                               \
                Object obj = detail::object_or_cast(item);                              \
                if (contains(obj)) {                                                    \
                    if (PySet_Discard(result, obj.ptr()) == -1) {                       \
                        throw error_already_set();                                      \
                    }                                                                   \
                } else {                                                                \
                    if (PySet_Add(result, obj.ptr())) {                                 \
                        throw error_already_set();                                      \
                    }                                                                   \
                }                                                                       \
            }                                                                           \
            return reinterpret_steal<cls>(result);                                      \
        } catch (...) {                                                                 \
            Py_DECREF(result);                                                          \
            throw;                                                                      \
        }                                                                               \
    }                                                                                   \
                                                                                        \
    /* Equivalent to Python `set.symmetric_difference(<braced initializer list)`. */    \
    inline cls symmetric_difference(                                                    \
        const std::initializer_list<impl::Initializer>& other                           \
    ) const {                                                                           \
        PyObject* result = Py##cls##_New(nullptr);                                      \
        if (result == nullptr) {                                                        \
            throw error_already_set();                                                  \
        }                                                                               \
        try {                                                                           \
            for (const impl::Initializer& item : other) {                               \
                if (contains(item.value)) {                                             \
                    if (PySet_Discard(result, item.value.ptr()) == -1) {                \
                        throw error_already_set();                                      \
                    }                                                                   \
                } else {                                                                \
                    if (PySet_Add(result, item.value.ptr())) {                          \
                        throw error_already_set();                                      \
                    }                                                                   \
                }                                                                       \
            }                                                                           \
            return reinterpret_steal<cls>(result);                                      \
        } catch (...) {                                                                 \
            Py_DECREF(result);                                                          \
            throw;                                                                      \
        }                                                                               \
    }                                                                                   \
                                                                                        \
        /*    OPERATORS    */                                                           \
                                                                                        \
    /* Equivalent to Python `key in set`. */                                            \
    template <typename T>                                                               \
    inline bool contains(T&& key) const {                                               \
        int result = PySet_Contains(                                                    \
            this->ptr(),                                                                \
            detail::object_or_cast(std::forward<T>(key)).ptr()                          \
        );                                                                              \
        if (result == -1) {                                                             \
            throw error_already_set();                                                  \
        }                                                                               \
        return result;                                                                  \
    }                                                                                   \
                                                                                        \
    using impl::Ops<cls>::operator<;                                                    \
    using impl::Ops<cls>::operator<=;                                                   \
    using impl::Ops<cls>::operator==;                                                   \
    using impl::Ops<cls>::operator!=;                                                   \
    using impl::Ops<cls>::operator>=;                                                   \
    using impl::Ops<cls>::operator>;                                                    \
                                                                                        \
    template <typename T>                                                               \
    inline cls operator|(T&& other) const {                                             \
        return union_(std::forward<T>(other));                                          \
    }                                                                                   \
                                                                                        \
    template <typename T>                                                               \
    inline cls operator|(const std::initializer_list<T>& other) const {                 \
        return union_(other);                                                           \
    }                                                                                   \
                                                                                        \
    inline cls operator|(const std::initializer_list<impl::Initializer>& other) const { \
        return union_(other);                                                           \
    }                                                                                   \
                                                                                        \
    template <typename T>                                                               \
    inline cls operator&(T&& other) const {                                             \
        return intersection(std::forward<T>(other));                                    \
    }                                                                                   \
                                                                                        \
    template <typename T>                                                               \
    inline cls operator&(const std::initializer_list<T>& other) const {                 \
        return intersection(other);                                                     \
    }                                                                                   \
                                                                                        \
    inline cls operator&(const std::initializer_list<impl::Initializer>& other) const { \
        return intersection(other);                                                     \
    }                                                                                   \
                                                                                        \
    template <typename T>                                                               \
    inline cls operator-(T&& other) const {                                             \
        return difference(std::forward<T>(other));                                      \
    }                                                                                   \
                                                                                        \
    template <typename T>                                                               \
    inline cls operator-(const std::initializer_list<T>& other) const {                 \
        return difference(other);                                                       \
    }                                                                                   \
                                                                                        \
    inline cls operator-(const std::initializer_list<impl::Initializer>& other) const { \
        return difference(other);                                                       \
    }                                                                                   \
                                                                                        \
    template <typename T>                                                               \
    inline cls operator^(T&& other) const {                                             \
        return symmetric_difference(std::forward<T>(other));                            \
    }                                                                                   \
                                                                                        \
    template <typename T>                                                               \
    inline cls operator^(const std::initializer_list<T>& other) const {                 \
        return symmetric_difference(other);                                             \
    }                                                                                   \
                                                                                        \
    inline cls operator^(const std::initializer_list<impl::Initializer>& other) const { \
        return symmetric_difference(other);                                             \
    }                                                                                   \
                                                                                        \

}  // namespace impl


/* Wrapper around pybind11::frozenset that allows it to be directly initialized using
std::initializer_list and replicates the Python interface as closely as possible. */
class FrozenSet : public Object, public impl::Ops<FrozenSet> {
    ANYSET_INTERFACE(FrozenSet);

    template <typename T>
    inline FrozenSet& operator|=(T&& other) {
        *this = union_(std::forward<T>(other));
        return *this;
    }

    template <typename T>
    inline FrozenSet& operator|=(const std::initializer_list<T>& other) {
        *this = union_(other);
        return *this;
    }

    inline FrozenSet& operator|=(const std::initializer_list<impl::Initializer>& other) {
        *this = union_(other);
        return *this;
    }

    template <typename T>
    inline FrozenSet& operator&=(T&& other) {
        *this = intersection(std::forward<T>(other));
        return *this;
    }

    template <typename T>
    inline FrozenSet& operator&=(const std::initializer_list<T>& other) {
        *this = intersection(other);
        return *this;
    }

    inline FrozenSet& operator&=(const std::initializer_list<impl::Initializer>& other) {
        *this = intersection(other);
        return *this;
    }

    template <typename T>
    inline FrozenSet& operator-=(T&& other) {
        *this = difference(std::forward<T>(other));
        return *this;
    }

    template <typename T>
    inline FrozenSet& operator-=(const std::initializer_list<T>& other) {
        *this = difference(other);
        return *this;
    }

    inline FrozenSet& operator-=(const std::initializer_list<impl::Initializer>& other) {
        *this = difference(other);
        return *this;
    }

    template <typename T>
    inline FrozenSet& operator^=(T&& other) {
        *this = symmetric_difference(std::forward<T>(other));
        return *this;
    }

    template <typename T>
    inline FrozenSet& operator^=(const std::initializer_list<T>& other) {
        *this = symmetric_difference(other);
        return *this;
    }

    inline FrozenSet& operator^=(const std::initializer_list<impl::Initializer>& other) {
        *this = symmetric_difference(other);
        return *this;
    }

};


/* Wrapper around pybind11::set that allows it to be directly initialized using
std::initializer_list and replicates the Python interface as closely as possible. */
class Set : public Object, public impl::Ops<Set> {
    ANYSET_INTERFACE(Set);

    ////////////////////////////////
    ////    PYTHON INTERFACE    ////
    ////////////////////////////////

    /* Equivalent to Python `set.add(key)`. */
    template <typename T>
    inline void add(T&& key) {
        if (PySet_Add(
            this->ptr(),
            detail::object_or_cast(std::forward<T>(key)).ptr()
        )) {
            throw error_already_set();
        }
    }

    /* Equivalent to Python `set.remove(key)`. */
    template <typename T>
    inline void remove(T&& key) {
        Object obj = cast<Object>(std::forward<T>(key));
        int result = PySet_Discard(this->ptr(), obj.ptr());
        if (result == -1) {
            throw error_already_set();
        } else if (result == 0) {
            throw KeyError(repr(obj));
        }
    }

    /* Equivalent to Python `set.discard(key)`. */
    template <typename T>
    inline void discard(T&& key) {
        if (PySet_Discard(
            this->ptr(),
            detail::object_or_cast(std::forward<T>(key)).ptr()
        ) == -1) {
            throw error_already_set();
        }
    }

    /* Equivalent to Python `set.pop()`. */
    inline Object pop() {
        PyObject* result = PySet_Pop(this->ptr());
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Object>(result);
    }

    /* Equivalent to Python `set.clear()`. */
    inline void clear() {
        if (PySet_Clear(this->ptr())) {
            throw error_already_set();
        }
    }

    /* Equivalent to Python `set.update(*others)`. */
    template <typename... Args>
    inline void update(Args&&... others) {
        this->attr("update")(
            detail::object_or_cast(std::forward<Args>(others))...
        );
    }

    /* Equivalent to Python `set.update(<braced initializer list>)`. */
    template <typename T>
    inline void update(const std::initializer_list<T>& other) {
        for (const T& item : other) {
            add(item);
        }
    }

    /* Equivalent to Python `set.update(<braced initializer list>)`. */
    inline void update(const std::initializer_list<impl::Initializer>& other) {
        for (const impl::Initializer& item : other) {
            add(item.value);
        }
    }

    /* Equivalent to Python `set.intersection_update(*others)`. */
    template <typename... Args>
    inline void intersection_update(Args&&... others) {
        this->attr("intersection_update")(
            detail::object_or_cast(std::forward<Args>(others))...
        );
    }

    /* Equivalent to Python `set.intersection_update(<braced initializer list>)`. */
    template <typename T>
    inline void intersection_update(const std::initializer_list<T>& other) {
        this->attr("intersection_update")(Set(other));
    }

    /* Equivalent to Python `set.intersection_update(<braced initializer list>)`. */
    inline void intersection_update(const std::initializer_list<impl::Initializer>& other) {
        this->attr("intersection_update")(Set(other));
    }

    /* Equivalent to Python `set.difference_update(*others)`. */
    template <typename... Args>
    inline void difference_update(Args&&... others) {
        this->attr("difference_update")(
            detail::object_or_cast(std::forward<Args>(others))...
        );
    }

    /* Equivalent to Python `set.difference_update(<braced initializer list>)`. */
    template <typename T>
    inline void difference_update(const std::initializer_list<T>& other) {
        for (const T& item : other) {
            discard(item);
        }
    }

    /* Equivalent to Python `set.difference_update(<braced initializer list>)`. */
    inline void difference_update(const std::initializer_list<impl::Initializer>& other) {
        for (const impl::Initializer& item : other) {
            discard(item.value);
        }
    }

    /* Equivalent to Python `set.symmetric_difference_update(other)`. */
    template <typename T>
    inline void symmetric_difference_update(T&& other) {
        this->attr("symmetric_difference_update")(
            detail::object_or_cast(std::forward<T>(other))
        );
    }

    /* Equivalent to Python `set.symmetric_difference_update(<braced initializer list>)`. */
    template <typename T>
    inline void symmetric_difference_update(const std::initializer_list<T>& other) {
        for (const T& item : other) {
            if (contains(item)) {
                discard(item);
            } else {
                add(item);
            }
        }
    }

    /* Equivalent to Python `set.symmetric_difference_update(<braced initializer list>)`. */
    inline void symmetric_difference_update(const std::initializer_list<impl::Initializer>& other) {
        for (const impl::Initializer& item : other) {
            if (contains(item.value)) {
                discard(item.value);
            } else {
                add(item.value);
            }
        }
    }

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    /* Equivalent to Python `set |= other`. */
    template <typename T>
    inline Set& operator|=(T&& other) {
        update(std::forward<T>(other));
        return *this;
    }

    /* Equivalent to Python `set |= <braced initializer list>`. */
    template <typename T>
    inline Set& operator|=(const std::initializer_list<T>& other) {
        update(other);
        return *this;
    }

    /* Equivalent to Python `set |= <braced initializer list>`. */
    inline Set& operator|=(const std::initializer_list<impl::Initializer>& other) {
        update(other);
        return *this;
    }

    /* Equivalent to Python `set &= other`. */
    template <typename T>
    inline Set& operator&=(T&& other) {
        intersection_update(std::forward<T>(other));
        return *this;
    }

    /* Equivalent to Python `set &= <braced initializer list>`. */
    template <typename T>
    inline Set& operator&=(const std::initializer_list<T>& other) {
        intersection_update(other);
        return *this;
    }

    /* Equivalent to Python `set &= <braced initializer list>`. */
    inline Set& operator&=(const std::initializer_list<impl::Initializer>& other) {
        intersection_update(other);
        return *this;
    }

    /* Equivalent to Python `set -= other`. */
    template <typename T>
    inline Set& operator-=(T&& other) {
        difference_update(std::forward<T>(other));
        return *this;
    }

    /* Equivalent to Python `set -= <braced initializer list>`. */
    template <typename T>
    inline Set& operator-=(const std::initializer_list<T>& other) {
        difference_update(other);
        return *this;
    }

    /* Equivalent to Python `set -= <braced initializer list>`. */
    inline Set& operator-=(const std::initializer_list<impl::Initializer>& other) {
        difference_update(other);
        return *this;
    }

    /* Equivalent to Python `set ^= other`. */
    template <typename T>
    inline Set& operator^=(T&& other) {
        symmetric_difference_update(std::forward<T>(other));
        return *this;
    }

    /* Equivalent to Python `set ^= <braced initializer list>`. */
    template <typename T>
    inline Set& operator^=(const std::initializer_list<T>& other) {
        symmetric_difference_update(other);
        return *this;
    }

    /* Equivalent to Python `set ^= <braced initializer list>`. */
    inline Set& operator^=(const std::initializer_list<impl::Initializer>& other) {
        symmetric_difference_update(other);
        return *this;
    }

};


#undef ANYSET_INTERFACE


}  // namespace python
}  // namespace bertrand


BERTRAND_STD_HASH(bertrand::py::FrozenSet)


#endif  // BERTRAND_PYTHON_SET_H
