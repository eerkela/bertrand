// include guard: BERTRAND_STRUCTS_UTIL_ITER_H
#ifndef BERTRAND_STRUCTS_UTIL_ITER_H
#define BERTRAND_STRUCTS_UTIL_ITER_H

#include <optional>  // std::optional
#include <string_view>  // std::string_view
#include <type_traits>
#include <typeinfo>  // typeid
#include <Python.h>  // CPython API
#include "slot.h"  // Slot
#include "python.h"  // PyIterator


namespace bertrand {
namespace structs {
namespace util {


////////////////////////////////////
////    FORWARD DECLARATIONS    ////
////////////////////////////////////


// template <typename Iterator, const std::string_view& name>
// class PyIterator;


template <typename Container, typename Func, bool rvalue>
class IterProxy;


// template <typename Container, typename Func>
// class PyIterProxy;


//////////////////////////////
////    ITER() FACTORY    ////
//////////////////////////////


/* The `iter()` method represents a two-way bridge between Python and C++ containers
implementing the standard iterator interface.

When called with a C++ container, the `iter()` method produces a proxy that forwards
the container's original iterator interface (however it is defined).  The proxy uses
these methods to generate equivalent Python iterators with corresponding `iter()` and
`next()` methods, which can be returned directly to the standard Python interpreter.
This translation works as long as the C++ iterators dereference to PyObject*, or if a
custom conversion function is provided via the `convert` argument.  This allows users
to insert a conversion in between the iterator dereference and the return of the
`next()` method on the Python side.  For example, if the C++ iterator dereferences to a
custom struct, the user can provide a lambda conversion that translates the struct into
a valid PyObject* reference, which is then returned to Python like normal.

When called with a Python container, the `iter()` method produces an equivalent proxy
that wraps the `PyObject_GetIter()` C API function and exposes a standard C++ interface
on the other side.  Just like the C++ to Python conversion, custom conversion functions
can be added in between the result of the `next()` method on the Python side and the
iterator dereference on the C++ side.  Note that due to the dynamic nature of Python's
type system, conversions of this sort will require foreknowledge of the container's
specific element type in order to perform the casts necessary to narrow the types to
their C++ equivalents.  In order to facilitate this, all the data structures exposed in
the `bertrand::structs` namespace support optional Python-side type specialization,
which can be used to enforce homogeneity at the container level.
*/


// TODO: proxies need to compile into two different forms based on whether a key
// function is provided or not, similar to how they compile into different forms for
// lvalue and rvalue references.

// non-keyed version includes an assertion that the iterators dereference to PyObject*.
// The keyed version requires that the key function accepts a single PyObject* and uses
// the inferred return value of the function type to determine the value_type of the
// resulting iterator.


/* A no-op function object that returns its argument unchanged. */
struct identity {
    template <typename T>
    inline constexpr T&& operator()(T&& value) const noexcept {
        return std::forward<T>(value);
    }
};


/*    LVALUE REFERENCES    */


/* Create a C++ to Python iterator proxy for a mutable C++ lvalue container. */
template <typename Container>
inline IterProxy<Container, identity, false> iter(Container& container) {
    return IterProxy<Container, identity, false>(container);
}


/* Create a C++ to Python iterator proxy for a const lvalue container. */
template <typename Container>
inline IterProxy<const Container, identity, false> iter(const Container& container) {
    return IterProxy<const Container, identity, false>(container);
}


/* Create a C++ to Python iterator proxy for a mutable C++ lvalue container. */
template <typename Container, typename Func>
inline IterProxy<Container, Func, false> iter(Container& container, Func convert) {
    return IterProxy<Container, Func, false>(container, convert);
}


/* Create a C++ to Python iterator proxy for a const lvalue container. */
template <typename Container, typename Func>
inline IterProxy<const Container, Func, false> iter(
    const Container& container,
    Func convert
) {
    return IterProxy<const Container, Func, false>(container, convert);
}


/*    RVALUE REFERENCES    */


/* Create a C++ to Python iterator proxy for a mutable rvalue container. */
template <typename Container>
inline IterProxy<Container, identity, true> iter(Container&& container) {
    return IterProxy<Container, identity, true>(std::move(container));
}


/* Create a C++ to Python iterator proxy for a const rvalue container. */
template <typename Container>
inline IterProxy<const Container, identity, true> iter(const Container&& container) {
    return IterProxy<const Container, identity, true>(std::move(container));
}


/* Create a C++ to Python iterator proxy for a mutable rvalue container. */
template <typename Container, typename Func>
inline IterProxy<Container, Func, true> iter(Container&& container, Func convert) {
    return IterProxy<Container, Func, true>(std::move(container), convert);
}


/* Create a C++ to Python iterator proxy for a const rvalue container. */
template <typename Container, typename Func>
inline IterProxy<const Container, Func, true> iter(
    const Container&& container,
    Func convert
) {
    return IterProxy<const Container, Func, true>(std::move(container), convert);
}


/*    PYTHON REFERENCES    */


// /* Create a Python to C++ iterator proxy for a mutable Python container. */
// template <typename Container>
// inline PyIterProxy<Container, identity> iter(PyObject* container) {
//     return PyIterProxy<Container, identity>(container);
// }


// /* Create a Python to C++ iterator proxy for a const Python container. */
// template <typename Container>
// inline PyIterProxy<const Container, identity> iter(const PyObject* container) {
//     return PyIterProxy<const Container, identity>(container);
// }


// /* Create a Python to C++ iterator proxy for a mutable Python container. */
// template <typename Container, typename Func>
// inline PyIterProxy<Container, Func> iter(PyObject* container, Func convert) {
//     return PyIterProxy<Container, Func>(container, convert);
// }


// /* Create a Python to C++ iterator proxy for a const Python container. */
// template <typename Container, typename Func>
// inline PyIterProxy<const Container, Func> iter(
//     const PyObject* container,
//     Func convert
// ) {
//     return PyIterProxy<const Container, Func>(container, convert);
// }


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
 * is forwarded as the proxy's own begin(), cbegin(), end() (etc.) member methods,
 * which standardizes the interface for all types of iterable containers.
 *
 * Python iterators are then constructed by coupling various pairs of `begin()` and
 * `end()` iterators and packaging them into an appropriate PyObject*, as returned by
 * the following proxy methods:
 *      python()            // (begin() + end())
 *      cpython()           // (cbegin() + cend())
 *      rpython()           // (rbegin() + rend())
 *      crpython()          // (crbegin() + crend())
 */


/* Base class for IterProxies around lvalue container references. */
template <typename Container, typename Func, bool rvalue = false>
class _IterProxy {
public:
    Container& container;
    const Func convert;
    _IterProxy(Container& c) : container(c), convert(Func{}) {}
    _IterProxy(Container& c, Func f) : container(c), convert(f) {}
};

/* Base class for IterProxies around rvalue container references.

NOTE: constructing an rvalue proxy requires moving the container, so there is a small
overhead compared to the lvalue proxy.  However, this is minimized as much as possible
through the use of a move constructor, which should be fairly efficient. */
template <typename Container, typename Func>
class _IterProxy<Container, Func, true> {
public:
    Container container;
    const Func convert;
    _IterProxy(Container&& c) : container(std::move(c)), convert(Func{}) {}
    _IterProxy(Container&& c, Func f) : container(std::move(c)), convert(f) {}
};


/* A proxy for a C++ container that allows iteration from both C++ and Python. */
template <typename Container, typename Func, bool rvalue>
class IterProxy : public _IterProxy<Container, Func, rvalue> {
    using Base = _IterProxy<Container, Func, rvalue>;
    static constexpr bool is_identity = std::is_same_v<Func, identity>;

    /* Get the Python-compatible name of the templated iterator, defaulting to the
    mangled C++ type name. */
    template <typename T, typename = void>
    struct type_name {
        static constexpr std::string_view value { typeid(T).name() };
    };

    /* Get the Python-compatible name of the templated iterator, using the static
    `name` attribute if it is available. */
    template <typename T>
    struct type_name<T, std::void_t<decltype(T::name)>> {
        static constexpr std::string_view value { T::name };
    };

    /* Get the final type for a valid iterator method without any conversions. */
    template <typename T, bool cond = false>
    struct iter_wrapper {
        using type = T;
        inline static type convert(T&& iter) {
            return iter;
        }
    };

    /* Get the final type for a valid iterator method with conversions. */
    template <typename T>
    struct iter_wrapper<T, true> {
        struct type : public T {
            const Func _bertrand_convert;
            using T::T;  // inherit constructors
            inline auto operator*() const {
                assert(_bertrand_convert != nullptr);
                return _bertrand_convert)(T::operator*());  // apply conversion
            }
        };
        inline static type convert(T&& iter) {
            type result(iter);
            result._bertrand_convert = &(Base::convert);
            return result;
        }
    };

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
    through to the proxy's `.begin()` member, which represents a unified interface for
    all types of iterable containers.  This allows us to write generic code that works
    with any container that implements the standard library iterator interface, without
    having to manually extend the proxy for each new container type.

    See https://en.cppreference.com/w/cpp/language/adl for more information on ADL and
    non-member functions. */
    #define TRAIT_FLAG(FLAG_NAME, STATEMENT) \
        template <typename T, typename = void> \
        struct FLAG_NAME : std::false_type {}; \
        template <typename T> \
        struct FLAG_NAME<T, std::void_t<decltype(STATEMENT)>> : std::true_type {}; \

    #define ITER_TRAIT(METHOD) \
        /* Default specialization for methods that don't exist on the Iterable type. */ \
        template <typename T, typename = void, typename = void, typename = void> \
        struct _##METHOD { \
            using type = void; \
            static constexpr bool exists = false; \
            static constexpr std::string_view name { "" }; \
        }; \
        /* First, check for a member method of the same name within Iterable. */ \
        template <typename T> \
        struct _##METHOD< \
            T, \
            std::void_t<decltype(std::declval<T&>().METHOD())> \
        > { \
            using base_type = decltype(std::declval<T&>().METHOD()); \
            using wrapper = iter_wrapper<base_type, is_identity>; \
            using type = typename wrapper::type; \
            static constexpr bool exists = true; \
            static constexpr std::string_view name { type_name<type>::value }; \
            inline static type func(T& iterable) { \
                return wrapper::convert(iterable.METHOD()); \
            } \
        }; \
        TRAIT_FLAG(has_member_##METHOD, std::declval<T&>().METHOD()) \
        /* Second, check for a non-member ADL method within the same namespace. */ \
        template <typename T> \
        struct _##METHOD< \
            T, \
            void, \
            std::enable_if_t< \
                !has_member_##METHOD<T>::value, \
                std::void_t<decltype(METHOD(std::declval<T&>()))> \
            > \
        > { \
            using base_type = decltype(METHOD(std::declval<T&>())); \
            using type = typename iter_wrapper<base_type, is_identity>::type; \
            static constexpr bool exists = true; \
            static constexpr std::string_view name { type_name<type>::value }; \
            inline static type func(T& iterable) { \
                if constexpr (is_identity) { \
                    return METHOD(iterable); \
                } else { \
                    type iter { METHOD(iterable) }; \
                    iter._bertrand_convert = &(Base::convert); \
                    return iter; \
                } \
            } \
        }; \
        TRAIT_FLAG(has_adl_##METHOD, METHOD(std::declval<T&>())) \
        /* Third, check for an equivalently-named standard library method. */ \
        template <typename T> \
        struct _##METHOD< \
            T, \
            void, \
            void, \
            std::enable_if_t< \
                !has_member_##METHOD<T>::value && !has_adl_##METHOD<T>::value, \
                std::void_t<decltype(std::METHOD(std::declval<T&>()))> \
            > \
        > { \
            using base_type = decltype(std::METHOD(std::declval<T&>())); \
            using type = typename iter_wrapper<base_type, is_identity>::type; \
            static constexpr bool exists = true; \
            static constexpr std::string_view name { type_name<type>::value }; \
            inline static type func(T& iterable) { \
                if constexpr (is_identity) { \
                    return std::METHOD(iterable); \
                } else { \
                    type iter { std::METHOD(iterable) }; \
                    iter._bertrand_convert = &(Base::convert); \
                    return iter; \
                } \
            } \
        }; \

    /* A collection of SFINAE traits that allows introspection of a mutable container's
    iterator interface. */
    template <typename Iterable>
    class _Traits {
        ITER_TRAIT(begin)
        ITER_TRAIT(cbegin)
        ITER_TRAIT(end)
        ITER_TRAIT(cend)
        ITER_TRAIT(rbegin)
        ITER_TRAIT(crbegin)
        ITER_TRAIT(rend)
        ITER_TRAIT(crend)

    public:
        using begin = _begin<Iterable>;
        using cbegin = _cbegin<Iterable>;
        using end = _end<Iterable>;
        using cend = _cend<Iterable>;
        using rbegin = _rbegin<Iterable>;
        using crbegin = _crbegin<Iterable>;
        using rend = _rend<Iterable>;
        using crend = _crend<Iterable>;
    };

    /* A collection of SFINAE traits that allows introspection of a const container's
    iterator interface.

    NOTE: this is slightly different from the mutable version in that the mutable
    begin(), end(), rbegin(), and rend() methods internally delegate to their const
    equivalents if they exist.  This is mostly for convenience so that we don't have to
    explicitly switch from begin() to cbegin() whenever we want to iterate over a const
    container. */
    template <typename Iterable>
    class _Traits<const Iterable> {
        ITER_TRAIT(cbegin)
        ITER_TRAIT(cend)
        ITER_TRAIT(crbegin)
        ITER_TRAIT(crend)

    public:
        using begin = _cbegin<const Iterable>;
        using cbegin = _cbegin<const Iterable>;
        using end = _cend<const Iterable>;
        using cend = _cend<const Iterable>;
        using rbegin = _crbegin<const Iterable>;
        using crbegin = _crbegin<const Iterable>;
        using rend = _crend<const Iterable>;
        using crend = _crend<const Iterable>;
    };

    #undef ITER_TRAIT
    #undef TRAIT_FLAG

    using Traits = _Traits<Container>;

public:

    /* Delegate to the container's begin() method, if it exists. */
    template <bool cond = Traits::begin::exists>
    inline auto begin() -> std::enable_if_t<cond, typename Traits::begin::type> {
        return Traits::begin::func(this->container);
    }

    /* Delegate to the container's cbegin() method, if it exists. */
    template <bool cond = Traits::cbegin::exists>
    inline auto cbegin() -> std::enable_if_t<cond, typename Traits::cbegin::type> {
        return Traits::cbegin::func(this->container);
    }

    /* Delegate to the container's end() method, if it exists. */
    template <bool cond = Traits::end::exists>
    inline auto end() -> std::enable_if_t<cond, typename Traits::end::type> {
        return Traits::end::func(this->container);
    }

    /* Delegate to the container's cend() method, if it exists. */
    template <bool cond = Traits::cend::exists>
    inline auto cend() -> std::enable_if_t<cond, typename Traits::cend::type> {
        return Traits::cend::func(this->container);
    }

    /* Delegate to the container's rbegin() method, if it exists. */
    template <bool cond = Traits::rbegin::exists>
    inline auto rbegin() -> std::enable_if_t<cond, typename Traits::rbegin::type> {
        return Traits::rbegin::func(this->container);
    }

    /* Delegate to the container's crbegin() method, if it exists. */
    template <bool cond = Traits::crbegin::exists>
    inline auto crbegin() -> std::enable_if_t<cond, typename Traits::crbegin::type> {
        return Traits::crbegin::func(this->container);
    }

    /* Delegate to the container's rend() method, if it exists. */
    template <bool cond = Traits::rend::exists>
    inline auto rend() -> std::enable_if_t<cond, typename Traits::rend::type> {
        return Traits::rend::func(this->container);
    }

    /* Delegate to the container's crend() method, if it exists. */
    template <bool cond = Traits::crend::exists>
    inline auto crend() -> std::enable_if_t<cond, typename Traits::crend::type> {
        return Traits::crend::func(this->container);
    }

    /* Create a forward Python iterator over the container using the begin()/end()
    methods. */
    template <bool cond = Traits::begin::exists && Traits::end::exists>
    inline auto python() -> std::enable_if_t<cond, PyObject*> {
        using Iter = PyIterator<typename Traits::begin::type, Traits::begin::name>;
        return Iter::init(begin(), end());
    }

    /* Create a forward Python iterator over the container using the cbegin()/cend()
    methods. */
    template <bool cond = Traits::cbegin::exists && Traits::cend::exists>
    inline auto cpython() -> std::enable_if_t<cond, PyObject*> {
        using Iter = PyIterator<typename Traits::cbegin::type, Traits::cbegin::name>;
        return Iter::init(cbegin(), cend());
    }

    /* Create a backward Python iterator over the container using the rbegin()/rend()
    methods. */
    template <bool cond = Traits::rbegin::exists && Traits::rend::exists>
    inline auto rpython() -> std::enable_if_t<cond, PyObject*> {
        using Iter = PyIterator<typename Traits::rbegin::type, Traits::rbegin::name>;
        return Iter::init(rbegin(), rend());
    }

    /* Create a backward Python iterator over the container using the crbegin()/crend()
    methods. */
    template <bool cond = Traits::crbegin::exists && Traits::crend::exists>
    inline auto crpython() -> std::enable_if_t<cond, PyObject*> {
        using Iter = PyIterator<typename Traits::crbegin::type, Traits::crbegin::name>;
        return Iter::init(crbegin(), crend());
    }

private:
    /* IterProxies can only be constructed through `iter()` factory function. */
    template <typename _Container>
    friend IterProxy<_Container, identity, false> iter(_Container& container);
    template <typename _Container>
    friend IterProxy<const _Container, identity, false> iter(const _Container& container);
    template <typename _Container, typename _Func>
    friend IterProxy<_Container, _Func, false> iter(_Container& container, _Func convert);
    template <typename _Container, typename _Func>
    friend IterProxy<const _Container, _Func, false> iter(
        const _Container& container,
        _Func convert
    );
    template <typename _Container>
    friend IterProxy<_Container, identity, true> iter(_Container&& container);
    template <typename _Container>
    friend IterProxy<const _Container, identity, true> iter(const _Container&& container);
    template <typename _Container, typename _Func>
    friend IterProxy<_Container, _Func, true> iter(_Container&& container, _Func convert);
    template <typename _Container, typename _Func>
    friend IterProxy<const _Container, _Func, true> iter(
        const _Container&& container,
        _Func convert
    );

    /* Construct an iterator proxy around a an lvalue container. */
    template <bool cond = rvalue, std::enable_if_t<!cond, int> = 0>
    IterProxy(Container& container) : Base(container) {}
    template <bool cond = rvalue, std::enable_if_t<!cond, int> = 0>
    IterProxy(Container& container, Func convert) : Base(container, convert) {}

    /* Construct an iterator proxy around an rvalue container. */
    template <bool cond = rvalue, std::enable_if_t<cond, int> = 0>
    IterProxy(Container&& container) : Base(std::move(container)) {}
    template <bool cond = rvalue, std::enable_if_t<cond, int> = 0>
    IterProxy(Container&& container, Func convert) : Base(std::move(container), convert) {}
};


///////////////////////////////
////    PYTHON BINDINGS    ////
///////////////////////////////


/* Python bindings involve retrieving a forward or backward Python iterator directly
 * from the CPython API and exposing it to C++ using a standard iterator interface plus
 * RAII semantics.  This abstracts away the CPython API (and the associated reference
 * counting/error handling) and allows for standard C++ loop constructs to be used
 * directly on Python containers.
 */


// /* A wrapper around a C++ iterator that allows it to be used from Python. */
// template <typename Iterator, const std::string_view& name>
// class PyIterator {
//     // sanity check
//     static_assert(
//         std::is_convertible_v<typename Iterator::value_type, PyObject*>,
//         "Iterator must dereference to PyObject*"
//     );

//     /* Store coupled iterators as raw data buffers.
    
//     NOTE: PyObject_New() does not allow for traditional stack allocation like we would
//     normally use to store the wrapped iterators.  Instead, we have to delay construction
//     until the init() method is called.  We could use pointers to heap-allocated memory
//     for this, but this adds extra allocation overhead.  Using raw data buffers avoids
//     this and places the iterators on the stack, where they belong. */
//     PyObject_HEAD
//     Slot<Iterator> first;
//     Slot<Iterator> second;

//     /* Force users to use init() factory method. */
//     PyIterator() = delete;
//     PyIterator(const PyIterator&) = delete;
//     PyIterator(PyIterator&&) = delete;

// public:

//     /* Construct a Python iterator from a C++ iterator range. */
//     inline static PyObject* init(Iterator&& begin, Iterator&& end) {
//         // create new iterator instance
//         PyIterator* result = PyObject_New(PyIterator, &Type);
//         if (result == nullptr) {
//             throw std::runtime_error("could not allocate Python iterator");
//         }

//         // initialize (NOTE: PyObject_New() does not call stack constructors)
//         new (&(result->first)) Slot<Iterator>();
//         new (&(result->second)) Slot<Iterator>();

//         // construct iterators within raw storage
//         result->first.construct(std::move(begin));
//         result->second.construct(std::move(end));

//         // return as PyObject*
//         return reinterpret_cast<PyObject*>(result);
//     }

//     /* Construct a Python iterator from a coupled iterator. */
//     inline static PyObject* init(CoupledIterator<Iterator>&& iter) {
//         return init(iter.begin(), iter.end());
//     }

//     /* Call next(iter) from Python. */
//     inline static PyObject* iter_next(PyIterator* self) {
//         Iterator& begin = *(self->first);
//         Iterator& end = *(self->second);

//         if (!(begin != end)) {  // terminate the sequence
//             PyErr_SetNone(PyExc_StopIteration);
//             return nullptr;
//         }

//         // increment iterator and return current value
//         PyObject* result = *begin;
//         ++begin;
//         return Py_NewRef(result);  // new reference
//     }

//     /* Free the Python iterator when its reference count falls to zero. */
//     inline static void dealloc(PyIterator* self) {
//         Type.tp_free(self);
//     }

// private:

//     /* Initialize a PyTypeObject to represent this iterator from Python. */
//     static PyTypeObject init_type() {
//         PyTypeObject type_obj;  // zero-initialize
//         type_obj.tp_name = name.data();
//         type_obj.tp_doc = "Python-compatible wrapper around a C++ iterator.";
//         type_obj.tp_basicsize = sizeof(PyIterator);
//         type_obj.tp_flags = (
//             Py_TPFLAGS_DEFAULT | Py_TPFLAGS_IMMUTABLETYPE |
//             Py_TPFLAGS_DISALLOW_INSTANTIATION
//         );
//         type_obj.tp_alloc = PyType_GenericAlloc;
//         type_obj.tp_iter = PyObject_SelfIter;
//         type_obj.tp_iternext = (iternextfunc) iter_next;
//         type_obj.tp_dealloc = (destructor) dealloc;

//         // register iterator type with Python
//         if (PyType_Ready(&type_obj) < 0) {
//             throw std::runtime_error("could not initialize PyIterator type");
//         }
//         return type_obj;
//     }

//     /* C-style Python type declaration. */
//     inline static PyTypeObject Type = init_type();

// };


// /* A wrapper around a Python iterator that manages reference counts and enables
// for-each loop syntax in C++. */
// template <typename Container, bool rvalue>
// class PyIterProxy {
// public:
//     class Iterator;
//     using IteratorPair = CoupledIterator<Iterator>;

//     /* Construct a PyIterable from a Python sequence. */
//     PyIterable(PyObject* seq) : py_iterator(PyObject_GetIter(seq)) {
//         if (py_iterator == nullptr) {
//             throw std::invalid_argument("could not get iter(sequence)");
//         }
//     }

//     /* Release the Python sequence. */
//     ~PyIterable() { Py_DECREF(py_iterator); }

//     /* Iterate over the sequence. */
//     inline Iterator begin() const { return Iterator(py_iterator); }
//     inline Iterator end() const { return Iterator(); }

//     class Iterator {
//     public:
//         // iterator tags for std::iterator_traits
//         using iterator_category     = std::forward_iterator_tag;
//         using difference_type       = std::ptrdiff_t;
//         using value_type            = PyObject*;
//         using pointer               = PyObject**;
//         using reference             = PyObject*&;

//         /* Get current item. */
//         PyObject* operator*() const { return curr; }

//         /* Advance to next item. */
//         Iterator& operator++() {
//             Py_DECREF(curr);
//             curr = PyIter_Next(py_iterator);
//             if (curr == nullptr && PyErr_Occurred()) {
//                 throw std::runtime_error("could not get next(iterator)");
//             }
//             return *this;
//         }

//         /* Terminate sequence. */
//         bool operator!=(const Iterator& other) const { return curr != other.curr; }

//         /* Handle reference counts if an iterator is destroyed partway through
//         iteration. */
//         ~Iterator() { Py_XDECREF(curr); }

//     private:
//         friend PyIterable;
//         friend IteratorPair;
//         PyObject* py_iterator;
//         PyObject* curr;

//         /* Return an iterator to the start of the sequence. */
//         Iterator(PyObject* py_iterator) : py_iterator(py_iterator), curr(nullptr) {
//             if (py_iterator != nullptr) {
//                 curr = PyIter_Next(py_iterator);
//                 if (curr == nullptr && PyErr_Occurred()) {
//                     throw std::runtime_error("could not get next(iterator)");
//                 }
//             }
//         }

//         /* Return an iterator to the end of the sequence. */
//         Iterator() : py_iterator(nullptr), curr(nullptr) {}
//     };

// protected:
//     PyObject* py_iterator;
// };


///////////////////////////
////    CONVERSIONS    ////
///////////////////////////



/* A decorator for a standard C++ iterator that applies a custom conversion at
each step. */
template <typename Iterator, typename Func>
class ConvertedIterator {
    Iterator iter;
    const Func convert;

public:

    /* Construct a converted iterator from a standard C++ iterator and a conversion
    function. */
    inline ConvertedIterator(Iterator&& i, Func f) : iter(std::move(i)), convert(f) {}

    /* Dereference the iterator and apply the conversion function. */
    inline auto operator*() const {
        return convert(*iter);
    }
    template <typename T>
    inline auto operator[](T&& index) const {
        return convert(iter[index]);
    }

    /* Forward all other methods to the underlying iterator. */
    inline ConvertedIterator& operator++() {
        ++iter;
        return *this;
    }
    inline ConvertedIterator operator++(int) {
        ConvertedIterator temp(*this);
        ++iter;
        return temp;
    }
    inline ConvertedIterator& operator--() {
        --iter;
        return *this;
    }
    inline ConvertedIterator operator--(int) {
        ConvertedIterator temp(*this);
        --iter;
        return temp;
    }
    inline bool operator==(const ConvertedIterator& other) const {
        return iter == other.iter;
    }
    inline bool operator!=(const ConvertedIterator& other) const {
        return iter != other.iter;
    }
    template <typename T>
    inline ConvertedIterator& operator+=(T&& other) {
        iter += other;
        return *this;
    }
    template <typename T>
    inline ConvertedIterator& operator-=(T&& other) {
        iter -= other;
        return *this;
    }
    inline bool operator<(const ConvertedIterator& other) const {
        return iter < other.iter;
    }
    inline bool operator>(const ConvertedIterator& other) const {
        return iter > other.iter;
    }
    inline bool operator<=(const ConvertedIterator& other) const {
        return iter <= other.iter;
    }
    inline bool operator>=(const ConvertedIterator& other) const {
        return iter >= other.iter;
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


template <typename T, typename Iterator, typename Func>
ConvertedIterator<Iterator, Func> operator+(
    const ConvertedIterator<Iterator, Func>& iter,
    T n
) {
    return iter.iter + n;
}


template <typename T, typename Iterator, typename Func>
auto operator+(T n, const ConvertedIterator<Iterator, Func>& iter) {
    return n + iter.iter;
}


template <typename T, typename Iterator, typename Func>
ConvertedIterator<Iterator, Func> operator-(
    const ConvertedIterator<Iterator, Func>& iter,
    T n
) {
    return iter.iter - n;
}


template <typename T, typename Iterator, typename Func>
ConvertedIterator<Iterator, Func> operator-(
    T n,
    const ConvertedIterator<Iterator, Func>& iter
) {
    return n - iter.iter;
}



}  // namespace util
}  // namespace structs
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_UTIL_ITER_H
