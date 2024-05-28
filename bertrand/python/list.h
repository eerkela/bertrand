#if !defined(BERTRAND_PYTHON_INCLUDED) && !defined(LINTER)
#error "This file should not be included directly.  Please include <bertrand/python.h> instead."
#endif

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


template <std::derived_from<impl::ListTag> Self>
struct __getattr__<Self, "append">                              : Returns<Function<
    void(typename Arg<"value", const typename Self::value_type&>::pos)
>> {};
template <std::derived_from<impl::ListTag> Self>
struct __getattr__<Self, "extend">                              : Returns<Function<
    void(typename Arg<"iterable", const Object&>::pos)
>> {};
template <std::derived_from<impl::ListTag> Self>
struct __getattr__<Self, "insert">                              : Returns<Function<
    void(
        typename Arg<"index", const Int&>::pos,
        typename Arg<"value", const typename Self::value_type&>::pos
    )
>> {};
template <std::derived_from<impl::ListTag> Self>
struct __getattr__<Self, "copy">                                : Returns<Function<
    Self()
>> {};
template <std::derived_from<impl::ListTag> Self>
struct __getattr__<Self, "count">                               : Returns<Function<
    Int(typename Arg<"value", const typename Self::value_type&>::pos)
>> {};
template <std::derived_from<impl::ListTag> Self>
struct __getattr__<Self, "index">                               : Returns<Function<
    Int(
        typename Arg<"value", const typename Self::value_type&>::pos,
        typename Arg<"start", const Int&>::pos::opt,
        typename Arg<"stop", const Int&>::pos::opt
    )
>> {};
template <std::derived_from<impl::ListTag> Self>
struct __getattr__<Self, "clear">                               : Returns<Function<
    void()
>> {};
template <std::derived_from<impl::ListTag> Self>
struct __getattr__<Self, "remove">                              : Returns<Function<
    void(typename Arg<"value", const typename Self::value_type&>::pos)
>> {};
template <std::derived_from<impl::ListTag> Self>
struct __getattr__<Self, "pop">                                 : Returns<Function<
    typename Self::value_type(typename Arg<"index", const Int&>::pos::opt)
>> {};
template <std::derived_from<impl::ListTag> Self>
struct __getattr__<Self, "reverse">                             : Returns<Function<
    void()
>> {};
template <std::derived_from<impl::ListTag> Self>
struct __getattr__<Self, "sort">                                : Returns<Function<
    void(
        typename Arg<"key", const Function<Bool(const typename Self::value_type&)>&>::kw::opt,
        typename Arg<"reverse", const Bool&>::kw::opt
    )
>> {};


namespace ops {

    template <typename Return, std::derived_from<impl::ListTag> Self>
    struct len<Return, Self> {
        static size_t operator()(const Self& self) {
            return PyList_GET_SIZE(self.ptr());
        }
    };

    template <typename Return, std::derived_from<impl::ListTag> Self>
    struct begin<Return, Self> {
        static auto operator()(const Self& self) {
            return impl::Iterator<impl::ListIter<Return>>(self, 0);
        }
    };

    template <typename Return, std::derived_from<impl::ListTag> Self>
    struct end<Return, Self> {
        static auto operator()(const Self& self) {
            return impl::Iterator<impl::ListIter<Return>>(PyList_GET_SIZE(self.ptr()));
        }
    };

    template <typename Return, std::derived_from<impl::ListTag> Self>
    struct rbegin<Return, Self> {
        static auto operator()(const Self& self) {
            return impl::ReverseIterator<impl::ListIter<Return>>(
                self,
                PyList_GET_SIZE(self.ptr()) - 1
            );
        }
    };

    template <typename Return, std::derived_from<impl::ListTag> Self>
    struct rend<Return, Self> {
        static auto operator()(const Self& self) {
            return impl::ReverseIterator<impl::ListIter<Return>>(-1);
        }
    };

    template <typename Return, typename L, typename R>
        requires (std::derived_from<L, impl::ListTag> || std::derived_from<R, impl::ListTag>)
    struct add<Return, L, R> : sequence::add<Return, L, R> {};

    template <typename Return, std::derived_from<impl::ListTag> L, typename R>
    struct iadd<Return, L, R> : sequence::iadd<Return, L, R> {};

    template <typename Return, typename L, typename R>
        requires (std::derived_from<L, impl::ListTag> || std::derived_from<R, impl::ListTag>)
    struct mul<Return, L, R> : sequence::mul<Return, L, R> {};

    template <typename Return, std::derived_from<impl::ListTag> L, typename R>
    struct imul<Return, L, R> : sequence::imul<Return, L, R> {};

}


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
    static_assert(
        std::derived_from<Val, Object>,
        "py::List can only contain types derived from py::Object."
    );

    static constexpr bool generic = std::same_as<Val, Object>;

    template <typename T>
    static constexpr bool check_value_type = std::derived_from<T, Object> ?
        std::derived_from<T, Val> : std::convertible_to<T, Val>;

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

    template <typename T>
    [[nodiscard]] static consteval bool typecheck() {
        if constexpr (!impl::list_like<std::decay_t<T>>) {
            return false;
        } else if constexpr (impl::pybind11_like<std::decay_t<T>>) {
            return generic;
        } else if constexpr (impl::is_iterable<std::decay_t<T>>) {
            return check_value_type<impl::iter_type<std::decay_t<T>>>;
        } else {
            return false;
        }
    }

    template <typename T>
    [[nodiscard]] static constexpr bool typecheck(const T& obj) {
        if constexpr (impl::cpp_like<T>) {
            return typecheck<T>();

        } else if constexpr (impl::is_object_exact<T>) {
            if constexpr (generic) {
                return obj.ptr() != nullptr && PyList_Check(obj.ptr());
            } else {
                return (
                    obj.ptr() != nullptr && PyTuple_Check(obj.ptr()) &&
                    std::ranges::all_of(obj, [](const auto& item) {
                        return value_type::typecheck(item);
                    })
                );
            }

        } else if constexpr (
            std::derived_from<T, List<Object>> ||
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

        } else if constexpr (impl::list_like<T>) {
            return obj.ptr() != nullptr && check_value_type<impl::iter_type<T>>;

        } else {
            return false;
        }
    }

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    List(Handle h, const borrowed_t& t) : Base(h, t) {}
    List(Handle h, const stolen_t& t) : Base(h, t) {}

    template <typename Policy>
    List(const pybind11::detail::accessor<Policy>& accessor) :
        Base(Base::from_pybind11_accessor<List>(accessor).release(), stolen_t{})
    {}

    /* Default constructor.  Initializes to an empty list. */
    List() : Base(PyList_New(0), stolen_t{}) {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /* Pack the contents of a braced initializer list into a new Python list. */
    List(const std::initializer_list<value_type>& contents) :
        Base(PyList_New(contents.size()), stolen_t{})
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

    /* Copy/move constructors from equivalent pybind11 types or other lists with a
    narrower value type. */
    template <impl::python_like T> requires (typecheck<T>())
    List(T&& other) : Base(std::forward<T>(other)) {}

    /* Explicitly unpack a generic Python container into a py::List. */
    template <impl::python_like T> requires (!impl::list_like<T> && impl::is_iterable<T>)
    explicit List(const T& contents) : Base(nullptr, stolen_t{}) {
        if constexpr (generic) {
            m_ptr = PySequence_List(contents.ptr());
            if (m_ptr == nullptr) {
                Exception::from_python();
            }
        } else {
            if constexpr (impl::has_size<T>) {
                m_ptr = PyList_New(std::size(contents));
                if (m_ptr == nullptr) {
                    Exception::from_python();
                }
                try {
                    size_t i = 0;
                    for (const auto& item : contents) {
                        PyList_SET_ITEM(m_ptr, i++, value_type(item).release().ptr());
                    }
                } catch (...) {
                    Py_DECREF(m_ptr);
                    throw;
                }
            } else {
                m_ptr = PyList_New(0);
                if (m_ptr == nullptr) {
                    Exception::from_python();
                }
                try {
                    for (const auto& item : contents) {
                        if (PyList_Append(m_ptr, value_type(item).ptr())) {
                            Exception::from_python();
                        }
                    }
                } catch (...) {
                    Py_DECREF(m_ptr);
                    throw;
                }
            }
        }
    }

    /* Explicitly unpack a generic C++ container into a new py::List. */
    template <impl::cpp_like T> requires (impl::is_iterable<T>)
    explicit List(const T& contents) : Base(nullptr, stolen_t{}) {
        if constexpr (impl::has_size<T>) {
            size_t size = std::size(contents);
            m_ptr = PyList_New(size);
            if (m_ptr == nullptr) {
                Exception::from_python();
            }
            try {
                size_t i = 0;
                for (const auto& item : contents) {
                    PyList_SET_ITEM(m_ptr, i++, value_type(item).release().ptr());
                }
            } catch (...) {
                Py_DECREF(m_ptr);
                throw;
            }
        } else {
            m_ptr = PyList_New(0);
            if (m_ptr == nullptr) {
                Exception::from_python();
            }
            try {
                for (const auto& item : contents) {
                    if (PyList_Append(m_ptr, value_type(item).ptr())) {
                        Exception::from_python();
                    }
                }
            } catch (...) {
                Py_DECREF(m_ptr);
                throw;
            }
        }
    }

    /* Construct a new list from a pair of input iterators. */
    template <typename Iter, std::sentinel_for<Iter> Sentinel>
    explicit List(Iter first, Sentinel last) : Base(PyList_New(0), stolen_t{}) {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
        try {
            while (first != last) {
                if (PyList_Append(m_ptr, value_type(*first).ptr())) {
                    Exception::from_python();
                }
                ++first;
            }
        } catch (...) {
            Py_DECREF(m_ptr);
            throw;
        }
    }

    /* Explicitly unpack a std::pair into a py::List. */
    template <typename First, typename Second>
        requires (
            std::constructible_from<value_type, First> &&
            std::constructible_from<value_type, Second>
        )
    explicit List(const std::pair<First, Second>& pair) :
        Base(PyList_New(2), stolen_t{})
    {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
        try {
            PyList_SET_ITEM(m_ptr, 0, value_type(pair.first).release().ptr());
            PyList_SET_ITEM(m_ptr, 1, value_type(pair.second).release().ptr());
        } catch (...) {
            Py_DECREF(m_ptr);
            throw;
        }
    }

    /* Explicitly unpack a std::tuple into a py::List. */
    template <typename... Args> requires (std::constructible_from<value_type, Args> && ...)
    explicit List(const std::tuple<Args...>& tuple) :
        Base(PyList_New(sizeof...(Args)), stolen_t{})
    {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }

        auto unpack_tuple = [&]<size_t... Ns>(std::index_sequence<Ns...>) {
            (
                PyList_SET_ITEM(
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
    explicit List(const char (&string)[N]) : Base(PyList_New(N - 1), stolen_t{}) {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
        try {
            for (size_t i = 0; i < N - 1; ++i) {
                PyObject* item = PyUnicode_FromStringAndSize(string + i, 1);
                if (item == nullptr) {
                    Exception::from_python();
                }
                PyList_SET_ITEM(m_ptr, i, item);
            }
        } catch (...) {
            Py_DECREF(m_ptr);
            throw;
        }
    }

    /* Explicitly unpack a C++ string pointer into a py::Tuple. */
    template <std::same_as<const char*> T> requires (generic || std::same_as<value_type, Str>)
    explicit List(T string) : Base(PyList_New(0), stolen_t{}) {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
        try {
            for (const char* ptr = string; *ptr != '\0'; ++ptr) {
                PyObject* item = PyUnicode_FromStringAndSize(ptr, 1);
                if (item == nullptr) {
                    Exception::from_python();
                }
                if (PyList_Append(m_ptr, item)) {
                    Exception::from_python();
                }
                Py_DECREF(item);
            }
        } catch (...) {
            Py_DECREF(m_ptr);
            throw;
        }
    }

    /////////////////////////
    ////    INTERFACE    ////
    /////////////////////////

    /* Get the underlying PyObject* array. */
    [[nodiscard]] PyObject** DATA() const noexcept {
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
    void extend(const T& items);

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

    /* Equivalent to Python `list.remove(value)`. */
    void remove(const value_type& value);

    /* Equivalent to Python `list.pop([index])`. */
    value_type pop(Py_ssize_t index = -1);

    /* Equivalent to Python `list.reverse()`. */
    void reverse() {
        if (PyList_Reverse(this->ptr())) {
            Exception::from_python();
        }
    }

    // TODO: can potentially unify sort() overloads using optional keyword arguments?

    /* Equivalent to Python `list.sort()`. */
    void sort() {
        if (PyList_Sort(this->ptr())) {
            Exception::from_python();
        }
    }

    /* Equivalent to Python `list.sort(reverse=reverse)`. */
    void sort(const Bool& reverse);

    /* Equivalent to Python `list.sort(key=key[, reverse=reverse])`.  The key function
    can be given as any C++ function-like object, but users should note that pybind11
    has a hard time parsing generic arguments, so templates and the `auto` keyword
    should be avoided. */
    void sort(const Function<const value_type&>& key, const Bool& reverse = false);

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

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
            PyObject** array = DATA();
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


}  // namespace py
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_LIST_H
