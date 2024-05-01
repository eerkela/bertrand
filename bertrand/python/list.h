#if !defined(BERTRAND_PYTHON_INCLUDED) && !defined(LINTER)
#error "This file should not be included directly.  Please include <bertrand/python.h> instead."
#endif

#ifndef BERTRAND_PYTHON_LIST_H
#define BERTRAND_PYTHON_LIST_H

#include "common.h"
#include "bool.h"


namespace bertrand {
namespace py {


template <std::derived_from<impl::ListTag> Self>
struct __getattr__<Self, "append">                          : Returns<Function> {};
template <std::derived_from<impl::ListTag> Self>
struct __getattr__<Self, "extend">                          : Returns<Function> {};
template <std::derived_from<impl::ListTag> Self>
struct __getattr__<Self, "insert">                          : Returns<Function> {};
template <std::derived_from<impl::ListTag> Self>
struct __getattr__<Self, "copy">                            : Returns<Function> {};
template <std::derived_from<impl::ListTag> Self>
struct __getattr__<Self, "count">                           : Returns<Function> {};
template <std::derived_from<impl::ListTag> Self>
struct __getattr__<Self, "index">                           : Returns<Function> {};
template <std::derived_from<impl::ListTag> Self>
struct __getattr__<Self, "clear">                           : Returns<Function> {};
template <std::derived_from<impl::ListTag> Self>
struct __getattr__<Self, "remove">                          : Returns<Function> {};
template <std::derived_from<impl::ListTag> Self>
struct __getattr__<Self, "pop">                             : Returns<Function> {};
template <std::derived_from<impl::ListTag> Self>
struct __getattr__<Self, "reverse">                         : Returns<Function> {};
template <std::derived_from<impl::ListTag> Self>
struct __getattr__<Self, "sort">                            : Returns<Function> {};


template <typename T>
List(const std::initializer_list<T>&) -> List<Object>;
template <typename T, typename... Args>
    requires (!std::derived_from<std::decay_t<T>, impl::ListTag>)
List(T&&, Args&&...) -> List<Object>;
template <typename T>
List(const List<T>&) -> List<T>;
template <typename T>
List(List<T>&&) -> List<T>;
template <impl::str_like T>
explicit List(T string) -> List<Str>;


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
    static constexpr bool typecheck = std::derived_from<T, Object> ?
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
    static consteval bool check() {
        if constexpr (!impl::list_like<std::decay_t<T>>) {
            return false;

        // pybind11 lists only match if this list is generc
        } else if constexpr (impl::pybind11_like<std::decay_t<T>>) {
            return generic;

        // if container has a value_type alias, check if it's convertible to the list's
        // value type
        } else {
            static_assert(
                impl::has_value_type<std::decay_t<T>>,
                "py::List can only be constructed from containers with a value_type "
                "alias."
            );
            return typecheck<typename std::decay_t<T>::value_type>;
        }
    }

    template <typename T>
    static constexpr bool check(const T& obj) {
        if constexpr (impl::cpp_like<T>) {
            return check<T>();

        } else if constexpr (impl::is_object_exact<T>) {
            if constexpr (generic) {
                return obj.ptr() != nullptr && PyList_Check(obj.ptr());
            } else {
                return (
                    obj.ptr() != nullptr && PyTuple_Check(obj.ptr()) &&
                    std::ranges::all_of(obj, [](const auto& item) {
                        return value_type::check(item);
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
                        return value_type::check(item);
                    })
                );
            }

        } else if constexpr (impl::list_like<T>) {
            return obj.tr() != nullptr && typecheck<typename T::value_type>;

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
    template <impl::python_like T> requires (check<T>())
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
                for (auto&& item : contents) {
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
    template <typename... Args>
    explicit List(const std::tuple<Args...>& tuple) :
        Base(PyList_New(sizeof...(Args)), stolen_t{})
    {
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

        if (m_ptr == nullptr) {
            Exception::from_python();
        }
        try {
            unpack_tuple(std::index_sequence_for<Args...>{});
        } catch (...) {
            Py_DECREF(m_ptr);
            throw;
        }
    }

    /* Explicitly unpack a C++ string literal into a py::Tuple. */
    template <size_t N> requires (generic || impl::str_like<value_type>)
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
    template <std::same_as<const char*> T> requires (generic || impl::str_like<value_type>)
    explicit List(T string) : Base(PyList_New(0), stolen_t{}) {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
        try {
            const char* curr = string;
            while (*curr != '\0') {
                PyObject* item = PyUnicode_FromStringAndSize(curr++, 1);
                if (item == nullptr) {
                    Exception::from_python();
                }
                if (PyList_Append(m_ptr, item)) {
                    Exception::from_python();
                }
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
    inline PyObject** DATA() const noexcept {
        return PySequence_Fast_ITEMS(this->ptr());
    }

    /* Directly access an item without bounds checking or constructing a proxy. */
    inline value_type GET_ITEM(Py_ssize_t index) const {
        return reinterpret_borrow<value_type>(PyList_GET_ITEM(this->ptr(), index));
    }

    /* Directly set an item without bounds checking or constructing a proxy.  */
    inline void SET_ITEM(Py_ssize_t index, PyObject* value) {
        PyObject* prev = PyList_GET_ITEM(this->ptr(), index);
        PyList_SET_ITEM(this->ptr(), index, value);
        Py_XDECREF(prev);
    }

    /* Equivalent to Python `list.append(value)`. */
    inline void append(const value_type& value) {
        if (PyList_Append(this->ptr(), value.ptr())) {
            Exception::from_python();
        }
    }

    /* Equivalent to Python `list.extend(items)`. */
    template <impl::is_iterable T>
        requires (std::convertible_to<impl::dereference_type<T>, value_type>)
    inline void extend(const T& items);

    /* Equivalent to Python `list.extend(items)`, where items are given as a braced
    initializer list. */
    inline void extend(const std::initializer_list<value_type>& items) {
        for (const value_type& item : items) {
            append(item);
        }
    }

    /* Equivalent to Python `list.insert(index, value)`. */
    inline void insert(Py_ssize_t index, const value_type& value) {
        if (PyList_Insert(this->ptr(), index, value.ptr())) {
            Exception::from_python();
        }
    }

    /* Equivalent to Python `list.copy()`. */
    inline List copy() const {
        PyObject* result = PyList_GetSlice(this->ptr(), 0, PyList_GET_SIZE(this->ptr()));
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<List>(result);
    }

    /* Equivalent to Python `list.count(value)`, but also takes optional start/stop
    indices similar to `list.index()`. */
    inline Py_ssize_t count(
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
    inline Py_ssize_t index(
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
    inline void clear() {
        if (PyList_SetSlice(this->ptr(), 0, PyList_GET_SIZE(this->ptr()), nullptr)) {
            Exception::from_python();
        }
    }

    /* Equivalent to Python `list.remove(value)`. */
    inline void remove(const value_type& value);

    /* Equivalent to Python `list.pop([index])`. */
    inline value_type pop(Py_ssize_t index = -1);

    /* Equivalent to Python `list.reverse()`. */
    inline void reverse() {
        if (PyList_Reverse(this->ptr())) {
            Exception::from_python();
        }
    }

    /* Equivalent to Python `list.sort()`. */
    inline void sort() {
        if (PyList_Sort(this->ptr())) {
            Exception::from_python();
        }
    }

    /* Equivalent to Python `list.sort(reverse=reverse)`. */
    inline void sort(const Bool& reverse);

    /* Equivalent to Python `list.sort(key=key[, reverse=reverse])`.  The key function
    can be given as any C++ function-like object, but users should note that pybind11
    has a hard time parsing generic arguments, so templates and the `auto` keyword
    should be avoided. */
    inline void sort(const Function& key, const Bool& reverse = false);

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    inline friend List operator+(
        const List& self,
        const std::initializer_list<value_type>& items
    ) {
        return self.concat(items);
    }

    inline friend List operator+(
        const std::initializer_list<value_type>& items,
        const List& self
    ) {
        return self.concat(items);
    }

    inline friend List& operator+=(
        List& self,
        const std::initializer_list<value_type>& items
    ) {
        self.extend(items);
        return self;
    }

protected:

    inline List concat(const std::initializer_list<value_type>& items) const {
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


namespace impl {
namespace ops {

    template <typename Return, std::derived_from<ListTag> Self>
    struct len<Return, Self> {
        static size_t operator()(const Self& self) {
            return PyList_GET_SIZE(self.ptr());
        }
    };

    template <typename Return, std::derived_from<ListTag> Self>
    struct begin<Return, Self> {
        static auto operator()(const Self& self) {
            return impl::Iterator<impl::ListIter<Return>>(self, 0);
        }
    };

    template <typename Return, std::derived_from<ListTag> Self>
    struct end<Return, Self> {
        static auto operator()(const Self& self) {
            return impl::Iterator<impl::ListIter<Return>>(PyList_GET_SIZE(self.ptr()));
        }
    };

    template <typename Return, std::derived_from<ListTag> Self>
    struct rbegin<Return, Self> {
        static auto operator()(const Self& self) {
            return impl::ReverseIterator<impl::ListIter<Return>>(
                self,
                PyList_GET_SIZE(self.ptr()) - 1
            );
        }
    };

    template <typename Return, std::derived_from<ListTag> Self>
    struct rend<Return, Self> {
        static auto operator()(const Self& self) {
            return impl::ReverseIterator<impl::ListIter<Return>>(-1);
        }
    };

    template <typename Return, typename L, typename R>
        requires (std::derived_from<L, ListTag> || std::derived_from<R, ListTag>)
    struct add<Return, L, R> : sequence::add<Return, L, R> {};

    template <typename Return, std::derived_from<ListTag> L, typename R>
    struct iadd<Return, L, R> : sequence::iadd<Return, L, R> {};

    template <typename Return, typename L, typename R>
        requires (std::derived_from<L, ListTag> || std::derived_from<R, ListTag>)
    struct mul<Return, L, R> : sequence::mul<Return, L, R> {};

    template <typename Return, std::derived_from<ListTag> L, typename R>
    struct imul<Return, L, R> : sequence::imul<Return, L, R> {};

}
}


template <std::derived_from<impl::ListTag> Self>
struct __len__<Self>                                            : Returns<size_t> {};
template <std::derived_from<impl::ListTag> Self>
struct __iter__<Self>                                           : Returns<typename Self::value_type> {};
template <std::derived_from<impl::ListTag> Self>
struct __reversed__<Self>                                       : Returns<typename Self::value_type> {};
template <
    std::derived_from<impl::ListTag> Self,
    std::convertible_to<typename Self::value_type> Key
>
struct __contains__<Self, Key>                                  : Returns<bool> {};
template <std::derived_from<impl::ListTag> Self>
struct __getitem__<Self, Object>                                : Returns<Object> {};
template <std::derived_from<impl::ListTag> Self, impl::int_like Index>
struct __getitem__<Self, Index>                                 : Returns<typename Self::value_type> {};
template <std::derived_from<impl::ListTag> Self>
struct __getitem__<Self, Slice>                                 : Returns<Self> {};
template <
    std::derived_from<impl::ListTag> Self,
    std::convertible_to<typename Self::value_type> Value
>
struct __setitem__<Self, Object, Value>                         : Returns<void> {};
template <
    std::derived_from<impl::ListTag> Self,
    impl::int_like Key,
    std::convertible_to<typename Self::value_type> Value
>
struct __setitem__<Self, Key, Value>                            : Returns<void> {};
template <
    std::derived_from<impl::ListTag> Self,
    std::convertible_to<Self> Value  // use Self::check<T>()?
>
struct __setitem__<Self, Slice, Value>                          : Returns<void> {};
template <std::derived_from<impl::ListTag> Self>
struct __delitem__<Self, Object>                                : Returns<void> {};
template <std::derived_from<impl::ListTag> Self, impl::int_like Key>
struct __delitem__<Self, Key>                                   : Returns<void> {};
template <std::derived_from<impl::ListTag> Self>
struct __delitem__<Self, Slice>                                 : Returns<void> {};
template <std::derived_from<impl::ListTag> Self, impl::list_like T>
    requires (impl::Broadcast<impl::lt_comparable, Self, T>::value)
struct __lt__<Self, T>                                          : Returns<bool> {};
template <impl::list_like T, std::derived_from<impl::ListTag> Self>
    requires (
        !std::derived_from<T, impl::ListTag> &&
        impl::Broadcast<impl::lt_comparable, Self, T>::value
    )
struct __lt__<T, Self>                                          : Returns<bool> {};
template <std::derived_from<impl::ListTag> Self, impl::list_like T>
    requires (impl::Broadcast<impl::le_comparable, Self, T>::value)
struct __le__<Self, T>                                          : Returns<bool> {};
template <impl::list_like T, std::derived_from<impl::ListTag> Self>
    requires (
        !std::derived_from<T, impl::ListTag> &&
        impl::Broadcast<impl::le_comparable, Self, T>::value
    )
struct __le__<T, Self>                                          : Returns<bool> {};
template <std::derived_from<impl::ListTag> Self, impl::list_like T>
    requires (impl::Broadcast<impl::eq_comparable, Self, T>::value)
struct __eq__<Self, T>                                          : Returns<bool> {};
template <impl::list_like T, std::derived_from<impl::ListTag> Self>
    requires (
        !std::derived_from<T, impl::ListTag> &&
        impl::Broadcast<impl::eq_comparable, Self, T>::value
    )
struct __eq__<T, Self>                                          : Returns<bool> {};
template <std::derived_from<impl::ListTag> Self, impl::list_like T>
    requires (impl::Broadcast<impl::ne_comparable, Self, T>::value)
struct __ne__<Self, T>                                          : Returns<bool> {};
template <impl::list_like T, std::derived_from<impl::ListTag> Self>
    requires (
        !std::derived_from<T, impl::ListTag> &&
        impl::Broadcast<impl::ne_comparable, Self, T>::value
    )
struct __ne__<T, Self>                                          : Returns<bool> {};
template <std::derived_from<impl::ListTag> Self, impl::list_like T>
    requires (impl::Broadcast<impl::ge_comparable, Self, T>::value)
struct __ge__<Self, T>                                          : Returns<bool> {};
template <impl::list_like T, std::derived_from<impl::ListTag> Self>
    requires (
        !std::derived_from<T, impl::ListTag> &&
        impl::Broadcast<impl::ge_comparable, Self, T>::value
    )
struct __ge__<T, Self>                                          : Returns<bool> {};
template <std::derived_from<impl::ListTag> Self, impl::list_like T>
    requires (impl::Broadcast<impl::gt_comparable, Self, T>::value)
struct __gt__<Self, T>                                          : Returns<bool> {};
template <impl::list_like T, std::derived_from<impl::ListTag> Self>
    requires (
        !std::derived_from<T, impl::ListTag> &&
        impl::Broadcast<impl::gt_comparable, Self, T>::value
    )
struct __gt__<T, Self>                                          : Returns<bool> {};
template <std::derived_from<impl::ListTag> Self, typename T>
    requires (Self::template check<T>())
struct __add__<Self, T>                                         : Returns<Self> {};
template <
    std::derived_from<impl::ListTag> T,
    std::derived_from<impl::ListTag> Self
> requires (!T::template check<Self>() && Self::template check<T>())
struct __add__<T, Self>                                         : Returns<Self> {};
template <std::derived_from<impl::ListTag> Self, typename T>
    requires (Self::template check<T>())
struct __iadd__<Self, T>                                        : Returns<Self&> {};
template <std::derived_from<impl::ListTag> Self, impl::int_like T>
struct __mul__<Self, T>                                         : Returns<Self> {};
template <impl::int_like T, std::derived_from<impl::ListTag> Self>
struct __mul__<T, Self>                                         : Returns<Self> {};
template <std::derived_from<impl::ListTag> Self, impl::int_like T>
struct __imul__<Self, T>                                        : Returns<Self&> {};


template <std::derived_from<impl::ListTag> Self, impl::list_like T>
    requires (impl::pybind11_like<T> && !Self::template check<T>())
struct __cast__<Self, T> : Returns<T> {
    static T operator()(const Self& self) {
        return reinterpret_borrow<T>(self.ptr());
    }
};


template <std::derived_from<impl::ListTag> Self, typename First, typename Second>
    requires (
        std::convertible_to<typename Self::value_type, First> &&
        std::convertible_to<typename Self::value_type, Second>
    )
struct __cast__<Self, std::pair<First, Second>> : Returns<std::pair<First, Second>> {
    static std::pair<First, Second> operator()(const Self& self) {
        if (self.size() != 2) {
            throw IndexError(
                "conversion to std::pair requires list of size 2, not "
                + std::to_string(self.size())
            );
        }
        return {
            impl::implicit_cast<First>(self.GET_ITEM(0)),
            impl::implicit_cast<Second>(self.GET_ITEM(1))
        };
    }
};


template <std::derived_from<impl::ListTag> Self, typename... Args>
    requires (std::convertible_to<typename Self::value_type, Args> && ...)
struct __cast__<Self, std::tuple<Args...>> : Returns<std::tuple<Args...>> {
    static std::tuple<Args...> operator()(const Self& self) {
        if (self.size() != sizeof...(Args)) {
            throw IndexError(
                "conversion to std::tuple requires list of size " +
                std::to_string(sizeof...(Args)) + ", not " +
                std::to_string(self.size())
            );
        }
        return [&]<size_t... N>(std::index_sequence<N...>) {
            return std::make_tuple(
                impl::implicit_cast<Args>(self.GET_ITEM(N))...
            );
        }(std::index_sequence_for<Args...>{});
    }
};


template <std::derived_from<impl::ListTag> Self, typename T, size_t N>
    requires (std::convertible_to<typename Self::value_type, T>)
struct __cast__<Self, std::array<T, N>> : Returns<std::array<T, N>> {
    static std::array<T, N> operator()(const Self& self) {
        if (N != self.size()) {
            throw IndexError(
                "conversion to std::array requires list of size " +
                std::to_string(N) + ", not " + std::to_string(self.size())
            );
        }
        std::array<T, N> result;
        for (size_t i = 0; i < N; ++i) {
            result[i] = impl::implicit_cast<T>(self.GET_ITEM(i));
        }
        return result;
    }
};


template <std::derived_from<impl::ListTag> Self, typename T, typename... Args>
    requires (std::convertible_to<typename Self::value_type, T>)
struct __cast__<Self, std::vector<T, Args...>> : Returns<std::vector<T, Args...>> {
    static std::vector<T, Args...> operator()(const Self& self) {
        std::vector<T, Args...> result;
        result.reserve(self.size());
        for (const auto& item : self) {
            result.push_back(impl::implicit_cast<T>(item));
        }
        return result;
    }
};


template <std::derived_from<impl::ListTag> Self, typename T, typename... Args>
    requires (std::convertible_to<typename Self::value_type, T>)
struct __cast__<Self, std::list<T, Args...>> : Returns<std::list<T, Args...>> {
    static std::list<T, Args...> operator()(const Self& self) {
        std::list<T, Args...> result;
        for (const auto& item : self) {
            result.push_back(impl::implicit_cast<T>(item));
        }
        return result;
    }
};


template <std::derived_from<impl::ListTag> Self, typename T, typename... Args>
    requires (std::convertible_to<typename Self::value_type, T>)
struct __cast__<Self, std::forward_list<T, Args...>> : Returns<std::forward_list<T, Args...>> {
    static std::forward_list<T, Args...> operator()(const Self& self) {
        std::forward_list<T, Args...> result;
        auto it = self.rbegin();
        auto end = self.rend();
        while (it != end) {
            result.push_front(impl::implicit_cast<T>(*it));
            ++it;
        }
        return result;
    }
};


template <std::derived_from<impl::ListTag> Self, typename T, typename... Args>
    requires (std::convertible_to<typename Self::value_type, T>)
struct __cast__<Self, std::deque<T, Args...>> : Returns<std::deque<T, Args...>> {
    static std::deque<T, Args...> operator()(const Self& self) {
        std::deque<T, Args...> result;
        for (const auto& item : self) {
            result.push_back(impl::implicit_cast<T>(item));
        }
        return result;
    }
};


}  // namespace py
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_LIST_H
