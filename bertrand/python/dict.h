#if !defined(BERTRAND_PYTHON_INCLUDED) && !defined(LINTER)
#error "This file should not be included directly.  Please include <bertrand/python.h> instead."
#endif

#ifndef BERTRAND_PYTHON_DICT_H
#define BERTRAND_PYTHON_DICT_H

#include "common.h"
#include "set.h"


// NOTE: MappingProxy is hashable as of Python 3.12
// NOTE: OrderedDict/defaultdict are dict subclasses, so it should be easily supported
// NOTE: ChainMap is complicated, but maybe possible to support as well.


namespace bertrand {
namespace py {


// NOTE: using dedicated Key, Value, and Item iterators (which use PyDict_Next) is
// faster than using a generic iterator, but only by ~10-15% due to reference counting.
// It's not clear if this is worth the extra complexity, especially since using a
// generic iterator would allow key, value, and item views to be used on any dict-like
// object, not just py::Dict.  That being said, we still need to keep the typedefs
// around in order to dynamically check mapping type at runtime, so we might as well
// use them for the iterators as well.


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


template <std::derived_from<impl::KeyTag> Self>
struct __getattr__<Self, "mapping">                             : Returns<MappingProxy<typename Self::mapping_type>> {};
template <std::derived_from<impl::KeyTag> Self>
struct __getattr__<Self, "isdisjoint">                          : Returns<Function<
    Bool(typename Arg<"other", const Object&>::pos)
>> {};


namespace ops {

    template <typename Return, std::derived_from<impl::KeyTag> Self>
    struct begin<Return, Self> {
        static auto operator()(const Self& self) {
            PyObject* dict = reinterpret_cast<PyObject*>(
                reinterpret_cast<impl::_PyDictViewObject*>(self.ptr())->dv_dict
            );
            return impl::Iterator<impl::KeyIter<Return>>(
                reinterpret_borrow<typename Self::mapping_type>(dict)
            );
        };
    };

    template <typename Return, std::derived_from<impl::KeyTag> Self>
    struct end<Return, Self> {
        static auto operator()(const Self& self) {
            return impl::Iterator<impl::KeyIter<Return>>();
        };
    };

}


/* Represents a statically-typed Python `dict.keys()` object in C++. */
template <typename Map>
class KeyView : public Object, public impl::KeyTag {
    using Base = Object;
    static_assert(
        std::derived_from<Map, impl::DictTag>,
        "py::KeyView mapping type must be derived from py::Dict."
    );

public:
    using impl::KeyTag::type;

    using mapping_type = Map;
    using key_type = typename Map::key_type;
    using value_type = key_type;  // awkward naming, but consistent with std::set
    using pointer = key_type*;
    using reference = key_type&;
    using const_pointer = const key_type*;
    using const_reference = const key_type&;
    using iterator = impl::Iterator<impl::KeyIter<key_type>>;
    using const_iterator = impl::Iterator<impl::KeyIter<const key_type>>;
    using reverse_iterator = impl::Iterator<impl::GenericIter<key_type>>;
    using const_reverse_iterator = impl::Iterator<impl::GenericIter<const key_type>>;

    template <typename T>
    [[nodiscard]] static consteval bool typecheck() {
        if constexpr (std::derived_from<std::decay_t<T>, impl::KeyTag>) {
            return mapping_type::template typecheck<typename std::decay_t<T>::mapping_type>();
        } else {
            return false;
        }
    }

    template <typename T>
    [[nodiscard]] static constexpr bool typecheck(const T& obj) {
        if constexpr (impl::cpp_like<T>) {
            return typecheck<T>();

        } else if constexpr (typecheck<T>()) {
            return obj.ptr() != nullptr;

        } else if constexpr (impl::is_object_exact<T>) {
            if (obj.ptr() == nullptr || !PyDictKeys_Check(obj.ptr())) {
                return false;
            }
            PyObject* dict = reinterpret_cast<PyObject*>(
                reinterpret_cast<impl::_PyDictViewObject*>(obj.ptr())->dv_dict
            );
            return mapping_type::typecheck(reinterpret_borrow<Object>(dict));

        } else {
            return false;
        }
    }

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    KeyView(Handle h, const borrowed_t& t) : Base(h, t) {}
    KeyView(Handle h, const stolen_t& t) : Base(h, t) {}

    template <typename Policy>
    KeyView(const pybind11::detail::accessor<Policy>& accessor) :
        Base(Base::from_pybind11_accessor<KeyView>(accessor).release(), stolen_t{})
    {}

    /* Copy/move constructor from equivalent pybind11 type(s) and other key views with
    the same or narrower mapping type. */
    template <impl::python_like T> requires (typecheck<T>())
    KeyView(T&& other) : Base(std::forward<T>(other)) {}

    /* Explicitly create a key view on an existing dictionary. */
    explicit KeyView(const Map& dict);

    ////////////////////////////////
    ////    PYTHON INTERFACE    ////
    ////////////////////////////////

    /* Equivalent to Python `dict.keys().mapping`. */
    [[nodiscard]] MappingProxy<mapping_type> mapping() const;

    /* Equivalent to Python `dict.keys().isdisjoint(other)`. */
    template <impl::is_iterable T>
        requires (std::convertible_to<impl::iter_type<T>, key_type>)
    [[nodiscard]] bool isdisjoint(const T& other) const;

    /* Equivalent to Python `dict.keys().isdisjoint(<braced initializer list>)`. */
    [[nodiscard]] bool isdisjoint(const std::initializer_list<key_type>& other) const;

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

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


//////////////////////
////    VALUES    ////
//////////////////////


template <std::derived_from<impl::ValueTag> Self>
struct __getattr__<Self, "mapping">                             : Returns<MappingProxy<typename Self::mapping_type>> {};


namespace ops {

    template <typename Return, std::derived_from<impl::ValueTag> Self>
    struct begin<Return, Self> {
        static auto operator()(const Self& self) {
            PyObject* dict = reinterpret_cast<PyObject*>(
                reinterpret_cast<impl::_PyDictViewObject*>(self.ptr())->dv_dict
            );
            return impl::Iterator<impl::ValueIter<Return>>(
                reinterpret_borrow<typename Self::mapping_type>(dict)
            );
        };
    };

    template <typename Return, std::derived_from<impl::ValueTag> Self>
    struct end<Return, Self> {
        static auto operator()(const Self& self) {
            return impl::Iterator<impl::ValueIter<Return>>();
        };
    };

}


/* Represents a statically-typed Python `dict.values()` object in C++. */
template <typename Map>
class ValueView : public Object, public impl::ValueTag {
    using Base = Object;
    static_assert(
        std::derived_from<Map, impl::DictTag>,
        "py::ValueView mapping type must be derived from py::Dict."
    );

public:
    using impl::ValueTag::type;

    using mapping_type = Map;
    using value_type = typename Map::value_type;
    using pointer = value_type*;
    using reference = value_type&;
    using const_pointer = const value_type*;
    using const_reference = const value_type&;
    using iterator = impl::Iterator<impl::ValueIter<value_type>>;
    using const_iterator = impl::Iterator<impl::ValueIter<const value_type>>;
    using reverse_iterator = impl::Iterator<impl::GenericIter<value_type>>;
    using const_reverse_iterator = impl::Iterator<impl::GenericIter<const value_type>>;

    template <typename T>
    [[nodiscard]] static consteval bool typecheck() {
        if constexpr (std::derived_from<std::decay_t<T>, impl::ValueTag>) {
            return mapping_type::template typecheck<typename std::decay_t<T>::mapping_type>();
        } else {
            return false;
        }
    }

    template <typename T>
    [[nodiscard]] static constexpr bool typecheck(const T& obj) {
        if constexpr (impl::cpp_like<T>) {
            return typecheck<T>();

        } else if constexpr (typecheck<T>()) {
            return obj.ptr() != nullptr;

        } else if constexpr (impl::is_object_exact<T>) {
            if (obj.ptr() == nullptr || PyDictValues_Check(obj.ptr())) {
                return false;
            }
            PyObject* dict = reinterpret_cast<PyObject*>(
                reinterpret_cast<impl::_PyDictViewObject*>(obj.ptr())->dv_dict
            );
            return mapping_type::typecheck(reinterpret_borrow<Object>(dict));

        } else {
            return false;
        }
    }

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    ValueView(Handle h, const borrowed_t& t) : Base(h, t) {}
    ValueView(Handle h, const stolen_t& t) : Base(h, t) {}

    template <typename Policy>
    ValueView(const pybind11::detail::accessor<Policy>& accessor) :
        Base(Base::from_pybind11_accessor<ValueView>(accessor).release(), stolen_t{})
    {}

    /* Copy/move constructor from equivalent pybind11 type(s) and other value views
    with the same or narrower mapping type. */
    template <impl::python_like T> requires (typecheck<T>())
    ValueView(T&& other) : Base(std::forward<T>(other)) {}

    /* Explicitly create a values view on an existing dictionary. */
    explicit ValueView(const Map& dict);

    ///////////////////////////////
    ////   PYTHON INTERFACE    ////
    ///////////////////////////////

    /* Equivalent to Python `dict.values().mapping`. */
    [[nodiscard]] MappingProxy<mapping_type> mapping() const;

};


/////////////////////
////    ITEMS    ////
/////////////////////


template <std::derived_from<impl::ItemTag> Self>
struct __getattr__<Self, "mapping">                             : Returns<MappingProxy<typename Self::mapping_type>> {};


namespace ops {

    template <typename Return, std::derived_from<impl::ItemTag> Self>
    struct begin<Return, Self> {
        static auto operator()(const Self& self) {
            PyObject* dict = reinterpret_cast<PyObject*>(
                reinterpret_cast<impl::_PyDictViewObject*>(self.ptr())->dv_dict
            );
            return impl::Iterator<impl::ItemIter<Return>>(
                reinterpret_borrow<typename Self::mapping_type>(dict)
            );
        };
    };

    template <typename Return, std::derived_from<impl::ItemTag> Self>
    struct end<Return, Self> {
        static auto operator()(const Self& self) {
            return impl::Iterator<impl::ItemIter<Return>>();
        };
    };


    // TODO: Item iterator can use an internal tuple object that it can reuse to
    // store the key and value, rather than creating a new tuple each time.  This
    // should have no extra overhead over std::pair, and would be more consistent with
    // the Python implementation.  It requires a py::Struct class though.

    // -> This is the exact use case for py::Struct, but py::Struct requires elements
    // from the function refactor (namely py::arg<name, type>)

}


/* Represents a statically-typed Python `dict.items()` object in C++. */
template <typename Map>
class ItemView : public Object, public impl::ItemTag {
    using Base = Object;
    static_assert(
        std::derived_from<Map, impl::DictTag>,
        "py::ItemView mapping type must be derived from py::Dict."
    );

public:
    using impl::ItemTag::type;

    using mapping_type = Map;
    using key_type = typename Map::key_type;
    using value_type = typename Map::value_type;
    using pair = std::pair<key_type, value_type>;
    using const_pair = std::pair<const key_type, const value_type>;
    using pointer = pair*;
    using reference = pair&;
    using const_pointer = const_pair*;
    using const_reference = const_pair&;
    using iterator = impl::Iterator<impl::ItemIter<pair>>;
    using const_iterator = impl::Iterator<impl::ItemIter<const_pair>>;
    using reverse_iterator = impl::Iterator<impl::GenericIter<pair>>;
    using const_reverse_iterator = impl::Iterator<impl::GenericIter<const_pair>>;

    template <typename T>
    [[nodiscard]] static consteval bool typecheck() {
        if constexpr (std::derived_from<std::decay_t<T>, impl::ItemTag>) {
            return mapping_type::template typecheck<typename std::decay_t<T>::mapping_type>();
        } else {
            return false;
        }
    }

    template <typename T>
    [[nodiscard]] static constexpr bool typecheck(const T& obj) {
        if constexpr (impl::cpp_like<T>) {
            return typecheck<T>();

        } else if constexpr (typecheck<T>()) {
            return obj.ptr() != nullptr;

        } else if constexpr (impl::is_object_exact<T>) {
            if (obj.ptr() == nullptr || PyDictItems_Check(obj.ptr())) {
                return false;
            }
            PyObject* dict = reinterpret_cast<PyObject*>(
                reinterpret_cast<impl::_PyDictViewObject*>(obj.ptr())->dv_dict
            );
            return mapping_type::typecheck(reinterpret_borrow<Object>(dict));

        } else {
            return false;
        }
    }

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    ItemView(Handle h, const borrowed_t& t) : Base(h, t) {}
    ItemView(Handle h, const stolen_t& t) : Base(h, t) {}

    template <typename Policy>
    ItemView(const pybind11::detail::accessor<Policy>& accessor) :
        Base(Base::from_pybind11_accessor<ItemView>(accessor).release(), stolen_t{})
    {}

    /* Copy/move constructor from equivalent pybind11 type(s) and other value views
    with the same or narrower mapping type. */
    template <impl::python_like T> requires (typecheck<T>())
    ItemView(T&& other) : Base(std::forward<T>(other)) {}

    /* Explicitly create an items view on an existing dictionary. */
    explicit ItemView(const Map& dict);

    ////////////////////////////////
    ////    PYTHON INTERFACE    ////
    ////////////////////////////////

    /* Equivalent to Python `dict.items().mapping`. */
    [[nodiscard]] MappingProxy<mapping_type> mapping() const;

};


////////////////////
////    DICT    ////
////////////////////


template <std::derived_from<impl::DictTag> Self>
struct __getattr__<Self, "fromkeys">                        : Returns<Function<
    Self(
        typename Arg<"keys", const Object&>::pos,
        typename Arg<"value", const Object&>::pos::opt
    )
>> {};
template <std::derived_from<impl::DictTag> Self>
struct __getattr__<Self, "copy">                            : Returns<Function<
    Self()
>> {};
template <std::derived_from<impl::DictTag> Self>
struct __getattr__<Self, "clear">                           : Returns<Function<
    void()
>> {};
template <std::derived_from<impl::DictTag> Self>
struct __getattr__<Self, "get">                             : Returns<Function<
    typename Self::value_type(
        typename Arg<"key", const Object&>::pos,
        typename Arg<"default", const Object&>::pos::opt
    )
>> {};
template <std::derived_from<impl::DictTag> Self>
struct __getattr__<Self, "pop">                             : Returns<Function<
    typename Self::value_type(
        typename Arg<"key", const Object&>::pos,
        typename Arg<"default", const Object&>::pos::opt
    )
>> {};
template <std::derived_from<impl::DictTag> Self>
struct __getattr__<Self, "popitem">                         : Returns<Function<
    Tuple<Object>()  // TODO: return a struct with defined key and value types instead
>> {};
template <std::derived_from<impl::DictTag> Self>
struct __getattr__<Self, "setdefault">                      : Returns<Function<
    typename Self::value_type(
        typename Arg<"key", const Object&>::pos,
        typename Arg<"default", const Object&>::pos::opt
    )
>> {};
template <std::derived_from<impl::DictTag> Self>
struct __getattr__<Self, "update">                          : Returns<Function<
    void(
        typename Arg<"other", const Object&>::pos,
        typename Arg<"kwargs", const Object&>::kwargs
    )
>> {};
template <std::derived_from<impl::DictTag> Self>
struct __getattr__<Self, "keys">                            : Returns<Function<
    KeyView<Self>()
>> {};
template <std::derived_from<impl::DictTag> Self>
struct __getattr__<Self, "values">                          : Returns<Function<
    ValueView<Self>()
>> {};
template <std::derived_from<impl::DictTag> Self>
struct __getattr__<Self, "items">                           : Returns<Function<
    ItemView<Self>()
>> {};


namespace ops {

    template <typename Return, std::derived_from<impl::DictTag> Self>
    struct len<Return, Self> {
        static size_t operator()(const Self& self) {
            return static_cast<size_t>(PyDict_Size(self.ptr()));
        }
    };

    template <typename Return, std::derived_from<impl::DictTag> Self>
    struct begin<Return, Self> {
        static auto operator()(const Self& self) {
            return impl::Iterator<impl::KeyIter<Return>>(self);
        };
    };

    template <typename Return, std::derived_from<impl::DictTag> Self>
    struct end<Return, Self> {
        static auto operator()(const Self& self) {
            return impl::Iterator<impl::KeyIter<Return>>();
        };
    };

    template <typename Return, std::derived_from<impl::DictTag> Self, typename Key>
    struct contains<Return, Self, Key> {
        static bool operator()(const Self& self, const impl::as_object_t<Key>& key) {
            int result = PyDict_Contains(self.ptr(), key.ptr());
            if (result == -1) {
                Exception::from_python();
            }
            return result;
        }
    };

}


/* Represents a statically-typed Python dictionary in C++. */
template <typename Key, typename Value>
class Dict : public Object, public impl::DictTag {
    using Base = Object;
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

    static constexpr bool generic_key = std::same_as<Key, Object>;
    static constexpr bool generic_value = std::same_as<Value, Object>;

    template <typename T>
    static constexpr bool check_key_type = std::derived_from<T, Object> ?
        std::derived_from<T, Key> : std::convertible_to<T, Key>;
    template <typename T>
    static constexpr bool check_value_type = std::derived_from<T, Object> ?
        std::derived_from<T, Value> : std::convertible_to<T, Value>;

    template <bool check_key, bool check_value>
    static bool dynamic_check(PyObject* ptr) {
        PyObject* key;
        PyObject* value;
        Py_ssize_t pos = 0;
        while (PyDict_Next(ptr, &pos, &key, &value)) {
            if constexpr (check_key && check_value) {
                if (
                    !Key::typecheck(reinterpret_borrow<Object>(key)) ||
                    !Value::typecheck(reinterpret_borrow<Object>(value))
                ) {
                    return false;
                }
            } else if constexpr (check_key) {
                if (!Key::typecheck(reinterpret_borrow<Object>(key))) {
                    return false;
                }
            } else if constexpr (check_value) {
                if (!Value::typecheck(reinterpret_borrow<Object>(value))) {
                    return false;
                }
            } else {
                static_assert(false, "unreachable");
            }
        }
        return true;
    }

public:
    using impl::DictTag::type;

    using size_type = size_t;
    using key_type = Key;
    using mapped_type = Value;
    using value_type = Value;
    using pointer = value_type*;
    using reference = value_type&;
    using const_pointer = const value_type*;
    using const_reference = const value_type&;
    using iterator = impl::Iterator<impl::KeyIter<value_type>>;
    using const_iterator = impl::Iterator<impl::KeyIter<const value_type>>;
    using reverse_iterator = impl::Iterator<impl::GenericIter<value_type>>;
    using const_reverse_iterator = impl::Iterator<impl::GenericIter<const value_type>>;

    // TODO: std::unordered_map reserves key_type for the key type, mapped_type for the
    // value type, and value_type for the pair type.  This is a bit confusing, but
    // we should probably follow the same convention.

    template <typename T>
    [[nodiscard]] static consteval bool typecheck() {
        using U = std::decay_t<T>;
        if constexpr (!impl::dict_like<U>) {
            return false;

        } else if constexpr (impl::pybind11_like<U>) {
            return generic_key && generic_value;

        } else if constexpr (std::derived_from<U, impl::DictTag>) {
            return check_key_type<typename U::key_type> &&
                   check_value_type<typename U::value_type>;

        } else if constexpr (impl::is_iterable<U>) {
            using Deref = impl::iter_type<U>;
            if constexpr (impl::pair_like<Deref>) {
                return check_key_type<decltype(std::declval<Deref>().first)> &&
                       check_value_type<decltype(std::declval<Deref>().second)>;
            } else {
                return false;
            }

        } else {
            return false;
        }
    }

    template <typename T>
    [[nodiscard]] static constexpr bool typecheck(const T& obj) {
        if constexpr (impl::cpp_like<T>) {
            return typecheck<T>();

        } else if constexpr (impl::is_object_exact<T>) {
            if constexpr (generic_key && generic_value) {
                return obj.ptr() != nullptr && PyDict_Check(obj.ptr());
            } else {
                return obj.ptr() != nullptr && PyDict_Check(obj.ptr()) &&
                    dynamic_check<!generic_key, !generic_value>(obj.ptr());
            }

        } else if constexpr (std::derived_from<T, pybind11::dict>) {
            if constexpr (generic_key && generic_value) {
                return obj.ptr() != nullptr;
            } else {
                return obj.ptr() != nullptr &&
                    dynamic_check<!generic_key, !generic_value>(obj.ptr());
            }

        } else if constexpr (impl::dict_like<T>) {
            using K = T::key_type;
            using V = T::value_type;
            constexpr bool check_key = std::same_as<K, Object> && !generic_key;
            constexpr bool check_value = std::same_as<V, Object> && !generic_value;
            if constexpr (check_key && check_value) {
                return obj.ptr() != nullptr &&
                    dynamic_check<check_key, check_value>(obj.ptr());
            } else if constexpr (check_key) {
                return obj.ptr() != nullptr && check_value_type<V> &&
                    dynamic_check<check_key, check_value>(obj.ptr());
            } else if constexpr (check_value) {
                return obj.ptr() != nullptr && check_key_type<K> &&
                    dynamic_check<check_key, check_value>(obj.ptr());
            } else {
                return obj.ptr() != nullptr && check_key_type<K> && check_value_type<V>;
            }

        } else {
            return false;
        }
    }

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    Dict(Handle h, const borrowed_t& t) : Base(h, t) {}
    Dict(Handle h, const stolen_t& t) : Base(h, t) {}

    template <typename Policy>
    Dict(const pybind11::detail::accessor<Policy>& accessor) :
        Base(Base::from_pybind11_accessor<Dict>(accessor).release(), stolen_t{})
    {}

    /* Default constructor.  Initializes to empty dict. */
    Dict() : Base(PyDict_New(), stolen_t{}) {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /* Pack the given arguments into a dictionary using an initializer list. */
    Dict(const std::initializer_list<std::pair<key_type, value_type>>& contents) :
        Base(PyDict_New(), stolen_t{})
    {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
        try {
            for (const std::pair<key_type, value_type>& item : contents) {
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

    /* Copy/move constructors from equivalent pybind11 types or other dicts with a
    narrower key or value type. */
    template <impl::python_like T> requires (typecheck<T>())
    Dict(T&& other) : Base(std::forward<T>(other)) {}

    /* Explicitly unpack an arbitrary Python container into a new py::Dict. */
    template <impl::python_like T> requires (!impl::dict_like<T> && impl::is_iterable<T>)
    explicit Dict(const T& contents) : Base(
        PyObject_CallOneArg((PyObject*) &PyDict_Type, contents.ptr()),
        stolen_t{}
    ) {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /* Explicitly unpack a arbitrary C++ container into a new py::Dict. */
    template <impl::cpp_like T>
        requires (impl::is_iterable<T> && impl::pair_like<impl::iter_type<T>>)
    explicit Dict(const T& container) : Base(PyDict_New(), stolen_t{}) {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
        try {
            for (const auto& [k, v] : container) {
                if (PyDict_SetItem(
                    m_ptr,
                    key_type(k).ptr(),
                    value_type(v).ptr()
                )) {
                    Exception::from_python();
                }
            }
        } catch (...) {
            Py_DECREF(m_ptr);
            throw;
        }
    }

    /* Construct a new dict from a pair of input iterators. */
    template <typename Iter, std::sentinel_for<Iter> Sentinel>
    explicit Dict(Iter first, Sentinel last) : Base(PyDict_New(), stolen_t{}) {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
        try {
            while (first != last) {
                const auto& [k, v] = *first;
                if (PyDict_SetItem(
                    m_ptr,
                    key_type(k).ptr(),
                    value_type(v).ptr()
                )) {
                    Exception::from_python();
                }
                ++first;
            }
        } catch (...) {
            Py_DECREF(m_ptr);
            throw;
        }
    }

    // TODO: properly supporting strict key/value typing for keyword constructor is
    // difficult because of the way pybind11 handles keyword arguments.  I might have
    // to roll my own adapter to handle this properly.  For now, you can only use
    // the keyword constructor on generic dictionaries.

    /* Construct a dictionary using pybind11-style keyword arguments.  This is
    technically superceeded by initializer lists, but it is provided for backwards
    compatibility with Python and pybind11. */
    template <
        typename... Args,
        typename collector = pybind11::detail::deferred_t<
            pybind11::detail::unpacking_collector<>, Args...
        >
    >
        requires (pybind11::args_are_all_keyword_or_ds<Args...>())
    explicit Dict(Args&&... args) :
        Dict(collector(std::forward<Args>(args)...).kwargs())
    {}

    ////////////////////////////////
    ////    PYTHON INTERFACE    ////
    ////////////////////////////////

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
    [[nodiscard]] static Dict fromkeys(const T& keys, const value_type& value) {
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
        const value_type& value
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

    /* Equivalent to Python `dict.get(key)`.  Returns nullopt if the key is not found. */
    [[nodiscard]] std::optional<value_type> get(const key_type& key) const {
        PyObject* result = PyDict_GetItemWithError(this->ptr(), key.ptr());
        if (result == nullptr) {
            if (PyErr_Occurred()) {
                Exception::from_python();
            }
            return std::nullopt;
        }
        return std::make_optional(reinterpret_steal<value_type>(result));
    }

    /* Equivalent to Python `dict.get(key, default_value)`. */
    [[nodiscard]] value_type get(const key_type& key, const value_type& default_value) const {
        PyObject* result = PyDict_GetItemWithError(this->ptr(), key.ptr());
        if (result == nullptr) {
            if (PyErr_Occurred()) {
                Exception::from_python();
            }
            return default_value;
        }
        return reinterpret_steal<value_type>(result);
    }

    /* Equivalent to Python `dict.pop(key)`.  Returns nullopt if the key is not found. */
    std::optional<value_type> pop(const key_type& key) {
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
        return std::make_optional(reinterpret_steal<value_type>(result));
    }

    /* Equivalent to Python `dict.pop(key, default_value)`. */
    value_type pop(const key_type& key, const value_type& default_value) {
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
        return reinterpret_steal<value_type>(result);
    }

    /* Equivalent to Python `dict.popitem()`. */
    value_type popitem();

    /* Equivalent to Python `dict.setdefault(key, default_value)`. */
    value_type setdefault(const key_type& key, const value_type& default_value) {
        PyObject* result = PyDict_SetDefault(
            this->ptr(),
            key.ptr(),
            default_value.ptr()
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<value_type>(result);
    }

    /* Equivalent to Python `dict.update(items)`. */
    void update(const Dict& items) {
        if (PyDict_Merge(this->ptr(), items.ptr(), 1)) {
            Exception::from_python();
        }
    }

    // TODO: check for convertibility to key_type and value_type

    // TODO: figure out a way to decompose the items into key/value pairs, including
    // generic objects that might yield iterables of size 2

    /* Equivalent to Python `dict.update(items)`. */
    template <impl::is_iterable T>
    void update(const T& items) {
        if constexpr (impl::python_like<T>) {
            if (PyDict_MergeFromSeq2(
                this->ptr(),
                Object(items).ptr(),  // ???
                1
            )) {
                Exception::from_python();
            }
        } else {
            for (const auto& [k, v] : items) {
                if (PyDict_SetItem(
                    this->ptr(),
                    key_type(k).ptr(),
                    value_type(v).ptr()
                )) {
                    Exception::from_python();
                }
            }
        }
    }

    /* Equivalent to Python `dict.update(<braced initializer list>)`. */
    void update(const std::initializer_list<std::pair<key_type, value_type>>& items) {
        for (const auto& [k, v] : items) {
            if (PyDict_SetItem(this->ptr(), k.ptr(), v.ptr())) {
                Exception::from_python();
            }
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

    /////////////////////
    ////    VIEWS    ////
    /////////////////////

    /* Equivalent to Python `dict.keys()`. */
    [[nodiscard]] KeyView<Dict> keys() const;

    /* Equivalent to Python `dict.values()`. */
    [[nodiscard]] ValueView<Dict> values() const;

    /* Equivalent to Python `dict.items()`. */
    [[nodiscard]] ItemView<Dict> items() const;

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    [[nodiscard]] friend Dict operator|(
        const Dict& self,
        const std::initializer_list<std::pair<key_type, value_type>>& other
    ) {
        Dict result = self.copy();
        result.update(other);
        return result;
    }

    friend Dict& operator|=(
        Dict& self,
        const std::initializer_list<std::pair<key_type, value_type>>& other
    ) {
        self.update(other);
        return self;
    }

};


////////////////////////////
////    MAPPINGPROXY    ////
////////////////////////////


template <std::derived_from<impl::MappingProxyTag> Self>
struct __getattr__<Self, "copy">                                : Returns<Function<
    typename Self::mapping_type()
>> {};
template <std::derived_from<impl::MappingProxyTag> Self>
struct __getattr__<Self, "get">                                 : Returns<Function<
    typename Self::value_type(
        typename Arg<"key", const Object&>::pos,
        typename Arg<"default", const Object&>::pos::opt
    )
>> {};
template <std::derived_from<impl::MappingProxyTag> Self>
struct __getattr__<Self, "keys">                                : Returns<Function<
    KeyView<typename Self::mapping_type>()
>> {};
template <std::derived_from<impl::MappingProxyTag> Self>
struct __getattr__<Self, "values">                              : Returns<Function<
    ValueView<typename Self::mapping_type>()
>> {};
template <std::derived_from<impl::MappingProxyTag> Self>
struct __getattr__<Self, "items">                               : Returns<Function<
    ItemView<typename Self::mapping_type>()
>> {};


namespace ops {

    template <typename Return, std::derived_from<impl::MappingProxyTag> Self>
    struct len<Return, Self> {
        static size_t operator()(const Self& self) {
            PyObject* dict = reinterpret_cast<PyObject*>(
                reinterpret_cast<impl::mappingproxyobject*>(self.ptr())->mapping
            );
            return len<Return, typename Self::mapping_type>::operator()(
                reinterpret_borrow<typename Self::mapping_type>(dict)
            );
        }
    };

    template <typename Return, std::derived_from<impl::MappingProxyTag> Self>
    struct begin<Return, Self> {
        static auto operator()(const Self& self) {
            PyObject* dict = reinterpret_cast<PyObject*>(
                reinterpret_cast<impl::mappingproxyobject*>(self.ptr())->mapping
            );
            return begin<Return, typename Self::mapping_type>::operator()(
                reinterpret_borrow<typename Self::mapping_type>(dict)
            );
        };
    };

    template <typename Return, std::derived_from<impl::MappingProxyTag> Self>
    struct end<Return, Self> {
        static auto operator()(const Self& self) {
            PyObject* dict = reinterpret_cast<PyObject*>(
                reinterpret_cast<impl::mappingproxyobject*>(self.ptr())->mapping
            );
            return end<Return, typename Self::mapping_type>::operator()(
                reinterpret_borrow<typename Self::mapping_type>(dict)
            );
        };
    };

    template <typename Return, std::derived_from<impl::MappingProxyTag> Self>
    struct rbegin<Return, Self> {
        static auto operator()(const Self& self) {
            PyObject* dict = reinterpret_cast<PyObject*>(
                reinterpret_cast<impl::mappingproxyobject*>(self.ptr())->mapping
            );
            return rbegin<Return, typename Self::mapping_type>::operator()(
                reinterpret_borrow<typename Self::mapping_type>(dict)
            );
        };
    };

    template <typename Return, std::derived_from<impl::MappingProxyTag> Self>
    struct rend<Return, Self> {
        static auto operator()(const Self& self) {
            PyObject* dict = reinterpret_cast<PyObject*>(
                reinterpret_cast<impl::mappingproxyobject*>(self.ptr())->mapping
            );
            return rend<Return, typename Self::mapping_type>::operator()(
                reinterpret_borrow<typename Self::mapping_type>(dict)
            );
        };
    };

    template <typename Return, std::derived_from<impl::MappingProxyTag> Self, typename Key>
    struct contains<Return, Self, Key> {
        static bool operator()(const Self& self, const Key& key) {
            PyObject* dict = reinterpret_cast<PyObject*>(
                reinterpret_cast<impl::mappingproxyobject*>(self.ptr())->mapping
            );
            return contains<Return, typename Self::mapping_type, Key>::operator()(
                reinterpret_borrow<typename Self::mapping_type>(dict),
                key
            );
        }
    };

}


/* Represents a statically-typed Python `MappingProxyType` object in C++. */
template <typename Map>
class MappingProxy : public Object, public impl::MappingProxyTag {
    using Base = Object;
    static_assert(
        std::derived_from<Map, Object>,
        "py::MappingProxy mapping type must be derived from py::Object."
    );

    Map unwrap() const {
        PyObject* dict = reinterpret_cast<PyObject*>(
            reinterpret_cast<impl::mappingproxyobject*>(m_ptr)->mapping
        );
        return reinterpret_borrow<Map>(dict);
    }

public:
    using impl::MappingProxyTag::type;

    using size_type = size_t;
    using mapping_type = Map;
    using key_type = typename Map::key_type;
    using value_type = typename Map::value_type;

    template <typename T>
    [[nodiscard]] static consteval bool typecheck() {
        if constexpr (std::derived_from<std::decay_t<T>, impl::MappingProxyTag>) {
            return mapping_type::template typecheck<typename std::decay_t<T>::mapping_type>();
        } else {
            return false;
        }
    }

    template <typename T>
    [[nodiscard]] static constexpr bool typecheck(const T& obj) {
        if constexpr (impl::cpp_like<T>) {
            return typecheck<T>();

        } else if constexpr (typecheck<T>()) {
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
                return mapping_type::typecheck(reinterpret_borrow<Object>(dict));
            }
            return false;

        } else {
            return false;
        }
    }

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    MappingProxy(Handle h, const borrowed_t& t) : Base(h, t) {}
    MappingProxy(Handle h, const stolen_t& t) : Base(h, t) {}

    template <typename Policy>
    MappingProxy(const pybind11::detail::accessor<Policy>& accessor) :
        Base(Base::from_pybind11_accessor<MappingProxy>(accessor).release(), stolen_t{})
    {}

    /* Copy/move constructor from equivalent pybind11 type(s) and other proxies with
    the same or narrower mapping type. */
    template <impl::python_like T> requires (typecheck<T>())
    MappingProxy(T&& other) : Base(std::forward<T>(other)) {}

    /* Construct a read-only view on an existing dictionary. */
    explicit MappingProxy(const Map& dict) : Base(PyDictProxy_New(dict.ptr()), stolen_t{}) {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    ////////////////////////////////
    ////    PYTHON INTERFACE    ////
    ////////////////////////////////

    /* Equivalent to Python `mappingproxy.copy()`. */
    [[nodiscard]] auto copy() const {
        return unwrap().copy();
    }

    /* Equivalent to Python `mappingproxy.get(key)`. */
    [[nodiscard]] auto get(const key_type& key) const {
        return unwrap().get(key);
    }

    /* Equivalent to Python `mappingproxy.get(key, default)`. */
    [[nodiscard]] auto get(const key_type& key, const value_type& default_value) const {
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

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    [[nodiscard]] friend mapping_type operator|(
        const MappingProxy& self,
        const std::initializer_list<std::pair<key_type, value_type>>& other
    ) {
        return self.unwrap() | other;
    }

};


template <typename Map>
[[nodiscard]] MappingProxy<Map> KeyView<Map>::mapping() const {
    return attr<"mapping">();
}


template <typename Map>
[[nodiscard]] MappingProxy<Map> ValueView<Map>::mapping() const {
    return attr<"mapping">();
}


template <typename Map>
[[nodiscard]] MappingProxy<Map> ItemView<Map>::mapping() const {
    return attr<"mapping">();
}


}  // namespace py
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_DICT_H
