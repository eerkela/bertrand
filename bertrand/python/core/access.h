#ifndef BERTRAND_CORE_ACCESS_H
#define BERTRAND_CORE_ACCESS_H

#include "declarations.h"
#include "object.h"
#include "except.h"
#include "ops.h"


namespace py {


////////////////////////////////
////    ATTRIBUTES/ITEMS    ////
////////////////////////////////


namespace impl {

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
            "Default attribute access operator must return a subclass of py::Object.  "
            "Check your specialization of __getattr__ for this type and ensure the "
            "Return type derives from py::Object, or define a custom call operator "
            "to override this behavior."
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

            } else if constexpr (has_cpp<Base>) {
                unwrap(*this) = unwrap(std::forward<Value>(value));

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
            "Default index operator must return a subclass of py::Object.  Check your "
            "specialization of __getitem__ for this type and ensure the Return type "
            "derives from py::Object, or define a custom call operator to override "
            "this behavior."
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

            } else if constexpr (impl::has_cpp<Base>) {
                static_assert(
                    impl::supports_item_assignment<Base, std::remove_cvref_t<Value>, Key...>,
                    "__setitem__<Self, Value, Key...> is enabled for operands whose C++ "
                    "representations have no viable overload for `Self[Key...] = Value`"
                );
                unwrap(*this) = unwrap(std::forward<Value>(value));

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


/// TODO: cast/explicit cast should inherit the behavior of the base type?


template <impl::lazily_evaluated From, typename To>
    requires (std::convertible_to<impl::lazy_type<From>, To>)
struct __cast__<From, To>                                   : Returns<To> {
    static To operator()(const From& item) {
        if constexpr (impl::has_cpp<impl::lazy_type<From>>) {
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
        if constexpr (impl::has_cpp<impl::lazy_type<From>>) {
            return static_cast<To>(unwrap(item));
        } else {
            return static_cast<To>(
                reinterpret_steal<impl::lazy_type<From>>(ptr(item))
            );
        }
    }
};


template <typename T, impl::lazily_evaluated Base>
struct __isinstance__<T, Base> : __isinstance__<T, impl::lazy_type<Base>> {};
template <typename T, impl::lazily_evaluated Base>
struct __issubclass__<T, Base> : __issubclass__<T, impl::lazy_type<Base>> {};
template <impl::lazily_evaluated Base, StaticStr Name>
struct __getattr__<Base, Name> : __getattr__<impl::lazy_type<Base>, Name> {};
template <impl::lazily_evaluated Base, StaticStr Name, typename Value>
struct __setattr__<Base, Name, Value> : __setattr__<impl::lazy_type<Base>, Name, Value> {};
template <impl::lazily_evaluated Base, StaticStr Name>
struct __delattr__<Base, Name> : __delattr__<impl::lazy_type<Base>, Name> {};
template <impl::lazily_evaluated Base, typename... Key>
struct __getitem__<Base, Key...> : __getitem__<impl::lazy_type<Base>, Key...> {};
template <impl::lazily_evaluated Base, typename Value, typename... Key>
struct __setitem__<Base, Value, Key...> : __setitem__<impl::lazy_type<Base>, Value, Key...> {};
template <impl::lazily_evaluated Base, typename... Key>
struct __delitem__<Base, Key...> : __delitem__<impl::lazy_type<Base>, Key...> {};
template <impl::lazily_evaluated Base>
struct __len__<Base> : __len__<impl::lazy_type<Base>> {};
template <impl::lazily_evaluated Base>
struct __iter__<Base> : __iter__<impl::lazy_type<Base>> {};
template <impl::lazily_evaluated Base>
struct __reversed__<Base> : __reversed__<impl::lazy_type<Base>> {};
template <impl::lazily_evaluated Base, typename Key>
struct __contains__<Base, Key> : __contains__<impl::lazy_type<Base>, Key> {};
template <impl::lazily_evaluated Base>
struct __hash__<Base> : __hash__<impl::lazy_type<Base>> {};
template <impl::lazily_evaluated Base>
struct __abs__<Base> : __abs__<impl::lazy_type<Base>> {};
template <impl::lazily_evaluated Base>
struct __invert__<Base> : __invert__<impl::lazy_type<Base>> {};
template <impl::lazily_evaluated Base>
struct __pos__<Base> : __pos__<impl::lazy_type<Base>> {};
template <impl::lazily_evaluated Base>
struct __neg__<Base> : __neg__<impl::lazy_type<Base>> {};
template <impl::lazily_evaluated Base>
struct __increment__<Base> : __increment__<impl::lazy_type<Base>> {};
template <impl::lazily_evaluated Base>
struct __decrement__<Base> : __decrement__<impl::lazy_type<Base>> {};
template <impl::lazily_evaluated L, impl::lazily_evaluated R>
struct __lt__<L, R> : __lt__<impl::lazy_type<L>, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, typename R> requires (!impl::lazily_evaluated<R>)
struct __lt__<L, R> : __lt__<impl::lazy_type<L>, R> {};
template <typename L, impl::lazily_evaluated R> requires (!impl::lazily_evaluated<L>)
struct __lt__<L, R> : __lt__<L, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, impl::lazily_evaluated R>
struct __le__<L, R> : __le__<impl::lazy_type<L>, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, typename R> requires (!impl::lazily_evaluated<R>)
struct __le__<L, R> : __le__<impl::lazy_type<L>, R> {};
template <typename L, impl::lazily_evaluated R> requires (!impl::lazily_evaluated<L>)
struct __le__<L, R> : __le__<L, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, impl::lazily_evaluated R>
struct __eq__<L, R> : __eq__<impl::lazy_type<L>, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, typename R> requires (!impl::lazily_evaluated<R>)
struct __eq__<L, R> : __eq__<impl::lazy_type<L>, R> {};
template <typename L, impl::lazily_evaluated R> requires (!impl::lazily_evaluated<L>)
struct __eq__<L, R> : __eq__<L, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, impl::lazily_evaluated R>
struct __ne__<L, R> : __ne__<impl::lazy_type<L>, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, typename R> requires (!impl::lazily_evaluated<R>)
struct __ne__<L, R> : __ne__<impl::lazy_type<L>, R> {};
template <typename L, impl::lazily_evaluated R> requires (!impl::lazily_evaluated<L>)
struct __ne__<L, R> : __ne__<L, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, impl::lazily_evaluated R>
struct __ge__<L, R> : __ge__<impl::lazy_type<L>, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, typename R> requires (!impl::lazily_evaluated<R>)
struct __ge__<L, R> : __ge__<impl::lazy_type<L>, R> {};
template <typename L, impl::lazily_evaluated R> requires (!impl::lazily_evaluated<L>)
struct __ge__<L, R> : __ge__<L, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, impl::lazily_evaluated R>
struct __gt__<L, R> : __gt__<impl::lazy_type<L>, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, typename R> requires (!impl::lazily_evaluated<R>)
struct __gt__<L, R> : __gt__<impl::lazy_type<L>, R> {};
template <typename L, impl::lazily_evaluated R> requires (!impl::lazily_evaluated<L>)
struct __gt__<L, R> : __gt__<L, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, impl::lazily_evaluated R>
struct __add__<L, R> : __add__<impl::lazy_type<L>, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, typename R> requires (!impl::lazily_evaluated<R>)
struct __add__<L, R> : __add__<impl::lazy_type<L>, R> {};
template <typename L, impl::lazily_evaluated R> requires (!impl::lazily_evaluated<L>)
struct __add__<L, R> : __add__<L, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, impl::lazily_evaluated R>
struct __sub__<L, R> : __sub__<impl::lazy_type<L>, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, typename R> requires (!impl::lazily_evaluated<R>)
struct __sub__<L, R> : __sub__<impl::lazy_type<L>, R> {};
template <typename L, impl::lazily_evaluated R> requires (!impl::lazily_evaluated<L>)
struct __sub__<L, R> : __sub__<L, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, impl::lazily_evaluated R>
struct __mul__<L, R> : __mul__<impl::lazy_type<L>, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, typename R> requires (!impl::lazily_evaluated<R>)
struct __mul__<L, R> : __mul__<impl::lazy_type<L>, R> {};
template <typename L, impl::lazily_evaluated R> requires (!impl::lazily_evaluated<L>)
struct __mul__<L, R> : __mul__<L, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, impl::lazily_evaluated R>
struct __truediv__<L, R> : __truediv__<impl::lazy_type<L>, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, typename R> requires (!impl::lazily_evaluated<R>)
struct __truediv__<L, R> : __truediv__<impl::lazy_type<L>, R> {};
template <typename L, impl::lazily_evaluated R> requires (!impl::lazily_evaluated<L>)
struct __truediv__<L, R> : __truediv__<L, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, impl::lazily_evaluated R>
struct __floordiv__<L, R> : __floordiv__<impl::lazy_type<L>, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, typename R> requires (!impl::lazily_evaluated<R>)
struct __floordiv__<L, R> : __floordiv__<impl::lazy_type<L>, R> {};
template <typename L, impl::lazily_evaluated R> requires (!impl::lazily_evaluated<L>)
struct __floordiv__<L, R> : __floordiv__<L, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, impl::lazily_evaluated R>
struct __mod__<L, R> : __mod__<impl::lazy_type<L>, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, typename R> requires (!impl::lazily_evaluated<R>)
struct __mod__<L, R> : __mod__<impl::lazy_type<L>, R> {};
template <typename L, impl::lazily_evaluated R> requires (!impl::lazily_evaluated<L>)
struct __mod__<L, R> : __mod__<L, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, impl::lazily_evaluated R>
struct __pow__<L, R> : __pow__<impl::lazy_type<L>, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, typename R> requires (!impl::lazily_evaluated<R>)
struct __pow__<L, R> : __pow__<impl::lazy_type<L>, R> {};
template <typename L, impl::lazily_evaluated R> requires (!impl::lazily_evaluated<L>)
struct __pow__<L, R> : __pow__<L, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, impl::lazily_evaluated R>
struct __lshift__<L, R> : __lshift__<impl::lazy_type<L>, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, typename R> requires (!impl::lazily_evaluated<R>)
struct __lshift__<L, R> : __lshift__<impl::lazy_type<L>, R> {};
template <typename L, impl::lazily_evaluated R> requires (!impl::lazily_evaluated<L>)
struct __lshift__<L, R> : __lshift__<L, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, impl::lazily_evaluated R>
struct __rshift__<L, R> : __rshift__<impl::lazy_type<L>, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, typename R> requires (!impl::lazily_evaluated<R>)
struct __rshift__<L, R> : __rshift__<impl::lazy_type<L>, R> {};
template <typename L, impl::lazily_evaluated R> requires (!impl::lazily_evaluated<L>)
struct __rshift__<L, R> : __rshift__<L, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, impl::lazily_evaluated R>
struct __and__<L, R> : __and__<impl::lazy_type<L>, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, typename R> requires (!impl::lazily_evaluated<R>)
struct __and__<L, R> : __and__<impl::lazy_type<L>, R> {};
template <typename L, impl::lazily_evaluated R> requires (!impl::lazily_evaluated<L>)
struct __and__<L, R> : __and__<L, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, impl::lazily_evaluated R>
struct __xor__<L, R> : __xor__<impl::lazy_type<L>, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, typename R> requires (!impl::lazily_evaluated<R>)
struct __xor__<L, R> : __xor__<impl::lazy_type<L>, R> {};
template <typename L, impl::lazily_evaluated R> requires (!impl::lazily_evaluated<L>)
struct __xor__<L, R> : __xor__<L, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, impl::lazily_evaluated R>
struct __or__<L, R> : __or__<impl::lazy_type<L>, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, typename R> requires (!impl::lazily_evaluated<R>)
struct __or__<L, R> : __or__<impl::lazy_type<L>, R> {};
template <typename L, impl::lazily_evaluated R> requires (!impl::lazily_evaluated<L>)
struct __or__<L, R> : __or__<L, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, impl::lazily_evaluated R>
struct __iadd__<L, R> : __iadd__<impl::lazy_type<L>, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, typename R> requires (!impl::lazily_evaluated<R>)
struct __iadd__<L, R> : __iadd__<impl::lazy_type<L>, R> {};
template <typename L, impl::lazily_evaluated R> requires (!impl::lazily_evaluated<L>)
struct __iadd__<L, R> : __iadd__<L, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, impl::lazily_evaluated R>
struct __isub__<L, R> : __isub__<impl::lazy_type<L>, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, typename R> requires (!impl::lazily_evaluated<R>)
struct __isub__<L, R> : __isub__<impl::lazy_type<L>, R> {};
template <typename L, impl::lazily_evaluated R> requires (!impl::lazily_evaluated<L>)
struct __isub__<L, R> : __isub__<L, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, impl::lazily_evaluated R>
struct __imul__<L, R> : __imul__<impl::lazy_type<L>, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, typename R> requires (!impl::lazily_evaluated<R>)
struct __imul__<L, R> : __imul__<impl::lazy_type<L>, R> {};
template <typename L, impl::lazily_evaluated R> requires (!impl::lazily_evaluated<L>)
struct __imul__<L, R> : __imul__<L, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, impl::lazily_evaluated R>
struct __itruediv__<L, R> : __itruediv__<impl::lazy_type<L>, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, typename R> requires (!impl::lazily_evaluated<R>)
struct __itruediv__<L, R> : __itruediv__<impl::lazy_type<L>, R> {};
template <typename L, impl::lazily_evaluated R> requires (!impl::lazily_evaluated<L>)
struct __itruediv__<L, R> : __itruediv__<L, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, impl::lazily_evaluated R>
struct __ifloordiv__<L, R> : __ifloordiv__<impl::lazy_type<L>, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, typename R> requires (!impl::lazily_evaluated<R>)
struct __ifloordiv__<L, R> : __ifloordiv__<impl::lazy_type<L>, R> {};
template <typename L, impl::lazily_evaluated R> requires (!impl::lazily_evaluated<L>)
struct __ifloordiv__<L, R> : __ifloordiv__<L, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, impl::lazily_evaluated R>
struct __imod__<L, R> : __imod__<impl::lazy_type<L>, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, typename R> requires (!impl::lazily_evaluated<R>)
struct __imod__<L, R> : __imod__<impl::lazy_type<L>, R> {};
template <typename L, impl::lazily_evaluated R> requires (!impl::lazily_evaluated<L>)
struct __imod__<L, R> : __imod__<L, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, impl::lazily_evaluated R>
struct __ipow__<L, R> : __ipow__<impl::lazy_type<L>, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, typename R> requires (!impl::lazily_evaluated<R>)
struct __ipow__<L, R> : __ipow__<impl::lazy_type<L>, R> {};
template <typename L, impl::lazily_evaluated R> requires (!impl::lazily_evaluated<L>)
struct __ipow__<L, R> : __ipow__<L, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, impl::lazily_evaluated R>
struct __ilshift__<L, R> : __ilshift__<impl::lazy_type<L>, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, typename R> requires (!impl::lazily_evaluated<R>)
struct __ilshift__<L, R> : __ilshift__<impl::lazy_type<L>, R> {};
template <typename L, impl::lazily_evaluated R> requires (!impl::lazily_evaluated<L>)
struct __ilshift__<L, R> : __ilshift__<L, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, impl::lazily_evaluated R>
struct __irshift__<L, R> : __irshift__<impl::lazy_type<L>, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, typename R> requires (!impl::lazily_evaluated<R>)
struct __irshift__<L, R> : __irshift__<impl::lazy_type<L>, R> {};
template <typename L, impl::lazily_evaluated R> requires (!impl::lazily_evaluated<L>)
struct __irshift__<L, R> : __irshift__<L, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, impl::lazily_evaluated R>
struct __iand__<L, R> : __iand__<impl::lazy_type<L>, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, typename R> requires (!impl::lazily_evaluated<R>)
struct __iand__<L, R> : __iand__<impl::lazy_type<L>, R> {};
template <typename L, impl::lazily_evaluated R> requires (!impl::lazily_evaluated<L>)
struct __iand__<L, R> : __iand__<L, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, impl::lazily_evaluated R>
struct __ixor__<L, R> : __ixor__<impl::lazy_type<L>, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, typename R> requires (!impl::lazily_evaluated<R>)
struct __ixor__<L, R> : __ixor__<impl::lazy_type<L>, R> {};
template <typename L, impl::lazily_evaluated R> requires (!impl::lazily_evaluated<L>)
struct __ixor__<L, R> : __ixor__<L, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, impl::lazily_evaluated R>
struct __ior__<L, R> : __ior__<impl::lazy_type<L>, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, typename R> requires (!impl::lazily_evaluated<R>)
struct __ior__<L, R> : __ior__<impl::lazy_type<L>, R> {};
template <typename L, impl::lazily_evaluated R> requires (!impl::lazily_evaluated<L>)
struct __ior__<L, R> : __ior__<L, impl::lazy_type<R>> {};


/////////////////////////
////    ITERATORS    ////
/////////////////////////


/// TODO: when an __iter__ method is registered, it must return a specialization of
/// py::Iterator, whose __export__ method will be invoked to attach the iterator type
/// to the class.  That's how binding iterators across languages will work.


/// TODO: include operator->* for list comprehensions


/// TODO: creating a Python iterator around a C++ object will not accept rvalues.  Only
/// const/non-const lvalue references to iterable containers will be accepted.


template <typename Begin, typename End>
struct Interface<Iterator<Begin, End>> {

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
`T` in an `__iter__<Container> : Returns<T> {};` specialization.  If you want to use
this class and avoid type safety issues, leave the return type set to `Object` (the
default), which will incur a runtime check on conversion. */
template <std::derived_from<Object> Return>
struct Iterator<Return, void> : Object, Interface<Iterator<Return, void>> {
    struct __python__ : def<__python__, Iterator>, PyObject {
        static Type<Iterator> __import__() {
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
            return reinterpret_steal<Type<Iterator>>(iterator);
        }
    };

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


template <std::derived_from<Object> T, typename Return>
struct __isinstance__<T, Iterator<Return>>                  : Returns<bool> {
    static constexpr bool operator()(const T& obj) {
        return PyIter_Check(ptr(obj));
    }
};


template <std::derived_from<Object> T, typename Return>
struct __issubclass__<T, Iterator<Return>>                  : Returns<bool> {
    static consteval bool operator()() {
        return
            std::derived_from<T, impl::IterTag> &&
            std::convertible_to<impl::iter_type<T>, Return>;
    }
};


template <typename T>
struct __iter__<Iterator<T>>                                : Returns<T> {
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

    /// NOTE: Python iterators cannot be copied, due to the inherent state of the
    /// iterator object.
    __iter__(const __iter__&) = delete;
    __iter__& operator=(const __iter__&) = delete;

    __iter__(__iter__&& other) :
        iter(std::move(other.iter)), curr(std::move(other.curr))
    {}

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

    [[nodiscard]] bool operator==(const __iter__& other) const {
        return ptr(curr) == ptr(other.curr);
    }

    [[nodiscard]] bool operator!=(const __iter__& other) const {
        return ptr(curr) != ptr(other.curr);
    }

};
/// NOTE: can't iterate over a const Iterator<T> because the iterator itself must be
/// mutable.


/* A wrapper around a pair of C++ iterators that allows them to be used from Python.

This will instantiate a unique Python type with an appropriate `__next__()` method for
every combination of C++ iterators, forwarding to their respective `operator*()`,
`operator++()`, and `operator==()` methods. */
template <std::input_iterator Begin, std::sentinel_for<Begin> End>
struct Iterator<Begin, End> : Object, Interface<Iterator<Begin, End>> {
    struct __python__ : def<__python__, Iterator>, PyObject {
        Begin begin;
        End end;

        __python__(Begin&& begin, End&& end) :
            begin(std::move(begin)), end(std::move(end))
        {}

        template <StaticStr ModName>
        static Type<Iterator> __export__(Module<ModName> module) {
            static PyType_Slot slots[] = {
                {Py_tp_iter, reinterpret_cast<void*>(PyObject_SelfIter)},
                {Py_tp_iternext, reinterpret_cast<void*>(__next__)},
                {0, nullptr}
            };
            static PyType_Spec spec = {
                .name = typeid(Iterator).name(),
                .basicsize = sizeof(__python__),
                .itemsize = 0,
                .flags = 
                    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HEAPTYPE | Py_TPFLAGS_HAVE_GC |
                    Py_TPFLAGS_IMMUTABLETYPE | Py_TPFLAGS_DISALLOW_INSTANTIATION,
                .slots = slots 
            };
            PyObject* cls = PyType_FromModuleAndSpec(
                ptr(module),
                &spec,
                nullptr
            );
            if (cls == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Type<Iterator>>(cls);
        }

        static PyObject* __next__(__python__* self) {
            try {
                if (self->begin == self->end) {
                    return nullptr;
                }
                if constexpr (std::is_lvalue_reference_v<decltype(*(self->begin))>) {
                    auto result = wrap(*(self->begin));  // non-owning obj
                    ++(self->begin);
                    return reinterpret_cast<PyObject*>(release(result));
                } else {
                    auto result = as_object(*(self->begin));  // owning obj
                    ++(self->begin);
                    return reinterpret_cast<PyObject*>(release(result));
                }
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

    };

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


/// TODO: there should be some consideration as to where the begin()/end() iterators
/// are coming from.  It's possible that for a pure-python type, the begin() and end()
/// methods will return py::Iterator<T, void> objects, in which case I should
/// short-circuit the CTAD logic and just use those objects directly.


/* CTAD guide will generate a Python iterator from an arbitrary C++ container. */
template <impl::iterable Container>
Iterator(Container) -> Iterator<
    decltype(std::ranges::begin(std::declval<Container>())),
    decltype(std::ranges::end(std::declval<Container>()))
>;


/* CTAD guide will infer the iterator types from the arguments. */
template <std::input_iterator Begin, std::sentinel_for<Begin> End>
Iterator(Begin, End) -> Iterator<Begin, End>;


/* Implement the CTAD guide for iterable C++ containers.  The container type may be
const. */
template <impl::iterable Container>
struct __init__<Iterator<
    decltype(std::ranges::begin(std::declval<Container>())),
    decltype(std::ranges::end(std::declval<Container>()))
>, Container> {
    using Iter = Iterator<
        decltype(std::ranges::begin(std::declval<Container>())),
        decltype(std::ranges::end(std::declval<Container>()))
    >;
    static auto operator()(Container& container) {
        return impl::construct<Iter>(
            std::ranges::begin(container),
            std::ranges::end(container)
        );
    }
    static auto operator()(Container&& container) {
        return impl::construct<Iter>(
            std::ranges::begin(container),
            std::ranges::end(container)
        );
    }
};


/* Construct a Python iterator from a pair of C++ iterators. */
template <std::input_iterator Begin, std::sentinel_for<Begin> End>
struct __init__<Iterator<Begin, End>, Begin, End> {
    static auto operator()(auto&& begin, auto&& end) {
        return impl::construct<Iterator<Begin, End>>(
            std::forward<decltype(begin)>(begin),
            std::forward<decltype(end)>(end)
        );
    }
};


/// TODO: replace superfluous int with a tag type held in Returns<T>.  (begin_t, end_t)


/// TODO: it might be best to use a special case in the begin() and end() operators to
/// directly extract the internal C++ iterators from the Python representation.


template <std::input_iterator Begin, std::sentinel_for<Begin> End>
struct __iter__<Iterator<Begin, End>> : Returns<decltype(*std::declval<Begin>())> {
    using iterator_category = std::input_iterator_tag;
    using difference_type   = std::ptrdiff_t;
    using value_type        = std::decay_t<decltype(*std::declval<Begin>())>;
    using pointer           = value_type*;
    using reference         = value_type&;

    Iterator<Begin, End> iter;

    __iter__(const Iterator<Begin, End>& self) : iter(self) {}
    __iter__(Iterator<Begin, End>&& self) : iter(std::move(self)) {}
    __iter__(const Iterator<Begin, End>& self, int) : __iter__(self) {}
    __iter__(Iterator<Begin, End>&& self, int) : __iter__(std::move(self)) {}
    __iter__(const __iter__& other) = default;
    __iter__(__iter__&& other) = default;
    __iter__& operator=(const __iter__& other) = default;
    __iter__& operator=(__iter__&& other) = default;

    [[nodiscard]] decltype(auto) operator*() { return *(ptr(iter)->begin); }
    [[nodiscard]] decltype(auto) operator*() const { return *(ptr(iter)->begin); }
    [[nodiscard]] auto* operator->() { return &(**this); }
    [[nodiscard]] auto* operator->() const { return &(**this); }

    __iter__& operator++() {
        ++(ptr(iter)->begin);
    }

    /// NOTE: post-increment is not supported due to inaccurate copy semantics.

    [[nodiscard]] bool operator==(const __iter__& other) const {
        return ptr(iter)->begin == ptr(other.curr);
    }

    [[nodiscard]] bool operator!=(const __iter__& other) const {
        return ptr(curr) != ptr(other.curr);
    }

};
/// NOTE: can't iterate over a const Iterator<T> because the iterator itself must be
/// mutable.





template <typename T, typename Begin, typename End>
struct __contains__<T, Iterator<Begin, End>> : Returns<bool> {};



/// TODO: include begin()/end() operators here































/// TODO: Iterator<> should be moved to access.h, along with operator->* and views




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
        if constexpr (impl::has_cpp<Self>) {
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
    if constexpr (std::is_constructible_v<__iter__<const Self>, const Self&, int>) {
        static_assert(
            std::is_constructible_v<__iter__<const Self>, const Self&>,
            "__iter__<T> specializes the begin iterator, but not the end iterator.  "
            "Did you forget to define an `__iter__(const T&)` constructor?"
        );
        return __iter__<const Self>(self, 0);
    } else {
        static_assert(
            !std::is_constructible_v<__iter__<const Self>, const Self&>,
            "__iter__<T> specializes the end iterator, but not the begin iterator.  "
            "Did you forget to define an `__iter__(const T&, int)` constructor?"
        );
        if constexpr (impl::has_cpp<Self>) {
            return std::ranges::begin(py::unwrap(self));
        } else {
            using Return = typename __iter__<const Self>::type;
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
        if constexpr (impl::has_cpp<Self>) {
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
    if constexpr (std::is_constructible_v<__iter__<const Self>, const Self&>) {
        static_assert(
            std::is_constructible_v<__iter__<const Self>, const Self&, int>,
            "__iter__<T> specializes the begin iterator, but not the end iterator.  "
            "Did you forget to define an `__iter__(const T&)` constructor?"
        );
        return __iter__<const Self>(self);
    } else {
        static_assert(
            !std::is_constructible_v<__iter__<const Self>, const Self&, int>,
            "__iter__<T> specializes the end iterator, but not the begin iterator.  "
            "Did you forget to define an `__iter__(const T&, int)` constructor?"
        );
        if constexpr (impl::has_cpp<Self>) {
            return std::ranges::end(py::unwrap(self));
        } else {
            using Return = typename __iter__<const Self>::type;
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
        if constexpr (impl::has_cpp<Self>) {
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
    if constexpr (std::is_constructible_v<__reversed__<const Self>, const Self&, int>) {
        static_assert(
            std::is_constructible_v<__reversed__<const Self>, const Self&>,
            "__reversed__<T> specializes the begin iterator, but not the end "
            "iterator.  Did you forget to define a `__reversed__(const T&)` "
            "constructor?"
        );
        return __reversed__<const Self>(self, 0);
    } else {
        static_assert(
            !std::is_constructible_v<__reversed__<const Self>, const Self&>,
            "__reversed__<T> specializes the end iterator, but not the begin "
            "iterator.  Did you forget to define a `__reversed__(const T&, int)` "
            "constructor?"
        );
        if constexpr (impl::has_cpp<Self>) {
            return std::ranges::rbegin(py::unwrap(self));
        } else {
            using Return = typename __reversed__<const Self>::type;
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
        if constexpr (impl::has_cpp<Self>) {
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
    if constexpr (std::is_constructible_v<__reversed__<const Self>, const Self&>) {
        static_assert(
            std::is_constructible_v<__reversed__<const Self>, const Self&, int>,
            "__reversed__<T> specializes the begin iterator, but not the end "
            "iterator.  Did you forget to define a `__reversed__(const T&)` "
            "constructor?"
        );
        return __reversed__<const Self>(self);
    } else {
        static_assert(
            !std::is_constructible_v<__reversed__<const Self>, const Self&, int>,
            "__reversed__<T> specializes the end iterator, but not the begin "
            "iterator.  Did you forget to define a `__reversed__(const T&, int)` "
            "constructor?"
        );
        if constexpr (impl::has_cpp<Self>) {
            return std::ranges::rend(py::unwrap(self));
        } else {
            using Return = typename __reversed__<const Self>::type;
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


}


#endif
