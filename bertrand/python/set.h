#if !defined(BERTRAND_PYTHON_INCLUDED) && !defined(LINTER)
#error "This file should not be included directly.  Please include <bertrand/python.h> instead."
#endif

#ifndef BERTRAND_PYTHON_SET_H
#define BERTRAND_PYTHON_SET_H

#include "common.h"


namespace bertrand {
namespace py {


namespace impl {

    struct ISetTag {};

    template <typename Derived>
    class ISet : public Object, public ISetTag {
        using Base = Object;

        inline Derived* self() { return static_cast<Derived*>(this); }
        inline const Derived* self() const { return static_cast<const Derived*>(this); }

    protected:

        template <typename T>
        inline static void insert_from_tuple(PyObject* result, const T& item) {
            if (PySet_Add(result, detail::object_or_cast(item).ptr())) {
                Exception::from_python();
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
                Exception::from_python();
            }
            return reinterpret_steal<Derived>(result);
        }

        /* Equivalent to Python `set.isdisjoint(other)`. */
        template <impl::is_iterable T>
        inline bool isdisjoint(const T& other) const;

        /* Equivalent to Python `set.isdisjoint(other)`, where other is given as a
        braced initializer list. */
        inline bool isdisjoint(
            const std::initializer_list<impl::HashInitializer>& other
        ) const {
            for (const impl::HashInitializer& item : other) {
                if (contains(item.first)) {
                    return false;
                }
            }
            return true;
        }

        /* Equivalent to Python `set.issubset(other)`. */
        template <impl::is_iterable T>
        inline bool issubset(const T& other) const;

        /* Equivalent to Python `set.issubset(other)`, where other is given as a
        braced initializer list. */
        inline bool issubset(
            const std::initializer_list<impl::HashInitializer>& other
        ) const;

        /* Equivalent to Python `set.issuperset(other)`. */
        template <impl::is_iterable T>
        inline bool issuperset(const T& other) const;

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
        inline Derived union_(const Args&... others) const;

        /* Equivalent to Python `set.union(other)`, where other is given as a braced
        initializer list. */
        inline Derived union_(const std::initializer_list<impl::HashInitializer>& other) const {
            PyObject* result = self()->alloc(self()->ptr());
            if (result == nullptr) {
                Exception::from_python();
            }
            try {
                for (const impl::HashInitializer& item : other) {
                    if (PySet_Add(result, item.first.ptr())) {
                        Exception::from_python();
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
        inline Derived intersection(const Args&... others) const;

        /* Equivalent to Python `set.intersection(other)`, where other is given as a
        braced initializer list. */
        inline Derived intersection(
            const std::initializer_list<impl::HashInitializer>& other
        ) const {
            PyObject* result = self()->alloc(nullptr);
            if (result == nullptr) {
                Exception::from_python();
            }
            try {
                for (const impl::HashInitializer& item : other) {
                    if (contains(item.first)) {
                        if (PySet_Add(result, item.first.ptr())) {
                            Exception::from_python();
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
        inline Derived difference(const Args&... others) const;

        /* Equivalent to Python `set.difference(other)`, where other is given as a
        braced initializer list. */
        inline Derived difference(
            const std::initializer_list<impl::HashInitializer>& other
        ) const {
            PyObject* result = self()->alloc(self()->ptr());
            if (result == nullptr) {
                Exception::from_python();
            }
            try {
                for (const impl::HashInitializer& item : other) {
                    if (PySet_Discard(result, item.first.ptr()) == -1) {
                        Exception::from_python();
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
        inline Derived symmetric_difference(const T& other) const;

        /* Equivalent to Python `set.symmetric_difference(other)`, where other is given
        as a braced initializer list. */
        inline Derived symmetric_difference(
            const std::initializer_list<impl::HashInitializer>& other
        ) const {
            PyObject* result = self()->alloc(nullptr);
            if (result == nullptr) {
                Exception::from_python();
            }
            try {
                for (const impl::HashInitializer& item : other) {
                    if (contains(item.first)) {
                        if (PySet_Discard(result, item.first.ptr()) == -1) {
                            Exception::from_python();
                        }
                    } else {
                        if (PySet_Add(result, item.first.ptr())) {
                            Exception::from_python();
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

        inline friend Derived operator|(
            const ISet& self,
            const std::initializer_list<impl::HashInitializer>& other
        ) {
            return self.union_(other);
        }

        inline friend Derived operator&(
            const ISet& self,
            const std::initializer_list<impl::HashInitializer>& other
        ) {
            return self.intersection(other);
        }

        inline friend Derived operator-(
            const ISet& self,
            const std::initializer_list<impl::HashInitializer>& other
        ) {
            return self.difference(other);
        }

        inline friend Derived operator^(
            const ISet& self,
            const std::initializer_list<impl::HashInitializer>& other
        ) {
            return self.symmetric_difference(other);
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
                Exception::from_python();
            }
            return result;
        }

    };

}


template <std::derived_from<impl::ISetTag> T>
struct __getattr__<T, "copy">                                   : Returns<Function> {};
template <std::derived_from<impl::ISetTag> T>
struct __getattr__<T, "isdisjoint">                             : Returns<Function> {};
template <std::derived_from<impl::ISetTag> T>
struct __getattr__<T, "issubset">                               : Returns<Function> {};
template <std::derived_from<impl::ISetTag> T>
struct __getattr__<T, "issuperset">                             : Returns<Function> {};
template <std::derived_from<impl::ISetTag> T>
struct __getattr__<T, "union">                                  : Returns<Function> {};
template <std::derived_from<impl::ISetTag> T>
struct __getattr__<T, "intersection">                           : Returns<Function> {};
template <std::derived_from<impl::ISetTag> T>
struct __getattr__<T, "difference">                             : Returns<Function> {};
template <std::derived_from<impl::ISetTag> T>
struct __getattr__<T, "symmetric_difference">                   : Returns<Function> {};

template <std::derived_from<impl::ISetTag> T>
struct __len__<T>                                               : Returns<size_t> {};
template <>
struct __hash__<FrozenSet>                                      : Returns<size_t> {};
template <std::derived_from<impl::ISetTag> T>
struct __iter__<T>                                              : Returns<Object> {};
template <std::derived_from<impl::ISetTag> T>
struct __reversed__<T>                                          : Returns<Object> {};
template <std::derived_from<impl::ISetTag> T, impl::is_hashable Key>
struct __contains__<T, Key>                                     : Returns<bool> {};
template <std::derived_from<impl::ISetTag> L>
struct __lt__<L, Object>                                        : Returns<bool> {};
template <std::derived_from<impl::ISetTag> L, impl::anyset_like R>
struct __lt__<L, R>                                             : Returns<bool> {};
template <std::derived_from<impl::ISetTag> L>
struct __le__<L, Object>                                        : Returns<bool> {};
template <std::derived_from<impl::ISetTag> L, impl::anyset_like R>
struct __le__<L, R>                                             : Returns<bool> {};
template <std::derived_from<impl::ISetTag> L>
struct __ge__<L, Object>                                        : Returns<bool> {};
template <std::derived_from<impl::ISetTag> L, impl::anyset_like R>
struct __ge__<L, R>                                             : Returns<bool> {};
template <std::derived_from<impl::ISetTag> L>
struct __gt__<L, Object>                                        : Returns<bool> {};
template <std::derived_from<impl::ISetTag> L, impl::anyset_like R>
struct __gt__<L, R>                                             : Returns<bool> {};
template <std::derived_from<impl::ISetTag> L>
struct __or__<L, Object>                                        : Returns<L> {};
template <std::derived_from<impl::ISetTag> L, impl::anyset_like R>
struct __or__<L, R>                                             : Returns<L> {};
template <std::derived_from<impl::ISetTag> L>
struct __ior__<L, Object>                                       : Returns<L&> {};
template <std::derived_from<impl::ISetTag> L, impl::anyset_like R>
struct __ior__<L, R>                                            : Returns<L&> {};
template <std::derived_from<impl::ISetTag> L>
struct __and__<L, Object>                                       : Returns<L> {};
template <std::derived_from<impl::ISetTag> L, impl::anyset_like R>
struct __and__<L, R>                                            : Returns<L> {};
template <std::derived_from<impl::ISetTag> L>
struct __iand__<L, Object>                                      : Returns<L&> {};
template <std::derived_from<impl::ISetTag> L, impl::anyset_like R>
struct __iand__<L, R>                                           : Returns<L&> {};
template <std::derived_from<impl::ISetTag> L>
struct __sub__<L, Object>                                       : Returns<L> {};
template <std::derived_from<impl::ISetTag> L, impl::anyset_like R>
struct __sub__<L, R>                                            : Returns<L> {};
template <std::derived_from<impl::ISetTag> L>
struct __isub__<L, Object>                                      : Returns<L&> {};
template <std::derived_from<impl::ISetTag> L, impl::anyset_like R>
struct __isub__<L, R>                                           : Returns<L&> {};
template <std::derived_from<impl::ISetTag> L>
struct __xor__<L, Object>                                       : Returns<L> {};
template <std::derived_from<impl::ISetTag> L, impl::anyset_like R>
struct __xor__<L, R>                                            : Returns<L> {};
template <std::derived_from<impl::ISetTag> L>
struct __ixor__<L, Object>                                      : Returns<L&> {};
template <std::derived_from<impl::ISetTag> L, impl::anyset_like R>
struct __ixor__<L, R>                                           : Returns<L&> {};


/////////////////////////
////    FROZENSET    ////
/////////////////////////


/* Wrapper around pybind11::frozenset that allows it to be directly initialized using
std::initializer_list and replicates the Python interface as closely as possible. */
class FrozenSet : public impl::ISet<FrozenSet> {
    using Base = impl::ISet<FrozenSet>;
    friend Base;

    /* This helper function is needed for ISet mixin. */
    inline static PyObject* alloc(PyObject* obj) {
        return PyFrozenSet_New(obj);
    }

public:
    static Type type;

    BERTRAND_OBJECT_COMMON(Base, FrozenSet, impl::frozenset_like, PyFrozenSet_Check)
    BERTRAND_OBJECT_OPERATORS(FrozenSet)

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Default constructor.  Initializes to an empty set. */
    FrozenSet() : Base(PyFrozenSet_New(nullptr), stolen_t{}) {
        if (m_ptr == nullptr) {
            Exception::from_python();
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
            Exception::from_python();
        }
        try {
            for (const impl::HashInitializer& item : contents) {
                if (PySet_Add(m_ptr, item.first.ptr())) {
                    Exception::from_python();
                }
            }
        } catch (...) {
            Py_DECREF(m_ptr);
            throw;
        }
    }

    /* Construct a new FrozenSet from a pair of input iterators. */
    template <typename Iter, std::sentinel_for<Iter> Sentinel>
    explicit FrozenSet(Iter first, Sentinel last) :
        Base(PyFrozenSet_New(nullptr), stolen_t{})
    {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
        try {
            while (first != last) {
                if (PySet_Add(m_ptr, detail::object_or_cast(*first).ptr())) {
                    Exception::from_python();
                }
                ++first;
            }
        } catch (...) {
            Py_DECREF(m_ptr);
            throw;
        }
    }

    /* Explicitly unpack an arbitrary Python container into a new py::FrozenSet. */
    template <typename T>
        requires (impl::is_iterable<T> && impl::python_like<T> && !impl::frozenset_like<T>)
    explicit FrozenSet(const T& contents) :
        Base(PyFrozenSet_New(contents.ptr()), stolen_t{})
    {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /* Explicitly unpack an arbitrary C++ container into a new py::FrozenSet. */
    template <typename T> requires (impl::is_iterable<T> && !impl::python_like<T>)
    explicit FrozenSet(T&& container) {
        m_ptr = PyFrozenSet_New(nullptr);
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
        try {
            for (auto&& item : container) {
                if (PySet_Add(
                    m_ptr,
                    detail::object_or_cast(std::forward<decltype(item)>(item)).ptr())
                ) {
                    Exception::from_python();
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
            Exception::from_python();
        }
        try {
            if (PySet_Add(m_ptr, detail::object_or_cast(pair.first).ptr())) {
                Exception::from_python();
            }
            if (PySet_Add(m_ptr, detail::object_or_cast(pair.second).ptr())) {
                Exception::from_python();
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
            Exception::from_python();
        }
        try {
            Base::unpack_tuple(m_ptr, tuple, std::index_sequence_for<Args...>{});
        } catch (...) {
            Py_DECREF(m_ptr);
            throw;
        }
    }

    /////////////////////////////
    ////    C++ INTERFACE    ////
    /////////////////////////////

    /* Implicitly convert to pybind11::frozenset. */
    inline operator pybind11::frozenset() const {
        return reinterpret_borrow<pybind11::frozenset>(m_ptr);
    }

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    inline friend FrozenSet& operator|=(
        FrozenSet& self,
        const std::initializer_list<impl::HashInitializer>& other
    ) {
        self = self.union_(other);
        return self;
    }

    inline friend FrozenSet& operator&=(
        FrozenSet& self,
        const std::initializer_list<impl::HashInitializer>& other
    ) {
        self = self.intersection(other);
        return self;
    }

    inline friend FrozenSet& operator-=(
        FrozenSet& self,
        const std::initializer_list<impl::HashInitializer>& other
    ) {
        self = self.difference(other);
        return self;
    }

    inline friend FrozenSet& operator^=(
        FrozenSet& self,
        const std::initializer_list<impl::HashInitializer>& other
    ) {
        self = self.symmetric_difference(other);
        return self;
    }

};


///////////////////
////    SET    ////
///////////////////


template <std::derived_from<Set> T>
struct __getattr__<T, "add">                                    : Returns<Function> {};
template <std::derived_from<Set> T>
struct __getattr__<T, "remove">                                 : Returns<Function> {};
template <std::derived_from<Set> T>
struct __getattr__<T, "discard">                                : Returns<Function> {};
template <std::derived_from<Set> T>
struct __getattr__<T, "pop">                                    : Returns<Function> {};
template <std::derived_from<Set> T>
struct __getattr__<T, "clear">                                  : Returns<Function> {};
template <std::derived_from<Set> T>
struct __getattr__<T, "update">                                 : Returns<Function> {};
template <std::derived_from<Set> T>
struct __getattr__<T, "intersection_update">                    : Returns<Function> {};
template <std::derived_from<Set> T>
struct __getattr__<T, "difference_update">                      : Returns<Function> {};
template <std::derived_from<Set> T>
struct __getattr__<T, "symmetric_difference_update">            : Returns<Function> {};


/* Wrapper around pybind11::set that allows it to be directly initialized using
std::initializer_list and replicates the Python interface as closely as possible. */
class Set : public impl::ISet<Set> {
    using Base = impl::ISet<Set>;
    friend Base;

    /* This helper function is needed for ISet mixin. */
    inline static PyObject* alloc(PyObject* obj) {
        return PySet_New(obj);
    }

public:
    static Type type;

    BERTRAND_OBJECT_COMMON(Base, Set, impl::set_like, PySet_Check)
    BERTRAND_OBJECT_OPERATORS(Set)

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Default constructor.  Initializes to an empty set. */
    Set() : Base(PySet_New(nullptr), stolen_t{}) {
        if (m_ptr == nullptr) {
            Exception::from_python();
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
            Exception::from_python();
        }
        try {
            for (const impl::HashInitializer& item : contents) {
                if (PySet_Add(m_ptr, item.first.ptr())) {
                    Exception::from_python();
                }
            }
        } catch (...) {
            Py_DECREF(m_ptr);
            throw;
        }
    }

    /* Construct a new Set from a pair of input iterators. */
    template <typename Iter, std::sentinel_for<Iter> Sentinel>
    explicit Set(Iter first, Sentinel last) :
        Base(PySet_New(nullptr), stolen_t{})
    {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
        try {
            while (first != last) {
                if (PySet_Add(m_ptr, detail::object_or_cast(*first).ptr())) {
                    Exception::from_python();
                }
                ++first;
            }
        } catch (...) {
            Py_DECREF(m_ptr);
            throw;
        }
    }

    /* Explicitly unpack an arbitrary Python container into a new py::Set. */
    template <typename T>
        requires (impl::is_iterable<T> && impl::python_like<T> && !impl::set_like<T>)
    explicit Set(const T& contents) :
        Base(PySet_New(contents.ptr()), stolen_t{})
    {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /* Explicitly unpack an arbitrary C++ container into a new py::Set. */
    template <typename T> requires (impl::is_iterable<T> && !impl::python_like<T>)
    explicit Set(T&& contents) : Base(PySet_New(nullptr), stolen_t{}) {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
        try {
            for (auto&& item : contents) {
                if (PySet_Add(
                    m_ptr,
                    detail::object_or_cast(std::forward<decltype(item)>(item)).ptr())
                ) {
                    Exception::from_python();
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
            Exception::from_python();
        }
        try {
            if (PySet_Add(m_ptr, detail::object_or_cast(pair.first).ptr())) {
                Exception::from_python();
            }
            if (PySet_Add(m_ptr, detail::object_or_cast(pair.second).ptr())) {
                Exception::from_python();
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
            Exception::from_python();
        }
        try {
            Base::unpack_tuple(m_ptr, tuple, std::index_sequence_for<Args...>{});
        } catch (...) {
            Py_DECREF(m_ptr);
            throw;
        }
    }

    /////////////////////////////
    ////    C++ INTERFACE    ////
    /////////////////////////////

    /* Implicitly convert to pybind11::set. */
    inline operator pybind11::set() const {
        return reinterpret_borrow<pybind11::set>(m_ptr);
    }

    ////////////////////////////////
    ////    PYTHON INTERFACE    ////
    ////////////////////////////////

    /* Equivalent to Python `set.add(key)`. */
    template <impl::is_hashable T>
    inline void add(const T& key) {
        if (PySet_Add(this->ptr(), detail::object_or_cast(key).ptr())) {
            Exception::from_python();
        }
    }

    /* Equivalent to Python `set.remove(key)`. */
    template <impl::is_hashable T>
    inline void remove(const T& key) {
        Object obj = detail::object_or_cast(key);
        int result = PySet_Discard(this->ptr(), obj.ptr());
        if (result == -1) {
            Exception::from_python();
        } else if (result == 0) {
            throw KeyError(repr(obj));
        }
    }

    /* Equivalent to Python `set.discard(key)`. */
    template <impl::is_hashable T>
    inline void discard(const T& key) {
        if (PySet_Discard(this->ptr(), detail::object_or_cast(key).ptr()) == -1) {
            Exception::from_python();
        }
    }

    /* Equivalent to Python `set.pop()`. */
    inline Object pop() {
        PyObject* result = PySet_Pop(this->ptr());
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Object>(result);
    }

    /* Equivalent to Python `set.clear()`. */
    inline void clear() {
        if (PySet_Clear(this->ptr())) {
            Exception::from_python();
        }
    }

    /* Equivalent to Python `set.update(*others)`. */
    template <impl::is_iterable... Args>
    inline void update(const Args&... others);

    /* Equivalent to Python `set.update(<braced initializer list>)`. */
    inline void update(const std::initializer_list<impl::HashInitializer>& other) {
        for (const impl::HashInitializer& item : other) {
            add(item.first);
        }
    }

    /* Equivalent to Python `set.intersection_update(*others)`. */
    template <impl::is_iterable... Args>
    inline void intersection_update(const Args&... others);

    /* Equivalent to Python `set.intersection_update(<braced initializer list>)`. */
    inline void intersection_update(
        const std::initializer_list<impl::HashInitializer>& other
    );

    /* Equivalent to Python `set.difference_update(*others)`. */
    template <impl::is_iterable... Args>
    inline void difference_update(const Args&... others);

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
    inline void symmetric_difference_update(const T& other);

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

    inline friend Set& operator|=(
        Set& self,
        const std::initializer_list<impl::HashInitializer>& other
    ) {
        self.update(other);
        return self;
    }


    inline friend Set& operator&=(
        Set& self,
        const std::initializer_list<impl::HashInitializer>& other
    ) {
        self.intersection_update(other);
        return self;
    }


    inline friend Set& operator-=(
        Set& self,
        const std::initializer_list<impl::HashInitializer>& other
    ) {
        self.difference_update(other);
        return self;
    }


    inline friend Set& operator^=(
        Set& self,
        const std::initializer_list<impl::HashInitializer>& other
    ) {
        self.symmetric_difference_update(other);
        return self;
    }

};


}  // namespace py
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_SET_H
