#ifndef BERTRAND_PYTHON_CORE_OPS_H
#define BERTRAND_PYTHON_CORE_OPS_H

#include "declarations.h"
#include "object.h"


namespace std {

    template <bertrand::meta::python T>
        requires (bertrand::__hash__<T>::enable && (
            std::is_invocable_r_v<size_t, bertrand::__hash__<T>, T> ||
            (
                !std::is_invocable_v<bertrand::__hash__<T>, T> &&
                bertrand::meta::has_cpp<T> &&
                bertrand::meta::hashable<bertrand::meta::cpp_type<T>>
            ) || (
                !std::is_invocable_v<bertrand::__hash__<T>, T> &&
                !bertrand::meta::has_cpp<T>
            )
        ))
    struct hash<T> {
        static constexpr size_t operator()(T obj) {
            if constexpr (std::is_invocable_v<bertrand::__hash__<T>, T>) {
                return bertrand::__hash__<T>{}(std::forward<T>(obj));

            } else if constexpr (bertrand::meta::has_cpp<T>) {
                return std::hash<bertrand::meta::cpp_type<std::remove_cvref_t<T>>>{}(
                    from_python(std::forward<T>(obj))
                );

            } else {
                Py_hash_t result = PyObject_Hash(bertrand::ptr(
                    bertrand::to_python(std::forward<T>(obj))
                ));
                if (result == -1 && PyErr_Occurred()) {
                    bertrand::Exception::from_python();
                }
                return result;
            }
        }
    };

};  // namespace std


namespace bertrand {


/// TODO: replace std::derived_from<> with better concepts from meta:: namespace

/// TODO: add assertions where appropriate to ensure that no object is ever null.


/////////////////////////////////////
////    ATTRIBUTE/ITEM ACCESS    ////
/////////////////////////////////////


/* Replicates Python's `del` keyword for attribute and item deletion.  Note that the
usage of `del` to dereference naked Python objects is not supported - only those uses
which would translate to a `PyObject_DelAttr()` or `PyObject_DelItem()` are considered
valid. */
template <typename Self, static_str Name>
    requires (
        __delattr__<Self, Name>::enable &&
        std::is_void_v<typename __delattr__<Self, Name>::type> && (
            std::is_invocable_r_v<void, __delattr__<Self, Name>, Self> ||
            !std::is_invocable_v<__delattr__<Self, Name>, Self>
        )
    )
void del(impl::Attr<Self, Name>&& attr);


/* Replicates Python's `del` keyword for attribute and item deletion.  Note that the
usage of `del` to dereference naked Python objects is not supported - only those uses
which would translate to a `PyObject_DelAttr()` or `PyObject_DelItem()` are considered
valid. */
template <typename Self, typename... Key>
    requires (
        __delitem__<Self, Key...>::enable &&
        std::is_void_v<typename __delitem__<Self, Key...>::type> && (
            std::is_invocable_r_v<void, __delitem__<Self, Key...>, Self, Key...> ||
            !std::is_invocable_v<__delitem__<Self, Key...>, Self, Key...>
        )
    )
void del(impl::Item<Self, Key...>&& item);


namespace impl {

    /* A global map storing cached Python strings for the `template_string<"name">`
    accessor.  This avoids the overhead of repeatedly creating identical strings for
    attribute lookups, and replaces it with a simple, 2-level hash lookup with proper
    per-interpreter state. */
    inline std::unordered_map<
        PyInterpreterState*,
        std::unordered_map<const char*, Object>
    > template_strings;

    /* Convert a compile-time string into a Python unicode object. */
    template <static_str name>
    const Object& template_string() {
        auto table = template_strings.find(PyInterpreterState_Get());
        if (table == template_strings.end()) {
            throw AssertionError(
                "no template string table found for the current Python interpreter"
            );
        }
        auto it = table->second.find(name);
        if (it != table->second.end()) {
            return it->second;
        }
        PyObject* result = PyUnicode_FromStringAndSize(name, name.size());
        if (result == nullptr) {
            Exception::from_python();
        }
        return table->second.emplace(
            name,
            steal<Object>(result)
        ).first->second;        
    }

    /* A proxy for the result of an attribute lookup that is controlled by the
    `__getattr__`, `__setattr__`, and `__delattr__` control structs.

    This is a simple extension of an Object type that intercepts `operator=` and
    assigns the new value back to the attribute using the appropriate API.  Mutating
    the object in any other way will also modify it in-place on the parent. */
    template <typename Self, static_str Name>
        requires (
            __getattr__<Self, Name>::enable &&
            std::derived_from<typename __getattr__<Self, Name>::type, Object> && (
                !std::is_invocable_v<__getattr__<Self, Name>, Self> ||
                std::is_invocable_r_v<
                    typename __getattr__<Self, Name>::type,
                    __getattr__<Self, Name>,
                    Self
                >
            )
        )
    struct Attr : std::remove_cv_t<typename __getattr__<Self, Name>::type> {
    private:
        using Base = std::remove_cv_t<typename __getattr__<Self, Name>::type>;

        template <typename S, static_str N>
            requires (
                __delattr__<S, N>::enable &&
                std::is_void_v<typename __delattr__<S, N>::type> && (
                    std::is_invocable_r_v<void, __delattr__<S, N>, S> ||
                    !std::is_invocable_v<__delattr__<S, N>, S>
                )
            )
        friend void bertrand::del(Attr<S, N>&& item);
        template <meta::python T>
        friend PyObject* bertrand::ptr(T&&);
        template <meta::python T> requires (!meta::is_const<T>)
        friend PyObject* bertrand::release(T&&);
        template <meta::has_python T> requires (!meta::is_qualified<T>)
        friend obj<T> bertrand::borrow(PyObject*);
        template <meta::has_python T> requires (!meta::is_qualified<T>)
        friend obj<T> bertrand::steal(PyObject*);
        template <meta::python T> requires (meta::has_cpp<T>)
        friend auto& impl::unwrap(T& obj);
        template <meta::python T> requires (meta::has_cpp<T>)
        friend const auto& impl::unwrap(const T& obj);

        /* m_self inherits the same const/volatile/reference qualifiers as the original
        object. */
        Self m_self;

        /* The wrapper's `m_ptr` member is lazily evaluated to avoid repeated lookups.
        Replacing it with a computed property will trigger a __getattr__ lookup the
        first time it is accessed. */
        __declspec(property(get = _get_ptr, put = _set_ptr)) PyObject* m_ptr;
        void _set_ptr(PyObject* value) { Base::m_ptr = value; }
        PyObject* _get_ptr() {
            if (Base::m_ptr == nullptr) {
                if constexpr (std::is_invocable_v<__getattr__<Self, Name>, Self>) {
                    Base::m_ptr = release(__getattr__<Self, Name>{}(
                        std::forward<Self>(m_self))
                    );
                } else {
                    PyObject* name = PyUnicode_FromStringAndSize(Name, Name.size());
                    if (name == nullptr) {
                        Exception::from_python();
                    }
                    PyObject* result = PyObject_GetAttr(ptr(m_self), name);
                    Py_DECREF(name);
                    if (result == nullptr) {
                        Exception::from_python();
                    }
                    Base::m_ptr = result;
                }
            }
            return Base::m_ptr;
        }

    public:

        Attr(Self&& self) :
            Base(nullptr, Object::stolen_t{}), m_self(std::forward<Self>(self))
        {}
        Attr(const Attr& other) = delete;
        Attr(Attr&& other) = delete;

        template <typename Value> requires (!__setattr__<Self, Name, Value>::enable)
        Attr& operator=(Value&& value) = delete;
        template <typename Value>
            requires (
                __setattr__<Self, Name, Value>::enable &&
                std::is_void_v<typename __setattr__<Self, Name, Value>::type> && (
                    std::is_invocable_r_v<void, __setattr__<Self, Name, Value>, Self, Value> || (
                        !std::is_invocable_v<__setattr__<Self, Name, Value>, Self, Value> &&
                        meta::has_cpp<Base> &&
                        std::is_assignable_v<meta::cpp_type<Base>&, Value>
                    ) || (
                        !std::is_invocable_v<__setattr__<Self, Name, Value>, Self, Value> &&
                        !meta::has_cpp<Base>
                    )
                )
            )
        Attr& operator=(Value&& value) && {
            if constexpr (std::is_invocable_v<__setattr__<Self, Name, Value>, Self, Value>) {
                __setattr__<Self, Name, Value>{}(
                    std::forward<Self>(m_self),
                    std::forward<Value>(value)
                );

            } else if constexpr (meta::has_cpp<Base>) {
                from_python(*this) = std::forward<Value>(value);

            } else {
                Base::operator=(std::forward<Value>(value));
                PyObject* name = PyUnicode_FromStringAndSize(Name, Name.size());
                if (name == nullptr) {
                    Exception::from_python();
                }
                int rc = PyObject_SetAttr(ptr(m_self), name, ptr(*this));
                Py_DECREF(name);
                if (rc) {
                    Exception::from_python();
                }
            }
            return *this;
        }
    };

    /* A proxy for an item in a Python container that is controlled by the
    `__getitem__`, `__setitem__`, and `__delitem__` control structs.

    This is a simple extension of an Object type that intercepts `operator=` and
    assigns the new value back to the container using the appropriate API.  Mutating
    the object in any other way will also modify it in-place within the container. */
    template <typename Self, typename... Key>
        requires (
            __getitem__<Self, Key...>::enable &&
            std::convertible_to<typename __getitem__<Self, Key...>::type, Object> && (
                std::is_invocable_r_v<
                    typename __getitem__<Self, Key...>::type,
                    __getitem__<Self, Key...>,
                    Self,
                    Key...
                > || (
                    !std::is_invocable_v<__getitem__<Self, Key...>, Self, Key...> &&
                    meta::has_cpp<Self> &&
                    meta::lookup_yields<
                        meta::cpp_type<Self>&,
                        typename __getitem__<Self, Key...>::type,
                        Key...
                    >
                ) || (
                    !std::is_invocable_v<__getitem__<Self, Key...>, Self, Key...> &&
                    !meta::has_cpp<Self> &&
                    std::derived_from<typename __getitem__<Self, Key...>::type, Object>
                )
            )
        )
    struct Item : std::remove_cv_t<typename __getitem__<Self, Key...>::type> {
    private:
        using Base = std::remove_cv_t<typename __getitem__<Self, Key...>::type>;

        template <typename S, typename... K>
            requires (
                __delitem__<S, K...>::enable &&
                std::is_void_v<typename __delitem__<S, K...>::type> && (
                    std::is_invocable_r_v<void, __delitem__<S, K...>, S, K...> ||
                    !std::is_invocable_v<__delitem__<S, K...>, S, K...>
                )
            )
        friend void bertrand::del(Item<S, K...>&& item);
        template <meta::python T>
        friend PyObject* bertrand::ptr(T&&);
        template <meta::python T> requires (!meta::is_const<T>)
        friend PyObject* bertrand::release(T&&);
        template <meta::has_python T> requires (!meta::is_qualified<T>)
        friend obj<T> bertrand::borrow(PyObject*);
        template <meta::has_python T> requires (!meta::is_qualified<T>)
        friend obj<T> bertrand::steal(PyObject*);
        template <meta::python T> requires (meta::has_cpp<T>)
        friend auto& impl::unwrap(T& obj);
        template <meta::python T> requires (meta::has_cpp<T>)
        friend const auto& impl::unwrap(const T& obj);

        /* m_self inherits the same const/volatile/reference qualifiers as the original
        object, and the keys are stored directly as members, retaining their original
        value categories without any extra copies/moves. */
        Self m_self;
        args<Key...> m_key;

        /* The wrapper's `m_ptr` member is lazily evaluated to avoid repeated lookups.
        Replacing it with a computed property will trigger a __getitem__ lookup the
        first time it is accessed. */
        __declspec(property(get = _get_ptr, put = _set_ptr)) PyObject* m_ptr;
        void _set_ptr(PyObject* value) { Base::m_ptr = value; }
        PyObject* _get_ptr() {
            if (Base::m_ptr == nullptr) {
                Base::m_ptr = std::move(m_key)([&](Key... key) {
                    if constexpr (std::is_invocable_v<__getitem__<Self, Key...>, Self, Key...>) {
                        return release(__getitem__<Self, Key...>{}(
                            std::forward<Self>(m_self),
                            std::forward<Key>(key)...
                        ));

                    } else if constexpr (sizeof...(Key) == 1) {
                        PyObject* result = PyObject_GetItem(
                            ptr(m_self),
                            ptr(to_python(std::forward<Key>(key)))...
                        );
                        if (result == nullptr) {
                            Exception::from_python();
                        }
                        return result;

                    } else {
                        PyObject* tuple = PyTuple_Pack(
                            sizeof...(Key),
                            ptr(to_python(std::forward<Key>(key)))...
                        );
                        if (tuple == nullptr) {
                            Exception::from_python();
                        }
                        PyObject* result = PyObject_GetItem(ptr(m_self), tuple);
                        Py_DECREF(tuple);
                        if (result == nullptr) {
                            Exception::from_python();
                        }
                        return result;
                    }
                });
            }
            return Base::m_ptr;
        }

    public:

        Item(Self&& self, Key&&... key) :
            Base(nullptr, Object::stolen_t{}),
            m_self(std::forward<Self>(self)),
            m_key(std::forward<Key>(key)...)
        {}
        Item(const Item& other) = delete;
        Item(Item&& other) = delete;

        template <typename Value> requires (!__setitem__<Self, Value, Key...>::enable)
        Item& operator=(Value&& other) = delete;
        template <typename Value>
            requires (
                __setitem__<Self, Value, Key...>::enable &&
                std::is_void_v<typename __setitem__<Self, Value, Key...>::type> && (
                    std::is_invocable_r_v<void, __setitem__<Self, Value, Key...>, Self, Value, Key...> || (
                        !std::is_invocable_v<__setitem__<Self, Value, Key...>, Self, Value, Key...> &&
                        meta::has_cpp<Base> &&
                        meta::supports_item_assignment<meta::cpp_type<Self>&, Value, Key...>
                    ) || (
                        !std::is_invocable_v<__setitem__<Self, Value, Key...>, Self, Value, Key...> &&
                        !meta::has_cpp<Base>
                    )
                )
            )
        Item& operator=(Value&& value) && {
            std::move(m_key)([&](Key... key) {
                if constexpr (std::is_invocable_v<__setitem__<Self, Value, Key...>, Self, Value, Key...>) {
                    __setitem__<Self, Value, Key...>{}(
                        std::forward<Self>(m_self),
                        std::forward<Value>(value),
                        std::forward<Key>(key)...
                    );

                } else if constexpr (meta::has_cpp<Base>) {
                    from_python(std::forward<Self>(m_self))[std::forward<Key>(key)...] =
                        std::forward<Value>(value);

                } else if constexpr (sizeof...(Key) == 1) {
                    Base::operator=(std::forward<Value>(value));
                    if (PyObject_SetItem(
                        ptr(m_self),
                        ptr(to_python(key))...,
                        ptr(*this)
                    )) {
                        Exception::from_python();
                    }

                } else {
                    Base::operator=(std::forward<Value>(value));
                    PyObject* tuple = PyTuple_Pack(
                        sizeof...(Key),
                        ptr(to_python(key))...
                    );
                    if (tuple == nullptr) {
                        Exception::from_python();
                    }
                    int rc = PyObject_SetItem(
                        ptr(m_self),
                        tuple,
                        ptr(*this)
                    );
                    Py_DECREF(tuple);
                    if (rc) {
                        Exception::from_python();
                    }
                }
            });
            return *this;
        }
    };

}


template <typename Self, static_str Name>
    requires (
        __delattr__<Self, Name>::enable &&
        std::is_void_v<typename __delattr__<Self, Name>::type> && (
            std::is_invocable_r_v<void, __delattr__<Self, Name>, Self> ||
            !std::is_invocable_v<__delattr__<Self, Name>, Self>
        )
    )
void del(impl::Attr<Self, Name>&& attr) {
    if constexpr (std::is_invocable_v<__delattr__<Self, Name>, Self>) {
        __delattr__<Self, Name>{}(std::forward<Self>(attr.m_self));

    } else {
        Object name = steal<Object>(
            PyUnicode_FromStringAndSize(Name, Name.size())
        );
        if (name.is(nullptr)) {
            Exception::from_python();
        }
        if (PyObject_DelAttr(ptr(attr.m_self), ptr(name))) {
            Exception::from_python();
        }
    }
}


template <typename Self, typename... Key>
    requires (
        __delitem__<Self, Key...>::enable &&
        std::is_void_v<typename __delitem__<Self, Key...>::type> && (
            std::is_invocable_r_v<void, __delitem__<Self, Key...>, Self, Key...> ||
            !std::is_invocable_v<__delitem__<Self, Key...>, Self, Key...>
        )
    )
void del(impl::Item<Self, Key...>&& item) {
    std::move(item.m_key)([&](auto&&... key) {
        if constexpr (std::is_invocable_v<__delitem__<Self, Key...>, Self, Key...>) {
            __delitem__<Self, Key...>{}(
                std::forward<Self>(item.m_self),
                std::forward<Key>(key)...
            );

        } else if constexpr (sizeof...(Key) == 1) {
            if (PyObject_DelItem(
                ptr(item.m_self),
                ptr(to_python(std::forward<decltype(key)>(key)))...)
            ) {
                Exception::from_python();
            }

        } else {
            Object tuple = steal<Object>(PyTuple_Pack(
                sizeof...(Key),
                ptr(to_python(std::forward<decltype(key)>(key)))...
            ));
            if (tuple.is(nullptr)) {
                Exception::from_python();
            }
            if (PyObject_DelItem(ptr(item.m_self), ptr(tuple))) {
                Exception::from_python();
            }
        }
    });
}


/// TODO: perhaps all of these could be avoided by exploiting inheritance for the
/// root control structs, which would help avoid ambiguities.


template <meta::lazily_evaluated Derived, meta::lazily_evaluated Base>
struct __isinstance__<Derived, Base> : __isinstance__<meta::lazy_type<Derived>, meta::lazy_type<Base>> {};
template <meta::lazily_evaluated Derived, typename Base>
struct __isinstance__<Derived, Base> : __isinstance__<meta::lazy_type<Derived>, Base> {};
template <typename Derived, meta::lazily_evaluated Base>
struct __isinstance__<Derived, Base> : __isinstance__<Derived, meta::lazy_type<Base>> {};
template <meta::lazily_evaluated Derived, meta::lazily_evaluated Base>
struct __issubclass__<Derived, Base> : __issubclass__<meta::lazy_type<Derived>, meta::lazy_type<Base>> {};
template <meta::lazily_evaluated Derived, typename Base>
struct __issubclass__<Derived, Base> : __issubclass__<meta::lazy_type<Derived>, Base> {};
template <typename Derived, meta::lazily_evaluated Base>
struct __issubclass__<Derived, Base> : __issubclass__<Derived, meta::lazy_type<Base>> {};
template <meta::lazily_evaluated Base, static_str Name>
struct __getattr__<Base, Name> : __getattr__<meta::lazy_type<Base>, Name> {};
template <meta::lazily_evaluated Base, static_str Name, typename Value>
struct __setattr__<Base, Name, Value> : __setattr__<meta::lazy_type<Base>, Name, Value> {};
template <meta::lazily_evaluated Base, static_str Name>
struct __delattr__<Base, Name> : __delattr__<meta::lazy_type<Base>, Name> {};
template <meta::lazily_evaluated Base, typename... Key>
struct __getitem__<Base, Key...> : __getitem__<meta::lazy_type<Base>, Key...> {};
template <meta::lazily_evaluated Base, typename Value, typename... Key>
struct __setitem__<Base, Value, Key...> : __setitem__<meta::lazy_type<Base>, Value, Key...> {};
template <meta::lazily_evaluated Base, typename... Key>
struct __delitem__<Base, Key...> : __delitem__<meta::lazy_type<Base>, Key...> {};
template <meta::lazily_evaluated Base>
struct __len__<Base> : __len__<meta::lazy_type<Base>> {};
template <meta::lazily_evaluated Base>
struct __iter__<Base> : __iter__<meta::lazy_type<Base>> {};
template <meta::lazily_evaluated Base>
struct __reversed__<Base> : __reversed__<meta::lazy_type<Base>> {};
template <meta::lazily_evaluated Base, typename Key>
struct __contains__<Base, Key> : __contains__<meta::lazy_type<Base>, Key> {};
template <meta::lazily_evaluated Base>
struct __hash__<Base> : __hash__<meta::lazy_type<Base>> {};
template <meta::lazily_evaluated Base>
struct __abs__<Base> : __abs__<meta::lazy_type<Base>> {};
template <meta::lazily_evaluated Base>
struct __invert__<Base> : __invert__<meta::lazy_type<Base>> {};
template <meta::lazily_evaluated Base>
struct __pos__<Base> : __pos__<meta::lazy_type<Base>> {};
template <meta::lazily_evaluated Base>
struct __neg__<Base> : __neg__<meta::lazy_type<Base>> {};
template <meta::lazily_evaluated Base>
struct __increment__<Base> : __increment__<meta::lazy_type<Base>> {};
template <meta::lazily_evaluated Base>
struct __decrement__<Base> : __decrement__<meta::lazy_type<Base>> {};
template <meta::lazily_evaluated L, meta::lazily_evaluated R>
struct __lt__<L, R> : __lt__<meta::lazy_type<L>, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, typename R>
struct __lt__<L, R> : __lt__<meta::lazy_type<L>, R> {};
template <typename L, meta::lazily_evaluated R>
struct __lt__<L, R> : __lt__<L, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, meta::lazily_evaluated R>
struct __le__<L, R> : __le__<meta::lazy_type<L>, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, typename R>
struct __le__<L, R> : __le__<meta::lazy_type<L>, R> {};
template <typename L, meta::lazily_evaluated R>
struct __le__<L, R> : __le__<L, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, meta::lazily_evaluated R>
struct __eq__<L, R> : __eq__<meta::lazy_type<L>, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, typename R>
struct __eq__<L, R> : __eq__<meta::lazy_type<L>, R> {};
template <typename L, meta::lazily_evaluated R>
struct __eq__<L, R> : __eq__<L, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, meta::lazily_evaluated R>
struct __ne__<L, R> : __ne__<meta::lazy_type<L>, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, typename R>
struct __ne__<L, R> : __ne__<meta::lazy_type<L>, R> {};
template <typename L, meta::lazily_evaluated R>
struct __ne__<L, R> : __ne__<L, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, meta::lazily_evaluated R>
struct __ge__<L, R> : __ge__<meta::lazy_type<L>, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, typename R>
struct __ge__<L, R> : __ge__<meta::lazy_type<L>, R> {};
template <typename L, meta::lazily_evaluated R>
struct __ge__<L, R> : __ge__<L, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, meta::lazily_evaluated R>
struct __gt__<L, R> : __gt__<meta::lazy_type<L>, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, typename R>
struct __gt__<L, R> : __gt__<meta::lazy_type<L>, R> {};
template <typename L, meta::lazily_evaluated R>
struct __gt__<L, R> : __gt__<L, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, meta::lazily_evaluated R>
struct __add__<L, R> : __add__<meta::lazy_type<L>, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, typename R>
struct __add__<L, R> : __add__<meta::lazy_type<L>, R> {};
template <typename L, meta::lazily_evaluated R>
struct __add__<L, R> : __add__<L, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, meta::lazily_evaluated R>
struct __sub__<L, R> : __sub__<meta::lazy_type<L>, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, typename R>
struct __sub__<L, R> : __sub__<meta::lazy_type<L>, R> {};
template <typename L, meta::lazily_evaluated R>
struct __sub__<L, R> : __sub__<L, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, meta::lazily_evaluated R>
struct __mul__<L, R> : __mul__<meta::lazy_type<L>, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, typename R>
struct __mul__<L, R> : __mul__<meta::lazy_type<L>, R> {};
template <typename L, meta::lazily_evaluated R>
struct __mul__<L, R> : __mul__<L, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, meta::lazily_evaluated R>
struct __truediv__<L, R> : __truediv__<meta::lazy_type<L>, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, typename R>
struct __truediv__<L, R> : __truediv__<meta::lazy_type<L>, R> {};
template <typename L, meta::lazily_evaluated R>
struct __truediv__<L, R> : __truediv__<L, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, meta::lazily_evaluated R>
struct __floordiv__<L, R> : __floordiv__<meta::lazy_type<L>, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, typename R>
struct __floordiv__<L, R> : __floordiv__<meta::lazy_type<L>, R> {};
template <typename L, meta::lazily_evaluated R>
struct __floordiv__<L, R> : __floordiv__<L, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, meta::lazily_evaluated R>
struct __mod__<L, R> : __mod__<meta::lazy_type<L>, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, typename R>
struct __mod__<L, R> : __mod__<meta::lazy_type<L>, R> {};
template <typename L, meta::lazily_evaluated R>
struct __mod__<L, R> : __mod__<L, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, meta::lazily_evaluated R>
struct __pow__<L, R> : __pow__<meta::lazy_type<L>, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, typename R>
struct __pow__<L, R> : __pow__<meta::lazy_type<L>, R> {};
template <typename L, meta::lazily_evaluated R>
struct __pow__<L, R> : __pow__<L, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, meta::lazily_evaluated R>
struct __lshift__<L, R> : __lshift__<meta::lazy_type<L>, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, typename R>
struct __lshift__<L, R> : __lshift__<meta::lazy_type<L>, R> {};
template <typename L, meta::lazily_evaluated R>
struct __lshift__<L, R> : __lshift__<L, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, meta::lazily_evaluated R>
struct __rshift__<L, R> : __rshift__<meta::lazy_type<L>, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, typename R>
struct __rshift__<L, R> : __rshift__<meta::lazy_type<L>, R> {};
template <typename L, meta::lazily_evaluated R>
struct __rshift__<L, R> : __rshift__<L, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, meta::lazily_evaluated R>
struct __and__<L, R> : __and__<meta::lazy_type<L>, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, typename R>
struct __and__<L, R> : __and__<meta::lazy_type<L>, R> {};
template <typename L, meta::lazily_evaluated R>
struct __and__<L, R> : __and__<L, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, meta::lazily_evaluated R>
struct __xor__<L, R> : __xor__<meta::lazy_type<L>, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, typename R>
struct __xor__<L, R> : __xor__<meta::lazy_type<L>, R> {};
template <typename L, meta::lazily_evaluated R>
struct __xor__<L, R> : __xor__<L, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, meta::lazily_evaluated R>
struct __or__<L, R> : __or__<meta::lazy_type<L>, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, typename R>
struct __or__<L, R> : __or__<meta::lazy_type<L>, R> {};
template <typename L, meta::lazily_evaluated R>
struct __or__<L, R> : __or__<L, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, meta::lazily_evaluated R>
struct __iadd__<L, R> : __iadd__<meta::lazy_type<L>, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, typename R>
struct __iadd__<L, R> : __iadd__<meta::lazy_type<L>, R> {};
template <typename L, meta::lazily_evaluated R>
struct __iadd__<L, R> : __iadd__<L, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, meta::lazily_evaluated R>
struct __isub__<L, R> : __isub__<meta::lazy_type<L>, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, typename R>
struct __isub__<L, R> : __isub__<meta::lazy_type<L>, R> {};
template <typename L, meta::lazily_evaluated R>
struct __isub__<L, R> : __isub__<L, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, meta::lazily_evaluated R>
struct __imul__<L, R> : __imul__<meta::lazy_type<L>, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, typename R>
struct __imul__<L, R> : __imul__<meta::lazy_type<L>, R> {};
template <typename L, meta::lazily_evaluated R>
struct __imul__<L, R> : __imul__<L, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, meta::lazily_evaluated R>
struct __itruediv__<L, R> : __itruediv__<meta::lazy_type<L>, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, typename R>
struct __itruediv__<L, R> : __itruediv__<meta::lazy_type<L>, R> {};
template <typename L, meta::lazily_evaluated R>
struct __itruediv__<L, R> : __itruediv__<L, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, meta::lazily_evaluated R>
struct __ifloordiv__<L, R> : __ifloordiv__<meta::lazy_type<L>, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, typename R>
struct __ifloordiv__<L, R> : __ifloordiv__<meta::lazy_type<L>, R> {};
template <typename L, meta::lazily_evaluated R>
struct __ifloordiv__<L, R> : __ifloordiv__<L, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, meta::lazily_evaluated R>
struct __imod__<L, R> : __imod__<meta::lazy_type<L>, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, typename R>
struct __imod__<L, R> : __imod__<meta::lazy_type<L>, R> {};
template <typename L, meta::lazily_evaluated R>
struct __imod__<L, R> : __imod__<L, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, meta::lazily_evaluated R>
struct __ipow__<L, R> : __ipow__<meta::lazy_type<L>, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, typename R>
struct __ipow__<L, R> : __ipow__<meta::lazy_type<L>, R> {};
template <typename L, meta::lazily_evaluated R>
struct __ipow__<L, R> : __ipow__<L, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, meta::lazily_evaluated R>
struct __ilshift__<L, R> : __ilshift__<meta::lazy_type<L>, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, typename R>
struct __ilshift__<L, R> : __ilshift__<meta::lazy_type<L>, R> {};
template <typename L, meta::lazily_evaluated R>
struct __ilshift__<L, R> : __ilshift__<L, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, meta::lazily_evaluated R>
struct __irshift__<L, R> : __irshift__<meta::lazy_type<L>, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, typename R>
struct __irshift__<L, R> : __irshift__<meta::lazy_type<L>, R> {};
template <typename L, meta::lazily_evaluated R>
struct __irshift__<L, R> : __irshift__<L, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, meta::lazily_evaluated R>
struct __iand__<L, R> : __iand__<meta::lazy_type<L>, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, typename R>
struct __iand__<L, R> : __iand__<meta::lazy_type<L>, R> {};
template <typename L, meta::lazily_evaluated R>
struct __iand__<L, R> : __iand__<L, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, meta::lazily_evaluated R>
struct __ixor__<L, R> : __ixor__<meta::lazy_type<L>, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, typename R>
struct __ixor__<L, R> : __ixor__<meta::lazy_type<L>, R> {};
template <typename L, meta::lazily_evaluated R>
struct __ixor__<L, R> : __ixor__<L, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, meta::lazily_evaluated R>
struct __ior__<L, R> : __ior__<meta::lazy_type<L>, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, typename R>
struct __ior__<L, R> : __ior__<meta::lazy_type<L>, R> {};
template <typename L, meta::lazily_evaluated R>
struct __ior__<L, R> : __ior__<L, meta::lazy_type<R>> {};


/* Equivalent to Python `hasattr(obj, name)` with a static attribute name. */
template <meta::python Self, static_str Name>
[[nodiscard]] constexpr bool hasattr() {
    return __getattr__<Self, Name>::enable;
}


/* Equivalent to Python `hasattr(obj, name)` with a static attribute name. */
template <static_str Name, meta::python Self>
[[nodiscard]] constexpr bool hasattr(Self&& obj) {
    return __getattr__<Self, Name>::enable;
}


/* Equivalent to Python `getattr(obj, name)` with a static attribute name. */
template <static_str Name, meta::python Self>
    requires (
        __getattr__<Self, Name>::enable &&
        meta::python<typename __getattr__<Self, Name>::type> &&
        !meta::is_qualified<typename __getattr__<Self, Name>::type> && (
            !std::is_invocable_v<__getattr__<Self, Name>, Self> ||
            std::is_invocable_r_v<
                typename __getattr__<Self, Name>::type,
                __getattr__<Self, Name>,
                Self
            >
        )
    )
[[nodiscard]] auto getattr(Self&& self) -> __getattr__<Self, Name>::type {
    using Return = __getattr__<Self, Name>::type;
    if constexpr (DEBUG) {
        assert_(
            !self.is(nullptr),
            "Cannot get attribute '" + Name + "' from a null object."
        );
    }
    if constexpr (std::is_invocable_v<__getattr__<Self, Name>, Self>) {
        return __getattr__<Self, Name>{}(std::forward<Self>(self));

    } else {
        Return result = steal<Return>(PyObject_GetAttr(
            ptr(self),
            ptr(impl::template_string<Name>())
        ));
        if (result.is(nullptr)) {
            Exception::from_python();
        }
        return result;
    }
}


/* Equivalent to Python `getattr(obj, name, default)` with a static attribute name and
default value. */
template <static_str Name, meta::python Self>
    requires (
        __getattr__<Self, Name>::enable &&
        meta::python<typename __getattr__<Self, Name>::type> &&
        !meta::is_qualified<typename __getattr__<Self, Name>::type> && (
            !std::is_invocable_v<
                __getattr__<Self, Name>,
                Self,
                const typename __getattr__<Self, Name>::type&
            > || std::is_invocable_r_v<
                typename __getattr__<Self, Name>::type,
                __getattr__<Self, Name>,
                Self,
                const typename __getattr__<Self, Name>::type&
            >
        )
    )
[[nodiscard]] auto getattr(
    Self&& self,
    const typename __getattr__<Self, Name>::type& default_value
) -> __getattr__<Self, Name>::type {
    using Return = __getattr__<Self, Name>::type;
    if constexpr (DEBUG) {
        assert_(
            !self.is(nullptr),
            "Cannot get attribute '" + Name + "' from a null object."
        );
    }
    if constexpr (std::is_invocable_v<__getattr__<Self, Name>, Self, const Return&>) {
        return __getattr__<Self, Name>{}(std::forward<Self>(self), default_value);

    } else {
        Return result = steal<Return>(PyObject_GetAttr(
            ptr(self),
            ptr(impl::template_string<Name>())
        ));
        if (result.is(nullptr)) {
            PyErr_Clear();
            return default_value;
        }
        return result;
    }
}


/* Equivalent to Python `setattr(obj, name, value)` with a static attribute name. */
template <static_str Name, meta::python Self, typename Value>
    requires (
        __setattr__<Self, Name, Value>::enable &&
        std::is_void_v<typename __setattr__<Self, Name, Value>::type> && (
            std::is_invocable_r_v<void, __setattr__<Self, Name, Value>, Self, Value> || (
                !std::is_invocable_v<__setattr__<Self, Name, Value>, Self, Value> &&
                meta::has_cpp<typename std::remove_cvref_t<typename __getattr__<Self, Name>::type>> &&
                std::is_assignable_v<typename std::remove_cvref_t<typename __getattr__<Self, Name>::type>&, Value>
            ) || (
                !std::is_invocable_v<__setattr__<Self, Name, Value>, Self, Value> &&
                !meta::has_cpp<typename std::remove_cvref_t<typename __getattr__<Self, Name>::type>>
            )
        )
    )
void setattr(Self&& self, Value&& value) {
    if constexpr (DEBUG) {
        assert_(
            !self.is(nullptr),
            "Cannot assign attribute '" + Name + "' on a null object."
        );
    }
    if constexpr (std::is_invocable_v<__setattr__<Self, Name, Value>, Self, Value>) {
        __setattr__<Self, Name, Value>{}(
            std::forward<Self>(self),
            std::forward<Value>(value)
        );

    } else {
        auto obj = to_python(std::forward<Value>(value));
        if constexpr (DEBUG) {
            assert_(
                !obj.is(nullptr),
                "Cannot assign attribute '" + Name + "' to a null object."
            );
        }
        if (PyObject_SetAttr(
            ptr(self),
            ptr(impl::template_string<Name>()),
            ptr(obj)
        )) {
            Exception::from_python();
        }
    }
}


/* Equivalent to Python `delattr(obj, name)` with a static attribute name. */
template <static_str Name, meta::python Self>
    requires (
        __delattr__<Self, Name>::enable && 
        std::is_void_v<typename __delattr__<Self, Name>::type> && (
            std::is_invocable_r_v<void, __delattr__<Self, Name>, Self> ||
            !std::is_invocable_v<__delattr__<Self, Name>, Self>
        )
    )
void delattr(Self&& self) {
    if constexpr (DEBUG) {
        assert_(
            !self.is(nullptr),
            "Cannot delete attribute '" + Name + "' on a null object."
        );
    }
    if constexpr (std::is_invocable_v<__delattr__<Self, Name>, Self>) {
        __delattr__<Self, Name>{}(std::forward<Self>(self));

    } else {
        if (PyObject_DelAttr(
            ptr(self),
            ptr(impl::template_string<Name>())
        )) {
            Exception::from_python();
        }
    }
}


/////////////////////////
////    ITERATORS    ////
/////////////////////////


template <typename Begin, typename End = void, typename Container = void>
struct Iterator;


template <typename Begin, typename End, typename Container>
struct interface<Iterator<Begin, End, Container>> : impl::IterTag {
private:

    template <typename B, typename E, typename C>
    struct traits {
        using begin_type = B;
        using end_type = E;
        using container_type = C;
        using value_type = decltype(*std::declval<B>());
    };
    template <typename R>
    struct traits<R, void, void> {
        using begin_type = __iter__<Iterator<R>>;
        using end_type = sentinel;
        using container_type = void;
        using value_type = R;
    };

public:
    using begin_type = traits<Begin, End, Container>::begin_type;
    using end_type = traits<Begin, End, Container>::end_type;
    using container_type = traits<Begin, End, Container>::container_type;
    using value_type = traits<Begin, End, Container>::value_type;

    decltype(auto) __iter__(this auto&& self) {
        return std::forward<decltype(self)>(self);
    }

    decltype(auto) __next__(this auto&& self) {
        if constexpr (std::is_void_v<End>) {
            PyObject* next = PyIter_Next(ptr(self));
            if (next == nullptr) {
                if (PyErr_Occurred()) {
                    Exception::from_python();
                }
                throw StopIteration();
            }
            return steal<Begin>(next);

        } else {
            auto inner = view(self);
            if (inner->begin == inner->end) {
                throw StopIteration();
            }
            ++(inner->begin);
            if (inner->begin == inner->end) {
                throw StopIteration();
            }
            return *(inner->begin);
        }
    }
};


template <typename Begin, typename End, typename Container>
struct interface<Type<Iterator<Begin, End, Container>>> {
    using begin_type        = interface<Iterator<Begin, End, Container>>::begin_type;
    using end_type          = interface<Iterator<Begin, End, Container>>::end_type;
    using container_type    = interface<Iterator<Begin, End, Container>>::container_type;
    using value_type        = interface<Iterator<Begin, End, Container>>::value_type;

    template <meta::inherits<interface<Iterator<Begin, End, Container>>> Self>
    static decltype(auto) __iter__(Self&& self) {
        return std::forward<Self>(self).__iter__();
    }

    template <meta::inherits<interface<Iterator<Begin, End, Container>>> Self>
    static decltype(auto) __next__(Self&& self) {
        return std::forward<Self>(self).__next__();
    }
};


/* A wrapper around a Python iterator that allows it to be used from C++.

This type has no fixed implementation, and can match any kind of Python iterator.  It
roughly corresponds to the `collections.abc.Iterator` abstract base class in Python,
and allows C++ to call the Python-level `__next__()` hook.  Note that the reverse
(exposing C++ iterators to Python) is done via a separate specialization.

In the interest of performance, no explicit checks are done to ensure that the return
type matches expectations.  As such, this class is one of the rare cases where type
safety may be violated, and should therefore be used with caution.  It is mostly meant
for internal use to back the default result of the `begin()` and `end()` operators when
no specialized C++ iterator can be found.  In that case, its value type is set to the
`T` in an `__iter__<Container> : returns<T> {};` specialization.  If you want to use
this class and avoid type safety issues, leave the return type set to `Object` (the
default), which will incur a runtime check on conversion. */
template <meta::python Return>
struct Iterator<Return, void, void> : Object, interface<Iterator<Return, void, void>> {
    struct __python__ : cls<__python__, Iterator>, PyObject {
        static Type<Iterator> __import__() {
            Object collections = steal<Object>(PyImport_Import(
                ptr(impl::template_string<"collections.abc">())
            ));
            if (collections.is(nullptr)) {
                Exception::from_python();
            }
            Type<Iterator> result = steal<Type<Iterator>>(
                PyObject_GetItem(
                    ptr(getattr<"Iterator">(collections)),
                    ptr(Type<Return>())
                )
            );
            if (result.is(nullptr)) {
                Exception::from_python();
            }
            return result;
        }
    };

    Iterator(PyObject* p, borrowed_t t) : Object(p, t) {}
    Iterator(PyObject* p, stolen_t t) : Object(p, t) {}

    template <typename T = Iterator> requires (__initializer__<T>::enable)
    Iterator(const std::initializer_list<typename __initializer__<T>::type>& init) :
        Object(__initializer__<T>{}(init))
    {}

    template <typename... Args> requires (implicit_ctor<Iterator>::template enable<Args...>)
    Iterator(Args&&... args) : Object(
        implicit_ctor<Iterator>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args> requires (explicit_ctor<Iterator>::template enable<Args...>)
    explicit Iterator(Args&&... args) : Object(
        explicit_ctor<Iterator>{},
        std::forward<Args>(args)...
    ) {}

};


/* A wrapper around a non-owning C++ range that allows them to be iterated over from
Python.

This will instantiate a unique Python type with an appropriate `__next__()` method for
every combination of C++ iterators, forwarding to their respective `operator*()`,
`operator++()`, and `operator==()` methods. */
template <std::input_or_output_iterator Begin, std::sentinel_for<Begin> End>
    requires (std::convertible_to<decltype(*std::declval<Begin>()), Object>)
struct Iterator<Begin, End, void> : Object, interface<Iterator<Begin, End, void>> {
    struct __python__ : cls<__python__, Iterator>, PyObject {
        inline static bool initialized = false;
        static PyTypeObject __type__;

        std::remove_reference_t<Begin> begin;
        std::remove_reference_t<End> end;

        __python__(auto& container) :
            begin(std::ranges::begin(container)), end(std::ranges::end(container))
        {
            ready();
        }

        __python__(Begin&& begin, End&& end) :
            begin(std::forward(begin)), end(std::forward(end))
        {
            ready();
        }

        static Type<Iterator> __import__() {
            ready();
            return borrow<Type<Iterator>>(&__type__);
        }

        static int __bool__(__python__* self) {
            try {
                return self->begin != self->end;
            } catch (...) {
                Exception::to_python();
                return -1;
            }
        }

        static PyObject* __next__(__python__* self) {
            try {
                if (self->begin == self->end) {
                    return nullptr;
                }
                auto result = to_python(*(self->begin));  // owning obj
                ++(self->begin);
                if constexpr (meta::python<decltype(*(self->begin))>) {
                    return Py_NewRef(ptr(result));
                } else {
                    return release(result);
                }
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

    private:

        inline static PyNumberMethods number = {
            .nb_bool = reinterpret_cast<inquiry>(__bool__)
        };

        static void ready() {
            if (!initialized) {
                __type__ = {
                    .tp_name = typeid(Iterator).name(),
                    .tp_basicsize = sizeof(__python__),
                    .tp_itemsize = 0,
                    .tp_as_number = &number,
                    .tp_flags = 
                        Py_TPFLAGS_DEFAULT | Py_TPFLAGS_DISALLOW_INSTANTIATION,
                    .tp_iter = PyObject_SelfIter,
                    .tp_iternext = reinterpret_cast<iternextfunc>(__next__)
                };
                if (PyType_Ready(&__type__) < 0) {
                    Exception::from_python();
                }
                initialized = true;
            }
        }

    };

    Iterator(PyObject* p, borrowed_t t) : Object(p, t) {}
    Iterator(PyObject* p, stolen_t t) : Object(p, t) {}

    template <typename T = Iterator> requires (__initializer__<T>::enable)
    Iterator(const std::initializer_list<typename __initializer__<T>::type>& init) :
        Object(__initializer__<T>{}(init))
    {}

    template <typename... Args> requires (implicit_ctor<Iterator>::template enable<Args...>)
    Iterator(Args&&... args) : Object(
        implicit_ctor<Iterator>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args> requires (explicit_ctor<Iterator>::template enable<Args...>)
    explicit Iterator(Args&&... args) : Object(
        explicit_ctor<Iterator>{},
        std::forward<Args>(args)...
    ) {}

};


/* A wrapper around an owning C++ range that was generated from a temporary container.
The container is moved into the Python iterator object and will remain valid as long as
the iterator object has a nonzero reference count.

This will instantiate a unique Python type with an appropriate `__next__()` method for
every combination of C++ iterators, forwarding to their respective `operator*()`,
`operator++()`, and `operator==()` methods. */
template <
    std::input_or_output_iterator Begin,
    std::sentinel_for<Begin> End,
    meta::iterable Container
>
    requires (std::convertible_to<decltype(*std::declval<Begin>()), Object>)
struct Iterator<Begin, End, Container> : Object, interface<Iterator<Begin, End, Container>> {
    struct __python__ : cls<__python__, Iterator>, PyObject {
        inline static bool initialized = false;
        static PyTypeObject __type__;

        Container container;
        Begin begin;
        End end;

        __python__(Container&& container) :
            container(std::move(container)),
            begin(std::ranges::begin(this->container)),
            end(std::ranges::end(this->container))
        {
            ready();
        }

        static Type<Iterator> __import__() {
            ready();
            return borrow<Type<Iterator>>(&__type__);
        }

        static int __bool__(__python__* self) {
            try {
                return self->begin != self->end;
            } catch (...) {
                Exception::to_python();
                return -1;
            }
        }

        static PyObject* __next__(__python__* self) {
            try {
                if (self->begin == self->end) {
                    return nullptr;
                }
                auto result = to_python(*(self->begin));
                ++(self->begin);
                if constexpr (meta::python<decltype(*(self->begin))>) {
                    return Py_NewRef(ptr(result));
                } else {
                    return release(result);
                }
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

    private:

        inline static PyNumberMethods number = {
            .nb_bool = reinterpret_cast<inquiry>(__bool__)
        };

        static void ready() {
            if (!initialized) {
                __type__ = {
                    .tp_name = typeid(Iterator).name(),
                    .tp_basicsize = sizeof(__python__),
                    .tp_itemsize = 0,
                    .tp_as_number = &number,
                    .tp_flags = 
                        Py_TPFLAGS_DEFAULT | Py_TPFLAGS_DISALLOW_INSTANTIATION,
                    .tp_iter = PyObject_SelfIter,
                    .tp_iternext = reinterpret_cast<iternextfunc>(__next__)
                };
                if (PyType_Ready(&__type__) < 0) {
                    Exception::from_python();
                }
                initialized = true;
            }
        }
    };

    Iterator(PyObject* p, borrowed_t t) : Object(p, t) {}
    Iterator(PyObject* p, stolen_t t) : Object(p, t) {}

    template <typename T = Iterator> requires (__initializer__<T>::enable)
    Iterator(const std::initializer_list<typename __initializer__<T>::type>& init) :
        Object(__initializer__<T>{}(init))
    {}

    template <typename... Args> requires (implicit_ctor<Iterator>::template enable<Args...>)
    Iterator(Args&&... args) : Object(
        implicit_ctor<Iterator>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args> requires (explicit_ctor<Iterator>::template enable<Args...>)
    explicit Iterator(Args&&... args) : Object(
        explicit_ctor<Iterator>{},
        std::forward<Args>(args)...
    ) {}
};


namespace impl {

    template <meta::iterable Container>
    struct IterTraits {
        using begin = decltype(std::ranges::begin(
            std::declval<std::add_lvalue_reference_t<Container>>()
        ));
        using end = decltype(std::ranges::end(
            std::declval<std::add_lvalue_reference_t<Container>>()
        ));
        using container = std::remove_reference_t<Container>;
    };
    template <meta::iterable Container> requires (std::is_lvalue_reference_v<Container>)
    struct IterTraits<Container> {
        using begin = decltype(std::ranges::begin(std::declval<Container>()));
        using end = decltype(std::ranges::end(std::declval<Container>()));
        using container = void;
    };

}


/* CTAD guide will generate a Python iterator around a pair of raw C++ iterators. */
template <std::input_or_output_iterator Begin, std::sentinel_for<Begin> End>
    requires (std::convertible_to<decltype(*std::declval<Begin>()), Object>)
Iterator(Begin, End) -> Iterator<Begin, End, void>;


/* CTAD guide will generate a Python iterator from an arbitrary C++ container, with
correct ownership semantics. */
template <meta::iterable Container>
    requires (meta::yields<Container, Object>)
Iterator(Container&&) -> Iterator<
    typename impl::IterTraits<Container>::begin,
    typename impl::IterTraits<Container>::end,
    typename impl::IterTraits<Container>::container
>;


/* Implement the CTAD guide for iterable containers.  The container type may be const,
which will be reflected in the deduced iterator types. */
template <meta::iterable Container>
    requires (meta::yields<Container, Object>)
struct __init__<
    Iterator<
        typename impl::IterTraits<Container>::begin,
        typename impl::IterTraits<Container>::end,
        typename impl::IterTraits<Container>::container
    >,
    Container
> : returns<Iterator<
    typename impl::IterTraits<Container>::begin,
    typename impl::IterTraits<Container>::end,
    typename impl::IterTraits<Container>::container
>> {
    static auto operator()(Container&& self) {
        return impl::construct<Iterator<
            typename impl::IterTraits<Container>::begin,
            typename impl::IterTraits<Container>::end,
            typename impl::IterTraits<Container>::container
        >>(std::forward<Container>(self));
    }
};


/* Construct a Python iterator from a pair of C++ iterators. */
template <std::input_or_output_iterator Begin, std::sentinel_for<Begin> End>
    requires (std::convertible_to<decltype(*std::declval<Begin>()), Object>)
struct __init__<Iterator<Begin, End, void>, Begin, End> : returns<Iterator<Begin, End, void>> {
    static auto operator()(auto&& begin, auto&& end) {
        return impl::construct<Iterator<Begin, End, void>>(
            std::forward<decltype(begin)>(begin),
            std::forward<decltype(end)>(end)
        );
    }
};


template <meta::is<Object> Derived, typename Return>
struct __isinstance__<Derived, Iterator<Return, void, void>> : returns<bool> {
    static constexpr bool operator()(Derived obj) {
        return PyIter_Check(ptr(obj));
    }
};


template <typename Derived, typename Return>
struct __issubclass__<Derived, Iterator<Return, void, void>> : returns<bool> {
    static constexpr bool operator()() {
        return
            meta::inherits<Derived, impl::IterTag> &&
            std::convertible_to<meta::iter_type<Derived>, Return>;
    }
    static constexpr bool operator()(Derived obj) {
        if constexpr (meta::is<Derived, Object>) {
            int rc = PyObject_IsSubclass(
                ptr(obj),
                ptr(Type<Iterator<Return, void, void>>())
            );
            if (rc == -1) {
                Exception::from_python();
            }
            return rc;
        } else {
            return operator()();
        }
    }
};


/* Traversing a Python iterator requires a customized C++ iterator type. */
template <typename T>
struct __iter__<Iterator<T, void, void>>                    : returns<T> {
    using iterator_category = std::input_iterator_tag;
    using difference_type   = std::ptrdiff_t;
    using value_type        = T;
    using pointer           = T*;
    using reference         = T&;

    Iterator<T> iter;
    T curr;

    __iter__(const Iterator<T>& self) :
        iter(self), curr(steal<T>(nullptr))
    {
        ++(*this);
    }

    __iter__(Iterator<T>&& self) :
        iter(self), curr(steal<T>(nullptr))
    {
        ++(*this);
    }

    /// NOTE: python iterators cannot be copied due to their stateful nature via the
    /// shared PyObject* pointer.

    __iter__(const __iter__&) = delete;
    __iter__(__iter__&& other) :
        iter(std::move(other.iter)), curr(std::move(other.curr))
    {}

    __iter__& operator=(const __iter__&) = delete;
    __iter__& operator=(__iter__&& other) {
        if (&other != this) {
            iter = std::move(other.iter);
            curr = std::move(other.curr);
        }
        return *this;
    }

    [[nodiscard]] const T& operator*() const { return curr; }
    [[nodiscard]] const T* operator->() const { return &curr; }

    __iter__& operator++() {
        PyObject* next = PyIter_Next(ptr(iter));
        if (PyErr_Occurred()) {
            Exception::from_python();
        }
        curr = steal<T>(next);
        return *this;
    }

    __iter__& operator++(int) {
        return ++(*this);
    }

    [[nodiscard]] friend bool operator==(const __iter__& self, sentinel) {
        return self.curr.is(nullptr);
    }

    [[nodiscard]] friend bool operator==(sentinel, const __iter__& self) {
        return self.curr.is(nullptr);
    }

    [[nodiscard]] friend bool operator!=(const __iter__& self, sentinel) {
        return !self.curr.is(nullptr);
    }

    [[nodiscard]] friend bool operator!=(sentinel, const __iter__& self) {
        return !self.curr.is(nullptr);
    }

};
/// NOTE: can't iterate over a const Iterator<T> because the iterator itself must be
/// mutable.


/* py::Iterator<Begin, End, ...> is special cased in the begin() and end() operators to
extract the internal C++ iterators rather than creating yet another layer of
indirection. */
template <std::input_or_output_iterator Begin, std::sentinel_for<Begin> End, typename Container>
struct __iter__<Iterator<Begin, End, Container>> : returns<decltype(*std::declval<Begin>())> {};


template <typename T, typename Begin, typename End, typename Container>
struct __contains__<T, Iterator<Begin, End, Container>> : returns<bool> {};


/// TODO: these attributes can only be defined after functions are defined


// template <impl::inherits<impl::IterTag> Self>
// struct __getattr__<Self, "__iter__"> : returns<
//     Function<impl::qualify<Self(std::remove_cvref_t<Self>::*)(), Self>>
// > {};
// template <impl::inherits<impl::IterTag> Self>
// struct __getattr__<Self, "__next__"> : returns<
//     Function<impl::qualify<
//         std::conditional_t<
//             std::is_void_v<typename std::remove_reference_t<Self>::end_type>,
//             std::remove_reference_t<decltype(
//                 *std::declval<typename std::remove_reference_t<Self>::begin_type>()
//             )>,
//             decltype(
//                 *std::declval<typename std::remove_reference_t<Self>::begin_type>()
//             )
//         >(std::remove_cvref_t<Self>::*)(),
//         Self
//     >>
// > {};
// template <impl::inherits<impl::IterTag> Self>
// struct __getattr__<Type<Self>, "__iter__"> : returns<Function<
//     Self(*)(Self)
// >> {};
// template <impl::inherits<impl::IterTag> Self>
// struct __getattr__<Type<Self>, "__next__"> : returns<Function<
//     std::conditional_t<
//         std::is_void_v<typename std::remove_reference_t<Self>::end_type>,
//         std::remove_reference_t<decltype(
//             *std::declval<typename std::remove_reference_t<Self>::begin_type>()
//         )>,
//         decltype(
//             *std::declval<typename std::remove_reference_t<Self>::begin_type>()
//         )
//     >(*)(Self)
// >> {};


/* Begin iteration operator.  Both this and the end iteration operator are
controlled by the __iter__ control struct, whose return type dictates the
iterator's dereference type. */
template <meta::python Self>
    requires (__iter__<Self>::enable && (
        std::is_constructible_v<__iter__<Self>, Self> ||
        (meta::has_cpp<Self> && meta::iterable<meta::cpp_type<Self>>) ||
        (!meta::has_cpp<Self> && std::derived_from<typename __iter__<Self>::type, Object>)
    ))
[[nodiscard]] auto begin(const Self& self) {
    if constexpr (std::is_constructible_v<__iter__<Self>, Self>) {
        return __iter__<Self>(self);

    } else if constexpr (meta::has_cpp<Self>) {
        return std::ranges::begin(from_python(self));

    } else if constexpr (meta::inherits<Self, impl::IterTag>) {
        if constexpr (!std::is_void_v<typename std::remove_reference_t<Self>::end_type>) {
            return view(self)->begin;
        } else {
            using T = __iter__<Self>::type;
            PyObject* iter = PyObject_GetIter(ptr(self));
            if (iter == nullptr) {
                Exception::from_python();
            }
            return __iter__<Iterator<T>>{steal<Iterator<T>>(iter)};
        }

    } else {
        using T = __iter__<Self>::type;
        PyObject* iter = PyObject_GetIter(ptr(self));
        if (iter == nullptr) {
            Exception::from_python();
        }
        return __iter__<Iterator<T>>{steal<Iterator<T>>(iter)};
    }
}


template <meta::python Self>
    requires (__iter__<Self>::enable && (
        std::is_constructible_v<__iter__<Self>, Self> ||
        (meta::has_cpp<Self> && meta::iterable<meta::cpp_type<Self>>) ||
        (!meta::has_cpp<Self> && std::derived_from<typename __iter__<Self>::type, Object>)
    ))
[[nodiscard]] auto begin(Self& self) {
    return begin(reinterpret_cast<std::add_const_t<Self>&>(self));
}


template <meta::python Self>
    requires (__iter__<Self>::enable && (
        std::is_constructible_v<__iter__<Self>, Self> ||
        (meta::has_cpp<Self> && meta::iterable<meta::cpp_type<Self>>) ||
        (!meta::has_cpp<Self> && std::derived_from<typename __iter__<Self>::type, Object>)
    ))
[[nodiscard]] auto cbegin(const Self& self) {
    return begin(self);
}


/* End iteration operator.  This terminates the iteration and is controlled by the
__iter__ control struct. */
template <meta::python Self>
    requires (__iter__<Self>::enable && (
        std::is_constructible_v<__iter__<Self>, Self> ||
        (meta::has_cpp<Self> && meta::iterable<meta::cpp_type<Self>>) ||
        (!meta::has_cpp<Self> && std::derived_from<typename __iter__<Self>::type, Object>)
    ))
[[nodiscard]] auto end(const Self& self) {
    if constexpr (std::is_constructible_v<__iter__<Self>, Self>) {
        return sentinel{};

    } else if constexpr (meta::has_cpp<Self>) {
        return std::ranges::end(from_python(std::forward<Self>(self)));

    } else if constexpr (meta::inherits<Self, impl::IterTag>) {
        if constexpr (!std::is_void_v<typename std::remove_reference_t<Self>::end_type>) {
            return view(self)->end;
        } else {
            return sentinel{};
        }

    } else {
        return sentinel{};
    }
}


template <meta::python Self>
    requires (__iter__<Self>::enable && (
        std::is_constructible_v<__iter__<Self>, Self> ||
        (meta::has_cpp<Self> && meta::iterable<meta::cpp_type<Self>>) ||
        (!meta::has_cpp<Self> && std::derived_from<typename __iter__<Self>::type, Object>)
    ))
[[nodiscard]] auto end(Self& self) {
    return end(reinterpret_cast<std::add_const_t<Self>&>(self));
}


/* Const end operator.  Similar to `cbegin()`, this is identical to `end()`. */
template <meta::python Self>
    requires (__iter__<Self>::enable && (
        std::is_constructible_v<__iter__<Self>, Self> ||
        (meta::has_cpp<Self> && meta::iterable<meta::cpp_type<Self>>) ||
        (!meta::has_cpp<Self> && std::derived_from<typename __iter__<Self>::type, Object>)
    ) && std::is_const_v<std::remove_reference_t<Self>>)
[[nodiscard]] auto cend(const Self& self) {
    return end(std::forward<Self>(self));
}


/* Reverse iteration operator.  Both this and the reverse end operator are
controlled by the __reversed__ control struct, whose return type dictates the
iterator's dereference type. */
template <meta::python Self>
    requires (__reversed__<Self>::enable && (
        std::is_constructible_v<__reversed__<Self>, Self> ||
        (meta::has_cpp<Self> && meta::reverse_iterable<meta::cpp_type<Self>>) ||
        (!meta::has_cpp<Self> && std::derived_from<typename __reversed__<Self>::type, Object>)
    ))
[[nodiscard]] auto rbegin(const Self& self) {
    if constexpr (std::is_constructible_v<__reversed__<Self>, Self>) {
        return __reversed__<Self>(std::forward<Self>(self));

    } else if constexpr (meta::has_cpp<Self>) {
        return std::ranges::rbegin(from_python(std::forward<Self>(self)));

    } else {
        using T = typename __reversed__<Self>::type;
        Object builtins = steal<Object>(
            PyFrame_GetBuiltins(PyEval_GetFrame())
        );
        Object func = steal<Object>(PyDict_GetItemWithError(
            ptr(builtins),
            ptr(impl::template_string<"reversed">())
        ));
        if (func.is(nullptr)) {
            if (PyErr_Occurred()) {
                Exception::from_python();
            }
            throw KeyError("'reversed'");
        }
        PyObject* iter = PyObject_CallOneArg(ptr(func), ptr(self));
        if (iter == nullptr) {
            Exception::from_python();
        }
        return __iter__<Iterator<T>>{steal<Iterator<T>>(iter)};
    }
}


template <meta::python Self>
    requires (__reversed__<Self>::enable && (
        std::is_constructible_v<__reversed__<Self>, Self> ||
        (meta::has_cpp<Self> && meta::reverse_iterable<meta::cpp_type<Self>>) ||
        (!meta::has_cpp<Self> && std::derived_from<typename __reversed__<Self>::type, Object>)
    ))
[[nodiscard]] auto rbegin(Self& self) {
    return rbegin(reinterpret_cast<std::add_const_t<Self>&>(self));
}


/* Const reverse iteration operator.  Python has no distinction between mutable
and immutable iterators, so this is fundamentally the same as the ordinary
rbegin() method.  Some libraries assume the existence of this method. */
template <meta::python Self>
    requires (__reversed__<Self>::enable && std::is_const_v<std::remove_reference_t<Self>>)
[[nodiscard]] auto crbegin(const Self& self) {
    return rbegin(std::forward<Self>(self));
}


/* Reverse end operator.  This terminates the reverse iteration and is controlled
by the __reversed__ control struct. */
template <meta::python Self>
    requires (__reversed__<Self>::enable && (
        std::is_constructible_v<__reversed__<Self>, Self> ||
        (meta::has_cpp<Self> && meta::reverse_iterable<meta::cpp_type<Self>>) ||
        (!meta::has_cpp<Self> && std::derived_from<typename __reversed__<Self>::type, Object>)
    ))
[[nodiscard]] auto rend(const Self& self) {
    if constexpr (std::is_constructible_v<__reversed__<Self>, Self>) {
        return sentinel{};

    } else if constexpr (meta::has_cpp<Self>) {
        return std::ranges::rend(from_python(std::forward<Self>(self)));

    } else {
        return sentinel{};
    }
}


template <meta::python Self>
    requires (__reversed__<Self>::enable && (
        std::is_constructible_v<__reversed__<Self>, Self> ||
        (meta::has_cpp<Self> && meta::reverse_iterable<meta::cpp_type<Self>>) ||
        (!meta::has_cpp<Self> && std::derived_from<typename __reversed__<Self>::type, Object>)
    ))
[[nodiscard]] auto rend(Self& self) {
    return rend(reinterpret_cast<std::add_const_t<Self>&>(self));
}


/* Const reverse end operator.  Similar to `crbegin()`, this is identical to
`rend()`. */
template <meta::python Self>
    requires (__reversed__<Self>::enable && (
        std::is_constructible_v<__reversed__<Self>, Self> ||
        (meta::has_cpp<Self> && meta::reverse_iterable<meta::cpp_type<Self>>) ||
        (!meta::has_cpp<Self> && std::derived_from<typename __reversed__<Self>::type, Object>)
    ) && std::is_const_v<std::remove_reference_t<Self>>)
[[nodiscard]] auto crend(const Self& self) {
    return rend(std::forward<Self>(self));
}


/* Contains operator.  Equivalent to Python's `in` keyword, but as a freestanding
non-member function (i.e. `x in y` -> `bertrand::in(x, y)`).  A member equivalent is
defined for all subclasses of `Object` (i.e. `x.in(y)`), which delegates to this
function. */
template <typename Key, typename Container>
    requires (
        __contains__<Container, Key>::enable &&
        std::same_as<typename __contains__<Container, Key>::type, bool> && (
            std::is_invocable_r_v<bool, __contains__<Container, Key>, Container, Key> || (
                !std::is_invocable_v<__contains__<Container, Key>, Container, Key> &&
                meta::has_cpp<Container> &&
                meta::has_contains<meta::cpp_type<Container>, meta::cpp_type<Key>>
            ) || (
                !std::is_invocable_v<__contains__<Container, Key>, Container, Key> &&
                !meta::has_cpp<Container>
            )
        )
    )
[[nodiscard]] bool in(Key&& key, Container&& container) {
    if constexpr (std::is_invocable_v<__contains__<Container, Key>, Container, Key>) {
        return __contains__<Container, Key>{}(
            std::forward<Container>(container),
            std::forward<Key>(key)
        );

    } else if constexpr (meta::has_cpp<Container>) {
        return from_python(std::forward<Container>(container)).contains(
            from_python(std::forward<Key>(key))
        );

    } else {
        int result = PySequence_Contains(
            ptr(to_python(std::forward<Container>(container))),
            ptr(to_python(std::forward<Key>(key)))
        );
        if (result == -1) {
            Exception::from_python();
        }
        return result;
    }
}


/* Member equivalent for `bertrand::in()` function, which simplifies the syntax if the
key is already a Python object */
template <typename Self, typename Key>
    requires (
        __contains__<Self, Key>::enable &&
        std::same_as<typename __contains__<Self, Key>::type, bool> && (
            std::is_invocable_r_v<bool, __contains__<Self, Key>, Self, Key> || (
                !std::is_invocable_v<__contains__<Self, Key>, Self, Key> &&
                meta::has_cpp<Self> &&
                meta::has_contains<meta::cpp_type<Self>, meta::cpp_type<Key>>
            ) || (
                !std::is_invocable_v<__contains__<Self, Key>, Self, Key> &&
                !meta::has_cpp<Self>
            )
        )
    )
[[nodiscard]] inline bool Object::in(this Self&& self, Key&& key) {
    return bertrand::in(std::forward<Key>(key), std::forward<Self>(self));
}


/* Equivalent to Python `len(obj)`. */
template <typename Self>
    requires (
        __len__<Self>::enable &&
        std::convertible_to<typename __len__<Self>::type, size_t> && (
            std::is_invocable_r_v<size_t, __len__<Self>, Self> || (
                !std::is_invocable_v<__len__<Self>, Self> &&
                meta::has_cpp<Self> &&
                meta::has_size<meta::cpp_type<Self>>
            ) || (
                !std::is_invocable_v<__len__<Self>, Self> &&
                !meta::has_cpp<Self>
            )
        )
    )
[[nodiscard]] size_t len(Self&& obj) {
    if constexpr (std::is_invocable_v<__len__<Self>, Self>) {
        return __len__<Self>{}(std::forward<Self>(obj));

    } else if constexpr (meta::has_cpp<Self>) {
        return std::ranges::size(from_python(std::forward<Self>(obj)));

    } else {
        Py_ssize_t size = PyObject_Length(
            ptr(to_python(std::forward<Self>(obj)))
        );
        if (size < 0) {
            Exception::from_python();
        }
        return size;
    }
}


/* Equivalent to Python `len(obj)`, except that it works on C++ objects implementing a
`size()` method. */
template <typename Self> requires (!__len__<Self>::enable && meta::has_size<Self>)
[[nodiscard]] size_t len(Self&& obj) {
    return std::ranges::size(std::forward<Self>(obj));
}


/* An alias for `bertrand::len(obj)`, but triggers ADL for constructs that expect a
free-floating size() function. */
template <typename Self>
    requires (
        __len__<Self>::enable &&
        std::convertible_to<typename __len__<Self>::type, size_t> && (
            std::is_invocable_r_v<size_t , __len__<Self>, Self> || (
                !std::is_invocable_v<__len__<Self>, Self> &&
                meta::has_cpp<Self> &&
                meta::has_size<meta::cpp_type<Self>>
            ) || (
                !std::is_invocable_v<__len__<Self>, Self> &&
                !meta::has_cpp<Self>
            )
        )
    )
[[nodiscard]] size_t size(Self&& obj) {
    return len(std::forward<Self>(obj));
}


/* An alias for `bertrand::len(obj)`, but triggers ADL for constructs that expect a
free-floating size() function. */
template <typename Self> requires (!__len__<Self>::enable && meta::has_size<Self>)
[[nodiscard]] size_t size(Self&& obj) {
    return len(std::forward<Self>(obj));
}


////////////////////////////////////
////    ARITHMETIC OPERATORS    ////
////////////////////////////////////


/* Equivalent to Python `abs(obj)` for any object that specializes the __abs__ control
struct. */
template <typename Self>
    requires (__abs__<Self>::enable && (
        std::is_invocable_r_v<typename __abs__<Self>::type, __abs__<Self>, Self> || (
            !std::is_invocable_v<__abs__<Self>, Self> &&
            meta::has_cpp<Self> &&
            meta::abs_returns<meta::cpp_type<Self>, typename __abs__<Self>::type>
        ) || (
            !std::is_invocable_v<__abs__<Self>, Self> &&
            !meta::has_cpp<Self> &&
            meta::python<typename __abs__<Self>::type> &&
            !meta::is_qualified<typename __abs__<Self>::type>
        )
    ))
[[nodiscard]] decltype(auto) abs(Self&& self) {
    using Return = std::remove_cvref_t<typename __abs__<Self>::type>;
    if constexpr (std::is_invocable_v<__abs__<Self>, Self>) {
        return __abs__<Self>{}(std::forward<Self>(self));

    } else if constexpr (meta::has_cpp<Self>) {
        return std::abs(from_python(std::forward<Self>(self)));

    } else {
        Return result = steal<Return>(PyNumber_Absolute(
            ptr(to_python(std::forward<Self>(self)))
        ));
        if (result.is(nullptr)) {
            Exception::from_python();
        }
        return result;
    }
}


/* Equivalent to Python `abs(obj)`, except that it takes a C++ value and applies
std::abs() for identical semantics. */
template <meta::has_abs Self> requires (!__abs__<Self>::enable)
[[nodiscard]] decltype(auto) abs(Self&& value) {
    return std::abs(std::forward<Self>(value));
}


template <meta::python Self> requires (!__invert__<Self>::enable)
decltype(auto) operator~(Self&& self) = delete;
template <meta::python Self>
    requires (__invert__<Self>::enable && (
        std::is_invocable_r_v<typename __invert__<Self>::type, __invert__<Self>, Self> || (
            !std::is_invocable_v<__invert__<Self>, Self> &&
            meta::has_cpp<Self> &&
            meta::invert_returns<meta::cpp_type<Self>, typename __invert__<Self>::type>
        ) || (
            !std::is_invocable_v<__invert__<Self>, Self> &&
            !meta::has_cpp<Self> &&
            meta::python<typename __invert__<Self>::type> &&
            !meta::is_qualified<typename __invert__<Self>::type>
        )
    ))
decltype(auto) operator~(Self&& self) {
    if constexpr (std::is_invocable_v<__invert__<Self>, Self>) {
        return __invert__<Self>{}(std::forward<Self>(self));

    } else if constexpr (meta::has_cpp<Self>) {
        return ~from_python(std::forward<Self>(self));

    } else {
        using Return = __invert__<Self>::type;
        Return result = steal<Return>(PyNumber_Invert(ptr(self)));
        if (result.is(nullptr)) {
            Exception::from_python();
        }
        return result;
    }
}


template <meta::python Self> requires (!__pos__<Self>::enable)
decltype(auto) operator+(Self&& self) = delete;
template <meta::python Self>
    requires (__pos__<Self>::enable && (
        std::is_invocable_r_v<typename __pos__<Self>::type, __pos__<Self>, Self> || (
            !std::is_invocable_v<__pos__<Self>, Self> &&
            meta::has_cpp<Self> &&
            meta::pos_returns<meta::cpp_type<Self>, typename __pos__<Self>::type>
        ) || (
            !std::is_invocable_v<__pos__<Self>, Self> &&
            !meta::has_cpp<Self> &&
            meta::python<typename __pos__<Self>::type> &&
            !meta::is_qualified<typename __pos__<Self>::type>
        )
    ))
decltype(auto) operator+(Self&& self) {
    if constexpr (std::is_invocable_v<__pos__<Self>, Self>) {
        return __pos__<Self>{}(std::forward<Self>(self));

    } else if constexpr (meta::has_cpp<Self>) {
        return +from_python(std::forward<Self>(self));

    } else {
        using Return = __pos__<Self>::type;
        Return result = steal<Return>(PyNumber_Positive(ptr(self)));
        if (result.is(nullptr)) {
            Exception::from_python();
        }
        return result;
    }
}


template <meta::python Self> requires (!__neg__<Self>::enable)
decltype(auto) operator-(Self&& self) = delete;
template <meta::python Self>
    requires (__neg__<Self>::enable && (
        std::is_invocable_r_v<typename __neg__<Self>::type, __neg__<Self>, Self> || (
            !std::is_invocable_v<__neg__<Self>, Self> &&
            meta::has_cpp<Self> &&
            meta::neg_returns<meta::cpp_type<Self>, typename __neg__<Self>::type>
        ) || (
            !std::is_invocable_v<__neg__<Self>, Self> &&
            !meta::has_cpp<Self> &&
            meta::python<typename __neg__<Self>::type> &&
            !meta::is_qualified<typename __neg__<Self>::type>
        )
    ))
decltype(auto) operator-(Self&& self) {
    if constexpr (std::is_invocable_v<__neg__<Self>, Self>) {
        return __neg__<Self>{}(std::forward<Self>(self));

    } else if constexpr (meta::has_cpp<Self>) {
        return -from_python(std::forward<Self>(self));

    } else {
        using Return = __neg__<Self>::type;
        Return result = steal<Return>(PyNumber_Negative(ptr(self)));
        if (result.is(nullptr)) {
            Exception::from_python();
        }
        return result;
    }
}


template <meta::python Self>
decltype(auto) operator++(Self&& self, int) = delete;  // post-increment is not valid
template <meta::python Self> requires (!__increment__<Self>::enable)
decltype(auto) operator++(Self&& self) = delete;
template <meta::python Self>
    requires (__increment__<Self>::enable && (
        std::is_invocable_r_v<typename __increment__<Self>::type, __increment__<Self>, Self> || (
            !std::is_invocable_v<__increment__<Self>, Self> &&
            meta::has_cpp<Self> &&
            meta::preincrement_returns<meta::cpp_type<Self>, typename __increment__<Self>::type>
        ) || (
            !std::is_invocable_v<__increment__<Self>, Self> &&
            !meta::has_cpp<Self> &&
            !meta::is_const<Self> &&
            std::same_as<typename __increment__<Self>::type, Self>
        )
    ))
decltype(auto) operator++(Self&& self) {
    if constexpr (std::is_invocable_v<__increment__<Self>, Self>) {
        return __increment__<Self>{}(std::forward<Self>(self));

    } else if constexpr (meta::has_cpp<Self>) {
        return ++from_python(std::forward<Self>(self));

    } else {
        using Return = std::remove_cvref_t<typename __increment__<Self>::type>;
        Object one = steal<Object>(PyLong_FromLong(1));
        if (one.is(nullptr)) {
            Exception::from_python();
        }
        Return result = steal<Return>(
            PyNumber_InPlaceAdd(ptr(self), ptr(one))
        );
        if (result.is(nullptr)) {
            Exception::from_python();
        } else if (!self.is(result)) {
            self = std::move(result);
        }
        return std::forward<Self>(self);
    }
}


template <meta::python Self>
decltype(auto) operator--(Self& self, int) = delete;  // post-decrement is not valid
template <meta::python Self> requires (!__decrement__<Self>::enable)
decltype(auto) operator--(Self& self) = delete;
template <meta::python Self>
    requires (__decrement__<Self>::enable && (
        std::is_invocable_r_v<typename __decrement__<Self>::type, __decrement__<Self>, Self> || (
            !std::is_invocable_v<__decrement__<Self>, Self> &&
            meta::has_cpp<Self> &&
            meta::predecrement_returns<meta::cpp_type<Self>, typename __decrement__<Self>::type>
        ) || (
            !std::is_invocable_v<__decrement__<Self>, Self> &&
            !meta::has_cpp<Self> &&
            !meta::is_const<Self> &&
            std::same_as<typename __decrement__<Self>::type, Self>
        )
    ))
decltype(auto) operator--(Self&& self) {
    if constexpr (std::is_invocable_v<__decrement__<Self>, Self>) {
        return __decrement__<Self>{}(std::forward<Self>(self));

    } else if constexpr (meta::has_cpp<Self>) {
        return --from_python(std::forward<Self>(self));

    } else {
        using Return = std::remove_cvref_t<typename __decrement__<Self>::type>;
        Object one = steal<Object>(PyLong_FromLong(1));
        if (one.is(nullptr)) {
            Exception::from_python();
        }
        Return result = steal<Return>(
            PyNumber_InPlaceSubtract(ptr(self), ptr(one))
        );
        if (result.is(nullptr)) {
            Exception::from_python();
        } else if (!self.is(result)) {
            self = std::move(result);
        }
        return std::forward<Self>(self);
    }
}


template <typename L, typename R>
    requires ((meta::python<L> || meta::python<R>) && !__lt__<L, R>::enable)
decltype(auto) operator<(L&& lhs, R&& rhs) = delete;
template <typename L, typename R>
    requires (__lt__<L, R>::enable && (
        std::is_invocable_r_v<typename __lt__<L, R>::type, __lt__<L, R>, L, R> || (
            !std::is_invocable_v<__lt__<L, R>, L, R> &&
            (meta::has_cpp<L> && meta::has_cpp<R>) &&
            meta::lt_returns<meta::cpp_type<L>, meta::cpp_type<R>, typename __lt__<L, R>::type>
        ) || (
            !std::is_invocable_v<__lt__<L, R>, L, R> &&
            !(meta::has_cpp<L> && meta::has_cpp<R>) &&
            meta::python<typename __lt__<L, R>::type> &&
            !meta::is_qualified<typename __lt__<L, R>::type>
        )
    ))
decltype(auto) operator<(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__lt__<L, R>, L, R>) {
        return __lt__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (meta::has_cpp<L> && meta::has_cpp<R>) {
        return
            from_python(std::forward<L>(lhs)) <
            from_python(std::forward<R>(rhs));

    } else {
        using Return = __lt__<L, R>::type;
        Return result = steal<Return>(PyObject_RichCompare(
            ptr(to_python(std::forward<L>(lhs))),
            ptr(to_python(std::forward<R>(rhs))),
            Py_LT
        ));
        if (result.is(nullptr)) {
            Exception::from_python();
        }
        return result;
    }
}


template <typename L, typename R>
    requires ((meta::python<L> || meta::python<R>) && !__le__<L, R>::enable)
decltype(auto) operator<=(L&& lhs, R&& rhs) = delete;
template <typename L, typename R>
    requires (__le__<L, R>::enable && (
        std::is_invocable_r_v<typename __le__<L, R>::type, __le__<L, R>, L, R> || (
            !std::is_invocable_v<__le__<L, R>, L, R> &&
            (meta::has_cpp<L> && meta::has_cpp<R>) &&
            meta::le_returns<meta::cpp_type<L>, meta::cpp_type<R>, typename __le__<L, R>::type>
        ) || (
            !std::is_invocable_v<__le__<L, R>, L, R> &&
            !(meta::has_cpp<L> && meta::has_cpp<R>) &&
            meta::python<typename __le__<L, R>::type> &&
            !meta::is_qualified<typename __le__<L, R>::type>
        )
    ))
decltype(auto) operator<=(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__le__<L, R>, L, R>) {
        return __le__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (meta::has_cpp<L> && meta::has_cpp<R>) {
        return
            from_python(std::forward<L>(lhs)) <=
            from_python(std::forward<R>(rhs));

    } else {
        using Return = __le__<L, R>::type;
        Return result = steal<Return>(PyObject_RichCompare(
            ptr(to_python(std::forward<L>(lhs))),
            ptr(to_python(std::forward<R>(rhs))),
            Py_LE
        ));
        if (result.is(nullptr)) {
            Exception::from_python();
        }
        return result;
    }
}


template <typename L, typename R>
    requires ((meta::python<L> || meta::python<R>) && !__eq__<L, R>::enable)
decltype(auto) operator==(L&& lhs, R&& rhs) = delete;
template <typename L, typename R>
    requires (__eq__<L, R>::enable && (
        std::is_invocable_r_v<typename __eq__<L, R>::type, __eq__<L, R>, L, R> || (
            !std::is_invocable_v<__eq__<L, R>, L, R> &&
            (meta::has_cpp<L> && meta::has_cpp<R>) &&
            meta::eq_returns<meta::cpp_type<L>, meta::cpp_type<R>, typename __eq__<L, R>::type>
        ) || (
            !std::is_invocable_v<__eq__<L, R>, L, R> &&
            !(meta::has_cpp<L> && meta::has_cpp<R>) &&
            meta::python<typename __eq__<L, R>::type> &&
            !meta::is_qualified<typename __eq__<L, R>::type>
        )
    ))
decltype(auto) operator==(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__eq__<L, R>, L, R>) {
        return __eq__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (meta::has_cpp<L> && meta::has_cpp<R>) {
        return
            from_python(std::forward<L>(lhs)) ==
            from_python(std::forward<R>(rhs));

    } else {
        using Return = __eq__<L, R>::type;
        Return result = steal<Return>(PyObject_RichCompare(
            ptr(to_python(std::forward<L>(lhs))),
            ptr(to_python(std::forward<R>(rhs))),
            Py_EQ
        ));
        if (result.is(nullptr)) {
            Exception::from_python();
        }
        return result;
    }
}


template <typename L, typename R>
    requires ((meta::python<L> || meta::python<R>) && !__ne__<L, R>::enable)
decltype(auto) operator!=(L&& lhs, R&& rhs) = delete;
template <typename L, typename R>
    requires (__ne__<L, R>::enable && (
        std::is_invocable_r_v<typename __ne__<L, R>::type, __ne__<L, R>, L, R> || (
            !std::is_invocable_v<__ne__<L, R>, L, R> &&
            (meta::has_cpp<L> && meta::has_cpp<R>) &&
            meta::ne_returns<meta::cpp_type<L>, meta::cpp_type<R>, typename __ne__<L, R>::type>
        ) || (
            !std::is_invocable_v<__ne__<L, R>, L, R> &&
            !(meta::has_cpp<L> && meta::has_cpp<R>) &&
            meta::python<typename __ne__<L, R>::type> &&
            !meta::is_qualified<typename __ne__<L, R>::type>
        )
    ))
decltype(auto) operator!=(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__ne__<L, R>, L, R>) {
        return __ne__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (meta::has_cpp<L> && meta::has_cpp<R>) {
        return
            from_python(std::forward<L>(lhs)) !=
            from_python(std::forward<R>(rhs));

    } else {
        using Return = __ne__<L, R>::type;
        Return result = steal<Return>(PyObject_RichCompare(
            ptr(to_python(std::forward<L>(lhs))),
            ptr(to_python(std::forward<R>(rhs))),
            Py_NE
        ));
        if (result.is(nullptr)) {
            Exception::from_python();
        }
        return result;
    }
}


template <typename L, typename R>
    requires ((meta::python<L> || meta::python<R>) && !__ge__<L, R>::enable)
decltype(auto) operator>=(L&& lhs, R&& rhs) = delete;
template <typename L, typename R>
    requires (__ge__<L, R>::enable && (
        std::is_invocable_r_v<typename __ge__<L, R>::type, __ge__<L, R>, L, R> || (
            !std::is_invocable_v<__ge__<L, R>, L, R> &&
            (meta::has_cpp<L> && meta::has_cpp<R>) &&
            meta::ge_returns<meta::cpp_type<L>, meta::cpp_type<R>, typename __ge__<L, R>::type>
        ) || (
            !std::is_invocable_v<__ge__<L, R>, L, R> &&
            !(meta::has_cpp<L> && meta::has_cpp<R>) &&
            meta::python<typename __ge__<L, R>::type> &&
            !meta::is_qualified<typename __ge__<L, R>::type>
        )
    ))
decltype(auto) operator>=(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__ge__<L, R>, L, R>) {
        return __ge__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (meta::has_cpp<L> && meta::has_cpp<R>) {
        return
            from_python(std::forward<L>(lhs)) >=
            from_python(std::forward<R>(rhs));

    } else {
        using Return = __ge__<L, R>::type;
        Return result = steal<Return>(PyObject_RichCompare(
            ptr(to_python(std::forward<L>(lhs))),
            ptr(to_python(std::forward<R>(rhs))),
            Py_GE
        ));
        if (result.is(nullptr)) {
            Exception::from_python();
        }
        return result;
    }
}


template <typename L, typename R>
    requires ((meta::python<L> || meta::python<R>) && !__gt__<L, R>::enable)
decltype(auto) operator>(L&& lhs, R&& rhs) = delete;
template <typename L, typename R>
    requires (__gt__<L, R>::enable && (
        std::is_invocable_r_v<typename __gt__<L, R>::type, __gt__<L, R>, L, R> || (
            !std::is_invocable_v<__gt__<L, R>, L, R> &&
            (meta::has_cpp<L> && meta::has_cpp<R>) &&
            meta::gt_returns<meta::cpp_type<L>, meta::cpp_type<R>, typename __gt__<L, R>::type>
        ) || (
            !std::is_invocable_v<__gt__<L, R>, L, R> &&
            !(meta::has_cpp<L> && meta::has_cpp<R>) &&
            meta::python<typename __gt__<L, R>::type> &&
            !meta::is_qualified<typename __gt__<L, R>::type>
        )
    ))
decltype(auto) operator>(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__gt__<L, R>, L, R>) {
        return __gt__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (meta::has_cpp<L> && meta::has_cpp<R>) {
        return
            from_python(std::forward<L>(lhs)) >
            from_python(std::forward<R>(rhs));

    } else {
        using Return = __gt__<L, R>::type;
        Return result = steal<Return>(PyObject_RichCompare(
            ptr(to_python(std::forward<L>(lhs))),
            ptr(to_python(std::forward<R>(rhs))),
            Py_GT
        ));
        if (result.is(nullptr)) {
            Exception::from_python();
        }
        return result;
    }
}


template <typename L, typename R>
    requires ((meta::python<L> || meta::python<R>) && !__add__<L, R>::enable)
decltype(auto) operator+(L&& lhs, R&& rhs) = delete;
template <typename L, typename R>
    requires (__add__<L, R>::enable && (
        std::is_invocable_r_v<typename __add__<L, R>::type, __add__<L, R>, L, R> || (
            !std::is_invocable_v<__add__<L, R>, L, R> &&
            (meta::has_cpp<L> && meta::has_cpp<R>) &&
            meta::add_returns<meta::cpp_type<L>, meta::cpp_type<R>, typename __add__<L, R>::type>
        ) || (
            !std::is_invocable_v<__add__<L, R>, L, R> &&
            !(meta::has_cpp<L> && meta::has_cpp<R>) &&
            meta::python<typename __add__<L, R>::type> &&
            !meta::is_qualified<typename __add__<L, R>::type>
        )
    ))
decltype(auto) operator+(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__add__<L, R>, L, R>) {
        return __add__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (meta::has_cpp<L> && meta::has_cpp<R>) {
        return
            from_python(std::forward<L>(lhs)) +
            from_python(std::forward<R>(rhs));

    } else {
        using Return = __add__<L, R>::type;
        Return result = steal<Return>(PyNumber_Add(
            ptr(to_python(std::forward<L>(lhs))),
            ptr(to_python(std::forward<R>(rhs)))
        ));
        if (result.is(nullptr)) {
            Exception::from_python();
        }
        return result;
    }
}


template <meta::python L, typename R> requires (!__iadd__<L, R>::enable)
decltype(auto) operator+=(L& lhs, R&& rhs) = delete;
template <meta::python L, typename R>
    requires (__iadd__<L, R>::enable && (
        std::is_invocable_r_v<typename __iadd__<L, R>::type, __iadd__<L, R>, L, R> || (
            !std::is_invocable_v<__iadd__<L, R>, L, R> &&
            (meta::has_cpp<L> && meta::has_cpp<R>) &&
            meta::iadd_returns<meta::cpp_type<L>, meta::cpp_type<R>, typename __iadd__<L, R>::type>
        ) || (
            !std::is_invocable_v<__iadd__<L, R>, L, R> &&
            !(meta::has_cpp<L> && meta::has_cpp<R>) &&
            !meta::is_const<L> &&
            std::same_as<typename __iadd__<L, R>::type, L>
        )
    ))
decltype(auto) operator+=(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__iadd__<L, R>, L, R>) {
        return __iadd__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (meta::has_cpp<L> && meta::has_cpp<R>) {
        return
            from_python(std::forward<L>(lhs)) +=
            from_python(std::forward<R>(rhs));

    } else {
        using Return = std::remove_cvref_t<typename __iadd__<L, R>::type>;
        Return result = steal<Return>(PyNumber_InPlaceAdd(
            ptr(lhs),
            ptr(to_python(std::forward<R>(rhs)))
        ));
        if (result.is(nullptr)) {
            Exception::from_python();
        } else if (!lhs.is(result)) {
            lhs = std::move(result);
        }
        return std::forward<L>(lhs);
    }
}


template <typename L, typename R>
    requires ((meta::python<L> || meta::python<R>) && !__sub__<L, R>::enable)
decltype(auto) operator-(L&& lhs, R&& rhs) = delete;
template <typename L, typename R>
    requires (__sub__<L, R>::enable && (
        std::is_invocable_r_v<typename __sub__<L, R>::type, __sub__<L, R>, L, R> || (
            !std::is_invocable_v<__sub__<L, R>, L, R> &&
            (meta::has_cpp<L> && meta::has_cpp<R>) &&
            meta::sub_returns<meta::cpp_type<L>, meta::cpp_type<R>, typename __sub__<L, R>::type>
        ) || (
            !std::is_invocable_v<__sub__<L, R>, L, R> &&
            !(meta::has_cpp<L> && meta::has_cpp<R>) &&
            meta::python<typename __sub__<L, R>::type> &&
            !meta::is_qualified<typename __sub__<L, R>::type>
        )
    ))
decltype(auto) operator-(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__sub__<L, R>, L, R>) {
        return __sub__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (meta::has_cpp<L> && meta::has_cpp<R>) {
        return
            from_python(std::forward<L>(lhs)) -
            from_python(std::forward<R>(rhs));

    } else {
        using Return = __sub__<L, R>::type;
        Return result = steal<Return>(PyNumber_Subtract(
            ptr(to_python(std::forward<L>(lhs))),
            ptr(to_python(std::forward<R>(rhs)))
        ));
        if (result.is(nullptr)) {
            Exception::from_python();
        }
        return result;
    }
}


template <meta::python L, typename R> requires (!__isub__<L, R>::enable)
decltype(auto) operator-=(L& lhs, R&& rhs) = delete;
template <meta::python L, typename R>
    requires (__isub__<L, R>::enable && (
        std::is_invocable_r_v<typename __isub__<L, R>::type, __isub__<L, R>, L, R> || (
            !std::is_invocable_v<__isub__<L, R>, L, R> &&
            (meta::has_cpp<L> && meta::has_cpp<R>) &&
            meta::isub_returns<meta::cpp_type<L>, meta::cpp_type<R>, typename __isub__<L, R>::type>
        ) || (
            !std::is_invocable_v<__isub__<L, R>, L, R> &&
            !(meta::has_cpp<L> && meta::has_cpp<R>) &&
            !meta::is_const<L> &&
            std::same_as<typename __isub__<L, R>::type, L>
        )
    ))
decltype(auto) operator-=(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__isub__<L, R>, L, R>) {
        return __isub__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (meta::has_cpp<L> && meta::has_cpp<R>) {
        return
            from_python(std::forward<L>(lhs)) -=
            from_python(std::forward<R>(rhs));

    } else {
        using Return = std::remove_cvref_t<typename __isub__<L, R>::type>;
        Return result = steal<Return>(PyNumber_InPlaceSubtract(
            ptr(lhs),
            ptr(to_python(std::forward<R>(rhs)))
        ));
        if (result.is(nullptr)) {
            Exception::from_python();
        } else if (!lhs.is(result)) {
            lhs = std::move(result);
        }
        return std::forward<L>(lhs);
    }
}


template <typename L, typename R>
    requires ((meta::python<L> || meta::python<R>) && !__mul__<L, R>::enable)
decltype(auto) operator*(L&& lhs, R&& rhs) = delete;
template <typename L, typename R>
    requires (__mul__<L, R>::enable && (
        std::is_invocable_r_v<typename __mul__<L, R>::type, __mul__<L, R>, L, R> || (
            !std::is_invocable_v<__mul__<L, R>, L, R> &&
            (meta::has_cpp<L> && meta::has_cpp<R>) &&
            meta::mul_returns<meta::cpp_type<L>, meta::cpp_type<R>, typename __mul__<L, R>::type>
        ) || (
            !std::is_invocable_v<__mul__<L, R>, L, R> &&
            !(meta::has_cpp<L> && meta::has_cpp<R>) &&
            meta::python<typename __mul__<L, R>::type> &&
            !meta::is_qualified<typename __mul__<L, R>::type>
        )
    ))
decltype(auto) operator*(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__mul__<L, R>, L, R>) {
        return __mul__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (meta::has_cpp<L> && meta::has_cpp<R>) {
        return
            from_python(std::forward<L>(lhs)) *
            from_python(std::forward<R>(rhs));

    } else {
        using Return = __mul__<L, R>::type;
        Return result = steal<Return>(PyNumber_Multiply(
            ptr(to_python(std::forward<L>(lhs))),
            ptr(to_python(std::forward<R>(rhs)))
        ));
        if (result.is(nullptr)) {
            Exception::from_python();
        }
        return result;
    }
}


template <meta::python L, typename R> requires (!__imul__<L, R>::enable)
decltype(auto) operator*=(L& lhs, R&& rhs) = delete;
template <meta::python L, typename R>
    requires (__imul__<L, R>::enable && (
        std::is_invocable_r_v<typename __imul__<L, R>::type, __imul__<L, R>, L, R> || (
            !std::is_invocable_v<__imul__<L, R>, L, R> &&
            (meta::has_cpp<L> && meta::has_cpp<R>) &&
            meta::imul_returns<meta::cpp_type<L>, meta::cpp_type<R>, typename __imul__<L, R>::type>
        ) || (
            !std::is_invocable_v<__imul__<L, R>, L, R> &&
            !(meta::has_cpp<L> && meta::has_cpp<R>) &&
            !meta::is_const<L> &&
            std::same_as<typename __imul__<L, R>::type, L>
        )
    ))
decltype(auto) operator*=(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__imul__<L, R>, L, R>) {
        return __imul__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (meta::has_cpp<L> && meta::has_cpp<R>) {
        return
            from_python(std::forward<L>(lhs)) *=
            from_python(std::forward<R>(rhs));

    } else {
        using Return = std::remove_cvref_t<typename __imul__<L, R>::type>;
        Return result = steal<Return>(PyNumber_InPlaceMultiply(
            ptr(lhs),
            ptr(to_python(std::forward<R>(rhs)))
        ));
        if (result.is(nullptr)) {
            Exception::from_python();
        } else if (!lhs.is(result)) {
            lhs = std::move(result);
        }
        return std::forward<L>(lhs);
    }
}


/* Equivalent to Python `base ** exp` (exponentiation). */
template <typename Base, typename Exp>
    requires (__pow__<Base, Exp>::enable && (
        std::is_invocable_r_v<typename __pow__<Base, Exp>::type, __pow__<Base, Exp>, Base, Exp> || (
            !std::is_invocable_v<__pow__<Base, Exp>, Base, Exp> &&
            (meta::has_cpp<Base> && meta::has_cpp<Exp>) &&
            meta::pow_returns<meta::cpp_type<Base>, meta::cpp_type<Exp>, typename __pow__<Base, Exp>::type>
        ) && (
            !std::is_invocable_v<__pow__<Base, Exp>, Base, Exp> &&
            !(meta::has_cpp<Base> && meta::has_cpp<Exp>) &&
            meta::python<typename __pow__<Base, Exp>::type> &&
            !meta::is_qualified<typename __pow__<Base, Exp>::type>
        )
    ))
decltype(auto) pow(Base&& base, Exp&& exp) {
    if constexpr (std::is_invocable_v<__pow__<Base, Exp>, Base, Exp>) {
        return __pow__<Base, Exp>{}(std::forward<Base>(base), std::forward<Exp>(exp));

    } else if constexpr (meta::has_cpp<Base> && meta::has_cpp<Exp>) {
        if constexpr (
            meta::complex_like<meta::cpp_type<Base>> &&
            meta::complex_like<meta::cpp_type<Exp>>
        ) {
            return std::common_type_t<meta::cpp_type<Base>, meta::cpp_type<Exp>>(
                pow(from_python(base).real(), from_python(exp).real()),
                pow(from_python(base).imag(), from_python(exp).imag())
            );
        } else if constexpr (meta::complex_like<meta::cpp_type<Base>>) {
            return Base(
                pow(from_python(base).real(), from_python(exp)),
                pow(from_python(base).real(), from_python(exp))
            );
        } else if constexpr (meta::complex_like<meta::cpp_type<Exp>>) {
            return Exp(
                pow(from_python(base), from_python(exp).real()),
                pow(from_python(base), from_python(exp).imag())
            );
        } else {
            return std::pow(
                from_python(std::forward<Base>(base)),
                from_python(std::forward<Exp>(exp))
            );
        }

    } else {
        using Return = __pow__<Base, Exp>::type;
        Return result = steal<Return>(PyNumber_Power(
            ptr(to_python(std::forward<Base>(base))),
            ptr(to_python(std::forward<Exp>(exp))),
            Py_None
        ));
        if (result.is(nullptr)) {
            Exception::from_python();
        }
        return result;
    }
}


/* Equivalent to Python `pow(base, exp)`, except that it takes a C++ value and applies
std::pow() for identical semantics. */
template <meta::cpp Base, meta::cpp Exp>
    requires (!__pow__<Base, Exp>::enable && (
        meta::complex_like<Base> ||
        meta::complex_like<Exp> ||
        meta::has_pow<Base, Exp>
    ))
decltype(auto) pow(Base&& base, Exp&& exp) {
    if constexpr (meta::complex_like<Base> && meta::complex_like<Exp>) {
        return std::common_type_t<std::remove_cvref_t<Base>, std::remove_cvref_t<Exp>>(
            pow(base.real(), exp.real()),
            pow(base.imag(), exp.imag())
        );
    } else if constexpr (meta::complex_like<Base>) {
        return Base(
            pow(base.real(), exp),
            pow(base.imag(), exp)
        );
    } else if constexpr (meta::complex_like<Exp>) {
        return Exp(
            pow(base, exp.real()),
            pow(base, exp.imag())
        );
    } else {
        return std::pow(base, exp);
    }
}


/* Equivalent to Python `pow(base, exp, mod)`. */
template <typename Base, typename Exp, typename Mod>
    requires (__pow__<Base, Exp, Mod>::enable && (
        std::is_invocable_r_v<
            typename __pow__<Base, Exp, Mod>::type,
            __pow__<Base, Exp, Mod>,
            Base,
            Exp,
            Mod
        > || (
            !std::is_invocable_v<__pow__<Base, Exp, Mod>, Base, Exp, Mod> &&
            meta::python<typename __pow__<Base, Exp, Mod>::type> &&
            !meta::is_qualified<typename __pow__<Base, Exp, Mod>::type>
        )
    ))
decltype(auto) pow(Base&& base, Exp&& exp, Mod&& mod) {
    if constexpr (std::is_invocable_v<__pow__<Base, Exp, Mod>, Base, Exp, Mod>) {
        return __pow__<Base, Exp, Mod>{}(
            std::forward<Base>(base),
            std::forward<Exp>(exp),
            std::forward<Mod>(mod)
        );

    } else {
        using Return = __pow__<Base, Exp, Mod>::type;
        Return result = steal<Return>(PyNumber_Power(
            ptr(to_python(std::forward<Base>(base))),
            ptr(to_python(std::forward<Exp>(exp))),
            ptr(to_python(std::forward<Mod>(mod)))
        ));
        if (result.is(nullptr)) {
            Exception::from_python();
        }
        return result;
    }
}


/* Equivalent to Python `pow(base, exp, mod)`, but works on C++ integers with identical
semantics. */
template <std::integral Base, std::integral Exp, std::integral Mod>
auto pow(Base base, Exp exp, Mod mod) {
    std::common_type_t<Base, Exp, Mod> result = 1;
    base = base % mod;
    while (exp > 0) {
        if (exp % 2) {
            result = (result * base) % mod;
        }
        exp >>= 1;
        base = (base * base) % mod;
    }
    return result;
}


template <typename L, typename R>
    requires ((meta::python<L> || meta::python<R>) && !__truediv__<L, R>::enable)
decltype(auto) operator/(L&& lhs, R&& rhs) = delete;
template <typename L, typename R>
    requires (__truediv__<L, R>::enable && (
        std::is_invocable_r_v<typename __truediv__<L, R>::type, __truediv__<L, R>, L, R> || (
            !std::is_invocable_v<__truediv__<L, R>, L, R> &&
            (meta::has_cpp<L> && meta::has_cpp<R>) &&
            meta::truediv_returns<meta::cpp_type<L>, meta::cpp_type<R>, typename __truediv__<L, R>::type>
        ) || (
            !std::is_invocable_v<__truediv__<L, R>, L, R> &&
            !(meta::has_cpp<L> && meta::has_cpp<R>) &&
            meta::python<typename __truediv__<L, R>::type> &&
            !meta::is_qualified<typename __truediv__<L, R>::type>
        )
    ))
decltype(auto) operator/(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__truediv__<L, R>, L, R>) {
        return __truediv__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (meta::has_cpp<L> && meta::has_cpp<R>) {
        return
            from_python(std::forward<L>(lhs)) /
            from_python(std::forward<R>(rhs));

    } else {
        using Return = __truediv__<L, R>::type;
        Return result = steal<Return>(PyNumber_TrueDivide(
            ptr(to_python(std::forward<L>(lhs))),
            ptr(to_python(std::forward<R>(rhs)))
        ));
        if (result.is(nullptr)) {
            Exception::from_python();
        }
        return result;
    }
}


template <meta::python L, typename R> requires (!__itruediv__<L, R>::enable)
decltype(auto) operator/=(L& lhs, R&& rhs) = delete;
template <meta::python L, typename R>
    requires (__itruediv__<L, R>::enable && (
        std::is_invocable_r_v<typename __itruediv__<L, R>::type, __itruediv__<L, R>, L, R> || (
            !std::is_invocable_v<__itruediv__<L, R>, L, R> &&
            (meta::has_cpp<L> && meta::has_cpp<R>) &&
            meta::itruediv_returns<meta::cpp_type<L>, meta::cpp_type<R>, typename __itruediv__<L, R>::type>
        ) || (
            !std::is_invocable_v<__itruediv__<L, R>, L, R> &&
            !(meta::has_cpp<L> && meta::has_cpp<R>) &&
            !meta::is_const<L> &&
            std::same_as<typename __itruediv__<L, R>::type, L>
        )
    ))
decltype(auto) operator/=(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__itruediv__<L, R>, L, R>) {
        return __itruediv__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (meta::has_cpp<L> && meta::has_cpp<R>) {
        return
            from_python(std::forward<L>(lhs)) /=
            from_python(std::forward<R>(rhs));

    } else {
        using Return = std::remove_cvref_t<typename __itruediv__<L, R>::type>;
        Return result = steal<Return>(PyNumber_InPlaceTrueDivide(
            ptr(lhs),
            ptr(to_python(std::forward<R>(rhs)))
        ));
        if (result.is(nullptr)) {
            Exception::from_python();
        } else if (!lhs.is(result)) {
            lhs = std::move(result);
        }
        return std::forward<L>(lhs);
    }
}


template <typename L, typename R>
    requires (__floordiv__<L, R>::enable && (
        std::is_invocable_r_v<typename __floordiv__<L, R>::type, __floordiv__<L, R>, L, R> || (
            !std::is_invocable_v<__floordiv__<L, R>, L, R> &&
            meta::python<typename __floordiv__<L, R>::type> &&
            !meta::is_qualified<typename __floordiv__<L, R>::type>
        )
    ))
decltype(auto) floordiv(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__floordiv__<L, R>, L, R>) {
        return __floordiv__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else {
        using Return = __floordiv__<L, R>::type;
        Return result = steal<Return>(PyNumber_FloorDivide(
            ptr(to_python(std::forward<L>(lhs))),
            ptr(to_python(std::forward<R>(lhs)))
        ));
        if (result.is(nullptr)) {
            Exception::from_python();
        }
        return result;
    }
}


template <meta::python L, typename R>
    requires (__ifloordiv__<L, R>::enable && (
        std::is_invocable_r_v<typename __ifloordiv__<L, R>::type, __ifloordiv__<L, R>, L, R> || (
            !std::is_invocable_v<__ifloordiv__<L, R>, L, R> &&
            !meta::is_const<L> &&
            std::same_as<typename __ifloordiv__<L, R>::type, L>
        )
    ))
decltype(auto) ifloordiv(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__ifloordiv__<L, R>, L, R>) {
        return __ifloordiv__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else {
        using Return = std::remove_cvref_t<typename __ifloordiv__<L, R>::type>;
        Return result = steal<Return>(PyNumber_InPlaceFloorDivide(
            ptr(lhs),
            ptr(to_python(std::forward<R>(rhs)))
        ));
        if (result.is(nullptr)) {
            Exception::from_python();
        } else if (!lhs.is(result)) {
            lhs = std::move(result);
        }
        return std::forward<L>(lhs);
    }
}


template <typename L, typename R>
    requires ((meta::python<L> || meta::python<R>) && !__mod__<L, R>::enable)
decltype(auto) operator%(L&& lhs, R&& rhs) = delete;
template <typename L, typename R>
    requires (__mod__<L, R>::enable && (
        std::is_invocable_r_v<typename __mod__<L, R>::type, __mod__<L, R>, L, R> || (
            !std::is_invocable_v<__mod__<L, R>, L, R> &&
            (meta::has_cpp<L> && meta::has_cpp<R>) &&
            meta::mod_returns<meta::cpp_type<L>, meta::cpp_type<R>, typename __mod__<L, R>::type>
        ) || (
            !std::is_invocable_v<__mod__<L, R>, L, R> &&
            !(meta::has_cpp<L> && meta::has_cpp<R>) &&
            meta::python<typename __mod__<L, R>::type> &&
            !meta::is_qualified<typename __mod__<L, R>::type>
        )
    ))
decltype(auto) operator%(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__mod__<L, R>, L, R>) {
        return __mod__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (meta::has_cpp<L> && meta::has_cpp<R>) {
        return
            from_python(std::forward<L>(lhs)) %
            from_python(std::forward<R>(rhs));

    } else {
        using Return = __mod__<L, R>::type;
        Return result = steal<Return>(PyNumber_Remainder(
            ptr(to_python(std::forward<L>(lhs))),
            ptr(to_python(std::forward<R>(rhs)))
        ));
        if (result.is(nullptr)) {
            Exception::from_python();
        }
        return result;
    }
}


template <meta::python L, typename R> requires (!__imod__<L, R>::enable)
decltype(auto) operator%=(L& lhs, R&& rhs) = delete;
template <meta::python L, typename R>
    requires (__imod__<L, R>::enable && (
        std::is_invocable_r_v<typename __imod__<L, R>::type, __imod__<L, R>, L, R> || (
            !std::is_invocable_v<__imod__<L, R>, L, R> &&
            (meta::has_cpp<L> && meta::has_cpp<R>) &&
            meta::imod_returns<meta::cpp_type<L>, meta::cpp_type<R>, typename __imod__<L, R>::type>
        ) || (
            !std::is_invocable_v<__imod__<L, R>, L, R> &&
            !(meta::has_cpp<L> && meta::has_cpp<R>) &&
            !meta::is_const<L> &&
            std::same_as<typename __imod__<L, R>::type, L>
        )
    ))
decltype(auto) operator%=(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__imod__<L, R>, L, R>) {
        return __imod__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (meta::has_cpp<L> && meta::has_cpp<R>) {
        return
            from_python(std::forward<L>(lhs)) %=
            from_python(std::forward<R>(rhs));

    } else {
        using Return = std::remove_cvref_t<typename __imod__<L, R>::type>;
        Return result = steal<Return>(PyNumber_InPlaceRemainder(
            ptr(lhs),
            ptr(to_python(std::forward<R>(rhs)))
        ));
        if (result.is(nullptr)) {
            Exception::from_python();
        } else if (!lhs.is(result)) {
            lhs = std::move(result);
        }
        return std::forward<L>(lhs);
    }
}


template <typename L, typename R>
    requires ((meta::python<L> || meta::python<R>) && !__lshift__<L, R>::enable)
decltype(auto) operator<<(L&& lhs, R&& rhs) = delete;
template <typename L, typename R>
    requires (__lshift__<L, R>::enable && (
        std::is_invocable_r_v<typename __lshift__<L, R>::type, __lshift__<L, R>, L, R> || (
            !std::is_invocable_v<__lshift__<L, R>, L, R> &&
            (meta::has_cpp<L> && meta::has_cpp<R>) &&
            meta::lshift_returns<meta::cpp_type<L>, meta::cpp_type<R>, typename __lshift__<L, R>::type>
        ) || (
            !std::is_invocable_v<__lshift__<L, R>, L, R> &&
            !(meta::has_cpp<L> && meta::has_cpp<R>) &&
            meta::python<typename __lshift__<L, R>::type> &&
            !meta::is_qualified<typename __lshift__<L, R>::type>
        )
    ))
decltype(auto) operator<<(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__lshift__<L, R>, L, R>) {
        return __lshift__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (meta::has_cpp<L> && meta::has_cpp<R>) {
        return
            from_python(std::forward<L>(lhs)) <<
            from_python(std::forward<R>(rhs));

    } else {
        using Return = __lshift__<L, R>::type;
        Return result = steal<Return>(PyNumber_Lshift(
            ptr(to_python(std::forward<L>(lhs))),
            ptr(to_python(std::forward<R>(rhs)))
        ));
        if (result.is(nullptr)) {
            Exception::from_python();
        }
        return result;
    }
}


template <meta::python L, typename R> requires (!__ilshift__<L, R>::enable)
decltype(auto) operator<<=(L& lhs, R&& rhs) = delete;
template <meta::python L, typename R>
    requires (__ilshift__<L, R>::enable && (
        std::is_invocable_r_v<typename __ilshift__<L, R>::type, __ilshift__<L, R>, L, R> || (
            !std::is_invocable_v<__ilshift__<L, R>, L, R> &&
            (meta::has_cpp<L> && meta::has_cpp<R>) &&
            meta::ilshift_returns<meta::cpp_type<L>, meta::cpp_type<R>, typename __ilshift__<L, R>::type>
        ) || (
            !std::is_invocable_v<__ilshift__<L, R>, L, R> &&
            !(meta::has_cpp<L> && meta::has_cpp<R>) &&
            !meta::is_const<L> &&
            std::same_as<typename __ilshift__<L, R>::type, L>
        )
    ))
decltype(auto) operator<<=(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__ilshift__<L, R>, L, R>) {
        return __ilshift__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (meta::has_cpp<L> && meta::has_cpp<R>) {
        return
            from_python(std::forward<L>(lhs)) <<=
            from_python(std::forward<R>(rhs));

    } else {
        using Return = std::remove_cvref_t<typename __ilshift__<L, R>::type>;
        Return result = steal<Return>(PyNumber_InPlaceLshift(
            ptr(lhs),
            ptr(to_python(std::forward<R>(rhs)))
        ));
        if (result.is(nullptr)) {
            Exception::from_python();
        } else if (!lhs.is(result)) {
            lhs = std::move(result);
        }
        return std::forward<L>(lhs);
    }
}


template <typename L, typename R>
    requires ((meta::python<L> || meta::python<R>) && !__rshift__<L, R>::enable)
decltype(auto) operator>>(L&& lhs, R&& rhs) = delete;
template <typename L, typename R>
    requires (__rshift__<L, R>::enable && (
        std::is_invocable_r_v<typename __rshift__<L, R>::type, __rshift__<L, R>, L, R> || (
            !std::is_invocable_v<__rshift__<L, R>, L, R> &&
            (meta::has_cpp<L> && meta::has_cpp<R>) &&
            meta::rshift_returns<meta::cpp_type<L>, meta::cpp_type<R>, typename __rshift__<L, R>::type>
        ) || (
            !std::is_invocable_v<__rshift__<L, R>, L, R> &&
            !(meta::has_cpp<L> && meta::has_cpp<R>) &&
            meta::python<typename __rshift__<L, R>::type> &&
            !meta::is_qualified<typename __rshift__<L, R>::type>
        )
    ))
decltype(auto) operator>>(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__rshift__<L, R>, L, R>) {
        return __rshift__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (meta::has_cpp<L> && meta::has_cpp<R>) {
        return
            from_python(std::forward<L>(lhs)) >>
            from_python(std::forward<R>(rhs));

    } else {
        using Return = __rshift__<L, R>::type;
        Return result = steal<Return>(PyNumber_Rshift(
            ptr(to_python(std::forward<L>(lhs))),
            ptr(to_python(std::forward<R>(rhs)))
        ));
        if (result.is(nullptr)) {
            Exception::from_python();
        }
        return result;
    }
}


template <meta::python L, typename R> requires (!__irshift__<L, R>::enable)
decltype(auto) operator>>=(L& lhs, R&& rhs) = delete;
template <meta::python L, typename R>
    requires (__irshift__<L, R>::enable && (
        std::is_invocable_r_v<typename __irshift__<L, R>::type, __irshift__<L, R>, L, R> || (
            !std::is_invocable_v<__irshift__<L, R>, L, R> &&
            (meta::has_cpp<L> && meta::has_cpp<R>) &&
            meta::irshift_returns<meta::cpp_type<L>, meta::cpp_type<R>, typename __irshift__<L, R>::type>
        ) || (
            !std::is_invocable_v<__irshift__<L, R>, L, R> &&
            !(meta::has_cpp<L> && meta::has_cpp<R>) &&
            !meta::is_const<L> &&
            std::same_as<typename __irshift__<L, R>::type, L>
        )
    ))
decltype(auto) operator>>=(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__irshift__<L, R>, L, R>) {
        return __irshift__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (meta::has_cpp<L> && meta::has_cpp<R>) {
        return
            from_python(std::forward<L>(lhs)) >>=
            from_python(std::forward<R>(rhs));

    } else {
        using Return = std::remove_cvref_t<typename __irshift__<L, R>::type>;
        Return result = steal<Return>(PyNumber_InPlaceRshift(
            ptr(lhs),
            ptr(to_python(std::forward<R>(rhs)))
        ));
        if (result.is(nullptr)) {
            Exception::from_python();
        } else if (!lhs.is(result)) {
            lhs = std::move(result);
        }
        return std::forward<L>(lhs);
    }
}


template <typename L, typename R>
    requires ((meta::python<L> || meta::python<R>) && !__and__<L, R>::enable)
decltype(auto) operator&(L&& lhs, R&& rhs) = delete;
template <typename L, typename R>
    requires (__and__<L, R>::enable && (
        std::is_invocable_r_v<typename __and__<L, R>::type, __and__<L, R>, L, R> || (
            !std::is_invocable_v<__and__<L, R>, L, R> &&
            (meta::has_cpp<L> && meta::has_cpp<R>) &&
            meta::and_returns<meta::cpp_type<L>, meta::cpp_type<R>, typename __and__<L, R>::type>
        ) || (
            !std::is_invocable_v<__and__<L, R>, L, R> &&
            !(meta::has_cpp<L> && meta::has_cpp<R>) &&
            meta::python<typename __and__<L, R>::type> &&
            !meta::is_qualified<typename __and__<L, R>::type>
        )
    ))
decltype(auto) operator&(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__and__<L, R>, L, R>) {
        return __and__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (meta::has_cpp<L> && meta::has_cpp<R>) {
        return
            from_python(std::forward<L>(lhs)) &
            from_python(std::forward<R>(rhs));

    } else {
        using Return = __and__<L, R>::type;
        Return result = steal<Return>(PyNumber_And(
            ptr(to_python(std::forward<L>(lhs))),
            ptr(to_python(std::forward<R>(rhs)))
        ));
        if (result.is(nullptr)) {
            Exception::from_python();
        }
        return result;
    }
}


template <meta::python L, typename R> requires (!__iand__<L, R>::enable)
decltype(auto) operator&=(L& lhs, R&& rhs) = delete;
template <meta::python L, typename R>
    requires (__iand__<L, R>::enable && (
        std::is_invocable_r_v<typename __iand__<L, R>::type, __iand__<L, R>, L, R> || (
            !std::is_invocable_v<__iand__<L, R>, L, R> &&
            (meta::has_cpp<L> && meta::has_cpp<R>) &&
            meta::iand_returns<meta::cpp_type<L>, meta::cpp_type<R>, typename __iand__<L, R>::type>
        ) || (
            !std::is_invocable_v<__iand__<L, R>, L, R> &&
            !(meta::has_cpp<L> && meta::has_cpp<R>) &&
            !meta::is_const<L> &&
            std::same_as<typename __iand__<L, R>::type, L>
        )
    ))
decltype(auto) operator&=(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__iand__<L, R>, L, R>) {
        return __iand__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (meta::has_cpp<L> && meta::has_cpp<R>) {
        return from_python(std::forward<L>(lhs)) &=
            from_python(std::forward<R>(rhs));

    } else {
        using Return = std::remove_cvref_t<typename __iand__<L, R>::type>;
        Return result = steal<Return>(PyNumber_InPlaceAnd(
            ptr(lhs),
            ptr(to_python(std::forward<R>(rhs)))
        ));
        if (result.is(nullptr)) {
            Exception::from_python();
        } else if (!lhs.is(result)) {
            lhs = std::move(result);
        }
        return std::forward<L>(lhs);
    }
}


template <typename L, typename R>
    requires ((meta::python<L> || meta::python<R>) && !__or__<L, R>::enable)
decltype(auto) operator|(L&& lhs, R&& rhs) = delete;
template <typename L, typename R>
    requires (__or__<L, R>::enable && (
        std::is_invocable_r_v<typename __or__<L, R>::type, __or__<L, R>, L, R> || (
            !std::is_invocable_v<__or__<L, R>, L, R> &&
            (meta::has_cpp<L> && meta::has_cpp<R>) &&
            meta::or_returns<meta::cpp_type<L>, meta::cpp_type<R>, typename __or__<L, R>::type>
        ) || (
            !std::is_invocable_v<__or__<L, R>, L, R> &&
            !(meta::has_cpp<L> && meta::has_cpp<R>) &&
            meta::python<typename __or__<L, R>::type> &&
            !meta::is_qualified<typename __or__<L, R>::type>
        )
    ))
decltype(auto) operator|(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__or__<L, R>, L, R>) {
        return __or__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (meta::has_cpp<L> && meta::has_cpp<R>) {
        return
            from_python(std::forward<L>(lhs)) |
            from_python(std::forward<R>(rhs));

    } else {
        using Return = __or__<L, R>::type;
        Return result = steal<Return>(PyNumber_Or(
            ptr(to_python(std::forward<L>(lhs))),
            ptr(to_python(std::forward<R>(rhs)))
        ));
        if (result.is(nullptr)) {
            Exception::from_python();
        }
        return result;
    }
}


template <meta::python L, typename R> requires (!__ior__<L, R>::enable)
decltype(auto) operator|=(L& lhs, R&& rhs) = delete;
template <meta::python L, typename R>
    requires (__ior__<L, R>::enable && (
        std::is_invocable_r_v<typename __ior__<L, R>::type, __ior__<L, R>, L, R> || (
            !std::is_invocable_v<__ior__<L, R>, L, R> &&
            (meta::has_cpp<L> && meta::has_cpp<R>) &&
            meta::ior_returns<meta::cpp_type<L>, meta::cpp_type<R>, typename __ior__<L, R>::type>
        ) || (
            !std::is_invocable_v<__ior__<L, R>, L, R> &&
            !(meta::has_cpp<L> && meta::has_cpp<R>) &&
            !meta::is_const<L> &&
            std::same_as<typename __ior__<L, R>::type, L>
        )
    ))
decltype(auto) operator|=(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__ior__<L, R>, L, R>) {
        return __ior__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (meta::has_cpp<L> && meta::has_cpp<R>) {
        return
            from_python(std::forward<L>(lhs)) |=
            from_python(std::forward<R>(rhs));

    } else {
        using Return = std::remove_cvref_t<typename __ior__<L, R>::type>;
        Return result = steal<Return>(PyNumber_InPlaceOr(
            ptr(lhs),
            ptr(to_python(std::forward<R>(rhs)))
        ));
        if (result.is(nullptr)) {
            Exception::from_python();
        } else if (!lhs.is(result)) {
            lhs = std::move(result);
        }
        return std::forward<L>(lhs);
    }
}


template <typename L, typename R>
    requires ((meta::python<L> || meta::python<R>) && !__xor__<L, R>::enable)
decltype(auto) operator^(L&& lhs, R&& rhs) = delete;
template <typename L, typename R>
    requires (__xor__<L, R>::enable && (
        std::is_invocable_r_v<typename __xor__<L, R>::type, __xor__<L, R>, L, R> || (
            !std::is_invocable_v<__xor__<L, R>, L, R> &&
            (meta::has_cpp<L> && meta::has_cpp<R>) &&
            meta::xor_returns<meta::cpp_type<L>, meta::cpp_type<R>, typename __xor__<L, R>::type>
        ) || (
            !std::is_invocable_v<__xor__<L, R>, L, R> &&
            !(meta::has_cpp<L> && meta::has_cpp<R>) &&
            meta::python<typename __xor__<L, R>::type> &&
            !meta::is_qualified<typename __xor__<L, R>::type>
        )
    ))
decltype(auto) operator^(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__xor__<L, R>, L, R>) {
        return __xor__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (meta::has_cpp<L> && meta::has_cpp<R>) {
        return
            from_python(std::forward<L>(lhs)) ^
            from_python(std::forward<R>(rhs));

    } else {
        using Return = __xor__<L, R>::type;
        Return result = steal<Return>(PyNumber_Xor(
            ptr(to_python(std::forward<L>(lhs))),
            ptr(to_python(std::forward<R>(rhs)))
        ));
        if (result.is(nullptr)) {
            Exception::from_python();
        }
        return result;
    }
}


template <meta::python L, typename R> requires (!__ixor__<L, R>::enable)
decltype(auto) operator^=(L& lhs, R&& rhs) = delete;
template <meta::python L, typename R>
    requires (__ixor__<L, R>::enable && (
        std::is_invocable_r_v<typename __ixor__<L, R>::type, __ixor__<L, R>, L, R> || (
            !std::is_invocable_v<__ixor__<L, R>, L, R> &&
            (meta::has_cpp<L> && meta::has_cpp<R>) &&
            meta::ixor_returns<meta::cpp_type<L>, meta::cpp_type<R>, typename __ixor__<L, R>::type>
        ) || (
            !std::is_invocable_v<__ixor__<L, R>, L, R> &&
            !(meta::has_cpp<L> && meta::has_cpp<R>) &&
            !meta::is_const<L> &&
            std::same_as<typename __ixor__<L, R>::type, L>
        )
    ))
decltype(auto) operator^=(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__ixor__<L, R>, L, R>) {
        return __ixor__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (meta::has_cpp<L> && meta::has_cpp<R>) {
        return
            from_python(std::forward<L>(lhs)) ^=
            from_python(std::forward<R>(rhs));

    } else {
        using Return = std::remove_cvref_t<typename __ixor__<L, R>::type>;
        Return result = steal<Return>(PyNumber_InPlaceXor(
            ptr(lhs),
            ptr(to_python(std::forward<R>(rhs)))
        ));
        if (result.is(nullptr)) {
            Exception::from_python();
        } else if (!lhs.is(result)) {
            lhs = std::move(result);
        }
        return std::forward<L>(lhs);
    }
}


//////////////////////////
////    EXCEPTIONS    ////
//////////////////////////


namespace impl {

    /* Get the current thread state and assert that it does not have an active
    exception. */
    inline PyThreadState* assert_no_active_python_exception() {
        PyThreadState* tstate = PyThreadState_Get();
        if (!tstate) {
            throw AssertionError(
                "Exception::to_python() called without an active Python interpreter"
            );
        }
        if (tstate->current_exception) {
            Object str = steal<Object>(PyObject_Repr(tstate->current_exception));
            if (str.is(nullptr)) {
                Exception::from_python();
            }
            Py_ssize_t len;
            const char* message = PyUnicode_AsUTF8AndSize(
                ptr(str),
                &len
            );
            if (message == nullptr) {
                Exception::from_python();
            }

            throw AssertionError(
                "Exception::to_python() called while an active Python exception "
                "already exists for the current interpreter:\n\n" +
                std::string(message, len)
            );
        }
        return tstate;
    }

    /* A wrapper around an existing Python exception that allows it to be handled as an
    equivalent C++ exception.  This inherits from both `Object` and the templated
    exception type, meaning it can be treated polymorphically in both directions.
    Under normal use, the user should not be aware that this class even exists,
    although it does optimize the case where an exception originates from Python,
    propagates through C++, and then is returned to Python by retaining the original
    Python object without any additional allocations. */
    template <meta::inherits<Exception> T> requires (!meta::is_qualified<T>)
    struct py_err;

    template <meta::inherits<Exception> T>
    py_err(T) -> py_err<T>;

    /* Holds the tables needed to translate C++ exceptions to python and vice versa
    for a particular interpreter. */
    struct ExceptionTable {
        ExceptionTable() {
            static constexpr auto to_string = [](const Object& obj) {
                Py_ssize_t len;
                const char* data = PyUnicode_AsUTF8AndSize(
                    ptr(obj),
                    &len
                );
                if (data == nullptr) {
                    Exception::from_python();
                }
                return std::string(data, len);
            };

            register_from_python<Exception>(borrow<Object>(PyExc_Exception));
            register_from_python<ArithmeticError>(borrow<Object>(PyExc_ArithmeticError));
            register_from_python<FloatingPointError>(borrow<Object>(PyExc_FloatingPointError));
            register_from_python<OverflowError>(borrow<Object>(PyExc_OverflowError));
            register_from_python<ZeroDivisionError>(borrow<Object>(PyExc_ZeroDivisionError));
            register_from_python<AssertionError>(borrow<Object>(PyExc_AssertionError));
            register_from_python<AttributeError>(borrow<Object>(PyExc_AttributeError));
            register_from_python<BufferError>(borrow<Object>(PyExc_BufferError));
            register_from_python<EOFError>(borrow<Object>(PyExc_EOFError));
            register_from_python<ImportError>(borrow<Object>(PyExc_ImportError));
            register_from_python<ModuleNotFoundError>(borrow<Object>(PyExc_ModuleNotFoundError));
            register_from_python<LookupError>(borrow<Object>(PyExc_LookupError));
            register_from_python<IndexError>(borrow<Object>(PyExc_IndexError));
            register_from_python<KeyError>(borrow<Object>(PyExc_KeyError));
            register_from_python<MemoryError>(borrow<Object>(PyExc_MemoryError));
            register_from_python<NameError>(borrow<Object>(PyExc_NameError));
            register_from_python<UnboundLocalError>(borrow<Object>(PyExc_UnboundLocalError));
            register_from_python<OSError>(borrow<Object>(PyExc_OSError));
            register_from_python<BlockingIOError>(borrow<Object>(PyExc_BlockingIOError));
            register_from_python<ChildProcessError>(borrow<Object>(PyExc_ChildProcessError));
            register_from_python<ConnectionError>(borrow<Object>(PyExc_ConnectionError));
            register_from_python<BrokenPipeError>(borrow<Object>(PyExc_BrokenPipeError));
            register_from_python<ConnectionAbortedError>(borrow<Object>(PyExc_ConnectionAbortedError));
            register_from_python<ConnectionRefusedError>(borrow<Object>(PyExc_ConnectionRefusedError));
            register_from_python<ConnectionResetError>(borrow<Object>(PyExc_ConnectionResetError));
            register_from_python<FileExistsError>(borrow<Object>(PyExc_FileExistsError));
            register_from_python<FileNotFoundError>(borrow<Object>(PyExc_FileNotFoundError));
            register_from_python<InterruptedError>(borrow<Object>(PyExc_InterruptedError));
            register_from_python<IsADirectoryError>(borrow<Object>(PyExc_IsADirectoryError));
            register_from_python<NotADirectoryError>(borrow<Object>(PyExc_NotADirectoryError));
            register_from_python<PermissionError>(borrow<Object>(PyExc_PermissionError));
            register_from_python<ProcessLookupError>(borrow<Object>(PyExc_ProcessLookupError));
            register_from_python<TimeoutError>(borrow<Object>(PyExc_TimeoutError));
            register_from_python<ReferenceError>(borrow<Object>(PyExc_ReferenceError));
            register_from_python<RuntimeError>(borrow<Object>(PyExc_RuntimeError));
            register_from_python<NotImplementedError>(borrow<Object>(PyExc_NotImplementedError));
            register_from_python<RecursionError>(borrow<Object>(PyExc_RecursionError));
            register_from_python<StopAsyncIteration>(borrow<Object>(PyExc_StopAsyncIteration));
            register_from_python<StopIteration>(borrow<Object>(PyExc_StopIteration));
            register_from_python<SyntaxError>(borrow<Object>(PyExc_SyntaxError));
            register_from_python<IndentationError>(borrow<Object>(PyExc_IndentationError));
            register_from_python<TabError>(borrow<Object>(PyExc_TabError));
            register_from_python<SystemError>(borrow<Object>(PyExc_SystemError));
            register_from_python<TypeError>(borrow<Object>(PyExc_TypeError));
            register_from_python<ValueError>(borrow<Object>(PyExc_ValueError));
            register_from_python<UnicodeError>(borrow<Object>(PyExc_UnicodeError));
            register_from_python<UnicodeDecodeError>(
                borrow<Object>(PyExc_UnicodeDecodeError),
                [](Object exception) -> void {
                    Object encoding = steal<Object>(PyUnicodeDecodeError_GetEncoding(
                        ptr(exception)
                    ));
                    if (encoding.is(nullptr)) {
                        Exception::from_python();
                    }
                    Object object = steal<Object>(PyUnicodeDecodeError_GetObject(
                        ptr(exception)
                    ));
                    if (object.is(nullptr)) {
                        Exception::from_python();
                    }
                    Py_ssize_t start;
                    Py_ssize_t end;
                    if (
                        PyUnicodeDecodeError_GetStart(ptr(exception), &start) ||
                        PyUnicodeDecodeError_GetEnd(ptr(exception), &end)
                    ) {
                        Exception::from_python();
                    }
                    Object reason = steal<Object>(PyUnicodeDecodeError_GetReason(
                        ptr(exception)
                    ));
                    if (reason.is(nullptr)) {
                        Exception::from_python();
                    }
                    throw UnicodeDecodeError(
                        to_string(encoding),
                        to_string(object),
                        start,
                        end,
                        to_string(reason)
                    );
                }
            );
            register_from_python<UnicodeEncodeError>(
                borrow<Object>(PyExc_UnicodeEncodeError),
                [](Object exception) -> void {
                    Object encoding = steal<Object>(PyUnicodeEncodeError_GetEncoding(
                        ptr(exception)
                    ));
                    if (encoding.is(nullptr)) {
                        Exception::from_python();
                    }
                    Object object = steal<Object>(PyUnicodeEncodeError_GetObject(
                        ptr(exception)
                    ));
                    if (object.is(nullptr)) {
                        Exception::from_python();
                    }
                    Py_ssize_t start;
                    Py_ssize_t end;
                    if (
                        PyUnicodeEncodeError_GetStart(ptr(exception), &start) ||
                        PyUnicodeEncodeError_GetEnd(ptr(exception), &end)
                    ) {
                        Exception::from_python();
                    }
                    Object reason = steal<Object>(PyUnicodeEncodeError_GetReason(
                        ptr(exception)
                    ));
                    if (reason.is(nullptr)) {
                        Exception::from_python();
                    }
                    throw UnicodeEncodeError(
                        to_string(encoding),
                        to_string(object),
                        start,
                        end,
                        to_string(reason)
                    );
                }
            );
            register_from_python<UnicodeTranslateError>(
                borrow<Object>(PyExc_UnicodeTranslateError),
                [](Object exception) -> void {
                    Object object = steal<Object>(PyUnicodeTranslateError_GetObject(
                        ptr(exception)
                    ));
                    if (object.is(nullptr)) {
                        Exception::from_python();
                    }
                    Py_ssize_t start;
                    Py_ssize_t end;
                    if (
                        PyUnicodeTranslateError_GetStart(ptr(exception), &start) ||
                        PyUnicodeTranslateError_GetEnd(ptr(exception), &end)
                    ) {
                        Exception::from_python();
                    }
                    Object reason = steal<Object>(PyUnicodeTranslateError_GetReason(
                        ptr(exception)
                    ));
                    throw UnicodeTranslateError(
                        to_string(object),
                        start,
                        end,
                        to_string(reason)
                    );
                }
            );

            register_to_python<Exception>();
            register_to_python<ArithmeticError>();
            register_to_python<FloatingPointError>();
            register_to_python<OverflowError>();
            register_to_python<ZeroDivisionError>();
            register_to_python<AssertionError>();
            register_to_python<AttributeError>();
            register_to_python<BufferError>();
            register_to_python<EOFError>();
            register_to_python<ImportError>();
            register_to_python<ModuleNotFoundError>();
            register_to_python<LookupError>();
            register_to_python<IndexError>();
            register_to_python<KeyError>();
            register_to_python<MemoryError>();
            register_to_python<NameError>();
            register_to_python<UnboundLocalError>();
            register_to_python<OSError>();
            register_to_python<BlockingIOError>();
            register_to_python<ChildProcessError>();
            register_to_python<ConnectionError>();
            register_to_python<BrokenPipeError>();
            register_to_python<ConnectionAbortedError>();
            register_to_python<ConnectionRefusedError>();
            register_to_python<ConnectionResetError>();
            register_to_python<FileExistsError>();
            register_to_python<FileNotFoundError>();
            register_to_python<InterruptedError>();
            register_to_python<IsADirectoryError>();
            register_to_python<NotADirectoryError>();
            register_to_python<PermissionError>();
            register_to_python<ProcessLookupError>();
            register_to_python<TimeoutError>();
            register_to_python<ReferenceError>();
            register_to_python<RuntimeError>();
            register_to_python<NotImplementedError>();
            register_to_python<RecursionError>();
            register_to_python<StopAsyncIteration>();
            register_to_python<StopIteration>();
            register_to_python<SyntaxError>();
            register_to_python<IndentationError>();
            register_to_python<TabError>();
            register_to_python<SystemError>();
            register_to_python<TypeError>();
            register_to_python<ValueError>();
            register_to_python<UnicodeError>();
            register_to_python<UnicodeDecodeError>(
                [](const Exception& exception) -> Object {
                    const UnicodeDecodeError& exc =
                        static_cast<const UnicodeDecodeError&>(exception);
                    Object result = steal<Object>(PyObject_CallFunction(
                        PyExc_UnicodeDecodeError,
                        "ssnns",
                        exc.encoding.data(),
                        exc.object.data(),
                        exc.start,
                        exc.end,
                        exc.reason.data()
                    ));
                    if (result.is(nullptr)) {
                        Exception::from_python();
                    }
                    return result;
                }
            );
            register_to_python<UnicodeEncodeError>(
                [](const Exception& exception) -> Object {
                    const UnicodeEncodeError& exc =
                        static_cast<const UnicodeEncodeError&>(exception);
                    Object result = steal<Object>(PyObject_CallFunction(
                        PyExc_UnicodeEncodeError,
                        "ssnns",
                        exc.encoding.data(),
                        exc.object.data(),
                        exc.start,
                        exc.end,
                        exc.reason.data()
                    ));
                    if (result.is(nullptr)) {
                        Exception::from_python();
                    }
                    return result;
                }
            );
            register_to_python<UnicodeTranslateError>(
                [](const Exception& exception) -> Object {
                    const UnicodeTranslateError& exc =
                        static_cast<const UnicodeTranslateError&>(exception);
                    Object result = steal<Object>(PyObject_CallFunction(
                        PyExc_UnicodeTranslateError,
                        "snns",
                        exc.object.data(),
                        exc.start,
                        exc.end,
                        exc.reason.data()
                    ));
                    if (result.is(nullptr)) {
                        Exception::from_python();
                    }
                    return result;
                }
            );
        }

        /* A map relating every bertrand exception type to its equivalent Python
        type. */
        std::unordered_map<std::type_index, Object> types;

        /* A map holding function pointers that take an arbitrary bertrand exception
        and convert it into an equivalent Python exception.  A program is malformed and
        will immediately exit if a subclass of `bertrand::Exception` is passed to
        Python without a corresponding entry in this map. */
        std::unordered_map<std::type_index, Object(*)(const Exception&)> to_python;

        /* A map holding function pointers that take a Python exception of a particular
        C++ type and re-throw it as a matching `py_err<T>` wrapper.  The result can
        then be caught and handled via ordinary semantics.  The value stored in this
        map is always identical to `from_python.at(types.at(typeid(T)))` - this map is
        just a shortcut to avoid the intermediate lookup.  It is used to implement the
        `borrow()` and `steal()` constructors for `py_err<T>`. */
        std::unordered_map<std::type_index, void(*)(Object)> to_cpp;

        /* A map holding function pointers that take an arbitrary Python exception
        object directly from the interpreter and re-throw it as a corresponding
        `py_err<T>` wrapper, which can be caught in C++ according to Python semantics.
        A program is malformed and will immediately exit if a Python exception is
        caught in C++ whose type is not present in this map. */
        std::unordered_map<Object, void(*)(Object)> from_python;

        /* Insert an `Exception::from_python()` hook for this exception type into the
        global map. */
        template <meta::inherits<Exception> T> requires (!meta::is_qualified<T>)
        void register_from_python(
            Object type,
            void(*callback)(Object) = simple_from_python<T>
        ) {
            types.emplace(typeid(T), type);
            types.emplace(typeid(py_err<T>), type);
            to_cpp.emplace(typeid(T), callback);
            to_cpp.emplace(typeid(py_err<T>), callback);
            from_python.emplace(
                type,
                [](Object exception) {
                    throw steal<py_err<T>>(release(exception));
                }
            );
        }

        /* Insert an `Exception::to_python()` hook for this exception type into the
        global map. */
        template <meta::inherits<Exception> T> requires (!meta::is_qualified<T>)
        void register_to_python(
            Object(*callback)(const Exception&) = simple_to_python<T>
        ) {
            to_python.emplace(typeid(T), callback);
            to_python.emplace(
                typeid(py_err<T>),
                [](const Exception& exception) -> Object {
                    return reinterpret_cast<const py_err<T>&>(exception);
                }
            );
        }

        /* Clear all Python handlers for this exception type. */
        template <meta::inherits<Exception> T> requires (!meta::is_qualified<T>)
        void unregister() {
            auto node = types.extract(typeid(T));
            if (node) {
                to_python.erase(node.key());
                to_cpp.erase(node.key());
                from_python.erase(node.mapped());
            }
            node = types.extract(typeid(py_err<T>));
            if (node) {
                to_python.erase(node.key());
                to_cpp.erase(node.key());
                from_python.erase(node.mapped());
            }
        }

        /* The default `Exception::from_python()` handler that will be registered if no
        explicit override is provided.  This will simply reinterpret the current Python
        error as a corresponding `py_err<T>` exception wrapper, which can be caught using
        typical bertrand. */
        template <typename T>
        [[noreturn]] static void simple_from_python(Object exception);

        /* The default `Exception::to_python()` handler that will be registered if no
        explicit override is provided.  This simply calls the exception's Python
        constructor with the raw text of the error message, and then */
        template <typename T>
        static Object simple_to_python(const Exception& exception);
    };

    /* Stores the global map of exception tables for each Python interpreter.  The
    lifecycle of and access to these entries is managed by the constructor and
    destructor of the `bertrand` module, so users shouldn't need to worry about
    them. */
    inline std::unordered_map<PyInterpreterState*, ExceptionTable> exception_table;

    /* A wrapper around an existing Python exception that allows it to be handled as an
    equivalent C++ exception.  This inherits from both `Object` and the templated
    exception type, meaning it can be treated polymorphically in both directions.
    Under normal use, the user should not be aware that this class even exists,
    although it does optimize the case where an exception originates from Python,
    propagates through C++, and then is returned to Python by retaining the original
    Python object without any additional allocations. */
    template <meta::inherits<Exception> T> requires (!meta::is_qualified<T>)
    struct py_err : T, Object {

        /* `borrow()` constructor.  Also converts the Python exception into an
        instance of `T` by consulting the exception tables. */
        py_err(PyObject* p, borrowed_t t) :
            T([](Object p) {
                auto table = exception_table.find(PyInterpreterState_Get());
                if (table == exception_table.end()) {
                    throw AssertionError(
                        "no exception table found for the current Python interpreter"
                    );
                }

                auto it = table->second.to_cpp.find(typeid(T));
                if (it == table->second.to_cpp.end()) {
                    throw AssertionError(
                        "no exception handler registered for '" + type_name<T> + "'"
                    );
                }
                try {
                    it->second(std::move(p));
                } catch (T&& exc) {
                    return T(std::move(exc).trim_before().skip());
                } catch (...) {
                    throw;
                }
                throw AssertionError(
                    "handler must throw an exception of type '" + type_name<T> + "'"
                );
            }(borrow<Object>(p))),
            Object(p, t)
        {}

        /* `steal()` constructor.  Also converts the Python exception into an instance
        of `T` by consulting the exception tables. */
        py_err(PyObject* p, stolen_t t) :
            T([](Object p) {
                auto table = exception_table.find(PyInterpreterState_Get());
                if (table == exception_table.end()) {
                    throw AssertionError(
                        "no exception table found for the current Python interpreter"
                    );
                }
                auto it = table->second.to_cpp.find(typeid(T));
                if (it == table->second.to_cpp.end()) {
                    throw AssertionError(
                        "no exception handler registered for '" + type_name<T> + "'"
                    );
                }
                try {
                    it->second(std::move(p));
                } catch (T&& exc) {
                    return T(std::move(exc).trim_before().skip());
                } catch (...) {
                    throw;
                }
                throw AssertionError(
                    "handler must throw an exception of type '" + type_name<T> + "'"
                );
            }(borrow<Object>(p))),
            Object(p, t)
        {}

        /* Forwarding constructor.  Directly constructs an instance of `T`, and then
        converts that instance into an equivalent Python form by consulting the
        exception tables. */
        template <typename... Args> requires (std::constructible_from<T, Args...>)
        explicit py_err(Args&&... args) :
            T(std::forward<Args>(args)...),
            Object([](const T& exc) {
                auto table = exception_table.find(PyInterpreterState_Get());
                if (table == exception_table.end()) {
                    throw AssertionError(
                        "no exception table found for the current Python interpreter"
                    );
                }
                auto it = table->second.to_python.find(typeid(T));
                if (it == table->second.to_python.end()) {
                    throw AssertionError(
                        "no Python exception type registered for C++ exception of type '" +
                        type_name<T> + "'"
                    );
                }
                return it->second(exc);
            }(static_cast<const T&>(*this)))
        {}

        /* A type index for this exception wrapper, which can be searched in the global
        exception tables to find a corresponding callback. */
        virtual std::type_index type() const noexcept override {
            return typeid(py_err);
        }

        /* The full exception diagnostic, including a traceback that interleaves the
        original Python trace with a continuation trace in C++. */
        constexpr virtual const char* what() const noexcept override {
            if (T::m_what.empty()) {
                T::m_what = "Traceback (most recent call last):\n";

                // insert C++ traceback
                if (const cpptrace::stacktrace* trace = this->trace()) {
                    for (size_t i = trace->frames.size(); i-- > 0;) {
                        T::m_what += T::format_frame(trace->frames[i]);
                    }
                }

                // continue with Python traceback
                Object tb = steal<Object>(PyException_GetTraceback(ptr(*this)));
                while (!tb.is(nullptr)) {
                    PyFrameObject* frame = reinterpret_cast<PyTracebackObject*>(
                        ptr(tb)
                    )->tb_frame;
                    if (!frame) {
                        break;
                    }
                    Object code = steal<Object>(reinterpret_cast<PyObject*>(
                        PyFrame_GetCode(frame)
                    ));
                    if (code.is(nullptr)) {
                        break;
                    }
                    Py_ssize_t len;
                    const char* str = PyUnicode_AsUTF8AndSize(
                        reinterpret_cast<PyCodeObject*>(
                            ptr(code)
                        )->co_filename,
                        &len
                    );
                    if (!str) {
                        Exception::from_python();
                    }
                    T::m_what += "    File \"" + std::string(str, len) + "\", line ";
                    T::m_what += std::to_string(PyFrame_GetLineNumber(frame));
                    T::m_what += ", in ";
                    str = PyUnicode_AsUTF8AndSize(
                        reinterpret_cast<PyCodeObject*>(
                            ptr(code)
                        )->co_name,
                        &len
                    );
                    if (!str) {
                        Exception::from_python();
                    }
                    T::m_what += std::string(str, len);
                }

                // exception message
                T::m_what += T::name();
                T::m_what += ": ";
                T::m_what += T::message();
            }
            return T::m_what.data();
        }
    };

    /* Append a C++ stack trace to a Python traceback, which can be attached to a
    newly-constructed exception object.  Steals a reference to `head` if it is given,
    and returns a new reference to the updated traceback, which may be null if no new
    frames were generated and `head` is null. */
    inline PyTracebackObject* build_traceback(
        const cpptrace::stacktrace& trace,
        PyTracebackObject* head = nullptr
    ) {
        PyThreadState* tstate = PyThreadState_Get();
        Object globals = steal<Object>(PyDict_New());
        if (globals.is(nullptr)) {
            Exception::from_python();
        }
        for (const cpptrace::stacktrace_frame& frame : trace) {
            size_t line = frame.line.value_or(0);
            PyCodeObject* code = PyCode_NewEmpty(
                frame.filename.c_str(),
                frame.symbol.c_str(),
                line
            );
            if (!code) {
                Exception::from_python();
            }
            PyFrameObject* py_frame = PyFrame_New(
                tstate,
                code,
                ptr(globals),
                nullptr
            );
            Py_DECREF(code);
            if (!py_frame) {
                Exception::from_python();
            }
            py_frame->f_lineno = line;
            PyTracebackObject* tb = PyObject_GC_New(
                PyTracebackObject,
                &PyTraceBack_Type
            );
            if (tb == nullptr) {
                Py_DECREF(py_frame);
                throw MemoryError();
            }
            tb->tb_next = head;
            tb->tb_frame = py_frame;  // steals reference
            tb->tb_lasti = PyFrame_GetLasti(tb->tb_frame) * sizeof(_Py_CODEUNIT);
            tb->tb_lineno = PyFrame_GetLineNumber(tb->tb_frame);
            PyObject_GC_Track(tb);
            head = tb;
        }
        return head;
    }

    template <typename T>
    [[noreturn]] void ExceptionTable::simple_from_python(Object exception) {
        Object args = steal<Object>(PyException_GetArgs(ptr(exception)));
        if (args.is(nullptr)) {
            Exception::from_python();
        }
        PyObject* message;
        if (
            PyTuple_GET_SIZE(ptr(args)) != 1 ||
            !PyUnicode_Check(message = PyTuple_GET_ITEM(ptr(args), 0))
        ) {
            throw AssertionError(
                "Python exception must take a single string argument or "
                "register a custom from_python() handler: '" + type_name<T> + "'"
            );
        }
        Py_ssize_t len;
        const char* text = PyUnicode_AsUTF8AndSize(message, &len);
        if (text == nullptr) {
            Exception::from_python();
        }
        throw T(std::string(text, len));
    }

    template <typename T>
    Object ExceptionTable::simple_to_python(const Exception& exception) {
        // look up equivalent Python type for T
        auto table = exception_table.find(PyInterpreterState_Get());
        if (table == exception_table.end()) {
            throw AssertionError(
                "no exception table found for the current Python interpreter"
            );
        }
        auto it = table->second.types.find(typeid(T));
        if (it == table->second.types.end()) {
            throw AssertionError(
                "no Python exception type registered for C++ exception of type '" +
                type_name<T> + "'"
            );
        }

        // convert C++ exception message to a Python string
        Object message = steal<Object>(PyUnicode_FromStringAndSize(
            exception.message().data(),
            exception.message().size()
        ));
        if (message.is(nullptr)) {
            Exception::from_python();
        }

        // call the Python constructor with the converted message
        Object value = steal<Object>(PyObject_CallOneArg(
            ptr(it->second),
            ptr(message)
        ));
        if (value.is(nullptr)) {
            Exception::from_python();
        }
        return value;
    }

}


namespace meta::detail {
    template <typename T>
    inline constexpr bool builtin_type<impl::py_err<T>> = true;
}


inline void Exception::to_python() noexcept {
    constexpr auto raise = [](const Exception& exc) {
        PyThreadState* tstate = impl::assert_no_active_python_exception();
        auto table = impl::exception_table.find(tstate->interp);
        if (table == impl::exception_table.end()) {
            throw AssertionError(
                "no exception table found for the current Python interpreter"
            );
        }
        auto it = table->second.to_python.find(exc.type());
        if (it == table->second.to_python.end()) {
            std::cerr << "no to_python() handler for exception of type '"
                      << exc.name() << "'";
            std::exit(1);
        }
        Object value = it->second(exc);
        if (auto traceback = exc.trace()) {
            PyObject* existing = PyException_GetTraceback(ptr(value));  // new reference
            Object tb = steal<Object>(reinterpret_cast<PyObject*>(
                impl::build_traceback(
                    *traceback,
                    reinterpret_cast<PyTracebackObject*>(existing)  // steals a reference
                ))
            );
            if (!tb.is(existing)) {
                PyException_SetTraceback(ptr(value), ptr(tb));
            }
        }
        PyErr_SetRaisedException(release(value));  // steals a reference
    };

    try {
        throw;
    } catch (const Exception& exc) {
        raise(exc.trim_after(1));
    } catch (const std::exception& e) {
        raise(Exception(e.what()).trim_after(1));
    } catch (...) {
        raise(Exception("unknown C++ exception").trim_after(1));
    }
}


[[noreturn]] inline void Exception::from_python() {
    Object exception = steal<Object>(PyErr_GetRaisedException());
    if (exception.is(nullptr)) {
        throw AssertionError(
            "Exception::from_python() called without an active Python exception "
            "for the current interpreter"
        );
    }
    PyTypeObject* type = Py_TYPE(ptr(exception));
    if (!type) {
        throw AssertionError(
            "Exception::from_python() could not determine the type of Python "
            "exception being raised"
        );
    }
    auto table = impl::exception_table.find(PyInterpreterState_Get());
    if (table == impl::exception_table.end()) {
        throw AssertionError(
            "no exception table found for the current Python interpreter"
        );
    }
    auto it = table->second.from_python.find(
        borrow<Object>(reinterpret_cast<PyObject*>(type))
    );
    if (it == table->second.from_python.end()) {
        throw AssertionError(
            "no from_python() handler for Python exception of type '" +
            demangle(type->tp_name) + "'"
        );
    }
    it->second(std::move(exception));  // throws py_err<T>
    throw AssertionError(
        "from_python() handler must throw an exception of type 'py_err<T>'"
    );
}


}  // namespace bertrand


#endif
