#include <sstream>
#ifndef BERTRAND_PYTHON_INCLUDED
#error "This file should not be included directly.  Please include <bertrand/python.h> instead."
#endif

#ifndef BERTRAND_PYTHON_TUPLE_H
#define BERTRAND_PYTHON_TUPLE_H

#include "common.h"


namespace bertrand {
namespace py {


/* Wrapper around pybind11::tuple that allows it to be directly initialized using
std::initializer_list and replicates the Python interface as closely as possible. */
class Tuple : public impl::SequenceOps, public impl::ReverseIterable<Tuple> {
    using Base = impl::SequenceOps;

    template <typename T>
    static inline PyObject* convert_newref(const T& value) {
        if constexpr (impl::is_python<T>) {
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
    static constexpr bool constructor1 = impl::is_python<T> && impl::is_list_like<T>;
    template <typename T>
    static constexpr bool constructor2 =
        impl::is_python<T> && !impl::is_list_like<T> && impl::is_iterable<T>;
    template <typename T>
    static constexpr bool constructor3 = !impl::is_python<T> && impl::is_iterable<T>;

public:
    static py::Type Type;

    template <typename T>
    static constexpr bool check() { return impl::is_tuple_like<T>; }

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    BERTRAND_OBJECT_CONSTRUCTORS(Base, Tuple, PyTuple_Check)

    /* Default constructor.  Initializes to empty tuple. */
    Tuple() : Base(PyTuple_New(0), stolen_t{}) {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Pack the contents of a homogenously-typed braced initializer into a new Python
    tuple. */
    template <typename T, std::enable_if_t<!impl::is_initializer<T>, int> = 0>
    Tuple(const std::initializer_list<T>& contents) :
        Base(PyTuple_New(contents.size()), stolen_t{})
    {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
        try {
            size_t i = 0;
            for (const T& item : contents) {
                PyTuple_SET_ITEM(m_ptr, i++, convert_newref(item));
            }
        } catch (...) {
            Py_DECREF(m_ptr);
            throw;
        }
    }

    /* Pack the contents of a mixed-type braced initializer into a new Python tuple. */
    Tuple(const std::initializer_list<impl::Initializer>& contents) :
        Base(PyTuple_New(contents.size()), stolen_t{})
    {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
        try {
            size_t i = 0;
            for (const impl::Initializer& element : contents) {
                PyTuple_SET_ITEM(
                    m_ptr,
                    i++,
                    const_cast<impl::Initializer&>(element).value.release().ptr()
                );
            }
        } catch (...) {
            Py_DECREF(m_ptr);
            throw;
        }
    }

    /* Explicitly unpack a Python list into a py::Tuple directly using the C API. */
    template <typename T, std::enable_if_t<constructor1<T>, int> = 0>
    explicit Tuple(const T& list) : Base(PyList_AsTuple(list.ptr()), stolen_t{}) {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Explicitly unpack a generic Python container into a py::Tuple. */
    template <typename T, std::enable_if_t<constructor2<T>, int> = 0>
    explicit Tuple(const T& contents) :
        Base(PySequence_Tuple(contents.ptr()), stolen_t{})
    {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Explicitly unpack a generic C++ container into a new py::Tuple. */
    template <typename T, std::enable_if_t<constructor3<T>, int> = 0>
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

    using Base::operator[];

    inline impl::TupleAccessor operator[](size_t index) const {
        return {*this, index};
    }

    detail::tuple_iterator begin() const {
        return {*this, 0};
    }

    detail::tuple_iterator end() const {
        return {*this, PyTuple_GET_SIZE(this->ptr())};
    }

    using Base::operator<;
    using Base::operator<=;
    using Base::operator==;
    using Base::operator!=;
    using Base::operator>;
    using Base::operator>=;

    using Base::concat;
    using Base::operator+;
    using Base::operator+=;
    using Base::operator*;
    using Base::operator*=;

    /* Overload of concat() that allows the operand to be a braced initializer list. */
    template <typename T, std::enable_if_t<!impl::is_initializer<T>, int> = 0>
    inline Tuple concat(const std::initializer_list<T>& items) const {
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
            for (const T& item : items) {
                PyTuple_SET_ITEM(result, i++, convert_newref(item));
            }
            return reinterpret_steal<Tuple>(result);
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
    }

    /* Overload of concat() that allows the operand to be a braced initializer list. */
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
                    const_cast<impl::Initializer&>(item).value.release().ptr()
                );
            }
            return reinterpret_steal<Tuple>(result);
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
    }

    template <typename T, std::enable_if_t<!impl::is_initializer<T>, int> = 0>
    inline Tuple operator+(const std::initializer_list<T>& items) const {
        return concat(items);
    }

    inline Tuple operator+(const std::initializer_list<impl::Initializer>& items) const {
        return concat(items);
    }

    template <typename T, std::enable_if_t<!impl::is_initializer<T>, int> = 0>
    inline Tuple& operator+=(const std::initializer_list<T>& items) {
        *this = concat(items);
        return *this;
    }

    inline Tuple& operator+=(const std::initializer_list<impl::Initializer>& items) {
        *this = concat(items);
        return *this;
    }

};


}  // namespace python
}  // namespace bertrand


BERTRAND_STD_HASH(bertrand::py::Tuple)


#endif  // BERTRAND_PYTHON_TUPLE_H
