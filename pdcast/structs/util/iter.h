// include guard: BERTRAND_STRUCTS_UTIL_ITER_H
#ifndef BERTRAND_STRUCTS_UTIL_ITER_H
#define BERTRAND_STRUCTS_UTIL_ITER_H

#include <iterator>  // std::iterator_traits
#include <type_traits>  // std::enable_if_t, std::is_same_v, std::void_t
#include <Python.h>  // CPython API
#include <utility>  // std::declval, std::move
#include "base.h"  // is_pyobject<>
#include "except.h"  // catch_python, TypeError
#include "func.h"  // identity, FuncTraits<>
#include "name.h"  // TypeName<>, PyName<>


/* The `iter()` method represents a two-way bridge between Python and C++ containers
implementing the standard iterator interface.  It can be invoked as follows:

    for (auto item : iter(container)) {
        // do something with item
    }

Where `container` is any C++ or Python container that implements the standard iterator
interface in its respective language.  On the C++ side, this includes all STL
containers, as well as any custom container that exposes some combination of `begin()`,
`end()`, `rbegin()`, `rend()`, etc.  On the Python side, it includes built-in lists,
tuples, sets, strings, dictionaries, and any other object that implements the
`__iter__()` and/or `__reversed__()` magic methods, including custom classes.

When called with a C++ container, the `iter()` method produces a proxy that forwards
the container's original iterator interface (however it is defined).  The proxy uses
these methods to generate equivalent Python iterators with corresponding `__iter__()`
and `__next__()` methods, which can be returned directly to the Python interpreter.
This translation works as long as the C++ iterators dereference to PyObject*, or if a
custom conversion function is provided via the optional `convert` argument.  This
allows users to insert a scalar conversion in between the iterator dereference and the
return of the `__next__()` method on the Python side.  For example, if the C++ iterator
dereferences to a custom struct, the user can provide an inline lambda that translates
the struct into a valid PyObject*, which is returned to Python like normal.  This
conversion can be invoked as follows:

    return iter(container, [](MyStruct& s) { return do_something(s); }).python();

Which returns a Python iterator that yields the result of `do_something(s)` for every
`s` in `container`.

When called with a Python container, the `iter()` method produces an equivalent proxy
that wraps the `PyObject_GetIter()` C API function and exposes a standard C++ iterator
interface on the other side.  Just like the C++ to Python translation, custom
conversion functions can be added in between the result of the `__next__()` method on
the Python side and the iterator dereference on the C++ side:

    for (auto item : iter(container, [](PyObject* obj) { return do_something(obj); })) {
        // item is the result of `do_something(obj)` for every `obj` in `container`
    }

Note that due to the dynamic nature of Python's type system, conversions of this sort
require foreknowledge of the container's specific element type in order to perform the
casts necessary to narrow Python types to their C++ counterparts.  To facilitate this,
each of the data structures exposed in the `bertrand::structs` namespace support
optional Python-side type specialization, which can be used to enforce homogeneity at
the container level.  With this in place, users can safely convert the contents of the
container to a specific C++ type without having to worry about type errors or
unexpected behavior.
*/


namespace bertrand {
namespace util {


/////////////////////////////////
////    ITERATOR WRAPPERS    ////
/////////////////////////////////


/* NOTE: CoupledIterators are used to share state between the begin() and end()
 * iterators in a loop and generally simplify the overall iterator interface.  They act
 * like pass-through decorators for the begin() iterator, and contain their own end()
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
    CoupledIterator(Iterator&& first, Iterator&& second) :
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
    inline auto prev() const -> decltype(std::declval<T>().prev()) {
        return first.prev();
    }

    template <typename T = Iterator>
    inline auto curr() const -> decltype(std::declval<T>().curr()) {
        return first.curr();
    }

    template <typename T = Iterator>
    inline auto next() const -> decltype(std::declval<T>().next()) {
        return first.next();
    }

    template <typename T = Iterator>
    inline auto insert(typename T::Node* value)
        -> decltype(std::declval<T>().insert(value))
    {
        return first.insert(value);  // void
    }

    template <typename T = Iterator>
    inline auto drop() -> decltype(std::declval<T>().drop()) {
        return first.drop();
    }

    template <typename T = Iterator>
    inline auto replace(typename T::Node* value)
        -> decltype(std::declval<T>().replace(value))
    {
        return first.replace(value);
    }

    template <typename T = Iterator>
    inline auto index() -> decltype(std::declval<T>().index()) const {
        return first.index();
    }

    template <typename T = Iterator>
    inline auto idx() -> decltype(std::declval<T>().idx()) const {
        return first.idx();
    }

protected:
    Iterator first, second;
};


/* NOTE: ConvertedIterators can be used to apply a custom conversion function to the
 * result of a standard C++ iterator's dereference operator.  This is useful for
 * applying conversions during iteration, which may be necessary when translating
 * between C++ and Python types, for example.
 *
 * ConvertedIterators use SFINAE and compile-time reflection to detect the presence of
 * the standard iterator interface, and to adjust the return type of the relevant
 * dereference operator(s).  This is inferred automatically at compile-time directly
 * from the provided conversion function, allowing for a unified interface across all
 * types of iterators.
 *
 * Note that any additional (non-operator) methods that are exposed by the underlying
 * iterator are not forwarded to the ConvertedIterator wrapper due to limitations with
 * dynamic forwarding in C++.  The ConvertedIterator does, however, expose the wrapped
 * iterator as a public attribute, which can be used to access these methods directly
 * if needed.
 */


/* A decorator for a standard C++ iterator that applies a custom conversion at
each step. */
template <typename Iterator, typename Func>
class ConvertedIterator {
    Func convert;

    /* Ensure that Func is callable with a single argument of the iterator's
    dereferenced value type and infer the corresponding return type. */
    using ConvTraits = FuncTraits<Func, decltype(*std::declval<Iterator>())>;
    using ReturnType = typename ConvTraits::ReturnType;

    /* Get iterator_traits from wrapped iterator. */
    using Traits = std::iterator_traits<Iterator>;

    /* Force SFINAE evaluation of the templated type. */
    template <typename T>
    static constexpr bool exists = std::is_same_v<T, T>;

    /* Detect whether the templated type supports the -> operator. */
    template <typename T, typename = void>  // default
    struct arrow_operator {
        using type = void;
        static constexpr bool value = false;
    };
    template <typename T>  // specialization for smart pointers
    struct arrow_operator<T, std::void_t<decltype(std::declval<T>().operator->())>> {
        using type = decltype(std::declval<T>().operator->());
        static constexpr bool value = true;
    };
    template <typename T>  // specialization for raw pointers
    struct arrow_operator<T*> {
        using type = T*;
        static constexpr bool value = true;
    };

public:
    Iterator wrapped;

    /* Forwards for std::iterator_traits. */
    using iterator_category = std::enable_if_t<
        exists<typename Traits::iterator_category>,
        typename Traits::iterator_category
    >;
    using pointer = std::enable_if_t<
        exists<typename Traits::pointer>,
        typename Traits::pointer
    >;
    using reference = std::enable_if_t<
        exists<typename Traits::reference>,
        typename Traits::reference
    >;
    using value_type = std::enable_if_t<
        exists<typename Traits::value_type>,
        typename Traits::value_type
    >;
    using difference_type = std::enable_if_t<
        exists<typename Traits::difference_type>,
        typename Traits::difference_type
    >;

    /* Construct a converted iterator from a standard C++ iterator and a conversion
    function. */
    inline ConvertedIterator(Iterator& i, Func f) : convert(f), wrapped(i) {}
    inline ConvertedIterator(Iterator&& i, Func f) : convert(f), wrapped(std::move(i)) {}
    inline ConvertedIterator(const ConvertedIterator& other) :
        convert(other.convert), wrapped(other.wrapped)
    {}
    inline ConvertedIterator(ConvertedIterator&& other) :
        convert(std::move(other.convert)), wrapped(std::move(other.wrapped))
    {}
    inline ConvertedIterator& operator=(const ConvertedIterator& other) {
        convert = other.convert;
        wrapped = other.wrapped;
        return *this;
    }
    inline ConvertedIterator& operator=(ConvertedIterator&& other) {
        convert = std::move(other.convert);
        wrapped = std::move(other.wrapped);
        return *this;
    }

    /* Dereference the iterator and apply the conversion function. */
    inline ReturnType operator*() const {
        return convert(*wrapped);
    }
    template <typename T>
    inline ReturnType operator[](T&& index) const {
        return convert(wrapped[index]);
    }
    template <
        bool cond = arrow_operator<ReturnType>::value,
        std::enable_if_t<cond, int> = 0
    >
    inline auto operator->() const -> typename arrow_operator<ReturnType>::type {
        return this->operator*().operator->();
    }

    /* Forward all other methods to the wrapped iterator. */
    inline ConvertedIterator& operator++() {
        ++wrapped;
        return *this;
    }
    inline ConvertedIterator operator++(int) {
        ConvertedIterator temp(*this);
        ++wrapped;
        return temp;
    }
    inline ConvertedIterator& operator--() {
        --wrapped;
        return *this;
    }
    inline ConvertedIterator operator--(int) {
        ConvertedIterator temp(*this);
        --wrapped;
        return temp;
    }
    inline bool operator==(const ConvertedIterator& other) const {
        return wrapped == other.wrapped;
    }
    inline bool operator!=(const ConvertedIterator& other) const {
        return wrapped != other.wrapped;
    }
    template <typename T>
    inline ConvertedIterator& operator+=(T&& other) {
        wrapped += other;
        return *this;
    }
    template <typename T>
    inline ConvertedIterator& operator-=(T&& other) {
        wrapped -= other;
        return *this;
    }
    inline bool operator<(const ConvertedIterator& other) const {
        return wrapped < other.wrapped;
    }
    inline bool operator>(const ConvertedIterator& other) const {
        return wrapped > other.wrapped;
    }
    inline bool operator<=(const ConvertedIterator& other) const {
        return wrapped <= other.wrapped;
    }
    inline bool operator>=(const ConvertedIterator& other) const {
        return wrapped >= other.wrapped;
    }

    /* operator+ implemented as a non-member function for commutativity. */
    template <typename T, typename _Iterator, typename _Func>
    friend ConvertedIterator<_Iterator, _Func> operator+(
        const ConvertedIterator<_Iterator, _Func>& iter,
        T n
    );
    template <typename T, typename _Iterator, typename _Func>
    friend ConvertedIterator<_Iterator, _Func> operator+(
        T n,
        const ConvertedIterator<_Iterator, _Func>& iter
    );

    /* operator- implemented as a non-member function for commutativity. */
    template <typename T, typename _Iterator, typename _Func>
    friend ConvertedIterator<_Iterator, _Func> operator-(
        const ConvertedIterator<_Iterator, _Func>& iter,
        T n
    );
    template <typename T, typename _Iterator, typename _Func>
    friend ConvertedIterator<_Iterator, _Func> operator-(
        T n,
        const ConvertedIterator<_Iterator, _Func>& iter
    );

};


/* Non-member operator+ overload to allow for commutativity. */
template <typename T, typename Iterator, typename Func>
ConvertedIterator<Iterator, Func> operator+(
    const ConvertedIterator<Iterator, Func>& iter,
    T n
) {
    return ConvertedIterator<Iterator, Func>(iter.wrapped + n, iter.convert);
}


/* Non-member operator+ overload to allow for commutativity. */
template <typename T, typename Iterator, typename Func>
ConvertedIterator<Iterator, Func> operator+(
    T n,
    const ConvertedIterator<Iterator, Func>& iter
) {
    return ConvertedIterator<Iterator, Func>(n + iter.wrapped, iter.convert);
}


/* Non-member operator- overload to allow for commutativity. */
template <typename T, typename Iterator, typename Func>
ConvertedIterator<Iterator, Func> operator-(
    const ConvertedIterator<Iterator, Func>& iter,
    T n
) {
    return ConvertedIterator<Iterator, Func>(iter.wrapped - n, iter.convert);
}


/* Non-member operator- overload to allow for commutativity. */
template <typename T, typename Iterator, typename Func>
ConvertedIterator<Iterator, Func> operator-(
    T n,
    const ConvertedIterator<Iterator, Func>& iter
) {
    return ConvertedIterator<Iterator, Func>(n - iter.wrapped, iter.convert);
}


/* NOTE: PyIterators are wrappers around standard C++ iterators that allow them to be
 * used from Python.  They are implemented using a C-style PyTypeObject definition to
 * expose the __iter__() and __next__() magic methods, which are used to implement the
 * iterator protocol in Python.  These Python methods simply delegate to the minimal
 * C++ forward iterator interface, which must include:
 *
 *      1. operator*() to dereference the iterator
 *      2. operator++() to preincrement the iterator
 *      3. operator!=() to terminate the sequence
 *
 * The only other requirement is that the iterator must dereference to PyObject*, or be
 * converted to PyObject* via a custom conversion function.  This ensures that the
 * items yielded by the iterator are compatible with the Python C API, and can be
 * passed to other Python functions without issue.  Failure to handle these will result
 * in compile-time errors.
 *
 * NOTE: PyIterators, just like other bertrand-enabled Python wrappers around C++
 * objects (e.g. PyLock, etc.), use compile-time type information (CTTI) to build their
 * respective PyTypeObject definitions, which are guaranteed to be unique for each of
 * the wrapped iterator types.  This allows the wrapper to be applied generically to
 * any C++ type without any additional configuration from the user.  The only potential
 * complication is in deriving an appropriate dotted name for the Python type, which
 * normally requires the use of compiler-specific macros.
 *
 * A robust solution to this problem is provided by the PyName<> class, which can
 * generate a mangled, Python-compatible name for any C++ type, taking into account
 * namespaces, templates, and other common C++ constructs.  This approach should work
 * for all major compilers (including GCC, Clang, and MSVC-based solutions), but should
 * it fail, a custom name can be provided by specializing the PyName<> template for the
 * desired type.  See the PyName<> documentation for more information.
 */


/* A wrapper around a C++ iterator that allows it to be used from Python. */
template <typename Iterator>
class PyIterator {
    // sanity check
    static_assert(
        std::is_convertible_v<typename Iterator::value_type, PyObject*>,
        "Iterator must dereference to PyObject*"
    );

    /* Store coupled iterators as raw data buffers.
    
    NOTE: PyObject_New() does not allow for traditional stack allocation like we would
    normally use to store the wrapped iterators.  Instead, we have to delay
    construction until the construct() method is called.  We could use pointers to
    heap-allocate memory for this, but this adds extra allocation overhead.  Using raw
    data buffers avoids this and places the iterators on the stack, where they
    belong.  They can be accessed via reinterpret_cast<Iterator&>. */
    PyObject_HEAD
    alignas(Iterator) char first[sizeof(Iterator)];
    alignas(Iterator) char second[sizeof(Iterator)];

    /* Force use of construct() factory method. */
    PyIterator() = delete;
    PyIterator(const PyIterator&) = delete;
    PyIterator(PyIterator&&) = delete;

public:

    /* Construct a Python iterator from a C++ iterator range. */
    inline static PyObject* construct(Iterator&& begin, Iterator&& end) {
        // allocate
        PyIterator* result = PyObject_New(PyIterator, &Type);
        if (result == nullptr) {
            throw std::runtime_error("could not allocate Python iterator");
        }

        // initialize
        new (&(result->first)) Iterator(std::move(begin));
        new (&(result->second)) Iterator(std::move(end));
        return reinterpret_cast<PyObject*>(result);
    }

    /* Construct a Python iterator from a coupled iterator. */
    inline static PyObject* construct(CoupledIterator<Iterator>&& iter) {
        return construct(iter.begin(), iter.end());
    }

    /* Call next(iter) from Python. */
    inline static PyObject* __next__(PyIterator* self) {
        Iterator& begin = reinterpret_cast<Iterator&>(self->first);
        Iterator& end = reinterpret_cast<Iterator&>(self->second);

        if (!(begin != end)) {  // terminate the sequence
            PyErr_SetNone(PyExc_StopIteration);
            return nullptr;
        }

        // increment iterator and return current value
        PyObject* result = *begin;
        ++begin;
        return Py_NewRef(result);  // new reference
    }

private:

    /* Free the Python iterator when its reference count falls to zero. */
    inline static void __dealloc__(PyIterator* self) {
        reinterpret_cast<Iterator&>(self->first).~Iterator();
        reinterpret_cast<Iterator&>(self->second).~Iterator();
        Type.tp_free(self);
    }

    /* Initialize a PyTypeObject to represent this iterator from Python. */
    static PyTypeObject init_type() {
        PyTypeObject slots = {
            .ob_base = PyVarObject_HEAD_INIT(NULL, 0)
            .tp_name = PyName<Iterator>.data(),
            .tp_basicsize = sizeof(PyIterator),
            .tp_dealloc = (destructor) __dealloc__,
            .tp_flags = (
                Py_TPFLAGS_DEFAULT | Py_TPFLAGS_IMMUTABLETYPE |
                Py_TPFLAGS_DISALLOW_INSTANTIATION
            ),
            .tp_doc = "Python-compatible wrapper around a C++ iterator.",
            .tp_iter = PyObject_SelfIter,
            .tp_iternext = (iternextfunc) __next__,
        };

        // register iterator type with Python
        if (PyType_Ready(&slots) < 0) {
            throw std::runtime_error("could not initialize PyIterator type");
        }
        return slots;
    }

public:

    /* C-style Python type declaration. */
    inline static PyTypeObject Type = init_type();

};


////////////////////////////
////    C++ BINDINGS    ////
////////////////////////////


/* C++ bindings consist of a battery of compile-time SFINAE checks to detect the
 * presence and return types of the standard C++ iterator interface, including the
 * following methods:
 *      begin()
 *      cbegin()
 *      end()
 *      cend()
 *      rbegin()
 *      crbegin()
 *      rend()
 *      crend()
 *
 * Which can be defined as either member methods, non-member ADL methods, or via the
 * equivalent standard library functions (in order of preference).  The first one found
 * is forwarded as the proxy's own begin(), cbegin(), end(), etc. member methods,
 * which standardizes the interface for all types of iterable containers.  What's more,
 * const/non-const overloads are detected and handled automatically, allowing us to
 * synthesize missing methods through delegation.  For example, if the container does
 * not implement cbegin(), but does implement a const overload for begin(), then we can
 * generate a cbegin() method by simply delegating to the const overload.  Similarly,
 * if a container does not implement begin(), but does implement cbegin(), then we can
 * generate begin() by delegating to cbegin().  This allows us to support as wide a
 * range of iterable containers as possible, regardless of their specific iterator
 * implementation.
 *
 * Python iterators are constructed by coupling various pairs of `begin()` and
 * `end()` iterators and packaging them into an appropriate PyIterator wrapper, as
 * returned by the following proxy methods:
 *      python()            // (begin() + end())
 *      cpython()           // (cbegin() + cend())
 *      rpython()           // (rbegin() + rend())
 *      crpython()          // (crbegin() + crend())
 */


/* A collection of SFINAE traits introspecting a C++ container's iterator interface. */
template <typename Container, typename Func = identity>
class ContainerTraits {
    static constexpr bool is_identity = std::is_same_v<Func, identity>;

    /* Create a wrapper around an iterator that applies a conversion function to the
    result of its dereference operator. */
    template <typename Iter, bool do_conversion = false>
    struct _conv {
        using type = ConvertedIterator<Iter, Func>;
        static inline type decorate(Iter&& iter, Func func) {
            return type(std::move(iter), func);
        }
    };

    /* If no conversion function is given, return the iterator unmodified. */
    template <typename Iter>
    struct _conv<Iter, true> {
        using type = Iter;
    };

    template <typename Iter>
    using conv = _conv<Iter, is_identity>;

    /* NOTE: using a preprocessor macro avoids a lot of boilerplate when it comes to
    instantiating correct SFINAE iterator traits, but can be a bit intimidating to
    read.  The basic idea is as follows:
    
    For each iterator method - begin(), end(), rbegin(), etc. - we check for 3 possible
    configurations to be as generic as possible:

        1.  A member method of the same name within the Iterable type itself.
                e.g. iterable.begin()
        2.  A non-member ADL method within the same namespace as the Iterable type.
                e.g. begin(iterable)
        3.  An equivalently-named standard library method.
                e.g. std::begin(iterable)

    These are checked in order at compile time, and the first one found is passed
    through to the proxy's `.begin()` member (or other member of a corresponding name),
    which represents a unified interface for all types of iterable containers.  If none
    of these methods exist, the proxy's `.begin()` member (or equivalent) is not
    defined, and any attempt to use it will result in a compile error.

    If a conversion function is supplied to the proxy, then the result of the
    `.begin()` method is wrapped in a ConvertedIterator<>, which applies the conversion
    function at the point of dereference.  For this to work, the underlying iterator
    must be copy/move constructible, and must implement any combination of the standard
    operator overloads (e.g. `*`, `[]`, `->`, `++`, `--`, `==`, `!=`, etc.).
    Additionally, the supplied conversion function must be invocable with the result of
    the iterator's original dereference operator.  If any of these conditions are not
    met, it will result in a compile error.

    Lastly, the conversion function's return type (evaluated at compile time) will be
    used to set the `value_type` of the converted iterator, and if it is convertible to
    PyObject*, then Python-compatible iterators can be constructed from it using the
    proxy's `python()`, `cpython()`, `rpython()`, and `crpython()` methods.  If the
    result of the conversion is not Python-compatible, then these methods will not be
    defined, and any attempt to use them will result in a compile error.

    See https://en.cppreference.com/w/cpp/language/adl for more information on ADL and
    non-member functions, and https://en.cppreference.com/w/cpp/language/sfinae for
    a reference on SFINAE substitution and compile-time metaprogramming. */
    #define TRAIT_FLAG(FLAG_NAME, STATEMENT) \
        /* Flags gate the SFINAE detection, ensuring that we stop at the first match */ \
        template <typename Iterable, typename = void> \
        struct FLAG_NAME : std::false_type {}; \
        template <typename Iterable> \
        struct FLAG_NAME<Iterable, std::void_t<decltype(STATEMENT)>> : std::true_type {}; \

    #define ITER_TRAIT(METHOD) \
        /* Default specialization for methods that don't exist on the Iterable type. */ \
        template < \
            typename Iterable, \
            typename MemberEnable = void, \
            typename ADLEnable = void, \
            typename STDEnable = void \
        > \
        struct _##METHOD { \
            static constexpr bool exists = false; \
            using type = void; \
        }; \
        /* First, check for a member method of the same name within Iterable. */ \
        template <typename Iterable> \
        struct _##METHOD< \
            Iterable, \
            std::void_t<decltype(std::declval<Iterable&>().METHOD())> \
        > { \
            static constexpr bool exists = true; \
            using base_type = decltype(std::declval<Iterable&>().METHOD()); \
            using wrapper = conv<base_type>; \
            using type = typename wrapper::type; \
            static inline type call(Iterable& iterable, Func func) { \
                if constexpr (is_identity) { \
                    return iterable.METHOD(); \
                } else { \
                    return wrapper::decorate(iterable.METHOD(), func); \
                } \
            } \
        }; \
        TRAIT_FLAG(has_member_##METHOD, std::declval<Iterable&>().METHOD()) \
        /* Second, check for a non-member ADL method within the same namespace. */ \
        template <typename Iterable> \
        struct _##METHOD< \
            Iterable, \
            void, \
            std::enable_if_t< \
                !has_member_##METHOD<Iterable>::value, \
                std::void_t<decltype(METHOD(std::declval<Iterable&>()))> \
            > \
        > { \
            static constexpr bool exists = true; \
            using base_type = decltype(METHOD(std::declval<Iterable&>())); \
            using wrapper = conv<base_type>; \
            using type = typename wrapper::type; \
            static inline type call(Iterable& iterable, Func func) { \
                if constexpr (is_identity) { \
                    return METHOD(iterable); \
                } else { \
                    return wrapper::decorate(METHOD(iterable), func); \
                } \
            } \
        }; \
        TRAIT_FLAG(has_adl_##METHOD, METHOD(std::declval<Iterable&>())) \
        /* Third, check for an equivalently-named standard library method. */ \
        template <typename Iterable> \
        struct _##METHOD< \
            Iterable, \
            void, \
            void, \
            std::enable_if_t< \
                !has_member_##METHOD<Iterable>::value && \
                !has_adl_##METHOD<Iterable>::value, \
                std::void_t<decltype(std::METHOD(std::declval<Iterable&>()))> \
            > \
        > { \
            static constexpr bool exists = true; \
            using base_type = decltype(std::METHOD(std::declval<Iterable&>())); \
            using wrapper = conv<base_type>; \
            using type = typename wrapper::type; \
            static inline type call(Iterable& iterable, Func func) { \
                if constexpr (is_identity) { \
                    return std::METHOD(iterable); \
                } else { \
                    return wrapper::decorate(std::METHOD(iterable), func); \
                } \
            } \
        }; \

    /* Detect presence of iterator interface on underlying container */
    ITER_TRAIT(begin)
    ITER_TRAIT(cbegin)
    ITER_TRAIT(end)
    ITER_TRAIT(cend)
    ITER_TRAIT(rbegin)
    ITER_TRAIT(crbegin)
    ITER_TRAIT(rend)
    ITER_TRAIT(crend)

    #undef ITER_TRAIT
    #undef TRAIT_FLAG

    /* A collection of SFINAE traits that delegates proxy calls to the appropriate
    iterator implementation. */
    template <typename T>
    struct _Traits {
        using C = const T;

        using Begin = std::conditional_t<_begin<T>::exists, _begin<T>, _cbegin<T>>;
        using CBegin = std::conditional_t<_cbegin<T>::exists, _cbegin<T>, _begin<C>>;
        using End = std::conditional_t<_end<T>::exists, _end<T>, _cend<T>>;
        using CEnd = std::conditional_t<_cend<T>::exists, _cend<T>, _end<C>>;
        using RBegin = std::conditional_t<_rbegin<T>::exists, _rbegin<T>, _crbegin<T>>;
        using CRBegin = std::conditional_t<_crbegin<T>::exists, _crbegin<T>, _rbegin<C>>;
        using REnd = std::conditional_t<_rend<T>::exists, _rend<T>, _crend<T>>;
        using CREnd = std::conditional_t<_crend<T>::exists, _crend<T>, _rend<C>>;
    };

    /* A collection of SFINAE traits that delegates proxy calls to the appropriate
    iterator implementation. */
    template <typename T>
    struct _Traits<const T> {
        using C = const T;

        using Begin = std::conditional_t<_begin<C>::exists, _begin<C>, _cbegin<C>>;
        using CBegin = std::conditional_t<_cbegin<C>::exists, _cbegin<C>, _begin<C>>;
        using End = std::conditional_t<_end<C>::exists, _end<C>, _cend<C>>;
        using CEnd = std::conditional_t<_cend<C>::exists, _cend<C>, _end<C>>;
        using RBegin = std::conditional_t<_rbegin<C>::exists, _rbegin<C>, _crbegin<C>>;
        using CRBegin = std::conditional_t<_crbegin<C>::exists, _crbegin<C>, _rbegin<C>>;
        using REnd = std::conditional_t<_rend<C>::exists, _rend<C>, _crend<C>>;
        using CREnd = std::conditional_t<_crend<C>::exists, _crend<C>, _rend<C>>;
    };

    using Traits = _Traits<Container>;

    /* Detect whether the templated type supports the size() method. */
    template <typename T, typename = void>
    struct _has_size : std::false_type {};
    template <typename T>
    struct _has_size<T, std::void_t<decltype(std::declval<T>().size())>> :
        std::true_type
    {};

    /* Detect whether the templated type supports indexing via operator[]. */
    template <typename T, typename = void>
    struct _indexable : std::false_type {};
    template <typename T>
    struct _indexable<T, std::void_t<decltype(std::declval<T>()[0])>> :
        std::true_type
    {};

public:
    using Begin = typename Traits::Begin;
    using CBegin = typename Traits::CBegin;
    using End = typename Traits::End;
    using CEnd = typename Traits::CEnd;
    using RBegin = typename Traits::RBegin;
    using CRBegin = typename Traits::CRBegin;
    using REnd = typename Traits::REnd;
    using CREnd = typename Traits::CREnd;

    static constexpr bool has_begin = Begin::exists;
    static constexpr bool has_cbegin = CBegin::exists;
    static constexpr bool has_end = End::exists;
    static constexpr bool has_cend = CEnd::exists;
    static constexpr bool has_rbegin = RBegin::exists;
    static constexpr bool has_crbegin = CRBegin::exists;
    static constexpr bool has_rend = REnd::exists;
    static constexpr bool has_crend = CREnd::exists;
    static constexpr bool forward_iterable = (
        (has_begin || has_cbegin) && (has_end || has_cend)
    );
    static constexpr bool reverse_iterable = (
        (has_rbegin || has_crbegin) && (has_rend || has_crend)
    );

    static constexpr bool has_size = _has_size<Container>::value;
    static constexpr bool indexable = _indexable<Container>::value;
};


/* A proxy for a C++ container that allows iteration from both C++ and Python. */
template <typename Container, typename Func>
class IterProxy {
    using Traits = ContainerTraits<Container, Func>;

    Container& container;
    Func convert;

    /* IterProxies can only be constructed through `iter()` factory function. */
    template <typename T>
    friend auto iter(T& container)
        -> std::enable_if_t<!is_pyobject<T>, IterProxy<T, identity>>;
    template <typename T, typename _Func>
    friend auto iter(T& container, _Func func)
        -> std::enable_if_t<!is_pyobject<T>, IterProxy<T, _Func>>;

    /* Construct an iterator proxy around a perfectly-forwarded container reference. */
    IterProxy(Container& c) : container(c), convert(Func{}) {}
    IterProxy(Container& c, Func f) : container(c), convert(f) {}

public:

    /////////////////////////////
    ////    C++ INTERFACE    ////
    /////////////////////////////

    /* The proxy uses SFINAE to expose only those methods that exist on the underlying
     * container.  The others are not compiled, and any attempt to use them will result
     * in a compile error.
     */

    /* Delegate to the container's begin() method, if it exists. */
    inline auto begin() {
        static_assert(Traits::has_begin, "container does not implement begin()");
        return Traits::Begin::call(this->container, this->convert);
    }

    /* Delegate to the container's cbegin() method, if it exists. */
    inline auto cbegin() {
        static_assert(Traits::has_cbegin, "container does not implement cbegin()");
        return Traits::CBegin::call(this->container, this->convert);
    }

    /* Delegate to the container's end() method, if it exists. */
    inline auto end() {
        static_assert(Traits::has_end, "container does not implement end()");
        return Traits::End::call(this->container, this->convert);
    }

    /* Delegate to the container's cend() method, if it exists. */
    inline auto cend() {
        static_assert(Traits::has_cend, "container does not implement cend()");
        return Traits::CEnd::call(this->container, this->convert);
    }

    /* Delegate to the container's rbegin() method, if it exists. */
    inline auto rbegin() {
        static_assert(Traits::has_rbegin, "container does not implement rbegin()");
        return Traits::RBegin::call(this->container, this->convert);
    }

    /* Delegate to the container's crbegin() method, if it exists. */
    inline auto crbegin() {
        static_assert(Traits::has_crbegin, "container does not implement crbegin()");
        return Traits::CRBegin::call(this->container, this->convert);
    }

    /* Delegate to the container's rend() method, if it exists. */
    inline auto rend() {
        static_assert(Traits::has_rend, "container does not implement rend()");
        return Traits::REnd::call(this->container, this->convert);
    }

    /* Delegate to the container's crend() method, if it exists. */
    inline auto crend() {
        static_assert(Traits::has_crend, "container does not implement crend()");
        return Traits::CREnd::call(this->container, this->convert);
    }

    /////////////////////////////////
    ////    COUPLED ITERATORS    ////
    /////////////////////////////////

    /* The typical C++ syntax for iterating over a container is a bit clunky at times,
     * especially when it comes to reverse iteration.  Normally, this requires separate
     * calls to `rbegin()` and `rend()`, which are then passed to a manual for loop
     * construction.  This is not very ergonomic, and can be a bit confusing at times.
     * Coupled iterators solve that.
     *
     * A coupled iterator represents a pair of `begin()` and `end()` iterators that are
     * bound into a single object.  This allows for the following syntax:
     *
     *      for (auto& item : iter(container).iter()) {
     *          // forward iteration
     *      }
     *      for (auto& item : iter(container).citer()) {
     *          // forward iteration over const container
     *      }
     *      for (auto& item : iter(container).reverse()) {
     *          // reverse iteration
     *      }
     *      for (auto& item : iter(container).creverse()) {
     *          // reverse iteration over const container
     *      }
     *
     * Which is considerably more readable than the equivalent:
     *
     *      for (auto it = container.rbegin(), end = container.rend(); it != end; ++it) {
     *          // reverse iteration
     *      }
     *
     * NOTE: the `iter()` method is not strictly necessary since the proxy itself
     * implements the standard iterator interface.  As a result, the following syntax
     * is identical in most cases:
     *
     *      for (auto& item : iter(container)) {
     *          // forward iteration
     *      }
     *
     * Lastly, coupled iterators can also be used in manual loop constructions if
     * access to the underlying iterator is required:
     *
     *      for (auto it = iter(container).reverse(); it != it.end(); ++it) {
     *          // reverse iteration
     *      }
     *
     * The `it` variable can then be used just like an ordinary `rbegin()` iterator.
     */

    /* Create a coupled iterator over the container using the begin()/end() methods. */
    inline auto forward() {
        static_assert(
            Traits::Begin::exists && Traits::End::exists,
            "container does not implement begin() and end()"
        );
        using BeginType = typename Traits::Begin::type;
        return CoupledIterator<BeginType>(begin(), end());
    }

    /* Create a coupled iterator over the container using the cbegin()/cend() methods. */
    inline auto cforward() {
        static_assert(
            Traits::CBegin::exists && Traits::CEnd::exists,
            "container does not implement cbegin() and cend()"
        );
        using BeginType = typename Traits::CBegin::type;
        return CoupledIterator<BeginType>(cbegin(), cend());
    }

    /* Create a coupled iterator over the container using the rbegin()/rend() methods. */
    inline auto reverse() {
        static_assert(
            Traits::RBegin::exists && Traits::REnd::exists,
            "container does not implement rbegin() and rend()"
        );
        using BeginType = typename Traits::RBegin::type;
        return CoupledIterator<BeginType>(rbegin(), rend());
    }

    /* Create a coupled iterator over the container using the crbegin()/crend() methods. */
    inline auto creverse() {
        static_assert(
            Traits::CRBegin::exists && Traits::CREnd::exists,
            "container does not implement crbegin() and crend()"
        );
        using BeginType = typename Traits::CRBegin::type;
        return CoupledIterator<BeginType>(crbegin(), crend());
    }

    ////////////////////////////////
    ////    PYTHON INTERFACE    ////
    ////////////////////////////////

    /* If the container's iterators dereference to PyObject* (or can be converted to it
     * using an inline conversion function), then the proxy can produce Python iterators
     * straight from C++.  This allows C++ objects to be iterated over directly from
     * Python using standard `for .. in ..` syntax.
     *
     * Doing so typically requires Cython, since the `iter()` function is only exposed
     * at the C++ level.  The cython/iter.pxd header contains the necessary Cython
     * declarations to do this, and can be included in any Cython module that needs to
     * iterate over C++ containers.
     *
     * This functionality is also baked into the Python-side equivalents of the data
     * structures exposed in the `bertrand::structs` namespace.  For example, here's
     * the implementation of the `__iter__()` method for the `LinkedList` class:
     *
     *      def __iter__(self):
     *          return <object>(iter(self.variant).python())
     *
     * This would ordinarily be an extremely delicate operation with lots of potential
     * for inefficiency and error, but the proxy's unified interface handles all of the
     * heavy lifting for us and yields a valid Python iterator with minimal overhead.
     */

    /* Create a forward Python iterator over the container using the begin()/end()
    methods. */
    inline PyObject* python() {
        static_assert(
            Traits::Begin::exists && Traits::End::exists,
            "container does not implement begin() and end()"
        );
        using PyIter = PyIterator<typename Traits::Begin::type>;
        return PyIter::construct(begin(), end());
    }

    /* Create a forward Python iterator over the container using the cbegin()/cend()
    methods. */
    inline PyObject* cpython() {
        static_assert(
            Traits::CBegin::exists && Traits::CEnd::exists,
            "container does not implement cbegin() and cend()"
        );
        using PyIter = PyIterator<typename Traits::CBegin::type>;
        return PyIter::construct(cbegin(), cend());
    }

    /* Create a backward Python iterator over the container using the rbegin()/rend()
    methods. */
    inline PyObject* rpython() {
        static_assert(
            Traits::RBegin::exists && Traits::REnd::exists,
            "container does not implement rbegin() and rend()"
        );
        using PyIter = PyIterator<typename Traits::RBegin::type>;
        return PyIter::construct(rbegin(), rend());
    }

    /* Create a backward Python iterator over the container using the crbegin()/crend()
    methods. */
    inline PyObject* crpython() {
        static_assert(
            Traits::CRBegin::exists && Traits::CREnd::exists,
            "container does not implement crbegin() and crend()"
        );
        using PyIter = PyIterator<typename Traits::CRBegin::type>;
        return PyIter::construct(crbegin(), crend());
    }

};


///////////////////////////////
////    PYTHON BINDINGS    ////
///////////////////////////////


/* Python bindings involve retrieving a forward or backward Python iterator directly
 * from the CPython API and exposing it to C++ using a standard iterator interface with
 * RAII semantics.  This abstracts away the CPython API (and the associated reference
 * counting/error handling) and allows for standard C++ loop constructs to be used
 * directly on Python containers using the same syntax as C++ containers.
 */


/* A wrapper around a Python iterator that manages reference counts and enables
for-each loop syntax in C++.

The default specialization is chosen whenever a custom conversion is given to the
`iter()` utility function. */
template <typename Container, typename Func>
class PyIterProxy {
    Container const container;  // ptr cannot be reassigned
    Func convert;
    static constexpr bool is_identity = std::is_same_v<Func, identity>;

    template <typename T>
    friend auto iter(T container)
        -> std::enable_if_t<is_pyobject<T>, PyIterProxy<T, identity>>;
    template <typename T, typename _Func>
    friend auto iter(T container, _Func convert)
        -> std::enable_if_t<is_pyobject<T>, PyIterProxy<T, _Func>>;

    /* Construct an iterator proxy around a python container. */
    PyIterProxy(Container c) : container(c) {}
    PyIterProxy(Container c, Func f) : container(c), convert(f) {}

public:

    ///////////////////////
    ////    WRAPPER    ////
    ///////////////////////

    /* A C++ wrapper around a Python iterator that exposes a standard interface. */
    class Iterator {
        Func convert;
        PyObject* py_iterator;
        PyObject* curr;

        /* Ensure that Func is callable with a single argument of the iterator's
        dereferenced value type and infer the corresponding return type. */
        using ConvTraits = FuncTraits<Func, PyObject*>;
        using ReturnType = typename ConvTraits::ReturnType;

        friend PyIterProxy;

        /* Return an iterator to the start of the sequence. */
        Iterator(PyObject* i, Func f) : convert(f), py_iterator(i), curr(nullptr) {
            // NOTE: py_iterator is a borrowed reference from PyObject_GetIter()
            if (py_iterator != nullptr) {
                curr = PyIter_Next(py_iterator);  // get first item
                if (curr == nullptr && PyErr_Occurred()) {
                    Py_DECREF(py_iterator);
                    throw catch_python<RuntimeError>();
                }
            }
        }

        /* Return an iterator to the end of the sequence. */
        Iterator(Func f) : convert(f), py_iterator(nullptr), curr(nullptr) {}

    public:
        // iterator tags for std::iterator_traits
        using iterator_category     = std::forward_iterator_tag;
        using difference_type       = std::ptrdiff_t;
        using value_type            = std::remove_reference_t<ReturnType>;
        using pointer               = value_type*;
        using reference             = value_type&;

        /* Copy constructor. */
        Iterator(const Iterator& other) :
            convert(other.convert), py_iterator(other.py_iterator), curr(other.curr)
        {
            Py_XINCREF(py_iterator);
            Py_XINCREF(curr);
        }

        /* Move constructor. */
        Iterator(Iterator&& other) :
            convert(std::move(other.convert)), py_iterator(other.py_iterator),
            curr(other.curr)
        {
            other.py_iterator = nullptr;
            other.curr = nullptr;
        }

        /* Copy assignment. */
        Iterator& operator=(const Iterator& other) {
            Py_XINCREF(py_iterator);
            Py_XINCREF(curr);
            convert = other.convert;
            py_iterator = other.py_iterator;
            curr = other.curr;
            return *this;
        }

        /* Handle reference counts if an iterator is destroyed partway through
        iteration. */
        ~Iterator() {
            Py_XDECREF(py_iterator);
            Py_XDECREF(curr);
        }

        /* Get current item. */
        inline value_type operator*() const {
            if constexpr (is_identity) {
                return curr;
            } else {
                return convert(curr);
            }
        }

        /* Advance to next item. */
        inline Iterator& operator++() {
            Py_DECREF(curr);
            curr = PyIter_Next(py_iterator);
            if (curr == nullptr && PyErr_Occurred()) {
                throw std::runtime_error("could not get next(iterator)");
            }
            return *this;
        }

        /* Terminate sequence. */
        inline bool operator!=(const Iterator& other) const {
            return curr != other.curr;
        }

    };

    /////////////////////////////
    ////    C++ INTERFACE    ////
    /////////////////////////////

    inline Iterator begin() { return Iterator(this->python(), this->convert); }
    inline Iterator end() { return Iterator(this->convert); }
    inline Iterator rbegin() { return Iterator(this->rpython(), this->convert); }
    inline Iterator rend() { return Iterator(this->convert); }
    inline Iterator begin() const { return cbegin(); }
    inline Iterator end() const { return cend(); }
    inline Iterator rbegin() const { return crbegin(); }
    inline Iterator rend() const { return crend(); }
    inline Iterator cbegin() const { return Iterator(this->python(), this->convert); }
    inline Iterator cend() const { return Iterator(this->convert); }
    inline Iterator crbegin() const { return Iterator(this->rpython(), this->convert); }
    inline Iterator crend() const { return Iterator(this->convert); }

    /////////////////////////////////
    ////    COUPLED ITERATORS    ////
    /////////////////////////////////

    inline auto forward() { return CoupledIterator<Iterator>(begin(), end()); }
    inline auto reverse() { return CoupledIterator<Iterator>(rbegin(), rend()); }
    inline auto forward() const { return cforward(); }
    inline auto reverse() const { return creverse(); }
    inline auto cforward() const { return CoupledIterator<Iterator>(cbegin(), cend()); }
    inline auto creverse() const { return CoupledIterator<Iterator>(crbegin(), crend()); }

    ////////////////////////////////
    ////    PYTHON INTERFACE    ////
    ////////////////////////////////

    /* Get a mutable forward Python iterator over a container. */
    inline PyObject* python() {
        PyObject* iter = PyObject_GetIter(this->container);
        if (iter == nullptr && PyErr_Occurred()) {
            throw catch_python<TypeError>();
        }
        return iter;
    }

    /* Get a mutable reverse Python iterator over a container. */
    inline PyObject* rpython() {
        PyObject* attr = PyObject_GetAttrString(this->container, "__reversed__");
        if (attr == nullptr && PyErr_Occurred()) {
            throw catch_python<TypeError>();
        }
        PyObject* iter = PyObject_CallObject(attr, nullptr);
        Py_DECREF(attr);
        if (iter == nullptr && PyErr_Occurred()) {
            throw catch_python<TypeError>();
        }
        return iter;  // new reference
    }

    /* Get a const forward Python iterator over a const container. */
    inline PyObject* python() const {
        return this->cpython();
    }

    /* Get a const reverse Python iterator over a const container. */
    inline PyObject* rpython() const {
        return this->crpython();
    }

    /* Get a const forward Python iterator over a container. */
    inline PyObject* cpython() const {
        return this->python();
    }

    /* Get a reverse Python iterator over an immutable container. */
    inline PyObject* crpython() const {
        return this->rpython();
    }

};



//////////////////////
////    iter()    ////
//////////////////////


/* Create a C++ to Python iterator proxy for a container. */
template <typename Container>
inline auto iter(Container& container)
    -> std::enable_if_t<!is_pyobject<Container>, IterProxy<Container, identity>>
{
    return IterProxy<Container, identity>(container);
}


/* Create a C++ to Python iterator proxy for a container, applying a custom conversion
function at each dereference. */
template <typename Container, typename Func>
inline auto iter(Container& container, Func func)
    -> std::enable_if_t<!is_pyobject<Container>, IterProxy<Container, Func>>
{
    return IterProxy<Container, Func>(container, func);
}


/* Create a Python to C++ iterator proxy for a mutable Python container. */
template <typename Container>
inline auto iter(Container container)
    -> std::enable_if_t<is_pyobject<Container>, PyIterProxy<Container, identity>>
{
    return PyIterProxy<Container, identity>(container);
}


/* Create a Python to C++ iterator proxy for a mutable Python container. */
template <typename Container, typename Func>
inline auto iter(Container container, Func convert)
    -> std::enable_if_t<is_pyobject<Container>, PyIterProxy<Container, Func>>
{
    return PyIterProxy<Container, Func>(container, convert);
}


}  // namespace util


/* Export to base namespace. */
using util::iter;


}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_UTIL_ITER_H
