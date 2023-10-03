// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_CORE_UTIL_H
#define BERTRAND_STRUCTS_CORE_UTIL_H

#include <array>  // std::array
#include <iostream>  // std::cout, std::endl
#include <optional>  // std::optional
#include <sstream>  // std::ostringstream
#include <string>  // std::string
#include <string_view>  // std::string_view
#include <typeinfo>  // std::bad_typeid, typeid()
#include <type_traits>  // std::decay_t<>
#include <Python.h>  // CPython API


/////////////////////////
////    UTILITIES    ////
/////////////////////////


/* Compile-time string concatenation for Python-style dotted type names. */
namespace String {

    /* Concatenate multiple std::string_views at compile-time. */
    template <const std::string_view&... Strings>
    class concat {

        /* Join all strings into a single std::array of chars with static storage. */
        static constexpr auto array = [] {
            // Get array with size equal to total length of strings 
            constexpr size_t len = (Strings.size() + ... + 0);
            std::array<char, len + 1> array{};

            // Append each string to the array
            auto append = [i = 0, &array](const auto& s) mutable {
                for (auto c : s) array[i++] = c;
            };
            (append(Strings), ...);
            array[len] = 0;  // null-terminate
            return array;
        }();

    public:
        /* Get the concatenated string as a std::string_view. */
        static constexpr std::string_view value { array.data(), array.size() - 1 };
    };

    /* Syntactic sugar for concat<...>::value */
    template <const std::string_view&... Strings>
    static constexpr auto concat_v = concat<Strings...>::value;

    /* A metadata trait that determines which specialization of repr() is appropriate
    for a given type. */
    template <typename T>
    class Repr {
        using True = std::true_type;
        using False = std::false_type;
        using Stream = std::ostringstream;

        enum class Strategy {
            python,
            to_string,
            stream,
            iterable,
            type_id
        };

        /* Check if the templated type is a Python object. */
        template<typename U>
        static auto _python(U u) -> decltype(
            PyObject_Repr(std::forward<U>(u)), True{}
        );
        static auto _python(...) -> False;

        /* Check if the templated type is a valid input to std::to_string. */
        template<typename U>
        static auto _to_string(U u) -> decltype(
            std::to_string(std::forward<U>(u)), True{}
        );
        static auto _to_string(...) -> False;

        /* Check if the templated type supports std::ostringstream insertion. */
        template<typename U>
        static auto _streamable(U u) -> decltype(
            std::declval<Stream&>() << std::forward<U>(u), True{}
        );
        static auto _streamable(...) -> False;

        /* Check if the templated type is iterable. */
        template<typename U>
        static auto _iterable(U u) -> decltype(
            std::begin(std::forward<U>(u)), std::end(std::forward<U>(u)), True{}
        );
        static auto _iterable(...) -> False;

        /* Determine the Repr() overload to use for objects of the templated type. */
        static constexpr Strategy category = [] {
            if constexpr (decltype(_python(std::declval<T>()))::value) {
                return Strategy::python;
            } else if constexpr (decltype(_to_string(std::declval<T>()))::value) {
                return Strategy::to_string;
            } else if constexpr (decltype(_streamable(std::declval<T>()))::value) {
                return Strategy::stream;
            } else if constexpr (decltype(_iterable(std::declval<T>()))::value) {
                return Strategy::iterable;
            } else {
                return Strategy::type_id;
            }
        }();

    public:
        static constexpr bool python = (category == Strategy::python);
        static constexpr bool streamable = (category == Strategy::stream);
        static constexpr bool to_string = (category == Strategy::to_string);
        static constexpr bool iterable = (category == Strategy::iterable);
        static constexpr bool type_id = (category == Strategy::type_id);
    };

}


/* A type trait that infers the return type of a C++ function pointer that accepts the
given arguments. */
template <typename Func, typename Enable = void, typename... Args>
struct _ReturnType {
    using type = std::invoke_result_t<Func, Args...>;
};


/* A type trait that represents the return type of a python callable. */
template <typename Func, typename... Args>
struct _ReturnType<
    Func,
    std::enable_if_t<std::is_convertible_v<Func, PyObject*>>,
    Args...
> {
    using type = PyObject*;
};


/* Infer the return type of a C++ function with the given arguments or a Python
callable. */
template <typename Func, typename... Args>
using ReturnType = typename _ReturnType<Func, void, Args...>::type;


/* A raw memory buffer that supports delayed construction from C++/Cython.

NOTE: To be able to stack allocate members of Cython/Python extension types, the
object must be trivially constructible.  If this is not the case, then a Slot can be
used to allocate raw memory on the stack, into which values can be easily constructed
and/or moved.  This allows intuitive RAII semantics in Cython, and avoids unnecessary
heap allocations and indirection that they bring. */
template <typename T>
class Slot {
private:
    bool _constructed = false;
    alignas(T) char data[sizeof(T)];  // access via reinterpret_cast

public:

    /* Construct the value within the memory buffer using the given arguments,
    replacing any previous value it may have held. */
    template <typename... Args>
    inline void construct(Args&&... args) {
        if (_constructed) {
            this->operator*().~T();
        }
        new (data) T(std::forward<Args>(args)...);
        _constructed = true;
    }

    /* Indicates whether the slot currently holds a valid object. */
    inline bool constructed() const noexcept {
        return _constructed;
    }

    /* Destroy the value within the memory buffer. */
    inline void destroy() noexcept {
        if (_constructed) {
            reinterpret_cast<T&>(data).~T();
            _constructed = false;
        }
    }

    /* Trivial constructor for delayed construction. */
    Slot() noexcept {}

    /* Move constructor. */
    Slot(Slot&& other) noexcept : _constructed(other._constructed) {
        if (_constructed) {
            this->construct(std::move(other.operator*()));
        }
    }

    /* Move assignment operator. */
    inline Slot& operator=(Slot&& other) {
        *this = std::move(*other);  // forward to rvalue overload
        return *this;
    }

    /* Assign a new value to the memory buffer using ordinary C++ move semantics. */
    inline Slot& operator=(T&& val) {
        this->construct(std::move(val));
        return *this;
    }

    /* Destroy the value within the memory buffer. */
    ~Slot() noexcept {
        this->destroy();
    }

    /* Dereference to get the value currently stored in the memory buffer. */
    inline T& operator*() {
        if (!_constructed) {
            throw std::runtime_error("Slot is not initialized");
        }
        return reinterpret_cast<T&>(data);
    }

    /* Get a pointer to the value currently stored in the memory buffer.

    NOTE: Cython sometimes has trouble with the above dereference operator and value
    semantics in general, so this method can be used as an alternative. */
    inline T* ptr() {
        return &(this->operator*());
    }

    /* Assign a new value into the memory buffer using Cython-compatible move semantics.

    NOTE: this has to accept a pointer to a temporary object because Cython does not
    support rvalue references.  This means that the referenced object will be in an
    undefined state after this method is called, as if it had been moved from. */
    inline void move_ptr(T* val) {
        *this = std::move(*val);  // forward to rvalue assignment
    }

};


/* Subclass of std::bad_typeid() to allow automatic conversion to Python TypeError by
Cython. */
class type_error : public std::bad_typeid {
private:
    std::string message;

public:
    using std::bad_typeid::bad_typeid;  // inherit default constructors

    /* Allow construction from a custom error message. */
    type_error(const std::string& what) : message(what) {}
    type_error(const char* what) : message(what) {}

    const char* what() const noexcept override { return message.c_str(); }
};


/* Convert the most recent Python error into a C++ exception. */
template <typename Exception>
Exception catch_python() {
    // sanity check
    static_assert(
        std::is_constructible_v<Exception, std::string>,
        "Exception type must be constructible from std::string"
    );

    PyObject* exc_type;  // PyErr_Fetch() initializes these for us
    PyObject* exc_value;
    PyObject* exc_traceback;

    // Get the most recent Python error
    PyErr_Fetch(&exc_type, &exc_value, &exc_traceback);  // clears error indicator
    PyErr_NormalizeException(&exc_type, &exc_value, &exc_traceback);

    // Get the error message from the exception if one exists
    std::string msg("Unknown error");
    if (exc_type != nullptr) {
        // Get msg from exc_value.__str__()
        PyObject* str = PyObject_Str(exc_value);
        if (str == nullptr) {
            msg = "Unknown error (could not get exception message)";
        } else if (!PyUnicode_Check(str)) {
            msg = "Unknown error (exception message is not a string)";
        } else {
            const char* utf8_str = PyUnicode_AsUTF8(str);
            if (utf8_str == nullptr) {
                msg = "Unknown error (exception message failed UTF-8 conversion)";
            } else {
                msg = utf8_str;
            }
        }
        Py_XDECREF(str);  // release python string
    }

    // Decrement reference counts
    Py_XDECREF(exc_type);
    Py_XDECREF(exc_value);
    Py_XDECREF(exc_traceback);

    return Exception(msg);
}


/* Get a string representation of a Python object using PyObject_Repr(). */
template <typename T, std::enable_if_t<String::Repr<T>::python, int> = 0>
std::string repr(const T& obj) {
    if (obj == nullptr) {
        return std::string("NULL");
    }
    PyObject* py_repr = PyObject_Repr(obj);
    if (py_repr == nullptr) {
        throw catch_python<std::runtime_error>();
    }
    const char* c_repr = PyUnicode_AsUTF8(py_repr);
    if (c_repr == nullptr) {
        throw catch_python<std::runtime_error>();
    }
    Py_DECREF(py_repr);
    return std::string(c_repr);
}


/* Get a string representation of a C++ object using `std::to_string()`. */
template <typename T, std::enable_if_t<String::Repr<T>::to_string, int> = 0>
std::string repr(const T& obj) {
    return std::to_string(obj);
}


/* Get a string representation of a C++ object by streaming it into a
`std::ostringstream`. */
template <typename T, std::enable_if_t<String::Repr<T>::streamable, int> = 0>
std::string repr(const T& obj) {
    std::ostringstream stream;
    stream << obj;
    return stream.str();
}


/* Get a string representation of an iterable C++ object by recursively unpacking
it. */
template <typename T, std::enable_if_t<String::Repr<T>::iterable, int> = 0>
std::string repr(const T& obj) {
    std::ostringstream stream;
    stream << '[';
    for (auto iter = std::begin(obj); iter != std::end(obj);) {
        stream << repr(*iter);
        if (++iter != std::end(obj)) {
            stream << ", ";
        }
    }
    stream << ']';
    return stream.str();
}


/* Get a string representation of an arbitrary C++ object by getting its mangled type
name.  NOTE: this is the default implementation if no specialization can be found. */
template <typename T, std::enable_if_t<String::Repr<T>::type_id, int> = 0>
std::string repr(const T& obj) {
    return std::string(typeid(obj).name());
}


/* Round a number up to the next power of two. */
template <typename T, std::enable_if_t<std::is_unsigned_v<T>, int> = 0>
inline T next_power_of_two(T n) {
    constexpr size_t bits = sizeof(T) * 8;
    --n;
    for (size_t i = 1; i < bits; i <<= 2) {
        n |= (n >> i);
    }
    return ++n;
}


/////////////////////////////////
////    COUPLED ITERATORS    ////
/////////////////////////////////


/* NOTE: CoupledIterators are used to share state between the begin() and end()
 * iterators in a loop and generally simplify the iterator interface.  They act like
 * pass-through decorators for the begin() iterator, and contain their own end()
 * iterator to terminate the loop.  This means we can write loops as follows:
 *
 * for (auto iter = view.iter(); iter != iter.end(); ++iter) {
 *     // full access to iter
 * }
 * 
 * Rather than the more verbose:
 * 
 * for (auto iter = view.begin(), end = view.end(); iter != end; ++iter) {
 *      // same as above
 * }
 * 
 * Both generate identical code, but the former is more concise and easier to read.  It
 * also allows any arguments provided to the call operator to be passed through to both
 * the begin() and end() iterators, which can be used to share state between the two.
 */


/* A coupled pair of begin() and end() iterators to simplify the iterator interface. */
template <typename IteratorType>
class CoupledIterator {
public:
    using Iterator = IteratorType;

    // iterator tags for std::iterator_traits
    using iterator_category     = typename Iterator::iterator_category;
    using difference_type       = typename Iterator::difference_type;
    using value_type            = typename Iterator::value_type;
    using pointer               = typename Iterator::pointer;
    using reference             = typename Iterator::reference;

    // couple the begin() and end() iterators into a single object
    CoupledIterator(const Iterator& first, const Iterator& second) :
        first(std::move(first)), second(std::move(second))
    {}

    // allow use of the CoupledIterator in a range-based for loop
    Iterator& begin() { return first; }
    Iterator& end() { return second; }

    // pass iterator protocol through to begin()
    inline value_type operator*() const { return *first; }
    inline CoupledIterator& operator++() { ++first; return *this; }
    inline bool operator!=(const Iterator& other) const { return first != other; }

    // conditionally compile all other methods based on Iterator interface.
    // NOTE: this uses SFINAE to detect the presence of these methods on the template
    // Iterator.  If the Iterator does not implement the named method, then it will not
    // be compiled, and users will get compile-time errors if they try to access it.
    // This avoids the need to manually extend the CoupledIterator interface to match
    // that of the Iterator.  See https://en.cppreference.com/w/cpp/language/sfinae
    // for more information.

    template <typename T = Iterator>
    inline auto insert(value_type value) -> decltype(std::declval<T>().insert(value)) {
        return first.insert(value);  // void
    }

    template <typename T = Iterator>
    inline auto remove() -> decltype(std::declval<T>().remove()) {
        return first.remove();
    }

    template <typename T = Iterator>
    inline auto replace(value_type value) -> decltype(std::declval<T>().replace(value)) {
        return first.replace(value);
    }

    template <typename T = Iterator>
    inline auto index() -> decltype(std::declval<T>().index()) const {
        return first.index();
    }

    template <typename T = Iterator>
    inline auto inverted() -> decltype(std::declval<T>().inverted()) const {
        return first.inverted();
    }

protected:
    Iterator first, second;
};


////////////////////////////////
////    PYTHON ITERABLES    ////
////////////////////////////////


/* NOTE: Python iterables are somewhat tricky to interact with from C++.  Reference
 * counts have to be carefully managed to avoid memory leaks, and the C API is not
 * particularly intuitive.  These helper classes simplify the interface and allow us to
 * use RAII to automatically manage reference counts.
 * 
 * PyIterator is a wrapper around a C++ iterator that allows it to be used from Python.
 * It can only be used for iterators that dereference to PyObject*, and it uses a
 * manually-defined PyTypeObject (whose name must be given as a template argument) to
 * expose the iterator to Python.  This type defines the __iter__() and __next__()
 * magic methods, which are used to implement the iterator protocol in Python.
 * 
 * PyIterable is essentially the inverse.  It represents a C++ wrapper around a Python
 * iterator that defines the __iter__() and __next__() magic methods.  The wrapper can
 * be iterated over using normal C++ syntax, and it automatically manages reference
 * counts for both the iterator itself and each element as we access them.
 * 
 * PySequence is a C++ wrapper around a Python sequence (list or tuple) that allows
 * elements to be accessed by index.  It corresponds to the PySequence_FAST() family of
 * C API functions.  Just like PyIterable, the wrapper automatically manages reference
 * counts for the sequence and its contents as they are accessed.
 */


/* A wrapper around a C++ iterator that allows it to be used from Python. */
template <typename Iterator, const std::string_view& name>
class PyIterator {
    // sanity check
    static_assert(
        std::is_convertible_v<typename Iterator::value_type, PyObject*>,
        "Iterator must dereference to PyObject*"
    );

    /* Store coupled iterators as raw data buffers.
    
    NOTE: PyObject_New() does not allow for traditional stack allocation like we would
    normally use to store the wrapped iterators.  Instead, we have to delay construction
    until the init() method is called.  We could use pointers to heap-allocated memory
    for this, but this adds extra allocation overhead.  Using raw data buffers avoids
    this and places the iterators on the stack, where they belong. */
    PyObject_HEAD
    Slot<Iterator> first;
    Slot<Iterator> second;

    /* Force users to use init() factory method. */
    PyIterator() = delete;
    PyIterator(const PyIterator&) = delete;
    PyIterator(PyIterator&&) = delete;

    /* Initialize a PyTypeObject to represent this iterator from Python. */
    static PyTypeObject init_type() {
        PyTypeObject type_obj;  // zero-initialize
        type_obj.tp_name = name.data();
        type_obj.tp_doc = "Python-compatible wrapper around a C++ iterator.";
        type_obj.tp_basicsize = sizeof(PyIterator);
        type_obj.tp_flags = Py_TPFLAGS_DEFAULT;
        type_obj.tp_alloc = PyType_GenericAlloc;
        type_obj.tp_new = PyType_GenericNew;
        type_obj.tp_iter = PyObject_SelfIter;
        type_obj.tp_iternext = iter_next;
        type_obj.tp_dealloc = (destructor) dealloc;

        // register iterator type with Python
        if (PyType_Ready(&type_obj) < 0) {
            throw std::runtime_error("could not initialize PyIterator type");
        }
        return type_obj;
    }

public:
    /* C-style Python type declaration. */
    inline static PyTypeObject Type = init_type();

    /* Construct a Python iterator from a C++ iterator range. */
    inline static PyObject* init(Iterator&& begin, Iterator&& end) {
        // create new iterator instance
        PyIterator* result = PyObject_New(PyIterator, &Type);
        if (result == nullptr) {
            throw std::runtime_error("could not allocate Python iterator");
        }

        // initialize iterators into raw storage
        result->first.construct(std::move(begin));
        result->second.construct(std::move(end));

        // return as PyObject*
        return reinterpret_cast<PyObject*>(result);
    }

    /* Construct a Python iterator from a coupled iterator. */
    inline static PyObject* init(CoupledIterator<Iterator>&& iter) {
        return init(iter.begin(), iter.end());
    }

    /* Call next(iter) from Python. */
    inline static PyObject* iter_next(PyObject* py_self) {
        PyIterator* self = reinterpret_cast<PyIterator*>(py_self);
        Iterator& begin = *(self->first);
        Iterator& end = *(self->second);

        if (!(begin != end)) {  // terminate the sequence
            PyErr_SetNone(PyExc_StopIteration);
            return nullptr;
        }

        // increment iterator and return current value
        PyObject* result = *begin;
        ++begin;
        return Py_NewRef(result);  // new reference
    }

    /* Free the Python iterator when its reference count falls to zero. */
    inline static void dealloc(PyObject* self) {
        // TODO: Type.tp_free(self)  <- we already have access to the type object
        Py_TYPE(self)->tp_free(self);
    }

};


/* A wrapper around a Python iterator that manages reference counts and enables
for-each loop syntax in C++. */
class PyIterable {
public:
    class Iterator;
    using IteratorPair = CoupledIterator<Iterator>;

    /* Construct a PyIterable from a Python sequence. */
    PyIterable(PyObject* seq) : py_iterator(PyObject_GetIter(seq)) {
        if (py_iterator == nullptr) {
            throw std::invalid_argument("could not get iter(sequence)");
        }
    }

    /* Release the Python sequence. */
    ~PyIterable() { Py_DECREF(py_iterator); }

    /* Iterate over the sequence. */
    inline IteratorPair iter() const { return IteratorPair(begin(), end()); }
    inline Iterator begin() const { return Iterator(py_iterator); }
    inline Iterator end() const { return Iterator(); }

    class Iterator {
    public:
        // iterator tags for std::iterator_traits
        using iterator_category     = std::forward_iterator_tag;
        using difference_type       = std::ptrdiff_t;
        using value_type            = PyObject*;
        using pointer               = PyObject**;
        using reference             = PyObject*&;

        /* Get current item. */
        PyObject* operator*() const { return curr; }

        /* Advance to next item. */
        Iterator& operator++() {
            Py_DECREF(curr);
            curr = PyIter_Next(py_iterator);
            if (curr == nullptr && PyErr_Occurred()) {
                throw std::runtime_error("could not get next(iterator)");
            }
            return *this;
        }

        /* Terminate sequence. */
        bool operator!=(const Iterator& other) const { return curr != other.curr; }

        /* Handle reference counts if an iterator is destroyed partway through
        iteration. */
        ~Iterator() { Py_XDECREF(curr); }

    private:
        friend PyIterable;
        friend IteratorPair;
        PyObject* py_iterator;
        PyObject* curr;

        /* Return an iterator to the start of the sequence. */
        Iterator(PyObject* py_iterator) : py_iterator(py_iterator), curr(nullptr) {
            if (py_iterator != nullptr) {
                curr = PyIter_Next(py_iterator);
                if (curr == nullptr && PyErr_Occurred()) {
                    throw std::runtime_error("could not get next(iterator)");
                }
            }
        }

        /* Return an iterator to the end of the sequence. */
        Iterator() : py_iterator(nullptr), curr(nullptr) {}
    };

protected:
    PyObject* py_iterator;
};


/* A wrapper around a fast Python sequence (list or tuple) that manages reference
counts and simplifies access. */
class PySequence {
public:

    /* Construct a PySequence from an iterable or other sequence. */
    PySequence(PyObject* items, const char* err_msg = "could not get sequence") :
        sequence(PySequence_Fast(items, err_msg)),
        length(static_cast<size_t>(PySequence_Fast_GET_SIZE(sequence)))
    {
        if (sequence == nullptr) {
            throw catch_python<type_error>();  // propagate error
        }
    }

    /* Release the Python sequence on destruction. */
    ~PySequence() { Py_DECREF(sequence); }

    /* Get the length of the sequence. */
    inline size_t size() const { return length; }

    /* Iterate over the sequence. */
    inline PyIterable iter() const { return PyIterable(sequence); }

    /* Get underlying PyObject* array. */
    inline PyObject** array() const { return PySequence_Fast_ITEMS(sequence); }

    /* Get the value at a particular index of the sequence. */
    inline PyObject* operator[](size_t index) const {
        if (index >= length) {
            throw std::out_of_range("index out of range");
        }
        return PySequence_Fast_GET_ITEM(sequence, index);  // borrowed reference
    }

protected:
    PyObject* sequence;
    size_t length;
};


/////////////////////////////
////    BIDIRECTIONAL    ////
/////////////////////////////


/* NOTE: Bidirectional is a type-erased iterator wrapper that can contain either a
 * forward or backward iterator over a linked list.  This allows us to write bare-metal
 * loops if the iteration direction is known at compile time, while also allowing for
 * dynamic traversal based on runtime conditions.  This is useful for implementing
 * slices, which can be iterated over in either direction depending on the step size
 * and singly- vs. doubly-linked nature of the list.
 * 
 * Bidirectional iterators have a small overhead compared to statically-typed iterators,
 * but this is minimized as much as possible through the use of tagged unions and
 * constexpr branches to eliminate conditionals.  If a list is doubly-linked, these
 * optimizations mean that we only add a single if statement on a constant boolean
 * discriminator to the body of the loop to determine the iterator direction.  If a
 * list is singly-linked, then the Bidirectional iterator is functionally equivalent to
 * a statically-typed forward iterator, and there are no unnecessary branches at all.
 */


/* enum to make iterator direction hints more readable. */
enum class Direction {
    forward,
    backward
};


/* Conditionally-compiled base class for Bidirectional iterators that respects the
reversability of the associated view. */
template <template <Direction> class Iterator, bool doubly_linked = false>
class BidirectionalBase {
public:
    Direction direction = Direction::forward;

protected:
    union {
        Iterator<Direction::forward> forward;
    };

    /* Initialize the union with a forward iterator. */
    BidirectionalBase(const Iterator<Direction::forward>& iter) {
        new (&forward) Iterator<Direction::forward>(iter);
    }

    /* Copy constructor. */
    BidirectionalBase(const BidirectionalBase& other) : direction(other.direction) {
        new (&forward) Iterator<Direction::forward>(other.forward);
    }

    /* Move constructor. */
    BidirectionalBase(BidirectionalBase&& other) noexcept : direction(other.direction) {
        new (&forward) Iterator<Direction::forward>(std::move(other.forward));
    }

    /* Copy assignment operator. */
    BidirectionalBase& operator=(const BidirectionalBase& other) {
        direction = other.direction;
        forward = other.forward;
        return *this;
    }

    /* Move assignment operator. */
    BidirectionalBase& operator=(BidirectionalBase&& other) {
        direction = other.direction;
        forward = std::move(other.forward);
        return *this;
    }

    /* Call the contained type's destructor. */
    ~BidirectionalBase() { forward.~Iterator(); }
};


/* Specialization for doubly-linked lists. */
template <template <Direction> class Iterator>
class BidirectionalBase<Iterator, true> {
public:
    Direction direction;

protected:
    union {
        Iterator<Direction::forward> forward;
        Iterator<Direction::backward> backward;
    };

    /* Initialize the union with a forward iterator. */
    BidirectionalBase(const Iterator<Direction::forward>& iter) :
        direction(Direction::forward)
    {
        new (&forward) Iterator<Direction::forward>(iter);
    }

    /* Initialize the union with a backward iterator. */
    BidirectionalBase(const Iterator<Direction::backward>& iter) :
        direction(Direction::backward)
    {
        new (&backward) Iterator<Direction::backward>(iter);
    }

    /* Copy constructor. */
    BidirectionalBase(const BidirectionalBase& other) : direction(other.direction) {
        switch (other.direction) {
            case Direction::backward:
                new (&backward) Iterator<Direction::backward>(other.backward);
                break;
            case Direction::forward:
                new (&forward) Iterator<Direction::forward>(other.forward);
                break;
        }
    }

    /* Move constructor. */
    BidirectionalBase(BidirectionalBase&& other) noexcept : direction(other.direction) {
        switch (other.direction) {
            case Direction::backward:
                new (&backward) Iterator<Direction::backward>(std::move(other.backward));
                break;
            case Direction::forward:
                new (&forward) Iterator<Direction::forward>(std::move(other.forward));
                break;
        }
    }

    /* Copy assignment operator. */
    BidirectionalBase& operator=(const BidirectionalBase& other) {
        direction = other.direction;
        switch (other.direction) {
            case Direction::backward:
                backward = other.backward;
                break;
            case Direction::forward:
                forward = other.forward;
                break;
        }
        return *this;
    }

    /* Move assignment operator. */
    BidirectionalBase& operator=(BidirectionalBase&& other) {
        direction = other.direction;
        switch (other.direction) {
            case Direction::backward:
                backward = std::move(other.backward);
                break;
            case Direction::forward:
                forward = std::move(other.forward);
                break;
        }
        return *this;
    }

    /* Call the contained type's destructor. */
    ~BidirectionalBase() {
        switch (direction) {
            case Direction::backward:
                backward.~Iterator();
                break;
            case Direction::forward:
                forward.~Iterator();
                break;
        }
    }
};


/* A type-erased iterator that can contain either a forward or backward iterator. */
template <template <Direction> class Iterator>
class Bidirectional : public BidirectionalBase<
    Iterator,
    Iterator<Direction::forward>::Node::doubly_linked
> {
public:
    using ForwardIterator = Iterator<Direction::forward>;
    using Node = typename ForwardIterator::Node;
    using Base = BidirectionalBase<Iterator, Node::doubly_linked>;

    // iterator tags for std::iterator_traits
    using iterator_category     = typename ForwardIterator::iterator_category;
    using difference_type       = typename ForwardIterator::difference_type;
    using value_type            = typename ForwardIterator::value_type;
    using pointer               = typename ForwardIterator::pointer;
    using reference             = typename ForwardIterator::reference;

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Initialize the union using an existing iterator. */
    template <Direction dir>
    explicit Bidirectional(const Iterator<dir>& it) : Base(it) {}

    /* Copy constructor. */
    Bidirectional(const Bidirectional& other) : Base(other) {}

    /* Move constructor. */
    Bidirectional(Bidirectional&& other) noexcept : Base(std::move(other)) {}

    // destructor is automatically called by BidirectionalBase

    /////////////////////////////////
    ////    ITERATOR PROTOCOL    ////
    /////////////////////////////////

    /* Dereference the iterator to get the node at the current position. */
    inline value_type operator*() const {
        /*
         * HACK: we rely on a special property of the templated iterators: that both
         * forward and backward iterators use the same implementation of the
         * dereference operator.  This, coupled with the fact that unions occupy the
         * same space in memory, means that we can safely dereference the iterators
         * using only the forward operator, even when the data we access is taken from
         * the backward iterator.  This avoids the need for any extra branches.
         *
         * If this specific implementation detail ever changes, then this hack should
         * be reconsidered in favor of a solution more like the one below.
         */
        return *(this->forward);

        /* 
         * if constexpr (Node::doubly_linked) {
         *     if (this->direction == Direction::backward) {
         *         return *(this->backward);
         *     }
         * }
         * return *(this->forward);
         */
    }

    /* Prefix increment to advance the iterator to the next node in the slice. */
    inline Bidirectional& operator++() {
        if constexpr (Node::doubly_linked) {
            if (this->direction == Direction::backward) {
                ++(this->backward);
                return *this;
            }
        }
        ++(this->forward);
        return *this;
    }

    /* Inequality comparison to terminate the slice. */
    inline bool operator!=(const Bidirectional& other) const {
        /*
         * HACK: We rely on a special property of the templated iterators: that both
         * forward and backward iterators can be safely compared using the same operator
         * implementation between them.  This, coupled with the fact that unions occupy
         * the same space in memory, means that we can directly compare the iterators
         * without any extra branches, regardless of which type is currently active.
         *
         * In practice, what this solution does is always use the forward-to-forward
         * comparison operator, but using data from the backward iterator if it is
         * currently active.  This is safe becase the backward-to-backward comparison
         * is exactly the same as the forward-to-forward comparison, as are all the
         * other possible combinations.  In other words, the directionality of the
         * iterator is irrelevant to the comparison.
         *
         * If these specific implementation details ever change, then this hack should
         * be reconsidered in favor of a solution more like the one below.
         */
        return this->forward != other.forward;

        /*
         * using OtherNode = typename std::decay_t<decltype(other)>::Node;
         *
         * if constexpr (Node::doubly_linked) {
         *     if (this->direction == Direction::backward) {
         *         if constexpr (OtherNode::doubly_linked) {
         *             if (other.direction == Direction::backward) {
         *                 return this->backward != other.backward;
         *             }
         *         }
         *         return this->backward != other.forward;
         *     }
         * }
         *
         * if constexpr (OtherNode::doubly_linked) {
         *     if (other.direction == Direction::backward) {
         *         return this->forward != other.backward;
         *     }
         * }
         * return this->forward != other.forward;
         */
    }

    ///////////////////////////////////
    ////    CONDITIONAL METHODS    ////
    ///////////////////////////////////

    // NOTE: this uses SFINAE to detect the presence of these methods on the template
    // Iterator.  If the Iterator does not implement the named method, then it will not
    // be compiled, and users will get compile-time errors if they try to access it.
    // This avoids the need to manually extend the Bidirectional interface to match
    // that of the Iterator.  See https://en.cppreference.com/w/cpp/language/sfinae
    // for more information.

    /* Insert a node at the current position. */
    template <typename T = ForwardIterator>
    inline auto insert(value_type value) -> decltype(std::declval<T>().insert(value)) {
        if constexpr (Node::doubly_linked) {
            if (this->direction == Direction::backward) {
                return this->backward.insert(value);
            }
        }
        return this->forward.insert(value);
    }

    /* Remove the node at the current position. */
    template <typename T = ForwardIterator>
    inline auto remove() -> decltype(std::declval<T>().remove()) {
        if constexpr (Node::doubly_linked) {
            if (this->direction == Direction::backward) {
                return this->backward.remove();
            }
        }
        return this->forward.remove();
    }

    /* Replace the node at the current position. */
    template <typename T = ForwardIterator>
    inline auto replace(value_type value) -> decltype(std::declval<T>().replace(value)) {
        if constexpr (Node::doubly_linked) {
            if (this->direction == Direction::backward) {
                return this->backward.replace(value);
            }
        }
        return this->forward.replace(value);
    }

    /* Get the index of the current position. */
    template <typename T = ForwardIterator>
    inline auto index() -> decltype(std::declval<T>().index()) const {
        if constexpr (Node::doubly_linked) {
            if (this->direction == Direction::backward) {
                return this->backward.index();
            }
        }
        return this->forward.index();
    }

    /* Check whether the iterator direction is consistent with a slice's step size. */
    template <typename T = ForwardIterator>
    inline auto inverted() -> decltype(std::declval<T>().inverted()) const {
        if constexpr (Node::doubly_linked) {
            if (this->direction == Direction::backward) {
                return this->backward.inverted();
            }
        }
        return this->forward.inverted();
    }

};


#endif  // BERTRAND_STRUCTS_CORE_UTIL_H
