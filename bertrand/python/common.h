#ifndef BERTRAND_PYTHON_COMMON_H
#define BERTRAND_PYTHON_COMMON_H

#include "common/declarations.h"
#include "common/except.h"
#include "common/ops.h"
#include "common/object.h"
#include "common/iter.h"
#include "common/func.h"
#include "common/module.h"
#include "common/control.h"


namespace py {


////////////////////
////    NONE    ////
////////////////////


template <typename T>
struct __issubclass__<T, NoneType>                          : Returns<bool> {
    static consteval bool operator()() { return impl::none_like<T>; }
};


template <typename T>
struct __isinstance__<T, NoneType>                          : Returns<bool> {
    static constexpr bool operator()(const T& obj) {
        if constexpr (impl::is_object_exact<T>) {
            return Py_IsNone(ptr(obj));
        } else {
            return issubclass<T, NoneType>();
        }
    }
};


/* Represents the type of Python's `None` singleton in C++. */
class NoneType : public Object {
    using Base = Object;

public:

    NoneType(Handle h, borrowed_t t) : Base(h, t) {}
    NoneType(Handle h, stolen_t t) : Base(h, t) {}

    template <typename... Args> requires (implicit_ctor<NoneType>::template enable<Args...>)
    NoneType(Args&&... args) : Base(
        implicit_ctor<NoneType>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args> requires (explicit_ctor<NoneType>::template enable<Args...>)
    explicit NoneType(Args&&... args) : Base(
        explicit_ctor<NoneType>{},
        std::forward<Args>(args)...
    ) {}

};


template <>
struct __init__<NoneType>                                   : Returns<NoneType> {
    static auto operator()() {
        return reinterpret_borrow<NoneType>(Py_None);
    }
};


template <impl::none_like T>
struct __init__<NoneType, T>                                : Returns<NoneType> {
    static NoneType operator()(const T&) { return {}; }
};


//////////////////////////////
////    NOTIMPLEMENTED    ////
//////////////////////////////


template <typename T>
struct __issubclass__<T, NotImplementedType>                : Returns<bool> {
    static consteval bool operator()() { return impl::notimplemented_like<T>; }
};


template <typename T>
struct __isinstance__<T, NotImplementedType>                : Returns<bool> {
    static constexpr bool operator()(const T& obj) {
        if constexpr (impl::is_object_exact<T>) {
            return PyType_IsSubtype(Py_TYPE(ptr(obj)), Py_TYPE(Py_NotImplemented));
        } else {
            return issubclass<T, NotImplementedType>();
        }
    }
};


/* Represents the type of Python's `NotImplemented` singleton in C++. */
class NotImplementedType : public Object {
    using Base = Object;

public:

    NotImplementedType(Handle h, borrowed_t t) : Base(h, t) {}
    NotImplementedType(Handle h, stolen_t t) : Base(h, t) {}

    template <typename... Args>
        requires (implicit_ctor<NotImplementedType>::template enable<Args...>)
    NotImplementedType(Args&&... args) : Base(
        implicit_ctor<NotImplementedType>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args>
        requires (explicit_ctor<NotImplementedType>::template enable<Args...>)
    explicit NotImplementedType(Args&&... args) : Base(
        explicit_ctor<NotImplementedType>{},
        std::forward<Args>(args)...
    ) {}

};


template <>
struct __init__<NotImplementedType>                         : Returns<NotImplementedType> {
    static auto operator()() {
        return reinterpret_borrow<NotImplementedType>(Py_NotImplemented);
    }
};


template <impl::notimplemented_like T>
struct __init__<NotImplementedType, T>                      : Returns<NotImplementedType> {
    static NotImplementedType operator()(const T&) { return {}; }
};


////////////////////////
////    ELLIPSIS    ////
////////////////////////


template <typename T>
struct __issubclass__<T, EllipsisType>                      : Returns<bool> {
    static consteval bool operator()() { return impl::ellipsis_like<T>; }
};


template <typename T>
struct __isinstance__<T, EllipsisType>                      : Returns<bool> {
    static constexpr bool operator()(const T& obj) {
        if constexpr (impl::is_object_exact<T>) {
            return PyType_IsSubtype(Py_TYPE(ptr(obj)), Py_TYPE(Py_Ellipsis));
        } else {
            return issubclass<T, EllipsisType>();
        }
    }
};


/* Represents the type of Python's `Ellipsis` singleton in C++. */
class EllipsisType : public Object {
    using Base = Object;

public:

    EllipsisType(Handle h, borrowed_t t) : Base(h, t) {}
    EllipsisType(Handle h, stolen_t t) : Base(h, t) {}

    template <typename... Args>
        requires (implicit_ctor<EllipsisType>::template enable<Args...>)
    EllipsisType(Args&&... args) : Base(
        implicit_ctor<EllipsisType>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args>
        requires (explicit_ctor<EllipsisType>::template enable<Args...>)
    explicit EllipsisType(Args&&... args) : Base(
        explicit_ctor<EllipsisType>{},
        std::forward<Args>(args)...
    ) {}

};


template <>
struct __init__<EllipsisType>                               : Returns<EllipsisType> {
    static auto operator()() {
        return reinterpret_borrow<EllipsisType>(Py_Ellipsis);
    }
};


template <impl::ellipsis_like T>
struct __init__<EllipsisType, T>                            : Returns<EllipsisType> {
    static EllipsisType operator()(const T&) { return {}; }
};


inline const NoneType None;
inline const EllipsisType Ellipsis;
inline const NotImplementedType NotImplemented;


/////////////////////
////    SLICE    ////
/////////////////////


template <typename T>
struct __issubclass__<T, Slice>                             : Returns<bool> {
    static consteval bool operator()() { return impl::slice_like<T>; }
};


template <typename T>
struct __isinstance__<T, Slice>                             : Returns<bool> {
    static constexpr bool operator()(const T& obj) {
        if constexpr (impl::is_object_exact<T>) {
            return PySlice_Check(ptr(obj));
        } else {
            return issubclass<T, Slice>();
        }
    }
};


namespace impl {

    /* An initializer that explicitly requires an integer or None. */
    struct SliceInitializer {
        Object value;
        template <typename T>
            requires (impl::int_like<T> || impl::none_like<T>)
        SliceInitializer(T&& value) : value(std::forward<T>(value)) {}
    };

}


/* Represents a statically-typed Python `slice` object in C++.  Note that the start,
stop, and step values do not strictly need to be integers. */
class Slice : public Object {
    using Base = Object;

public:

    Slice(Handle h, borrowed_t t) : Base(h, t) {}
    Slice(Handle h, stolen_t t) : Base(h, t) {}

    template <typename... Args> requires (implicit_ctor<Slice>::template enable<Args...>)
    Slice(Args&&... args) : Base(
        implicit_ctor<Slice>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args> requires (explicit_ctor<Slice>::template enable<Args...>)
    explicit Slice(Args&&... args) : Base(
        explicit_ctor<Slice>{},
        std::forward<Args>(args)...
    ) {}

    /* Initializer list constructor.  Unlike the other constructors (which can accept
    any kind of object), this syntax is restricted only to integers, py::None, and
    std::nullopt. */
    Slice(const std::initializer_list<impl::SliceInitializer>& indices) :
        Base(nullptr, stolen_t{})
    {
        if (indices.size() > 3) {
            throw ValueError("slices must be of the form {[start[, stop[, step]]]}");
        }
        size_t i = 0;
        std::array<Object, 3> params {None, None, None};
        for (const impl::SliceInitializer& item : indices) {
            params[i++] = item.value;
        }
        m_ptr = PySlice_New(
            ptr(params[0]),
            ptr(params[1]),
            ptr( params[2])
        );
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /* Get the start object of the slice.  Note that this might not be an integer. */
    __declspec(property(get = _get_start)) Object start;
    [[nodiscard]] Object _get_start() const {
        return getattr<"start">(*this);
    }

    /* Get the stop object of the slice.  Note that this might not be an integer. */
    __declspec(property(get = _get_stop)) Object stop;
    [[nodiscard]] Object _get_stop() const {
        return getattr<"stop">(*this);
    }

    /* Get the step object of the slice.  Note that this might not be an integer. */
    __declspec(property(get = _get_step)) Object step;
    [[nodiscard]] Object _get_step() const {
        return getattr<"step">(*this);
    }

    /* Normalize the indices of this slice against a container of the given length.
    This accounts for negative indices and clips those that are out of bounds.
    Returns a simple data struct with the following fields:

        * (Py_ssize_t) start: the normalized start index
        * (Py_ssize_t) stop: the normalized stop index
        * (Py_ssize_t) step: the normalized step size
        * (Py_ssize_t) length: the number of indices that are included in the slice

    It can be destructured using C++17 structured bindings:

        auto [start, stop, step, length] = slice.indices(size);
    */
    [[nodiscard]] auto indices(size_t size) const {
        struct Indices {
            Py_ssize_t start = 0;
            Py_ssize_t stop = 0;
            Py_ssize_t step = 0;
            Py_ssize_t length = 0;
        };

        Indices result;
        if (PySlice_GetIndicesEx(
            ptr(*this),
            size,
            &result.start,
            &result.stop,
            &result.step,
            &result.length
        )) {
            Exception::from_python();
        }
        return result;
    }

};


template <
    std::convertible_to<Object> Start,
    std::convertible_to<Object> Stop,
    std::convertible_to<Object> Step
>
struct __explicit_init__<Slice, Start, Stop, Step>           : Returns<Slice> {
    static auto operator()(const Object& start, const Object& stop, const Object& step) {
        PyObject* result = PySlice_New(
            ptr(start),
            ptr(stop),
            ptr(step)
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Slice>(result);
    }
};


template <
    std::convertible_to<Object> Start,
    std::convertible_to<Object> Stop
>
struct __explicit_init__<Slice, Start, Stop>                 : Returns<Slice> {
    static auto operator()(const Object& start, const Object& stop) {
        PyObject* result = PySlice_New(
            ptr(start),
            ptr(stop),
            nullptr
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Slice>(result);
    }
};


template <std::convertible_to<Object> Stop>
struct __explicit_init__<Slice, Stop>                        : Returns<Slice> {
    static auto operator()(const Object& stop) {
        PyObject* result = PySlice_New(
            nullptr,
            ptr(stop),
            nullptr
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Slice>(result);
    }
};


template <>
struct __init__<Slice>                                      : Returns<Slice> {
    static auto operator()() {
        PyObject* result = PySlice_New(nullptr, nullptr, nullptr);
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Slice>(result);
    }
};


////////////////////////////////
////    GLOBAL FUNCTIONS    ////
////////////////////////////////


/* Equivalent to Python `print(args...)`. */
template <typename... Args>
    requires (
        Function<void(
            Arg<"args", const Str&>::args,
            Arg<"sep", const Str&>::opt,
            Arg<"end", const Str&>::opt,
            Arg<"file", const Object&>::opt,
            Arg<"flush", const Bool&>::opt
        )>::invocable<Args...>
    )
void print(Args&&... args) {
    static Object func = [] {
        PyObject* builtins = PyEval_GetBuiltins();
        if (builtins == nullptr) {
            Exception::from_python();
        }
        PyObject* func = PyDict_GetItem(builtins, impl::TemplateString<"print">::ptr);
        if (func == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Object>(func);
    }();

    Function<void(
        Arg<"args", const Str&>::args,
        Arg<"sep", const Str&>::opt,
        Arg<"end", const Str&>::opt,
        Arg<"file", const Object&>::opt,
        Arg<"flush", const Bool&>::opt
    )>::invoke_py(func, std::forward<Args>(args)...);
}


////////////////////////////////////
////    FORWARD DECLARATIONS    ////
////////////////////////////////////


// TODO: all of these should probably return wrappers around their respective iterators
// rather than the iterators directly.  All the classes I just wrote in iter.h should
// be used here.

// TODO: in general, ops may return C++ values when I bypass the Python interpreter,
// so the control structures shouldn't be so strict about the return type.  It's only
// necessary when we're wrapping a generic Python iterator, in which case it determines
// the return type.  Otherwise, you get a C++ value back.
// -> It might need to apply to custom iterators as well?  If they implement an
// increment operator, then I might not want to make any assumptions about the
// dereference operator, and I might not insert the curr() field.  In fact I should
// reconsider just making the __iter__ class an iterator in its own right.  It would
// have 2 constructors, both of which take the container to iterate over, and the
// end iterator takes an extra integer argument to disambiguate.


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


template <typename Self> requires (__getitem__<Self, Slice>::enable)
auto Object::operator[](
    this const Self& self,
    const std::initializer_list<impl::SliceInitializer>& slice
) {
    return self[Slice(slice)];
}


/* Fall back to the python-level __init__/__new__ constructors if no other constructor
is available. */
template <std::derived_from<Object> Self, typename... Args>
    requires (
        !__init__<Self, Args...>::enable &&
        !__explicit_init__<Self, Args...>::enable &&
        impl::attr_is_callable_with<Self, "__init__", Args...> ||
        impl::attr_is_callable_with<Self, "__new__", Args...>
    )
struct __explicit_init__<Self, Args...>                     : Returns<Self> {
    static auto operator()(Args&&... args) {
        static_assert(
            impl::attr_is_callable_with<Self, "__init__", Args...> ||
            impl::attr_is_callable_with<Self, "__new__", Args...>,
            "Type must have either an __init__ or __new__ method that is callable "
            "with the given arguments."
        );
        if constexpr (impl::attr_is_callable_with<Self, "__init__", Args...>) {
            return __getattr__<Self, "__init__">::type::template with_return<Self>::invoke_py(
                Type<Self>(),
                std::forward<Args>(args)...
            );
        } else {
            return __getattr__<Self, "__new__">::type::invoke_py(
                Type<Self>(),
                std::forward<Args>(args)...
            );
        }
    }
};


/* Invoke a type's metaclass to dynamically create a new Python type.  This 2-argument
form allows the base type to be specified as the template argument, and restricts the
type to single inheritance. */
template <typename T, typename... Args>
    requires (
        Function<Type<T>(
            py::Arg<"name", const Str&>,
            py::Arg<"dict", const Dict<Str, Object>&>)
        >::template invocable<Args...>
    )
struct __explicit_init__<Type<T>, Args...> {
    static auto operator()(Args&&... args) {
        auto helper = [](
            py::Arg<"name", const Str&> name,
            py::Arg<"dict", const Dict<Str, Object>&> dict
        ) {
            Type<T> self;
            return Function<Type<T>(
                py::Arg<"name", const Str&>,
                py::Arg<"bases", const Tuple<Type<T>>&>,
                py::Arg<"dict", const Dict<Str, Object>&>)
            >::template invoke_py<Type<T>>(
                reinterpret_cast<PyObject*>(Py_TYPE(ptr(self))),
                name.value,
                Tuple<Type<T>>{self},
                dict.value
            );
        };
        return Function<decltype(helper)>::template invoke_cpp(
            std::forward<Args>(args)...
        );
    }
};


/* Invoke the `type` metaclass to dynamically create a new Python type.  This
3-argument form is only available for the root Type<Object> class, and allows a tuple
of bases to be passed to enable multiple inheritance. */
template <typename... Args>
    requires (
        Function<Type<Object>(
            py::Arg<"name", const Str&>,
            py::Arg<"bases", const Tuple<Type<Object>>&>,
            py::Arg<"dict", const Dict<Str, Object>&>)
        >::template invocable<Args...>
    )
struct __explicit_init__<Type<Object>, Args...> {
    static auto operator()(Args&&... args) {
        return Function<Type<Object>(
            py::Arg<"name", const Str&>,
            py::Arg<"bases", const Tuple<Type<Object>>&>,
            py::Arg<"dict", const Dict<Str, Object>&>)
        >::template invoke_py<Type<Object>>(
            reinterpret_cast<PyObject*>(&PyType_Type),
            std::forward<Args>(args)...
        );
    }
};


template <typename Return>
Type<Iterator<Return>> Type<Iterator<Return>>::__python__::__import__() {
    return reinterpret_steal<Type<Iterator<Return>>>(
        release(getattr<"Iterator">(Module<"collections.abc">()))
    );
}


}  // namespace py


#endif
