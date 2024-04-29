#if !defined(BERTRAND_PYTHON_INCLUDED) && !defined(LINTER)
#error "This file should not be included directly.  Please include <bertrand/python.h> instead."
#endif

#ifndef BERTRAND_PYTHON_LIST_H
#define BERTRAND_PYTHON_LIST_H

#include "common.h"
#include "bool.h"


namespace bertrand {
namespace py {


template <>
struct __getattr__<List, "append">                          : Returns<Function> {};
template <>
struct __getattr__<List, "extend">                          : Returns<Function> {};
template <>
struct __getattr__<List, "insert">                          : Returns<Function> {};
template <>
struct __getattr__<List, "copy">                            : Returns<Function> {};
template <>
struct __getattr__<List, "clear">                           : Returns<Function> {};
template <>
struct __getattr__<List, "remove">                          : Returns<Function> {};
template <>
struct __getattr__<List, "pop">                             : Returns<Function> {};
template <>
struct __getattr__<List, "reverse">                         : Returns<Function> {};
template <>
struct __getattr__<List, "sort">                            : Returns<Function> {};
template <>
struct __getattr__<List, "count">                           : Returns<Function> {};
template <>
struct __getattr__<List, "index">                           : Returns<Function> {};

template <>
struct __len__<List>                                        : Returns<size_t> {};
template <>
struct __iter__<List>                                       : Returns<Object> {};
template <>
struct __reversed__<List>                                   : Returns<Object> {};
template <typename T>
struct __contains__<List, T>                                : Returns<bool> {};
template <impl::int_like T>
struct __getitem__<List, T>                                 : Returns<Object> {};
template <>
struct __getitem__<List, Slice>                             : Returns<List> {};
template <impl::int_like Key, typename Value>
struct __setitem__<List, Key, Value>                        : Returns<void> {};
template <impl::list_like Value>
struct __setitem__<List, Slice, Value>                      : Returns<void> {};
template <impl::int_like Key>
struct __delitem__<List, Key>                               : Returns<void> {};
template <>
struct __delitem__<List, Slice>                             : Returns<void> {};
template <>
struct __lt__<List, Object>                                 : Returns<bool> {};
template <impl::list_like T>
struct __lt__<List, T>                                      : Returns<bool> {};
template <>
struct __le__<List, Object>                                 : Returns<bool> {};
template <impl::list_like T>
struct __le__<List, T>                                      : Returns<bool> {};
template <>
struct __ge__<List, Object>                                 : Returns<bool> {};
template <impl::list_like T>
struct __ge__<List, T>                                      : Returns<bool> {};
template <>
struct __gt__<List, Object>                                 : Returns<bool> {};
template <impl::list_like T>
struct __gt__<List, T>                                      : Returns<bool> {};
template <>
struct __add__<List, Object>                                : Returns<List> {};
template <impl::list_like T>
struct __add__<List, T>                                     : Returns<List> {};
template <>
struct __iadd__<List, Object>                               : Returns<List&> {};
template <impl::list_like T>
struct __iadd__<List, T>                                    : Returns<List&> {};
template <>
struct __mul__<List, Object>                                : Returns<List> {};
template <impl::int_like T>
struct __mul__<List, T>                                     : Returns<List> {};
template <>
struct __imul__<List, Object>                               : Returns<List&> {};
template <impl::int_like T>
struct __imul__<List, T>                                    : Returns<List&> {};


/* Represents a statically-typed Python list in C++. */
class List : public Object, public impl::SequenceOps<List>, public impl::ListTag {
    using Base = Object;

public:
    static const Type type;

    using size_type = size_t;
    using difference_type = std::ptrdiff_t;
    using value_type = Object;  // TODO: CTAD
    using pointer = value_type*;
    using const_pointer = const value_type*;
    using reference = value_type&;
    using const_reference = const value_type&;
    using iterator = impl::Iterator<impl::ListIter<value_type>>;
    using const_iterator = impl::Iterator<impl::ListIter<const value_type>>;
    using reverse_iterator = impl::ReverseIterator<impl::ListIter<value_type>>;
    using const_reverse_iterator = impl::ReverseIterator<impl::ListIter<const value_type>>;

    // TODO: When implementing CTAD, check() should account for the member type.
    // pybind11 types should only be considered matches if and only if the value type
    // is set to py::Object.

    template <typename T>
    static consteval bool check() {
        return impl::list_like<T>;
    }

    template <typename T>
    static constexpr bool check(const T& obj) {
        if constexpr (impl::python_like<T>) {
            return obj.ptr() != nullptr && PyList_Check(obj.ptr());
        } else {
            return check<T>();
        }
    }

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    List(Handle h, const borrowed_t& t) : Base(h, t) {}
    List(Handle h, const stolen_t& t) : Base(h, t) {}

    template <impl::pybind11_like T> requires (check<T>())
    List(T&& other) : Base(std::forward<T>(other)) {}

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
    List(const std::initializer_list<Object>& contents) :
        Base(PyList_New(contents.size()), stolen_t{})
    {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
        try {
            size_t i = 0;
            for (const Object& item : contents) {
                PyList_SET_ITEM(m_ptr, i++, Object(item).release().ptr());
            }
        } catch (...) {
            Py_DECREF(m_ptr);
            throw;
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
                append(*first);
                ++first;
            }
        } catch (...) {
            Py_DECREF(m_ptr);
            throw;
        }
    }

    /* Explicitly unpack an arbitrary Python container into a new py::List. */
    template <impl::python_like T> requires (!impl::list_like<T> && impl::is_iterable<T>)
    explicit List(const T& contents) :
        Base(PySequence_List(contents.ptr()), stolen_t{})
    {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /* Explicitly unpack a generic C++ container into a new py::List. */
    template <impl::cpp_like T> requires (impl::is_iterable<T>)
    explicit List(T&& contents) : Base(nullptr, stolen_t{}) {
        if constexpr (impl::has_size<T>) {
            size_t size = std::size(contents);
            m_ptr = PyList_New(size);  // TODO: this can potentially create an empty list
            if (m_ptr == nullptr) {
                Exception::from_python();
            }
            try {
                size_t i = 0;
                for (const auto& item : contents) {
                    PyList_SET_ITEM(m_ptr, i++, Object(item).release().ptr());
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
                    append(item);
                }
            } catch (...) {
                Py_DECREF(m_ptr);
                throw;
            }
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
            PyList_SET_ITEM(m_ptr, 0, Object(pair.first).release().ptr());
            PyList_SET_ITEM(m_ptr, 1, Object(pair.second).release().ptr());
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
        auto unpack_tuple = []<typename... As, size_t... Ns>(
            PyObject* result,
            const std::tuple<Args...>& tuple,
            std::index_sequence<Ns...>
        ) {
            (
                PyList_SET_ITEM(
                    result,
                    Ns,
                    Object(std::get<Ns>(tuple)).release().ptr()
                ),
                ...
            );
        };

        if (m_ptr == nullptr) {
            Exception::from_python();
        }
        try {
            unpack_tuple(m_ptr, tuple, std::index_sequence_for<Args...>{});
        } catch (...) {
            Py_DECREF(m_ptr);
            throw;
        }
    }

    /////////////////////////////
    ////    C++ INTERFACE    ////
    /////////////////////////////

    /* Get the underlying PyObject* array. */
    inline PyObject** data() const noexcept {
        return PySequence_Fast_ITEMS(this->ptr());
    }

    /* Directly access an item without bounds checking or constructing a proxy. */
    inline Object GET_ITEM(Py_ssize_t index) const {
        return reinterpret_borrow<Object>(PyList_GET_ITEM(this->ptr(), index));
    }

    /* Directly set an item without bounds checking or constructing a proxy.
    
    NOTE: This steals a reference to `value` and does not clear the previous
    item if one is present.  This is dangerous, and should only be used to fill in
    a newly-allocated (empty) list. */
    inline void SET_ITEM(Py_ssize_t index, PyObject* value) {
        PyList_SET_ITEM(this->ptr(), index, value);
    }

    ////////////////////////////////
    ////    PYTHON INTERFACE    ////
    ////////////////////////////////

    /* Equivalent to Python `list.append(value)`. */
    inline void append(const Object& value) {
        if (PyList_Append(this->ptr(), value.ptr())) {
            Exception::from_python();
        }
    }

    /* Equivalent to Python `list.extend(items)`. */
    template <impl::is_iterable T>
    inline void extend(const T& items);

    /* Equivalent to Python `list.extend(items)`, where items are given as a braced
    initializer list. */
    inline void extend(const std::initializer_list<Object>& items) {
        for (const Object& item : items) {
            append(item);
        }
    }

    /* Equivalent to Python `list.insert(index, value)`. */
    inline void insert(Py_ssize_t index, const Object& value) {
        if (PyList_Insert(this->ptr(), index, value.ptr())) {
            Exception::from_python();
        }
    }

    /* Equivalent to Python `list.copy()`. */
    inline List copy() const {
        PyObject* result = PyList_GetSlice(this->ptr(), 0, size());
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<List>(result);
    }

    /* Equivalent to Python `list.clear()`. */
    inline void clear() {
        if (PyList_SetSlice(this->ptr(), 0, size(), nullptr)) {
            Exception::from_python();
        }
    }

    /* Equivalent to Python `list.remove(value)`. */
    inline void remove(const Object& value);

    /* Equivalent to Python `list.pop([index])`. */
    inline Object pop(Py_ssize_t index = -1);

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
        const std::initializer_list<Object>& items
    ) {
        return self.concat(items);
    }

    inline friend List operator+(
        const std::initializer_list<Object>& items,
        const List& self
    ) {
        return self.concat(items);
    }

    inline friend List& operator+=(
        List& self,
        const std::initializer_list<Object>& items
    ) {
        self.extend(items);
        return self;
    }

protected:

    inline List concat(const std::initializer_list<Object>& items) const {
        PyObject* result = PyList_New(size() + items.size());
        if (result == nullptr) {
            Exception::from_python();
        }
        try {
            size_t i = 0;
            size_t length = size();
            PyObject** array = data();
            while (i < length) {
                PyList_SET_ITEM(result, i, Py_NewRef(array[i]));
                ++i;
            }
            for (const Object& item : items) {
                PyList_SET_ITEM(result, i++, Object(item).release().ptr());
            }
            return reinterpret_steal<List>(result);
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
    }

};


/* Implicitly convert a py::List into a std::pair if and only if the list is of length
two and its contents are implicitly convertible to the member types. */
template <std::derived_from<impl::ListTag> Self, typename First, typename Second>
    requires (
        std::convertible_to<typename Self::value_type, First> &&
        std::convertible_to<typename Self::value_type, Second>
    )
struct __cast__<Self, std::pair<First, Second>> : Returns<std::pair<First, Second>> {
    static std::pair<First, Second> cast(const Self& self) {
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


/* Implicitly convert a py::List into a std::tuple if and only if the list is of length
equal to the tuple arguments, and its contents are implicitly convertible to the member
types.  */
template <std::derived_from<impl::ListTag> Self, typename... Args>
    requires (std::convertible_to<typename Self::value_type, Args> && ...)
struct __cast__<Self, std::tuple<Args...>> : Returns<std::tuple<Args...>> {
    static std::tuple<Args...> cast(const Self& self) {
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


/* Implicitly convert a py::List into a std::array if and only if the list is of the
specified length, and its contents are implicitly convertible to the array type. */
template <std::derived_from<impl::ListTag> Self, typename T, size_t N>
    requires (std::convertible_to<typename Self::value_type, T>)
struct __cast__<Self, std::array<T, N>> : Returns<std::array<T, N>> {
    static std::array<T, N> cast(const Self& self) {
        if (self.size() != N) {
            throw IndexError(
                "conversion to std::array requires list of size " +
                std::to_string(N) + ", not " +
                std::to_string(self.size())
            );
        }
        std::array<T, N> result;
        for (size_t i = 0; i < N; ++i) {
            result[i] = impl::implicit_cast<T>(self.GET_ITEM(i));
        }
        return result;
    }
};


/* Implicitly convert a py::List into a std::vector if its contents are convertible to
the vector type. */
template <std::derived_from<impl::ListTag> Self, typename T, typename... Args>
    requires (std::convertible_to<typename Self::value_type, T>)
struct __cast__<Self, std::vector<T, Args...>> : Returns<std::vector<T, Args...>> {
    static std::vector<T, Args...> cast(const Self& self) {
        std::vector<T, Args...> result;
        result.reserve(self.size());
        for (const auto& item : self) {
            result.push_back(impl::implicit_cast<T>(item));
        }
        return result;
    }
};


/* Implicitly convert a py::List into a std::list if its contents are convertible to
the list type. */
template <std::derived_from<impl::ListTag> Self, typename T, typename... Args>
    requires (std::convertible_to<typename Self::value_type, T>)
struct __cast__<Self, std::list<T, Args...>> : Returns<std::list<T, Args...>> {
    static std::list<T, Args...> cast(const Self& self) {
        std::list<T, Args...> result;
        for (const auto& item : self) {
            result.push_back(impl::implicit_cast<T>(item));
        }
        return result;
    }
};


/* Implicitly convert a py::List into a std::forward_list if its contents are
convertible to the list type. */
template <std::derived_from<impl::ListTag> Self, typename T, typename... Args>
    requires (std::convertible_to<typename Self::value_type, T>)
struct __cast__<Self, std::forward_list<T, Args...>> : Returns<std::forward_list<T, Args...>> {
    static std::forward_list<T, Args...> cast(const Self& self) {
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


/* Implicitly convert a py::List into a std::deque if its contents are convertible to
the deque type. */
template <std::derived_from<impl::ListTag> Self, typename T, typename... Args>
    requires (std::convertible_to<typename Self::value_type, T>)
struct __cast__<Self, std::deque<T, Args...>> : Returns<std::deque<T, Args...>> {
    static std::deque<T, Args...> cast(const Self& self) {
        std::deque<T, Args...> result;
        for (const auto& item : self) {
            result.push_back(impl::implicit_cast<T>(item));
        }
        return result;
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


}  // namespace py
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_LIST_H
