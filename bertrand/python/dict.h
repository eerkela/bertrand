#if !defined(BERTRAND_PYTHON_INCLUDED) && !defined(LINTER)
#error "This file should not be included directly.  Please include <bertrand/python.h> instead."
#endif

#ifndef BERTRAND_PYTHON_DICT_H
#define BERTRAND_PYTHON_DICT_H

#include "common.h"
#include "set.h"


namespace bertrand {
namespace py {


/* New subclass of pybind11::object representing a read-only proxy for a Python
dictionary or other mapping. */
class MappingProxy : public impl::Ops<MappingProxy> {
    using Base = impl::Ops<MappingProxy>;

    inline static bool mappingproxy_check(PyObject* obj) {
        int result = PyObject_IsInstance(obj, (PyObject*) &PyDictProxy_Type);
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

public:
    static Type type;

    template <typename T>
    static constexpr bool check() { return impl::mappingproxy_like<T>; }

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    BERTRAND_OBJECT_COMMON(Base, MappingProxy, mappingproxy_check)

    /* Explicitly construct a read-only view on an existing dictionary. */
    template <typename T> requires (impl::python_like<T> && impl::dict_like<T>)
    explicit MappingProxy(const T& dict) : Base(PyDictProxy_New(dict.ptr()), stolen_t{}) {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    ////////////////////////////////
    ////    PYTHON INTERFACE    ////
    ////////////////////////////////

    /* Get the length of the dictionary. */
    inline size_t size() const noexcept {
        return static_cast<size_t>(PyObject_Length(this->ptr()));
    }

    /* Check if the dictionary is empty. */
    inline bool empty() const noexcept {
        return size() == 0;
    }

    /* Equivalent to Python `mappingproxy.copy()`. */
    inline Dict copy() const;

    /* Equivalent to Python `mappingproxy.get(key)`. */
    template <impl::is_hashable K>
    inline Object get(const K& key) const {
        return this->attr("get")(detail::object_or_cast(key), py::None);
    }

    /* Equivalent to Python `mappingproxy.get(key, default)`. */
    template <impl::is_hashable K, typename V>
    inline Object get(const K& key, const V& default_value) const {
        return this->attr("get")(
            detail::object_or_cast(key),
            detail::object_or_cast(default_value)
        );
    }

    /* Equivalent to Python `mappingproxy.keys()`. */
    inline KeysView keys() const;

    /* Equivalent to Python `mappingproxy.values()`. */
    inline ValuesView values() const;

    /* Equivalent to Python `mappingproxy.items()`. */
    inline ItemsView items() const;

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    /* Equivalent to Python `key in mappingproxy`. */
    template <impl::is_hashable K>
    inline bool contains(const K& key) const;

    inline Dict operator|(
        const std::initializer_list<impl::DictInitializer>& other
    ) const;

};


template <>
struct impl::__or__<MappingProxy, Object> : impl::Returns<Dict> {};
template <impl::dict_like T>
struct impl::__or__<MappingProxy, T> : impl::Returns<Dict> {};


/* New subclass of pybind11::object representing a view into the keys of a dictionary
object. */
class KeysView : public impl::Ops<KeysView> {
    using Base = impl::Ops<KeysView>;

    inline static bool keys_check(PyObject* obj) {
        int result = PyObject_IsInstance(obj, (PyObject*) &PyDictKeys_Type);
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

public:
    static Type type;

    template <typename T>
    static constexpr bool check() { return std::is_base_of_v<KeysView, T>; }

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    BERTRAND_OBJECT_COMMON(Base, KeysView, keys_check)

    /* Explicitly create a keys view on an existing dictionary. */
    template <typename T> requires (impl::python_like<T> && impl::dict_like<T>)
    explicit KeysView(const T& dict) :
        Base(dict.attr("keys")().release(), stolen_t{})
    {}

    ////////////////////////////////
    ////    PYTHON INTERFACE    ////
    ////////////////////////////////

    /* Get the number of keys. */
    inline size_t size() const noexcept {
        return static_cast<size_t>(PyObject_Length(this->ptr()));
    }

    /* Check if the keys are empty. */
    inline bool empty() const noexcept {
        return size() == 0;
    }

    /* Equivalent to Python `dict.keys().mapping`. */
    inline MappingProxy mapping() const {
        return this->attr("mapping");
    }

    /* Equivalent to Python `dict.keys().isdisjoint(other)`. */
    template <impl::is_iterable T>
    inline bool isdisjoint(const T& other) const {
        return static_cast<bool>(
            this->attr("isdisjoint")(detail::object_or_cast(other))
        );
    }

    /* Equivalent to Python `dict.keys().isdisjoint(<braced initializer list>)`. */
    inline bool isdisjoint(
        const std::initializer_list<impl::HashInitializer>& other
    ) const {
        return static_cast<bool>(this->attr("isdisjoint")(Set(other)));
    }

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    /* Equivalent to `key in dict.keys()`. */
    template <impl::is_hashable K>
    inline bool contains(const K& key) const {
        int result = PySequence_Contains(this->ptr(), detail::object_or_cast(key).ptr());
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

    template <typename T>
    auto operator[](const T& key) const = delete;

    inline Set operator|(
        const std::initializer_list<impl::HashInitializer>& other
    ) const {
        return Set(this->attr("__or__")(Set(other)));
    }

    inline Set operator&(
        const std::initializer_list<impl::HashInitializer>& other
    ) const {
        return Set(this->attr("__and__")(Set(other)));
    }

    inline Set operator-(
        const std::initializer_list<impl::HashInitializer>& other
    ) const {
        return Set(this->attr("__sub__")(Set(other)));
    }

    inline Set operator^(
        const std::initializer_list<impl::HashInitializer>& other
    ) const {
        return Set(this->attr("__xor__")(Set(other)));
    }

};


namespace impl {


template <>
struct __lt__<KeysView, Object> : Returns<bool> {};
template <anyset_like T>
struct __lt__<KeysView, T> : Returns<bool> {};

template <>
struct __le__<KeysView, Object> : Returns<bool> {};
template <anyset_like T>
struct __le__<KeysView, T> : Returns<bool> {};

template <>
struct __ge__<KeysView, Object> : Returns<bool> {};
template <anyset_like T>
struct __ge__<KeysView, T> : Returns<bool> {};

template <>
struct __gt__<KeysView, Object> : Returns<bool> {};
template <anyset_like T>
struct __gt__<KeysView, T> : Returns<bool> {};

template <>
struct __or__<KeysView, Object> : Returns<Set> {};
template <anyset_like T>
struct __or__<KeysView, T> : Returns<Set> {};

template <>
struct __and__<KeysView, Object> : Returns<Set> {};
template <anyset_like T>
struct __and__<KeysView, T> : Returns<Set> {};

template <>
struct __sub__<KeysView, Object> : Returns<Set> {};
template <anyset_like T>
struct __sub__<KeysView, T> : Returns<Set> {};

template <>
struct __xor__<KeysView, Object> : Returns<Set> {};
template <anyset_like T>
struct __xor__<KeysView, T> : Returns<Set> {};


}  // namespace impl


/* New subclass of pybind11::object representing a view into the values of a dictionary
object. */
class ValuesView : public impl::Ops<ValuesView> {
    using Base = impl::Ops<ValuesView>;

    inline static bool values_check(PyObject* obj) {
        int result = PyObject_IsInstance(obj, (PyObject*) &PyDictValues_Type);
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

public:
    static Type type;

    template <typename T>
    static constexpr bool check() { return std::is_base_of_v<ValuesView, T>; }

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    BERTRAND_OBJECT_COMMON(Base, ValuesView, values_check)

    /* Explicitly create a values view on an existing dictionary. */
    template <typename T> requires (impl::python_like<T> && impl::dict_like<T>)
    explicit ValuesView(const T& dict) :
        Base(dict.attr("values")().release(), stolen_t{})
    {}

    ///////////////////////////////
    ////   PYTHON INTERFACE    ////
    ///////////////////////////////

    /* Get the number of values. */
    inline size_t size() const noexcept {
        return static_cast<size_t>(PyObject_Length(this->ptr()));
    }

    /* Check if the values are empty. */
    inline bool empty() const noexcept {
        return size() == 0;
    }

    /* Equivalent to Python `dict.values().mapping`. */
    inline MappingProxy mapping() const {
        return this->attr("mapping");
    }

    /* Equivalent to `value in dict.values()`. */
    template <typename T>
    inline bool contains(const T& value) const {
        int result = PySequence_Contains(this->ptr(), detail::object_or_cast(value).ptr());
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

    template <typename T>
    auto operator[](const T& key) const = delete;

};


/* New subclass of pybind11::object representing a view into the items of a dictionary
object. */
class ItemsView : public impl::Ops<ItemsView> {
    using Base = impl::Ops<ItemsView>;

    inline static bool items_check(PyObject* obj) {
        int result = PyObject_IsInstance(obj, (PyObject*) &PyDictItems_Type);
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

public:
    static Type type;

    template <typename T>
    static constexpr bool check() { return std::is_base_of_v<ItemsView, T>; }

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    BERTRAND_OBJECT_COMMON(Base, ItemsView, items_check)

    /* Explicitly create an items view on an existing dictionary. */
    template <typename T> requires (impl::python_like<T> && impl::dict_like<T>)
    explicit ItemsView(const T& dict) :
        Base(dict.attr("items")().release(), stolen_t{})
    {}

    ////////////////////////////////
    ////    PYTHON INTERFACE    ////
    ////////////////////////////////

    /* Get the number of items. */
    inline size_t size() const noexcept {
        return static_cast<size_t>(PyObject_Length(this->ptr()));
    }

    /* Check if the items are empty. */
    inline bool empty() const noexcept {
        return size() == 0;
    }

    /* Equivalent to Python `dict.items().mapping`. */
    inline MappingProxy mapping() const {
        return this->attr("mapping");
    }

    /* Equivalent to `value in dict.values()`. */
    template <typename T>
    inline bool contains(const T& value) const {
        int result = PySequence_Contains(
            this->ptr(),
            detail::object_or_cast(value).ptr()
        );
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

    template <typename T>
    auto operator[](const T& key) const = delete;

};


/* Wrapper around pybind11::dict that allows it to be directly initialized using
std::initializer_list and enables extra C API functionality. */
class Dict : public impl::Ops<Dict> {
    using Base = impl::Ops<Dict>;

    template <typename T>
    static constexpr bool constructor1 = !impl::python_like<T> && impl::is_iterable<T>;
    template <typename T>
    static constexpr bool constructor2 =
        impl::python_like<T> && !impl::dict_like<T> && impl::is_iterable<T>;

public:
    static Type type;

    template <typename T>
    static constexpr bool check() { return impl::dict_like<T>; }

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    BERTRAND_OBJECT_COMMON(Base, Dict, PyDict_Check);

    /* Copy/move constructors from equivalent pybind11 type. */
    Dict(const pybind11::dict& other) : Base(other.ptr(), borrowed_t{}) {}
    Dict(pybind11::dict&& other) : Base(other.release(), stolen_t{}) {}

    /* Default constructor.  Initializes to empty dict. */
    Dict() : Base(PyDict_New(), stolen_t{}) {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Pack the given arguments into a dictionary using an initializer list. */
    Dict(const std::initializer_list<impl::DictInitializer>& contents)
        : Base(PyDict_New(), stolen_t{})
    {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
        try {
            for (const impl::DictInitializer& item : contents) {
                if (PyDict_SetItem(m_ptr, item.first.ptr(), item.second.ptr())) {
                    throw error_already_set();
                }
            }
        } catch (...) {
            Py_DECREF(m_ptr);
            throw;
        }
    }

    /* Explicitly unpack a arbitrary C++ container into a new py::Dict. */
    template <typename T> requires (constructor1<T>)
    explicit Dict(const T& container) : Base(PyDict_New(), stolen_t{}) {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
        try {
            for (auto&& [k, v] : container) {
                if (PyDict_SetItem(
                    m_ptr,
                    detail::object_or_cast(std::forward<decltype(k)>(k)).ptr(),
                    detail::object_or_cast(std::forward<decltype(v)>(v)).ptr()
                )) {
                    throw error_already_set();
                }
            }
        } catch (...) {
            Py_DECREF(m_ptr);
            throw;
        }
    }

    /* Explicitly unpack an arbitrary Python container into a new py::Dict. */
    template <typename T> requires (constructor2<T>)
    explicit Dict(const T& contents) :
        Base(PyObject_CallOneArg((PyObject*) &PyDict_Type, contents.ptr()), stolen_t{})
    {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Construct a dictionary using pybind11-style keyword arguments.  This is
    technically superceeded by initializer lists, but it is provided for backwards
    compatibility with Python and pybind11. */
    template <
        typename... Args,
        typename collector = detail::deferred_t<detail::unpacking_collector<>, Args...>
    >
        requires (pybind11::args_are_all_keyword_or_ds<Args...>())
    explicit Dict(Args&&... args) :
        Dict(collector(std::forward<Args>(args)...).kwargs())
    {}

    ///////////////////////////
    ////    CONVERSIONS    ////
    ///////////////////////////

    /* Implicitly convert to a C++ dict type. */
    template <typename T> requires (!impl::python_like<T> && impl::dict_like<T>)
    inline operator T() const {
        T result;

        PyObject* key;
        PyObject* value;
        Py_ssize_t pos = 0;
        while (PyDict_Next(this->ptr(), &pos, &key, &value)) {
            using Key = typename T::key_type;
            using Value = typename T::mapped_type;
            Key converted_key = Handle(key).template cast<Key>();
            Value converted_value = Handle(value).template cast<Value>();
            result[converted_key] = converted_value;
        }

        return result;
    }

    ///////////////////////////
    ////    PyDict_ API    ////
    ///////////////////////////

    /* Get the size of the dict. */
    inline size_t size() const noexcept {
        return static_cast<size_t>(PyDict_Size(this->ptr()));
    }

    /* Check if the dict is empty. */
    inline bool empty() const noexcept {
        return size() == 0;
    }

    /* Equivalent to Python `dict.update(items)`, but does not overwrite keys. */
    template <impl::dict_like T>
    inline void merge(const T& items) {
        if (PyDict_Merge(this->ptr(), detail::object_or_cast(items).ptr(), 0)) {
            throw error_already_set();
        }
    }

    /* Equivalent to Python `dict.update(items)`, but does not overwrite keys. */
    template <typename T> requires (!impl::dict_like<T> && impl::is_iterable<T>)
    inline void merge(const T& items) {
        if (PyDict_MergeFromSeq2(this->ptr(), detail::object_or_cast(items).ptr(), 0)) {
            throw error_already_set();
        }
    }

    ////////////////////////////////
    ////    PYTHON INTERFACE    ////
    ////////////////////////////////

    /* Equivalent to Python `dict.clear()`. */
    inline void clear() { 
        PyDict_Clear(this->ptr());
    }

    /* Equivalent to Python `dict.copy()`. */
    inline Dict copy() const {
        PyObject* result = PyDict_Copy(this->ptr());
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Dict>(result);
    }

    /* Equivalent to Python `dict.fromkeys(keys)`.  Values default to None. */
    template <impl::is_iterable K>
    static inline Dict fromkeys(const K& keys) {
        PyObject* result = PyDict_New();
        if (result == nullptr) {
            throw error_already_set();
        }
        try {
            for (auto&& key : keys) {
                if (PyDict_SetItem(
                    result,
                    detail::object_or_cast(std::forward<decltype(key)>(key)).ptr(),
                    Py_None
                )) {
                    throw error_already_set();
                }
            }
            return reinterpret_steal<Dict>(result);
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
    }

    /* Equivalent to Python `dict.fromkeys(<braced initializer list>)`. */
    inline Dict fromkeys(const std::initializer_list<impl::HashInitializer>& keys) {
        PyObject* result = PyDict_New();
        if (result == nullptr) {
            throw error_already_set();
        }
        try {
            for (const impl::HashInitializer& init : keys) {
                if (PyDict_SetItem(
                    result,
                    init.first.ptr(),
                    Py_None
                )) {
                    throw error_already_set();
                }
            }
            return reinterpret_steal<Dict>(result);
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
    }

    /* Equivalent to Python `dict.fromkeys(keys, value)`. */
    template <impl::is_iterable K, typename V>
    static inline Dict fromkeys(const K& keys, const V& value) {
        Object converted = detail::object_or_cast(value);
        PyObject* result = PyDict_New();
        if (result == nullptr) {
            throw error_already_set();
        }
        try {
            for (auto&& key : keys) {
                if (PyDict_SetItem(
                    result,
                    detail::object_or_cast(std::forward<decltype(key)>(key)).ptr(),
                    converted.ptr()
                )) {
                    throw error_already_set();
                }
            }
            return reinterpret_steal<Dict>(result);
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
    }

    /* Equivalent to Python `dict.fromkeys(<braced initializer list>, value)`. */
    template <typename V>
    inline Dict fromkeys(
        const std::initializer_list<impl::HashInitializer>& keys,
        const V& value
    ) {
        Object converted = detail::object_or_cast(value);
        PyObject* result = PyDict_New();
        if (result == nullptr) {
            throw error_already_set();
        }
        try {
            for (const impl::HashInitializer& init : keys) {
                if (PyDict_SetItem(
                    result,
                    init.first.ptr(),
                    converted.ptr()
                )) {
                    throw error_already_set();
                }
            }
            return reinterpret_steal<Dict>(result);
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
    }

    /* Equivalent to Python `dict.get(key)`.  Returns None if the key is not found. */
    template <impl::is_hashable K>
    inline Object get(const K& key) const {
        PyObject* result = PyDict_GetItemWithError(
            this->ptr(),
            detail::object_or_cast(key).ptr()
        );
        if (result == nullptr) {
            if (PyErr_Occurred()) {
                throw error_already_set();
            }
            return reinterpret_borrow<Object>(Py_None);
        }
        return reinterpret_steal<Object>(result);
    }

    /* Equivalent to Python `dict.get(key, default_value)`. */
    template <impl::is_hashable K, typename V>
    inline Object get(const K& key, const V& default_value) const {
        PyObject* result = PyDict_GetItemWithError(
            this->ptr(),
            detail::object_or_cast(key).ptr()
        );
        if (result == nullptr) {
            if (PyErr_Occurred()) {
                throw error_already_set();
            }
            return detail::object_or_cast(default_value);
        }
        return reinterpret_steal<Object>(result);
    }

    /* Equivalent to Python `dict.pop(key)`.  Returns None if the key is not found. */
    template <impl::is_hashable K>
    inline Object pop(const K& key) {
        PyObject* result = PyDict_GetItemWithError(
            this->ptr(),
            detail::object_or_cast(key).ptr()
        );
        if (result == nullptr) {
            if (PyErr_Occurred()) {
                throw error_already_set();
            }
            return reinterpret_borrow<Object>(Py_None);
        }
        if (PyDict_DelItem(this->ptr(), result)) {
            throw error_already_set();
        }
        return reinterpret_steal<Object>(result);
    }

    /* Equivalent to Python `dict.pop(key, default_value)`. */
    template <impl::is_hashable K, typename V>
    inline Object pop(const K& key, const V& default_value) {
        PyObject* result = PyDict_GetItemWithError(
            this->ptr(),
            detail::object_or_cast(key).ptr()
        );
        if (result == nullptr) {
            if (PyErr_Occurred()) {
                throw error_already_set();
            }
            return detail::object_or_cast(default_value);
        }
        if (PyDict_DelItem(this->ptr(), result)) {
            throw error_already_set();
        }
        return reinterpret_steal<Object>(result);
    }

    /* Equivalent to Python `dict.popitem()`. */
    inline Object popitem() {
        return this->attr("popitem")();
    }

    /* Equivalent to Python `dict.setdefault(key)`. */
    template <impl::is_hashable K>
    inline Object setdefault(const K& key) {
        PyObject* result = PyDict_SetDefault(
            this->ptr(),
            detail::object_or_cast(key).ptr(),
            Py_None
        );
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Object>(result);
    }

    /* Equivalent to Python `dict.setdefault(key, default_value)`. */
    template <impl::is_hashable K, typename V>
    inline Object setdefault(const K& key, const V& default_value) {
        PyObject* result = PyDict_SetDefault(
            this->ptr(),
            detail::object_or_cast(key).ptr(),
            detail::object_or_cast(default_value).ptr()
        );
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Object>(result);
    }

    /* Equivalent to Python `dict.update(items)`. */
    template <impl::dict_like T>
    inline void update(const T& items) {
        if (PyDict_Merge(this->ptr(), items.ptr(), 1)) {
            throw error_already_set();
        }
    }

    /* Equivalent to Python `dict.update(items)`. */
    template <typename T> requires (!impl::dict_like<T> && impl::is_iterable<T>)
    inline void update(const T& items) {
        if constexpr (impl::python_like<T>) {
            if (PyDict_MergeFromSeq2(
                this->ptr(),
                detail::object_or_cast(items).ptr(),
                1
            )) {
                throw error_already_set();
            }
        } else {
            for (auto&& [k, v] : items) {
                if (PyDict_SetItem(
                    this->ptr(),
                    detail::object_or_cast(std::forward<decltype(k)>(k)).ptr(),
                    detail::object_or_cast(std::forward<decltype(v)>(v)).ptr()
                )) {
                    throw error_already_set();
                }
            }
        }
    }

    /* Equivalent to Python `dict.update(<braced initializer list>)`. */
    inline void update(const std::initializer_list<impl::DictInitializer>& items) {
        for (const impl::DictInitializer& item : items) {
            if (PyDict_SetItem(this->ptr(), item.first.ptr(), item.second.ptr())) {
                throw error_already_set();
            }
        }
    }

    /* Equivalent to Python `dict.keys()`. */
    inline KeysView keys() const {
        return reinterpret_steal<KeysView>(this->attr("keys")().release());
    }

    /* Equivalent to Python `dict.values()`. */
    inline ValuesView values() const {
        return reinterpret_steal<ValuesView>(this->attr("values")().release());
    }

    /* Equivalent to Python `dict.items()`. */
    inline ItemsView items() const {
        return reinterpret_steal<ItemsView>(this->attr("items")().release());
    }

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    inline auto begin() const { return Object::begin(); }
    inline auto end() const { return Object::end(); }

    /* Equivalent to Python `key in dict`. */
    template <impl::is_hashable K>
    inline bool contains(const K& key) const {
        int result = PyDict_Contains(this->ptr(), detail::object_or_cast(key).ptr());
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

    inline Dict operator|(
        const std::initializer_list<impl::DictInitializer>& other
    ) const {
        Dict result = copy();
        result.update(other);
        return result;
    }

    inline Dict& operator|=(
        const std::initializer_list<impl::DictInitializer>& other
    ) {
        update(other);
        return *this;
    }

};


namespace impl {

template <>
struct __or__<Dict, Object> : Returns<Dict> {};
template <dict_like T>
struct __or__<Dict, T> : Returns<Dict> {};

template <>
struct __ior__<Dict, Object> : Returns<Dict&> {};
template <dict_like T>
struct __ior__<Dict, T> : Returns<Dict&> {};

}  // namespace impl


inline Dict MappingProxy::copy() const{
    return reinterpret_steal<Dict>(this->attr("copy")().release());
}

inline KeysView MappingProxy::keys() const{
    return reinterpret_steal<KeysView>(this->attr("keys")().release());
}

inline ValuesView MappingProxy::values() const{
    return reinterpret_steal<ValuesView>(this->attr("values")().release());
}

template <impl::is_hashable K>
inline bool MappingProxy::contains(const K& key) const {
    return this->keys().contains(detail::object_or_cast(key));
}

inline Dict MappingProxy::operator|(
    const std::initializer_list<impl::DictInitializer>& other
) const {
    Dict result = copy();
    result |= other;
    return result;
}


}  // namespace python
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_DICT_H
