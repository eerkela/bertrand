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


/* Wrapper around pybind11::tuple that allows it to be directly initialized using
std::initializer_list and replicates the Python interface as closely as possible. */
class Tuple : public Object, public impl::SequenceOps<Tuple> {
    using Base = Object;

    template <typename T>
    static inline PyObject* convert_newref(const T& value) {
        if constexpr (impl::python_like<T>) {
            PyObject* result = value.ptr();
            if (result == nullptr) {
                result = Py_None;
            }
            return Py_NewRef(result);
        } else {
            return Object(value).release().ptr();
        }
    }

    template <typename T>
    static constexpr bool py_list_constructor = impl::python_like<T> && impl::list_like<T>;
    template <typename T>
    static constexpr bool py_unpacking_constructor =
        impl::python_like<T> && !impl::list_like<T> && impl::is_iterable<T>;
    template <typename T>
    static constexpr bool cpp_unpacking_constructor =
        !impl::python_like<T> && impl::is_iterable<T>;

public:
    static Type type;

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

    /* Copy/move constructors. */
    template <typename T> requires (check<T>() && impl::python_like<T>)
    Tuple(T&& other) : Base(std::forward<T>(other)) {}

    /* Pack the contents of a braced initializer into a new Python tuple. */
    Tuple(const std::initializer_list<impl::Initializer>& contents) :
        Base(PyTuple_New(contents.size()), stolen_t{})
    {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
        try {
            size_t i = 0;
            for (const impl::Initializer& item : contents) {
                PyTuple_SET_ITEM(
                    m_ptr,
                    i++,
                    const_cast<impl::Initializer&>(item).first.release().ptr()
                );
            }
        } catch (...) {
            Py_DECREF(m_ptr);
            throw;
        }
    }

    /* Pack the contents of a braced initializer into a new Python tuple. */
    template <typename T, std::enable_if_t<impl::is_initializer<T>, int> = 0>
    Tuple(const std::initializer_list<T>& contents) :
        Base(PyTuple_New(contents.size()), stolen_t{})
    {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
        try {
            size_t i = 0;
            for (const T& item : contents) {
                PyTuple_SET_ITEM(m_ptr, i++, const_cast<T&>(item).first.release().ptr());
            }
        } catch (...) {
            Py_DECREF(m_ptr);
            throw;
        }
    }

    /* Explicitly unpack a Python list into a py::Tuple directly using the C API. */
    template <typename T> requires (py_list_constructor<T>)
    explicit Tuple(const T& list) : Base(PyList_AsTuple(list.ptr()), stolen_t{}) {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /* Explicitly unpack a generic Python container into a py::Tuple. */
    template <typename T> requires (py_unpacking_constructor<T>)
    explicit Tuple(const T& contents) :
        Base(PySequence_Tuple(contents.ptr()), stolen_t{})
    {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /* Explicitly unpack a generic C++ container into a new py::Tuple. */
    template <typename T> requires (cpp_unpacking_constructor<T>)
    explicit Tuple(const T& contents) {
        size_t size = 0;
        if constexpr (impl::has_size<T>) {
            size = contents.size();
        }
        m_ptr = PyTuple_New(size);
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
        try {
            size_t i = 0;
            for (auto&& item : contents) {
                PyTuple_SET_ITEM(
                    m_ptr,
                    i++,
                    convert_newref(std::forward<decltype(item)>(item))
                );
            }
        } catch (...) {
            Py_DECREF(m_ptr);
            throw;
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
            PyTuple_SET_ITEM(m_ptr, 0, convert_newref(pair.first));
            PyTuple_SET_ITEM(m_ptr, 1, convert_newref(pair.second));
        } catch (...) {
            Py_DECREF(m_ptr);
            throw;
        }
    }

private:

    template <typename... Args, size_t... N>
    inline static void unpack_tuple(
        PyObject* result,
        const std::tuple<Args...>& tuple,
        std::index_sequence<N...>
    ) {
        (PyTuple_SET_ITEM(result, N, convert_newref(std::get<N>(tuple))), ...);
    }

public:

    /* Explicitly unpack a std::tuple into a py::Tuple. */
    template <typename... Args>
    explicit Tuple(const std::tuple<Args...>& tuple) :
        Base(PyTuple_New(sizeof...(Args)), stolen_t{})
    {
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

private:

    template <typename... Args, size_t... N>
    inline std::tuple<Args...> convert_to_std_tuple(std::index_sequence<N...>) const {
        return std::make_tuple(static_cast<Args>(GET_ITEM(N))...);
    }

public:

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
        return convert_to_std_tuple<Args...>(std::index_sequence_for<Args...>{});
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
        const std::initializer_list<impl::Initializer>& items
    ) {
        return self.concat(items);
    }

    inline friend Tuple operator+(
        const std::initializer_list<impl::Initializer>& items,
        const Tuple& self
    ) {
        return self.concat(items);
    }

    inline friend Tuple& operator+=(
        Tuple& self,
        const std::initializer_list<impl::Initializer>& items
    ) {
        self = self.concat(items);
        return self;
    }

protected:

    using impl::SequenceOps<Tuple>::operator_add;
    using impl::SequenceOps<Tuple>::operator_iadd;
    using impl::SequenceOps<Tuple>::operator_mul;
    using impl::SequenceOps<Tuple>::operator_imul;

    template <typename Return, typename T>
    inline static size_t operator_len(const T& self) {
        return PyTuple_GET_SIZE(self.ptr());
    }

    inline Tuple concat(const std::initializer_list<impl::Initializer>& items) const {
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
            for (const impl::Initializer& item : items) {
                PyTuple_SET_ITEM(
                    result,
                    i++,
                    const_cast<impl::Initializer&>(item).first.release().ptr()
                );
            }
            return reinterpret_steal<Tuple>(result);
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
    }

    template <typename Return, typename T>
    inline static auto operator_begin(const T& obj)
        -> impl::Iterator<impl::TupleIter<Return>>
    {
        return {obj, 0};
    }

    template <typename Return, typename T>
    inline static auto operator_end(const T& obj)
        -> impl::Iterator<impl::TupleIter<Return>>
    {
        return {PyTuple_GET_SIZE(obj.ptr())};
    }

    template <typename Return, typename T>
    inline static auto operator_rbegin(const T& obj)
        -> impl::ReverseIterator<impl::TupleIter<Return>>
    {
        return {obj, PyTuple_GET_SIZE(obj.ptr()) - 1};
    }

    template <typename Return, typename T>
    inline static auto operator_rend(const T& obj)
        -> impl::ReverseIterator<impl::TupleIter<Return>>
    {
        return {-1};
    }

};


}  // namespace py
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_TUPLE_H
