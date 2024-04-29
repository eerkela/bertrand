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


template <typename T>
Tuple(const Tuple<T>&) -> Tuple<T>;
template <typename T>
Tuple(Tuple<T>&&) -> Tuple<T>;
template <typename T, typename... Args>
    requires (!std::derived_from<std::decay_t<T>, impl::TupleTag>)
Tuple(T&&, Args&&...) -> Tuple<Object>;
template <typename T>
Tuple(const std::initializer_list<T>&) -> Tuple<Object>;


/* Represents a statically-typed Python tuple in C++. */
template <typename Val>
class Tuple : public Object, public impl::TupleTag {
    using Base = Object;
    static_assert(
        std::derived_from<Val, Object>,
        "py::Tuple can only contain types derived from py::Object."
    );

    static constexpr bool generic = std::same_as<Val, Object>;

    // TODO: maybe the way to address this is to create a separate Initializer class
    // that is inherited from in order to define explicit and implicit constructors.
    // -> Maybe Initializer only includes explicit constructors, and each class
    // defines its own implicit constructors.

    // -> This could mean I could implement a generic forwarding constructor of this
    // form:

    // template <typename... Args> requires (std::constructible_from<Initializer, Args...>)
    // explicit Tuple(Args&&... args) :
    //     Base(Initializer(std::forward<Args>(args)...).release(), stolen_t{})
    // {}

    // TODO: separate classes for explicit and implicit constructors?  Implicit
    // initializers could inherit from a CRTP impl:: class that holds the
    // reinterpret_borrow<>, reinterpret_steal<> constructors.

    // implicit constructors

    struct Initializer {
        PyObject* m_ptr = nullptr;

        /* Explicitly unpack a C++ string literal into a py::Tuple. */
        template <size_t N>
        explicit Initializer(const char (&string)[N]) : m_ptr(PyTuple_New(N - 1)) {
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
        template <std::same_as<const char*> T>
        explicit Initializer(T string) {
            PyObject* list = PyList_New(0);
            if (list == nullptr) {
                Exception::from_python();
            }
            try {
                const char* curr = string;
                while (*curr != '\0') {
                    PyObject* item = PyUnicode_FromStringAndSize(curr++, 1);
                    if (item == nullptr) {
                        Exception::from_python();
                    }
                    if (PyList_Append(list, item)) {
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

        inline PyObject* release() {
            PyObject* result = m_ptr;
            m_ptr = nullptr;
            return result;
        }

    };

    // TODO: maybe the real benefit of these structs is just encapsulation.
    // -> implicit_init, explicit_init, implicit_convert, explicit_convert

    // if this works through template specialization, then there could be no need to
    // delete conversion operators from a parent class.  Object would define conversions
    // to handle, object, and proxies, and would then have a general templates for
    // implicit conversions as long as the associated 

public:
    using impl::TupleTag::type;

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

    // TODO: account for compatibility with templated types in check().  pybind11 types
    // should only be considered matches if and only if the value type is set to py::Object.

    template <typename T>
    static consteval bool check() {
        return impl::tuple_like<T>;
    }

    template <typename T>
    static constexpr bool check(const T& obj) {
        if constexpr (impl::python_like<T>) {
            return obj.ptr() != nullptr && PyTuple_Check(obj.ptr());
        } else {
            return check<T>();
        }
    }

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    Tuple(Handle h, const borrowed_t& t) : Base(h, t) {}
    Tuple(Handle h, const stolen_t& t) : Base(h, t) {}

    template <impl::pybind11_like T> requires (check<T>())
    Tuple(T&& other) : Base(std::forward<T>(other)) {}

    template <typename Policy>
    Tuple(const pybind11::detail::accessor<Policy>& accessor) :
        Base(Base::from_pybind11_accessor<Tuple>(accessor).release(), stolen_t{})
    {}

    /* Default constructor.  Initializes to an empty tuple. */
    Tuple() : Base(PyTuple_New(0), stolen_t{}) {
        py::print("default constructor");
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /* Copy constructor from another tuple with a narrower type. */
    template <std::derived_from<value_type> T>
    Tuple(const Tuple<T>& other) : Base(other.ptr(), borrowed_t{}) {}

    /* Move constructor from another tuple with a narrower type. */
    template <std::derived_from<value_type> T>
    Tuple(Tuple<T>&& other) : Base(other.release(), stolen_t{}) {}

    // TODO: figure copy/move constructors out.  Probably just delete OBJECT_COMMON and
    // replace with explicit specializations here.
    // -> This is currently repeated in an ambiguous way, so I need to figure this out.

    /* Copy/move constructors from equivalent pybind11 type(s).  Only enabled if this
    tuple's value type is set to py::Object. */
    template <typename T>
        requires (
            impl::python_like<T> && check<T>() &&
            !std::derived_from<std::decay_t<T>, impl::TupleTag> && generic
        )
    Tuple(T&& other) : Base(std::forward<T>(other)) {}

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

    /* Explicitly unpack a Python list into a py::Tuple directly using the C API. */
    template <impl::python_like T> requires (impl::list_like<T>)
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
    template <impl::python_like T> requires (!impl::list_like<T> && impl::is_iterable<T>)
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
    template <impl::cpp_like T> requires (impl::is_iterable<T>)
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

    template <typename... Args> requires (std::constructible_from<Initializer, Args...>)
    explicit Tuple(Args&&... initializer) :
        Base(Initializer(std::forward<Args>(initializer)...).release(), stolen_t{})
    {}

    /* Explicitly unpack a C++ string literal into a py::Tuple. */
    template <size_t N>
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
    template <std::same_as<const char*> T>
    explicit Tuple(T string) : Base(nullptr, stolen_t{}) {
        PyObject* list = PyList_New(0);
        if (list == nullptr) {
            Exception::from_python();
        }
        try {
            const char* curr = string;
            while (*curr != '\0') {
                PyObject* item = PyUnicode_FromStringAndSize(curr++, 1);
                if (item == nullptr) {
                    Exception::from_python();
                }
                if (PyList_Append(list, item)) {
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
    template <typename... Args> requires (std::constructible_from<value_type, Args> && ...)
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

    ////////////////////////////////
    ////    PYTHON INTERFACE    ////
    ////////////////////////////////

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

    /* Equivalent to Python `tuple.count(value)`, but also takes optional start/stop
    indices similar to `tuple.index()`. */
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
            if (result == -1 && PyErr_Occurred()) {
                Exception::from_python();
            }
            return result;
        } else {
            Py_ssize_t result = PySequence_Count(this->ptr(), value.ptr());
            if (result == -1 && PyErr_Occurred()) {
                Exception::from_python();
            }
            return result;
        }
    }

    /* Equivalent to Python `tuple.index(value[, start[, stop]])`. */
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
            if (result == -1 && PyErr_Occurred()) {
                Exception::from_python();
            }
            return result;
        } else {
            Py_ssize_t result = PySequence_Index(this->ptr(), value.ptr());
            if (result == -1 && PyErr_Occurred()) {
                Exception::from_python();
            }
            return result;
        }
    }

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

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

};


/* Implicitly convert a py::Tuple into a std::pair if and only if the tuple is of
length two and its contents are implicitly covnertible to the member types. */
template <std::derived_from<impl::TupleTag> Self, typename First, typename Second>
    requires (
        std::convertible_to<typename Self::value_type, First> &&
        std::convertible_to<typename Self::value_type, Second>
    )
struct __cast__<Self, std::pair<First, Second>> : Returns<std::pair<First, Second>> {
    static std::pair<First, Second> operator()(const Self& self) {
        if (self.size() != 2) {
            throw IndexError(
                "conversion to std::pair requires tuple of size 2, not " +
                std::to_string(self.size())
            );
        }
        return {
            impl::implicit_cast<First>(self.GET_ITEM(0)),
            impl::implicit_cast<Second>(self.GET_ITEM(1))
        };
    }
};


/* Implicitly convert a py::Tuple into a std::tuple if and only if the tuple is of
length equal to the tuple arguments, and its contents are implicitly convertible to
the member types. */
template <std::derived_from<impl::TupleTag> Self, typename... Args>
    requires (std::convertible_to<typename Self::value_type, Args> && ...)
struct __cast__<Self, std::tuple<Args...>> : Returns<std::tuple<Args...>> {
    static std::tuple<Args...> operator()(const Self& self) {
        if (self.size() != sizeof...(Args)) {
            throw IndexError(
                "conversion to std::tuple requires tuple of size " +
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


/* Implicitly convert a py::Tuple into a std::array if and only if the tuple is of the
specified length, and its contents are implicitly convertible to the array type. */
template <std::derived_from<impl::TupleTag> Self, typename T, size_t N>
    requires (std::convertible_to<typename Self::value_type, T>)
struct __cast__<Self, std::array<T, N>> : Returns<std::array<T, N>> {
    static std::array<T, N> operator()(const Self& self) {
        if (N != self.size()) {
            throw IndexError(
                "conversion to std::array requires tuple of size " +
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


/* Implicitly convert a py::Tuple into a std::vector if its contents are convertible to
the vector type. */
template <std::derived_from<impl::TupleTag> Self, typename T, typename... Args>
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


/* Implicitly convert a py::Tuple into a std::list if its contents are convertible to
the list type. */
template <std::derived_from<impl::TupleTag> Self, typename T, typename... Args>
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


/* Implicitly convert a py::Tuple into a std::forward_list if its contents are
convertible to the list type. */
template <std::derived_from<impl::TupleTag> Self, typename T, typename... Args>
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


/* Implicitly convert a py::Tuple into a std::deque if its contents are convertible to
the deque type. */
template <std::derived_from<impl::TupleTag> Self, typename T, typename... Args>
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


namespace impl {
namespace ops {

    template <typename Return, std::derived_from<TupleTag> Self>
    struct len<Return, Self> {
        static size_t operator()(const Self& self) {
            return PyTuple_GET_SIZE(self.ptr());
        }
    };

    template <typename Return, std::derived_from<TupleTag> Self>
    struct begin<Return, Self> {
        static auto operator()(const Self& self) {
            return impl::Iterator<impl::TupleIter<Return>>(self, 0);
        }
    };

    template <typename Return, std::derived_from<TupleTag> Self>
    struct end<Return, Self> {
        static auto operator()(const Self& self) {
            return impl::Iterator<impl::TupleIter<Return>>(
                PyTuple_GET_SIZE(self.ptr())
            );
        }
    };

    template <typename Return, std::derived_from<TupleTag> Self>
    struct rbegin<Return, Self> {
        static auto operator()(const Self& self) {
            return impl::ReverseIterator<impl::TupleIter<Return>>(
                self,
                PyTuple_GET_SIZE(self.ptr()) - 1
            );
        }
    };

    template <typename Return, std::derived_from<TupleTag> Self>
    struct rend<Return, Self> {
        static auto operator()(const Self& self) {
            return impl::ReverseIterator<impl::TupleIter<Return>>(-1);
        }
    };

    template <typename Return, typename L, typename R>
        requires (std::derived_from<L, TupleTag> || std::derived_from<R, TupleTag>)
    struct add<Return, L, R> : sequence::add<Return, L, R> {};

    template <typename Return, std::derived_from<TupleTag> L, typename R>
    struct iadd<Return, L, R> : sequence::iadd<Return, L, R> {};

    template <typename Return, typename L, typename R>
        requires (std::derived_from<L, TupleTag> || std::derived_from<R, TupleTag>)
    struct mul<Return, L, R> : sequence::mul<Return, L, R> {};

    template <typename Return, std::derived_from<TupleTag> L, typename R>
    struct imul<Return, L, R> : sequence::imul<Return, L, R> {};

}
}


}  // namespace py
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_TUPLE_H
