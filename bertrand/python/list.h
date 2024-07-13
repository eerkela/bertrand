// #ifndef BERTRAND_PYTHON_MODULE_GUARD
// #error "Header implementations should not be included directly.  Import 'bertrand.python' instead."
// #endif

#ifndef BERTRAND_PYTHON_LIST_H
#define BERTRAND_PYTHON_LIST_H

#include "common.h"
#include "bool.h"


// TODO: right now, containers are convertible to but not from their equivalent types.
// This is an asymmetry that could cause bugs down the line, so it should probably be
// rethought.

// Also, I might be able to make implicit constructors more generic using the
// __as_object__ control struct.  Anything that is mapped to List is a candidate for
// implicit conversion.


namespace bertrand {
namespace py {


template <typename T, typename Value>
struct __issubclass__<T, List<Value>>                       : Returns<bool> {
    static constexpr bool generic = std::same_as<Value, Object>;
    template <typename U>
    static constexpr bool check_value_type = std::derived_from<U, Object> ?
        std::derived_from<U, Value> : std::convertible_to<U, Value>;

    static consteval bool operator()(const T&) { return operator()(); }
    static consteval bool operator()() {
        if constexpr (!impl::list_like<T>) {
            return false;
        } else if constexpr (impl::pybind11_like<T>) {
            return generic;
        } else if constexpr (impl::is_iterable<T>) {
            return check_value_type<impl::iter_type<T>>;
        } else {
            return false;
        }
    }
};


template <typename T, typename Value>
struct __isinstance__<T, List<Value>>                       : Returns<bool> {
    static constexpr bool generic = std::same_as<Value, Object>;
    template <typename U>
    static constexpr bool check_value_type = std::derived_from<U, Object> ?
        std::derived_from<U, Value> : std::convertible_to<U, Value>;

    static constexpr bool operator()(const T& obj) {
        if constexpr (impl::cpp_like<T>) {
            return issubclass<T, List<Value>>();

        } else if constexpr (impl::is_object_exact<T>) {
            if constexpr (generic) {
                return obj.ptr() != nullptr && PyList_Check(obj.ptr());
            } else {
                return (
                    obj.ptr() != nullptr && PyList_Check(obj.ptr()) &&
                    std::ranges::all_of(obj, [](const auto& item) {
                        return isinstance<Value>(item);
                    })
                );
            }

        } else if constexpr (
            std::derived_from<T, List<Object>> ||
            std::derived_from<T, pybind11::list>
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

        } else if constexpr (impl::list_like<T>) {
            return obj.ptr() != nullptr && check_value_type<impl::iter_type<T>>;

        } else {
            return false;
        }
    }
};


template <typename T>
List(const std::initializer_list<T>&) -> List<impl::as_object_t<T>>;
template <impl::is_iterable T>
List(T) -> List<impl::as_object_t<impl::iter_type<T>>>;
template <typename T, typename... Args>
    requires (!impl::is_iterable<T> && !impl::str_like<T>)
List(T, Args...) -> List<Object>;
template <impl::str_like T>
List(T) -> List<Str>;
template <size_t N>
List(const char(&)[N]) -> List<Str>;


/* Represents a statically-typed Python list in C++. */
template <typename Val>
class List : public Object, public impl::ListTag {
    using Base = Object;
    using Self = List;
    static_assert(
        std::derived_from<Val, Object>,
        "py::List can only contain types derived from py::Object."
    );

public:
    using impl::ListTag::type;

    using size_type = size_t;
    using difference_type = std::ptrdiff_t;
    using value_type = Val;
    using pointer = value_type*;
    using const_pointer = const value_type*;
    using reference = value_type&;
    using const_reference = const value_type&;
    using iterator = impl::Iterator<impl::ListIter<value_type>>;
    using const_iterator = impl::Iterator<impl::ListIter<const value_type>>;
    using reverse_iterator = impl::ReverseIterator<impl::ListIter<value_type>>;
    using const_reverse_iterator = impl::ReverseIterator<impl::ListIter<const value_type>>;

    List(Handle h, borrowed_t t) : Base(h, t) {}
    List(Handle h, stolen_t t) : Base(h, t) {}

    template <typename... Args>
        requires (
            std::is_invocable_r_v<List, __init__<List, std::remove_cvref_t<Args>...>, Args...> &&
            __init__<List, std::remove_cvref_t<Args>...>::enable
        )
    List(Args&&... args) : Base((
        Interpreter::init(),
        __init__<List, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    )) {}

    template <typename... Args>
        requires (
            !__init__<List, std::remove_cvref_t<Args>...>::enable &&
            std::is_invocable_r_v<List, __explicit_init__<List, std::remove_cvref_t<Args>...>, Args...> &&
            __explicit_init__<List, std::remove_cvref_t<Args>...>::enable
        )
    explicit List(Args&&... args) : Base((
        Interpreter::init(),
        __explicit_init__<List, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    )) {}

    /* Pack the contents of a braced initializer list into a new Python list. */
    List(const std::initializer_list<value_type>& contents) :
        Base((Interpreter::init(), PyList_New(contents.size())), stolen_t{})
    {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
        try {
            size_t i = 0;
            for (const value_type& item : contents) {
                PyList_SET_ITEM(m_ptr, i++, value_type(item).release().ptr());
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
        return reinterpret_borrow<value_type>(PyList_GET_ITEM(this->ptr(), index));
    }

    /* Directly set an item without bounds checking or constructing a proxy.  */
    void SET_ITEM(Py_ssize_t index, PyObject* value) {
        PyObject* prev = PyList_GET_ITEM(this->ptr(), index);
        PyList_SET_ITEM(this->ptr(), index, value);
        Py_XDECREF(prev);
    }

    /* Equivalent to Python `list.append(value)`. */
    void append(const value_type& value) {
        if (PyList_Append(this->ptr(), value.ptr())) {
            Exception::from_python();
        }
    }

    /* Equivalent to Python `list.extend(items)`. */
    template <impl::is_iterable T>
        requires (std::convertible_to<impl::iter_type<T>, value_type>)
    void extend(const T& items) {
        if constexpr (impl::python_like<T>) {
            impl::call_method<"extend">(*this, items);
        } else {
            for (const auto& item : items) {
                append(item);
            }
        }
    }

    /* Equivalent to Python `list.extend(items)`, where items are given as a braced
    initializer list. */
    void extend(const std::initializer_list<value_type>& items) {
        for (const value_type& item : items) {
            append(item);
        }
    }

    /* Equivalent to Python `list.insert(index, value)`. */
    void insert(Py_ssize_t index, const value_type& value) {
        if (PyList_Insert(this->ptr(), index, value.ptr())) {
            Exception::from_python();
        }
    }

    /* Equivalent to Python `list.copy()`. */
    [[nodiscard]] List copy() const {
        PyObject* result = PyList_GetSlice(this->ptr(), 0, PyList_GET_SIZE(this->ptr()));
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<List>(result);
    }

    /* Equivalent to Python `list.count(value)`, but also takes optional start/stop
    indices similar to `list.index()`. */
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

    /* Equivalent to Python `list.index(value[, start[, stop]])`. */
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

    /* Equivalent to Python `list.clear()`. */
    void clear() {
        if (PyList_SetSlice(this->ptr(), 0, PyList_GET_SIZE(this->ptr()), nullptr)) {
            Exception::from_python();
        }
    }

    template <typename... Args> requires (impl::invocable<Self, "remove", Args...>)
    decltype(auto) remove(Args&&... args) {
        return impl::call_method<"remove">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "pop", Args...>)
    decltype(auto) pop(Args&&... args) {
        return impl::call_method<"pop">(*this, std::forward<Args>(args)...);
    }

    /* Equivalent to Python `list.reverse()`. */
    void reverse() {
        if (PyList_Reverse(this->ptr())) {
            Exception::from_python();
        }
    }

    template <typename... Args> requires (impl::invocable<Self, "sort", Args...>)
    decltype(auto) sort(Args&&... args) {
        return impl::call_method<"sort">(*this, std::forward<Args>(args)...);
    }

    [[nodiscard]] friend List operator+(
        const List& self,
        const std::initializer_list<value_type>& items
    ) {
        return self.concat(items);
    }

    [[nodiscard]] friend List operator+(
        const std::initializer_list<value_type>& items,
        const List& self
    ) {
        return self.concat(items);
    }

    friend List& operator+=(
        List& self,
        const std::initializer_list<value_type>& items
    ) {
        self.extend(items);
        return self;
    }

protected:

    List concat(const std::initializer_list<value_type>& items) const {
        Py_ssize_t length = PyList_GET_SIZE(this->ptr());
        PyObject* result = PyList_New(length + items.size());
        if (result == nullptr) {
            Exception::from_python();
        }
        try {
            PyObject** array = data();
            Py_ssize_t i = 0;
            while (i < length) {
                PyList_SET_ITEM(result, i, Py_NewRef(array[i]));
                ++i;
            }
            for (const value_type& item : items) {
                PyList_SET_ITEM(result, i++, value_type(item).release().ptr());
            }
            return reinterpret_steal<List>(result);
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
    }

};


/* Default constructor.  Initializes to an empty list. */
template <typename Value>
struct __init__<List<Value>>                                : Returns<List<Value>> {
    static auto operator()() {
        PyObject* result = PyList_New(0);
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<List<Value>>(result);
    }
};


/* Converting constructor from compatible C++ lists. */
template <typename Value, impl::cpp_like Container>
    requires (
        impl::list_like<Container> &&
        std::convertible_to<impl::iter_type<Container>, Value>
    )
struct __init__<List<Value>, Container>                     : Returns<List<Value>> {
    static auto operator()(const Container& contents) {
        if constexpr (impl::has_size<Container>) {
            PyObject* result = PyList_New(std::size(contents));
            if (result == nullptr) {
                Exception::from_python();
            }
            try {
                size_t i = 0;
                for (const auto& item : contents) {
                    PyList_SET_ITEM(result, i++, Value(item).release().ptr());
                }
            } catch (...) {
                Py_DECREF(result);
                throw;
            }
            return reinterpret_steal<List<Value>>(result);
        } else {
            PyObject* result = PyList_New(0);
            if (result == nullptr) {
                Exception::from_python();
            }
            try {
                for (const auto& item : contents) {
                    if (PyList_Append(result, Value(item).ptr())) {
                        Exception::from_python();
                    }
                }
            } catch (...) {
                Py_DECREF(result);
                throw;
            }
            return reinterpret_steal<List<Value>>(result);
        }
    }
};


/* Explicitly convert an arbitrary C++ container into a py::List. */
template <typename Value, impl::cpp_like Container>
    requires (
        !impl::list_like<Container> &&
        impl::is_iterable<Container> &&
        std::constructible_from<Value, impl::iter_type<Container>>
    )
struct __explicit_init__<List<Value>, Container>            : Returns<List<Value>> {
    static auto operator()(const Container& contents) {
        if constexpr (impl::has_size<Container>) {
            PyObject* result = PyList_New(std::size(contents));
            if (result == nullptr) {
                Exception::from_python();
            }
            try {
                size_t i = 0;
                for (const auto& item : contents) {
                    PyList_SET_ITEM(result, i++, Value(item).release().ptr());
                }
            } catch (...) {
                Py_DECREF(result);
                throw;
            }
            return reinterpret_steal<List<Value>>(result);
        } else {
            PyObject* result = PyList_New(0);
            if (result == nullptr) {
                Exception::from_python();
            }
            try {
                for (const auto& item : contents) {
                    if (PyList_Append(result, Value(item).ptr())) {
                        Exception::from_python();
                    }
                }
            } catch (...) {
                Py_DECREF(result);
                throw;
            }
            return reinterpret_steal<List<Value>>(result);
        }
    }
};


/* Explicitly convert an arbitrary Python container into a py::List. */
template <typename Value, impl::python_like Container>
    requires (
        !impl::list_like<Container> &&
        impl::is_iterable<Container> &&
        std::constructible_from<Value, impl::iter_type<Container>>
    )
struct __explicit_init__<List<Value>, Container>            : Returns<List<Value>> {
    static auto operator()(const Container& contents) {
        if constexpr (std::same_as<Value, Object>) {
            PyObject* result = PySequence_List(contents.ptr());
            if (result == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<List<Value>>(result);
        } else {
            if constexpr (impl::has_size<Container>) {
                PyObject* result = PyList_New(std::size(contents));
                if (result == nullptr) {
                    Exception::from_python();
                }
                try {
                    size_t i = 0;
                    for (const auto& item : contents) {
                        PyList_SET_ITEM(result, i++, Value(item).release().ptr());
                    }
                } catch (...) {
                    Py_DECREF(result);
                    throw;
                }
                return reinterpret_steal<List<Value>>(result);
            } else {
                PyObject* result = PyList_New(0);
                if (result == nullptr) {
                    Exception::from_python();
                }
                try {
                    for (const auto& item : contents) {
                        if (PyList_Append(result, Value(item).ptr())) {
                            Exception::from_python();
                        }
                    }
                } catch (...) {
                    Py_DECREF(result);
                    throw;
                }
                return reinterpret_steal<List<Value>>(result);
            }
        }
    }
};


/* Explicitly convert a std::pair into a py::List. */
template <typename Value, typename First, typename Second>
    requires (
        std::constructible_from<Value, First> &&
        std::constructible_from<Value, Second>
    )
struct __explicit_init__<List<Value>, std::pair<First, Second>> : Returns<List<Value>> {
    static auto operator()(const std::pair<First, Second>& pair) {
        PyObject* result = PyList_New(2);
        if (result == nullptr) {
            Exception::from_python();
        }
        try {
            PyList_SET_ITEM(result, 0, Value(pair.first).release().ptr());
            PyList_SET_ITEM(result, 1, Value(pair.second).release().ptr());
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
        return reinterpret_steal<List<Value>>(result);
    }
};


/* Explicitly convert a std::tuple into a py::List. */
template <typename Value, typename... Ts>
    requires (std::constructible_from<Value, Ts> && ...)
struct __explicit_init__<List<Value>, std::tuple<Ts...>>    : Returns<List<Value>> {
    static auto operator()(const std::tuple<Ts...>& tuple) {
        PyObject* result = PyList_New(sizeof...(Ts));
        if (result == nullptr) {
            Exception::from_python();
        }

        auto unpack_tuple = [&]<size_t... Ns>(std::index_sequence<Ns...>) {
            (
                PyList_SET_ITEM(
                    result,
                    Ns,
                    Value(std::get<Ns>(tuple)).release().ptr()
                ),
                ...
            );
        };

        try {
            unpack_tuple(std::index_sequence_for<Ts...>{});
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
        return reinterpret_steal<List<Value>>(result);
    }
};


/* Explicitly convert a C++ string literal into a py::List. */
template <typename Value, size_t N>
    requires (std::convertible_to<const char(&)[1], Value>)
struct __explicit_init__<List<Value>, char[N]>              : Returns<List<Value>> {
    static auto operator()(const char(&string)[N]) {
        PyObject* result = PyList_New(N - 1);
        if (result == nullptr) {
            Exception::from_python();
        }
        try {
            for (size_t i = 0; i < N - 1; ++i) {
                PyList_SET_ITEM(result, i, Value(string[i]).release().ptr());
            }
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
        return reinterpret_steal<List<Value>>(result);
    }
};


/* Explicitly convert a C++ string pointer into a py::List. */
template <typename Value>
    requires (std::convertible_to<const char*, Value>)
struct __explicit_init__<List<Value>, const char*>           : Returns<List<Value>> {
    static auto operator()(const char* string) {
        PyObject* result = PyList_New(0);
        if (result == nullptr) {
            Exception::from_python();
        }
        try {
            for (const char* ptr = string; *ptr != '\0'; ++ptr) {
                if (PyList_Append(result, Value(ptr).ptr())) {
                    Exception::from_python();
                }
            }
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
        return reinterpret_steal<List<Value>>(result);
    }
};


/* Construct a new py::List from a pair of input iterators. */
template <typename Value, typename Iter, std::sentinel_for<Iter> Sentinel>
    requires (std::constructible_from<Value, decltype(*std::declval<Iter>())>)
struct __explicit_init__<List<Value>, Iter, Sentinel>       : Returns<List<Value>> {
    static auto operator()(Iter first, Sentinel last) {
        PyObject* result = PyList_New(0);
        if (result == nullptr) {
            Exception::from_python();
        }
        try {
            while (first != last) {
                if (PyList_Append(result, Value(*first).ptr())) {
                    Exception::from_python();
                }
                ++first;
            }
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
        return reinterpret_steal<List<Value>>(result);
    }
};


/* Implicitly convert a py::List into a pybind11::list regardless of the templated
value type. */
template <std::derived_from<impl::ListTag> From, impl::list_like To>
    requires (impl::pybind11_like<To> && !issubclass<To, From>())
struct __cast__<From, To> : Returns<To> {
    static auto operator()(const From& from) {
        return reinterpret_borrow<To>(from.ptr());
    }
};


template <std::derived_from<impl::ListTag> From, typename First, typename Second>
    requires (
        std::convertible_to<typename From::value_type, First> &&
        std::convertible_to<typename From::value_type, Second>
    )
struct __cast__<From, std::pair<First, Second>> : Returns<std::pair<First, Second>> {
    static std::pair<First, Second> operator()(const From& from) {
        size_t size = len(from);
        if (size != 2) {
            throw IndexError(
                "conversion to std::pair requires list of size 2, not "
                + std::to_string(size)
            );
        }
        return {
            impl::implicit_cast<First>(from.GET_ITEM(0)),
            impl::implicit_cast<Second>(from.GET_ITEM(1))
        };
    }
};


template <std::derived_from<impl::ListTag> From, typename... Args>
    requires (std::convertible_to<typename From::value_type, Args> && ...)
struct __cast__<From, std::tuple<Args...>> : Returns<std::tuple<Args...>> {
    static std::tuple<Args...> operator()(const From& from) {
        size_t size = len(from);
        if (size != sizeof...(Args)) {
            throw IndexError(
                "conversion to std::tuple requires list of size " +
                std::to_string(sizeof...(Args)) + ", not " +
                std::to_string(size)
            );
        }
        return [&from]<size_t... N>(std::index_sequence<N...>) {
            return std::make_tuple(
                impl::implicit_cast<Args>(from.GET_ITEM(N))...
            );
        }(std::index_sequence_for<Args...>{});
    }
};


template <std::derived_from<impl::ListTag> From, typename T, size_t N>
    requires (std::convertible_to<typename From::value_type, T>)
struct __cast__<From, std::array<T, N>> : Returns<std::array<T, N>> {
    static auto operator()(const From& from) {
        size_t size = len(from);
        if (size != N) {
            throw IndexError(
                "conversion to std::array requires list of size " +
                std::to_string(N) + ", not " + std::to_string(size)
            );
        }
        std::array<T, N> result;
        for (size_t i = 0; i < N; ++i) {
            result[i] = impl::implicit_cast<T>(from.GET_ITEM(i));
        }
        return result;
    }
};


template <std::derived_from<impl::ListTag> From, typename T, typename... Args>
    requires (std::convertible_to<typename From::value_type, T>)
struct __cast__<From, std::vector<T, Args...>> : Returns<std::vector<T, Args...>> {
    static auto operator()(const From& from) {
        std::vector<T, Args...> result;
        result.reserve(len(from));
        for (const auto& item : from) {
            result.push_back(impl::implicit_cast<T>(item));
        }
        return result;
    }
};


template <std::derived_from<impl::ListTag> From, typename T, typename... Args>
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


template <std::derived_from<impl::ListTag> From, typename T, typename... Args>
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


template <std::derived_from<impl::ListTag> From, typename T, typename... Args>
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


template <std::derived_from<impl::ListTag> Self>
struct __len__<Self>                                        : Returns<size_t> {
    static size_t operator()(const Self& self) {
        return PyList_GET_SIZE(self.ptr());
    }
};


template <std::derived_from<impl::ListTag> L, std::convertible_to<L> R>
struct __add__<L, R>                                        : Returns<List<typename L::value_type>> {
    static auto operator()(const L& lhs, const R& rhs) {
        PyObject* result = PySequence_Concat(
            as_object(lhs).ptr(),
            as_object(rhs).ptr()
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<List<typename L::value_type>>(result);
    }
};


template <typename L, std::derived_from<impl::ListTag> R>
    requires (!std::convertible_to<R, L> && std::convertible_to<L, R>)
struct __add__<L, R>                                        : Returns<List<typename R::value_type>> {
    static auto operator()(const L& lhs, const R& rhs) {
        PyObject* result = PySequence_Concat(
            as_object(lhs).ptr(),
            as_object(rhs).ptr()
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<List<typename L::value_type>>(result);
    }
};


template <std::derived_from<impl::ListTag> L, std::convertible_to<L> R>
struct __iadd__<L, R>                                       : Returns<List<typename L::value_type>&> {
    static void operator()(L& lhs, const R& rhs) {
        PyObject* result = PySequence_InPlaceConcat(
            lhs.ptr(),
            as_object(rhs).ptr()
        );
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == lhs.ptr()) {
            Py_DECREF(result);
        } else {
            lhs = reinterpret_steal<L>(result);
        }
    }
};


template <std::derived_from<impl::ListTag> L, impl::int_like R>
struct __mul__<L, R>                                        : Returns<List<typename L::value_type>> {
    static auto operator()(const L& lhs, const R& rhs) {
        PyObject* result = PySequence_Repeat(lhs.ptr(), rhs);
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<List<typename L::value_type>>(result);
    }
};


template <impl::int_like L, std::derived_from<impl::ListTag> R>
struct __mul__<L, R>                                        : Returns<List<typename R::value_type>> {
    static auto operator()(const L& lhs, const R& rhs) {
        PyObject* result = PySequence_Repeat(rhs.ptr(), lhs);
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<List<typename L::value_type>>(result);
    }
};


template <std::derived_from<impl::ListTag> L, impl::int_like R>
struct __imul__<L, R>                                       : Returns<List<typename L::value_type>&> {
    static void operator()(L& lhs, Py_ssize_t rhs) {
        PyObject* result = PySequence_InPlaceRepeat(lhs.ptr(), rhs);
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == lhs.ptr()) {
            Py_DECREF(result);
        } else {
            lhs = reinterpret_steal<L>(result);
        }
    }
};


template <std::derived_from<impl::ListTag> Self>
struct __iter__<Self>                                       : Returns<typename Self::value_type> {
    static auto begin(const Self& self) {
        return impl::Iterator<impl::ListIter<typename __iter__<Self>::type>>(self, 0);
    }
    static auto end(const Self& self) {
        return impl::Iterator<impl::ListIter<typename __iter__<Self>::type>>(
            PyList_GET_SIZE(self.ptr())
        );
    }
};


template <std::derived_from<impl::ListTag> Self>
struct __reversed__<Self>                                   : Returns<typename Self::value_type> {
    static auto rbegin(const Self& self) {
        return impl::ReverseIterator<impl::ListIter<typename __reversed__<Self>::type>>(
            self,
            PyList_GET_SIZE(self.ptr()) - 1
        );
    }
    static auto rend(const Self& self) {
        return impl::ReverseIterator<impl::ListIter<typename __reversed__<Self>::type>>(-1);
    }
};


}  // namespace py
}  // namespace bertrand


#endif
