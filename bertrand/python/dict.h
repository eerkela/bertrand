#ifndef BERTRAND_PYTHON_DICT_H
#define BERTRAND_PYTHON_DICT_H

#include "bertrand/python/common/declarations.h"
#include "common.h"
#include "list.h"
#include "set.h"


// NOTE: MappingProxy is hashable as of Python 3.12
// NOTE: OrderedDict/defaultdict are dict subclasses, so it should be easily supported
// NOTE: ChainMap is complicated, but maybe possible to support as well.


namespace py {


namespace impl {

    /* A replication of Python's internal data format for MappingProxyType objects,
    which allows us to access the underlying dictionary being viewed.  This is
    currently defined in `Objects/descrobject.c`, but is not exported in a public
    header. */
    typedef struct {
        PyObject_HEAD
        PyObject* mapping;
    } mappingproxyobject;

    /* A replication of Python's internal data format for dict view objects, which
    allows us to access the underlying dictionary being viewed.  This is currently
    defined in `Include/internal/pycore_dict.h`, but is not exported in a public
    header. */
    typedef struct {
        PyObject_HEAD
        PyDictObject* dv_dict;
    } _PyDictViewObject;

}


////////////////////
////    KEYS    ////
////////////////////


template <typename T, typename Map>
struct __issubclass__<T, KeyView<Map>>                      : Returns<bool> {
    static consteval bool operator()(const T&) { return operator()(); }
    static consteval bool operator()() {
        if constexpr (std::derived_from<T, impl::KeyTag>) {
            return issubclass<typename T::mapping_type, Map>();
        } else {
            return false;
        }
    }
};


template <typename T, typename Map>
struct __isinstance__<T, KeyView<Map>>                      : Returns<bool> {
    static constexpr bool operator()(const T& obj) {
        if constexpr (impl::cpp_like<T>) {
            return issubclass<T, KeyView<Map>>();

        } else if constexpr (issubclass<T, KeyView<Map>>()) {
            return obj.ptr() != nullptr;

        } else if constexpr (impl::is_object_exact<T>) {
            if (obj.ptr() == nullptr || !PyDictKeys_Check(obj.ptr())) {
                return false;
            }
            PyObject* dict = reinterpret_cast<PyObject*>(
                reinterpret_cast<impl::_PyDictViewObject*>(obj.ptr())->dv_dict
            );
            return isinstance<Map>(reinterpret_borrow<Object>(dict));

        } else {
            return false;
        }
    }
};


/* Represents a statically-typed Python `dict.keys()` object in C++. */
template <typename Map>
class KeyView : public Object, public impl::KeyTag {
    using Base = Object;
    using Self = KeyView;
    static_assert(
        std::derived_from<Map, impl::DictTag>,
        "py::KeyView mapping type must be derived from py::Dict."
    );

public:
    using impl::KeyTag::type;

    using mapping_type = Map;
    using key_type = typename Map::key_type;
    using value_type = key_type;
    using pointer = key_type*;
    using reference = key_type&;
    using const_pointer = const key_type*;
    using const_reference = const key_type&;
    using iterator = impl::Iterator<impl::KeyIter<key_type>>;
    using const_iterator = impl::Iterator<impl::KeyIter<const key_type>>;
    using reverse_iterator = impl::Iterator<impl::GenericIter<key_type>>;
    using const_reverse_iterator = impl::Iterator<impl::GenericIter<const key_type>>;

    KeyView(Handle h, borrowed_t t) : Base(h, t) {}
    KeyView(Handle h, stolen_t t) : Base(h, t) {}

    template <typename... Args>
        requires (
            std::is_invocable_r_v<KeyView, __init__<KeyView, std::remove_cvref_t<Args>...>, Args...> &&
            __init__<KeyView, std::remove_cvref_t<Args>...>::enable
        )
    KeyView(Args&&... args) : Base((
        Interpreter::init(),
        __init__<KeyView, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    )) {}

    template <typename... Args>
        requires (
            !__init__<KeyView, std::remove_cvref_t<Args>...>::enable &&
            std::is_invocable_r_v<KeyView, __explicit_init__<KeyView, std::remove_cvref_t<Args>...>, Args...> &&
            __explicit_init__<KeyView, std::remove_cvref_t<Args>...>::enable
        )
    explicit KeyView(Args&&... args) : Base((
        Interpreter::init(),
        __explicit_init__<KeyView, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    )) {}

    /* Equivalent to Python `dict.keys().mapping`. */
    __declspec(property(get=_get_mapping)) mapping_type mapping;
    [[nodiscard]] mapping_type _get_mapping() const;

    /* Equivalent to Python `dict.keys().isdisjoint(other)`. */
    template <impl::is_iterable T>
        requires (std::convertible_to<impl::iter_type<T>, key_type>)
    [[nodiscard]] bool isdisjoint(const T& other) const {
        return impl::call_method<"isdisjoint">(*this, other);
    }

    /* Equivalent to Python `dict.keys().isdisjoint(<braced initializer list>)`. */
    [[nodiscard]] bool isdisjoint(const std::initializer_list<key_type>& other) const {
        return impl::call_method<"isdisjoint">(*this, other);
    }

    [[nodiscard]] friend Set<key_type> operator|(
        const KeyView& self,
        const std::initializer_list<key_type>& other
    ) {
        PyObject* result = PyNumber_Or(self.ptr(), Set<key_type>(other).ptr());
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Set<key_type>>(result);
    }

    [[nodiscard]] friend Set<key_type> operator&(
        const KeyView& self,
        const std::initializer_list<key_type>& other
    ) {
        PyObject* result = PyNumber_And(self.ptr(), Set<key_type>(other).ptr());
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Set<key_type>>(result);
    }

    [[nodiscard]] friend Set<key_type> operator-(
        const KeyView& self,
        const std::initializer_list<key_type>& other
    ) {
        PyObject* result = PyNumber_Subtract(self.ptr(), Set<key_type>(other).ptr());
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Set<key_type>>(result);
    }

    [[nodiscard]] friend Set<key_type> operator^(
        const KeyView& self,
        const std::initializer_list<key_type>& other
    ) {
        PyObject* result = PyNumber_Xor(self.ptr(), Set<key_type>(other).ptr());
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Set<key_type>>(result);
    }

};


template <typename Map>
struct __explicit_init__<KeyView<Map>, Map>                 : Returns<KeyView<Map>> {
    static auto operator()(const Map& dict) {
        return reinterpret_steal<KeyView<Map>>(
            impl::call_method<"keys">(dict).release()
        );
    }
};


template <std::derived_from<impl::KeyTag> Self>
struct __iter__<Self>                                       : Returns<typename Self::key_type> {
    using iterator_category = std::input_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using value_type = Self::key_type;
    using pointer = value_type*;
    using reference = value_type&;

    Self container;
    Py_ssize_t pos;
    PyObject* key;

    __iter__(const Self& container, int) :
        container(container), pos(0), key(nullptr)
    {
        ++(*this);
    }

    __iter__(Self&& container, int) :
        container(std::move(container)), pos(0), key(nullptr)
    {
        ++(*this);
    }

    __iter__(const Self& container) :
        container(container), pos(-1), key(nullptr)
    {}

    __iter__(Self&& container) :
        container(std::move(container)), pos(-1), key(nullptr)
    {}

    __iter__(const __iter__& other) :
        container(other.container), pos(other.pos), key(other.key)
    {}

    __iter__(__iter__&& other) :
        container(std::move(other.container)), pos(other.pos), key(other.key)
    {
        other.key = nullptr;
    }

    __iter__& operator=(const __iter__& other) {
        if (this != &other) {
            container = other.container;
            pos = other.pos;
            key = other.key;
        }
        return *this;
    }

    __iter__& operator=(__iter__&& other) {
        if (this != &other) {
            container = std::move(other.container);
            pos = other.pos;
            key = other.key;
            other.key = nullptr;
        }
        return *this;
    }

    value_type operator*() const {
        return reinterpret_borrow<value_type>(key);
    }

    pointer operator->() const {
        return &(**this);
    }

    __iter__& operator++() {
        PyObject* dict = reinterpret_cast<PyObject*>(
            reinterpret_cast<impl::_PyDictViewObject*>(ptr(container))->dv_dict
        );
        if (!PyDict_Next(dict, &pos, &key, nullptr)) {
            pos = -1;
        }
        return *this;
    }

    __iter__ operator++(int) {
        __iter__ copy(*this);
        ++(*this);
        return copy;
    }

    bool operator==(const __iter__& other) const {
        return ptr(container) == ptr(other.container) && pos == other.pos;
    }

    bool operator!=(const __iter__& other) const {
        return !(*this == other);
    }

};


template <std::derived_from<impl::KeyTag> Self>
struct __reversed__<Self>                                   : Returns<typename Self::key_type> {};


//////////////////////
////    VALUES    ////
//////////////////////


template <typename T, typename Map>
struct __issubclass__<T, ValueView<Map>>                    : Returns<bool> {
    static consteval bool operator()(const T&) { return operator()(); }
    static consteval bool operator()() {
        if constexpr (std::derived_from<T, impl::ValueTag>) {
            return issubclass<typename T::mapping_type, Map>();
        } else {
            return false;
        }
    }
};


template <typename T, typename Map>
struct __isinstance__<T, ValueView<Map>>                    : Returns<bool> {
    static constexpr bool operator()(const T& obj) {
        if constexpr (impl::cpp_like<T>) {
            return issubclass<T, ValueView<Map>>();

        } else if constexpr (issubclass<T, ValueView<Map>>()) {
            return obj.ptr() != nullptr;

        } else if constexpr (impl::is_object_exact<T>) {
            if (obj.ptr() == nullptr || !PyDictValues_Check(obj.ptr())) {
                return false;
            }
            PyObject* dict = reinterpret_cast<PyObject*>(
                reinterpret_cast<impl::_PyDictViewObject*>(obj.ptr())->dv_dict
            );
            return isinstance<Map>(reinterpret_borrow<Object>(dict));

        } else {
            return false;
        }
    }
};


/* Represents a statically-typed Python `dict.values()` object in C++. */
template <typename Map>
class ValueView : public Object, public impl::ValueTag {
    using Base = Object;
    using Self = ValueView;
    static_assert(
        std::derived_from<Map, impl::DictTag>,
        "py::ValueView mapping type must be derived from py::Dict."
    );

public:
    using impl::ValueTag::type;

    using mapping_type = Map;
    using mapped_type = typename Map::mapped_type;
    using value_type = mapped_type;
    using pointer = value_type*;
    using reference = value_type&;
    using const_pointer = const value_type*;
    using const_reference = const value_type&;
    using iterator = impl::Iterator<impl::ValueIter<value_type>>;
    using const_iterator = impl::Iterator<impl::ValueIter<const value_type>>;
    using reverse_iterator = impl::Iterator<impl::GenericIter<value_type>>;
    using const_reverse_iterator = impl::Iterator<impl::GenericIter<const value_type>>;

    ValueView(Handle h, borrowed_t t) : Base(h, t) {}
    ValueView(Handle h, stolen_t t) : Base(h, t) {}

    template <typename... Args>
        requires (
            std::is_invocable_r_v<ValueView, __init__<ValueView, std::remove_cvref_t<Args>...>, Args...> &&
            __init__<ValueView, std::remove_cvref_t<Args>...>::enable
        )
    ValueView(Args&&... args) : Base((
        Interpreter::init(),
        __init__<ValueView, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    )) {}

    template <typename... Args>
        requires (
            !__init__<ValueView, std::remove_cvref_t<Args>...>::enable &&
            std::is_invocable_r_v<ValueView, __explicit_init__<ValueView, std::remove_cvref_t<Args>...>, Args...> &&
            __explicit_init__<ValueView, std::remove_cvref_t<Args>...>::enable
        )
    explicit ValueView(Args&&... args) : Base((
        Interpreter::init(),
        __explicit_init__<ValueView, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    )) {}

    /* Equivalent to Python `dict.values().mapping`. */
    __declspec(property(get=_get_mapping)) mapping_type mapping;
    [[nodiscard]] mapping_type _get_mapping() const;

};


template <typename Map>
struct __explicit_init__<ValueView<Map>, Map>               : Returns<ValueView<Map>> {
    static auto operator()(const Map& dict) {
        return reinterpret_steal<ValueView<Map>>(
            impl::call_method<"values">(dict).release()
        );
    }
};


template <std::derived_from<impl::ValueTag> Self>
struct __iter__<Self>                                       : Returns<typename Self::mapped_type> {
    using iterator_category = std::input_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using value_type = Self::mapped_type;
    using pointer = value_type*;
    using reference = value_type&;

    Self container;
    Py_ssize_t pos;
    PyObject* value;

    __iter__(const Self& container, int) :
        container(container), pos(0), value(nullptr)
    {
        ++(*this);
    }

    __iter__(Self&& container, int) :
        container(std::move(container)), pos(0), value(nullptr)
    {
        ++(*this);
    }

    __iter__(const Self& container) :
        container(container), pos(-1), value(nullptr)
    {}

    __iter__(Self&& container) :
        container(std::move(container)), pos(-1), value(nullptr)
    {}

    __iter__(const __iter__& other) :
        container(other.container), pos(other.pos), value(other.value)
    {}

    __iter__(__iter__&& other) :
        container(std::move(other.container)), pos(other.pos), value(other.value)
    {
        other.value = nullptr;
    }

    __iter__& operator=(const __iter__& other) {
        if (this != &other) {
            container = other.container;
            pos = other.pos;
            value = other.value;
        }
        return *this;
    }

    __iter__& operator=(__iter__&& other) {
        if (this != &other) {
            container = std::move(other.container);
            pos = other.pos;
            value = other.value;
            other.value = nullptr;
        }
        return *this;
    }

    value_type operator*() const {
        return reinterpret_borrow<value_type>(value);
    }

    pointer operator->() const {
        return &(**this);
    }

    __iter__& operator++() {
        PyObject* dict = reinterpret_cast<PyObject*>(
            reinterpret_cast<impl::_PyDictViewObject*>(ptr(container))->dv_dict
        );
        if (!PyDict_Next(dict, &pos, nullptr, &value)) {
            pos = -1;
        }
        return *this;
    }

    __iter__ operator++(int) {
        __iter__ copy(*this);
        ++(*this);
        return copy;
    }

    bool operator==(const __iter__& other) const {
        return ptr(container) == ptr(other.container) && pos == other.pos;
    }

    bool operator!=(const __iter__& other) const {
        return !(*this == other);
    }

};

template <std::derived_from<impl::ValueTag> Self>
struct __reversed__<Self>                                   : Returns<typename Self::mapped_type> {};



/////////////////////
////    ITEMS    ////
/////////////////////


template <typename T, typename Map>
struct __issubclass__<T, ItemView<Map>>                     : Returns<bool> {
    static consteval bool operator()(const T&) { return operator()(); }
    static consteval bool operator()() {
        if constexpr (std::derived_from<T, impl::ItemTag>) {
            return issubclass<typename T::mapping_type, Map>();
        } else {
            return false;
        }
    }
};


template <typename T, typename Map>
struct __isinstance__<T, ItemView<Map>>                     : Returns<bool> {
    static constexpr bool operator()(const T& obj) {
        if constexpr (impl::cpp_like<T>) {
            return issubclass<T, ItemView<Map>>();

        } else if constexpr (issubclass<T, ItemView<Map>>()) {
            return obj.ptr() != nullptr;

        } else if constexpr (impl::is_object_exact<T>) {
            if (obj.ptr() == nullptr || !PyDictItems_Check(obj.ptr())) {
                return false;
            }
            PyObject* dict = reinterpret_cast<PyObject*>(
                reinterpret_cast<impl::_PyDictViewObject*>(obj.ptr())->dv_dict
            );
            return isinstance<Map>(reinterpret_borrow<Object>(dict));

        } else {
            return false;
        }
    }
};


/* Represents a statically-typed Python `dict.items()` object in C++. */
template <typename Map>
class ItemView : public Object, public impl::ItemTag {
    using Base = Object;
    using Self = ItemView;
    static_assert(
        std::derived_from<Map, impl::DictTag>,
        "py::ItemView mapping type must be derived from py::Dict."
    );

public:
    using impl::ItemTag::type;

    using mapping_type = Map;
    using key_type = typename Map::key_type;
    using mapped_type = typename Map::mapped_type;
    using value_type = std::pair<key_type, mapped_type>;
    using pointer = value_type*;
    using reference = value_type&;
    using const_pointer = const value_type*;
    using const_reference = const value_type&;
    using iterator = impl::Iterator<impl::ItemIter<value_type>>;
    using const_iterator = impl::Iterator<impl::ItemIter<const value_type>>;
    using reverse_iterator = impl::Iterator<impl::GenericIter<value_type>>;
    using const_reverse_iterator = impl::Iterator<impl::GenericIter<const value_type>>;

    ItemView(Handle h, borrowed_t t) : Base(h, t) {}
    ItemView(Handle h, stolen_t t) : Base(h, t) {}

    template <typename... Args>
        requires (
            std::is_invocable_r_v<ItemView, __init__<ItemView, std::remove_cvref_t<Args>...>, Args...> &&
            __init__<ItemView, std::remove_cvref_t<Args>...>::enable
        )
    ItemView(Args&&... args) : Base((
        Interpreter::init(),
        __init__<ItemView, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    )) {}

    template <typename... Args>
        requires (
            !__init__<ItemView, std::remove_cvref_t<Args>...>::enable &&
            std::is_invocable_r_v<ItemView, __explicit_init__<ItemView, std::remove_cvref_t<Args>...>, Args...> &&
            __explicit_init__<ItemView, std::remove_cvref_t<Args>...>::enable
        )
    explicit ItemView(Args&&... args) : Base((
        Interpreter::init(),
        __explicit_init__<ItemView, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    )) {}

    /* Equivalent to Python `dict.items().mapping`. */
    __declspec(property(get=_get_mapping)) mapping_type mapping;
    [[nodiscard]] mapping_type _get_mapping() const;

};


template <typename Map>
struct __explicit_init__<ItemView<Map>, Map>                : Returns<ItemView<Map>> {
    static auto operator()(const Map& dict) {
        return reinterpret_steal<ItemView<Map>>(
            impl::call_method<"items">(dict).release()
        );
    }
};


template <std::derived_from<impl::ItemTag> Self>
struct __iter__<Self>                                       : Returns<std::pair<typename Self::key_type, typename Self::mapped_type>> {
    using iterator_category = std::input_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using value_type = std::pair<typename Self::key_type, typename Self::mapped_type>;
    using pointer = value_type*;
    using reference = value_type&;

    Self container;
    Py_ssize_t pos;
    PyObject* key;
    PyObject* value;

    __iter__(const Self& container, int) :
        container(container), pos(0), key(nullptr), value(nullptr)
    {
        ++(*this);
    }

    __iter__(Self&& container, int) :
        container(std::move(container)), pos(0), key(nullptr), value(nullptr)
    {
        ++(*this);
    }

    __iter__(const Self& container) :
        container(container), pos(-1), key(nullptr), value(nullptr)
    {}

    __iter__(Self&& container) :
        container(std::move(container)), pos(-1), key(nullptr), value(nullptr)
    {}

    __iter__(const __iter__& other) :
        container(other.container), pos(other.pos), key(other.key), value(other.value)
    {}

    __iter__(__iter__&& other) :
        container(std::move(other.container)), pos(other.pos), key(other.key),
        value(other.value)
    {
        other.key = nullptr;
        other.value = nullptr;
    }

    __iter__& operator=(const __iter__& other) {
        if (this != &other) {
            container = other.container;
            pos = other.pos;
            key = other.key;
            value = other.value;
        }
        return *this;
    }

    __iter__& operator=(__iter__&& other) {
        if (this != &other) {
            container = std::move(other.container);
            pos = other.pos;
            key = other.key;
            value = other.value;
            other.key = nullptr;
            other.value = nullptr;
        }
        return *this;
    }

    value_type operator*() const {
        return std::make_pair(
            reinterpret_borrow<typename Self::key_type>(key),
            reinterpret_borrow<typename Self::mapped_type>(value)
        );
    }

    pointer operator->() const {
        return &(**this);
    }

    __iter__& operator++() {
        PyObject* dict = reinterpret_cast<PyObject*>(
            reinterpret_cast<impl::_PyDictViewObject*>(ptr(container))->dv_dict
        );
        if (!PyDict_Next(dict, &pos, &key, &value)) {
            pos = -1;
        }
        return *this;
    }

    __iter__ operator++(int) {
        __iter__ copy(*this);
        ++(*this);
        return copy;
    }

    bool operator==(const __iter__& other) const {
        return ptr(container) == ptr(other.container) && pos == other.pos;
    }

    bool operator!=(const __iter__& other) const {
        return !(*this == other);
    }

};


template <std::derived_from<impl::ItemTag> Self>
struct __reversed__<Self>                                   : Returns<std::pair<typename Self::key_type, typename Self::mapped_type>> {};


////////////////////
////    DICT    ////
////////////////////


template <typename T, typename Key, typename Value>
struct __issubclass__<T, Dict<Key, Value>>                  : Returns<bool> {
    static constexpr bool generic_key = std::same_as<Key, Object>;
    static constexpr bool generic_value = std::same_as<Value, Object>;
    template <typename U>
    static constexpr bool check_key_type = std::derived_from<U, Object> ?
        std::derived_from<U, Key> : std::convertible_to<U, Key>;
    template <typename U>
    static constexpr bool check_value_type = std::derived_from<U, Object> ?
        std::derived_from<U, Value> : std::convertible_to<U, Value>;

    static consteval bool operator()(const T&) { return operator()(); }
    static consteval bool operator()() {
        if constexpr (!impl::dict_like<T>) {
            return false;

        } else if constexpr (std::derived_from<T, impl::DictTag>) {
            return (
                check_key_type<typename T::key_type> &&
                check_value_type<typename T::mapped_type>
            );

        } else if constexpr (impl::is_iterable<T>) {
            using Deref = impl::iter_type<T>;
            if constexpr (impl::pair_like<Deref>) {
                return (
                    check_key_type<decltype(std::declval<Deref>().first)> &&
                    check_value_type<decltype(std::declval<Deref>().second)>
                );
            } else {
                return false;
            }

        } else {
            return false;
        }
    }
};


template <typename T, typename Key, typename Value>
struct __isinstance__<T, Dict<Key, Value>>                  : Returns<bool> {
    static constexpr bool generic_key = std::same_as<Key, Object>;
    static constexpr bool generic_value = std::same_as<Value, Object>;
    template <typename U>
    static constexpr bool check_key_type = std::derived_from<U, Object> ?
        std::derived_from<U, Key> : std::convertible_to<U, Key>;
    template <typename U>
    static constexpr bool check_value_type = std::derived_from<U, Object> ?
        std::derived_from<U, Value> : std::convertible_to<U, Value>;

    template <bool check_key, bool check_value>
    static constexpr bool check_dynamic(PyObject* ptr) {
        PyObject* key;
        PyObject* value;
        Py_ssize_t pos = 0;
        while (PyDict_Next(ptr, &pos, &key, &value)) {
            if constexpr (check_key && check_value) {
                if (
                    !isinstance<Key>(reinterpret_borrow<Object>(key)) ||
                    !isinstance<Value>(reinterpret_borrow<Object>(value))
                ) {
                    return false;
                }
            } else if constexpr (check_key) {
                if (!isinstance<Key>(reinterpret_borrow<Object>(key))) {
                    return false;
                }
            } else if constexpr (check_value) {
                if (!isinstance<Value>(reinterpret_borrow<Object>(value))) {
                    return false;
                }
            } else {
                static_assert(false, "unreachable");
            }
        }
        return true;
    }

    static constexpr bool operator()(const T& obj) {
        if constexpr (impl::cpp_like<T>) {
            return issubclass<T, Dict<Key, Value>>();

        } else if constexpr (impl::is_object_exact<T>) {
            if constexpr (generic_key && generic_value) {
                return obj.ptr() != nullptr && PyDict_Check(obj.ptr());
            } else {
                return obj.ptr() != nullptr && PyDict_Check(obj.ptr()) &&
                    check_dynamic<!generic_key, !generic_value>(obj.ptr());
            }

        } else if constexpr (impl::dict_like<T>) {
            using K = T::key_type;
            using V = T::mapped_type;
            constexpr bool check_key = std::same_as<K, Object> && !generic_key;
            constexpr bool check_value = std::same_as<V, Object> && !generic_value;
            if constexpr (check_key && check_value) {
                return obj.ptr() != nullptr &&
                    check_dynamic<check_key, check_value>(obj.ptr());
            } else if constexpr (check_key) {
                return obj.ptr() != nullptr && check_value_type<V> &&
                    check_dynamic<check_key, check_value>(obj.ptr());
            } else if constexpr (check_value) {
                return obj.ptr() != nullptr && check_key_type<K> &&
                    check_dynamic<check_key, check_value>(obj.ptr());
            } else {
                return obj.ptr() != nullptr && check_key_type<K> && check_value_type<V>;
            }

        } else {
            return false;
        }
    }
};


// NOTE: due to language limitations, it is impossible to correctly deduce the type of
// a nested initializer.  This is solved by explicitly specifying the first element
// to be a std::pair, which allows the types to be deduced using CTAD.  This is also
// the case for std::unordered_map, and there is no way to eliminate it as of now.


template <typename First, typename Second>
Dict(const std::initializer_list<std::pair<First, Second>>&)
    -> Dict<impl::as_object_t<First>, impl::as_object_t<Second>>;

template <typename... Args>
    requires (Function<void(typename Arg<"kwargs", Object>::kwargs)>::template invocable<Args...>)
Dict(Args&&...) -> Dict<Str, Object>;


/* Represents a statically-typed Python dictionary in C++. */
template <typename Key, typename Value>
class Dict : public Object, public impl::DictTag {
    using Base = Object;
    using Self = Dict;
    static_assert(
        std::derived_from<Key, Object>,
        "py::Dict key type must be derived from py::Object."
    );
    static_assert(
        impl::is_hashable<Key>,
        "py::Dict key type must be hashable."
    );
    static_assert(
        std::derived_from<Value, Object>,
        "py::Dict value type must be derived from py::Object."
    );

    static Dict kw_constructor(Arg<"kwargs", Object>::kwargs kwargs) {
        return Dict(kwargs.value());
    }

public:
    using impl::DictTag::type;

    using size_type = size_t;
    using key_type = Key;
    using mapped_type = Value;
    using value_type = key_type;
    using pointer = value_type*;
    using reference = value_type&;
    using const_pointer = const value_type*;
    using const_reference = const value_type&;
    using iterator = impl::Iterator<impl::KeyIter<value_type>>;
    using const_iterator = impl::Iterator<impl::KeyIter<const value_type>>;
    using reverse_iterator = impl::Iterator<impl::GenericIter<value_type>>;
    using const_reverse_iterator = impl::Iterator<impl::GenericIter<const value_type>>;

    Dict(Handle h, borrowed_t t) : Base(h, t) {}
    Dict(Handle h, stolen_t t) : Base(h, t) {}

    template <typename... Args>
        requires (
            std::is_invocable_r_v<Dict, __init__<Dict, std::remove_cvref_t<Args>...>, Args...> &&
            __init__<Dict, std::remove_cvref_t<Args>...>::enable
        )
    Dict(Args&&... args) : Base((
        Interpreter::init(),
        __init__<Dict, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    )) {}

    template <typename... Args>
        requires (
            !__init__<Dict, std::remove_cvref_t<Args>...>::enable &&
            std::is_invocable_r_v<Dict, __explicit_init__<Dict, std::remove_cvref_t<Args>...>, Args...> &&
            __explicit_init__<Dict, std::remove_cvref_t<Args>...>::enable
        )
    explicit Dict(Args&&... args) : Base((
        Interpreter::init(),
        __explicit_init__<Dict, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    )) {}

    /* Pack the given arguments into a dictionary using an initializer list. */
    Dict(const std::initializer_list<std::pair<key_type, mapped_type>>& contents) :
        Base((Interpreter::init(), PyDict_New()), stolen_t{})
    {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
        try {
            for (const std::pair<key_type, mapped_type>& item : contents) {
                if (PyDict_SetItem(
                    m_ptr,
                    item.first.ptr(),
                    item.second.ptr()
                )) {
                    Exception::from_python();
                }
            }
        } catch (...) {
            Py_DECREF(m_ptr);
            throw;
        }
    }

    /* Equivalent to Python `dict.update(items)`, but does not overwrite existing
    keys. */
    void merge(const Dict& items) {
        if (PyDict_Merge(this->ptr(), items.ptr(), 0)) {
            Exception::from_python();
        }
    }

    /* Equivalent to Python `dict.update(items)`, but does not overwrite existing
    keys. */
    template <impl::is_iterable T>
    void merge(const T& items) {
        if (PyDict_MergeFromSeq2(this->ptr(), List(items).ptr(), 0)) {
            Exception::from_python();
        }
    }

    /* Equivalent to Python `dict.clear()`. */
    void clear() { 
        PyDict_Clear(this->ptr());
    }

    /* Equivalent to Python `dict.copy()`. */
    [[nodiscard]] Dict copy() const {
        PyObject* result = PyDict_Copy(this->ptr());
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Dict>(result);
    }

    /* Equivalent to Python `dict.fromkeys(keys, value)`. */
    template <impl::is_iterable T>
        requires (std::convertible_to<impl::iter_type<T>, key_type>)
    [[nodiscard]] static Dict fromkeys(const T& keys, const mapped_type& value) {
        PyObject* result = PyDict_New();
        if (result == nullptr) {
            Exception::from_python();
        }
        try {
            for (const auto& key : keys) {
                if (PyDict_SetItem(
                    result,
                    key_type(key).ptr(),
                    value.ptr()
                )) {
                    Exception::from_python();
                }
            }
            return reinterpret_steal<Dict>(result);
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
    }

    /* Equivalent to Python `dict.fromkeys(<braced initializer list>, value)`. */
    [[nodiscard]] static Dict fromkeys(
        const std::initializer_list<key_type>& keys,
        const mapped_type& value
    ) {
        PyObject* result = PyDict_New();
        if (result == nullptr) {
            Exception::from_python();
        }
        try {
            for (const key_type& init : keys) {
                if (PyDict_SetItem(
                    result,
                    init.ptr(),
                    value.ptr()
                )) {
                    Exception::from_python();
                }
            }
            return reinterpret_steal<Dict>(result);
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
    }

    [[nodiscard]] std::optional<mapped_type> get(const key_type& key) const {
        PyObject* result = PyDict_GetItemWithError(this->ptr(), key.ptr());
        if (result == nullptr) {
            if (PyErr_Occurred()) {
                Exception::from_python();
            }
            return std::nullopt;
        }
        return std::make_optional(reinterpret_steal<mapped_type>(result));
    }

    [[nodiscard]] mapped_type get(const key_type& key, const mapped_type& default_value) const {
        PyObject* result = PyDict_GetItemWithError(this->ptr(), key.ptr());
        if (result == nullptr) {
            if (PyErr_Occurred()) {
                Exception::from_python();
            }
            return default_value;
        }
        return reinterpret_steal<mapped_type>(result);
    }

    std::optional<mapped_type> pop(const key_type& key) {
        PyObject* result = PyDict_GetItemWithError(this->ptr(), key.ptr());
        if (result == nullptr) {
            if (PyErr_Occurred()) {
                Exception::from_python();
            }
            return std::nullopt;
        }
        if (PyDict_DelItem(this->ptr(), result)) {
            Exception::from_python();
        }
        return std::make_optional(reinterpret_steal<mapped_type>(result));
    }

    mapped_type pop(const key_type& key, const mapped_type& default_value) {
        PyObject* result = PyDict_GetItemWithError(this->ptr(), key.ptr());
        if (result == nullptr) {
            if (PyErr_Occurred()) {
                Exception::from_python();
            }
            return default_value;
        }
        if (PyDict_DelItem(this->ptr(), result)) {
            Exception::from_python();
        }
        return reinterpret_steal<mapped_type>(result);
    }

    auto popitem() {
        return impl::call_method<"popitem">(*this);
    }

    mapped_type setdefault(const key_type& key, const mapped_type& default_value) {
        PyObject* result = PyDict_SetDefault(
            this->ptr(),
            key.ptr(),
            default_value.ptr()
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<mapped_type>(result);
    }

    /* Equivalent to Python `dict.update(items)`. */
    void update(const Dict& items) {
        if (PyDict_Merge(this->ptr(), items.ptr(), 1)) {
            Exception::from_python();
        }
    }

    // TODO: check for convertibility to key_type and mapped_type

    // TODO: figure out a way to decompose the items into key/value pairs, including
    // generic objects that might yield iterables of size 2

    /* Equivalent to Python `dict.update(items)`. */
    template <impl::is_iterable T>
    void update(const T& items) {
        if constexpr (impl::python_like<T>) {
            if (PyDict_MergeFromSeq2(
                this->ptr(),
                as_object(items).ptr(),
                1
            )) {
                Exception::from_python();
            }
        } else {
            for (const auto& [k, v] : items) {
                if (PyDict_SetItem(
                    this->ptr(),
                    key_type(k).ptr(),
                    mapped_type(v).ptr()
                )) {
                    Exception::from_python();
                }
            }
        }
    }

    /* Equivalent to Python `dict.update(<braced initializer list>)`. */
    void update(const std::initializer_list<std::pair<key_type, mapped_type>>& items) {
        for (const auto& [k, v] : items) {
            if (PyDict_SetItem(this->ptr(), k.ptr(), v.ptr())) {
                Exception::from_python();
            }
        }
    }

    /* Equivalent to Python `dict.keys()`. */
    [[nodiscard]] auto keys() const {
        return KeyView(*this);
    }

    /* Equivalent to Python `dict.values()`. */
    [[nodiscard]] auto values() const {
        return ValueView(*this);
    }

    /* Equivalent to Python `dict.items()`. */
    [[nodiscard]] auto items() const {
        return ItemView(*this);
    }

    [[nodiscard]] friend Dict operator|(
        const Dict& self,
        const std::initializer_list<std::pair<key_type, mapped_type>>& other
    ) {
        Dict result = self.copy();
        result.update(other);
        return result;
    }

    friend Dict& operator|=(
        Dict& self,
        const std::initializer_list<std::pair<key_type, mapped_type>>& other
    ) {
        self.update(other);
        return self;
    }

};


/* Default constructor.  Initializes to an empty dictionary. */
template <typename Key, typename Value>
struct __init__<Dict<Key, Value>>                           : Returns<Dict<Key, Value>> {
    static auto operator()() {
        PyObject* result = PyDict_New();
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Dict<Key, Value>>(result);
    }
};


/* Converting constructor from compatible C++ dictionaries. */
template <typename Key, typename Value, impl::cpp_like Container>
    requires (
        impl::dict_like<Container> &&
        impl::is_iterable<Container> &&
        impl::pair_like<impl::iter_type<Container>>
        // TODO: check to make sure key is convertible to key, and value to value
    )
struct __init__<Dict<Key, Value>, Container>                : Returns<Dict<Key, Value>> {
    static auto operator()(const Container& contents) {
        PyObject* result = PyDict_New();
        if (result == nullptr) {
            Exception::from_python();
        }
        try {
            for (const auto& [k, v] : contents) {
                if (PyDict_SetItem(result, Key(k).ptr(), Value(v).ptr())) {
                    Exception::from_python();
                }
            }
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
        return reinterpret_steal<Dict<Key, Value>>(result);
    }
};


/* Explicitly convert an arbitrary C++ container into a new py::Dict. */
template <typename Key, typename Value, impl::cpp_like Container>
    requires (
        !impl::dict_like<Container> &&
        impl::is_iterable<Container> &&
        impl::pair_like<impl::iter_type<Container>>
        // TODO: check to make sure key is convertible to key, and value to value
    )
struct __explicit_init__<Dict<Key, Value>, Container>       : Returns<Dict<Key, Value>> {
    static auto operator()(const Container& contents) {
        PyObject* result = PyDict_New();
        if (result == nullptr) {
            Exception::from_python();
        }
        try {
            for (const auto& [k, v] : contents) {
                if (PyDict_SetItem(result, Key(k).ptr(), Value(v).ptr())) {
                    Exception::from_python();
                }
            }
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
        return reinterpret_steal<Dict<Key, Value>>(result);
    }
};


/* Explicitly convert an arbitrary Python container into a new py::Dict. */
template <typename Key, typename Value, impl::python_like Container>
    requires (!impl::dict_like<Container> && impl::is_iterable<Container>)
struct __explicit_init__<Dict<Key, Value>, Container>       : Returns<Dict<Key, Value>> {
    static auto operator()(const Container& contents) {
        PyObject* result = PyObject_CallOneArg((PyObject*) &PyDict_Type, contents.ptr());
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Dict<Key, Value>>(result);
    }
};


/* Construct a new dict from a pair of input iterators. */
template <typename Key, typename Value, typename Iter, std::sentinel_for<Iter> Sentinel>
    // TODO: check to make sure iterator dereferences to compatible key/value pairs
struct __explicit_init__<Dict<Key, Value>, Iter, Sentinel>  : Returns<Dict<Key, Value>> {
    static auto operator()(Iter first, Sentinel last) {
        PyObject* result = PyDict_New();
        if (result == nullptr) {
            Exception::from_python();
        }
        try {
            while (first != last) {
                const auto& [k, v] = *first;
                if (PyDict_SetItem(
                    result,
                    Key(k).ptr(),
                    Value(v).ptr()
                )) {
                    Exception::from_python();
                }
                ++first;
            }
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
        return reinterpret_steal<Dict<Key, Value>>(result);
    }
};


template <std::derived_from<impl::DictTag> From, impl::cpp_like To>
    requires (impl::dict_like<To>)
struct __cast__<From, To> {
    static auto operator()(const From& self) {
        To result;
        PyObject* k;
        PyObject* v;
        Py_ssize_t pos = 0;
        while (PyDict_Next(self.ptr(), &pos, &k, &v)) {
            auto key = reinterpret_borrow<typename From::key_type>(k);
            auto value = reinterpret_borrow<typename From::mapped_type>(v);
            result[impl::implicit_cast<typename To::key_type>(key)] =
                impl::implicit_cast<typename To::mapped_type>(value);
        }
        return result;
    }
};


template <
    std::derived_from<impl::DictTag> Self,
    std::convertible_to<typename Self::key_type> Key
>
struct __contains__<Self, Key>                              : Returns<bool> {
    static bool operator()(const Self& self, const Key& key) {
        int result = PyDict_Contains(self.ptr(), as_object(key).ptr());
        if (result == -1) {
            Exception::from_python();
        }
        return result;
    }
};


template <std::derived_from<impl::DictTag> Self>
struct __len__<Self>                                        : Returns<size_t> {
    static size_t operator()(const Self& self) {
        return PyDict_Size(self.ptr());
    }
};


template <std::derived_from<impl::DictTag> Self>
struct __iter__<Self>                                       : Returns<typename Self::key_type> {
    using Return = Self::key_type;
    static auto begin(const Self& self) {
        return impl::Iterator<impl::KeyIter<Return>>(self);
    }
    static auto end(const Self& self) {
        return impl::Iterator<impl::KeyIter<Return>>();
    }
};


////////////////////////////
////    MAPPINGPROXY    ////
////////////////////////////


template <typename T, typename Map>
struct __issubclass__<T, MappingProxy<Map>>                 : Returns<bool> {
    static consteval bool operator()(const T&) { return operator()(); }
    static consteval bool operator()() {
        if constexpr (std::derived_from<T, impl::MappingProxyTag>) {
            return issubclass<typename T::mapping_type, Map>();
        } else {
            return false;
        }
    }
};


template <typename T, typename Map>
struct __isinstance__<T, MappingProxy<Map>>                 : Returns<bool> {
    static constexpr bool operator()(const T& obj) {
        if constexpr (impl::cpp_like<T>) {
            return issubclass<T, MappingProxy<Map>>();

        } else if constexpr (issubclass<T, MappingProxy<Map>>()) {
            return obj.ptr() != nullptr;

        } else if constexpr (impl::is_object_exact<T>) {
            if (obj.ptr() == nullptr) {
                return false;
            }
            int result = PyObject_IsInstance(
                obj,
                (PyObject*) &PyDictProxy_Type
            );
            if (result == -1) {
                Exception::from_python();
            } else if (result) {
                PyObject* dict = reinterpret_cast<PyObject*>(
                    reinterpret_cast<impl::mappingproxyobject*>(obj.ptr())->mapping
                );
                return isinstance<Map>(reinterpret_borrow<Object>(dict));
            }
            return false;

        } else {
            return false;
        }
    }
};


/* Represents a statically-typed Python `MappingProxyType` object in C++. */
template <typename Map>
class MappingProxy : public Object, public impl::MappingProxyTag {
    using Base = Object;
    using Self = MappingProxy;
    static_assert(
        std::derived_from<Map, Object>,
        "py::MappingProxy mapping type must be derived from py::Object."
    );

    Map unwrap() const {
        PyObject* dict = reinterpret_cast<PyObject*>(
            reinterpret_cast<impl::mappingproxyobject*>(ptr(*this))->mapping
        );
        return reinterpret_borrow<Map>(dict);
    }

public:
    using impl::MappingProxyTag::type;

    using size_type = size_t;
    using mapping_type = Map;
    using key_type = typename Map::key_type;
    using mapped_type = typename Map::mapped_type;
    using value_type = typename Map::value_type;

    MappingProxy(Handle h, borrowed_t t) : Base(h, t) {}
    MappingProxy(Handle h, stolen_t t) : Base(h, t) {}

    template <typename... Args>
        requires (
            std::is_invocable_r_v<MappingProxy, __init__<MappingProxy, std::remove_cvref_t<Args>...>, Args...> &&
            __init__<MappingProxy, std::remove_cvref_t<Args>...>::enable
        )
    MappingProxy(Args&&... args) : Base((
        Interpreter::init(),
        __init__<MappingProxy, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    )) {}

    template <typename... Args>
        requires (
            !__init__<MappingProxy, std::remove_cvref_t<Args>...>::enable &&
            std::is_invocable_r_v<MappingProxy, __explicit_init__<MappingProxy, std::remove_cvref_t<Args>...>, Args...> &&
            __explicit_init__<MappingProxy, std::remove_cvref_t<Args>...>::enable
        )
    explicit MappingProxy(Args&&... args) : Base((
        Interpreter::init(),
        __explicit_init__<MappingProxy, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    )) {}

    /* Equivalent to Python `mappingproxy.copy()`. */
    [[nodiscard]] auto copy() const {
        return unwrap().copy();
    }

    /* Equivalent to Python `mappingproxy.get(key)`. */
    [[nodiscard]] auto get(const key_type& key) const {
        return unwrap().get(key);
    }

    /* Equivalent to Python `mappingproxy.get(key, default)`. */
    [[nodiscard]] auto get(const key_type& key, const mapped_type& default_value) const {
        return unwrap().get(key, default_value);
    }

    /* Equivalent to Python `mappingproxy.keys()`. */
    [[nodiscard]] auto keys() const {
        return unwrap().keys();
    }

    /* Equivalent to Python `mappingproxy.values()`. */
    [[nodiscard]] auto values() const {
        return unwrap().values();
    }

    /* Equivalent to Python `mappingproxy.items()`. */
    [[nodiscard]] auto items() const {
        return unwrap().items();
    }

    [[nodiscard]] friend mapping_type operator|(
        const MappingProxy& self,
        const std::initializer_list<std::pair<key_type, mapped_type>>& other
    ) {
        return self.unwrap() | other;
    }

};


template <typename Map>
struct __explicit_init__<MappingProxy<Map>, Map>            : Returns<MappingProxy<Map>> {
    static auto operator()(const Map& dict) {
        return reinterpret_steal<MappingProxy<Map>>(
            PyDictProxy_New(dict.ptr())
        );
    }
};


template <std::derived_from<impl::MappingProxyTag> Self>
struct __iter__<Self>                                       : Returns<typename Self::key_type> {
    static auto begin(const Self& self) {
        PyObject* dict = reinterpret_cast<PyObject*>(
            reinterpret_cast<impl::mappingproxyobject*>(self.ptr())->mapping
        );
        return py::begin(reinterpret_borrow<typename Self::mapping_type>(dict));
    }
    static auto end(const Self& self) {
        PyObject* dict = reinterpret_cast<PyObject*>(
            reinterpret_cast<impl::mappingproxyobject*>(self.ptr())->mapping
        );
        return py::end(reinterpret_borrow<typename Self::mapping_type>(dict));
    }
};


template <std::derived_from<impl::MappingProxyTag> Self>
struct __reversed__<Self>                                   : Returns<typename Self::key_type> {
    static auto rbegin(const Self& self) {
        PyObject* dict = reinterpret_cast<PyObject*>(
            reinterpret_cast<impl::mappingproxyobject*>(self.ptr())->mapping
        );
        return py::rbegin(reinterpret_borrow<typename Self::mapping_type>(dict));
    }
    static auto rend(const Self& self) {
        PyObject* dict = reinterpret_cast<PyObject*>(
            reinterpret_cast<impl::mappingproxyobject*>(self.ptr())->mapping
        );
        return py::rend(reinterpret_borrow<typename Self::mapping_type>(dict));
    }
};


template <typename Map>
[[nodiscard]] Map KeyView<Map>::_get_mapping() const {
    return getattr<"mapping">(*this);
}


template <typename Map>
[[nodiscard]] Map ValueView<Map>::_get_mapping() const {
    return getattr<"mapping">(*this);
}


template <typename Map>
[[nodiscard]] Map ItemView<Map>::_get_mapping() const {
    return getattr<"mapping">(*this);
}


}  // namespace py


#endif
