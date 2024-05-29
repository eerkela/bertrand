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


/* Represents a statically-typed Python tuple in C++. */
template <typename Val>
class Tuple : public Object, public impl::TupleTag {
    using Base = Object;
    using Self = Tuple;
    static_assert(
        std::derived_from<Val, Object>,
        "py::Tuple can only contain types derived from py::Object."
    );

    static constexpr bool generic = std::same_as<Val, Object>;

    template <typename T>
    static constexpr bool check_value_type = std::derived_from<T, Object> ?
        std::derived_from<T, Val> : std::convertible_to<T, Val>;

    template <typename T>
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

    // TODO: use decay_t in all Object::check<T>(); methods

    template <typename T>
    [[nodiscard]] static consteval bool typecheck() {
        if constexpr (!impl::tuple_like<T>) {
            return false;
        } else if constexpr (impl::pybind11_like<T>) {
            return generic;
        } else if constexpr (impl::is_iterable<T>) {
            return check_value_type<impl::iter_type<T>>;
        } else if constexpr (stl_check<std::decay_t<T>>::match) {
            return stl_check<std::decay_t<T>>::value;
        } else {
            return false;
        }
    }

    template <typename T>
    [[nodiscard]] static constexpr bool typecheck(const T& obj) {
        if (impl::cpp_like<T>) {
            return typecheck<T>();

        } else if constexpr (impl::is_object_exact<T>) {
            if constexpr (generic) {
                return obj.ptr() != nullptr && PyTuple_Check(obj.ptr());
            } else {
                return (
                    obj.ptr() != nullptr && PyTuple_Check(obj.ptr()) &&
                    std::ranges::all_of(obj, [](const auto& item) {
                        return value_type::typecheck(item);
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
                        return value_type::typecheck(item);
                    })
                );
            }

        } else if constexpr (impl::tuple_like<T>) {
            return obj.ptr() != nullptr && check_value_type<impl::iter_type<T>>;

        } else {
            return false;
        }
    }

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Default constructor.  Initializes to an empty tuple. */
    Tuple() : Base(PyTuple_New(0), stolen_t{}) {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /* Reinterpret_borrow/reinterpret_steal constructors. */
    Tuple(Handle h, const borrowed_t& t) : Base(h, t) {}
    Tuple(Handle h, const stolen_t& t) : Base(h, t) {}

    /* Copy/move constructors from equivalent pybind11 types or other tuples with a
    narrower value type. */
    template <impl::python_like T> requires (typecheck<T>())
    Tuple(T&& other) : Base(std::forward<T>(other)) {}

    /* Unwrap a pybind11 accessor into a py::Tuple. */
    template <typename Policy>
    Tuple(const pybind11::detail::accessor<Policy>& accessor) :
        Base(Base::from_pybind11_accessor<Tuple>(accessor).release(), stolen_t{})
    {}

    /* Pack the contents of a braced initializer into a new Python tuple. */
    Tuple(const std::initializer_list<value_type>& contents) :
        Base(PyTuple_New(contents.size()), stolen_t{})
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

    /* Explicitly unpack a generic Python container into a py::Tuple. */
    template <impl::python_like T> requires (!impl::tuple_like<T> && impl::is_iterable<T>)
    explicit Tuple(const T& contents) : Base(nullptr, stolen_t{}) {
        if constexpr (generic) {
            if constexpr (impl::list_like<T>) {
                m_ptr = PyList_AsTuple(contents.ptr());
            } else {
                m_ptr = PySequence_Tuple(contents.ptr());
            }
            if (m_ptr == nullptr) {
                Exception::from_python();
            }
        } else {
            if constexpr (impl::has_size<T>) {
                m_ptr = PyTuple_New(std::size(contents));
                if (m_ptr == nullptr) {
                    Exception::from_python();
                }
                try {
                    size_t i = 0;
                    for (const auto& item : contents) {
                        PyTuple_SET_ITEM(m_ptr, i++, value_type(item).release().ptr());
                    }
                } catch (...) {
                    Py_DECREF(m_ptr);
                    throw;
                }
            } else {
                PyObject* list = PyList_New(0);
                if (list == nullptr) {
                    Exception::from_python();
                }
                try {
                    for (const auto& item : contents) {
                        if (PyList_Append(list, value_type(item).ptr())) {
                            Exception::from_python();
                        }
                    }
                } catch (...) {
                    Py_DECREF(list);
                    throw;
                }
                m_ptr = PyList_AsTuple(list);
                Py_DECREF(list);
                if (m_ptr == nullptr) {
                    Exception::from_python();
                }
            }
        }
    }

    /* Explicitly unpack a generic C++ container into a new py::Tuple. */
    template <impl::cpp_like T> requires (impl::is_iterable<T>)
    explicit Tuple(const T& contents) : Base(nullptr, stolen_t{}) {
        if constexpr (impl::has_size<T>) {
            size_t size = std::size(contents);
            m_ptr = PyTuple_New(size);
            if (m_ptr == nullptr) {
                Exception::from_python();
            }
            try {
                size_t i = 0;
                for (const auto& item : contents) {
                    PyTuple_SET_ITEM(m_ptr, i++, value_type(item).release().ptr());
                }
            } catch (...) {
                Py_DECREF(m_ptr);
                throw;
            }
        } else {
            PyObject* list = PyList_New(0);
            if (list == nullptr) {
                Exception::from_python();
            }
            try {
                for (const auto& item : contents) {
                    if (PyList_Append(list, value_type(item).ptr())) {
                        Exception::from_python();
                    }
                }
            } catch (...) {
                Py_DECREF(list);
                throw;
            }
            m_ptr = PyList_AsTuple(list);
            Py_DECREF(list);
            if (m_ptr == nullptr) {
                Exception::from_python();
            }
        }
    }

    /* Construct a new tuple from a pair of input iterators. */
    template <typename Iter, std::sentinel_for<Iter> Sentinel>
    explicit Tuple(Iter first, Sentinel last) : Base(nullptr, stolen_t{}) {
        PyObject* list = PyList_New(0);
        if (list == nullptr) {
            Exception::from_python();
        }
        try {
            while (first != last) {
                if (PyList_Append(list, value_type(*first).ptr())) {
                    Exception::from_python();
                }
                ++first;
            }
        } catch (...) {
            Py_DECREF(list);
            throw;
        }
        m_ptr = PyList_AsTuple(list);
        Py_DECREF(list);
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /* Explicitly unpack a std::pair into a py::Tuple. */
    template <typename First, typename Second>
        requires (
            std::convertible_to<value_type, First> &&
            std::convertible_to<value_type, Second>
        )
    explicit Tuple(const std::pair<First, Second>& pair) :
        Base(PyTuple_New(2), stolen_t{})
    {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
        try {
            PyTuple_SET_ITEM(m_ptr, 0, value_type(pair.first).release().ptr());
            PyTuple_SET_ITEM(m_ptr, 1, value_type(pair.second).release().ptr());
        } catch (...) {
            Py_DECREF(m_ptr);
            throw;
        }
    }

    /* Explicitly unpack a std::tuple into a py::Tuple. */
    template <typename... Args> requires (std::constructible_from<value_type, Args> && ...)
    explicit Tuple(const std::tuple<Args...>& tuple) :
        Base(PyTuple_New(sizeof...(Args)), stolen_t{})
    {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }

        auto unpack_tuple = [&]<size_t... Ns>(std::index_sequence<Ns...>) {
            (
                PyTuple_SET_ITEM(
                    m_ptr,
                    Ns,
                    value_type(std::get<Ns>(tuple)).release().ptr()
                ),
                ...
            );
        };

        try {
            unpack_tuple(std::index_sequence_for<Args...>{});
        } catch (...) {
            Py_DECREF(m_ptr);
            throw;
        }
    }

    /* Explicitly unpack a C++ string literal into a py::Tuple. */
    template <size_t N> requires (generic || std::same_as<value_type, Str>)
    explicit Tuple(const char (&string)[N]) : Base(PyTuple_New(N - 1), stolen_t{}) {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
        try {
            for (size_t i = 0; i < N - 1; ++i) {
                PyObject* item = PyUnicode_FromStringAndSize(string + i, 1);
                if (item == nullptr) {
                    Exception::from_python();
                }
                PyTuple_SET_ITEM(m_ptr, i, item);
            }
        } catch (...) {
            Py_DECREF(m_ptr);
            throw;
        }
    }

    /* Explicitly unpack a C++ string pointer into a py::Tuple. */
    template <std::same_as<const char*> T> requires (generic || std::same_as<value_type, Str>)
    explicit Tuple(T string) : Base(nullptr, stolen_t{}) {
        PyObject* list = PyList_New(0);
        if (list == nullptr) {
            Exception::from_python();
        }
        try {
            for (const char* ptr = string; *ptr != '\0'; ++ptr) {
                PyObject* item = PyUnicode_FromStringAndSize(ptr, 1);
                if (item == nullptr) {
                    Exception::from_python();
                }
                if (PyList_Append(list, item)) {
                    Exception::from_python();
                }
                Py_DECREF(item);
            }
        } catch (...) {
            Py_DECREF(list);
            throw;
        }
        m_ptr = PyList_AsTuple(list);
        Py_DECREF(list);
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /////////////////////////////
    ////    C++ INTERFACE    ////
    /////////////////////////////

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

    ////////////////////////////////
    ////    PYTHON INTERFACE    ////
    ////////////////////////////////

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

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

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


template <std::derived_from<impl::TupleTag> From, impl::tuple_like To>
    requires (impl::pybind11_like<To> && !From::template typecheck<To>())
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

    template <typename T>
    [[nodiscard]] static consteval bool typecheck() {
        return false;  // TODO: implement
    }

    template <typename T>
    [[nodiscard]] static constexpr bool typecheck(const T& obj) {
        return false;  // TODO: implement
    }

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
