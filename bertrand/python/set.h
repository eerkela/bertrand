#if !defined(BERTRAND_PYTHON_INCLUDED) && !defined(LINTER)
#error "This file should not be included directly.  Please include <bertrand/python.h> instead."
#endif

#ifndef BERTRAND_PYTHON_SET_H
#define BERTRAND_PYTHON_SET_H

#include "common.h"


namespace bertrand {
namespace py {


namespace impl {

    template <typename Derived>
    class ISet : public Object {
        using Base = Object;

        inline Derived* self() { return static_cast<Derived*>(this); }
        inline const Derived* self() const { return static_cast<const Derived*>(this); }

    protected:

        template <typename T>
        inline static void insert_from_tuple(PyObject* result, const T& item) {
            if (PySet_Add(result, detail::object_or_cast(item).ptr())) {
                throw error_already_set();
            }
        }

        template <typename... Args, size_t... N>
        inline static void unpack_tuple(
            PyObject* result,
            const std::tuple<Args...>& tuple,
            std::index_sequence<N...>
        ) {
            (insert_from_tuple(result, std::get<N>(tuple)), ...);
        }

    public:
        using Base::Base;

        /////////////////////////////
        ////    C++ INTERFACE    ////
        /////////////////////////////

        /* Implicitly convert a py::FrozenSet into a C++ set or unordered_set. */
        template <typename T> requires (!impl::python_like<T> && impl::anyset_like<T>)
        inline operator T() const {
            T result;
            for (auto&& item : *self()) {
                result.insert(item.template cast<typename T::value_type>());
            }
        }
                                        
        ////////////////////////////////
        ////    PYTHON INTERFACE    ////
        ////////////////////////////////

        /* Equivalent to Python `set.copy()`. */
        inline Derived copy() const {
            PyObject* result = self()->alloc(self()->ptr());
            if (result == nullptr) {
                throw error_already_set();
            }
            return reinterpret_steal<Derived>(result);
        }

        /* Equivalent to Python `set.isdisjoint(other)`. */
        template <impl::is_iterable T>
        inline bool isdisjoint(const T& other) const {
            if constexpr (impl::python_like<T>) {
                static const pybind11::str method = "isdisjoint";
                return static_cast<bool>(self()->attr(method)(other));
            } else {
                for (auto&& item : other) {
                    if (contains(std::forward<decltype(item)>(item))) {
                        return false;
                    }
                }
                return true;
            }
        }

        /* Equivalent to Python `set.isdisjoint(other)`, where other is given as a
        braced initializer list. */
        inline bool isdisjoint(const std::initializer_list<impl::HashInitializer>& other) const {
            for (const impl::HashInitializer& item : other) {
                if (contains(item.first)) {
                    return false;
                }
            }
            return true;
        }

        /* Equivalent to Python `set.issubset(other)`. */
        template <impl::is_iterable T>
        inline bool issubset(const T& other) const {
            static const pybind11::str method = "issubset";
            return static_cast<bool>(self()->attr(method)(
                detail::object_or_cast(other)
            ));
        }

        /* Equivalent to Python `set.issubset(other)`, where other is given as a
        braced initializer list. */
        inline bool issubset(const std::initializer_list<impl::HashInitializer>& other) const {
            static const pybind11::str method = "issubset";
            return static_cast<bool>(self()->attr(method)(Derived(other)));
        }

        /* Equivalent to Python `set.issuperset(other)`. */
        template <impl::is_iterable T>
        inline bool issuperset(const T& other) const {
            if constexpr (impl::python_like<T>) {
                static const pybind11::str method = "issuperset";
                return static_cast<bool>(self()->attr(method)(other));
            } else {
                for (auto&& item : other) {
                    if (!contains(std::forward<decltype(item)>(item))) {
                        return false;
                    }
                }
                return true;
            }
        }

        /* Equivalent to Python `set.issuperset(other)`, where other is given as a
        braced initializer list. */
        inline bool issuperset(
            const std::initializer_list<impl::HashInitializer>& other
        ) const {
            for (const impl::HashInitializer& item : other) {
                if (!contains(item.first)) {
                    return false;
                }
            }
            return true;
        }

        /* Equivalent to Python `set.union(*others)`. */
        template <impl::is_iterable... Args>
        inline Derived union_(const Args&... others) const {
            static const pybind11::str method = "union";
            return self()->attr(method)(
                detail::object_or_cast(std::forward<Args>(others))...
            );
        }

        /* Equivalent to Python `set.union(other)`, where other is given as a braced
        initializer list. */
        inline Derived union_(const std::initializer_list<impl::HashInitializer>& other) const {
            PyObject* result = self()->alloc(self()->ptr());
            if (result == nullptr) {
                throw error_already_set();
            }
            try {
                for (const impl::HashInitializer& item : other) {
                    if (PySet_Add(result, item.first.ptr())) {
                        throw error_already_set();
                    }
                }
                return reinterpret_steal<Derived>(result);
            } catch (...) {
                Py_DECREF(result);
                throw;
            }
        }

        /* Equivalent to Python `set.intersection(other)`. */
        template <impl::is_iterable... Args>
        inline Derived intersection(const Args&... others) const {
            static const pybind11::str method = "intersection";
            return self()->attr(method)(
                detail::object_or_cast(std::forward<Args>(others))...
            );
        }

        /* Equivalent to Python `set.intersection(other)`, where other is given as a
        braced initializer list. */
        inline Derived intersection(
            const std::initializer_list<impl::HashInitializer>& other
        ) const {
            PyObject* result = self()->alloc(nullptr);
            if (result == nullptr) {
                throw error_already_set();
            }
            try {
                for (const impl::HashInitializer& item : other) {
                    if (contains(item.first)) {
                        if (PySet_Add(result, item.first.ptr())) {
                            throw error_already_set();
                        }
                    }
                }
                return reinterpret_steal<Derived>(result);
            } catch (...) {
                Py_DECREF(result);
                throw;
            }
        }

        /* Equivalent to Python `set.difference(other)`. */
        template <impl::is_iterable... Args>
        inline Derived difference(const Args&... others) const {
            static const pybind11::str method = "difference";
            return self()->attr(method)(
                detail::object_or_cast(std::forward<Args>(others))...
            );
        }

        /* Equivalent to Python `set.difference(other)`, where other is given as a
        braced initializer list. */
        inline Derived difference(
            const std::initializer_list<impl::HashInitializer>& other
        ) const {
            PyObject* result = self()->alloc(self()->ptr());
            if (result == nullptr) {
                throw error_already_set();
            }
            try {
                for (const impl::HashInitializer& item : other) {
                    if (PySet_Discard(result, item.first.ptr()) == -1) {
                        throw error_already_set();
                    }
                }
                return reinterpret_steal<Derived>(result);
            } catch (...) {
                Py_DECREF(result);
                throw;
            }
        }

        /* Equivalent to Python `set.symmetric_difference(other)`. */
        template <impl::is_iterable T>
        inline Derived symmetric_difference(const T& other) const {
            static const pybind11::str method = "symmetric_difference";
            return self()->attr(method)(detail::object_or_cast(other));
        }

        /* Equivalent to Python `set.symmetric_difference(other)`, where other is given
        as a braced initializer list. */
        inline Derived symmetric_difference(
            const std::initializer_list<impl::HashInitializer>& other
        ) const {
            PyObject* result = self()->alloc(nullptr);
            if (result == nullptr) {
                throw error_already_set();
            }
            try {
                for (const impl::HashInitializer& item : other) {
                    if (contains(item.first)) {
                        if (PySet_Discard(result, item.first.ptr()) == -1) {
                            throw error_already_set();
                        }
                    } else {
                        if (PySet_Add(result, item.first.ptr())) {
                            throw error_already_set();
                        }
                    }
                }
                return reinterpret_steal<Derived>(result);
            } catch (...) {
                Py_DECREF(result);
                throw;
            }
        }

        /////////////////////////
        ////    OPERATORS    ////
        /////////////////////////

        using Base::operator|;
        using Base::operator&;
        using Base::operator-;
        using Base::operator^;

        inline Derived operator|(
            const std::initializer_list<impl::HashInitializer>& other
        ) const {
            return union_(other);
        }

        inline Derived operator&(
            const std::initializer_list<impl::HashInitializer>& other
        ) const {
            return intersection(other);
        }

        inline Derived operator-(
            const std::initializer_list<impl::HashInitializer>& other
        ) const {
            return difference(other);
        }

        inline Derived operator^(
            const std::initializer_list<impl::HashInitializer>& other
        ) const {
            return symmetric_difference(other);
        }

    protected:

        template <typename Return, typename T>
        inline static size_t operator_len(const T& self) {
            return static_cast<size_t>(PySet_GET_SIZE(self.ptr()));
        }

        template <typename Return, typename L, typename R>
        inline static bool operator_contains(const L& self, const R& key) {
            int result = PySet_Contains(
                self.ptr(),
                detail::object_or_cast(key).ptr()
            );
            if (result == -1) {
                throw error_already_set();
            }
            return result;
        }

    };

template <>
struct __len__<FrozenSet>                                       : Returns<size_t> {};
template <>
struct __iter__<FrozenSet>                                      : Returns<Object> {};
template <>
struct __reversed__<FrozenSet>                                  : Returns<Object> {};
template <is_hashable T>
struct __contains__<FrozenSet, T>                               : Returns<bool> {};
template <>
struct __lt__<FrozenSet, Object>                                : Returns<bool> {};
template <anyset_like T>
struct __lt__<FrozenSet, T>                                     : Returns<bool> {};
template <>
struct __le__<FrozenSet, Object>                                : Returns<bool> {};
template <anyset_like T>
struct __le__<FrozenSet, T>                                     : Returns<bool> {};
template <>
struct __ge__<FrozenSet, Object>                                : Returns<bool> {};
template <anyset_like T>
struct __ge__<FrozenSet, T>                                     : Returns<bool> {};
template <>
struct __gt__<FrozenSet, Object>                                : Returns<bool> {};
template <anyset_like T>
struct __gt__<FrozenSet, T>                                     : Returns<bool> {};
template <>
struct __or__<FrozenSet, Object>                                : Returns<FrozenSet> {};
template <anyset_like T>
struct __or__<FrozenSet, T>                                     : Returns<FrozenSet> {};
template <>
struct __and__<FrozenSet, Object>                               : Returns<FrozenSet> {};
template <anyset_like T>
struct __and__<FrozenSet, T>                                    : Returns<FrozenSet> {};
template <>
struct __sub__<FrozenSet, Object>                               : Returns<FrozenSet> {};
template <anyset_like T>
struct __sub__<FrozenSet, T>                                    : Returns<FrozenSet> {};
template <>
struct __xor__<FrozenSet, Object>                               : Returns<FrozenSet> {};
template <anyset_like T>
struct __xor__<FrozenSet, T>                                    : Returns<FrozenSet> {};
template <>
struct __ior__<FrozenSet, Object>                               : Returns<FrozenSet&> {};
template <anyset_like T>
struct __ior__<FrozenSet, T>                                    : Returns<FrozenSet&> {};
template <>
struct __iand__<FrozenSet, Object>                              : Returns<FrozenSet&> {};
template <anyset_like T>
struct __iand__<FrozenSet, T>                                   : Returns<FrozenSet&> {};
template <>
struct __isub__<FrozenSet, Object>                              : Returns<FrozenSet&> {};
template <anyset_like T>
struct __isub__<FrozenSet, T>                                   : Returns<FrozenSet&> {};
template <>
struct __ixor__<FrozenSet, Object>                              : Returns<FrozenSet&> {};
template <anyset_like T>
struct __ixor__<FrozenSet, T>                                   : Returns<FrozenSet&> {};

template <>
struct __len__<Set>                                             : Returns<size_t> {};
template <>
struct __iter__<Set>                                            : Returns<Object> {};
template <>
struct __reversed__<Set>                                        : Returns<Object> {};
template <is_hashable T>
struct __contains__<Set, T>                                     : Returns<bool> {};
template <>
struct __lt__<Set, Object>                                      : Returns<bool> {};
template <anyset_like T>
struct __lt__<Set, T>                                           : Returns<bool> {};
template <>
struct __le__<Set, Object>                                      : Returns<bool> {};
template <anyset_like T>
struct __le__<Set, T>                                           : Returns<bool> {};
template <>
struct __ge__<Set, Object>                                      : Returns<bool> {};
template <anyset_like T>
struct __ge__<Set, T>                                           : Returns<bool> {};
template <>
struct __gt__<Set, Object>                                      : Returns<bool> {};
template <anyset_like T>
struct __gt__<Set, T>                                           : Returns<bool> {};
template <>
struct __or__<Set, Object>                                      : Returns<Set> {};
template <anyset_like T>
struct __or__<Set, T>                                           : Returns<Set> {};
template <>
struct __and__<Set, Object>                                     : Returns<Set> {};
template <anyset_like T>
struct __and__<Set, T>                                          : Returns<Set> {};
template <>
struct __sub__<Set, Object>                                     : Returns<Set> {};
template <anyset_like T>
struct __sub__<Set, T>                                          : Returns<Set> {};
template <>
struct __xor__<Set, Object>                                     : Returns<Set> {};
template <anyset_like T>
struct __xor__<Set, T>                                          : Returns<Set> {};
template <>
struct __ior__<Set, Object>                                     : Returns<Set&> {};
template <anyset_like T>
struct __ior__<Set, T>                                          : Returns<Set&> {};
template <>
struct __iand__<Set, Object>                                    : Returns<Set&> {};
template <anyset_like T>
struct __iand__<Set, T>                                         : Returns<Set&> {};
template <>
struct __isub__<Set, Object>                                    : Returns<Set&> {};
template <anyset_like T>
struct __isub__<Set, T>                                         : Returns<Set&> {};
template <>
struct __ixor__<Set, Object>                                    : Returns<Set&> {};
template <anyset_like T>
struct __ixor__<Set, T>                                         : Returns<Set&> {};

}  // namespace impl


/* Wrapper around pybind11::frozenset that allows it to be directly initialized using
std::initializer_list and replicates the Python interface as closely as possible. */
class FrozenSet : public impl::ISet<FrozenSet> {
    using Base = impl::ISet<FrozenSet>;
    friend Base;

    /* This helper function is needed for ISet mixin. */
    inline static PyObject* alloc(PyObject* obj) {
        return PyFrozenSet_New(obj);
    }

    template <typename T>
    static constexpr bool py_unpacking_constructor =
        impl::python_like<T> && !impl::frozenset_like<T> && impl::is_iterable<T>;
    template <typename T>
    static constexpr bool cpp_unpacking_constructor =
        !impl::python_like<T> && impl::is_iterable<T>;

public:
    static Type type;

    template <typename T>
    static constexpr bool check() { return impl::frozenset_like<T>; }

    BERTRAND_OBJECT_COMMON(Base, FrozenSet, PyFrozenSet_Check)

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Default constructor.  Initializes to an empty set. */
    FrozenSet() : Base(PyFrozenSet_New(nullptr), stolen_t{}) {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Copy/move constructors. */
    template <typename T> requires (check<T>() && impl::python_like<T>)
    FrozenSet(T&& other) : Base(std::forward<T>(other)) {}

    /* Pack the contents of a braced initializer list into a new Python frozenset. */
    FrozenSet(const std::initializer_list<impl::HashInitializer>& contents) :
        Base(PyFrozenSet_New(nullptr), stolen_t{})
    {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
        try {
            for (const impl::HashInitializer& item : contents) {
                if (PySet_Add(m_ptr, item.first.ptr())) {
                    throw error_already_set();
                }
            }
        } catch (...) {
            Py_DECREF(m_ptr);
            throw;
        }
    }

    /* Explicitly unpack an arbitrary Python container into a new py::FrozenSet. */
    template <typename T> requires (py_unpacking_constructor<T>)
    explicit FrozenSet(const T& contents) :
        Base(PyFrozenSet_New(contents.ptr()), stolen_t{})
    {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Explicitly unpack an arbitrary C++ container into a new py::FrozenSet. */
    template <typename T> requires (cpp_unpacking_constructor<T>)
    explicit FrozenSet(const T& container) {
        m_ptr = PyFrozenSet_New(nullptr);
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
        try {
            for (auto&& item : container) {
                if (PySet_Add(
                    m_ptr,
                    detail::object_or_cast(std::forward<decltype(item)>(item)).ptr())
                ) {
                    throw error_already_set();
                }
            }
        } catch (...) {
            Py_DECREF(m_ptr);
            throw;
        }
    }

    /* Explicitly unpack a std::pair into a py::FrozenSet. */
    template <impl::is_hashable First, impl::is_hashable Second>
    explicit FrozenSet(const std::pair<First, Second>& pair) :
        Base(PyFrozenSet_New(nullptr), stolen_t{})
    {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
        try {
            if (PySet_Add(m_ptr, detail::object_or_cast(pair.first).ptr())) {
                throw error_already_set();
            }
            if (PySet_Add(m_ptr, detail::object_or_cast(pair.second).ptr())) {
                throw error_already_set();
            }
        } catch (...) {
            Py_DECREF(m_ptr);
            throw;
        }
    }

    /* Explicitly unpack a std::tuple into a py::FrozenSet. */
    template <impl::is_hashable... Args>
    explicit FrozenSet(const std::tuple<Args...>& tuple) :
        Base(PyFrozenSet_New(nullptr), stolen_t{})
    {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
        try {
            Base::unpack_tuple(m_ptr, tuple, std::index_sequence_for<Args...>{});
        } catch (...) {
            Py_DECREF(m_ptr);
            throw;
        }
    }

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    inline FrozenSet& operator|=(const std::initializer_list<impl::HashInitializer>& other) {
        *this = union_(other);
        return *this;
    }

    inline FrozenSet& operator&=(const std::initializer_list<impl::HashInitializer>& other) {
        *this = intersection(other);
        return *this;
    }

    inline FrozenSet& operator-=(const std::initializer_list<impl::HashInitializer>& other) {
        *this = difference(other);
        return *this;
    }

    inline FrozenSet& operator^=(const std::initializer_list<impl::HashInitializer>& other) {
        *this = symmetric_difference(other);
        return *this;
    }

};


/* Wrapper around pybind11::set that allows it to be directly initialized using
std::initializer_list and replicates the Python interface as closely as possible. */
class Set : public impl::ISet<Set> {
    using Base = impl::ISet<Set>;
    friend Base;

    /* This helper function is needed for ISet mixin. */
    inline static PyObject* alloc(PyObject* obj) {
        return PySet_New(obj);
    }

    template <typename T>
    static constexpr bool py_unpacking_constructor =
        impl::python_like<T> && !impl::set_like<T> && impl::is_iterable<T>;
    template <typename T>
    static constexpr bool cpp_unpacking_constructor =
        !impl::python_like<T> && impl::is_iterable<T>;

public:
    static Type type;

    template <typename T>
    static constexpr bool check() { return impl::set_like<T>; }

    BERTRAND_OBJECT_COMMON(Base, Set, PySet_Check);

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Default constructor.  Initializes to an empty set. */
    Set() : Base(PySet_New(nullptr), stolen_t{}) {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Copy/move constructors. */
    template <typename T> requires (check<T>() && impl::python_like<T>)
    Set(T&& other) : Base(std::forward<T>(other)) {}

    /* Pack the contents of a braced initializer list into a new Python set. */
    Set(const std::initializer_list<impl::HashInitializer>& contents) :
        Base(PySet_New(nullptr), stolen_t{})
    {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
        try {
            for (const impl::HashInitializer& item : contents) {
                if (PySet_Add(m_ptr, item.first.ptr())) {
                    throw error_already_set();
                }
            }
        } catch (...) {
            Py_DECREF(m_ptr);
            throw;
        }
    }

    /* Explicitly unpack an arbitrary Python container into a new py::Set. */
    template <typename T> requires (py_unpacking_constructor<T>)
    explicit Set(const T& contents) :
        Base(PySet_New(contents.ptr()), stolen_t{})
    {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Explicitly unpack an arbitrary C++ container into a new py::Set. */
    template <typename T> requires (cpp_unpacking_constructor<T>)
    explicit Set(const T& contents) : Base(PySet_New(nullptr), stolen_t{}) {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
        try {
            for (auto&& item : contents) {
                if (PySet_Add(
                    m_ptr,
                    detail::object_or_cast(std::forward<decltype(item)>(item)).ptr())
                ) {
                    throw error_already_set();
                }
            }
        } catch (...) {
            Py_DECREF(m_ptr);
            throw;
        }
    }

    /* Explicitly unpack a std::pair into a py::Set. */
    template <impl::is_hashable First, impl::is_hashable Second>
    explicit Set(const std::pair<First, Second>& pair) :
        Base(PySet_New(nullptr), stolen_t{})
    {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
        try {
            if (PySet_Add(m_ptr, detail::object_or_cast(pair.first).ptr())) {
                throw error_already_set();
            }
            if (PySet_Add(m_ptr, detail::object_or_cast(pair.second).ptr())) {
                throw error_already_set();
            }
        } catch (...) {
            Py_DECREF(m_ptr);
            throw;
        }
    }

    /* Explicitly unpack a std::tuple into a py::Set. */
    template <impl::is_hashable... Args>
    explicit Set(const std::tuple<Args...>& tuple) :
        Base(PySet_New(nullptr), stolen_t{})
    {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
        try {
            Base::unpack_tuple(m_ptr, tuple, std::index_sequence_for<Args...>{});
        } catch (...) {
            Py_DECREF(m_ptr);
            throw;
        }
    }

    ////////////////////////////////
    ////    PYTHON INTERFACE    ////
    ////////////////////////////////

    /* Equivalent to Python `set.add(key)`. */
    template <impl::is_hashable T>
    inline void add(const T& key) {
        if (PySet_Add(this->ptr(), detail::object_or_cast(key).ptr())) {
            throw error_already_set();
        }
    }

    /* Equivalent to Python `set.remove(key)`. */
    template <impl::is_hashable T>
    inline void remove(const T& key) {
        Object obj = detail::object_or_cast(key);
        int result = PySet_Discard(this->ptr(), obj.ptr());
        if (result == -1) {
            throw error_already_set();
        } else if (result == 0) {
            throw KeyError(repr(obj));
        }
    }

    /* Equivalent to Python `set.discard(key)`. */
    template <impl::is_hashable T>
    inline void discard(const T& key) {
        if (PySet_Discard(this->ptr(), detail::object_or_cast(key).ptr()) == -1) {
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
    template <impl::is_iterable... Args>
    inline void update(const Args&... others) {
        static const pybind11::str method = "update";
        attr(method)(detail::object_or_cast(std::forward<Args>(others))...);
    }

    /* Equivalent to Python `set.update(<braced initializer list>)`. */
    inline void update(const std::initializer_list<impl::HashInitializer>& other) {
        for (const impl::HashInitializer& item : other) {
            add(item.first);
        }
    }

    /* Equivalent to Python `set.intersection_update(*others)`. */
    template <impl::is_iterable... Args>
    inline void intersection_update(const Args&... others) {
        static const pybind11::str method = "intersection_update";
        attr(method)(detail::object_or_cast(std::forward<Args>(others))...);
    }

    /* Equivalent to Python `set.intersection_update(<braced initializer list>)`. */
    inline void intersection_update(
        const std::initializer_list<impl::HashInitializer>& other
    ) {
        static const pybind11::str method = "intersection_update";
        attr(method)(Set(other));
    }

    /* Equivalent to Python `set.difference_update(*others)`. */
    template <impl::is_iterable... Args>
    inline void difference_update(const Args&... others) {
        static const pybind11::str method = "difference_update";
        attr(method)(detail::object_or_cast(std::forward<Args>(others))...);
    }

    /* Equivalent to Python `set.difference_update(<braced initializer list>)`. */
    inline void difference_update(
        const std::initializer_list<impl::HashInitializer>& other
    ) {
        for (const impl::HashInitializer& item : other) {
            discard(item.first);
        }
    }

    /* Equivalent to Python `set.symmetric_difference_update(other)`. */
    template <impl::is_iterable T>
    inline void symmetric_difference_update(const T& other) {
        static const pybind11::str method = "symmetric_difference_update";
        attr(method)(detail::object_or_cast(other));
    }

    /* Equivalent to Python `set.symmetric_difference_update(<braced initializer list>)`. */
    inline void symmetric_difference_update(
        const std::initializer_list<impl::HashInitializer>& other
    ) {
        for (const impl::HashInitializer& item : other) {
            if (contains(item.first)) {
                discard(item.first);
            } else {
                add(item.first);
            }
        }
    }

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    /* Equivalent to Python `set |= <braced initializer list>`. */
    inline Set& operator|=(const std::initializer_list<impl::HashInitializer>& other) {
        update(other);
        return *this;
    }

    /* Equivalent to Python `set &= <braced initializer list>`. */
    inline Set& operator&=(const std::initializer_list<impl::HashInitializer>& other) {
        intersection_update(other);
        return *this;
    }

    /* Equivalent to Python `set -= <braced initializer list>`. */
    inline Set& operator-=(const std::initializer_list<impl::HashInitializer>& other) {
        difference_update(other);
        return *this;
    }

    /* Equivalent to Python `set ^= <braced initializer list>`. */
    inline Set& operator^=(const std::initializer_list<impl::HashInitializer>& other) {
        symmetric_difference_update(other);
        return *this;
    }

};


}  // namespace py
}  // namespace bertrand


BERTRAND_STD_HASH(bertrand::py::FrozenSet)


#endif  // BERTRAND_PYTHON_SET_H
