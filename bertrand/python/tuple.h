#if !defined(BERTRAND_PYTHON_INCLUDED) && !defined(LINTER)
#error "This file should not be included directly.  Please include <bertrand/python.h> instead."
#endif

#ifndef BERTRAND_PYTHON_TUPLE_H
#define BERTRAND_PYTHON_TUPLE_H

#include "common.h"


namespace bertrand {
namespace py {


template <typename Val>
struct __getattr__<Tuple<Val>, "count">                         : Returns<Function> {};
template <typename Val>
struct __getattr__<Tuple<Val>, "index">                         : Returns<Function> {};

template <typename Val>
struct __len__<Tuple<Val>>                                      : Returns<size_t> {};
template <typename Val>
struct __hash__<Tuple<Val>>                                     : Returns<size_t> {};
template <typename Val>
struct __iter__<Tuple<Val>>                                     : Returns<Val> {};
template <typename Val>
struct __reversed__<Tuple<Val>>                                 : Returns<Val> {};
template <typename Val, typename T>
struct __contains__<Tuple<Val>, T>                              : Returns<bool> {};
template <typename Val>
struct __getitem__<Tuple<Val>, Object>                          : Returns<Object> {};
template <typename Val, impl::int_like T>
struct __getitem__<Tuple<Val>, T>                               : Returns<Val> {};
template <typename Val>
struct __getitem__<Tuple<Val>, Slice>                           : Returns<Tuple<Val>> {};
template <typename Val>
struct __lt__<Tuple<Val>, Object>                               : Returns<bool> {};
template <typename Val, impl::tuple_like T>
struct __lt__<Tuple<Val>, T>                                    : Returns<bool> {};
template <typename Val>
struct __le__<Tuple<Val>, Object>                               : Returns<bool> {};
template <typename Val, impl::tuple_like T>
struct __le__<Tuple<Val>, T>                                    : Returns<bool> {};
template <typename Val>
struct __ge__<Tuple<Val>, Object>                               : Returns<bool> {};
template <typename Val, impl::tuple_like T>
struct __ge__<Tuple<Val>, T>                                    : Returns<bool> {};
template <typename Val>
struct __gt__<Tuple<Val>, Object>                               : Returns<bool> {};
template <typename Val, impl::tuple_like T>
struct __gt__<Tuple<Val>, T>                                    : Returns<bool> {};
template <typename Val>
struct __add__<Tuple<Val>, Object>                              : Returns<Tuple<Object>> {};  // TODO: return narrow tuple type?
template <typename Val, impl::tuple_like T>
struct __add__<Tuple<Val>, T>                                   : Returns<Tuple<Object>> {};  // TODO: return narrow tuple type?
template <typename Val>
struct __iadd__<Tuple<Val>, Object>                             : Returns<Tuple<Val>&> {};  // TODO: make sure types are compatible
template <typename Val, impl::tuple_like T>
struct __iadd__<Tuple<Val>, T>                                  : Returns<Tuple<Val>&> {};  // TODO: make sure types are compatible
template <typename Val>
struct __mul__<Tuple<Val>, Object>                              : Returns<Tuple<Val>> {};
template <typename Val, impl::int_like T>
struct __mul__<Tuple<Val>, T>                                   : Returns<Tuple<Val>> {};
template <typename Val>
struct __imul__<Tuple<Val>, Object>                             : Returns<Tuple<Val>&> {};
template <typename Val, impl::int_like T>
struct __imul__<Tuple<Val>, T>                                  : Returns<Tuple<Val>&> {};


/* Represents a statically-typed Python tuple in C++. */
template <typename Val>
class Tuple : public Object, public impl::SequenceOps<Tuple<Val>>, public impl::TupleTag {
    using Base = Object;
    static_assert(
        std::derived_from<Val, Object>,
        "py::Tuple can only contain types derived from py::Object."
    );

    static constexpr bool generic = std::same_as<Val, Object>;

public:
    using size_type = size_t;
    using difference_type = std::ptrdiff_t;
    using value_type = Val;
    using pointer = value_type*;
    using const_pointer = const value_type*;
    using reference = value_type&;
    using const_reference = const value_type&;
    using iterator = impl::Iterator<impl::TupleIter<value_type>>;
    using const_iterator = impl::Iterator<impl::TupleIter<const value_type>>;
    using reverse_iterator = impl::ReverseIterator<impl::TupleIter<value_type>>;
    using const_reverse_iterator = impl::ReverseIterator<impl::TupleIter<const value_type>>;

    template <typename T>
    static consteval bool check() { return impl::tuple_like<T>; }

    // template <typename T> requires (!impl::python_like<T>)
    // static consteval bool check(const T&) {
    //     return check<T>();
    // }

    template <typename T> requires (impl::python_like<T>)
    static bool check(const T& obj) {
        return obj.ptr() != nullptr && PyTuple_Check(obj.ptr());
    }

    template <typename T>
    static constexpr bool check_(const T& value) { return check(value); }

    BERTRAND_OBJECT_OPERATORS(Tuple)

    //////////////////////
    ////    COMMON    ////
    //////////////////////

    /* Inherit tagged borrow/steal constructors. */
    Tuple(Handle h, const borrowed_t& t) : Base(h, t) {}
    Tuple(Handle h, const stolen_t& t) : Base(h, t) {}

    /* Convert a pybind11 accessor into this type. */
    template <typename Policy>
    Tuple(const detail::accessor<Policy>& accessor) {
        pybind11::object obj(accessor);
        if (check(obj)) {
            m_ptr = obj.release().ptr();
        } else {
            throw impl::noconvert<Tuple>(obj.ptr());
        }
    }

    Tuple(const Tuple& other) : Base(other.ptr(), borrowed_t{}) {
        py::print("copy constructor2?");
    }

    /* Copy constructor from another tuple with the same or narrower type. */
    template <std::derived_from<value_type> T>
    Tuple(const Tuple<T>& other) : Base(other.ptr(), borrowed_t{}) {
        py::print("copy constructor");
    }

    /* Move constructor from another tuple with the same or narrower type. */
    template <std::derived_from<value_type> T>
    Tuple(Tuple<T>&& other) : Base(other.release(), stolen_t{}) {
        py::print("move constructor");
    }

    /* Copy/move constructors from equivalent pybind11 type(s).  Only enabled if this
    tuple's value type is set to py::Object. */
    template <typename T>
        requires (
            impl::python_like<T> &&
            !std::derived_from<std::decay_t<T>, impl::TupleTag> &&
            check<T>() &&
            generic
        )
    Tuple(T&& other) : Base(std::forward<T>(other)) {
        py::print("widening constructor");
    }

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Default constructor.  Initializes to an empty tuple. */
    Tuple() : Base(PyTuple_New(0), stolen_t{}) {
        py::print("default constructor");
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /* Pack the contents of a braced initializer into a new Python tuple. */
    Tuple(const std::initializer_list<value_type>& contents) :
        Base(PyTuple_New(contents.size()), stolen_t{})
    {
        py::print("initializer constructor");
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

    /* Explicitly unpack a Python list into a py::Tuple directly using the C API. */
    template <typename T> requires (impl::python_like<T> && impl::list_like<T>)
    explicit Tuple(const T& list) : Base(nullptr, stolen_t{}) {
        if constexpr (generic) {
            m_ptr = PyList_AsTuple(list.ptr());
            if (m_ptr == nullptr) {
                Exception::from_python();
            }
        } else {
            m_ptr = PyTuple_New(std::size(list));
            if (m_ptr == nullptr) {
                Exception::from_python();
            }
            try {
                size_t i = 0;
                for (const auto& item : list) {
                    PyTuple_SET_ITEM(m_ptr, i++, value_type(item).release().ptr());
                }
            } catch (...) {
                Py_DECREF(m_ptr);
                throw;
            }
        }
    }

    /* Explicitly unpack a generic Python container into a py::Tuple. */
    template <typename T>
        requires (impl::python_like<T> && !impl::list_like<T> && impl::is_iterable<T>)
    explicit Tuple(const T& contents) : Base(nullptr, stolen_t{}) {
        if constexpr (generic) {
            m_ptr = PySequence_Tuple(contents.ptr());
            if (m_ptr == nullptr) {
                Exception::from_python();
            }
        } else {
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
        }
    }

    /* Explicitly unpack a generic C++ container into a new py::Tuple. */
    template <typename T> requires (!impl::python_like<T> && impl::is_iterable<T>)
    explicit Tuple(T&& contents) : Base(nullptr, stolen_t{}) {
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

    /* Explicitly unpack a std::pair into a py::Tuple. */
    template <typename First, typename Second>
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
    template <typename... Args>
    explicit Tuple(const std::tuple<Args...>& tuple) :
        Base(PyTuple_New(sizeof...(Args)), stolen_t{})
    {
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

    /* Copy assignment operator. */
    template <std::derived_from<value_type> T>
    Tuple& operator=(const Tuple<T>& other) {
        if (this != &other) {
            PyObject* temp = m_ptr;
            m_ptr = Py_XNewRef(other.m_ptr);
            Py_XDECREF(temp);
        }
        return *this;
    }

    /* Move assignment operator. */
    template <std::derived_from<value_type> T>
    Tuple& operator=(Tuple<T>&& other) {
        if (this != &other) {
            PyObject* temp = m_ptr;
            m_ptr = other.m_ptr;
            other.m_ptr = nullptr;
            Py_XDECREF(temp);
        }
        return *this;
    }

    ///////////////////////////
    ////    CONVERSIONS    ////
    ///////////////////////////

    template <std::derived_from<Object> T>
    operator T() const = delete;

    inline operator pybind11::handle() const {
        return m_ptr;
    }

    inline operator pybind11::object() const {
        return pybind11::reinterpret_borrow<pybind11::object>(m_ptr);
    }

    inline operator pybind11::tuple() const {
        return reinterpret_borrow<pybind11::tuple>(m_ptr);
    }

    template <typename First, typename Second>
    operator std::pair<First, Second>() const {
        if (size() != 2) {
            throw IndexError(
                "conversion to std::pair requires tuple of size 2, not " +
                std::to_string(size())
            );
        }
        return {
            static_cast<First>(GET_ITEM(0)),
            static_cast<Second>(GET_ITEM(1))
        };
    }

    template <typename... Args>
    operator std::tuple<Args...>() const {
        if (size() != sizeof...(Args)) {
            throw IndexError(
                "conversion to std::tuple requires tuple of size " +
                std::to_string(sizeof...(Args)) + ", not " +
                std::to_string(size())
            );
        }

        return [&]<size_t... N>(std::index_sequence<N...>) {
            return std::make_tuple(static_cast<Args>(GET_ITEM(N))...);
        }(std::index_sequence_for<Args...>{});
    }

    template <typename T, size_t N>
    operator std::array<T, N>() const {
        if (size() != N) {
            throw IndexError(
                "conversion to std::array requires tuple of size " +
                std::to_string(N) + ", not " + std::to_string(size())
            );
        }
        std::array<T, N> result;
        for (size_t i = 0; i < N; ++i) {
            result[i] = static_cast<T>(GET_ITEM(i));
        }
        return result;
    }

    template <impl::not_proxy_like T> requires (!std::derived_from<T, Object>)
    explicit operator T() const {
        return Base::operator T();
    }

    /////////////////////////////////
    ////    CPYTHON INTERFACE    ////
    /////////////////////////////////

    // TODO: from_args() and to_args() need special handling for value type.  Maybe they
    // can be rolled into the constructor?

    /* Extract variadic positional arguments from pybind11 into a more expressive
    py::Tuple object. */
    static Tuple from_args(const pybind11::args& args) {
        return reinterpret_borrow<Tuple>(args.ptr());
    }

    /* Convert a tuple to variadic positional arguments for pybind11. */
    static pybind11::args to_args(const Tuple& tuple) {
        return reinterpret_borrow<pybind11::args>(tuple);
    }

    /* Get the underlying PyObject* array. */
    inline PyObject** data() const noexcept {
        return PySequence_Fast_ITEMS(this->ptr());
    }

    /* Directly access an item without bounds checking or constructing a proxy. */
    inline value_type GET_ITEM(Py_ssize_t index) const {
        return reinterpret_borrow<value_type>(PyTuple_GET_ITEM(this->ptr(), index));
    }

    /* Directly set an item without bounds checking or constructing a proxy. */
    inline void SET_ITEM(Py_ssize_t index, const value_type& value) {
        PyObject* prev = PyTuple_GET_ITEM(this->ptr(), index);
        PyTuple_SET_ITEM(this->ptr(), index, Py_XNewRef(value.ptr()));
        Py_XDECREF(prev);
    }

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    // TODO: these operators should be lifted out of py::Tuple in order to avoid
    // spamming global namespace?

    inline friend Tuple operator+(
        const Tuple& self,
        const std::initializer_list<value_type>& items
    ) {
        return self.concat(items);
    }

    inline friend Tuple operator+(
        const std::initializer_list<value_type>& items,
        const Tuple& self
    ) {
        return self.concat(items);
    }

    inline friend Tuple& operator+=(
        Tuple& self,
        const std::initializer_list<value_type>& items
    ) {
        self = self.concat(items);
        return self;
    }

protected:

    using impl::SequenceOps<Tuple>::operator_add;
    using impl::SequenceOps<Tuple>::operator_iadd;
    using impl::SequenceOps<Tuple>::operator_mul;
    using impl::SequenceOps<Tuple>::operator_imul;

    template <typename Return, typename Self>
    inline static size_t operator_len(const Self& self) {
        return PyTuple_GET_SIZE(self.ptr());
    }

    inline Tuple concat(const std::initializer_list<value_type>& items) const {
        PyObject* result = PyTuple_New(size() + items.size());
        if (result == nullptr) {
            Exception::from_python();
        }
        try {
            size_t i = 0;
            size_t length = size();
            PyObject** array = data();
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

    template <typename Return, typename Self>
    inline static auto operator_begin(const Self& self)
        -> impl::Iterator<impl::TupleIter<Return>>
    {
        return impl::Iterator<impl::TupleIter<Return>>(self, 0);
    }

    template <typename Return, typename Self>
    inline static auto operator_end(const Self& self)
        -> impl::Iterator<impl::TupleIter<Return>>
    {
        return impl::Iterator<impl::TupleIter<Return>>(PyTuple_GET_SIZE(self.ptr()));
    }

    template <typename Return, typename Self>
    inline static auto operator_rbegin(const Self& self)
        -> impl::ReverseIterator<impl::TupleIter<Return>>
    {
        return impl::ReverseIterator<impl::TupleIter<Return>>(
            self,
            PyTuple_GET_SIZE(self.ptr()) - 1
        );
    }

    template <typename Return, typename Self>
    inline static auto operator_rend(const Self& self)
        -> impl::ReverseIterator<impl::TupleIter<Return>>
    {
        return impl::ReverseIterator<impl::TupleIter<Return>>(-1);
    }

};


}  // namespace py
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_TUPLE_H
