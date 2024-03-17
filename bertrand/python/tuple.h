#if !defined(BERTRAND_PYTHON_INCLUDED) && !defined(LINTER)
#error "This file should not be included directly.  Please include <bertrand/python.h> instead."
#endif

#ifndef BERTRAND_PYTHON_TUPLE_H
#define BERTRAND_PYTHON_TUPLE_H

#include "common.h"


namespace bertrand {
namespace py {


/* Wrapper around pybind11::tuple that allows it to be directly initialized using
std::initializer_list and replicates the Python interface as closely as possible. */
class Tuple :
    public Object,
    public impl::SequenceOps<Tuple>,
    public impl::ReverseIterable<Tuple>
{
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
            PyObject* result = pybind11::cast(value).release().ptr();
            if (result == nullptr) {
                throw error_already_set();
            }
            return result;
        }
    }

    template <typename T>
    static constexpr bool constructor1 = impl::python_like<T> && impl::list_like<T>;
    template <typename T>
    static constexpr bool constructor2 =
        impl::python_like<T> && !impl::list_like<T> && impl::is_iterable<T>;
    template <typename T>
    static constexpr bool constructor3 = !impl::python_like<T> && impl::is_iterable<T>;

public:
    static Type type;

    template <typename T>
    static constexpr bool check() { return impl::tuple_like<T>; }

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    BERTRAND_OBJECT_COMMON(Base, Tuple, PyTuple_Check)

    /* Default constructor.  Initializes to empty tuple. */
    Tuple() : Base(PyTuple_New(0), stolen_t{}) {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Pack the contents of a braced initializer into a new Python tuple. */
    Tuple(const std::initializer_list<impl::Initializer>& contents) :
        Base(PyTuple_New(contents.size()), stolen_t{})
    {
        if (m_ptr == nullptr) {
            throw error_already_set();
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
            throw error_already_set();
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
    template <typename T> requires (constructor1<T>)
    explicit Tuple(const T& list) : Base(PyList_AsTuple(list.ptr()), stolen_t{}) {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Explicitly unpack a generic Python container into a py::Tuple. */
    template <typename T> requires (constructor2<T>)
    explicit Tuple(const T& contents) :
        Base(PySequence_Tuple(contents.ptr()), stolen_t{})
    {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Explicitly unpack a generic C++ container into a new py::Tuple. */
    template <typename T> requires (constructor3<T>)
    explicit Tuple(const T& contents) {
        size_t size = 0;
        if constexpr (impl::has_size<T>) {
            size = contents.size();
        }
        m_ptr = PyTuple_New(size);
        if (m_ptr == nullptr) {
            throw error_already_set();
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
            throw error_already_set();
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
            throw error_already_set();
        }
        try {
            unpack_tuple(m_ptr, tuple, std::index_sequence_for<Args...>{});
        } catch (...) {
            Py_DECREF(m_ptr);
            throw;
        }
    }

    ///////////////////////////
    ////    CONVERSIONS    ////
    ///////////////////////////

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

    ////////////////////////////
    ////    PyTuple_ API    ////
    ////////////////////////////

    /* Get the size of the tuple. */
    inline size_t size() const noexcept {
        return PyTuple_GET_SIZE(this->ptr());
    }

    /* Check if the tuple is empty. */
    inline bool empty() const noexcept {
        return size() == 0;
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

    inline detail::tuple_iterator begin() const {
        return {*this, 0};
    }

    inline detail::tuple_iterator end() const {
        return {*this, PyTuple_GET_SIZE(this->ptr())};
    }

    inline Tuple operator+(const std::initializer_list<impl::Initializer>& items) const {
        return concat(items);
    }

    inline friend Tuple operator+(
        const std::initializer_list<impl::Initializer>& items,
        const Tuple& self
    ) {
        return self.concat(items);
    }

    inline Tuple& operator+=(const std::initializer_list<impl::Initializer>& items) {
        *this = concat(items);
        return *this;
    }

protected:

    using impl::SequenceOps<Tuple>::operator_add;
    using impl::SequenceOps<Tuple>::operator_iadd;
    using impl::SequenceOps<Tuple>::operator_mul;
    using impl::SequenceOps<Tuple>::operator_imul;

    inline Tuple concat(const std::initializer_list<impl::Initializer>& items) const {
        PyObject* result = PyTuple_New(size() + items.size());
        if (result == nullptr) {
            throw error_already_set();
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

};


namespace impl {

template <>
struct __dereference__<Tuple>                                   : Returns<detail::args_proxy> {};
template <>
struct __len__<Tuple>                                           : Returns<size_t> {};
template <>
struct __iter__<Tuple>                                          : Returns<Object> {};
template <>
struct __reversed__<Tuple>                                      : Returns<Object> {};
template <typename T>
struct __contains__<Tuple, T>                                   : Returns<bool> {};
template <int_like T>
struct __getitem__<Tuple, T>                                    : Returns<Object> {};
template <>
struct __getitem__<Tuple, Slice>                                : Returns<Tuple> {};
template <>
struct __lt__<Tuple, Object>                                    : Returns<bool> {};
template <tuple_like T>
struct __lt__<Tuple, T>                                         : Returns<bool> {};
template <>
struct __le__<Tuple, Object>                                    : Returns<bool> {};
template <tuple_like T>
struct __le__<Tuple, T>                                         : Returns<bool> {};
template <>
struct __ge__<Tuple, Object>                                    : Returns<bool> {};
template <tuple_like T>
struct __ge__<Tuple, T>                                         : Returns<bool> {};
template <>
struct __gt__<Tuple, Object>                                    : Returns<bool> {};
template <tuple_like T>
struct __gt__<Tuple, T>                                         : Returns<bool> {};
template <>
struct __add__<Tuple, Object>                                   : Returns<Tuple> {};
template <tuple_like T>
struct __add__<Tuple, T>                                        : Returns<Tuple> {};
template <>
struct __iadd__<Tuple, Object>                                  : Returns<Tuple&> {};
template <tuple_like T>
struct __iadd__<Tuple, T>                                       : Returns<Tuple&> {};

}

}  // namespace python
}  // namespace bertrand


BERTRAND_STD_HASH(bertrand::py::Tuple)


#endif  // BERTRAND_PYTHON_TUPLE_H
