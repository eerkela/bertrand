#if !defined(BERTRAND_PYTHON_INCLUDED) && !defined(LINTER)
#error "This file should not be included directly.  Please include <bertrand/python.h> instead."
#endif

#ifndef BERTRAND_PYTHON_TUPLE_H
#define BERTRAND_PYTHON_TUPLE_H

#include "common.h"


// TODO: py::Struct replaces py::namedtuple

// TODO: when implementing py::Struct:

// template <std::derived_from<Object>... Args>
// struct Struct : public Tuple<
//     std::conditional_t<impl::homogenous<Args...>, impl::first<Args...>, Object>
// > {


// TODO: support py::deque?


namespace bertrand {
namespace py {


template <std::derived_from<impl::TupleTag> Self>
struct __getattr__<Self, "count">                               : Returns<Function<
    Int(typename Arg<"value", const Object&>::pos)
>> {};
template <std::derived_from<impl::TupleTag> Self>
struct __getattr__<Self, "index">                               : Returns<Function<
    Int(
        typename Arg<"value", const Object&>::pos,
        typename Arg<"start", const Int&>::pos::opt,
        typename Arg<"stop", const Int&>::pos::opt
    )
>> {};


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


/* Represents a statically-typed Python tuple in C++. */
template <typename Val>
class Tuple : public Object, public impl::TupleTag {
    using Base = Object;
    static_assert(
        std::derived_from<Val, Object>,
        "py::Tuple can only contain types derived from py::Object."
    );

    static constexpr bool generic = std::same_as<Val, Object>;

    template <typename T>
    static constexpr bool check_value_type = std::derived_from<T, Object> ?
        std::derived_from<T, Val> : std::convertible_to<T, Val>;

    template <typename T>
    struct std_tuple_check {
        static constexpr bool match = false;
    };

    template <typename First, typename Second>
    struct std_tuple_check<std::pair<First, Second>> {
        static constexpr bool match = true;
        static constexpr bool value = check_value_type<First> && check_value_type<Second>;
    };

    template <typename... Args>
    struct std_tuple_check<std::tuple<Args...>> {
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
    static consteval bool typecheck() {
        using U = std::decay_t<T>;
        if constexpr (!impl::tuple_like<U>) {
            return false;
        } else if constexpr (impl::pybind11_like<U>) {
            return generic;
        } else if constexpr (impl::is_iterable<U>) {
            return check_value_type<impl::iter_type<U>>;
        } else if constexpr (std_tuple_check<U>::match) {
            return std_tuple_check<U>::value;
        } else {
            return false;
        }
    }

    template <typename T>
    static constexpr bool typecheck(const T& obj) {
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

    Tuple(Handle h, const borrowed_t& t) : Base(h, t) {}
    Tuple(Handle h, const stolen_t& t) : Base(h, t) {}

    template <typename Policy>
    Tuple(const pybind11::detail::accessor<Policy>& accessor) :
        Base(Base::from_pybind11_accessor<Tuple>(accessor).release(), stolen_t{})
    {}

    /* Default constructor.  Initializes to an empty tuple. */
    Tuple() : Base(PyTuple_New(0), stolen_t{}) {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

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

    /* Copy/move constructors from equivalent pybind11 types or other tuples with a
    narrower value type. */
    template <impl::python_like T> requires (typecheck<T>())
    Tuple(T&& other) : Base(std::forward<T>(other)) {}

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

    /////////////////////////
    ////    INTERFACE    ////
    /////////////////////////

    /* Get the underlying PyObject* array. */
    PyObject** DATA() const noexcept {
        return PySequence_Fast_ITEMS(this->ptr());
    }

    /* Directly access an item without bounds checking or constructing a proxy. */
    value_type GET_ITEM(Py_ssize_t index) const {
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
    size_t count(
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
    size_t index(
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

    friend Tuple operator+(
        const Tuple& self,
        const std::initializer_list<value_type>& items
    ) {
        return self.concat(items);
    }

    friend Tuple operator+(
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
            PyObject** array = DATA();
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


}  // namespace py
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_TUPLE_H
