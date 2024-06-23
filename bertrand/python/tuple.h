#if !defined(BERTRAND_PYTHON_INCLUDED) && !defined(LINTER)
#error "This file should not be included directly.  Please include <bertrand/python.h> instead."
#endif

#ifndef BERTRAND_PYTHON_TUPLE_H
#define BERTRAND_PYTHON_TUPLE_H

#include "common.h"


// TODO: support py::deque?


namespace bertrand {
namespace py {


/////////////////////
////    TUPLE    ////
/////////////////////


template <typename T, typename Value>
struct __issubclass__<T, Tuple<Value>>                      : Returns<bool> {
    static constexpr bool generic = std::same_as<Value, Object>;
    template <typename U>
    static constexpr bool check_value_type = std::derived_from<U, Object> ?
        std::derived_from<U, Value> : std::convertible_to<U, Value>;

    template <typename U>
    struct stl_check {
        static constexpr bool match = false;
    };
    template <typename First, typename Second>
    struct stl_check<std::pair<First, Second>> {
        static constexpr bool match = true;
        static constexpr bool value = check_value_type<First> && check_value_type<Second>;
    };
    template <typename... Args>
    struct stl_check<std::tuple<Args...>> {
        static constexpr bool match = true;
        static constexpr bool value = (check_value_type<Args> && ...);
    };

    static consteval bool operator()(const T&) { return operator()(); }
    static consteval bool operator()() {
        if constexpr (!impl::tuple_like<T>) {
            return false;
        } else if constexpr (impl::pybind11_like<T>) {
            return generic;
        } else if constexpr (impl::is_iterable<T>) {
            return check_value_type<impl::iter_type<T>>;
        } else if constexpr (stl_check<T>::match) {
            return stl_check<T>::value;
        } else {
            return false;
        }
    }
};


template <typename T, typename Value>
struct __isinstance__<T, Tuple<Value>>                      : Returns<bool> {
    static constexpr bool generic = std::same_as<Value, Object>;
    template <typename U>
    static constexpr bool check_value_type = std::derived_from<U, Object> ?
        std::derived_from<U, Value> : std::convertible_to<U, Value>;

    static constexpr bool operator()(const T& obj) {
        if constexpr (impl::cpp_like<T>) {
            return issubclass<T, Tuple<Value>>();

        } else if constexpr (impl::is_object_exact<T>) {
            if constexpr (generic) {
                return obj.ptr() != nullptr && PyTuple_Check(obj.ptr());
            } else {
                return (
                    obj.ptr() != nullptr && PyTuple_Check(obj.ptr()) &&
                    std::ranges::all_of(obj, [](const auto& item) {
                        return isinstance<Value>(item);
                    })
                );
            }

        } else if constexpr (
            std::derived_from<T, Tuple<Object>> ||
            std::derived_from<T, pybind11::tuple>
        ) {
            if constexpr (generic) {
                return obj.ptr() != nullptr;
            } else {
                return (
                    obj.ptr() != nullptr &&
                    std::ranges::all_of(obj, [](const auto& item) {
                        return isinstance<Value>(item);
                    })
                );
            }

        } else if constexpr (impl::tuple_like<T>) {
            return obj.ptr() != nullptr && check_value_type<impl::iter_type<T>>;

        } else {
            return false;
        }
    }

};


template <typename T>
Tuple(const std::initializer_list<T>&) -> Tuple<impl::as_object_t<T>>;
template <impl::is_iterable T>
Tuple(T) -> Tuple<impl::as_object_t<impl::iter_type<T>>>;
template <typename T, typename... Args>
    requires (!impl::is_iterable<T> && !impl::str_like<T>)
Tuple(T, Args...) -> Tuple<Object>;
template <impl::str_like T>
Tuple(T) -> Tuple<Str>;
template <size_t N>
Tuple(const char(&)[N]) -> Tuple<Str>;


/* Represents a statically-typed Python tuple in C++. */
template <typename Val>
class Tuple : public Object, public impl::TupleTag {
    using Base = Object;
    using Self = Tuple;
    static_assert(
        std::derived_from<Val, Object>,
        "py::Tuple can only contain types derived from py::Object."
    );

public:
    using impl::TupleTag::type;

    using size_type = size_t;
    using difference_type = std::ptrdiff_t;
    using value_type = Val;
    using pointer = value_type*;
    using reference = value_type&;
    using const_pointer = const value_type*;
    using const_reference = const value_type&;
    using iterator = impl::Iterator<impl::TupleIter<value_type>>;
    using const_iterator = impl::Iterator<impl::TupleIter<const value_type>>;
    using reverse_iterator = impl::ReverseIterator<impl::TupleIter<value_type>>;
    using const_reverse_iterator = impl::ReverseIterator<impl::TupleIter<const value_type>>;

    Tuple(Handle h, borrowed_t t) : Base(h, t) {}
    Tuple(Handle h, stolen_t t) : Base(h, t) {}

    template <typename... Args>
        requires (
            std::is_invocable_r_v<Tuple, __init__<Tuple, std::remove_cvref_t<Args>...>, Args...> &&
            __init__<Tuple, std::remove_cvref_t<Args>...>::enable
        )
    Tuple(Args&&... args) : Base((
        Interpreter::init(),
        __init__<Tuple, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    )) {}

    template <typename... Args>
        requires (
            !__init__<Tuple, std::remove_cvref_t<Args>...>::enable &&
            std::is_invocable_r_v<Tuple, __explicit_init__<Tuple, std::remove_cvref_t<Args>...>, Args...> &&
            __explicit_init__<Tuple, std::remove_cvref_t<Args>...>::enable
        )
    explicit Tuple(Args&&... args) : Base((
        Interpreter::init(),
        __explicit_init__<Tuple, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    )) {}

    /* Pack the contents of a braced initializer into a new Python tuple. */
    Tuple(const std::initializer_list<value_type>& contents) :
        Base((Interpreter::init(), PyTuple_New(contents.size())), stolen_t{})
    {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
        try {
            size_t i = 0;
            for (const value_type& item : contents) {
                PyTuple_SET_ITEM(m_ptr, i++, value_type(item).release().ptr());
            }
        } catch (...) {
            Py_DECREF(m_ptr);
            throw;
        }
    }

    /* Get the underlying PyObject* array. */
    [[nodiscard]] PyObject** data() const noexcept {
        return PySequence_Fast_ITEMS(this->ptr());
    }

    /* Directly access an item without bounds checking or constructing a proxy. */
    [[nodiscard]] value_type GET_ITEM(Py_ssize_t index) const {
        return reinterpret_borrow<value_type>(PyTuple_GET_ITEM(this->ptr(), index));
    }

    /* Directly set an item without bounds checking or constructing a proxy. */
    void SET_ITEM(Py_ssize_t index, const value_type& value) {
        PyObject* prev = PyTuple_GET_ITEM(this->ptr(), index);
        PyTuple_SET_ITEM(this->ptr(), index, Py_XNewRef(value.ptr()));
        Py_XDECREF(prev);
    }

    /* Equivalent to Python `tuple.count(value)`, but also takes optional start/stop
    indices similar to `tuple.index()`. */
    [[nodiscard]] Py_ssize_t count(
        const value_type& value,
        Py_ssize_t start = 0,
        Py_ssize_t stop = -1
    ) const {
        if (start != 0 || stop != -1) {
            PyObject* slice = PySequence_GetSlice(this->ptr(), start, stop);
            if (slice == nullptr) {
                Exception::from_python();
            }
            Py_ssize_t result = PySequence_Count(slice, value.ptr());
            Py_DECREF(slice);
            if (result < 0) {
                Exception::from_python();
            }
            return result;
        } else {
            Py_ssize_t result = PySequence_Count(this->ptr(), value.ptr());
            if (result < 0) {
                Exception::from_python();
            }
            return result;
        }
    }

    /* Equivalent to Python `tuple.index(value[, start[, stop]])`. */
    [[nodiscard]] Py_ssize_t index(
        const value_type& value,
        Py_ssize_t start = 0,
        Py_ssize_t stop = -1
    ) const {
        if (start != 0 || stop != -1) {
            PyObject* slice = PySequence_GetSlice(this->ptr(), start, stop);
            if (slice == nullptr) {
                Exception::from_python();
            }
            Py_ssize_t result = PySequence_Index(slice, value.ptr());
            Py_DECREF(slice);
            if (result < 0) {
                Exception::from_python();
            }
            return result;
        } else {
            Py_ssize_t result = PySequence_Index(this->ptr(), value.ptr());
            if (result < 0) {
                Exception::from_python();
            }
            return result;
        }
    }

    [[nodiscard]] friend Tuple operator+(
        const Tuple& self,
        const std::initializer_list<value_type>& items
    ) {
        return self.concat(items);
    }

    [[nodiscard]] friend Tuple operator+(
        const std::initializer_list<value_type>& items,
        const Tuple& self
    ) {
        return self.concat(items);
    }

    friend Tuple& operator+=(
        Tuple& self,
        const std::initializer_list<value_type>& items
    ) {
        self = self.concat(items);
        return self;
    }

protected:

    Tuple concat(const std::initializer_list<value_type>& items) const {
        Py_ssize_t length = PyTuple_GET_SIZE(this->ptr());
        PyObject* result = PyTuple_New(length + items.size());
        if (result == nullptr) {
            Exception::from_python();
        }
        try {
            PyObject** array = data();
            Py_ssize_t i = 0;
            while (i < length) {
                PyTuple_SET_ITEM(result, i, Py_NewRef(array[i]));
                ++i;
            }
            for (const value_type& item : items) {
                PyTuple_SET_ITEM(result, i++, value_type(item).release().ptr());
            }
            return reinterpret_steal<Tuple>(result);
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
    }

};


/* Default constructor.  Initializes to an empty tuple. */
template <typename Value>
struct __init__<Tuple<Value>>                               : Returns<Tuple<Value>> {
    static auto operator()() {
        PyObject* result = PyTuple_New(0);
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Tuple<Value>>(result);
    }
};


/* Converting constructor from std::pair. */
template <
    typename Value,
    std::convertible_to<Value> First,
    std::convertible_to<Value> Second
>
struct __init__<Tuple<Value>, std::pair<First, Second>>     : Returns<Tuple<Value>> {
    static auto operator()(const std::pair<First, Second>& pair) {
        PyObject* result = PyTuple_New(2);
        if (result == nullptr) {
            Exception::from_python();
        }
        try {
            PyTuple_SET_ITEM(result, 0, Value(pair.first).release().ptr());
            PyTuple_SET_ITEM(result, 1, Value(pair.second).release().ptr());
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
        return reinterpret_steal<Tuple<Value>>(result);
    }
};


/* Converting constructor from std::tuple. */
template <typename Value, std::convertible_to<Value>... Args>
struct __init__<Tuple<Value>, std::tuple<Args...>>           : Returns<Tuple<Value>> {
    static auto operator()(const std::tuple<Args...>& tuple) {
        PyObject* result = PyTuple_New(sizeof...(Args));
        if (result == nullptr) {
            Exception::from_python();
        }

        auto unpack_tuple = [&]<size_t... Ns>(std::index_sequence<Ns...>) {
            (
                PyTuple_SET_ITEM(
                    result,
                    Ns,
                    Value(std::get<Ns>(tuple)).release().ptr()
                ),
                ...
            );
        };

        try {
            unpack_tuple(std::index_sequence_for<Args...>{});
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
        return reinterpret_steal<Tuple<Value>>(result);
    }
};


/* Converting constructor from iterable C++ tuples. */
template <typename Value, impl::cpp_like Container>
    requires (
        impl::tuple_like<Container> &&
        impl::is_iterable<Container> &&
        std::convertible_to<impl::iter_type<Container>, Value>
    )
struct __init__<Tuple<Value>, Container>                    : Returns<Tuple<Value>> {
    static auto operator()(const Container& contents) {
        if constexpr (impl::has_size<Container>) {
            PyObject* result = PyTuple_New(std::size(contents));
            if (result == nullptr) {
                Exception::from_python();
            }
            try {
                size_t i = 0;
                for (const auto& item : contents) {
                    PyTuple_SET_ITEM(result, i++, Value(item).release().ptr());
                }
            } catch (...) {
                Py_DECREF(result);
                throw;
            }
            return reinterpret_steal<Tuple<Value>>(result);
        } else {
            PyObject* list = PyList_New(0);
            if (list == nullptr) {
                Exception::from_python();
            }
            try {
                for (const auto& item : contents) {
                    if (PyList_Append(list, Value(item).ptr())) {
                        Exception::from_python();
                    }
                }
            } catch (...) {
                Py_DECREF(list);
                throw;
            }
            PyObject* result = PyList_AsTuple(list);
            Py_DECREF(list);
            if (result == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Tuple<Value>>(result);
        }
    }
};


/* Explicitly convert an arbitrary C++ container into a py::Tuple. */
template <typename Value, impl::cpp_like Container>
    requires (
        !impl::tuple_like<Container> &&
        impl::is_iterable<Container> &&
        std::constructible_from<Value, impl::iter_type<Container>>
    )
struct __explicit_init__<Tuple<Value>, Container>            : Returns<Tuple<Value>> {
    static auto operator()(const Container& contents) {
        if constexpr (impl::has_size<Container>) {
            PyObject* result = PyTuple_New(std::size(contents));
            if (result == nullptr) {
                Exception::from_python();
            }
            try {
                size_t i = 0;
                for (const auto& item : contents) {
                    PyTuple_SET_ITEM(result, i++, Value(item).release().ptr());
                }
            } catch (...) {
                Py_DECREF(result);
                throw;
            }
            return reinterpret_steal<Tuple<Value>>(result);
        } else {
            PyObject* list = PyList_New(0);
            if (list == nullptr) {
                Exception::from_python();
            }
            try {
                for (const auto& item : contents) {
                    if (PyList_Append(list, Value(item).ptr())) {
                        Exception::from_python();
                    }
                }
            } catch (...) {
                Py_DECREF(list);
                throw;
            }
            PyObject* result = PyList_AsTuple(list);
            Py_DECREF(list);
            if (result == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Tuple<Value>>(result);
        }
    }
};


/* Explicitly convert an arbitrary Python container into a py::Tuple. */
template <typename Value, impl::python_like Container>
    requires (
        !impl::tuple_like<Container> &&
        impl::is_iterable<Container> &&
        std::constructible_from<Value, impl::iter_type<Container>>
    )
struct __explicit_init__<Tuple<Value>, Container>           : Returns<Tuple<Value>> {
    static constexpr bool generic = std::same_as<Value, Object>;

    static auto operator()(const Container& contents) {
        if constexpr (generic) {
            if constexpr (impl::list_like<Container>) {
                PyObject* result = PyList_AsTuple(contents.ptr());
                if (result == nullptr) {
                    Exception::from_python();
                }
                return reinterpret_steal<Tuple<Value>>(result);
            } else {
                PyObject* result = PySequence_Tuple(contents.ptr());
                if (result == nullptr) {
                    Exception::from_python();
                }
                return reinterpret_steal<Tuple<Value>>(result);
            }
        } else {
            if constexpr (impl::has_size<Container>) {
                PyObject* result = PyTuple_New(std::size(contents));
                if (result == nullptr) {
                    Exception::from_python();
                }
                try {
                    size_t i = 0;
                    for (const auto& item : contents) {
                        PyTuple_SET_ITEM(result, i++, Value(item).release().ptr());
                    }
                } catch (...) {
                    Py_DECREF(result);
                    throw;
                }
                return reinterpret_steal<Tuple<Value>>(result);
            } else {
                PyObject* list = PyList_New(0);
                if (list == nullptr) {
                    Exception::from_python();
                }
                try {
                    for (const auto& item : contents) {
                        if (PyList_Append(list, Value(item).ptr())) {
                            Exception::from_python();
                        }
                    }
                } catch (...) {
                    Py_DECREF(list);
                    throw;
                }
                PyObject* result = PyList_AsTuple(list);
                Py_DECREF(list);
                if (result == nullptr) {
                    Exception::from_python();
                }
                return reinterpret_steal<Tuple<Value>>(result);
            }
        }
    }
};


/* Explicitly convert a C++ string literal into a py::Tuple. */
template <typename Value, size_t N>
    requires (std::convertible_to<const char(&)[N], Value>)
struct __explicit_init__<Tuple<Value>, char[N]>             : Returns<Tuple<Value>> {
    static auto operator()(const char(&string)[N]) {
        PyObject* result = PyTuple_New(N - 1);
        if (result == nullptr) {
            Exception::from_python();
        }
        try {
            for (size_t i = 0; i < N - 1; ++i) {
                PyTuple_SET_ITEM(result, i, Value(string[i]).release().ptr());
            }
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
        return reinterpret_steal<Tuple<Value>>(result);
    }
};


/* Explicitly unpack a C++ string pointer into a py::Tuple. */
template <typename Value>
    requires (std::convertible_to<const char*, Value>)
struct __explicit_init__<Tuple<Value>, const char*>         : Returns<Tuple<Value>> {
    static auto operator()(const char* string) {
        PyObject* list = PyList_New(0);
        if (list == nullptr) {
            Exception::from_python();
        }
        try {
            for (const char* ptr = string; *ptr != '\0'; ++ptr) {
                if (PyList_Append(list, Value(ptr).ptr())) {
                    Exception::from_python();
                }
            }
        } catch (...) {
            Py_DECREF(list);
            throw;
        }
        PyObject* result = PyList_AsTuple(list);
        Py_DECREF(list);
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Tuple<Value>>(result);
    }
};


/* Construct a new py::Tuple from a pair of input iterators. */
template <typename Value, typename Iter, std::sentinel_for<Iter> Sentinel>
    requires (std::constructible_from<Value, decltype(*std::declval<Iter>())>)
struct __explicit_init__<Tuple<Value>, Iter, Sentinel>      : Returns<Tuple<Value>> {
    static auto operator()(Iter first, Sentinel last) {
        PyObject* list = PyList_New(0);
        if (list == nullptr) {
            Exception::from_python();
        }
        try {
            while (first != last) {
                if (PyList_Append(list, Value(*first).ptr())) {
                    Exception::from_python();
                }
                ++first;
            }
        } catch (...) {
            Py_DECREF(list);
            throw;
        }
        PyObject* result = PyList_AsTuple(list);
        Py_DECREF(list);
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Tuple<Value>>(result);
    }
};


template <std::derived_from<impl::TupleTag> From, impl::tuple_like To>
    requires (impl::pybind11_like<To> && !issubclass<To, From>())
struct __cast__<From, To> : Returns<To> {
    static To operator()(const From& from) {
        return reinterpret_borrow<To>(from.ptr());
    }
};


template <std::derived_from<impl::TupleTag> From, typename First, typename Second>
    requires (
        std::convertible_to<typename From::value_type, First> &&
        std::convertible_to<typename From::value_type, Second>
    )
struct __cast__<From, std::pair<First, Second>> : Returns<std::pair<First, Second>> {
    static std::pair<First, Second> operator()(const From& from) {
        if (from.size() != 2) {
            throw IndexError(
                "conversion to std::pair requires tuple of size 2, not " +
                std::to_string(from.size())
            );
        }
        return {
            impl::implicit_cast<First>(from.GET_ITEM(0)),
            impl::implicit_cast<Second>(from.GET_ITEM(1))
        };
    }
};


template <std::derived_from<impl::TupleTag> From, typename... Args>
    requires (std::convertible_to<typename From::value_type, Args> && ...)
struct __cast__<From, std::tuple<Args...>> : Returns<std::tuple<Args...>> {
    static std::tuple<Args...> operator()(const From& from) {
        if (from.size() != sizeof...(Args)) {
            throw IndexError(
                "conversion to std::tuple requires tuple of size " +
                std::to_string(sizeof...(Args)) + ", not " +
                std::to_string(from.size())
            );
        }
        return [&from]<size_t... N>(std::index_sequence<N...>) {
            return std::make_tuple(
                impl::implicit_cast<Args>(from.GET_ITEM(N))...
            );
        }(std::index_sequence_for<Args...>{});
    }
};


template <std::derived_from<impl::TupleTag> From, typename T, size_t N>
    requires (std::convertible_to<typename From::value_type, T>)
struct __cast__<From, std::array<T, N>> : Returns<std::array<T, N>> {
    static auto operator()(const From& from) {
        if (N != from.size()) {
            throw IndexError(
                "conversion to std::array requires tuple of size " +
                std::to_string(N) + ", not " + std::to_string(from.size())
            );
        }
        std::array<T, N> result;
        for (size_t i = 0; i < N; ++i) {
            result[i] = impl::implicit_cast<T>(from.GET_ITEM(i));
        }
        return result;
    }
};


template <std::derived_from<impl::TupleTag> From, typename T, typename... Args>
    requires (std::convertible_to<typename From::value_type, T>)
struct __cast__<From, std::vector<T, Args...>> : Returns<std::vector<T, Args...>> {
    static auto operator()(const From& from) {
        std::vector<T, Args...> result;
        result.reserve(from.size());
        for (const auto& item : from) {
            result.push_back(impl::implicit_cast<T>(item));
        }
        return result;
    }
};


template <std::derived_from<impl::TupleTag> From, typename T, typename... Args>
    requires (std::convertible_to<typename From::value_type, T>)
struct __cast__<From, std::list<T, Args...>> : Returns<std::list<T, Args...>> {
    static auto operator()(const From& from) {
        std::list<T, Args...> result;
        for (const auto& item : from) {
            result.push_back(impl::implicit_cast<T>(item));
        }
        return result;
    }
};


template <std::derived_from<impl::TupleTag> From, typename T, typename... Args>
    requires (std::convertible_to<typename From::value_type, T>)
struct __cast__<From, std::forward_list<T, Args...>> : Returns<std::forward_list<T, Args...>> {
    static auto operator()(const From& from) {
        std::forward_list<T, Args...> result;
        auto it = from.rbegin();
        auto end = from.rend();
        while (it != end) {
            result.push_front(impl::implicit_cast<T>(*it));
            ++it;
        }
        return result;
    }
};


template <std::derived_from<impl::TupleTag> From, typename T, typename... Args>
    requires (std::convertible_to<typename From::value_type, T>)
struct __cast__<From, std::deque<T, Args...>> : Returns<std::deque<T, Args...>> {
    static auto operator()(const From& from) {
        std::deque<T, Args...> result;
        for (const auto& item : from) {
            result.push_back(impl::implicit_cast<T>(item));
        }
        return result;
    }
};


namespace ops {

    template <typename Return, std::derived_from<impl::TupleTag> Self>
    struct len<Return, Self> {
        static size_t operator()(const Self& self) {
            return PyTuple_GET_SIZE(self.ptr());
        }
    };

    template <typename Return, std::derived_from<impl::TupleTag> Self>
    struct begin<Return, Self> {
        static auto operator()(const Self& self) {
            return impl::Iterator<impl::TupleIter<Return>>(self, 0);
        }
    };

    template <typename Return, std::derived_from<impl::TupleTag> Self>
    struct end<Return, Self> {
        static auto operator()(const Self& self) {
            return impl::Iterator<impl::TupleIter<Return>>(PyTuple_GET_SIZE(self.ptr()));
        }
    };

    template <typename Return, std::derived_from<impl::TupleTag> Self>
    struct rbegin<Return, Self> {
        static auto operator()(const Self& self) {
            return impl::ReverseIterator<impl::TupleIter<Return>>(
                self,
                PyTuple_GET_SIZE(self.ptr()) - 1
            );
        }
    };

    template <typename Return, std::derived_from<impl::TupleTag> Self>
    struct rend<Return, Self> {
        static auto operator()(const Self& self) {
            return impl::ReverseIterator<impl::TupleIter<Return>>(-1);
        }
    };

    template <typename Return, typename L, typename R>
        requires (std::derived_from<L, impl::TupleTag> || std::derived_from<R, impl::TupleTag>)
    struct add<Return, L, R> : sequence::add<Return, L, R> {};

    template <typename Return, std::derived_from<impl::TupleTag> L, typename R>
    struct iadd<Return, L, R> : sequence::iadd<Return, L, R> {};

    template <typename Return, typename L, typename R>
        requires (std::derived_from<L, impl::TupleTag> || std::derived_from<R, impl::TupleTag>)
    struct mul<Return, L, R> : sequence::mul<Return, L, R> {};

    template <typename Return, std::derived_from<impl::TupleTag> L, typename R>
    struct imul<Return, L, R> : sequence::imul<Return, L, R> {};

}


///////////////////////////////////
////    STRUCTURED BINDINGS    ////
///////////////////////////////////


namespace impl {

    template <typename T>
    concept field_like =
        std::same_as<T, std::decay_t<T>> &&
        std::derived_from<T, impl::ArgTag> &&
        T::is_positional &&
        T::is_keyword &&
        !T::is_optional &&
        !T::is_variadic &&
        std::same_as<typename T::type, std::decay_t<typename T::type>> &&
        std::derived_from<typename T::type, Object>;

    template <typename... Ts>
    static constexpr bool fields_are_homogenous = true;
    template <typename T1, typename T2, typename... Ts>
    static constexpr bool fields_are_homogenous<T1, T2, Ts...> =
        std::same_as<typename T1::type, typename T2::type> &&
        fields_are_homogenous<T2, Ts...>;

    template <typename... Ts>
    static constexpr bool field_names_are_unique = true;
    template <typename T, typename... Ts>
    static constexpr bool field_names_are_unique<T, Ts...> =
        ((T::name != Ts::name) && ...) && field_names_are_unique<Ts...>;

    template <typename... Ts>
    struct get_first_field { using type = Object; };
    template <typename T, typename... Ts>
    struct get_first_field<T, Ts...> { using type = typename T::type; };

    template <typename... Fields>
    using field_type = std::conditional_t<
        fields_are_homogenous<Fields...>,
        typename get_first_field<Fields...>::type,
        Object
    >;

}


/* A subclass of Tuple that can represent mixed types.  These are semantically
equivalent to collections.namedtuple instances in Python, except that attributes are
mutable by default, similar to std::pair and std::tuple.  They can also be used as
structured bindings. */
template <impl::field_like... Fields>
    requires (sizeof...(Fields) > 0 && impl::field_names_are_unique<Fields...>)
class Struct : public Tuple<impl::field_type<Fields...>> {
    using field_type = impl::field_type<Fields...>;
    using Base = Tuple<field_type>;
    using Self = Struct;


public:
    using impl::TupleTag::type;

    using size_type = size_t;
    using difference_type = std::ptrdiff_t;
    using value_type = field_type;
    using pointer = value_type*;
    using reference = value_type&;
    using const_pointer = const value_type*;
    using const_reference = const value_type&;
    using iterator = impl::Iterator<impl::TupleIter<value_type>>;
    using const_iterator = impl::Iterator<impl::TupleIter<const value_type>>;
    using reverse_iterator = impl::ReverseIterator<impl::TupleIter<value_type>>;
    using const_reverse_iterator = impl::ReverseIterator<impl::TupleIter<const value_type>>;

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    // TODO: requires all fields to be default-constructible

    /* Default constructor.  Default-initializes all fields. */
    Struct() : Base() {}


    // TODO: this should represent a namedtuple at the python level?
    // -> namedtuples are immutable, so setattr won't work as expected.  Probably I
    // should reimplement it myself or specialize Attr<> accordingly.


};


}  // namespace py
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_TUPLE_H
