#if !defined(BERTRAND_PYTHON_INCLUDED) && !defined(LINTER)
#error "This file should not be included directly.  Please include <bertrand/python.h> instead."
#endif

#ifndef BERTRAND_PYTHON_DICT_H
#define BERTRAND_PYTHON_DICT_H

#include "common.h"
#include "set.h"


namespace bertrand {
namespace py {


namespace impl {

template <>
struct __len__<KeysView>                                    : Returns<size_t> {};
template <>
struct __iter__<KeysView>                                   : Returns<Object> {};
template <>
struct __reversed__<KeysView>                               : Returns<Object> {};
template <is_hashable Key>
struct __contains__<KeysView, Key>                          : Returns<bool> {};
template <>
struct __lt__<KeysView, Object>                             : Returns<bool> {};
template <typename T> requires (std::is_base_of_v<KeysView, T>)
struct __lt__<KeysView, T>                                  : Returns<bool> {};
template <anyset_like T>
struct __lt__<KeysView, T>                                  : Returns<bool> {};
template <>
struct __le__<KeysView, Object>                             : Returns<bool> {};
template <typename T> requires (std::is_base_of_v<KeysView, T>)
struct __le__<KeysView, T>                                  : Returns<bool> {};
template <anyset_like T>
struct __le__<KeysView, T>                                  : Returns<bool> {};
template <>
struct __ge__<KeysView, Object>                             : Returns<bool> {};
template <typename T> requires (std::is_base_of_v<KeysView, T>)
struct __ge__<KeysView, T>                                  : Returns<bool> {};
template <anyset_like T>
struct __ge__<KeysView, T>                                  : Returns<bool> {};
template <>
struct __gt__<KeysView, Object>                             : Returns<bool> {};
template <typename T> requires (std::is_base_of_v<KeysView, T>)
struct __gt__<KeysView, T>                                  : Returns<bool> {};
template <anyset_like T>
struct __gt__<KeysView, T>                                  : Returns<bool> {};
template <>
struct __or__<KeysView, Object>                             : Returns<Set> {};
template <typename T> requires (std::is_base_of_v<KeysView, T>)
struct __or__<KeysView, T>                                  : Returns<Set> {};
template <anyset_like T>
struct __or__<KeysView, T>                                  : Returns<Set> {};
template <>
struct __and__<KeysView, Object>                            : Returns<Set> {};
template <typename T> requires (std::is_base_of_v<KeysView, T>)
struct __and__<KeysView, T>                                 : Returns<Set> {};
template <anyset_like T>
struct __and__<KeysView, T>                                 : Returns<Set> {};
template <>
struct __sub__<KeysView, Object>                            : Returns<Set> {};
template <typename T> requires (std::is_base_of_v<KeysView, T>)
struct __sub__<KeysView, T>                                 : Returns<Set> {};
template <anyset_like T>
struct __sub__<KeysView, T>                                 : Returns<Set> {};
template <>
struct __xor__<KeysView, Object>                            : Returns<Set> {};
template <typename T> requires (std::is_base_of_v<KeysView, T>)
struct __xor__<KeysView, T>                                 : Returns<Set> {};
template <anyset_like T>
struct __xor__<KeysView, T>                                 : Returns<Set> {};

template <>
struct __len__<ValuesView>                                  : Returns<size_t> {};
template <>
struct __iter__<ValuesView>                                 : Returns<Object> {};
template <>
struct __reversed__<ValuesView>                             : Returns<Object> {};
template <typename T>
struct __contains__<ValuesView, T>                          : Returns<bool> {};

template <>
struct __len__<ItemsView>                                   : Returns<size_t> {};
template <>
struct __iter__<ItemsView>                                  : Returns<Tuple> {};
template <>
struct __reversed__<ItemsView>                              : Returns<Tuple> {};
template <impl::tuple_like T>
struct __contains__<ItemsView, T>                           : Returns<bool> {};

template <>
struct __len__<Dict>                                        : Returns<size_t> {};
template <>
struct __iter__<Dict>                                       : Returns<Object> {};
template <>
struct __reversed__<Dict>                                   : Returns<Object> {};
template <is_hashable Key>
struct __contains__<Dict, Key>                              : Returns<bool> {};
template <is_hashable Key>
struct __getitem__<Dict, Key>                               : Returns<Object> {};
template <is_hashable Key, typename Value>
struct __setitem__<Dict, Key, Value>                        : Returns<void> {};
template <is_hashable Key>
struct __delitem__<Dict, Key>                               : Returns<void> {};
template <>
struct __or__<Dict, Object>                                 : Returns<Dict> {};
template <dict_like T>
struct __or__<Dict, T>                                      : Returns<Dict> {};
template <>
struct __ior__<Dict, Object>                                : Returns<Dict> {};
template <dict_like T>
struct __ior__<Dict, T>                                     : Returns<Dict> {};

template <>
struct __len__<MappingProxy>                                : Returns<size_t> {};
template <>
struct __iter__<MappingProxy>                               : Returns<Object> {};
template <>
struct __reversed__<MappingProxy>                           : Returns<Object> {};
template <is_hashable Key>
struct __contains__<MappingProxy, Key>                      : Returns<bool> {};
template <is_hashable Key>
struct __getitem__<MappingProxy, Key>                       : Returns<Object> {};
template <>
struct __or__<MappingProxy, Object>                         : Returns<Dict> {};
template <dict_like T>
struct __or__<MappingProxy, T>                              : Returns<Dict> {};

}


/* New subclass of pybind11::object representing a view into the keys of a dictionary
object. */
class KeysView : public Object {
    using Base = Object;

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

    BERTRAND_OBJECT_COMMON(Base, KeysView, keys_check)

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Copy/move constructors. */
    template <typename T> requires (check<T>() && impl::python_like<T>)
    KeysView(T&& other) : Base(std::forward<T>(other)) {}

    /* Explicitly create a keys view on an existing dictionary. */
    template <typename T> requires (impl::dict_like<T> && impl::python_like<T>)
    explicit KeysView(const T& dict) {
        m_ptr = dict.template attr<"keys">()().release().ptr();
    }

    ////////////////////////////////
    ////    PYTHON INTERFACE    ////
    ////////////////////////////////

    /* Equivalent to Python `dict.keys().mapping`. */
    inline impl::AttrProxy<MappingProxy> mapping() const;

    /* Equivalent to Python `dict.keys().isdisjoint(other)`. */
    template <impl::is_iterable T>
    inline bool isdisjoint(const T& other) const {
        return static_cast<bool>(attr<"isdisjoint">()(detail::object_or_cast(other)));
    }

    /* Equivalent to Python `dict.keys().isdisjoint(<braced initializer list>)`. */
    inline bool isdisjoint(
        const std::initializer_list<impl::HashInitializer>& other
    ) const {
        return static_cast<bool>(attr<"isdisjoint">()(Set(other)));
    }

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    inline Set operator|(
        const std::initializer_list<impl::HashInitializer>& other
    ) const {
        // TODO: use PyNumber_Or here?  <-- benchmark and test
        return Set(attr<"__or__">()(Set(other)));
    }

    inline Set operator&(
        const std::initializer_list<impl::HashInitializer>& other
    ) const {
        // TODO: same with PyNumber_And
        return Set(attr<"__and__">()(Set(other)));
    }

    inline Set operator-(
        const std::initializer_list<impl::HashInitializer>& other
    ) const {
        return Set(attr<"__sub__">()(Set(other)));
    }

    inline Set operator^(
        const std::initializer_list<impl::HashInitializer>& other
    ) const {
        return Set(attr<"__xor__">()(Set(other)));
    }

};


/* New subclass of pybind11::object representing a view into the values of a dictionary
object. */
class ValuesView : public Object {
    using Base = Object;

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

    BERTRAND_OBJECT_COMMON(Base, ValuesView, values_check)

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Copy/move constructors. */
    template <typename T> requires (check<T>() && impl::python_like<T>)
    ValuesView(T&& other) : Base(std::forward<T>(other)) {}

    /* Explicitly create a values view on an existing dictionary. */
    template <typename T> requires (impl::dict_like<T> && impl::python_like<T>)
    explicit ValuesView(const T& dict) {
        m_ptr = dict.template attr<"values">()().release().ptr();
    }

    ///////////////////////////////
    ////   PYTHON INTERFACE    ////
    ///////////////////////////////

    /* Equivalent to Python `dict.values().mapping`. */
    inline MappingProxy mapping() const;

};


/* New subclass of pybind11::object representing a view into the items of a dictionary
object. */
class ItemsView : public Object {
    using Base = Object;

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

    BERTRAND_OBJECT_COMMON(Base, ItemsView, items_check)

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Copy/move constructors. */
    template <typename T> requires (check<T>() && impl::python_like<T>)
    ItemsView(T&& other) : Base(std::forward<T>(other)) {}

    /* Explicitly create an items view on an existing dictionary. */
    template <typename T> requires (impl::dict_like<T> && impl::python_like<T>)
    explicit ItemsView(const T& dict) {
        m_ptr = dict.template attr<"items">()().release().ptr();
    }

    ////////////////////////////////
    ////    PYTHON INTERFACE    ////
    ////////////////////////////////

    /* Equivalent to Python `dict.items().mapping`. */
    inline MappingProxy mapping() const;

};


/* Wrapper around pybind11::dict that allows it to be directly initialized using
std::initializer_list and enables extra C API functionality. */
class Dict : public Object {
    using Base = Object;

    template <typename T>
    static constexpr bool py_unpacking_constructor =
        impl::python_like<T> && !impl::dict_like<T> && impl::is_iterable<T>;
    template <typename T>
    static constexpr bool cpp_unpacking_constructor =
        !impl::python_like<T> && impl::is_iterable<T>;


public:
    static Type type;

    template <typename T>
    static constexpr bool check() { return impl::dict_like<T>; }

    BERTRAND_OBJECT_COMMON(Base, Dict, PyDict_Check);

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Default constructor.  Initializes to empty dict. */
    Dict() : Base(PyDict_New(), stolen_t{}) {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Copy/move constructors. */
    template <typename T> requires (check<T>() && impl::python_like<T>)
    Dict(T&& other) : Base(std::forward<T>(other)) {}

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

    /* Explicitly unpack an arbitrary Python container into a new py::Dict. */
    template <typename T> requires (py_unpacking_constructor<T>)
    explicit Dict(const T& contents) :
        Base(PyObject_CallOneArg((PyObject*) &PyDict_Type, contents.ptr()), stolen_t{})
    {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Explicitly unpack a arbitrary C++ container into a new py::Dict. */
    template <typename T> requires (cpp_unpacking_constructor<T>)
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

    /////////////////////////////
    ////    C++ INTERFACE    ////
    /////////////////////////////

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

    /* Equivalent to Python `dict.update(items)`, but does not overwrite keys. */
    template <impl::dict_like T>
    inline void merge(const T& items) {
        if (PyDict_Merge(
            this->ptr(),
            detail::object_or_cast(items).ptr(),
            0
        )) {
            throw error_already_set();
        }
    }

    /* Equivalent to Python `dict.update(items)`, but does not overwrite keys. */
    template <typename T> requires (!impl::dict_like<T> && impl::is_iterable<T>)
    inline void merge(const T& items) {
        if (PyDict_MergeFromSeq2(
            this->ptr(),
            detail::object_or_cast(items).ptr(),
            0
        )) {
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
        return attr<"popitem">()();
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
        return reinterpret_steal<KeysView>(attr<"keys">()().release());
    }

    /* Equivalent to Python `dict.values()`. */
    inline ValuesView values() const {
        return reinterpret_steal<ValuesView>(attr<"values">()().release());
    }

    /* Equivalent to Python `dict.items()`. */
    inline ItemsView items() const {
        return reinterpret_steal<ItemsView>(attr<"items">()().release());
    }

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    inline auto begin() const { return Object::begin(); }
    inline auto end() const { return Object::end(); }

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

protected:

    template <typename Return, typename T>
    inline static size_t operator_len(const T& self) {
        return static_cast<size_t>(PyDict_Size(self.ptr()));
    }

    template <typename Return, typename L, typename R>
    inline static bool operator_contains(const L& self, const R& key) {
        int result = PyDict_Contains(
            self.ptr(),
            detail::object_or_cast(key).ptr()
        );
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

};


/* New subclass of pybind11::object representing a read-only proxy for a Python
dictionary or other mapping. */
class MappingProxy : public Object {
    using Base = Object;

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

    BERTRAND_OBJECT_COMMON(Base, MappingProxy, mappingproxy_check)

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Copy/move constructors. */
    template <typename T> requires (check<T>() && impl::python_like<T>)
    MappingProxy(T&& other) : Base(std::forward<T>(other)) {}

    /* Explicitly construct a read-only view on an existing dictionary. */
    MappingProxy(const Dict& dict) : Base(PyDictProxy_New(dict.ptr()), stolen_t{}) {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    ////////////////////////////////
    ////    PYTHON INTERFACE    ////
    ////////////////////////////////

    /* Equivalent to Python `mappingproxy.copy()`. */
    inline Dict copy() const {
        return reinterpret_steal<Dict>(attr<"copy">()().release());
    }

    /* Equivalent to Python `mappingproxy.get(key)`. */
    template <impl::is_hashable K>
    inline Object get(const K& key) const {
        return attr<"get">()(detail::object_or_cast(key), py::None);
    }

    /* Equivalent to Python `mappingproxy.get(key, default)`. */
    template <impl::is_hashable K, typename V>
    inline Object get(const K& key, const V& default_value) const {
        return attr<"get">()(
            detail::object_or_cast(key),
            detail::object_or_cast(default_value)
        );
    }

    /* Equivalent to Python `mappingproxy.keys()`. */
    inline KeysView keys() const {
        return reinterpret_steal<KeysView>(attr<"keys">()().release());
    }

    /* Equivalent to Python `mappingproxy.values()`. */
    inline ValuesView values() const {
        return reinterpret_steal<ValuesView>(attr<"values">()().release());
    }

    /* Equivalent to Python `mappingproxy.items()`. */
    inline ItemsView items() const {
        return reinterpret_steal<ItemsView>(attr<"items">()().release());
    }

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    inline Dict operator|(
        const std::initializer_list<impl::DictInitializer>& other
    ) const {
        Dict result = copy();
        result |= other;
        return result;
    }

};


// TODO: figure out how to return a type-safe AttrProxy here
// -> just return the attribute directly.  The proxy's copy/move constructors should
// be able to handle it.
// -> same idea should apply to access policies (readable, writable, deletable), which
// can be used to make the AttrProxy type-safe during read/write/delete operations.


inline impl::AttrProxy<MappingProxy> KeysView::mapping() const {
    static const pybind11::str method = "mapping";
    return impl::AttrProxy<MappingProxy>(*this, method);
}


inline MappingProxy ValuesView::mapping() const {
    return attr<"mapping">();
}


inline MappingProxy ItemsView::mapping() const {
    return attr<"mapping">();
}



// impl::AttrProxy<Object, impl::get | impl::set | impl::del>


}  // namespace py
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_DICT_H
