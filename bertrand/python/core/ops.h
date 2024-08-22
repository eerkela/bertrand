#ifndef BERTRAND_PYTHON_CORE_OPS_H
#define BERTRAND_PYTHON_CORE_OPS_H

#include "declarations.h"
#include "object.h"
#include "except.h"
#include "type.h"


namespace py {


template <std::derived_from<Object> T>
struct __as_object__<T>                                     : Returns<T> {
    static decltype(auto) operator()(T&& value) { return std::forward<T>(value); }
};


/* Convert an arbitrary C++ value to an equivalent Python object if it isn't one
already. */
template <typename T> requires (__as_object__<std::remove_cvref_t<T>>::enable)
[[nodiscard]] decltype(auto) as_object(T&& value) {
    using Obj = __as_object__<std::remove_cvref_t<T>>;
    static_assert(
        !std::same_as<typename Obj::type, Object>,
        "C++ types cannot be converted to py::Object directly.  Check your "
        "specialization of __as_object__ for this type and ensure the Return type "
        "is set to a subclass of py::Object, not py::Object itself."
    );
    if constexpr (impl::has_call_operator<Obj>) {
        return Obj{}(std::forward<T>(value));
    } else {
        return typename Obj::type(std::forward<T>(value));
    }
}


template <typename Self>
[[nodiscard]] Object::operator bool(this const Self& self) {
    if constexpr (
        impl::originates_from_cpp<Self> &&
        impl::has_operator_bool<impl::cpp_type<Self>>
    ) {
        return static_cast<bool>(unwrap(self));
    } else {
        int result = PyObject_IsTrue(ptr(self));
        if (result == -1) {
            Exception::from_python();
        }
        return result;   
    }
}


template <typename Self, typename Key> requires (__contains__<Self, Key>::enable)
[[nodiscard]] bool Object::contains(this const Self& self, const Key& key) {
    using Return = typename __contains__<Self, Key>::type;
    static_assert(
        std::same_as<Return, bool>,
        "contains() operator must return a boolean value.  Check your "
        "specialization of __contains__ for these types and ensure the Return "
        "type is set to bool."
    );
    if constexpr (impl::has_call_operator<__contains__<Self, Key>>) {
        return __contains__<Self, Key>{}(self, key);

    } else if constexpr (
        impl::originates_from_cpp<Self> &&
        impl::cpp_or_originates_from_cpp<Key>
    ) {
        static_assert(
            impl::has_contains<impl::cpp_type<Self>, impl::cpp_type<Key>>,
            "__contains__<Self, Key> is enabled for operands whose C++ "
            "representations have no viable overload for `Self.contains(Key)`"
        );
        return unwrap(self).contains(unwrap(key));

    } else {
        int result = PySequence_Contains(
            ptr(self),
            ptr(as_object(key))
        );
        if (result == -1) {
            Exception::from_python();
        }
        return result;
    }
}


namespace impl {
    static PyObject* one = (Interpreter::init(), PyLong_FromLong(1));  // immortal

    /// TODO: control structures need to be defined for attr and item proxies, which
    /// forward to the wrapped type.

    /* A proxy for the result of an attribute lookup that is controlled by the
    `__getattr__`, `__setattr__`, and `__delattr__` control structs.

    This is a simple extension of an Object type that intercepts `operator=` and
    assigns the new value back to the attribute using the appropriate API.  Mutating
    the object in any other way will also modify it in-place on the parent. */
    template <typename Self, StaticStr Name>
        requires (__getattr__<Self, Name>::enable)
    struct Attr : __getattr__<Self, Name>::type {
    private:
        using Base = __getattr__<Self, Name>::type;
        static_assert(
            std::derived_from<Base, Object>,
            "default attribute access operator must return a subclass of py::Object.  "
            "Check your specialization of __getattr__ for this type and ensure the "
            "Returns annotation is set to a subclass of py::Object, or define a "
            "custom call operator to implement the desired behavior."
        );

        template <typename T> requires (
            std::is_rvalue_reference_v<T> &&
            impl::attr_is_deletable<std::remove_cvref_t<T>>::value
        )
        friend void del(T&& item);
        template <std::derived_from<Object> T>
        friend PyObject* ptr(const T&);
        template <std::derived_from<Object> T>
        friend PyObject* release(T&);
        template <std::derived_from<Object> T> requires (!std::is_const_v<T>)
        friend PyObject* release(T&& obj);

        Self m_self;

        /* The wrapper's `m_ptr` member is lazily evaluated to avoid repeated lookups.
        Replacing it with a computed property will trigger a __getattr__ lookup the
        first time it is accessed. */
        __declspec(property(get = get_ptr, put = set_ptr)) PyObject* m_ptr;
        void set_ptr(PyObject* value) { Base::m_ptr = value; }
        PyObject* get_ptr() {
            if (Base::m_ptr == nullptr) {
                if constexpr (has_call_operator<__getattr__<Self, Name>>) {
                    Base::m_ptr = release(__getattr__<Self, Name>{}(m_self));
                } else {
                    PyObject* result = PyObject_GetAttr(
                        ptr(m_self),
                        impl::TemplateString<Name>::ptr
                    );
                    if (result == nullptr) {
                        Exception::from_python();
                    }
                    Base::m_ptr = result;
                }
            }
            return Base::m_ptr;
        }

    public:

        Attr(const Self& self) :
            Base(nullptr, Object::stolen_t{}), m_self(self)
        {}
        Attr(Self&& self) :
            Base(nullptr, Object::stolen_t{}), m_self(std::move(self))
        {}
        Attr(const Attr& other) :
            Base(other), m_self(other.m_self)
        {}
        Attr(Attr&& other) :
            Base(std::move(other)), m_self(std::move(other.m_self))
        {}

        template <typename Value>
            requires (!__setattr__<Self, Name, std::remove_cvref_t<Value>>::enable)
        Attr& operator=(Value&& other) = delete;
        template <typename Value>
            requires (__setattr__<Self, Name, std::remove_cvref_t<Value>>::enable)
        Attr& operator=(Value&& value) {
            using setattr = __setattr__<Self, Name, std::remove_cvref_t<Value>>;
            using Return = typename setattr::type;
            static_assert(
                std::is_void_v<Return>,
                "attribute assignment operator must return void.  Check your "
                "specialization of __setattr__ for these types and ensure the Return "
                "type is set to void."
            );
            Base::operator=(std::forward<Value>(value));
            if constexpr (has_call_operator<setattr>) {
                setattr{}(m_self, value);

            } else if constexpr (
                originates_from_cpp<Base> &&
                cpp_or_originates_from_cpp<Value>
            ) {
                if constexpr (python_like<std::remove_cvref_t<Value>>) {
                    unwrap(*this) = unwrap(std::forward<Value>(value));
                } else {
                    unwrap(*this) = std::forward<Value>(value);
                }

            } else {
                if (PyObject_SetAttr(
                    ptr(m_self),
                    TemplateString<Name>::ptr,
                    m_ptr
                )) {
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
    template <typename Container, typename... Key>
        requires (__getitem__<Container, Key...>::enable)
    struct Item : __getitem__<Container, Key...>::type {
    private:
        using Base = __getitem__<Container, Key...>::type;
        static_assert(sizeof...(Key) > 0, "Item must have at least one key.");
        static_assert(
            std::derived_from<Base, Object>,
            "default index operator must return a subclass of py::Object.  Check your "
            "specialization of __getitem__ for this type and ensure the Returns "
            "annotation is set to a subclass of py::Object, or define a custom call "
            "operator to implement the desired behavior."
        );

        using M_Key = std::conditional_t<
            (sizeof...(Key) == 1),
            std::tuple_element_t<0, std::tuple<Key...>>,
            std::tuple<Key...>
        >;

        template <typename T> requires (
            std::is_rvalue_reference_v<T> &&
            impl::item_is_deletable<std::remove_cvref_t<T>>::value
        )
        friend void del(T&& item);
        template <std::derived_from<Object> T>
        friend PyObject* ptr(const T&);
        template <std::derived_from<Object> T>
        friend PyObject* release(T&);
        template <std::derived_from<Object> T> requires (!std::is_const_v<T>)
        friend PyObject* release(T&& obj);

        Container m_container;
        M_Key m_key;

        /* The wrapper's `m_ptr` member is lazily evaluated to avoid repeated lookups.
        Replacing it with a computed property will trigger a __getitem__ lookup the
        first time it is accessed. */
        __declspec(property(get = get_ptr, put = set_ptr)) PyObject* m_ptr;
        void set_ptr(PyObject* value) { Base::m_ptr = value; }
        PyObject* get_ptr() {
            if (Base::m_ptr == nullptr) {
                if constexpr (has_call_operator<__getitem__<Container, Key...>>) {
                    Base::m_ptr = std::apply(
                        [&](const Key&... keys) {
                            return release(
                                __getitem__<Container, Key...>{}(m_container, keys...)
                            );
                        },
                        m_key
                    );
                } else {
                    PyObject* result = PyObject_GetItem(
                        ptr(m_container),
                        ptr(as_object(m_key))
                    );
                    if (result == nullptr) {
                        Exception::from_python();
                    }
                    Base::m_ptr = result;
                }
            }
            return Base::m_ptr;
        }

    public:

        template <typename C, typename... Ks>
        Item(const Container& container, Ks&&... key) :
            Base(nullptr, Object::stolen_t{}), m_container(container),
            m_key(std::forward<Ks>(key)...)
        {}
        template <typename C, typename... Ks>
        Item(Container&& container, Ks&&... key) :
            Base(nullptr, Object::stolen_t{}), m_container(std::move(container)),
            m_key(std::forward<Ks>(key)...)
        {}
        Item(const Item& other) :
            Base(other), m_container(other.m_container), m_key(other.m_key)
        {}
        Item(Item&& other) :
            Base(std::move(other)), m_container(std::move(other.m_container)),
            m_key(std::move(other.m_key))
        {}

        template <typename Value>
            requires (!__setitem__<Container, std::remove_cvref_t<Value>, Key...>::enable)
        Item& operator=(Value&& other) = delete;
        template <typename Value>
            requires (__setitem__<Container, std::remove_cvref_t<Value>, Key...>::enable)
        Item& operator=(Value&& value) {
            using setitem = __setitem__<Container, std::remove_cvref_t<Value>, Key...>;
            using Return = typename setitem::type;
            static_assert(
                std::is_void_v<Return>,
                "index assignment operator must return void.  Check your "
                "specialization of __setitem__ for these types and ensure the Return "
                "type is set to void."
            );
            Base::operator=(std::forward<Value>(value));
            if constexpr (impl::has_call_operator<setitem>) {
                /// NOTE: all custom __setitem__ operators must reverse the order of the
                /// value and keys.  Also, they will only ever be called with the
                /// value as a python object.
                std::apply([&](const Key&... keys) {
                    setitem{}(m_container, *this, keys...);
                }, m_key);

            } else if constexpr (
                impl::originates_from_cpp<Base> &&
                (impl::cpp_or_originates_from_cpp<Key> && ...) &&
                impl::cpp_or_originates_from_cpp<std::remove_cvref_t<Value>>
            ) {
                static_assert(
                    impl::supports_item_assignment<Base, std::remove_cvref_t<Value>, Key...>,
                    "__setitem__<Self, Value, Key...> is enabled for operands whose C++ "
                    "representations have no viable overload for `Self[Key...] = Value`"
                );
                if constexpr (impl::python_like<std::remove_cvref_t<Value>>) {
                    unwrap(*this) = unwrap(std::forward<Value>(value));
                } else {
                    unwrap(*this) = std::forward<Value>(value);
                }

            } else {
                if (PyObject_SetItem(
                    ptr(m_container),
                    ptr(as_object(m_key)),
                    m_ptr
                )) {
                    Exception::from_python();
                }
            }
            return *this;
        }

    };

}


template <typename Self, typename... Key> requires (__getitem__<Self, Key...>::enable)
decltype(auto) Object::operator[](this const Self& self, Key&&... key) {
    using getitem = __getitem__<Self, std::decay_t<Key>...>;
    if constexpr (std::derived_from<typename getitem::type, Object>) {
        return impl::Item<Self, std::decay_t<Key>...>(self, std::forward<Key>(key)...);
    } else {
        static_assert(
            std::is_invocable_r_v<typename getitem::type, getitem, const Self&, Key...>,
            "__getitem__ is specialized to return a C++ value, but the call operator "
            "does not accept the correct arguments.  Check your specialization of "
            "__getitem__ for these types and ensure a call operator is defined that "
            "accepts these arguments."
        );
        return getitem{}(self, std::forward<Key>(key)...);
    }
}


/* Replicates Python's `del` keyword for attribute and item deletion.  Note that the
usage of `del` to dereference naked Python objects is not supported - only those uses
which would translate to a `PyObject_DelAttr()` or `PyObject_DelItem()` are considered
valid. */
template <typename T>
    requires (std::is_rvalue_reference_v<T> && impl::item_is_deletable<T>::value)
void del(T&& item) {
    using delitem = impl::item_is_deletable<T>::type;
    using Return = delitem::type;
    static_assert(
        std::is_void_v<Return>,
        "index deletion operator must return void.  Check your specialization "
        "of __delitem__ for these types and ensure the Return type is set to void."
    );
    if constexpr (impl::has_call_operator<delitem>) {
        delitem{}(std::move(item));
    } else {
        if (PyObject_DelItem(
            ptr(item.m_container),
            ptr(as_object(item.m_key))
        )) {
            Exception::from_python();
        }
    }
}


/* Replicates Python's `del` keyword for attribute and item deletion.  Note that the
usage of `del` to dereference naked Python objects is not supported - only those uses
which would translate to a `PyObject_DelAttr()` or `PyObject_DelItem()` are considered
valid. */
template <typename T>
    requires (std::is_rvalue_reference_v<T> && impl::attr_is_deletable<T>::value)
void del(T&& attr) {
    using delattr = impl::attr_is_deletable<T>::type;
    using Return = delattr::type;
    static_assert(
        std::is_void_v<Return>,
        "index deletion operator must return void.  Check your specialization "
        "of __delitem__ for these types and ensure the Return type is set to void."
    );
    if constexpr (impl::has_call_operator<delattr>) {
        delattr{}(std::move(attr));
    } else {
        if (PyObject_DelAttr(
            ptr(attr.m_container),
            impl::TemplateString<delattr::name>::ptr
        )) {
            Exception::from_python();
        }
    }
}


template <impl::lazily_evaluated From, typename To>
    requires (std::convertible_to<impl::lazy_type<From>, To>)
struct __cast__<From, To>                                   : Returns<To> {
    static To operator()(const From& item) {
        if constexpr (impl::originates_from_cpp<impl::lazy_type<From>>) {
            return unwrap(item);
        } else {
            return reinterpret_steal<impl::lazy_type<From>>(ptr(item));
        }
    }
};


template <impl::lazily_evaluated From, typename To>
    requires (impl::explicitly_convertible_to<impl::lazy_type<From>, To>)
struct __explicit_cast__<From, To>                          : Returns<To> {
    static To operator()(const From& item) {
        if constexpr (impl::originates_from_cpp<impl::lazy_type<From>>) {
            return static_cast<To>(unwrap(item));
        } else {
            return static_cast<To>(
                reinterpret_steal<impl::lazy_type<From>>(ptr(item))
            );
        }
    }
};


/// TODO: more control structures for Attr and Item?


namespace impl {

    /* Represents a keyword parameter pack obtained by double-dereferencing a Python
    object. */
    template <std::derived_from<Object> T> requires (impl::mapping_like<T>)
    struct KwargPack {
        using key_type = T::key_type;
        using mapped_type = T::mapped_type;

        T value;

    private:

        static constexpr bool can_iterate =
            impl::yields_pairs_with<T, key_type, mapped_type> ||
            impl::has_items<T> ||
            (impl::has_keys<T> && impl::has_values<T>) ||
            (impl::yields<T, key_type> && impl::lookup_yields<T, mapped_type, key_type>) ||
            (impl::has_keys<T> && impl::lookup_yields<T, mapped_type, key_type>);

        auto transform() const {
            if constexpr (impl::yields_pairs_with<T, key_type, mapped_type>) {
                return value;

            } else if constexpr (impl::has_items<T>) {
                return value.items();

            } else if constexpr (impl::has_keys<T> && impl::has_values<T>) {
                return std::ranges::views::zip(value.keys(), value.values());

            } else if constexpr (
                impl::yields<T, key_type> && impl::lookup_yields<T, mapped_type, key_type>
            ) {
                return std::ranges::views::transform(
                    value,
                    [&](const key_type& key) {
                        return std::make_pair(key, value[key]);
                    }
                );

            } else {
                return std::ranges::views::transform(
                    value.keys(),
                    [&](const key_type& key) {
                        return std::make_pair(key, value[key]);
                    }
                );
            }
        }

    public:

        template <typename U = T> requires (can_iterate)
        auto begin() const { return std::ranges::begin(transform()); }
        template <typename U = T> requires (can_iterate)
        auto cbegin() const { return begin(); }
        template <typename U = T> requires (can_iterate)
        auto end() const { return std::ranges::end(transform()); }
        template <typename U = T> requires (can_iterate)
        auto cend() const { return end(); }

    };


    /* Represents a positional parameter pack obtained by dereferencing a Python
    object. */
    template <std::derived_from<Object> T> requires (impl::iterable<T>)
    struct ArgPack {
        T value;

        auto begin() const { return std::ranges::begin(value); }
        auto cbegin() const { return begin(); }
        auto end() const { return std::ranges::end(value); }
        auto cend() const { return end(); }

        template <typename U = T> requires (impl::mapping_like<U>)
        auto operator*() const {
            return KwargPack<U>{value};
        }        
    };

}


template <std::derived_from<Object> Container> requires (impl::iterable<Container>)
[[nodiscard]] auto operator*(const Container& self) {
    return impl::ArgPack<Container>{self};
}


template <std::derived_from<Object> Container, std::ranges::view View>
    requires (impl::iterable<Container>)
[[nodiscard]] auto operator->*(const Container& container, const View& view) {
    return std::views::all(container) | view;
}


template <std::derived_from<Object> Container, typename Func>
    requires (
        impl::iterable<Container> &&
        !std::ranges::view<Func> &&
        std::is_invocable_v<Func, impl::iter_type<Container>>
    )
[[nodiscard]] auto operator->*(const Container& container, Func func) {
    using Return = std::invoke_result_t<Func, impl::iter_type<Container>>;
    if constexpr (impl::is_optional<Return>) {
        return (
            std::views::all(container) |
            std::views::transform(func) |
            std::views::filter([](const Return& value) {
                return value.has_value();
            }) | std::views::transform([](const Return& value) {
            return value.value();
            })
        );
    } else {
        return std::views::all(container) | std::views::transform(func);
    }
}


/// TODO: eliminate full py::Iterator<> in favor of a simpler impl::Iterator<> type?


template <std::derived_from<Object> Return>
struct Type<Iterator<Return>>;


template <std::derived_from<Object> Return>
struct Interface<Iterator<Return>> {};
template <std::derived_from<Object> Return>
struct Interface<Type<Iterator<Return>>> {
    static py::Iterator<Return> __iter__(auto& iter);
    static Return __next__(auto& iter);
};


/* A generic Python iterator with a static value type.

This type has no fixed implementation, and can match any kind of iterator.  It roughly
corresponds to the `collections.abc.Iterator` abstract base class in Python, and makes
an arbitrary Python iterator accessible from C++.  Note that the reverse (exposing a
C++ iterator to Python) is done via the `__python__::__iter__(PyObject* self)` API
method, which returns a unique type for each container.  This class will match any of
those types, but is not restricted to them, and will be universally slower as a result.

In the interest of performance, no explicit checks are done to ensure that the return
type matches expectations.  As such, this class is one of the rare cases where type
safety may be violated, and should therefore be used with caution.  It is mostly meant
for internal use to back the default result of the `begin()` and `end()` operators when
no specialized C++ iterator can be found.  In that case, its value type is set to the
`T` in an `__iter__<Container> : Returns<T> {};` specialization. */
template <typename Return = Object>
struct Iterator : Object, Interface<Iterator<Return>>, impl::IterTag {

    Iterator(PyObject* p, borrowed_t t) : Object(p, t) {}
    Iterator(PyObject* p, stolen_t t) : Object(p, t) {}

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


/* The type of a generic Python iterator.  This is identical to the
`collections.abc.Iterator` abstract base class, and will match any Python iterator
regardless of return type. */
template <typename Return>
struct Type<Iterator<Return>> : Object, Interface<Type<Iterator<Return>>>, impl::TypeTag {
    struct __python__ : impl::TypeTag::def<__python__, py::Iterator<Return>> {
        static Type __import__() {
            PyObject* collections_abc = PyImport_Import(
                impl::TemplateString<"collections.abc">::ptr
            );
            if (collections_abc == nullptr) {
                Exception::from_python();
            }
            PyObject* iterator = PyObject_GetAttr(
                collections_abc,
                impl::TemplateString<"Iterator">::ptr
            );
            Py_DECREF(collections_abc);
            if (iterator == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Type>(iterator);
        }
    };

    Type(PyObject* p, borrowed_t t) : Object(p, t) {}
    Type(PyObject* p, stolen_t t) : Object(p, t) {}

    template <typename... Args> requires (implicit_ctor<Type>::template enable<Args...>)
    Type(Args&&... args) : Object(
        implicit_ctor<Type>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args> requires (explicit_ctor<Type>::template enable<Args...>)
    explicit Type(Args&&... args) : Object(
        explicit_ctor<Type>{},
        std::forward<Args>(args)...
    ) {}

};


template <std::derived_from<Object> T, typename Return>
struct __isinstance__<T, Iterator<Return>> : Returns<bool> {
    static constexpr bool operator()(const T& obj) {
        return PyIter_Check(ptr(obj));
    }
};


template <std::derived_from<Object> T, typename Return>
struct __issubclass__<T, Iterator<Return>> : Returns<bool> {
    static consteval bool operator()() {
        return
            std::derived_from<T, impl::IterTag> &&
            std::convertible_to<impl::iter_type<T>, Return>;
    }
};


template <typename T>
struct __iter__<Iterator<T>> : Returns<T> {
    using iterator_category = std::input_iterator_tag;
    using difference_type   = std::ptrdiff_t;
    using value_type        = T;
    using pointer           = T*;
    using reference         = T&;

    Iterator<T> iter;
    T curr;

    __iter__(const Iterator<T>& self) :
        iter(self), curr(reinterpret_steal<T>(nullptr))
    {}

    __iter__(Iterator<T>&& self) :
        iter(std::move(self)), curr(reinterpret_steal<T>(nullptr))
    {}

    __iter__(const Iterator<T>& self, int) : __iter__(self) {
        ++(*this);
    }

    __iter__(Iterator<T>&& self, int) : __iter__(std::move(self)) {
        ++(*this);
    }

    /// NOTE: modifying a copy of a Python iterator will also modify the original due
    /// to the inherent state of the iterator, which is managed by the Python runtime.
    /// This class is only copyable in order to fulfill the requirements of the
    /// iterator protocol, but it is not recommended to use it in this way.

    __iter__(const __iter__& other) :
        iter(other.iter), curr(other.curr)
    {}

    __iter__(__iter__&& other) :
        iter(std::move(other.iter)), curr(std::move(other.curr))
    {}

    __iter__& operator=(const __iter__& other) {
        if (&other != this) {
            iter = other.iter;
            curr = other.curr;
        }
        return *this;
    }

    __iter__& operator=(__iter__&& other) {
        if (&other != this) {
            iter = std::move(other.iter);
            curr = std::move(other.curr);
        }
        return *this;
    }

    [[nodiscard]] T& operator*() { return curr; }
    [[nodiscard]] T* operator->() { return &curr; }
    [[nodiscard]] const T& operator*() const { return curr; }
    [[nodiscard]] const T* operator->() const { return &curr; }

    __iter__& operator++() {
        PyObject* next = PyIter_Next(ptr(iter));
        if (PyErr_Occurred()) {
            Exception::from_python();
        }
        curr = reinterpret_steal<T>(next);
        return *this;
    }

    /// NOTE: post-increment is not supported due to inaccurate copy semantics.

    [[nodiscard]] bool operator==(const __iter__& other) const {
        return ptr(curr) == ptr(other.curr);
    }

    [[nodiscard]] bool operator!=(const __iter__& other) const {
        return ptr(curr) != ptr(other.curr);
    }

};
/// NOTE: no __iter__<const Iterator<T>> specialization since marking the iterator
/// itself as const prevents it from being incremented.


template <typename T, typename Return>
struct __contains__<T, Iterator<Return>> : Returns<bool> {};


template <typename Return>
struct __getattr__<Iterator<Return>, "__iter__"> : Returns<Function<Iterator<Return>()>> {};
template <typename Return>
struct __getattr__<Iterator<Return>, "__next__"> : Returns<Function<Return()>> {};
template <typename Return>
struct __getattr__<Type<Iterator<Return>>, "__iter__"> : Returns<Function<
    Iterator<Return>(Iterator<Return>&)
>> {};
template <typename Return>
struct __getattr__<Type<Iterator<Return>>, "__next__"> : Returns<Function<
    Return(Iterator<Return>&)
>> {};


template <std::derived_from<Object> Return>
auto Interface<Type<py::Iterator<Return>>>::__iter__(auto& iter)
    -> py::Iterator<Return>
{
    return iter;
}


template <std::derived_from<Object> Return>
auto Interface<Type<py::Iterator<Return>>>::__next__(auto& iter)
    -> Return
{
    PyObject* next = PyIter_Next(ptr(iter));
    if (next == nullptr) {
        if (PyErr_Occurred()) {
            Exception::from_python();
        }
        throw StopIteration();
    }
    return reinterpret_steal<Return>(next);
}


/* Begin iteration operator.  Both this and the end iteration operator are
controlled by the __iter__ control struct, whose return type dictates the
iterator's dereference type. */
template <typename Self> requires (__iter__<Self>::enable)
[[nodiscard]] auto begin(Self& self) {
    if constexpr (std::is_constructible_v<__iter__<Self>, const Self&, int>) {
        static_assert(
            std::is_constructible_v<__iter__<Self>, const Self&>,
            "__iter__<T> specializes the begin iterator, but not the end iterator.  "
            "Did you forget to define an `__iter__(const T&)` constructor?"
        );
        return __iter__<Self>(self, 0);
    } else {
        static_assert(
            !std::is_constructible_v<__iter__<Self>, const Self&>,
            "__iter__<T> specializes the end iterator, but not the begin iterator.  "
            "Did you forget to define an `__iter__(const T&, int)` constructor?"
        );
        if constexpr (impl::originates_from_cpp<Self>) {
            return std::ranges::begin(py::unwrap(self));
        } else {
            using Return = typename __iter__<Self>::type;
            static_assert(
                std::derived_from<Return, Object>,
                "iterator must dereference to a subclass of Object.  Check your "
                "specialization of __iter__ for this types and ensure the Return type "
                "is a subclass of py::Object."
            );
            PyObject* iter = PyObject_GetIter(ptr(self));
            if (iter == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Iterator<Return>>(iter);
        }
    }
}


/* Begin iteration operator.  Both this and the end iteration operator are
controlled by the __iter__ control struct, whose return type dictates the
iterator's dereference type. */
template <typename Self> requires (__iter__<const Self>::enable)
[[nodiscard]] auto begin(const Self& self) {
    if constexpr (std::is_constructible_v<__iter__<Self>, const Self&, int>) {
        static_assert(
            std::is_constructible_v<__iter__<Self>, const Self&>,
            "__iter__<T> specializes the begin iterator, but not the end iterator.  "
            "Did you forget to define an `__iter__(const T&)` constructor?"
        );
        return __iter__<Self>(self, 0);
    } else {
        static_assert(
            !std::is_constructible_v<__iter__<Self>, const Self&>,
            "__iter__<T> specializes the end iterator, but not the begin iterator.  "
            "Did you forget to define an `__iter__(const T&, int)` constructor?"
        );
        if constexpr (impl::originates_from_cpp<Self>) {
            return std::ranges::begin(py::unwrap(self));
        } else {
            using Return = typename __iter__<Self>::type;
            static_assert(
                std::derived_from<Return, Object>,
                "iterator must dereference to a subclass of Object.  Check your "
                "specialization of __iter__ for this types and ensure the Return type "
                "is a subclass of py::Object."
            );
            PyObject* iter = PyObject_GetIter(ptr(self));
            if (iter == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Iterator<Return>>(iter);
        }
    }
}


/* Const iteration operator.  Python has no distinction between mutable and
immutable iterators, so this is fundamentally the same as the ordinary begin()
method.  Some libraries assume the existence of this method. */
template <typename Self> requires (__iter__<const Self>::enable)
[[nodiscard]] auto cbegin(const Self& self) {
    return begin(self);
}


/* End iteration operator.  This terminates the iteration and is controlled by the
__iter__ control struct. */
template <typename Self> requires (__iter__<Self>::enable)
[[nodiscard]] auto end(Self& self) {
    if constexpr (std::is_constructible_v<__iter__<Self>, const Self&>) {
        static_assert(
            std::is_constructible_v<__iter__<Self>, const Self&, int>,
            "__iter__<T> specializes the begin iterator, but not the end iterator.  "
            "Did you forget to define an `__iter__(const T&)` constructor?"
        );
        return __iter__<Self>(self);
    } else {
        static_assert(
            !std::is_constructible_v<__iter__<Self>, const Self&, int>,
            "__iter__<T> specializes the end iterator, but not the begin iterator.  "
            "Did you forget to define an `__iter__(const T&, int)` constructor?"
        );
        if constexpr (impl::originates_from_cpp<Self>) {
            return std::ranges::end(py::unwrap(self));
        } else {
            using Return = typename __iter__<Self>::type;
            static_assert(
                std::derived_from<Return, Object>,
                "iterator must dereference to a subclass of Object.  Check your "
                "specialization of __iter__ for this types and ensure the Return type "
                "is a subclass of py::Object."
            );
            return reinterpret_steal<Iterator<Return>>(nullptr);
        }
    }
}


/* End iteration operator.  This terminates the iteration and is controlled by the
__iter__ control struct. */
template <typename Self> requires (__iter__<const Self>::enable)
[[nodiscard]] auto end(const Self& self) {
    if constexpr (std::is_constructible_v<__iter__<Self>, const Self&>) {
        static_assert(
            std::is_constructible_v<__iter__<Self>, const Self&, int>,
            "__iter__<T> specializes the begin iterator, but not the end iterator.  "
            "Did you forget to define an `__iter__(const T&)` constructor?"
        );
        return __iter__<Self>(self);
    } else {
        static_assert(
            !std::is_constructible_v<__iter__<Self>, const Self&, int>,
            "__iter__<T> specializes the end iterator, but not the begin iterator.  "
            "Did you forget to define an `__iter__(const T&, int)` constructor?"
        );
        if constexpr (impl::originates_from_cpp<Self>) {
            return std::ranges::end(py::unwrap(self));
        } else {
            using Return = typename __iter__<Self>::type;
            static_assert(
                std::derived_from<Return, Object>,
                "iterator must dereference to a subclass of Object.  Check your "
                "specialization of __iter__ for this types and ensure the Return type "
                "is a subclass of py::Object."
            );
            return reinterpret_steal<Iterator<Return>>(nullptr);
        }
    }
}


/* Const end operator.  Similar to `cbegin()`, this is identical to `end()`. */
template <typename Self> requires (__iter__<const Self>::enable)
[[nodiscard]] auto cend(const Self& self) {
    return end(self);
}


/* Reverse iteration operator.  Both this and the reverse end operator are
controlled by the __reversed__ control struct, whose return type dictates the
iterator's dereference type. */
template <typename Self> requires (__reversed__<Self>::enable)
[[nodiscard]] auto rbegin(Self& self) {
    if constexpr (std::is_constructible_v<__reversed__<Self>, const Self&, int>) {
        static_assert(
            std::is_constructible_v<__reversed__<Self>, const Self&>,
            "__reversed__<T> specializes the begin iterator, but not the end "
            "iterator.  Did you forget to define a `__reversed__(const T&)` "
            "constructor?"
        );
        return __reversed__<Self>(self, 0);
    } else {
        static_assert(
            !std::is_constructible_v<__reversed__<Self>, const Self&>,
            "__reversed__<T> specializes the end iterator, but not the begin "
            "iterator.  Did you forget to define a `__reversed__(const T&, int)` "
            "constructor?"
        );
        if constexpr (impl::originates_from_cpp<Self>) {
            return std::ranges::rbegin(py::unwrap(self));
        } else {
            using Return = typename __reversed__<Self>::type;
            static_assert(
                std::derived_from<Return, Object>,
                "iterator must dereference to a subclass of Object.  Check your "
                "specialization of __reversed__ for this types and ensure the Return "
                "type is a subclass of py::Object."
            );
            PyObject* iter = PyObject_CallMethodNoArgs(
                ptr(self),
                impl::TemplateString<"__reversed__">::ptr
            );
            if (iter == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Iterator<Return>>(iter);
        }
    }
}


/* Reverse iteration operator.  Both this and the reverse end operator are
controlled by the __reversed__ control struct, whose return type dictates the
iterator's dereference type. */
template <typename Self> requires (__reversed__<const Self>::enable)
[[nodiscard]] auto rbegin(const Self& self) {
    if constexpr (std::is_constructible_v<__reversed__<Self>, const Self&, int>) {
        static_assert(
            std::is_constructible_v<__reversed__<Self>, const Self&>,
            "__reversed__<T> specializes the begin iterator, but not the end "
            "iterator.  Did you forget to define a `__reversed__(const T&)` "
            "constructor?"
        );
        return __reversed__<Self>(self, 0);
    } else {
        static_assert(
            !std::is_constructible_v<__reversed__<Self>, const Self&>,
            "__reversed__<T> specializes the end iterator, but not the begin "
            "iterator.  Did you forget to define a `__reversed__(const T&, int)` "
            "constructor?"
        );
        if constexpr (impl::originates_from_cpp<Self>) {
            return std::ranges::rbegin(py::unwrap(self));
        } else {
            using Return = typename __reversed__<Self>::type;
            static_assert(
                std::derived_from<Return, Object>,
                "iterator must dereference to a subclass of Object.  Check your "
                "specialization of __reversed__ for this types and ensure the Return "
                "type is a subclass of py::Object."
            );
            PyObject* iter = PyObject_CallMethodNoArgs(
                ptr(self),
                impl::TemplateString<"__reversed__">::ptr
            );
            if (iter == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Iterator<Return>>(iter);
        }
    }
}


/* Const reverse iteration operator.  Python has no distinction between mutable
and immutable iterators, so this is fundamentally the same as the ordinary
rbegin() method.  Some libraries assume the existence of this method. */
template <typename Self> requires (__reversed__<const Self>::enable)
[[nodiscard]] auto crbegin(const Self& self) {
    return rbegin(self);
}


/* Reverse end operator.  This terminates the reverse iteration and is controlled
by the __reversed__ control struct. */
template <typename Self> requires (__reversed__<Self>::enable)
[[nodiscard]] auto rend(Self& self) {
    if constexpr (std::is_constructible_v<__reversed__<Self>, const Self&>) {
        static_assert(
            std::is_constructible_v<__reversed__<Self>, const Self&, int>,
            "__reversed__<T> specializes the begin iterator, but not the end "
            "iterator.  Did you forget to define a `__reversed__(const T&)` "
            "constructor?"
        );
        return __reversed__<Self>(self);
    } else {
        static_assert(
            !std::is_constructible_v<__reversed__<Self>, const Self&, int>,
            "__reversed__<T> specializes the end iterator, but not the begin "
            "iterator.  Did you forget to define a `__reversed__(const T&, int)` "
            "constructor?"
        );
        if constexpr (impl::originates_from_cpp<Self>) {
            return std::ranges::rend(py::unwrap(self));
        } else {
            using Return = typename __reversed__<Self>::type;
            static_assert(
                std::derived_from<Return, Object>,
                "iterator must dereference to a subclass of Object.  Check your "
                "specialization of __reversed__ for this types and ensure the Return "
                "type is a subclass of py::Object."
            );
            return reinterpret_steal<Iterator<Return>>(nullptr);
        }
    }
}


/* Reverse end operator.  This terminates the reverse iteration and is controlled
by the __reversed__ control struct. */
template <typename Self> requires (__reversed__<const Self>::enable)
[[nodiscard]] auto rend(const Self& self) {
    if constexpr (std::is_constructible_v<__reversed__<Self>, const Self&>) {
        static_assert(
            std::is_constructible_v<__reversed__<Self>, const Self&, int>,
            "__reversed__<T> specializes the begin iterator, but not the end "
            "iterator.  Did you forget to define a `__reversed__(const T&)` "
            "constructor?"
        );
        return __reversed__<Self>(self);
    } else {
        static_assert(
            !std::is_constructible_v<__reversed__<Self>, const Self&, int>,
            "__reversed__<T> specializes the end iterator, but not the begin "
            "iterator.  Did you forget to define a `__reversed__(const T&, int)` "
            "constructor?"
        );
        if constexpr (impl::originates_from_cpp<Self>) {
            return std::ranges::rend(py::unwrap(self));
        } else {
            using Return = typename __reversed__<Self>::type;
            static_assert(
                std::derived_from<Return, Object>,
                "iterator must dereference to a subclass of Object.  Check your "
                "specialization of __reversed__ for this types and ensure the Return "
                "type is a subclass of py::Object."
            );
            return reinterpret_steal<Iterator<Return>>(nullptr);
        }
    }
}


/* Const reverse end operator.  Similar to `crbegin()`, this is identical to
`rend()`. */
template <typename Self> requires (__reversed__<const Self>::enable)
[[nodiscard]] auto crend(const Self& self) {
    return rend(self);
}


/// TODO: __isinstance__ and __issubclass__ should test for inheritance from a Python
/// object's `Interface<T>` type, not the type itself.  That allows it to correctly
/// model multiple inheritance.


// TODO: implement the extra overloads for Object, BertrandMeta, Type, and Tuple of
// types/generic objects


// TODO: there also might be an issue with infinite recursion during automated
// isinstance() calls when converting Object to one of its subclasses, if that
// conversion occurs within the logic of isinstance() itself.


// TODO: update docs to better reflect the actual behavior of each overload.
//
//  isinstance<Base>(obj): checks if obj can be safely converted to Base when narrowing
//      a dynamic type.
//  isinstance(obj, base): equivalent to a Python-level isinstance() check.  Base must
//      be type-like, a union of types, or a dynamic object which can be narrowed to
//      either of the above.
//  issubclass<Base, Derived>(): does a compile-time check to see if Derived inherits
//      from Base.
//  issubclass<Base>(obj): devolves to an issubclass<Base, Derived>() check unless obj
//      is a dynamic object which may be narrowed to a single type.
//  issubclass(obj, base): equivalent to a Python-level issubclass() check.  Derived
//      must be a single type or a dynamic object which can be narrowed to a single
//      type, and base must be type-like, a union of types, or a dynamic object which
//      can be narrowed to either of the above.


/* Equivalent to Python `isinstance(obj, base)`, except that base is given as a
template parameter.  If a specialization of `__isinstance__` exists and implements a
call operator that takes a single `const Derived&` argument, then it will be called
directly.  Otherwise, if the argument is a dynamic object, it falls back to a
Python-level `isinstance()` check.  In all other cases, it will be evaluated at
compile-time by calling `issubclass<Derived, Base>()`. */
template <typename Base, typename Derived>
[[nodiscard]] constexpr bool isinstance(const Derived& obj) {
    if constexpr (std::is_invocable_v<
        __isinstance__<Derived, Base>,
        const Derived&
    >) {
        return __isinstance__<Derived, Base>{}(obj);

    } else if constexpr (impl::python_like<Base> && impl::dynamic_type<Derived>) {
        int result = PyObject_IsInstance(
            ptr(obj),
            ptr(Type<Base>())
        );
        if (result < 0) {
            Exception::from_python();
        }
        return result;

    } else {
        return issubclass<Derived, Base>();
    }
}


/* Equivalent to Python `isinstance(obj, base)`.  This overload must be explicitly
enabled by defining a two-argument call operator in a specialization of
`__issubclass__`.  By default, this is only done for `Type`, `BertrandMeta`, `Object`,
and `Tuple` as right-hand arguments. */
template <typename Derived, typename Base>
    requires (std::is_invocable_r_v<
        bool,
        __isinstance__<Derived, Base>,
        const Derived&,
        const Base&
    >)
[[nodiscard]] constexpr bool isinstance(const Derived& obj, const Base& base) {
    return __isinstance__<Derived, Base>{}(obj, base);
}


// TODO: isinstance() seems to work, but issubclass() is still complicated


/* Equivalent to Python `issubclass(obj, base)`, except that both arguments are given
as template parameters, marking the check as `consteval`.  This is essentially
equivalent to a `std::derived_from<>` check, except that custom logic from the
`__issubclass__` control structure will be used if available. */
template <typename Derived, typename Base>
[[nodiscard]] consteval bool issubclass() {
    if constexpr (std::is_invocable_v<__issubclass__<Derived, Base>>) {
        return __issubclass__<Derived, Base>{}();
    } else {
        return std::derived_from<Derived, Base>;
    }
}


/* Equivalent to Python `issubclass(obj, base)`, except that the base is given as a
template parameter, marking the check as `constexpr`.  This overload must be explicitly
enabled by defining a one-argument call operator in a specialization of the
`__issubclass__` control structure.  By default, this is only done for `Type`,
`BertrandMeta`, and `Object` as left-hand arguments, as well as `Type`, `BertrandMeta`,
`Object`, and `Tuple` as right-hand arguments. */
template <std::derived_from<Object> Base, std::derived_from<Object> Derived>
[[nodiscard]] constexpr bool issubclass(const Derived& obj) {
    if constexpr (std::is_invocable_v<__issubclass__<Derived, Base>, const Derived&>) {
        return __issubclass__<Derived, Base>{}(obj);

    } else if constexpr (impl::dynamic_type<Derived>) {
        return PyType_Check(ptr(obj)) && PyObject_IsSubclass(
            ptr(obj),
            ptr(Type<Base>())  // TODO: correct?
        );

    } else if constexpr (impl::type_like<Derived>) {
        return issubclass<Derived, Base>();

    } else {
        return false;
    }
}


/* Equivalent to Python `issubclass(obj, base)`.  This overload must be explicitly
enabled by defining a two-argument call operator in a specialization of
`__issubclass__`.  By default, this is only done for `Type`, `BertrandMeta`, and
`Object`, as left-hand arguments, as well as `Type`, `BertrandMeta`, `Object`, and
`Tuple` as right-hand arguments. */
template <typename Derived, typename Base>
    requires (std::is_invocable_v<
        __issubclass__<Derived, Base>,
        const Derived&,
        const Base&
    >)
[[nodiscard]] bool issubclass(const Derived& obj, const Base& base) {
    return __issubclass__<Derived, Base>{}(obj, base);
}


/* Equivalent to Python `hasattr(obj, name)` with a static attribute name. */
template <typename T, StaticStr name> requires (__as_object__<T>::enable)
[[nodiscard]] consteval bool hasattr() {
    return __getattr__<T, name>::enable;
}


/* Equivalent to Python `hasattr(obj, name)` with a static attribute name. */
template <StaticStr name, typename T>
[[nodiscard]] consteval bool hasattr(const T& obj) {
    return __getattr__<T, name>::enable;
}


/* Equivalent to Python `getattr(obj, name)` with a static attribute name. */
template <StaticStr name, typename T> requires (__getattr__<T, name>::enable)
[[nodiscard]] auto getattr(const T& obj) -> __getattr__<T, name>::type {
    if constexpr (impl::has_call_operator<__getattr__<T, name>>) {
        return __getattr__<T, name>{}(obj);
    } else {
        PyObject* result = PyObject_GetAttr(ptr(obj), impl::TemplateString<name>::ptr);
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<typename __getattr__<T, name>::type>(result);
    }
}


/* Equivalent to Python `getattr(obj, name, default)` with a static attribute name and
default value. */
template <StaticStr name, typename T> requires (__getattr__<T, name>::enable)
[[nodiscard]] auto getattr(
    const T& obj,
    const typename __getattr__<T, name>::type& default_value
) -> __getattr__<T, name>::type {
    if constexpr (impl::has_call_operator<__getattr__<T, name>>) {
        return __getattr__<T, name>{}(obj, default_value);
    } else {
        PyObject* result = PyObject_GetAttr(ptr(obj), impl::TemplateString<name>::ptr);
        if (result == nullptr) {
            PyErr_Clear();
            return default_value;
        }
        return reinterpret_steal<typename __getattr__<T, name>::type>(result);
    }
}


/* Equivalent to Python `setattr(obj, name, value)` with a static attribute name. */
template <StaticStr name, typename T, typename V> requires (__setattr__<T, name, V>::enable)
void setattr(const T& obj, const V& value) {
    if constexpr (impl::has_call_operator<__setattr__<T, name, V>>) {
        __setattr__<T, name, V>{}(obj, value);
    } else {
        if (PyObject_SetAttr(
            ptr(obj),
            impl::TemplateString<name>::ptr,
            ptr(as_object(value))
        ) < 0) {
            Exception::from_python();
        }
    }
}


/* Equivalent to Python `delattr(obj, name)` with a static attribute name. */
template <StaticStr name, typename T> requires (__delattr__<T, name>::enable)
void delattr(const T& obj) {
    if constexpr (impl::has_call_operator<__delattr__<T, name>>) {
        __delattr__<T, name>{}(obj);
    } else {
        if (PyObject_DelAttr(ptr(obj), impl::TemplateString<name>::ptr) < 0) {
            Exception::from_python();
        }
    }
}


/* Equivalent to Python `repr(obj)`, but returns a std::string and attempts to
represent C++ types using std::to_string or the stream insertion operator (<<).  If all
else fails, falls back to typeid(obj).name(). */
template <typename T>
[[nodiscard]] std::string repr(const T& obj) {
    if constexpr (impl::has_stream_insertion<T>) {
        std::ostringstream stream;
        stream << obj;
        return stream.str();

    } else if constexpr (impl::has_to_string<T>) {
        return std::to_string(obj);

    } else {
        try {
            PyObject* str = PyObject_Repr(ptr(as_object(obj)));
            if (str == nullptr) {
                Exception::from_python();
            }
            Py_ssize_t size;
            const char* data = PyUnicode_AsUTF8AndSize(str, &size);
            if (data == nullptr) {
                Py_DECREF(str);
                Exception::from_python();
            }
            std::string result(data, size);
            Py_DECREF(str);
            return result;
        } catch (...) {
            return typeid(obj).name();
        }
    }
}


/* Equivalent to Python `hash(obj)`, but delegates to std::hash, which is overloaded
for the relevant Python types.  This promotes hash-not-implemented exceptions into
compile-time equivalents. */
template <impl::hashable T>
[[nodiscard]] size_t hash(const T& obj) {
    return std::hash<T>{}(obj);
}


/* Equivalent to Python `len(obj)`. */
template <typename T> requires (__len__<T>::enable)
[[nodiscard]] size_t len(const T& obj) {
    using Return = typename __len__<T>::type;
    static_assert(
        std::same_as<Return, size_t>,
        "size() operator must return a size_t for compatibility with C++ "
        "containers.  Check your specialization of __len__ for these types "
        "and ensure the Return type is set to size_t."
    );
    if constexpr (impl::has_call_operator<__len__<T>>) {
        return __len__<T>{}(obj);

    } else if constexpr (impl::cpp_or_originates_from_cpp<T>) {
        static_assert(
            impl::has_size<impl::cpp_type<T>>,
            "__len__<T> is enabled for a type whose C++ representation does not have "
            "a viable overload for `std::size(T)`"
        );
        return std::size(unwrap(obj));

    } else {
        Py_ssize_t size = PyObject_Length(ptr(obj));
        if (size < 0) {
            Exception::from_python();
        }
        return size;
    }
}


/* Equivalent to Python `len(obj)`, except that it works on C++ objects implementing a
`size()` method. */
template <typename T> requires (!__len__<T>::enable && impl::has_size<T>)
[[nodiscard]] size_t len(const T& obj) {
    return std::size(obj);
}


/* An alias for `py::len(obj)`, but triggers ADL for constructs that expect a
free-floating size() function. */
template <typename T> requires (__len__<T>::enable)
[[nodiscard]] size_t size(const T& obj) {
    return len(obj);
}


/* An alias for `py::len(obj)`, but triggers ADL for constructs that expect a
free-floating size() function. */
template <typename T> requires (!__len__<T>::enable && impl::has_size<T>)
[[nodiscard]] size_t size(const T& obj) {
    return len(obj);
}


/* Equivalent to Python `abs(obj)` for any object that specializes the __abs__ control
struct. */
template <std::derived_from<Object> Self> requires (__abs__<Self>::enable)
[[nodiscard]] auto abs(const Self& self) {
    using Return = __abs__<Self>::type;
    static_assert(
        std::derived_from<Return, Object>,
        "Absolute value operator must return a py::Object subclass.  Check your "
        "specialization of __abs__ for this type and ensure the Return type is set to "
        "a py::Object subclass."
    );
    if constexpr (impl::has_call_operator<__abs__<Self>>) {
        return __abs__<Self>{}(self);

    } else if (impl::cpp_or_originates_from_cpp<Self>) {
        static_assert(
            impl::has_abs<impl::cpp_type<Self>>,
            "__abs__<T> is enabled for a type whose C++ representation does not have "
            "a viable overload for `std::abs(T)`"
        );
        return std::abs(unwrap(self));

    } else {
        PyObject* result = PyNumber_Absolute(ptr(self));
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Return>(result);
    }
}


/* Equivalent to Python `abs(obj)`, except that it takes a C++ value and applies
std::abs() for identical semantics. */
template <impl::has_abs T> requires (!__abs__<T>::enable && impl::has_abs<T>)
[[nodiscard]] auto abs(const T& value) {
    return std::abs(value);
}


/* Equivalent to Python `base ** exp` (exponentiation). */
template <typename Base, typename Exp> requires (__pow__<Base, Exp>::enable)
[[nodiscard]] auto pow(const Base& base, const Exp& exp) {
    using Return = typename __pow__<Base, Exp>::type;
    static_assert(
        std::derived_from<Return, Object>,
        "pow() must return a py::Object subclass.  Check your specialization "
        "of __pow__ for this type and ensure the Return type is derived from "
        "py::Object."
    );
    if constexpr (impl::has_call_operator<__pow__<Base, Exp>>) {
        return __pow__<Base, Exp>{}(base, exp);

    } else if constexpr (
        impl::cpp_or_originates_from_cpp<Base> &&
        impl::cpp_or_originates_from_cpp<Exp>
    ) {
        if constexpr (
            impl::complex_like<impl::cpp_type<Base>> &&
            impl::complex_like<impl::cpp_type<Exp>>
        ) {
            return std::common_type_t<impl::cpp_type<Base>, impl::cpp_type<Exp>>(
                pow(unwrap(base).real(), unwrap(exp).real()),
                pow(unwrap(base).imag(), unwrap(exp).imag())
            );
        } else if constexpr (impl::complex_like<impl::cpp_type<Base>>) {
            return Base(
                pow(unwrap(base).real(), unwrap(exp)),
                pow(unwrap(base).real(), unwrap(exp))
            );
        } else if constexpr (impl::complex_like<impl::cpp_type<Exp>>) {
            return Exp(
                pow(unwrap(base), unwrap(exp).real()),
                pow(unwrap(base), unwrap(exp).imag())
            );
        } else {
            static_assert(
                impl::has_pow<impl::cpp_type<Base>, impl::cpp_type<Exp>>,
                "__pow__<Base, Exp> is enabled for operands whose C++ representations "
                "have no viable overload for `std::pow(Base, Exp)`"
            );
            return std::pow(unwrap(base), unwrap(exp));
        }

    } else {
        PyObject* result = PyNumber_Power(
            ptr(as_object(base)),
            ptr(as_object(exp)),
            Py_None
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Return>(result);
    }
}


/* Equivalent to Python `pow(base, exp)`, except that it takes a C++ value and applies
std::pow() for identical semantics. */
template <typename Base, typename Exp>
    requires (
        !impl::any_are_python_like<Base, Exp> &&
        !__pow__<Base, Exp>::enable &&
        (
            impl::complex_like<Base> ||
            impl::complex_like<Exp> ||
            impl::has_pow<Base, Exp>
        )
    )
[[nodiscard]] auto pow(const Base& base, const Exp& exp) {
    if constexpr (impl::complex_like<Base> && impl::complex_like<Exp>) {
        return std::common_type_t<Base, Exp>(
            pow(base.real(), exp.real()),
            pow(base.imag(), exp.imag())
        );
    } else if constexpr (impl::complex_like<Base>) {
        return Base(
            pow(base.real(), exp),
            pow(base.imag(), exp)
        );
    } else if constexpr (impl::complex_like<Exp>) {
        return Exp(
            pow(base, exp.real()),
            pow(base, exp.imag())
        );
    } else {
        return std::pow(base, exp);
    }
}


/* Equivalent to Python `pow(base, exp, mod)`. */
template <impl::int_like Base, impl::int_like Exp, impl::int_like Mod>
    requires (__pow__<Base, Exp>::enable)
[[nodiscard]] auto pow(const Base& base, const Exp& exp, const Mod& mod) {
    using Return = typename __pow__<Base, Exp>::type;
    static_assert(
        std::derived_from<Return, Object>,
        "pow() must return a py::Object subclass.  Check your specialization "
        "of __pow__ for this type and ensure the Return type is derived from "
        "py::Object."
    );
    if constexpr (std::is_invocable_v<
        __pow__<Base, Exp>,
        const Base&,
        const Exp&,
        const Mod&
    >) {
        return __pow__<Base, Exp>{}(base, exp, mod);

    } else if constexpr (
        impl::cpp_or_originates_from_cpp<Base> &&
        impl::cpp_or_originates_from_cpp<Exp> &&
        impl::cpp_or_originates_from_cpp<Mod>
    ) {
        return pow(unwrap(base), unwrap(exp), unwrap(mod));

    } else {
        PyObject* result = PyNumber_Power(
            ptr(as_object(base)),
            ptr(as_object(exp)),
            ptr(as_object(mod))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Return>(result);
    }
}


/* Equivalent to Python `pow(base, exp, mod)`, but works on C++ integers with identical
semantics. */
template <std::integral Base, std::integral Exp, std::integral Mod>
[[nodiscard]] auto pow(Base base, Exp exp, Mod mod) {
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


template <typename L, typename R> requires (__floordiv__<L, R>::enable)
auto floordiv(const L& lhs, const R& rhs) {
    using Return = typename __floordiv__<L, R>::type;
    static_assert(
        std::derived_from<Return, Object>,
        "Floor division operator must return a py::Object subclass.  Check your "
        "specialization of __floordiv__ for these types and ensure the Return "
        "type is derived from py::Object."
    );
    if constexpr (impl::has_call_operator<__floordiv__<L, R>>) {
        return __floordiv__<L, R>{}(lhs, rhs);
    } else {
        PyObject* result = PyNumber_FloorDivide(
            ptr(as_object(lhs)),
            ptr(as_object(rhs))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Return>(result);
    }
}


template <std::derived_from<Object> L, typename R> requires (__ifloordiv__<L, R>::enable)
L& ifloordiv(L& lhs, const R& rhs) {
    using Return = typename __ifloordiv__<L, R>::type;
    static_assert(
        std::same_as<Return, L&>,
        "In-place floor division operator must return a mutable reference to the "
        "left operand.  Check your specialization of __ifloordiv__ for these "
        "types and ensure the Return type is set to the left operand."
    );
    if constexpr (impl::has_call_operator<__ifloordiv__<L, R>>) {
        __ifloordiv__<L, R>{}(lhs, rhs);
    } else {
        PyObject* result = PyNumber_InPlaceFloorDivide(
            ptr(lhs),
            ptr(as_object(rhs))
        );
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == ptr(lhs)) {
            Py_DECREF(result);
        } else {
            lhs = reinterpret_steal<Return>(result);
        }
    }
    return lhs;
}


template <std::derived_from<Object> Self> requires (!__invert__<Self>::enable)
auto operator~(const Self& self) = delete;
template <std::derived_from<Object> Self> requires (__invert__<Self>::enable)
auto operator~(const Self& self) {
    using Return = typename __invert__<Self>::type;
    static_assert(
        std::derived_from<Return, Object>,
        "Bitwise NOT operator must return a py::Object subclass.  Check your "
        "specialization of __invert__ for this type and ensure the Return type "
        "is set to a py::Object subclass."
    );
    if constexpr (impl::has_call_operator<__invert__<Self>>) {
        return __invert__<Self>{}(self);

    } else if constexpr (impl::cpp_or_originates_from_cpp<Self>) {
        static_assert(
            impl::has_invert<impl::cpp_type<Self>>,
            "__invert__<T> is enabled for a type whose C++ representation does not "
            "have a viable overload for `~T`"
        );
        return ~unwrap(self);

    } else {
        PyObject* result = PyNumber_Invert(ptr(self));
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Return>(result);
    }
}


template <std::derived_from<Object> Self> requires (!__pos__<Self>::enable)
auto operator+(const Self& self) = delete;
template <std::derived_from<Object> Self> requires (__pos__<Self>::enable)
auto operator+(const Self& self) {
    using Return = typename __pos__<Self>::type;
    static_assert(
        std::derived_from<Return, Object>,
        "Unary positive operator must return a py::Object subclass.  Check "
        "your specialization of __pos__ for this type and ensure the Return "
        "type is set to a py::Object subclass."
    );
    if constexpr (impl::has_call_operator<__pos__<Self>>) {
        return __pos__<Self>{}(self);

    } else if constexpr (impl::cpp_or_originates_from_cpp<Self>) {
        static_assert(
            impl::has_pos<impl::cpp_type<Self>>,
            "__pos__<T> is enabled for a type whose C++ representation does not "
            "have a viable overload for `+T`"
        );
        return +unwrap(self);

    } else {
        PyObject* result = PyNumber_Positive(ptr(self));
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Return>(result);
    }
}


template <std::derived_from<Object> Self> requires (!__neg__<Self>::enable)
auto operator-(const Self& self) = delete;
template <std::derived_from<Object> Self> requires (__neg__<Self>::enable)
auto operator-(const Self& self) {
    using Return = typename __neg__<Self>::type;
    static_assert(
        std::derived_from<Return, Object>,
        "Unary negative operator must return a py::Object subclass.  Check "
        "your specialization of __neg__ for this type and ensure the Return "
        "type is set to a py::Object subclass."
    );
    if constexpr (impl::has_call_operator<__neg__<Self>>) {
        return __neg__<Self>{}(self);

    } else if constexpr (impl::cpp_or_originates_from_cpp<Self>) {
        static_assert(
            impl::has_neg<impl::cpp_type<Self>>,
            "__neg__<T> is enabled for a type whose C++ representation does not "
            "have a viable overload for `-T`"
        );
        return -unwrap(self);

    } else {
        PyObject* result = PyNumber_Negative(ptr(self));
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Return>(result);
    }
}


template <std::derived_from<Object> Self> requires (!__increment__<Self>::enable)
Self& operator++(Self& self) = delete;
template <std::derived_from<Object> Self> requires (__increment__<Self>::enable)
Self& operator++(Self& self) {
    static_assert(
        impl::python_like<Self>,
        "Increment operator requires a Python object."
    );
    using Return = typename __increment__<Self>::type;
    static_assert(
        std::same_as<Return, Self>,
        "Increment operator must return a reference to the derived type.  "
        "Check your specialization of __increment__ for this type and ensure "
        "the Return type is set to the derived type."
    );
    if constexpr (impl::has_call_operator<__increment__<Self>>) {
        __increment__<Self>{}(self);

    } else if constexpr (impl::cpp_or_originates_from_cpp<Self>) {
        static_assert(
            impl::has_preincrement<impl::cpp_type<Self>>,
            "__increment__<T> is enabled for a type whose C++ representation does not "
            "have a viable overload for `++T`"
        );
        return ++unwrap(self);

    } else {
        PyObject* result = PyNumber_InPlaceAdd(ptr(self), impl::one);
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == ptr(self)) {
            Py_DECREF(result);
        } else {
            self = reinterpret_steal<Return>(result);
        }
    }
    return self;
}


template <std::derived_from<Object> Self> requires (!__increment__<Self>::enable)
Self operator++(Self& self, int) = delete;
template <std::derived_from<Object> Self> requires (__increment__<Self>::enable)
Self operator++(Self& self, int) {
    static_assert(
        impl::python_like<Self>,
        "Increment operator requires a Python object."
    );
    using Return = typename __increment__<Self>::type;
    static_assert(
        std::same_as<Return, Self>,
        "Increment operator must return a reference to the derived type.  "
        "Check your specialization of __increment__ for this type and ensure "
        "the Return type is set to the derived type."
    );
    Self copy = self;
    if constexpr (impl::has_call_operator<__increment__<Self>>) {
        __increment__<Self>{}(self);

    } else if constexpr (impl::cpp_or_originates_from_cpp<Self>) {
        static_assert(
            impl::has_postincrement<impl::cpp_type<Self>>,
            "__increment__<T> is enabled for a type whose C++ representation does not "
            "have a viable overload for `T++`"
        );
        return unwrap(self)++;

    } else {
        PyObject* result = PyNumber_InPlaceAdd(ptr(self), impl::one);
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == ptr(self)) {
            Py_DECREF(result);
        } else {
            self = reinterpret_steal<Return>(result);
        }
    }
    return copy;
}


template <std::derived_from<Object> Self> requires (!__decrement__<Self>::enable)
Self& operator--(Self& self) = delete;
template <std::derived_from<Object> Self> requires (__decrement__<Self>::enable)
Self& operator--(Self& self) {
    static_assert(
        impl::python_like<Self>,
        "Decrement operator requires a Python object."
    );
    using Return = typename __decrement__<Self>::type;
    static_assert(
        std::same_as<Return, Self>,
        "Decrement operator must return a reference to the derived type.  "
        "Check your specialization of __decrement__ for this type and ensure "
        "the Return type is set to the derived type."
    );
    if constexpr (impl::has_call_operator<__decrement__<Self>>) {
        __decrement__<Self>{}(self);

    } else if constexpr (impl::cpp_or_originates_from_cpp<Self>) {
        static_assert(
            impl::has_predecrement<impl::cpp_type<Self>>,
            "__decrement__<T> is enabled for a type whose C++ representation does not "
            "have a viable overload for `--T`"
        );
        return --unwrap(self);

    } else {
        PyObject* result = PyNumber_InPlaceSubtract(ptr(self), impl::one);
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == ptr(self)) {
            Py_DECREF(result);
        } else {
            self = reinterpret_steal<Return>(result);
        }
    }
    return self;
}


template <std::derived_from<Object> Self> requires (!__decrement__<Self>::enable)
Self operator--(Self& self, int) = delete;
template <std::derived_from<Object> Self> requires (__decrement__<Self>::enable)
Self operator--(Self& self, int) {
    static_assert(
        impl::python_like<Self>,
        "Decrement operator requires a Python object."
    );
    using Return = typename __decrement__<Self>::type;
    static_assert(
        std::same_as<Return, Self>,
        "Decrement operator must return a reference to the derived type.  "
        "Check your specialization of __decrement__ for this type and ensure "
        "the Return type is set to the derived type."
    );
    Self copy = self;
    if constexpr (impl::has_call_operator<__decrement__<Self>>) {
        __decrement__<Self>{}(self);

    } else if constexpr (impl::cpp_or_originates_from_cpp<Self>) {
        static_assert(
            impl::has_postdecrement<impl::cpp_type<Self>>,
            "__decrement__<T> is enabled for a type whose C++ representation does not "
            "have a viable overload for `T--`"
        );
        return unwrap(self)--;

    } else {
        PyObject* result = PyNumber_InPlaceSubtract(ptr(self), impl::one);
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == ptr(self)) {
            Py_DECREF(result);
        } else {
            self = reinterpret_steal<Return>(result);
        }
    }
    return copy;
}


template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && !__lt__<L, R>::enable)
auto operator<(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && __lt__<L, R>::enable)
auto operator<(const L& lhs, const R& rhs) {
    using Return = typename __lt__<L, R>::type;
    static_assert(
        std::same_as<Return, bool>,
        "Less-than operator must return a boolean value.  Check your "
        "specialization of __lt__ for these types and ensure the Return type "
        "is set to bool."
    );
    if constexpr (impl::has_call_operator<__lt__<L, R>>) {
        return __lt__<L, R>{}(lhs, rhs);

    } else if constexpr (
        impl::cpp_or_originates_from_cpp<L> &&
        impl::cpp_or_originates_from_cpp<R>
    ) {
        static_assert(
            impl::has_lt<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__lt__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L < R`"
        );
        return unwrap(lhs) < unwrap(rhs);

    } else {
        int result = PyObject_RichCompareBool(
            ptr(as_object(lhs)),
            ptr(as_object(rhs)),
            Py_LT
        );
        if (result == -1) {
            Exception::from_python();
        }
        return result;
    }
}


template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && !__le__<L, R>::enable)
auto operator<=(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && __le__<L, R>::enable)
auto operator<=(const L& lhs, const R& rhs) {
    using Return = typename __le__<L, R>::type;
    static_assert(
        std::same_as<Return, bool>,
        "Less-than-or-equal operator must return a boolean value.  Check your "
        "specialization of __le__ for this type and ensure the Return type is "
        "set to bool."
    );
    if constexpr (impl::has_call_operator<__le__<L, R>>) {
        return __le__<L, R>{}(lhs, rhs);

    } else if constexpr (
        impl::cpp_or_originates_from_cpp<L> &&
        impl::cpp_or_originates_from_cpp<R>
    ) {
        static_assert(
            impl::has_le<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__le__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L <= R`"
        );
        return unwrap(lhs) <= unwrap(rhs);

    } else {
        int result = PyObject_RichCompareBool(
            ptr(as_object(lhs)),
            ptr(as_object(rhs)),
            Py_LE
        );
        if (result == -1) {
            Exception::from_python();
        }
        return result;
    }
}


template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && !__eq__<L, R>::enable)
auto operator==(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && __eq__<L, R>::enable)
auto operator==(const L& lhs, const R& rhs) {
    using Return = typename __eq__<L, R>::type;
    static_assert(
        std::same_as<Return, bool>,
        "Equality operator must return a boolean value.  Check your "
        "specialization of __eq__ for this type and ensure the Return type is "
        "set to bool."
    );
    if constexpr (impl::has_call_operator<__eq__<L, R>>) {
        return __eq__<L, R>{}(lhs, rhs);

    } else if constexpr (
        impl::cpp_or_originates_from_cpp<L> &&
        impl::cpp_or_originates_from_cpp<R>
    ) {
        static_assert(
            impl::has_eq<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__eq__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L == R`"
        );
        return unwrap(lhs) == unwrap(rhs);

    } else {
        int result = PyObject_RichCompareBool(
            ptr(as_object(lhs)),
            ptr(as_object(rhs)),
            Py_EQ
        );
        if (result == -1) {
            Exception::from_python();
        }
        return result;
    }
}


template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && !__ne__<L, R>::enable)
auto operator!=(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && __ne__<L, R>::enable)
auto operator!=(const L& lhs, const R& rhs) {
    using Return = typename __ne__<L, R>::type;
    static_assert(
        std::same_as<Return, bool>,
        "Inequality operator must return a boolean value.  Check your "
        "specialization of __ne__ for this type and ensure the Return type is "
        "set to bool."
    );
    if constexpr (impl::has_call_operator<__ne__<L, R>>) {
        return __ne__<L, R>{}(lhs, rhs);

    } else if constexpr (
        impl::cpp_or_originates_from_cpp<L> &&
        impl::cpp_or_originates_from_cpp<R>
    ) {
        static_assert(
            impl::has_ne<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__ne__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L != R`"
        );
        return unwrap(lhs) != unwrap(rhs);

    } else {
        int result = PyObject_RichCompareBool(
            ptr(as_object(lhs)),
            ptr(as_object(rhs)),
            Py_NE
        );
        if (result == -1) {
            Exception::from_python();
        }
        return result;
    }
}


template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && !__ge__<L, R>::enable)
auto operator>=(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && __ge__<L, R>::enable)
auto operator>=(const L& lhs, const R& rhs) {
    using Return = typename __ge__<L, R>::type;
    static_assert(
        std::same_as<Return, bool>,
        "Greater-than-or-equal operator must return a boolean value.  Check "
        "your specialization of __ge__ for this type and ensure the Return "
        "type is set to bool."
    );
    if constexpr (impl::has_call_operator<__ge__<L, R>>) {
        return __ge__<L, R>{}(lhs, rhs);

    } else if constexpr (
        impl::cpp_or_originates_from_cpp<L> &&
        impl::cpp_or_originates_from_cpp<R>
    ) {
        static_assert(
            impl::has_ge<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__ge__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L >= R`"
        );
        return unwrap(lhs) >= unwrap(rhs);

    } else {
        int result = PyObject_RichCompareBool(
            ptr(as_object(lhs)),
            ptr(as_object(rhs)),
            Py_GE
        );
        if (result == -1) {
            Exception::from_python();
        }
        return result;
    }
}


template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && !__gt__<L, R>::enable)
auto operator>(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && __gt__<L, R>::enable)
auto operator>(const L& lhs, const R& rhs) {
    using Return = typename __gt__<L, R>::type;
    static_assert(
        std::same_as<Return, bool>,
        "Greater-than operator must return a boolean value.  Check your "
        "specialization of __gt__ for this type and ensure the Return type is "
        "set to bool."
    );
    if constexpr (impl::has_call_operator<__gt__<L, R>>) {
        return __gt__<L, R>{}(lhs, rhs);

    } else if constexpr (
        impl::cpp_or_originates_from_cpp<L> &&
        impl::cpp_or_originates_from_cpp<R>
    ) {
        static_assert(
            impl::has_gt<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__gt__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L > R`"
        );
        return unwrap(lhs) > unwrap(rhs);

    } else {
        int result = PyObject_RichCompareBool(
            ptr(as_object(lhs)),
            ptr(as_object(rhs)),
            Py_GT
        );
        if (result == -1) {
            Exception::from_python();
        }
        return result;
    }
}


template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && !__add__<L, R>::enable)
auto operator+(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && __add__<L, R>::enable)
auto operator+(const L& lhs, const R& rhs) {
    using Return = typename __add__<L, R>::type;
    static_assert(
        std::derived_from<Return, Object>,
        "Addition operator must return a py::Object subclass.  Check your "
        "specialization of __add__ for this type and ensure the Return type is "
        "derived from py::Object."
    );
    if constexpr (impl::has_call_operator<__add__<L, R>>) {
        return __add__<L, R>{}(lhs, rhs);

    } else if constexpr (
        impl::cpp_or_originates_from_cpp<L> &&
        impl::cpp_or_originates_from_cpp<R>
    ) {
        static_assert(
            impl::has_add<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__add__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L + R`"
        );
        return unwrap(lhs) + unwrap(rhs);

    } else {
        PyObject* result = PyNumber_Add(
            ptr(as_object(lhs)),
            ptr(as_object(rhs))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Return>(result);
    }
}


template <std::derived_from<Object> L, typename R> requires (!__iadd__<L, R>::enable)
L& operator+=(L& lhs, const R& rhs) = delete;
template <std::derived_from<Object> L, typename R> requires (__iadd__<L, R>::enable)
L& operator+=(L& lhs, const R& rhs) {
    static_assert(
        impl::python_like<L>,
        "In-place addition operator requires a Python object."
    );
    using Return = typename __iadd__<L, R>::type;
    static_assert(
        std::same_as<Return, L&>,
        "In-place addition operator must return a mutable reference to the left "
        "operand.  Check your specialization of __iadd__ for these types and "
        "ensure the Return type is set to the left operand."
    );
    if constexpr (impl::has_call_operator<__iadd__<L, R>>) {
        __iadd__<L, R>{}(lhs, rhs);

    } else if constexpr (
        impl::cpp_or_originates_from_cpp<L> &&
        impl::cpp_or_originates_from_cpp<R>
    ) {
        static_assert(
            impl::has_iadd<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__iadd__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L += R`"
        );
        unwrap(lhs) += unwrap(rhs);

    } else {
        PyObject* result = PyNumber_InPlaceAdd(
            ptr(lhs),
            ptr(as_object(rhs))
        );
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == ptr(lhs)) {
            Py_DECREF(result);
        } else {
            lhs = reinterpret_steal<Return>(result);
        }
    }
    return lhs;
}


template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && !__sub__<L, R>::enable)
auto operator-(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && __sub__<L, R>::enable)
auto operator-(const L& lhs, const R& rhs) {
    using Return = typename __sub__<L, R>::type;
    static_assert(
        std::derived_from<Return, Object>,
        "Subtraction operator must return a py::Object subclass.  Check your "
        "specialization of __sub__ for this type and ensure the Return type is "
        "derived from py::Object."
    );
    if constexpr (impl::has_call_operator<__sub__<L, R>>) {
        return __sub__<L, R>{}(lhs, rhs);

    } else if constexpr (
        impl::cpp_or_originates_from_cpp<L> &&
        impl::cpp_or_originates_from_cpp<R>
    ) {
        static_assert(
            impl::has_sub<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__sub__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L - R`"
        );
        return unwrap(lhs) - unwrap(rhs);

    } else {
        PyObject* result = PyNumber_Subtract(
            ptr(as_object(lhs)),
            ptr(as_object(rhs))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Return>(result);
    }
}


template <std::derived_from<Object> L, typename R> requires (!__isub__<L, R>::enable)
L& operator-=(L& lhs, const R& rhs) = delete;
template <std::derived_from<Object> L, typename R> requires (__isub__<L, R>::enable)
L& operator-=(L& lhs, const R& rhs) {
    static_assert(
        impl::python_like<L>,
        "In-place addition operator requires a Python object."
    );
    using Return = typename __isub__<L, R>::type;
    static_assert(
        std::same_as<Return, L&>,
        "In-place addition operator must return a mutable reference to the left "
        "operand.  Check your specialization of __isub__ for these types and "
        "ensure the Return type is set to the left operand."
    );
    if constexpr (impl::has_call_operator<__isub__<L, R>>) {
        __isub__<L, R>{}(lhs, rhs);

    } else if constexpr (
        impl::cpp_or_originates_from_cpp<L> &&
        impl::cpp_or_originates_from_cpp<R>
    ) {
        static_assert(
            impl::has_isub<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__isub__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L -= R`"
        );
        unwrap(lhs) -= unwrap(rhs);

    } else {
        PyObject* result = PyNumber_InPlaceSubtract(
            ptr(lhs),
            ptr(as_object(rhs))
        );
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == ptr(lhs)) {
            Py_DECREF(result);
        } else {
            lhs = reinterpret_steal<Return>(result);
        }
    }
    return lhs;
}


template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && !__mul__<L, R>::enable)
auto operator*(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && __mul__<L, R>::enable)
auto operator*(const L& lhs, const R& rhs) {
    using Return = typename __mul__<L, R>::type;
    static_assert(
        std::derived_from<Return, Object>,
        "Multiplication operator must return a py::Object subclass.  Check "
        "your specialization of __mul__ for this type and ensure the Return "
        "type is derived from py::Object."
    );
    if constexpr (impl::has_call_operator<__mul__<L, R>>) {
        return __mul__<L, R>{}(lhs, rhs);

    } else if constexpr (
        impl::cpp_or_originates_from_cpp<L> &&
        impl::cpp_or_originates_from_cpp<R>
    ) {
        static_assert(
            impl::has_mul<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__mul__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L * R`"
        );
        return unwrap(lhs) * unwrap(rhs);

    } else {
        PyObject* result = PyNumber_Multiply(
            ptr(as_object(lhs)),
            ptr(as_object(rhs))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Return>(result);
    }
}


template <std::derived_from<Object> L, typename R> requires (!__imul__<L, R>::enable)
L& operator*=(L& lhs, const R& rhs) = delete;
template <std::derived_from<Object> L, typename R> requires (__imul__<L, R>::enable)
L& operator*=(L& lhs, const R& rhs) {
    static_assert(
        impl::python_like<L>,
        "In-place multiplication operator requires a Python object."
    );
    using Return = typename __imul__<L, R>::type;
    static_assert(
        std::same_as<Return, L&>,
        "In-place multiplication operator must return a mutable reference to the "
        "left operand.  Check your specialization of __imul__ for these types "
        "and ensure the Return type is set to the left operand."
    );
    if constexpr (impl::has_call_operator<__imul__<L, R>>) {
        __imul__<L, R>{}(lhs, rhs);

    } else if constexpr (
        impl::cpp_or_originates_from_cpp<L> &&
        impl::cpp_or_originates_from_cpp<R>
    ) {
        static_assert(
            impl::has_imul<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__imul__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L *= R`"
        );
        unwrap(lhs) *= unwrap(rhs);

    } else {
        PyObject* result = PyNumber_InPlaceMultiply(
            ptr(lhs),
            ptr(as_object(rhs))
        );
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == ptr(lhs)) {
            Py_DECREF(result);
        } else {
            lhs = reinterpret_steal<Return>(result);
        }
    }
    return lhs;
}


template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && !__truediv__<L, R>::enable)
auto operator/(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && __truediv__<L, R>::enable)
auto operator/(const L& lhs, const R& rhs) {
    using Return = typename __truediv__<L, R>::type;
    static_assert(
        std::derived_from<Return, Object>,
        "True division operator must return a py::Object subclass.  Check "
        "your specialization of __truediv__ for this type and ensure the "
        "Return type is derived from py::Object."
    );
    if constexpr (impl::has_call_operator<__truediv__<L, R>>) {
        return __truediv__<L, R>{}(lhs, rhs);

    } else if constexpr (
        impl::cpp_or_originates_from_cpp<L> &&
        impl::cpp_or_originates_from_cpp<R>
    ) {
        static_assert(
            impl::has_truediv<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__truediv__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L / R`"
        );
        return unwrap(lhs) / unwrap(rhs);

    } else {
        PyObject* result = PyNumber_TrueDivide(
            ptr(as_object(lhs)),
            ptr(as_object(rhs))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Return>(result);
    }
}


template <std::derived_from<Object> L, typename R> requires (!__itruediv__<L, R>::enable)
L& operator/=(L& lhs, const R& rhs) = delete;
template <std::derived_from<Object> L, typename R> requires (__itruediv__<L, R>::enable)
L& operator/=(L& lhs, const R& rhs) {
    static_assert(
        impl::python_like<L>,
        "In-place true division operator requires a Python object."
    );
    using Return = typename __itruediv__<L, R>::type;
    static_assert(
        std::same_as<Return, L&>,
        "In-place true division operator must return a mutable reference to the "
        "left operand.  Check your specialization of __itruediv__ for these "
        "types and ensure the Return type is set to the left operand."
    );
    if constexpr (impl::has_call_operator<__itruediv__<L, R>>) {
        __itruediv__<L, R>{}(lhs, rhs);

    } else if constexpr (
        impl::cpp_or_originates_from_cpp<L> &&
        impl::cpp_or_originates_from_cpp<R>
    ) {
        static_assert(
            impl::has_itruediv<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__itruediv__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L /= R`"
        );
        unwrap(lhs) /= unwrap(rhs);

    } else {
        PyObject* result = PyNumber_InPlaceTrueDivide(
            ptr(lhs),
            ptr(as_object(rhs))
        );
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == ptr(lhs)) {
            Py_DECREF(result);
        } else {
            lhs = reinterpret_steal<Return>(result);
        }
    }
    return lhs;
}


template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && !__mod__<L, R>::enable)
auto operator%(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && __mod__<L, R>::enable)
auto operator%(const L& lhs, const R& rhs) {
    using Return = typename __mod__<L, R>::type;
    static_assert(
        std::derived_from<Return, Object>,
        "Modulus operator must return a py::Object subclass.  Check your "
        "specialization of __mod__ for this type and ensure the Return type "
        "is derived from py::Object."
    );
    if constexpr (impl::has_call_operator<__mod__<L, R>>) {
        return __mod__<L, R>{}(lhs, rhs);

    } else if constexpr (
        impl::cpp_or_originates_from_cpp<L> &&
        impl::cpp_or_originates_from_cpp<R>
    ) {
        static_assert(
            impl::has_mod<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__mod__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L % R`"
        );
        return unwrap(lhs) % unwrap(rhs);

    } else {
        PyObject* result = PyNumber_Remainder(
            ptr(as_object(lhs)),
            ptr(as_object(rhs))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Return>(result);
    }
}


template <std::derived_from<Object> L, typename R> requires (!__imod__<L, R>::enable)
L& operator%=(L& lhs, const R& rhs) = delete;
template <std::derived_from<Object> L, typename R> requires (__imod__<L, R>::enable)
L& operator%=(L& lhs, const R& rhs) {
    static_assert(
        impl::python_like<L>,
        "In-place modulus operator requires a Python object."
    );
    using Return = typename __imod__<L, R>::type;
    static_assert(
        std::same_as<Return, L&>,
        "In-place modulus operator must return a mutable reference to the left "
        "operand.  Check your specialization of __imod__ for these types and "
        "ensure the Return type is set to the left operand."
    );
    if constexpr (impl::has_call_operator<__imod__<L, R>>) {
        __imod__<L, R>{}(lhs, rhs);

    } else if constexpr (
        impl::cpp_or_originates_from_cpp<L> &&
        impl::cpp_or_originates_from_cpp<R>
    ) {
        static_assert(
            impl::has_imod<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__imod__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L %= R`"
        );
        unwrap(lhs) %= unwrap(rhs);

    } else {
        PyObject* result = PyNumber_InPlaceRemainder(
            ptr(lhs),
            ptr(as_object(rhs))
        );
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == ptr(lhs)) {
            Py_DECREF(result);
        } else {
            lhs = reinterpret_steal<Return>(result);
        }
    }
    return lhs;
}


template <typename L, typename R>
    requires (
        impl::any_are_python_like<L, R> &&
        !std::derived_from<L, std::ostream> &&
        !__lshift__<L, R>::enable
    )
auto operator<<(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (
        impl::any_are_python_like<L, R> &&
        !std::derived_from<L, std::ostream> &&
        __lshift__<L, R>::enable
    )
auto operator<<(const L& lhs, const R& rhs) {
    using Return = typename __lshift__<L, R>::type;
    static_assert(
        std::derived_from<Return, Object>,
        "Left shift operator must return a py::Object subclass.  Check your "
        "specialization of __lshift__ for this type and ensure the Return "
        "type is derived from py::Object."
    );
    if constexpr (impl::has_call_operator<__lshift__<L, R>>) {
        return __lshift__<L, R>{}(lhs, rhs);

    } else if constexpr (
        impl::cpp_or_originates_from_cpp<L> &&
        impl::cpp_or_originates_from_cpp<R>
    ) {
        static_assert(
            impl::has_lshift<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__lshift__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L << R`"
        );
        return unwrap(lhs) << unwrap(rhs);

    } else {
        PyObject* result = PyNumber_Lshift(
            ptr(as_object(lhs)),
            ptr(as_object(rhs))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Return>(result);
    }
}


template <std::derived_from<std::ostream> L, std::derived_from<Object> R>
L& operator<<(L& os, const R& obj) {
    PyObject* repr = PyObject_Repr(ptr(obj));
    if (repr == nullptr) {
        Exception::from_python();
    }
    Py_ssize_t size;
    const char* data = PyUnicode_AsUTF8AndSize(repr, &size);
    if (data == nullptr) {
        Py_DECREF(repr);
        Exception::from_python();
    }
    os.write(data, size);
    Py_DECREF(repr);
    return os;
}


template <std::derived_from<Object> L, typename R> requires (!__ilshift__<L, R>::enable)
L& operator<<=(L& lhs, const R& rhs) = delete;
template <std::derived_from<Object> L, typename R> requires (__ilshift__<L, R>::enable)
L& operator<<=(L& lhs, const R& rhs) {
    static_assert(
        impl::python_like<L>,
        "In-place left shift operator requires a Python object."
    );
    using Return = typename __ilshift__<L, R>::type;
    static_assert(
        std::same_as<Return, L&>,
        "In-place left shift operator must return a mutable reference to the left "
        "operand.  Check your specialization of __ilshift__ for these types "
        "and ensure the Return type is set to the left operand."
    );
    if constexpr (impl::has_call_operator<__ilshift__<L, R>>) {
        __ilshift__<L, R>{}(lhs, rhs);

    } else if constexpr (
        impl::cpp_or_originates_from_cpp<L> &&
        impl::cpp_or_originates_from_cpp<R>
    ) {
        static_assert(
            impl::has_ilshift<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__ilshift__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L <<= R`"
        );
        unwrap(lhs) <<= unwrap(rhs);

    } else {
        PyObject* result = PyNumber_InPlaceLshift(
            ptr(lhs),
            ptr(as_object(rhs))
        );
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == ptr(lhs)) {
            Py_DECREF(result);
        } else {
            lhs = reinterpret_steal<Return>(result);
        }
    }
    return lhs;
}


template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && !__rshift__<L, R>::enable)
auto operator>>(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && __rshift__<L, R>::enable)
auto operator>>(const L& lhs, const R& rhs) {
    using Return = typename __rshift__<L, R>::type;
    static_assert(
        std::derived_from<Return, Object>,
        "Right shift operator must return a py::Object subclass.  Check your "
        "specialization of __rshift__ for this type and ensure the Return "
        "type is derived from py::Object."
    );
    if constexpr (impl::has_call_operator<__rshift__<L, R>>) {
        return __rshift__<L, R>{}(lhs, rhs);

    } else if constexpr (
        impl::cpp_or_originates_from_cpp<L> &&
        impl::cpp_or_originates_from_cpp<R>
    ) {
        static_assert(
            impl::has_rshift<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__rshift__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L >> R`"
        );
        return unwrap(lhs) >> unwrap(rhs);

    } else {
        PyObject* result = PyNumber_Rshift(
            ptr(as_object(lhs)),
            ptr(as_object(rhs))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Return>(result);
    }
}


template <std::derived_from<Object> L, typename R> requires (!__irshift__<L, R>::enable)
L& operator>>=(L& lhs, const R& rhs) = delete;
template <std::derived_from<Object> L, typename R> requires (__irshift__<L, R>::enable)
L& operator>>=(L& lhs, const R& rhs) {
    static_assert(
        impl::python_like<L>,
        "In-place right shift operator requires a Python object."
    );
    using Return = typename __irshift__<L, R>::type;
    static_assert(
        std::same_as<Return, L&>,
        "In-place right shift operator must return a mutable reference to the left "
        "operand.  Check your specialization of __irshift__ for these types "
        "and ensure the Return type is set to the left operand."
    );
    if constexpr (impl::has_call_operator<__irshift__<L, R>>) {
        __irshift__<L, R>{}(lhs, rhs);

    } else if constexpr (
        impl::cpp_or_originates_from_cpp<L> &&
        impl::cpp_or_originates_from_cpp<R>
    ) {
        static_assert(
            impl::has_irshift<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__irshift__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L >>= R`"
        );
        unwrap(lhs) >>= unwrap(rhs);

    } else {
        PyObject* result = PyNumber_InPlaceRshift(
            ptr(lhs),
            ptr(as_object(rhs))
        );
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == ptr(lhs)) {
            Py_DECREF(result);
        } else {
            lhs = reinterpret_steal<Return>(result);
        }
    }
    return lhs;
}


template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && !__and__<L, R>::enable)
auto operator&(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && __and__<L, R>::enable)
auto operator&(const L& lhs, const R& rhs) {
    using Return = typename __and__<L, R>::type;
    static_assert(
        std::derived_from<Return, Object>,
        "Bitwise AND operator must return a py::Object subclass.  Check your "
        "specialization of __and__ for this type and ensure the Return type "
        "is derived from py::Object."
    );
    if constexpr (impl::has_call_operator<__and__<L, R>>) {
        return __and__<L, R>{}(lhs, rhs);

    } else if constexpr (
        impl::cpp_or_originates_from_cpp<L> &&
        impl::cpp_or_originates_from_cpp<R>
    ) {
        static_assert(
            impl::has_and<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__and__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L & R`"
        );
        return unwrap(lhs) & unwrap(rhs);

    } else {
        PyObject* result = PyNumber_And(
            ptr(as_object(lhs)),
            ptr(as_object(rhs))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Return>(result);
    }
}


template <std::derived_from<Object> L, typename R> requires (!__iand__<L, R>::enable)
L& operator&=(L& lhs, const R& rhs) = delete;
template <std::derived_from<Object> L, typename R> requires (__iand__<L, R>::enable)
L& operator&=(L& lhs, const R& rhs) {
    static_assert(
        impl::python_like<L>,
        "In-place bitwise AND operator requires a Python object."
    );
    using Return = typename __iand__<L, R>::type;
    static_assert(
        std::same_as<Return, L&>,
        "In-place bitwise AND operator must return a mutable reference to the left "
        "operand.  Check your specialization of __iand__ for these types and "
        "ensure the Return type is set to the left operand."
    );
    if constexpr (impl::has_call_operator<__iand__<L, R>>) {
        __iand__<L, R>{}(lhs, rhs);

    } else if constexpr (
        impl::cpp_or_originates_from_cpp<L> &&
        impl::cpp_or_originates_from_cpp<R>
    ) {
        static_assert(
            impl::has_iand<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__iand__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L &= R`"
        );
        unwrap(lhs) &= unwrap(rhs);

    } else {
        PyObject* result = PyNumber_InPlaceAnd(
            ptr(lhs),
            ptr(as_object(rhs))
        );
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == ptr(lhs)) {
            Py_DECREF(result);
        } else {
            lhs = reinterpret_steal<Return>(result);
        }
    }
    return lhs;
}


template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && !__or__<L, R>::enable)
auto operator|(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && __or__<L, R>::enable)
auto operator|(const L& lhs, const R& rhs) {
    using Return = typename __or__<L, R>::type;
    static_assert(
        std::derived_from<Return, Object>,
        "Bitwise OR operator must return a py::Object subclass.  Check your "
        "specialization of __or__ for this type and ensure the Return type is "
        "derived from py::Object."
    );
    if constexpr (impl::has_call_operator<__or__<L, R>>) {
        return __or__<L, R>{}(lhs, rhs);

    } else if constexpr (
        impl::cpp_or_originates_from_cpp<L> &&
        impl::cpp_or_originates_from_cpp<R>
    ) {
        static_assert(
            impl::has_or<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__or__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L | R`"
        );
        return unwrap(lhs) | unwrap(rhs);

    } else {
        PyObject* result = PyNumber_Or(
            ptr(as_object(lhs)),
            ptr(as_object(rhs))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Return>(result);
    }
}


template <std::derived_from<Object> L, typename R> requires (!__ior__<L, R>::enable)
L& operator|=(L& lhs, const R& rhs) = delete;
template <std::derived_from<Object> L, typename R> requires (__ior__<L, R>::enable)
L& operator|=(L& lhs, const R& rhs) {
    static_assert(
        impl::python_like<L>,
        "In-place bitwise OR operator requires a Python object."
    );
    using Return = typename __ior__<L, R>::type;
    static_assert(
        std::same_as<Return, L&>,
        "In-place bitwise OR operator must return a mutable reference to the left "
        "operand.  Check your specialization of __ior__ for these types and "
        "ensure the Return type is set to the left operand."
    );
    if constexpr (impl::has_call_operator<__ior__<L, R>>) {
        __ior__<L, R>{}(lhs, rhs);

    } else if constexpr (
        impl::cpp_or_originates_from_cpp<L> &&
        impl::cpp_or_originates_from_cpp<R>
    ) {
        static_assert(
            impl::has_ior<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__ior__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L |= R`"
        );
        unwrap(lhs) |= unwrap(rhs);

    } else {
        PyObject* result = PyNumber_InPlaceOr(
            ptr(lhs),
            ptr(as_object(rhs))
        );
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == ptr(lhs)) {
            Py_DECREF(result);
        } else {
            lhs = reinterpret_steal<Return>(result);
        }
    }
    return lhs;
}


template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && !__xor__<L, R>::enable)
auto operator^(const L& lhs, const R& rhs) = delete;
template <typename L, typename R>
    requires (impl::any_are_python_like<L, R> && __xor__<L, R>::enable)
auto operator^(const L& lhs, const R& rhs) {
    using Return = typename __xor__<L, R>::type;
    static_assert(
        std::derived_from<Return, Object>,
        "Bitwise XOR operator must return a py::Object subclass.  Check your "
        "specialization of __xor__ for this type and ensure the Return type "
        "is derived from py::Object."
    );
    if constexpr (impl::has_call_operator<__xor__<L, R>>) {
        return __xor__<L, R>{}(lhs, rhs);

    } else if constexpr (
        impl::cpp_or_originates_from_cpp<L> &&
        impl::cpp_or_originates_from_cpp<R>
    ) {
        static_assert(
            impl::has_xor<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__xor__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L ^ R`"
        );
        return unwrap(lhs) ^ unwrap(rhs);

    } else {
        PyObject* result = PyNumber_Xor(
            ptr(as_object(lhs)),
            ptr(as_object(rhs))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Return>(result);
    }
}


template <std::derived_from<Object> L, typename R> requires (!__ixor__<L, R>::enable)
L& operator^=(L& lhs, const R& rhs) = delete;
template <std::derived_from<Object> L, typename R> requires (__ixor__<L, R>::enable)
L& operator^=(L& lhs, const R& rhs) {
    static_assert(
        impl::python_like<L>,
        "In-place bitwise XOR operator requires a Python object."
    );
    using Return = typename __ixor__<L, R>::type;
    static_assert(
        std::same_as<Return, L&>,
        "In-place bitwise XOR operator must return a mutable reference to the left "
        "operand.  Check your specialization of __ixor__ for these types and "
        "ensure the Return type is set to the left operand."
    );
    if constexpr (impl::has_call_operator<__ixor__<L, R>>) {
        __ixor__<L, R>{}(lhs, rhs);

    } else if constexpr (
        impl::cpp_or_originates_from_cpp<L> &&
        impl::cpp_or_originates_from_cpp<R>
    ) {
        static_assert(
            impl::has_ixor<impl::cpp_type<L>, impl::cpp_type<R>>,
            "__ixor__<L, R> is enabled for operands whose C++ representations have "
            "no viable overload for `L ^= R`"
        );
        unwrap(lhs) ^= unwrap(rhs);

    } else {
        PyObject* result = PyNumber_InPlaceXor(
            ptr(lhs),
            ptr(as_object(rhs))
        );
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == ptr(lhs)) {
            Py_DECREF(result);
        } else {
            lhs = reinterpret_steal<Return>(result);
        }
    }
    return lhs;
}


}  // namespace py


namespace std {

    template <typename T> requires (py::__hash__<T>::enable)
    struct hash<T> {
        static_assert(
            std::same_as<typename py::__hash__<T>::type, size_t>,
            "std::hash<> must return size_t for compatibility with other C++ types.  "
            "Check your specialization of __hash__ for this type and ensure the "
            "Return type is set to size_t."
        );
        static constexpr size_t operator()(const T& obj) {
            if constexpr (py::impl::has_call_operator<py::__hash__<T>>) {
                return py::__hash__<T>{}(obj);
            } else if constexpr (py::impl::cpp_or_originates_from_cpp<T>) {
                static_assert(
                    py::impl::hashable<py::impl::cpp_type<T>>,
                    "__hash__<T> is enabled for a type whose C++ representation does "
                    "not have a viable overload for `std::hash<T>{}`"
                );
                return std::hash<T>{}(py::unwrap(obj));
            } else {
                Py_ssize_t result = PyObject_Hash(py::ptr(obj));
                if (result == -1 && PyErr_Occurred()) {
                    py::Exception::from_python();
                }
                return static_cast<size_t>(result);
            }
        }
    };

};  // namespace std


#endif
