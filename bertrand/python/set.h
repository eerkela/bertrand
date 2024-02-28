#ifndef BERTRAND_PYTHON_INCLUDED
#error "This file should not be included directly.  Please include <bertrand/python.h> instead."
#endif

#ifndef BERTRAND_PYTHON_SET_H
#define BERTRAND_PYTHON_SET_H

#include "common.h"


namespace bertrand {
namespace py {


namespace impl {

    template <typename Derived>
    class ISet {

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

        template <typename T>
        struct is_iterable_struct {
            static constexpr bool value = impl::is_iterable<T>;
        };

        template <typename... Args>
        static constexpr bool all_iterable =
            std::conjunction_v<is_iterable_struct<std::decay_t<Args>>...>;

    public:

        ///////////////////////////
        ////    CONVERSIONS    ////
        ///////////////////////////

        /* Implicitly convert a py::FrozenSet into a C++ set or unordered_set. */
        template <
            typename T,
            std::enable_if_t<impl::is_anyset_like<T> && !impl::is_object<T>, int> = 0
        >
        inline operator T() const {
            T result;
            for (auto&& item : *self()) {
                result.insert(item.template cast<typename T::value_type>());
            }
        }

        //////////////////////////
        ////    PySet_ API    ////
        //////////////////////////

        /* Get the size of the set. */
        inline size_t size() const noexcept {
            return static_cast<size_t>(PySet_GET_SIZE(self()->ptr()));
        }

        /* Cehcek if the set is empty. */
        inline bool empty() const noexcept {
            return size() == 0;
        }
                                        
        ////////////////////////////////
        ////    PYTHON INTERFACE    ////
        ////////////////////////////////

        /* Equivalent to Python `set.copy()`. */
        inline Derived copy() const {
            PyObject* result = self()->convert_to_set(self()->ptr());
            if (result == nullptr) {
                throw error_already_set();
            }
            return reinterpret_steal<Derived>(result);
        }

        /* Equivalent to Python `set.isdisjoint(other)`. */
        template <typename T, std::enable_if_t<impl::is_iterable<T>, int> = 0>
        inline bool isdisjoint(const T& other) const {
            if constexpr (detail::is_pyobject<T>::value) {
                return static_cast<bool>(self()->attr("isdisjoint")(other));
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
        homogenously-typed braced initializer list. */
        template <typename T, std::enable_if_t<!impl::is_initializer<T>, int> = 0>
        inline bool isdisjoint(const std::initializer_list<T>& other) const {
            for (const T& item : other) {
                if (contains(item)) {
                    return false;
                }
            }
            return true;
        }

        /* Equivalent to Python `set.isdisjoint(other)`, where other is given as a
        mixed-type braced initializer list. */
        inline bool isdisjoint(const std::initializer_list<impl::Initializer>& other) const {
            for (const impl::Initializer& item : other) {
                if (contains(item.value)) {
                    return false;
                }
            }
            return true;
        }

        /* Equivalent to Python `set.issubset(other)`. */
        template <typename T, std::enable_if_t<impl::is_iterable<T>, int> = 0>
        inline bool issubset(const T& other) const {
            return static_cast<bool>(self()->attr("issubset")(
                detail::object_or_cast(other)
            ));
        }

        /* Equivalent to Python `set.issubset(other)`, where other is given as a
        homogenously-typed braced initializer list. */
        template <typename T, std::enable_if_t<!impl::is_initializer<T>, int> = 0>
        inline bool issubset(const std::initializer_list<T> other) const {
            return static_cast<bool>(self()->attr("issubset")(Derived(other)));
        }

        /* Equivalent to Python `set.issubset(other)`, where other is given as a
        mixed-type braced initializer list. */
        inline bool issubset(const std::initializer_list<impl::Initializer>& other) const {
            return static_cast<bool>(self()->attr("issubset")(Derived(other)));
        }

        /* Equivalent to Python `set.issuperset(other)`. */
        template <typename T, std::enable_if_t<impl::is_iterable<T>, int> = 0>
        inline bool issuperset(const T& other) const {
            if constexpr (detail::is_pyobject<T>::value) {
                return static_cast<bool>(self()->attr("issuperset")(other));
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
        homogenously-typed braced initializer list. */
        template <typename T, std::enable_if_t<!impl::is_initializer<T>, int> = 0>
        inline bool issuperset(const std::initializer_list<T>& other) const {
            for (const T& item : other) {
                if (!contains(item)) {
                    return false;
                }
            }
            return true;
        }

        /* Equivalent to Python `set.issuperset(other)`, where other is given as a
        mixed-type braced initializer list. */
        inline bool issuperset(
            const std::initializer_list<impl::Initializer>& other
        ) const {
            for (const impl::Initializer& item : other) {
                if (!contains(item.value)) {
                    return false;
                }
            }
            return true;
        }

        /* Equivalent to Python `set.union(*others)`. */
        template <typename... Args, std::enable_if_t<all_iterable<Args...>, int> = 0>
        inline Derived union_(Args&&... others) const {
            return self()->attr("union")(
                detail::object_or_cast(std::forward<Args>(others))...
            );
        }

        /* Equivalent to Python `set.union(other)`, where other is given as a 
        homogenously-typed braced initializer list. */
        template <typename T, std::enable_if_t<!impl::is_initializer<T>, int> = 0>
        inline Derived union_(const std::initializer_list<T>& other) const {
            PyObject* result = self()->convert_to_set(self()->ptr());
            if (result == nullptr) {
                throw error_already_set();
            }
            try {
                for (const T& item : other) {
                    if (PySet_Add(result, detail::object_or_cast(item).ptr())) {
                        throw error_already_set();
                    }
                }
                return reinterpret_steal<Derived>(result);
            } catch (...) {
                Py_DECREF(result);
                throw;
            }
        }

        /* Equivalent to Python `set.union(other)`, where other is given as a
        mixed-type braced initializer list. */
        inline Derived union_(const std::initializer_list<impl::Initializer>& other) const {
            PyObject* result = self()->convert_to_set(self()->ptr());
            if (result == nullptr) {
                throw error_already_set();
            }
            try {
                for (const impl::Initializer& item : other) {
                    if (PySet_Add(result, item.value.ptr())) {
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
        template <typename... Args, std::enable_if_t<all_iterable<Args...>, int> = 0>
        inline Derived intersection(Args&&... others) const {
            return self()->attr("intersection")(
                detail::object_or_cast(std::forward<Args>(others))...
            );
        }

        /* Equivalent to Python `set.intersection(other)`, where other is given as a
        homogenously-typed braced initializer list. */
        template <typename T, std::enable_if_t<!impl::is_initializer<T>, int> = 0>
        inline Derived intersection(const std::initializer_list<T>& other) const {
            PyObject* result = self()->convert_to_set(nullptr);
            if (result == nullptr) {
                throw error_already_set();
            }
            try {
                for (const T& item : other) {
                    Object obj = detail::object_or_cast(item);
                    if (contains(obj)) {
                        if (PySet_Add(result, obj.ptr())) {
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

        /* Equivalent to Python `set.intersection(other)`, where other is given as a
        mixed-type braced initializer list. */
        inline Derived intersection(
            const std::initializer_list<impl::Initializer>& other
        ) const {
            PyObject* result = self()->convert_to_set(nullptr);
            if (result == nullptr) {
                throw error_already_set();
            }
            try {
                for (const impl::Initializer& item : other) {
                    if (contains(item.value)) {
                        if (PySet_Add(result, item.value.ptr())) {
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
        template <typename... Args, std::enable_if_t<all_iterable<Args...>, int> = 0>
        inline Derived difference(Args&&... others) const {
            return self()->attr("difference")(
                detail::object_or_cast(std::forward<Args>(others))...
            );
        }

        /* Equivalent to Python `set.difference(other)`, where other is given as a
        homogenously-typed braced initializer list. */
        template <typename T, std::enable_if_t<!impl::is_initializer<T>, int> = 0>
        inline Derived difference(const std::initializer_list<T>& other) const {
            PyObject* result = self()->convert_to_set(self()->ptr());
            if (result == nullptr) {
                throw error_already_set();
            }
            try {
                for (const T& item : other) {
                    if (PySet_Discard(result, detail::object_or_cast(item).ptr()) == -1) {
                        throw error_already_set();
                    }
                }
                return reinterpret_steal<Derived>(result);
            } catch (...) {
                Py_DECREF(result);
                throw;
            }
        }

        /* Equivalent to Python `set.difference(other)`, where other is given as a
        mixed-type braced initializer list. */
        inline Derived difference(
            const std::initializer_list<impl::Initializer>& other
        ) const {
            PyObject* result = self()->convert_to_set(self()->ptr());
            if (result == nullptr) {
                throw error_already_set();
            }
            try {
                for (const impl::Initializer& item : other) {
                    if (PySet_Discard(result, item.value.ptr()) == -1) {
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
        template <typename T, std::enable_if_t<impl::is_iterable<T>, int> = 0>
        inline Derived symmetric_difference(const T& other) const {
            return self()->attr("symmetric_difference")(detail::object_or_cast(other));
        }

        /* Equivalent to Python `set.symmetric_difference(other)`, where other is given
        as a homogenously-typed braced initializer list. */
        template <typename T, std::enable_if_t<!impl::is_initializer<T>, int> = 0>
        inline Derived symmetric_difference(const std::initializer_list<T>& other) const {
            PyObject* result = self()->convert_to_set(nullptr);
            if (result == nullptr) {
                throw error_already_set();
            }
            try {
                for (const T& item : other) {
                    Object obj = detail::object_or_cast(item);
                    if (contains(obj)) {
                        if (PySet_Discard(result, obj.ptr()) == -1) {
                            throw error_already_set();
                        }
                    } else {
                        if (PySet_Add(result, obj.ptr())) {
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

        /* Equivalent to Python `set.symmetric_difference(other)`, where other is given
        as a mixed-type braced initializer list. */
        inline Derived symmetric_difference(
            const std::initializer_list<impl::Initializer>& other
        ) const {
            PyObject* result = self()->convert_to_set(nullptr);
            if (result == nullptr) {
                throw error_already_set();
            }
            try {
                for (const impl::Initializer& item : other) {
                    if (contains(item.value)) {
                        if (PySet_Discard(result, item.value.ptr()) == -1) {
                            throw error_already_set();
                        }
                    } else {
                        if (PySet_Add(result, item.value.ptr())) {
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

        /* Equivalent to Python `key in set`. */
        template <typename T>
        inline bool contains(const T& key) const {
            int result = PySet_Contains(
                self()->ptr(),
                detail::object_or_cast(key).ptr()
            );
            if (result == -1) {
                throw error_already_set();
            }
            return result;
        }

        template <typename T, std::enable_if_t<impl::is_anyset_like<T>, int> = 0>
        inline Derived operator|(const T& other) const {
            return union_(other);
        }

        template <typename T, std::enable_if_t<!impl::is_initializer<T>, int> = 0>
        inline Derived operator|(const std::initializer_list<T>& other) const {
            return union_(other);
        }

        inline Derived operator|(
            const std::initializer_list<impl::Initializer>& other
        ) const {
            return union_(other);
        }

        template <typename T, std::enable_if_t<impl::is_anyset_like<T>, int> = 0>
        inline Derived operator&(const T& other) const {
            return intersection(other);
        }

        template <typename T, std::enable_if_t<!impl::is_initializer<T>, int> = 0>
        inline Derived operator&(const std::initializer_list<T>& other) const {
            return intersection(other);
        }

        inline Derived operator&(
            const std::initializer_list<impl::Initializer>& other
        ) const {
            return intersection(other);
        }

        template <typename T, std::enable_if_t<impl::is_anyset_like<T>, int> = 0>
        inline Derived operator-(const T& other) const {
            return difference(other);
        }

        template <typename T, std::enable_if_t<!impl::is_initializer<T>, int> = 0>
        inline Derived operator-(const std::initializer_list<T>& other) const {
            return difference(other);
        }

        inline Derived operator-(
            const std::initializer_list<impl::Initializer>& other
        ) const {
            return difference(other);
        }

        template <typename T, std::enable_if_t<impl::is_anyset_like<T>, int> = 0>
        inline Derived operator^(const T& other) const {
            return symmetric_difference(other);
        }

        template <typename T, std::enable_if_t<!impl::is_initializer<T>, int> = 0>
        inline Derived operator^(const std::initializer_list<T>& other) const {
            return symmetric_difference(other);
        }

        inline Derived operator^(
            const std::initializer_list<impl::Initializer>& other
        ) const {
            return symmetric_difference(other);
        }

    };

}  // namespace impl


/* Wrapper around pybind11::frozenset that allows it to be directly initialized using
std::initializer_list and replicates the Python interface as closely as possible. */
class FrozenSet :
    public Object,
    public impl::ISet<FrozenSet>,
    public impl::Ops<FrozenSet>
{
    using ISet = impl::ISet<FrozenSet>;
    using Ops = impl::Ops<FrozenSet>;

    friend ISet;

    /* This helper function is needed for interface mixin. */
    inline static PyObject* convert_to_set(PyObject* obj) {
        return PyFrozenSet_New(obj);
    }

    template <typename T>
    static constexpr bool constructor1 =
        !(impl::is_object_exact<T> || impl::is_object<T> && impl::is_frozenset_like<T>) &&
        impl::is_iterable<T>;

public:
    static py::Type Type;

    template <typename T>
    static constexpr bool like = impl::is_frozenset_like<T>;

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    BERTRAND_PYTHON_CONSTRUCTORS(Object, FrozenSet, PyFrozenSet_Check, convert_to_set);

    /* Default constructor.  Initializes to an empty set. */
    FrozenSet() : Object(PyFrozenSet_New(nullptr), stolen_t{}) {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Pack the contents of a homogenously-typed braced initializer list into a new
    Python frozenset. */
    template <typename T, std::enable_if_t<!impl::is_initializer<T>, int> = 0>
    FrozenSet(const std::initializer_list<T>& contents) :
        Object(PyFrozenSet_New(nullptr), stolen_t{})
    {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
        try {
            for (const T& element : contents) {
                if (PySet_Add(m_ptr, detail::object_or_cast(element).ptr())) {
                    throw error_already_set();
                }
            }
        } catch (...) {
            Py_DECREF(m_ptr);
            throw;
        }
    }

    /* Pack the contents of a mixed-type braced initializer list into a new Python
    frozenset. */
    FrozenSet(const std::initializer_list<impl::Initializer>& contents) :
        Object(PyFrozenSet_New(nullptr), stolen_t{})
    {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
        try {
            for (const impl::Initializer& element : contents) {
                if (PySet_Add(m_ptr, element.value.ptr())) {
                    throw error_already_set();
                }
            }
        } catch (...) {
            Py_DECREF(m_ptr);
            throw;
        }
    }

    /* Explicitly unpack a generic Python or C++ container into a new py::FrozenSet. */
    template <typename T, std::enable_if_t<constructor1<T>, int> = 0>
    explicit FrozenSet(const T& container) {
        if constexpr (detail::is_pyobject<T>::value) {
            m_ptr = PyFrozenSet_New(container.ptr());
            if (m_ptr == nullptr) {
                throw error_already_set();
            }
        } else {
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
    }

    /* Explicitly unpack a std::pair into a py::FrozenSet. */
    template <typename First, typename Second>
    explicit FrozenSet(const std::pair<First, Second>& pair) :
        Object(PyFrozenSet_New(nullptr), stolen_t{})
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
    template <typename... Args>
    explicit FrozenSet(const std::tuple<Args...>& tuple) :
        Object(PyFrozenSet_New(nullptr), stolen_t{})
    {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
        try {
            ISet::unpack_tuple(m_ptr, tuple, std::index_sequence_for<Args...>{});
        } catch (...) {
            Py_DECREF(m_ptr);
            throw;
        }
    }

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    using ISet::size;
    using ISet::empty;
    using ISet::contains;

    using Ops::operator<;
    using Ops::operator<=;
    using Ops::operator==;
    using Ops::operator!=;
    using Ops::operator>=;
    using Ops::operator>;

    template <typename T, std::enable_if_t<impl::is_anyset_like<T>, int> = 0>
    inline FrozenSet& operator|=(const T& other) {
        *this = union_(other);
        return *this;
    }

    template <typename T, std::enable_if_t<!impl::is_initializer<T>, int> = 0>
    inline FrozenSet& operator|=(const std::initializer_list<T>& other) {
        *this = union_(other);
        return *this;
    }

    inline FrozenSet& operator|=(const std::initializer_list<impl::Initializer>& other) {
        *this = union_(other);
        return *this;
    }

    template <typename T, std::enable_if_t<impl::is_anyset_like<T>, int> = 0>
    inline FrozenSet& operator&=(const T& other) {
        *this = intersection(other);
        return *this;
    }

    template <typename T, std::enable_if_t<!impl::is_initializer<T>, int> = 0>
    inline FrozenSet& operator&=(const std::initializer_list<T>& other) {
        *this = intersection(other);
        return *this;
    }

    inline FrozenSet& operator&=(const std::initializer_list<impl::Initializer>& other) {
        *this = intersection(other);
        return *this;
    }

    template <typename T, std::enable_if_t<impl::is_anyset_like<T>, int> = 0>
    inline FrozenSet& operator-=(const T& other) {
        *this = difference(other);
        return *this;
    }

    template <typename T, std::enable_if_t<!impl::is_initializer<T>, int> = 0>
    inline FrozenSet& operator-=(const std::initializer_list<T>& other) {
        *this = difference(other);
        return *this;
    }

    inline FrozenSet& operator-=(const std::initializer_list<impl::Initializer>& other) {
        *this = difference(other);
        return *this;
    }

    template <typename T, std::enable_if_t<impl::is_anyset_like<T>, int> = 0>
    inline FrozenSet& operator^=(const T& other) {
        *this = symmetric_difference(other);
        return *this;
    }

    template <typename T, std::enable_if_t<!impl::is_initializer<T>, int> = 0>
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
class Set :
    public Object,
    public impl::ISet<Set>,
    public impl::Ops<Set>
{
    using ISet = impl::ISet<Set>;
    using Ops = impl::Ops<Set>;

    friend ISet;

    /* This helper function is needed for interface mixin. */
    inline static PyObject* convert_to_set(PyObject* obj) {
        return PySet_New(obj);
    }

    template <typename T>
    static constexpr bool constructor1 =
        !(impl::is_object_exact<T> || impl::is_object<T> && impl::is_set_like<T>) &&
        impl::is_iterable<T>;

public:
    static py::Type Type;

    template <typename T>
    static constexpr bool like = impl::is_set_like<T>;

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    BERTRAND_PYTHON_CONSTRUCTORS(Object, Set, PySet_Check, convert_to_set);

    /* Default constructor.  Initializes to an empty set. */
    Set() : Object(PySet_New(nullptr), stolen_t{}) {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Pack the contents of a homogenously-typed braced initializer list into a new
    Python set. */
    template <typename T, std::enable_if_t<!impl::is_initializer<T>, int> = 0>
    Set(const std::initializer_list<T>& contents) :
        Object(PySet_New(nullptr), stolen_t{})
    {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
        try {
            for (const T& element : contents) {
                if (PySet_Add(m_ptr, detail::object_or_cast(element).ptr())) {
                    throw error_already_set();
                }
            }
        } catch (...) {
            Py_DECREF(m_ptr);
            throw;
        }
    }

    /* Pack the contents of a mixed-type braced initializer list into a new Python
    set. */
    Set(const std::initializer_list<impl::Initializer>& contents) :
        Object(PySet_New(nullptr), stolen_t{})
    {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
        try {
            for (const impl::Initializer& element : contents) {
                if (PySet_Add(m_ptr, element.value.ptr())) {
                    throw error_already_set();
                }
            }
        } catch (...) {
            Py_DECREF(m_ptr);
            throw;
        }
    }

    /* Explicitly unpack a generic C++ container into a new py::Set. */
    template <typename T, std::enable_if_t<constructor1<T>, int> = 0>
    explicit Set(const T& container) {
        m_ptr = PySet_New(nullptr);
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

    /* Explicitly unpack a std::pair into a py::Set. */
    template <typename First, typename Second>
    explicit Set(const std::pair<First, Second>& pair) :
        Object(PySet_New(nullptr), stolen_t{})
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
    template <typename... Args>
    explicit Set(const std::tuple<Args...>& tuple) :
        Object(PySet_New(nullptr), stolen_t{})
    {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
        try {
            ISet::unpack_tuple(m_ptr, tuple, std::index_sequence_for<Args...>{});
        } catch (...) {
            Py_DECREF(m_ptr);
            throw;
        }
    }

    ////////////////////////////////
    ////    PYTHON INTERFACE    ////
    ////////////////////////////////

    /* Equivalent to Python `set.add(key)`. */
    template <typename T>
    inline void add(const T& key) {
        if (PySet_Add(this->ptr(), detail::object_or_cast(key).ptr())) {
            throw error_already_set();
        }
    }

    /* Equivalent to Python `set.remove(key)`. */
    template <typename T>
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
    template <typename T>
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
    template <typename... Args, std::enable_if_t<ISet::all_iterable<Args...>, int> = 0>
    inline void update(Args&&... others) {
        this->attr("update")(
            detail::object_or_cast(std::forward<Args>(others))...
        );
    }

    /* Equivalent to Python `set.update(<braced initializer list>)`. */
    template <typename T, std::enable_if_t<!impl::is_initializer<T>, int> = 0>
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
    template <typename... Args, std::enable_if_t<ISet::all_iterable<Args...>, int> = 0>
    inline void intersection_update(Args&&... others) {
        this->attr("intersection_update")(
            detail::object_or_cast(std::forward<Args>(others))...
        );
    }

    /* Equivalent to Python `set.intersection_update(<braced initializer list>)`. */
    template <typename T, std::enable_if_t<!impl::is_initializer<T>, int> = 0>
    inline void intersection_update(const std::initializer_list<T>& other) {
        this->attr("intersection_update")(Set(other));
    }

    /* Equivalent to Python `set.intersection_update(<braced initializer list>)`. */
    inline void intersection_update(
        const std::initializer_list<impl::Initializer>& other
    ) {
        this->attr("intersection_update")(Set(other));
    }

    /* Equivalent to Python `set.difference_update(*others)`. */
    template <typename... Args, std::enable_if_t<ISet::all_iterable<Args...>, int> = 0>
    inline void difference_update(Args&&... others) {
        this->attr("difference_update")(
            detail::object_or_cast(std::forward<Args>(others))...
        );
    }

    /* Equivalent to Python `set.difference_update(<braced initializer list>)`. */
    template <typename T, std::enable_if_t<!impl::is_initializer<T>, int> = 0>
    inline void difference_update(const std::initializer_list<T>& other) {
        for (const T& item : other) {
            discard(item);
        }
    }

    /* Equivalent to Python `set.difference_update(<braced initializer list>)`. */
    inline void difference_update(
        const std::initializer_list<impl::Initializer>& other
    ) {
        for (const impl::Initializer& item : other) {
            discard(item.value);
        }
    }

    /* Equivalent to Python `set.symmetric_difference_update(other)`. */
    template <typename T, std::enable_if_t<impl::is_iterable<T>, int> = 0>
    inline void symmetric_difference_update(const T& other) {
        this->attr("symmetric_difference_update")(detail::object_or_cast(other));
    }

    /* Equivalent to Python `set.symmetric_difference_update(<braced initializer list>)`. */
    template <typename T, std::enable_if_t<!impl::is_initializer<T>, int> = 0>
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
    inline void symmetric_difference_update(
        const std::initializer_list<impl::Initializer>& other
    ) {
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

    using ISet::size;
    using ISet::empty;
    using ISet::contains;

    using Ops::operator<;
    using Ops::operator<=;
    using Ops::operator==;
    using Ops::operator!=;
    using Ops::operator>=;
    using Ops::operator>;

    /* Equivalent to Python `set |= other`. */
    template <typename T, std::enable_if_t<impl::is_anyset_like<T>, int> = 0>
    inline Set& operator|=(const T& other) {
        update(other);
        return *this;
    }

    /* Equivalent to Python `set |= <braced initializer list>`. */
    template <typename T, std::enable_if_t<!impl::is_initializer<T>, int> = 0>
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
    template <typename T, std::enable_if_t<impl::is_anyset_like<T>, int> = 0>
    inline Set& operator&=(const T& other) {
        intersection_update(other);
        return *this;
    }

    /* Equivalent to Python `set &= <braced initializer list>`. */
    template <typename T, std::enable_if_t<!impl::is_initializer<T>, int> = 0>
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
    template <typename T, std::enable_if_t<impl::is_anyset_like<T>, int> = 0>
    inline Set& operator-=(const T& other) {
        difference_update(other);
        return *this;
    }

    /* Equivalent to Python `set -= <braced initializer list>`. */
    template <typename T, std::enable_if_t<!impl::is_initializer<T>, int> = 0>
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
    template <typename T, std::enable_if_t<impl::is_anyset_like<T>, int> = 0>
    inline Set& operator^=(const T& other) {
        symmetric_difference_update(other);
        return *this;
    }

    /* Equivalent to Python `set ^= <braced initializer list>`. */
    template <typename T, std::enable_if_t<!impl::is_initializer<T>, int> = 0>
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


}  // namespace python
}  // namespace bertrand


BERTRAND_STD_HASH(bertrand::py::FrozenSet)


#endif  // BERTRAND_PYTHON_SET_H
