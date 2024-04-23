#if !defined(BERTRAND_PYTHON_INCLUDED) && !defined(LINTER)
#error "This file should not be included directly.  Please include <bertrand/python.h> instead."
#endif

#ifndef BERTRAND_PYTHON_TUPLE_H
#define BERTRAND_PYTHON_TUPLE_H

#include "common.h"


namespace bertrand {
namespace py {


template <>
struct __getattr__<Tuple, "count">                              : Returns<Function> {};
template <>
struct __getattr__<Tuple, "index">                              : Returns<Function> {};

template <>
struct __len__<Tuple>                                           : Returns<size_t> {};
template <>
struct __hash__<Tuple>                                          : Returns<size_t> {};
template <>
struct __iter__<Tuple>                                          : Returns<Object> {};
template <>
struct __reversed__<Tuple>                                      : Returns<Object> {};
template <typename T>
struct __contains__<Tuple, T>                                   : Returns<bool> {};
template <>
struct __getitem__<Tuple, Object>                               : Returns<Object> {};
template <impl::int_like T>
struct __getitem__<Tuple, T>                                    : Returns<Object> {};
template <>
struct __getitem__<Tuple, Slice>                                : Returns<Tuple> {};
template <>
struct __lt__<Tuple, Object>                                    : Returns<bool> {};
template <impl::tuple_like T>
struct __lt__<Tuple, T>                                         : Returns<bool> {};
template <>
struct __le__<Tuple, Object>                                    : Returns<bool> {};
template <impl::tuple_like T>
struct __le__<Tuple, T>                                         : Returns<bool> {};
template <>
struct __ge__<Tuple, Object>                                    : Returns<bool> {};
template <impl::tuple_like T>
struct __ge__<Tuple, T>                                         : Returns<bool> {};
template <>
struct __gt__<Tuple, Object>                                    : Returns<bool> {};
template <impl::tuple_like T>
struct __gt__<Tuple, T>                                         : Returns<bool> {};
template <>
struct __add__<Tuple, Object>                                   : Returns<Tuple> {};
template <impl::tuple_like T>
struct __add__<Tuple, T>                                        : Returns<Tuple> {};
template <>
struct __iadd__<Tuple, Object>                                  : Returns<Tuple&> {};
template <impl::tuple_like T>
struct __iadd__<Tuple, T>                                       : Returns<Tuple&> {};
template <>
struct __mul__<Tuple, Object>                                   : Returns<Tuple> {};
template <impl::int_like T>
struct __mul__<Tuple, T>                                        : Returns<Tuple> {};
template <>
struct __imul__<Tuple, Object>                                  : Returns<Tuple&> {};
template <impl::int_like T>
struct __imul__<Tuple, T>                                       : Returns<Tuple&> {};


/* Represents a statically-typed Python tuple in C++. */
class Tuple : public Object, public impl::SequenceOps<Tuple>, public impl::TupleTag {
    using Base = Object;

public:
    // TODO: include a bevy of typedefs to conform to STL containers

    static const Type type;;  // TODO: CTAD refactor requires this to be lifted to a base class.
    // -> type can be placed in TupleTag

    BERTRAND_OBJECT_COMMON(Base, Tuple, impl::tuple_like, PyTuple_Check)
    BERTRAND_OBJECT_OPERATORS(Tuple)

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Default constructor.  Initializes to empty tuple. */
    Tuple() : Base(PyTuple_New(0), stolen_t{}) {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    // TODO: make sure copy/move constructors work consistently with CTAD.  They should
    // be able to widen the type of the contained object, but not narrow it.  Narrowing
    // requires an explicit constructor call.
    // TODO: might need to make copy/move constructors explicit (i.e. only for Tuple
    // and pybind11::tuple (which is interpreted as Tuple<Object>))

    /* Copy/move constructors. */
    template <typename T> requires (check<T>() && impl::python_like<T>)
    Tuple(T&& other) : Base(std::forward<T>(other)) {}

    // TODO: insert value type into initializer list

    /* Pack the contents of a braced initializer into a new Python tuple. */
    Tuple(const std::initializer_list<Object>& contents) :
        Base(PyTuple_New(contents.size()), stolen_t{})
    {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
        try {
            size_t i = 0;
            for (const Object& item : contents) {
                PyTuple_SET_ITEM(m_ptr, i++, Object(item).release().ptr());
            }
        } catch (...) {
            Py_DECREF(m_ptr);
            throw;
        }
    }

    // TODO: call the templated type's constructor here rather than object_or_cast,
    // in order to get better error diagnostics, or require that the iterator
    // dereference type is convertible to that of the tuple.

    /* Construct a new tuple from a pair of input iterators. */
    template <typename Iter, std::sentinel_for<Iter> Sentinel>
    explicit Tuple(Iter first, Sentinel last) : Base(nullptr, stolen_t{}) {
        PyObject* list = PyList_New(0);
        if (list == nullptr) {
            Exception::from_python();
        }
        while (first != last) {
            if (PyList_Append(list, Object(*first).ptr())) {
                Py_DECREF(list);
                Exception::from_python();
            }
            ++first;
        }
        m_ptr = PyList_AsTuple(list);
        Py_DECREF(list);
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    // TODO: apply a type check against the templated type if it is not listed as
    // object.  If it is object, then the type check is unnecessary.

    /* Explicitly unpack a Python list into a py::Tuple directly using the C API. */
    template <typename T> requires (impl::python_like<T> && impl::list_like<T>)
    explicit Tuple(const T& list) : Base(PyList_AsTuple(list.ptr()), stolen_t{}) {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /* Explicitly unpack a generic Python container into a py::Tuple. */
    template <typename T>
        requires (impl::is_iterable<T> && impl::python_like<T> && !impl::list_like<T>)
    explicit Tuple(const T& contents) :
        Base(PySequence_Tuple(contents.ptr()), stolen_t{})
    {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    // TODO: similarly, this should call the templated type, not Object

    /* Explicitly unpack a generic C++ container into a new py::Tuple. */
    template <typename T> requires (impl::is_iterable<T> && !impl::python_like<T>)
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
                    PyTuple_SET_ITEM(m_ptr, i++, Object(item).release().ptr());
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
            for (auto&& item : contents) {
                if (PyList_Append(
                    list,
                    Object(std::forward<decltype(item)>(item)).ptr()
                )) {
                    Py_DECREF(list);
                    Exception::from_python();
                }
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
            PyTuple_SET_ITEM(m_ptr, 0, Object(pair.first).release().ptr());
            PyTuple_SET_ITEM(m_ptr, 1, Object(pair.second).release().ptr());
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
        auto unpack_tuple = []<typename... As, size_t... Ns>(
            PyObject* result,
            const std::tuple<Args...>& tuple,
            std::index_sequence<Ns...>
        ) {
            (
                PyTuple_SET_ITEM(
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

    /* Implicitly convert to pybind11::tuple. */
    inline operator pybind11::tuple() const {
        return reinterpret_borrow<pybind11::tuple>(m_ptr);
    }

    /* Implicitly convert a py::Tuple into a C++ std::pair.  Throws an error if the
    tuple is not of length 2. */
    template <typename First, typename Second>
    inline operator std::pair<First, Second>() const {
        if (size() != 2) {
            std::ostringstream msg;
            msg << "conversion to std::pair requires tuple of size 2, not " << size();
            throw IndexError(msg.str());
        }
        return {
            static_cast<First>(GET_ITEM(0)),
            static_cast<Second>(GET_ITEM(1))
        };
    }

    /* Implicitly convert a py::Tuple into a C++ std::tuple.  Throws an error if the
    tuple does not have the expected length. */
    template <typename... Args>
    inline operator std::tuple<Args...>() const {
        if (size() != sizeof...(Args)) {
            std::ostringstream msg;
            msg << "conversion to std::tuple requires tuple of size " << sizeof...(Args)
                << ", not " << size();
            throw IndexError(msg.str());
        }

        return [&]<size_t... N>(std::index_sequence<N...>) {
            return std::make_tuple(static_cast<Args>(GET_ITEM(N))...);
        }(std::index_sequence_for<Args...>{});
    }

    /* Implicitly convert a py::Tuple into a C++ std::array.  Throws an error if the
    tuple does not have the expected length. */
    template <typename T, size_t N>
    inline operator std::array<T, N>() const {
        if (size() != N) {
            std::ostringstream msg;
            msg << "conversion to std::array requires tuple of size " << N << ", not "
                << size();
            throw IndexError(msg.str());
        }
        std::array<T, N> result;
        for (size_t i = 0; i < N; ++i) {
            result[i] = static_cast<T>(GET_ITEM(i));
        }
        return result;
    }

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
    inline Object GET_ITEM(Py_ssize_t index) const {
        return reinterpret_borrow<Object>(PyTuple_GET_ITEM(this->ptr(), index));
    }

    /* Directly set an item without bounds checking or constructing a proxy.
    
    NOTE: This steals a reference to `value` and does not clear the previous
    item if one is present.  This is dangerous, and should only be used to fill in
    a newly-allocated (empty) tuple. */
    template <typename T>
    inline void SET_ITEM(Py_ssize_t index, PyObject* value) {
        PyTuple_SET_ITEM(this->ptr(), index, value);
    }

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    inline friend Tuple operator+(
        const Tuple& self,
        const std::initializer_list<Object>& items
    ) {
        return self.concat(items);
    }

    inline friend Tuple operator+(
        const std::initializer_list<Object>& items,
        const Tuple& self
    ) {
        return self.concat(items);
    }

    inline friend Tuple& operator+=(
        Tuple& self,
        const std::initializer_list<Object>& items
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

    inline Tuple concat(const std::initializer_list<Object>& items) const {
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
            for (const Object& item : items) {
                PyTuple_SET_ITEM(result, i++, Object(item).release().ptr());
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
