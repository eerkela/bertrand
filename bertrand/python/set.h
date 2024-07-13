#ifndef BERTRAND_PYTHON_MODULE_GUARD
#error "Internal headers should not be included directly.  Import 'bertrand.python' instead."
#endif

#ifndef BERTRAND_PYTHON_SET_H
#define BERTRAND_PYTHON_SET_H

#include "common.h"


// TODO: check to make sure control structs are properly enabled after CTAD refactor


namespace bertrand {
namespace py {


///////////////////
////    SET    ////
///////////////////


template <typename T, typename Key>
struct __issubclass__<T, Set<Key>>                          : Returns<bool> {
    static constexpr bool generic = std::same_as<Key, Object>;
    template <typename U>
    static constexpr bool check_key_type = std::derived_from<U, Object> ?
        std::derived_from<U, Key> : std::convertible_to<U, Key>;

    static consteval bool operator()(const T&) { return operator()(); }
    static consteval bool operator()() {
        if constexpr (!impl::set_like<T>) {
            return false;
        } else if constexpr (impl::pybind11_like<T>) {
            return generic;
        } else if constexpr (impl::is_iterable<T>) {
            return check_key_type<impl::iter_type<T>>;
        } else {
            return false;
        }
    }
};


template <typename T, typename Key>
struct __isinstance__<T, Set<Key>>                          : Returns<bool> {
    static constexpr bool generic = std::same_as<Key, Object>;
    template <typename U>
    static constexpr bool check_key_type = std::derived_from<U, Object> ?
        std::derived_from<U, Key> : std::convertible_to<U, Key>;

    static constexpr bool operator()(const T& obj) {
        if constexpr (impl::cpp_like<T>) {
            return issubclass<T, Set<Key>>();

        } else if constexpr (impl::is_object_exact<T>) {
            if constexpr (generic) {
                return obj.ptr() != nullptr && PySet_Check(obj.ptr());
            } else {
                return (
                    obj.ptr() != nullptr && PySet_Check(obj.ptr()) &&
                    std::ranges::all_of(obj, [](const auto& item) {
                        return isinstance<Key>(item);
                    })
                );
            }

        } else if constexpr (
            std::derived_from<T, Set<Object>> ||
            std::derived_from<T, pybind11::set>
        ) {
            if constexpr (generic) {
                return obj.ptr() != nullptr;
            } else {
                return (
                    obj.ptr() != nullptr &&
                    std::ranges::all_of(obj, [](const auto& item) {
                        return isinstance<Key>(item);
                    })
                );
            }

        } else if constexpr (impl::set_like<T>) {
            return obj.ptr() != nullptr && check_key_type<impl::iter_type<T>>;

        } else {
            return false;
        }
    }
};


template <typename T>
Set(const std::initializer_list<T>&) -> Set<impl::as_object_t<T>>;
template <impl::is_iterable T>
Set(T) -> Set<impl::as_object_t<impl::iter_type<T>>>;
template <typename T, typename... Args>
    requires (!impl::is_iterable<T> && !impl::str_like<T>)
Set(T, Args...) -> Set<Object>;
template <impl::str_like T>
Set(T) -> Set<Str>;
template <size_t N>
Set(const char(&)[N]) -> Set<Str>;


/* Represents a statically-typed Python set in C++. */
template <typename Key>
class Set : public Object, public impl::SetTag {
    using Base = Object;
    using Self = Set;
    static_assert(
        std::derived_from<Key, Object>,
        "py::Set can only contain types derived from py::Object."
    );
    static_assert(
        impl::is_hashable<Key>,
        "py::Set can only contain hashable types."
    );

    static constexpr bool generic = std::same_as<Key, Object>;

    template <typename T>
    static constexpr bool check_key_type = std::derived_from<T, Object> ?
        std::derived_from<T, Key> : std::convertible_to<T, Key>;

public:
    using impl::SetTag::type;

    using size_type = size_t;
    using key_type = Key;
    using value_type = Key;
    using pointer = value_type*;
    using reference = value_type&;
    using const_pointer = const value_type*;
    using const_reference = const value_type&;
    using iterator = impl::Iterator<impl::GenericIter<value_type>>;
    using const_iterator = impl::Iterator<impl::GenericIter<const value_type>>;
    using reverse_iterator = impl::ReverseIterator<impl::GenericIter<value_type>>;
    using const_reverse_iterator = impl::ReverseIterator<impl::GenericIter<const value_type>>;

    Set(Handle h, borrowed_t t) : Base(h, t) {}
    Set(Handle h, stolen_t t) : Base(h, t) {}

    template <typename... Args>
        requires (
            std::is_invocable_r_v<Set, __init__<Set, std::remove_cvref_t<Args>...>, Args...> &&
            __init__<Set, std::remove_cvref_t<Args>...>::enable
        )
    Set(Args&&... args) : Base((
        Interpreter::init(),
        __init__<Set, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    )) {}

    template <typename... Args>
        requires (
            !__init__<Set, std::remove_cvref_t<Args>...>::enable &&
            std::is_invocable_r_v<Set, __explicit_init__<Set, std::remove_cvref_t<Args>...>, Args...> &&
            __explicit_init__<Set, std::remove_cvref_t<Args>...>::enable
        )
    explicit Set(Args&&... args) : Base((
        Interpreter::init(),
        __explicit_init__<Set, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    )) {}

    /* Pack the contents of a braced initializer list into a new Python set. */
    Set(const std::initializer_list<key_type>& contents) :
        Base((Interpreter::init(), PySet_New(nullptr)), stolen_t{})
    {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
        try {
            for (const key_type& item : contents) {
                if (PySet_Add(m_ptr, item.ptr())) {
                    Exception::from_python();
                }
            }
        } catch (...) {
            Py_DECREF(m_ptr);
            throw;
        }
    }

    /* Equivalent to Python `set.add(key)`. */
    void add(const key_type& key) {
        if (PySet_Add(this->ptr(), key.ptr())) {
            Exception::from_python();
        }
    }

    /* Equivalent to Python `set.remove(key)`. */
    void remove(const key_type& key) {
        int result = PySet_Discard(this->ptr(), key.ptr());
        if (result == -1) {
            Exception::from_python();
        } else if (result == 0) {
            throw KeyError(key);
        }
    }

    /* Equivalent to Python `set.discard(key)`. */
    void discard(const key_type& key) {
        if (PySet_Discard(this->ptr(), key.ptr()) == -1) {
            Exception::from_python();
        }
    }

    /* Equivalent to Python `set.pop()`. */
    key_type pop() {
        PyObject* result = PySet_Pop(this->ptr());
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<key_type>(result);
    }

    /* Equivalent to Python `set.clear()`. */
    void clear() {
        if (PySet_Clear(this->ptr())) {
            Exception::from_python();
        }
    }

    /* Equivalent to Python `set.copy()`. */
    [[nodiscard]] Set copy() const {
        PyObject* result = PySet_New(this->ptr());
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Set>(result);
    }

    /* Equivalent to Python `set.isdisjoint(other)`. */
    template <impl::is_iterable T>
        requires (std::convertible_to<impl::iter_type<T>, key_type>)
    [[nodiscard]] bool isdisjoint(const T& other) const {
        if constexpr (impl::python_like<T>) {
            return impl::call_method<"isdisjoint">(*this, other);
        } else {
            for (const auto& item : other) {
                if (contains(item)) {
                    return false;
                }
            }
            return true;
        }
    }

    /* Equivalent to Python `set.isdisjoint(other)`, where other is given as a
    braced initializer list. */
    [[nodiscard]] bool isdisjoint(const std::initializer_list<key_type>& other) const {
        for (const key_type& item : other) {
            if (contains(item)) {
                return false;
            }
        }
        return true;
    }

    /* Equivalent to Python `set.issubset(other)`. */
    template <impl::is_iterable T>
        requires (std::convertible_to<impl::iter_type<T>, key_type>)
    [[nodiscard]] bool issubset(const T& other) const {
        return impl::call_method<"issubset">(*this, other);
    }

    /* Equivalent to Python `set.issubset(other)`, where other is given as a
    braced initializer list. */
    [[nodiscard]] bool issubset(const std::initializer_list<key_type>& other) const {
        return impl::call_method<"issubset">(*this, other);
    }

    /* Equivalent to Python `set.issuperset(other)`. */
    template <impl::is_iterable T>
        requires (std::convertible_to<impl::iter_type<T>, key_type>)
    [[nodiscard]] bool issuperset(const T& other) const {
        if constexpr (impl::python_like<T>) {
            return impl::call_method<"issuperset">(*this, other);
        } else {
            for (const auto& item : other) {
                if (!contains(item)) {
                    return false;
                }
            }
            return true;
        }
    }

    /* Equivalent to Python `set.issuperset(other)`, where other is given as a
    braced initializer list. */
    [[nodiscard]] bool issuperset(const std::initializer_list<key_type>& other) const {
        for (const key_type& item : other) {
            if (!contains(item)) {
                return false;
            }
        }
        return true;
    }

    /* Equivalent to Python `set.union(*others)`. */
    template <impl::is_iterable... Args>
        requires (std::convertible_to<impl::iter_type<Args>, key_type> && ...)
    [[nodiscard]] Set union_(const Args&... others) const {
        return impl::call_method<"union">(*this, others...);
    }

    /* Equivalent to Python `set.union(other)`, where other is given as a braced
    initializer list. */
    [[nodiscard]] Set union_(const std::initializer_list<key_type>& other) const {
        PyObject* result = PySet_New(this->ptr());
        if (result == nullptr) {
            Exception::from_python();
        }
        try {
            for (const key_type& item : other) {
                if (PySet_Add(result, item.ptr())) {
                    Exception::from_python();
                }
            }
            return reinterpret_steal<Set>(result);
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
    }

    /* Equivalent to Python `set.update(*others)`. */
    template <impl::is_iterable... Args>
        requires (std::convertible_to<impl::iter_type<Args>, key_type> && ...)
    void update(const Args&... others) {
        impl::call_method<"update">(*this, others...);
    }

    /* Equivalent to Python `set.update(<braced initializer list>)`. */
    void update(const std::initializer_list<key_type>& other) {
        for (const key_type& item : other) {
            add(item);
        }
    }

    /* Equivalent to Python `set.intersection(other)`. */
    template <impl::is_iterable... Args>
        requires (std::convertible_to<impl::iter_type<Args>, key_type> && ...)
    [[nodiscard]] Set intersection(const Args&... others) const {
        return impl::call_method<"intersection">(*this, others...);
    }

    /* Equivalent to Python `set.intersection(other)`, where other is given as a
    braced initializer list. */
    [[nodiscard]] Set intersection(const std::initializer_list<key_type>& other) const {
        PyObject* result = PySet_New(nullptr);
        if (result == nullptr) {
            Exception::from_python();
        }
        try {
            for (const key_type& item : other) {
                if (contains(item)) {
                    if (PySet_Add(result, item.ptr())) {
                        Exception::from_python();
                    }
                }
            }
            return reinterpret_steal<Set>(result);
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
    }

    /* Equivalent to Python `set.intersection_update(*others)`. */
    template <impl::is_iterable... Args>
        requires (std::convertible_to<impl::iter_type<Args>, key_type> && ...)
    void intersection_update(const Args&... others) {
        impl::call_method<"intersection_update">(*this, others...);
    }

    /* Equivalent to Python `set.intersection_update(<braced initializer list>)`. */
    void intersection_update(const std::initializer_list<key_type>& other) {
        impl::call_method<"intersection_update">(*this, Set(other));
    }

    /* Equivalent to Python `set.difference(other)`. */
    template <impl::is_iterable... Args>
        requires (std::convertible_to<impl::iter_type<Args>, key_type> && ...)
    [[nodiscard]] Set difference(const Args&... others) const {
        return impl::call_method<"difference">(*this, others...);
    }

    /* Equivalent to Python `set.difference(other)`, where other is given as a
    braced initializer list. */
    [[nodiscard]] Set difference(const std::initializer_list<key_type>& other) const {
        PyObject* result = PySet_New(this->ptr());
        if (result == nullptr) {
            Exception::from_python();
        }
        try {
            for (const key_type& item : other) {
                if (PySet_Discard(result, item.ptr()) == -1) {
                    Exception::from_python();
                }
            }
            return reinterpret_steal<Set>(result);
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
    }

    /* Equivalent to Python `set.difference_update(*others)`. */
    template <impl::is_iterable... Args>
        requires (std::convertible_to<impl::iter_type<Args>, key_type> && ...)
    void difference_update(const Args&... others) {
        impl::call_method<"difference_update">(*this, others...);
    }

    /* Equivalent to Python `set.difference_update(<braced initializer list>)`. */
    void difference_update(const std::initializer_list<key_type>& other) {
        for (const key_type& item : other) {
            discard(item);
        }
    }

    /* Equivalent to Python `set.symmetric_difference(other)`. */
    template <impl::is_iterable T>
        requires (std::convertible_to<impl::iter_type<T>, key_type>)
    [[nodiscard]] Set symmetric_difference(const T& other) const {
        return impl::call_method<"symmetric_difference">(*this, other);
    }

    /* Equivalent to Python `set.symmetric_difference(other)`, where other is given
    as a braced initializer list. */
    [[nodiscard]] Set symmetric_difference(const std::initializer_list<key_type>& other) const {
        PyObject* result = PySet_New(nullptr);
        if (result == nullptr) {
            Exception::from_python();
        }
        try {
            for (const key_type& item : other) {
                if (contains(item)) {
                    if (PySet_Discard(result, item.ptr()) == -1) {
                        Exception::from_python();
                    }
                } else {
                    if (PySet_Add(result, item.ptr())) {
                        Exception::from_python();
                    }
                }
            }
            return reinterpret_steal<Set>(result);
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
    }

    /* Equivalent to Python `set.symmetric_difference_update(other)`. */
    template <impl::is_iterable T>
        requires (std::convertible_to<impl::iter_type<T>, key_type>)
    void symmetric_difference_update(const T& other) {
        impl::call_method<"symmetric_difference_update">(*this, other);
    }

    /* Equivalent to Python `set.symmetric_difference_update(<braced initializer list>)`. */
    void symmetric_difference_update(const std::initializer_list<key_type>& other) {
        for (const key_type& item : other) {
            if (contains(item)) {
                discard(item);
            } else {
                add(item);
            }
        }
    }

    [[nodiscard]] friend Set operator|(
        const Set& self,
        const std::initializer_list<key_type>& other
    ) {
        return self.union_(other);
    }

    friend Set& operator|=(
        Set& self,
        const std::initializer_list<key_type>& other
    ) {
        self.update(other);
        return self;
    }

    [[nodiscard]] friend Set operator&(
        const Set& self,
        const std::initializer_list<key_type>& other
    ) {
        return self.intersection(other);
    }

    friend Set& operator&=(
        Set& self,
        const std::initializer_list<key_type>& other
    ) {
        self.intersection_update(other);
        return self;
    }

    [[nodiscard]] friend Set operator-(
        const Set& self,
        const std::initializer_list<key_type>& other
    ) {
        return self.difference(other);
    }

    friend Set& operator-=(
        Set& self,
        const std::initializer_list<key_type>& other
    ) {
        self.difference_update(other);
        return self;
    }

    [[nodiscard]] friend Set operator^(
        const Set& self,
        const std::initializer_list<key_type>& other
    ) {
        return self.symmetric_difference(other);
    }

    friend Set& operator^=(
        Set& self,
        const std::initializer_list<key_type>& other
    ) {
        self.symmetric_difference_update(other);
        return self;
    }

};


/* Default constructor.  Initializes to an empty set. */
template <typename Key>
struct __init__<Set<Key>>                                   : Returns<Set<Key>> {
    static auto operator()() {
        PyObject* result = PySet_New(nullptr);
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Set<Key>>(result);
    }
};


/* Converting constructor from compatible C++ sets. */
template <typename Key, impl::cpp_like Container>
    requires (
        impl::set_like<Container> &&
        impl::is_iterable<Container> &&
        std::convertible_to<impl::iter_type<Container>, Key>
    )
struct __init__<Set<Key>, Container>                        : Returns<Set<Key>> {
    static auto operator()(const Container& contents) {
        PyObject* result = PySet_New(nullptr);
        if (result == nullptr) {
            Exception::from_python();
        }
        try {
            for (const auto& item : contents) {
                if (PySet_Add(result, Key(item).ptr())) {
                    Exception::from_python();
                }
            }
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
        return reinterpret_steal<Set<Key>>(result);
    }
};


/* Explicitly convert an arbitrary C++ container into a py::Set. */
template <typename Key, impl::cpp_like Container>
    requires (
        !impl::set_like<Container> &&
        impl::is_iterable<Container> &&
        std::convertible_to<impl::iter_type<Container>, Key>
    )
struct __explicit_init__<Set<Key>, Container>               : Returns<Set<Key>> {
    static auto operator()(const Container& contents) {
        PyObject* result = PySet_New(nullptr);
        if (result == nullptr) {
            Exception::from_python();
        }
        try {
            for (const auto& item : contents) {
                if (PySet_Add(result, Key(item).ptr())) {
                    Exception::from_python();
                }
            }
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
        return reinterpret_steal<Set<Key>>(result);
    }
};


/* Explicitly convert an arbitrary Python container into a new py::Set. */
template <typename Key, impl::python_like Container>
    requires (!impl::set_like<Container> && impl::is_iterable<Container>)
struct __explicit_init__<Set<Key>, Container>               : Returns<Set<Key>> {
    static auto operator()(const Container& contents) {
        PyObject* result = PySet_New(contents.ptr());
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Set<Key>>(result);
    }
};


/* Explicitly convert a std::pair into a py::Set. */
template <typename Key, typename First, typename Second>
    requires (
        std::constructible_from<Key, First> &&
        std::constructible_from<Key, Second>
    )
struct __explicit_init__<Set<Key>, std::pair<First, Second>> : Returns<Set<Key>> {
    static auto operator()(const std::pair<First, Second>& pair) {
        PyObject* result = PySet_New(nullptr);
        if (result == nullptr) {
            Exception::from_python();
        }
        try {
            if (PySet_Add(result, Key(pair.first).ptr())) {
                Exception::from_python();
            }
            if (PySet_Add(result, Key(pair.second).ptr())) {
                Exception::from_python();
            }
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
        return reinterpret_steal<Set<Key>>(result);
    }
};


/* Explicitly convert a std::tuple into a py::Set. */
template <typename Key, typename... Args>
    requires (std::constructible_from<Key, Args> && ...)
struct __explicit_init__<Set<Key>, std::tuple<Args...>>     : Returns<Set<Key>> {
    static auto operator()(const std::tuple<Args...>& contents) {
        PyObject* result = PySet_New(nullptr);
        if (result == nullptr) {
            Exception::from_python();
        }

        auto unpack_tuple = [&]<size_t... Ns>(std::index_sequence<Ns...>) {
            auto insert = [](PyObject* m_ptr, const Key& item) {
                if (PySet_Add(m_ptr, item.ptr())) {
                    Exception::from_python();
                }
            };
            (insert(result, Key(std::get<Ns>(contents))), ...);
        };

        try {
            unpack_tuple(std::index_sequence_for<Args...>{});
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
        return reinterpret_steal<Set<Key>>(result);
    }
};


/* Explicitly convert a C++ string literal into a py::Set. */
template <typename Key, size_t N>
    requires (std::convertible_to<const char(&)[1], Key>)
struct __explicit_init__<Set<Key>, char[N]>                 : Returns<Set<Key>> {
    static auto operator()(const char(&string)[N]) {
        PyObject* result = PySet_New(nullptr);
        if (result == nullptr) {
            Exception::from_python();
        }
        try {
            for (size_t i = 0; i < N; ++i) {
                if (PySet_Add(result, Key(string[i]).ptr())) {
                    Exception::from_python();
                }
            }
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
        return reinterpret_steal<Set<Key>>(result);
    }
};


/* Explicitly convert a C++ string pointer into a py::Set. */
template <typename Key>
    requires (std::convertible_to<const char*, Key>)
struct __explicit_init__<Set<Key>, const char*>             : Returns<Set<Key>> {
    static auto operator()(const char* string) {
        PyObject* result = PySet_New(nullptr);
        if (result == nullptr) {
            Exception::from_python();
        }
        try {
            for (const char* ptr = string; *ptr != '\0'; ++ptr) {
                if (PySet_Add(result, Key(ptr).ptr())) {
                    Exception::from_python();
                }
            }
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
        return reinterpret_steal<Set<Key>>(result);
    }
};


/* Construct a new py::Set from a pair of input iterators. */
template <typename Key, typename Iter, std::sentinel_for<Iter> Sentinel>
    requires (std::constructible_from<Key, decltype(*std::declval<Iter>())>)
struct __explicit_init__<Set<Key>, Iter, Sentinel>          : Returns<Set<Key>> {
    static auto operator()(Iter first, Sentinel last) {
        PyObject* result = PySet_New(nullptr);
        if (result == nullptr) {
            Exception::from_python();
        }
        try {
            while (first != last) {
                if (PySet_Add(result, Key(*first).ptr())) {
                    Exception::from_python();
                }
                ++first;
            }
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
        return reinterpret_steal<Set<Key>>(result);
    }
};


template <std::derived_from<impl::SetTag> From, impl::cpp_like To>
    requires (impl::anyset_like<To>)
struct __cast__<From, To> : Returns<To> {
    static auto operator()(const From& from) {
        To result;
        for (const auto& item : from) {
            result.insert(static_cast<typename To::value_type>(item));
        }
        return result;
    }
};


template <std::derived_from<impl::SetTag> Self>
struct __len__<Self>                                        : Returns<size_t> {
    static size_t operator()(const Self& self) {
        return PySet_GET_SIZE(self.ptr());
    }
};


template <
    std::derived_from<impl::SetTag> Self,
    std::convertible_to<typename Self::key_type> Key
>
struct __contains__<Self, Key>                              : Returns<bool> {
    static bool operator()(const Self& self, const Key& key) {
        int result = PySet_Contains(self.ptr(), as_object(key).ptr());
        if (result == -1) {
            Exception::from_python();
        }
        return result;
    }
};


/////////////////////////
////    FROZENSET    ////
/////////////////////////


template <typename T, typename Key>
struct __issubclass__<T, FrozenSet<Key>>                    : Returns<bool> {
    static constexpr bool generic = std::same_as<Key, Object>;
    template <typename U>
    static constexpr bool check_key_type = std::derived_from<U, Object> ?
        std::derived_from<U, Key> : std::convertible_to<U, Key>;

    static consteval bool operator()(const T&) { return operator()(); }
    static consteval bool operator()() {
        if constexpr (!impl::frozenset_like<T>) {
            return false;
        } else if constexpr (impl::pybind11_like<T>) {
            return generic;
        } else if constexpr (impl::is_iterable<T>) {
            return check_key_type<impl::iter_type<T>>;
        } else {
            return false;
        }
    }
};


template <typename T, typename Key>
struct __isinstance__<T, FrozenSet<Key>>                    : Returns<bool> {
    static constexpr bool generic = std::same_as<Key, Object>;
    template <typename U>
    static constexpr bool check_key_type = std::derived_from<U, Object> ?
        std::derived_from<U, Key> : std::convertible_to<U, Key>;

    static constexpr bool operator()(const T& obj) {
        if constexpr (impl::cpp_like<T>) {
            return issubclass<T, FrozenSet<Key>>();

        } else if constexpr (impl::is_object_exact<T>) {
            if constexpr (generic) {
                return obj.ptr() != nullptr && PyFrozenSet_Check(obj.ptr());
            } else {
                return (
                    obj.ptr() != nullptr && PyFrozenSet_Check(obj.ptr()) &&
                    std::ranges::all_of(obj, [](const auto& item) {
                        return isinstance<Key>(item);
                    })
                );
            }

        } else if constexpr (
            std::derived_from<T, FrozenSet<Object>> ||
            std::derived_from<T, pybind11::frozenset>
        ) {
            if constexpr (generic) {
                return obj.ptr() != nullptr;
            } else {
                return (
                    obj.ptr() != nullptr &&
                    std::ranges::all_of(obj, [](const auto& item) {
                        return isinstance<Key>(item);
                    })
                );
            }

        } else if constexpr (impl::frozenset_like<T>) {
            return obj.ptr() != nullptr && check_key_type<impl::iter_type<T>>;

        } else {
            return false;
        }
    }
};


template <typename T>
FrozenSet(const std::initializer_list<T>&) -> FrozenSet<impl::as_object_t<T>>;
template <impl::is_iterable T>
FrozenSet(T) -> FrozenSet<impl::as_object_t<impl::iter_type<T>>>;
template <typename T, typename... Args>
    requires (!impl::is_iterable<T> && !impl::str_like<T>)
FrozenSet(T, Args...) -> FrozenSet<Object>;
template <impl::str_like T>
FrozenSet(T) -> FrozenSet<Str>;
template <size_t N>
FrozenSet(const char(&)[N]) -> FrozenSet<Str>;


/* Represents a statically-typed Python `frozenset` object in C++. */
template <typename Key>
class FrozenSet : public Object, public impl::FrozenSetTag {
    using Base = Object;
    using Self = FrozenSet;
    static_assert(
        std::derived_from<Key, Object>,
        "py::FrozenSet can only contain types derived from py::Object."
    );
    static_assert(
        impl::is_hashable<Key>,
        "py::FrozenSet can only contain hashable types."
    );

    static constexpr bool generic = std::same_as<Key, Object>;

    template <typename T>
    static constexpr bool check_key_type = std::derived_from<T, Object> ?
        std::derived_from<T, Key> : std::convertible_to<T, Key>;

public:
    using impl::FrozenSetTag::type;

    using size_type = size_t;
    using key_type = Key;
    using value_type = Key;
    using pointer = key_type*;
    using reference = key_type&;
    using const_pointer = const key_type*;
    using const_reference = const key_type&;
    using iterator = impl::Iterator<impl::GenericIter<key_type>>;
    using const_iterator = impl::Iterator<impl::GenericIter<const key_type>>;
    using reverse_iterator = impl::ReverseIterator<impl::GenericIter<key_type>>;
    using const_reverse_iterator = impl::ReverseIterator<impl::GenericIter<const key_type>>;

    FrozenSet(Handle h, borrowed_t t) : Base(h, t) {}
    FrozenSet(Handle h, stolen_t t) : Base(h, t) {}

    template <typename... Args>
        requires (
            std::is_invocable_r_v<FrozenSet, __init__<FrozenSet, std::remove_cvref_t<Args>...>, Args...> &&
            __init__<FrozenSet, std::remove_cvref_t<Args>...>::enable
        )
    FrozenSet(Args&&... args) : Base((
        Interpreter::init(),
        __init__<FrozenSet, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    )) {}

    template <typename... Args>
        requires (
            !__init__<FrozenSet, std::remove_cvref_t<Args>...>::enable &&
            std::is_invocable_r_v<FrozenSet, __explicit_init__<FrozenSet, std::remove_cvref_t<Args>...>, Args...> &&
            __explicit_init__<FrozenSet, std::remove_cvref_t<Args>...>::enable
        )
    explicit FrozenSet(Args&&... args) : Base((
        Interpreter::init(),
        __explicit_init__<FrozenSet, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    )) {}

    /* Pack the contents of a braced initializer list into a new Python frozenset. */
    FrozenSet(const std::initializer_list<key_type>& contents) :
        Base((Interpreter::init(), PyFrozenSet_New(nullptr)), stolen_t{})
    {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
        try {
            for (const key_type& item : contents) {
                if (PySet_Add(m_ptr, item.ptr())) {
                    Exception::from_python();
                }
            }
        } catch (...) {
            Py_DECREF(m_ptr);
            throw;
        }
    }

    /* Equivalent to Python `set.copy()`. */
    [[nodiscard]] FrozenSet copy() const {
        PyObject* result = PyFrozenSet_New(this->ptr());
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<FrozenSet>(result);
    }

    /* Equivalent to Python `set.isdisjoint(other)`. */
    template <impl::is_iterable T>
        requires (std::convertible_to<impl::iter_type<T>, key_type>)
    [[nodiscard]] bool isdisjoint(const T& other) const {
        if constexpr (impl::python_like<T>) {
            return impl::call_method<"isdisjoint">(*this, other);
        } else {
            for (const auto& item : other) {
                if (contains(item)) {
                    return false;
                }
            }
            return true;
        }
    }

    /* Equivalent to Python `set.isdisjoint(other)`, where other is given as a
    braced initializer list. */
    [[nodiscard]] bool isdisjoint(const std::initializer_list<key_type>& other) const {
        for (const key_type& item : other) {
            if (contains(item)) {
                return false;
            }
        }
        return true;
    }

    /* Equivalent to Python `set.issubset(other)`. */
    template <impl::is_iterable T>
        requires (std::convertible_to<impl::iter_type<T>, key_type>)
    [[nodiscard]] bool issubset(const T& other) const {
        return impl::call_method<"issubset">(*this, other);
    }

    /* Equivalent to Python `set.issubset(other)`, where other is given as a
    braced initializer list. */
    [[nodiscard]] bool issubset(const std::initializer_list<key_type>& other) const {
        return impl::call_method<"issubset">(*this, other);
    }

    /* Equivalent to Python `set.issuperset(other)`. */
    template <impl::is_iterable T>
        requires (std::convertible_to<impl::iter_type<T>, key_type>)
    [[nodiscard]] bool issuperset(const T& other) const {
        if constexpr (impl::python_like<T>) {
            return impl::call_method<"issuperset">(*this, other);
        } else {
            for (const auto& item : other) {
                if (!contains(item)) {
                    return false;
                }
            }
            return true;
        }
    }

    /* Equivalent to Python `set.issuperset(other)`, where other is given as a
    braced initializer list. */
    [[nodiscard]] bool issuperset(const std::initializer_list<key_type>& other) const {
        for (const key_type& item : other) {
            if (!contains(item)) {
                return false;
            }
        }
        return true;
    }

    /* Equivalent to Python `set.union(*others)`. */
    template <impl::is_iterable... Args>
        requires (std::convertible_to<impl::iter_type<Args>, key_type> && ...)
    [[nodiscard]] FrozenSet union_(const Args&... others) const {
        return impl::call_method<"union">(*this, others...);
    }

    /* Equivalent to Python `set.union(other)`, where other is given as a braced
    initializer list. */
    [[nodiscard]] FrozenSet union_(const std::initializer_list<key_type>& other) const {
        PyObject* result = PyFrozenSet_New(this->ptr());
        if (result == nullptr) {
            Exception::from_python();
        }
        try {
            for (const key_type& item : other) {
                if (PySet_Add(result, item.ptr())) {
                    Exception::from_python();
                }
            }
            return reinterpret_steal<FrozenSet>(result);
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
    }

    /* Equivalent to Python `set.intersection(other)`. */
    template <impl::is_iterable... Args>
        requires (std::convertible_to<impl::iter_type<Args>, key_type> && ...)
    [[nodiscard]] FrozenSet intersection(const Args&... others) const {
        return impl::call_method<"intersection">(*this, others...);
    }

    /* Equivalent to Python `set.intersection(other)`, where other is given as a
    braced initializer list. */
    [[nodiscard]] FrozenSet intersection(const std::initializer_list<key_type>& other) const {
        PyObject* result = PyFrozenSet_New(nullptr);
        if (result == nullptr) {
            Exception::from_python();
        }
        try {
            for (const key_type& item : other) {
                if (contains(item)) {
                    if (PySet_Add(result, item.ptr())) {
                        Exception::from_python();
                    }
                }
            }
            return reinterpret_steal<FrozenSet>(result);
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
    }

    /* Equivalent to Python `set.difference(other)`. */
    template <impl::is_iterable... Args>
        requires (std::convertible_to<impl::iter_type<Args>, key_type> && ...)
    [[nodiscard]] FrozenSet difference(const Args&... others) const {
        return impl::call_method<"difference">(*this, others...);
    }

    /* Equivalent to Python `set.difference(other)`, where other is given as a
    braced initializer list. */
    [[nodiscard]] FrozenSet difference(const std::initializer_list<key_type>& other) const {
        PyObject* result = PyFrozenSet_New(this->ptr());
        if (result == nullptr) {
            Exception::from_python();
        }
        try {
            for (const key_type& item : other) {
                if (PySet_Discard(result, item.ptr()) == -1) {
                    Exception::from_python();
                }
            }
            return reinterpret_steal<FrozenSet>(result);
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
    }

    /* Equivalent to Python `set.symmetric_difference(other)`. */
    template <impl::is_iterable T>
        requires (std::convertible_to<impl::iter_type<T>, key_type>)
    [[nodiscard]] FrozenSet symmetric_difference(const T& other) const {
        return impl::call_method<"symmetric_difference">(*this, other);
    }

    /* Equivalent to Python `set.symmetric_difference(other)`, where other is given
    as a braced initializer list. */
    [[nodiscard]] FrozenSet symmetric_difference(const std::initializer_list<key_type>& other) const {
        PyObject* result = PyFrozenSet_New(nullptr);
        if (result == nullptr) {
            Exception::from_python();
        }
        try {
            for (const key_type& item : other) {
                if (contains(item)) {
                    if (PySet_Discard(result, item.ptr()) == -1) {
                        Exception::from_python();
                    }
                } else {
                    if (PySet_Add(result, item.ptr())) {
                        Exception::from_python();
                    }
                }
            }
            return reinterpret_steal<FrozenSet>(result);
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
    }

    [[nodiscard]] friend FrozenSet operator|(
        const FrozenSet& self,
        const std::initializer_list<key_type>& other
    ) {
        return self.union_(other);
    }

    friend FrozenSet& operator|=(
        FrozenSet& self,
        const std::initializer_list<key_type>& other
    ) {
        self = self.union_(other);
        return self;
    }

    [[nodiscard]] friend FrozenSet operator&(
        const FrozenSet& self,
        const std::initializer_list<key_type>& other
    ) {
        return self.intersection(other);
    }

    friend FrozenSet& operator&=(
        FrozenSet& self,
        const std::initializer_list<key_type>& other
    ) {
        self = self.intersection(other);
        return self;
    }

    [[nodiscard]] friend FrozenSet operator-(
        const FrozenSet& self,
        const std::initializer_list<key_type>& other
    ) {
        return self.difference(other);
    }

    friend FrozenSet& operator-=(
        FrozenSet& self,
        const std::initializer_list<key_type>& other
    ) {
        self = self.difference(other);
        return self;
    }

    [[nodiscard]] friend FrozenSet operator^(
        const FrozenSet& self,
        const std::initializer_list<key_type>& other
    ) {
        return self.symmetric_difference(other);
    }

    friend FrozenSet& operator^=(
        FrozenSet& self,
        const std::initializer_list<key_type>& other
    ) {
        self = self.symmetric_difference(other);
        return self;
    }

};


/* Default constructor.  Initializes to an empty set. */
template <typename Key>
struct __init__<FrozenSet<Key>>                             : Returns<FrozenSet<Key>> {
    static auto operator()() {
        PyObject* result = PyFrozenSet_New(nullptr);
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<FrozenSet<Key>>(result);
    }
};


/* Converting constructor from compatible C++ sets. */
template <typename Key, impl::cpp_like Container>
    requires (
        impl::frozenset_like<Container> &&
        impl::is_iterable<Container> &&
        std::convertible_to<impl::iter_type<Container>, Key>
    )
struct __init__<FrozenSet<Key>, Container>                  : Returns<FrozenSet<Key>> {
    static auto operator()(const Container& contents) {
        PyObject* result = PyFrozenSet_New(nullptr);
        if (result == nullptr) {
            Exception::from_python();
        }
        try {
            for (const auto& item : contents) {
                if (PySet_Add(result, Key(item).ptr())) {
                    Exception::from_python();
                }
            }
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
        return reinterpret_steal<FrozenSet<Key>>(result);
    }
};


/* Explicitly convert an arbitrary C++ container into a py::FrozenSet. */
template <typename Key, impl::cpp_like Container>
    requires (
        !impl::frozenset_like<Container> &&
        impl::is_iterable<Container> &&
        std::convertible_to<impl::iter_type<Container>, Key>
    )
struct __explicit_init__<FrozenSet<Key>, Container>         : Returns<FrozenSet<Key>> {
    static auto operator()(const Container& contents) {
        PyObject* result = PyFrozenSet_New(nullptr);
        if (result == nullptr) {
            Exception::from_python();
        }
        try {
            for (const auto& item : contents) {
                if (PySet_Add(result, Key(item).ptr())) {
                    Exception::from_python();
                }
            }
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
        return reinterpret_steal<FrozenSet<Key>>(result);
    }
};


/* Explicitly convert an arbitrary Python container into a new py::FrozenSet. */
template <typename Key, impl::python_like Container>
    requires (!impl::frozenset_like<Container> && impl::is_iterable<Container>)
struct __explicit_init__<FrozenSet<Key>, Container>         : Returns<FrozenSet<Key>> {
    static auto operator()(const Container& contents) {
        PyObject* result = PyFrozenSet_New(contents.ptr());
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<FrozenSet<Key>>(result);
    }
};


/* Explicitly convert a std::pair into a py::FrozenSet. */
template <typename Key, typename First, typename Second>
    requires (
        std::constructible_from<Key, First> &&
        std::constructible_from<Key, Second>
    )
struct __explicit_init__<FrozenSet<Key>, std::pair<First, Second>> : Returns<FrozenSet<Key>> {
    static auto operator()(const std::pair<First, Second>& pair) {
        PyObject* result = PyFrozenSet_New(nullptr);
        if (result == nullptr) {
            Exception::from_python();
        }
        try {
            if (PySet_Add(result, Key(pair.first).ptr())) {
                Exception::from_python();
            }
            if (PySet_Add(result, Key(pair.second).ptr())) {
                Exception::from_python();
            }
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
        return reinterpret_steal<FrozenSet<Key>>(result);
    }
};


/* Explicitly convert a std::tuple into a py::FrozenSet. */
template <typename Key, typename... Args>
    requires (std::constructible_from<Key, Args> && ...)
struct __explicit_init__<FrozenSet<Key>, std::tuple<Args...>> : Returns<FrozenSet<Key>> {
    static auto operator()(const std::tuple<Args...>& contents) {
        PyObject* result = PyFrozenSet_New(nullptr);
        if (result == nullptr) {
            Exception::from_python();
        }

        auto unpack_tuple = [&]<size_t... Ns>(std::index_sequence<Ns...>) {
            auto insert = [](PyObject* m_ptr, const Key& item) {
                if (PySet_Add(m_ptr, item.ptr())) {
                    Exception::from_python();
                }
            };
            (insert(result, Key(std::get<Ns>(contents))), ...);
        };

        try {
            unpack_tuple(std::index_sequence_for<Args...>{});
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
        return reinterpret_steal<FrozenSet<Key>>(result);
    }
};


/* Explicitly convert a C++ string literal into a py::FrozenSet. */
template <typename Key, size_t N>
    requires (std::convertible_to<const char(&)[1], Key>)
struct __explicit_init__<FrozenSet<Key>, char[N]>           : Returns<FrozenSet<Key>> {
    static auto operator()(const char(&string)[N]) {
        PyObject* result = PyFrozenSet_New(nullptr);
        if (result == nullptr) {
            Exception::from_python();
        }
        try {
            for (size_t i = 0; i < N; ++i) {
                if (PySet_Add(result, Key(string[i]).ptr())) {
                    Exception::from_python();
                }
            }
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
        return reinterpret_steal<FrozenSet<Key>>(result);
    }
};


/* Explicitly convert a C++ string pointer into a py::FrozenSet. */
template <typename Key>
    requires (std::convertible_to<const char*, Key>)
struct __explicit_init__<FrozenSet<Key>, const char*>       : Returns<FrozenSet<Key>> {
    static auto operator()(const char* string) {
        PyObject* result = PyFrozenSet_New(nullptr);
        if (result == nullptr) {
            Exception::from_python();
        }
        try {
            for (const char* ptr = string; *ptr != '\0'; ++ptr) {
                if (PySet_Add(result, Key(ptr).ptr())) {
                    Exception::from_python();
                }
            }
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
        return reinterpret_steal<FrozenSet<Key>>(result);
    }
};


/* Construct a new py::FrozenSet from a pair of input iterators. */
template <typename Key, typename Iter, std::sentinel_for<Iter> Sentinel>
    requires (std::constructible_from<Key, decltype(*std::declval<Iter>())>)
struct __explicit_init__<FrozenSet<Key>, Iter, Sentinel>    : Returns<FrozenSet<Key>> {
    static auto operator()(Iter first, Sentinel last) {
        PyObject* result = PyFrozenSet_New(nullptr);
        if (result == nullptr) {
            Exception::from_python();
        }
        try {
            while (first != last) {
                if (PySet_Add(result, Key(*first).ptr())) {
                    Exception::from_python();
                }
                ++first;
            }
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
        return reinterpret_steal<FrozenSet<Key>>(result);
    }
};


template <std::derived_from<impl::FrozenSetTag> From, impl::cpp_like To>
    requires (impl::anyset_like<To>)
struct __cast__<From, To> : Returns<To> {
    static auto operator()(const From& from) {
        To result;
        for (const auto& item : from) {
            result.insert(static_cast<typename To::value_type>(item));
        }
        return result;
    }
};


template <std::derived_from<impl::FrozenSetTag> Self>
struct __len__<Self>                                        : Returns<size_t> {
    static size_t operator()(const Self& self) {
        return PySet_GET_SIZE(self.ptr());
    }
};


template <
    std::derived_from<impl::FrozenSetTag> Self,
    std::convertible_to<typename Self::key_type> Key
>
struct __contains__<Self, Key>                              : Returns<bool> {
    static bool operator()(const Self& self, const Key& key) {
        int result = PySet_Contains(self.ptr(), as_object(key).ptr());
        if (result == -1) {
            Exception::from_python();
        }
        return result;
    }
};


}  // namespace py
}  // namespace bertrand


#endif
