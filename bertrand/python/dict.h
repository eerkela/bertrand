#ifndef BERTRAND_PYTHON_DICT_H
#define BERTRAND_PYTHON_DICT_H

#include "common.h"
#include "set.h"
#include <ostream>


namespace bertrand {
namespace py {


/* New subclass of pybind11::object representing a read-only proxy for a Python
dictionary or other mapping. */
class MappingProxy : public Object, public impl::Ops<MappingProxy> {

    inline static bool mappingproxy_check(PyObject* obj) {
        int result = PyObject_IsInstance(obj, (PyObject*) &PyDictProxy_Type);
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

    inline static PyObject* convert_to_mappingproxy(PyObject* obj) {
        PyObject* result = PyDictProxy_New(obj);
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }

public:
    static py::Type Type;
    BERTRAND_PYTHON_CONSTRUCTORS(
        Object,
        MappingProxy,
        mappingproxy_check,
        convert_to_mappingproxy
    )

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
    inline Dict copy() const;  // out of line to avoid circular dependency.

    /* Equivalent to Python `mappingproxy.get(key)`. */
    template <typename K, typename V>
    inline Object get(K&& key) const {
        return this->attr("get")(
            detail::object_or_cast(std::forward<K>(key)),
            py::None
        );
    }

    /* Equivalent to Python `mappingproxy.get(key, default)`. */
    template <typename K, typename V>
    inline Object get(K&& key, V&& default_value) const {
        return this->attr("get")(
            detail::object_or_cast(std::forward<K>(key)),
            detail::object_or_cast(std::forward<V>(default_value))
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
    template <typename T>
    inline bool contains(T&& key) const;

    using impl::Ops<MappingProxy>::operator==;
    using impl::Ops<MappingProxy>::operator!=;
    using impl::Ops<MappingProxy>::operator&;
    using impl::Ops<MappingProxy>::operator<<;

    // Operator overloads provided out of line to avoid circular dependency.

};


/* New subclass of pybind11::object representing a view into the keys of a dictionary
object. */
class KeysView : public Object, public impl::Ops<KeysView> {

    inline static bool keys_check(PyObject* obj) {
        int result = PyObject_IsInstance(obj, (PyObject*) &PyDictKeys_Type);
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

    inline static PyObject* convert_to_keys(PyObject* obj) {
        if (PyDict_Check(obj)) {
            PyObject* attr = PyObject_GetAttrString(obj, "keys");
            if (attr == nullptr) {
                throw error_already_set();
            }
            PyObject* result = PyObject_CallNoArgs(attr);
            Py_DECREF(attr);
            if (result == nullptr) {
                throw error_already_set();
            }
            return result;
        } else {
            throw TypeError("expected a dict");
        }
    }

public:
    static py::Type Type;
    BERTRAND_PYTHON_CONSTRUCTORS(Object, KeysView, keys_check, convert_to_keys)

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
    template <typename T>
    inline bool isdisjoint(T&& other) const {
        return this->attr("isdisjoint")(
            detail::object_or_cast(std::forward<T>(other))
        ).template cast<bool>();
    }

    /* Equivalent to Python `dict.keys().isdisjoint(<braced initializer list>)`. */
    template <typename T>
    inline bool isdisjoint(const std::initializer_list<T>& other) const {
        return this->attr("isdisjoint")(Set(other)).template cast<bool>();
    }

    /* Equivalent to Python `dict.keys().isdisjoint(<braced initializer list>)`. */
    inline bool isdisjoint(const std::initializer_list<impl::Initializer>& other) const {
        return this->attr("isdisjoint")(Set(other)).template cast<bool>();
    }

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    /* Equivalent to `key in dict.keys()`. */
    inline bool contains(const Handle& key) const {
        int result = PySequence_Contains(this->ptr(), key.ptr());
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

    template <typename T>
    inline Set operator|(T&& other) const {
        return Set(this->attr("__or__")(
            detail::object_or_cast(std::forward<T>(other))
        ));
    }

    template <typename T>
    inline Set operator|(const std::initializer_list<T>& other) const {
        return Set(this->attr("__or__")(Set(other)));
    }

    inline Set operator|(const std::initializer_list<impl::Initializer>& other) const {
        return Set(this->attr("__or__")(Set(other)));
    }

    template <typename T>
    inline Set operator&(T&& other) const {
        return Set(this->attr("__and__")(
            detail::object_or_cast(std::forward<T>(other))
        ));
    }

    template <typename T>
    inline Set operator&(const std::initializer_list<T>& other) const {
        return Set(this->attr("__and__")(Set(other)));
    }

    inline Set operator&(const std::initializer_list<impl::Initializer>& other) const {
        return Set(this->attr("__and__")(Set(other)));
    }

    template <typename T>
    inline Set operator-(T&& other) const {
        return Set(this->attr("__sub__")(
            detail::object_or_cast(std::forward<T>(other))
        ));
    }

    template <typename T>
    inline Set operator-(const std::initializer_list<T>& other) const {
        return Set(this->attr("__sub__")(Set(other)));
    }

    inline Set operator-(const std::initializer_list<impl::Initializer>& other) const {
        return Set(this->attr("__sub__")(Set(other)));
    }

    template <typename T>
    inline Set operator^(T&& other) const {
        return Set(this->attr("__xor__")(
            detail::object_or_cast(std::forward<T>(other))
        ));
    }

    template <typename T>
    inline Set operator^(const std::initializer_list<T>& other) const {
        return Set(this->attr("__xor__")(Set(other)));
    }

    inline Set operator^(const std::initializer_list<impl::Initializer>& other) const {
        return Set(this->attr("__xor__")(Set(other)));
    }

    using impl::Ops<KeysView>::operator<;
    using impl::Ops<KeysView>::operator<=;
    using impl::Ops<KeysView>::operator==;
    using impl::Ops<KeysView>::operator!=;
    using impl::Ops<KeysView>::operator>=;
    using impl::Ops<KeysView>::operator>;
};


/* New subclass of pybind11::object representing a view into the values of a dictionary
object. */
class ValuesView : public Object, public impl::Ops<ValuesView> {

    inline static bool values_check(PyObject* obj) {
        int result = PyObject_IsInstance(obj, (PyObject*) &PyDictValues_Type);
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

    inline static PyObject* convert_to_values(PyObject* obj) {
        if (PyDict_Check(obj)) {
            PyObject* attr = PyObject_GetAttrString(obj, "values");
            if (attr == nullptr) {
                throw error_already_set();
            }
            PyObject* result = PyObject_CallNoArgs(attr);
            Py_DECREF(attr);
            if (result == nullptr) {
                throw error_already_set();
            }
            return result;
        } else {
            throw TypeError("expected a dict");
        }
    }

public:
    static py::Type Type;
    BERTRAND_PYTHON_CONSTRUCTORS(Object, ValuesView, values_check, convert_to_values)

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
    inline bool contains(const Handle& value) const {
        int result = PySequence_Contains(this->ptr(), value.ptr());
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

    using impl::Ops<ValuesView>::operator<;
    using impl::Ops<ValuesView>::operator<=;
    using impl::Ops<ValuesView>::operator==;
    using impl::Ops<ValuesView>::operator!=;
    using impl::Ops<ValuesView>::operator>=;
    using impl::Ops<ValuesView>::operator>;
};


/* New subclass of pybind11::object representing a view into the items of a dictionary
object. */
struct ItemsView : public Object, public impl::Ops<ItemsView> {

    inline static bool items_check(PyObject* obj) {
        int result = PyObject_IsInstance(obj, (PyObject*) &PyDictItems_Type);
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

    inline static PyObject* convert_to_items(PyObject* obj) {
        if (PyDict_Check(obj)) {
            PyObject* attr = PyObject_GetAttrString(obj, "items");
            if (attr == nullptr) {
                throw error_already_set();
            }
            PyObject* result = PyObject_CallNoArgs(attr);
            Py_DECREF(attr);
            if (result == nullptr) {
                throw error_already_set();
            }
            return result;
        } else {
            throw TypeError("expected a dict");
        }
    }

public:
    static py::Type Type;
    BERTRAND_PYTHON_CONSTRUCTORS(Object, ItemsView, items_check, convert_to_items)

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
    inline bool contains(const Handle& value) const {
        int result = PySequence_Contains(this->ptr(), value.ptr());
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

    using impl::Ops<ItemsView>::operator<;
    using impl::Ops<ItemsView>::operator<=;
    using impl::Ops<ItemsView>::operator==;
    using impl::Ops<ItemsView>::operator!=;
    using impl::Ops<ItemsView>::operator>=;
    using impl::Ops<ItemsView>::operator>;
};


/* Wrapper around pybind11::dict that allows it to be directly initialized using
std::initializer_list and enables extra C API functionality. */
class Dict : public Object, public impl::Ops<Dict> {

    struct DictInit {
        Object key;
        Object value;

        template <typename K, typename V>
        DictInit(K&& key, V&& value) :
            key(detail::object_or_cast(std::forward<K>(key))),
            value(detail::object_or_cast(std::forward<V>(value)))
        {}
    };

    static PyObject* convert_to_dict(PyObject* obj) {
        PyObject* result = PyObject_CallOneArg((PyObject*) &PyDict_Type, obj);
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }

    template <typename T>
    static constexpr bool is_dictlike = (
        std::is_base_of_v<Dict, T> || std::is_base_of_v<pybind11::dict, T>
    );

public:
    static py::Type Type;
    BERTRAND_PYTHON_CONSTRUCTORS(Object, Dict, PyDict_Check, convert_to_dict);

    /* Default constructor.  Initializes to empty dict. */
    inline Dict() : Object(PyDict_New(), stolen_t{}) {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Pack the given arguments into a dictionary using an initializer list. */
    Dict(const std::initializer_list<DictInit>& contents) {
        m_ptr = PyDict_New();
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
        try {
            for (const DictInit& item : contents) {
                if (PyDict_SetItem(m_ptr, item.key.ptr(), item.value.ptr())) {
                    throw error_already_set();
                }
            }
        } catch (...) {
            Py_DECREF(m_ptr);
            throw;
        }
    }

    /* Unpack a generic container into a new dictionary. */
    template <typename T, std::enable_if_t<!is_dictlike<T>, int> = 0>
    explicit Dict(T&& container) {
        m_ptr = PyDict_New();
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
        try {
            if constexpr (detail::is_pyobject<T>::value) {
                for (const Handle& item : container) {
                    if (py::len(item) != 2) {
                        std::ostringstream msg;
                        msg << "expected sequence of length 2 (key, value), not: ";
                        msg << py::repr(item);
                        throw ValueError(msg.str());
                    }
                    auto it = py::iter(item);
                    Handle key = *it;
                    Handle value = *(++it);
                    if (PyDict_SetItem(m_ptr, key.ptr(), value.ptr())) {
                        throw error_already_set();
                    }
                }
            } else {
                for (auto&& [k, v] : container) {
                    if (PyDict_SetItem(
                        m_ptr,
                        detail::object_or_cast(std::forward<decltype(k)>(k)).ptr(),
                        detail::object_or_cast(std::forward<decltype(v)>(v)).ptr()
                    )) {
                        throw error_already_set();
                    }
                }
            }
        } catch (...) {
            Py_DECREF(m_ptr);
            throw;
        }
    }

    /* Unpack a pybind11::dict into a new dictionary directly using the C API. */
    template <typename T, std::enable_if_t<is_dictlike<T>, int> = 0>
    explicit Dict(T&& dict) {
        m_ptr = PyDict_New();
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
        if (PyDict_Merge(m_ptr, dict.ptr(), 1)) {
            Py_DECREF(m_ptr);
            throw error_already_set();
        }
    }

    /* Construct a dictionary using optional keyword arguments, following pybind11
    syntax. */
    template <
        typename... Args,
        typename = std::enable_if_t<pybind11::args_are_all_keyword_or_ds<Args...>()>,
        typename collector = detail::deferred_t<detail::unpacking_collector<>, Args...>
    >
    explicit Dict(Args&&... args) :
        Dict(collector(std::forward<Args>(args)...).kwargs())
    {}

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
    template <typename T, std::enable_if_t<is_dictlike<T>, int> = 0>
    inline void merge(T&& items) {
        if (PyDict_Merge(
            this->ptr(),
            detail::object_or_cast(std::forward<T>(items)).ptr(),
            0
        )) {
            throw error_already_set();
        }
    }

    /* Equivalent to Python `dict.update(items)`, but does not overwrite keys. */
    template <typename T, std::enable_if_t<!is_dictlike<T>, int> = 0>
    inline void merge(T&& items) {
        if (PyDict_MergeFromSeq2(
            this->ptr(),
            detail::object_or_cast(std::forward<T>(items)).ptr(),
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
    template <typename K>
    static inline Dict fromkeys(K&& keys) {
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
    template <typename K>
    static inline Dict fromkeys(const std::initializer_list<K>& keys) {
        PyObject* result = PyDict_New();
        if (result == nullptr) {
            throw error_already_set();
        }
        try {
            for (const K& key : keys) {
                if (PyDict_SetItem(
                    result,
                    detail::object_or_cast(key).ptr(),
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
    inline Dict fromkeys(const std::initializer_list<impl::Initializer>& keys) {
        PyObject* result = PyDict_New();
        if (result == nullptr) {
            throw error_already_set();
        }
        try {
            for (const impl::Initializer& init : keys) {
                if (PyDict_SetItem(
                    result,
                    init.value.ptr(),
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
    template <typename K, typename V>
    static inline Dict fromkeys(K&& keys, V&& value) {
        Object converted = detail::object_or_cast(std::forward<V>(value));
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
    template <typename K, typename V>
    static inline Dict fromkeys(const std::initializer_list<K>& keys, V&& value) {
        Object converted = detail::object_or_cast(std::forward<V>(value));
        PyObject* result = PyDict_New();
        if (result == nullptr) {
            throw error_already_set();
        }
        try {
            for (const K& key : keys) {
                if (PyDict_SetItem(
                    result,
                    detail::object_or_cast(key).ptr(),
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
    inline Dict fromkeys(const std::initializer_list<impl::Initializer>& keys, V&& value) {
        Object converted = detail::object_or_cast(std::forward<V>(value));
        PyObject* result = PyDict_New();
        if (result == nullptr) {
            throw error_already_set();
        }
        try {
            for (const impl::Initializer& init : keys) {
                if (PyDict_SetItem(
                    result,
                    init.value.ptr(),
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
    template <typename K>
    inline Object get(K&& key) const {
        PyObject* result = PyDict_GetItemWithError(
            this->ptr(),
            detail::object_or_cast(std::forward<K>(key)).ptr()
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
    template <typename K, typename V>
    inline Object get(K&& key, V&& default_value) const {
        PyObject* result = PyDict_GetItemWithError(
            this->ptr(),
            detail::object_or_cast(std::forward<K>(key)).ptr()
        );
        if (result == nullptr) {
            if (PyErr_Occurred()) {
                throw error_already_set();
            }
            return detail::object_or_cast(std::forward<V>(default_value));
        }
        return reinterpret_steal<Object>(result);
    }

    /* Equivalent to Python `dict.pop(key)`.  Returns None if the key is not found. */
    template <typename K>
    inline Object pop(K&& key) {
        PyObject* result = PyDict_GetItemWithError(
            this->ptr(),
            detail::object_or_cast(std::forward<K>(key)).ptr()
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
    template <typename K, typename V>
    inline Object pop(K&& key, V&& default_value) {
        PyObject* result = PyDict_GetItemWithError(
            this->ptr(),
            detail::object_or_cast(std::forward<K>(key)).ptr()
        );
        if (result == nullptr) {
            if (PyErr_Occurred()) {
                throw error_already_set();
            }
            return detail::object_or_cast(std::forward<V>(default_value));
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
    template <typename K>
    inline Object setdefault(K&& key) {
        PyObject* result = PyDict_SetDefault(
            this->ptr(),
            detail::object_or_cast(std::forward<K>(key)).ptr(),
            Py_None
        );
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Object>(result);
    }

    /* Equivalent to Python `dict.setdefault(key, default_value)`. */
    template <typename K, typename V>
    inline Object setdefault(K&& key, V&& default_value) {
        PyObject* result = PyDict_SetDefault(
            this->ptr(),
            detail::object_or_cast(std::forward<K>(key)).ptr(),
            detail::object_or_cast(std::forward<V>(default_value)).ptr()
        );
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Object>(result);
    }

    /* Equivalent to Python `dict.update(items)`. */
    template <typename T, std::enable_if_t<is_dictlike<T>, int> = 0>
    inline void update(T&& items) {
        if (PyDict_Merge(this->ptr(), items.ptr(), 1)) {
            throw error_already_set();
        }
    }

    /* Equivalent to Python `dict.update(items)`. */
    template <typename T, std::enable_if_t<!is_dictlike<T>, int> = 0>
    inline void update(T&& items) {
        if constexpr (detail::is_pyobject<T>::value) {
            if (PyDict_MergeFromSeq2(
                this->ptr(),
                detail::object_or_cast(std::forward<T>(items)).ptr(),
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
    inline void update(const std::initializer_list<DictInit>& items) {
        for (const DictInit& item : items) {
            if (PyDict_SetItem(this->ptr(), item.key.ptr(), item.value.ptr())) {
                throw error_already_set();
            }
        }
    }

    /* Equivalent to Python `dict.keys()`. */
    inline KeysView keys() const {
        return this->attr("keys")();
    }

    /* Equivalent to Python `dict.values()`. */
    inline ValuesView values() const {
        return this->attr("values")();
    }

    /* Equivalent to Python `dict.items()`. */
    inline ItemsView items() const {
        return this->attr("items")();
    }

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    inline auto begin() const { return Object::begin(); }
    inline auto end() const { return Object::end(); }

    /* Equivalent to Python `key in dict`. */
    template <typename T>
    inline bool contains(T&& key) const {
        int result = PyDict_Contains(
            this->ptr(),
            detail::object_or_cast(std::forward<T>(key)).ptr()
        );
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

    using impl::Ops<Dict>::operator==;
    using impl::Ops<Dict>::operator!=;

    template <typename T>
    inline Dict operator|(T&& other) const {
        Dict result = copy();
        result.update(std::forward<T>(other));
        return result;
    }

    inline Dict operator|(const std::initializer_list<DictInit>& other) const {
        Dict result = copy();
        result.update(other);
        return result;
    }

    template <typename T>
    inline Dict& operator|=(T&& other) {
        update(std::forward<T>(other));
        return *this;
    }

    inline Dict& operator|=(const std::initializer_list<DictInit>& other) {
        update(other);
        return *this;
    }

};


inline Dict MappingProxy::copy() const{
    return this->attr("copy")();
}

inline KeysView MappingProxy::keys() const{
    return this->attr("keys")();
}

inline ValuesView MappingProxy::values() const{
    return this->attr("values")();
}

template <typename T>
inline bool MappingProxy::contains(T&& key) const {
    return this->keys().contains(detail::object_or_cast(std::forward<T>(key)));
}

template <typename T>
inline Dict operator|(const MappingProxy& mapping, T&& other) {
    return mapping.attr("__or__")(detail::object_or_cast(std::forward<T>(other)));
}

template <typename T>
inline Dict operator&(const MappingProxy& mapping, T&& other) {
    return mapping.attr("__and__")(detail::object_or_cast(std::forward<T>(other)));
}

template <typename T>
inline Dict operator-(const MappingProxy& mapping, T&& other) {
    return mapping.attr("__sub__")(detail::object_or_cast(std::forward<T>(other)));
}

template <typename T>
inline Dict operator^(const MappingProxy& mapping, T&& other) {
    return mapping.attr("__xor__")(detail::object_or_cast(std::forward<T>(other)));
}


/* Get Python's builtin namespace as a dictionary.  This doesn't exist in normal
Python, but makes it much more convenient to interact with generic Python code from
C++. */
inline Dict builtins() {
    PyObject* result = PyEval_GetBuiltins();
    if (result == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<Dict>(result);
}


/* Equivalent to Python `locals()`. */
inline Dict locals() {
    PyObject* result = PyEval_GetLocals();
    if (result == nullptr) {
        throw error_already_set();
    }
    return reinterpret_borrow<Dict>(result);
}


/* Equivalent to Python `vars()`. */
inline Dict vars() {
    return locals();
}


/* Equivalent to Python `vars(object)`. */
inline Dict vars(const pybind11::handle& object) {
    return object.attr("__dict__");
}


}  // namespace python
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_DICT_H
