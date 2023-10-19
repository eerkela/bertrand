// include guard: BERTRAND_STRUCTS_UTIL_ITER_H
#ifndef BERTRAND_STRUCTS_UTIL_ITER_H
#define BERTRAND_STRUCTS_UTIL_ITER_H

#include <optional>  // std::optional
#include <string_view>  // std::string_view
#include <type_traits>  // std::enable_if_t, std::is_same_v, std::void_t
#include <typeinfo>  // typeid
#include <Python.h>  // CPython API
#include <utility>  // std::declval, std::move
#include "coupled_iter.h"  // CoupledIterator
#include "func.h"  // identity, FuncTraits
#include "python.h"  // PyIterator
#include "slot.h"  // Slot


namespace bertrand {
namespace structs {
namespace util {


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
    using IterTraits = std::iterator_traits<Iterator>;

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
        exists<typename IterTraits::iterator_category>,
        typename IterTraits::iterator_category
    >;
    using pointer = std::enable_if_t<
        exists<typename IterTraits::pointer>,
        typename IterTraits::pointer
    >;
    using reference = std::enable_if_t<
        exists<typename IterTraits::reference>,
        typename IterTraits::reference
    >;
    using value_type = std::enable_if_t<
        exists<typename IterTraits::value_type>,
        typename IterTraits::value_type
    >;
    using difference_type = std::enable_if_t<
        exists<typename IterTraits::difference_type>,
        typename IterTraits::difference_type
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


/* A proxy for a C++ container that allows iteration from both C++ and Python. */
template <typename Container, typename Func, bool rvalue>
class IterProxy {
    // NOTE: if rvalue is true, then we own the container.  Otherwise, we only keep
    // a reference to it.
    std::conditional_t<rvalue, Container, Container&> container;
    Func convert;
    static constexpr bool is_identity = std::is_same_v<Func, identity>;

    /* Get a wrapper around an iterator that applies a conversion function to the
    result of its dereference operator. */
    template <typename Iter, bool cond = false>
    struct conversion_wrapper {
        using type = ConvertedIterator<Iter, Func>;
        inline static type decorate(Iter&& iter, Func func) {
            return type(std::move(iter), func);
        }
    };

    /* If no conversion function is given, return the iterator unmodified. */
    template <typename Iter>
    struct conversion_wrapper<Iter, true> {
        using type = Iter;
        inline static type decorate(Iter&& iter, Func func) {
            return iter;
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
    all types of iterable containers.  If none of these methods exist, the proxy's
    `.begin()` member is not defined, and any attempt to use it will result in a
    compile error.

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
        template <typename Iterable, typename = void, typename = void, typename = void> \
        struct _##METHOD { \
            using type = void; \
            static constexpr bool exists = false; \
            static constexpr std::string_view name { "" }; \
        }; \
        /* First, check for a member method of the same name within Iterable. */ \
        template <typename Iterable> \
        struct _##METHOD< \
            Iterable, \
            std::void_t<decltype(std::declval<Iterable&>().METHOD())> \
        > { \
            using base_type = decltype(std::declval<Iterable&>().METHOD()); \
            using wrapper = conversion_wrapper<base_type, is_identity>; \
            using type = typename wrapper::type; \
            static constexpr bool exists = true; \
            inline static type call(Iterable& iterable, Func func) { \
                return wrapper::decorate(iterable.METHOD(), func); \
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
            using base_type = decltype(METHOD(std::declval<Iterable&>())); \
            using wrapper = conversion_wrapper<base_type, is_identity>; \
            using type = typename wrapper::type; \
            static constexpr bool exists = true; \
            inline static type call(Iterable& iterable, Func func) { \
                return wrapper::decorate(METHOD(iterable), func); \
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
            using base_type = decltype(std::METHOD(std::declval<Iterable&>())); \
            using wrapper = conversion_wrapper<base_type, is_identity>; \
            using type = typename wrapper::type; \
            static constexpr bool exists = true; \
            inline static type call(Iterable& iterable, Func func) { \
                return wrapper::decorate(std::METHOD(iterable), func); \
            } \
        }; \

    /* A collection of SFINAE traits that allows introspection of a mutable container's
    iterator interface. */
    template <typename _Iterable>
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
        using begin = _begin<_Iterable>;
        using cbegin = _cbegin<_Iterable>;
        using end = _end<_Iterable>;
        using cend = _cend<_Iterable>;
        using rbegin = _rbegin<_Iterable>;
        using crbegin = _crbegin<_Iterable>;
        using rend = _rend<_Iterable>;
        using crend = _crend<_Iterable>;
    };

    /* A collection of SFINAE traits that allows introspection of a const container's
    iterator interface.

    NOTE: this is slightly different from the mutable version in that the mutable
    begin(), end(), rbegin(), and rend() methods internally delegate to their const
    equivalents if they exist.  This is mostly for convenience so that we don't have to
    explicitly switch from begin() to cbegin() whenever we want to iterate over a const
    container. */
    template <typename _Iterable>
    class _Traits<const _Iterable> {
        ITER_TRAIT(cbegin)
        ITER_TRAIT(cend)
        ITER_TRAIT(crbegin)
        ITER_TRAIT(crend)

    public:
        using begin = _cbegin<const _Iterable>;
        using cbegin = _cbegin<const _Iterable>;
        using end = _cend<const _Iterable>;
        using cend = _cend<const _Iterable>;
        using rbegin = _crbegin<const _Iterable>;
        using crbegin = _crbegin<const _Iterable>;
        using rend = _crend<const _Iterable>;
        using crend = _crend<const _Iterable>;
    };

    #undef ITER_TRAIT
    #undef TRAIT_FLAG

public:
    using Traits = _Traits<Container>;

    /////////////////////////////
    ////    C++ INTERFACE    ////
    /////////////////////////////

    /* The proxy uses SFINAE to expose only those methods that exist on the underlying
     * container.  The others are not compiled, and any attempt to use them will result
     * in a compile error.
     */

    /* Delegate to the container's begin() method, if it exists. */
    template <bool cond = Traits::begin::exists>
    inline auto begin() -> std::enable_if_t<cond, typename Traits::begin::type> {
        return Traits::begin::call(this->container, this->convert);
    }

    /* Delegate to the container's cbegin() method, if it exists. */
    template <bool cond = Traits::cbegin::exists>
    inline auto cbegin() -> std::enable_if_t<cond, typename Traits::cbegin::type> {
        return Traits::cbegin::call(this->container, this->convert);
    }

    /* Delegate to the container's end() method, if it exists. */
    template <bool cond = Traits::end::exists>
    inline auto end() -> std::enable_if_t<cond, typename Traits::end::type> {
        return Traits::end::call(this->container, this->convert);
    }

    /* Delegate to the container's cend() method, if it exists. */
    template <bool cond = Traits::cend::exists>
    inline auto cend() -> std::enable_if_t<cond, typename Traits::cend::type> {
        return Traits::cend::call(this->container, this->convert);
    }

    /* Delegate to the container's rbegin() method, if it exists. */
    template <bool cond = Traits::rbegin::exists>
    inline auto rbegin() -> std::enable_if_t<cond, typename Traits::rbegin::type> {
        return Traits::rbegin::call(this->container, this->convert);
    }

    /* Delegate to the container's crbegin() method, if it exists. */
    template <bool cond = Traits::crbegin::exists>
    inline auto crbegin() -> std::enable_if_t<cond, typename Traits::crbegin::type> {
        return Traits::crbegin::call(this->container, this->convert);
    }

    /* Delegate to the container's rend() method, if it exists. */
    template <bool cond = Traits::rend::exists>
    inline auto rend() -> std::enable_if_t<cond, typename Traits::rend::type> {
        return Traits::rend::call(this->container, this->convert);
    }

    /* Delegate to the container's crend() method, if it exists. */
    template <bool cond = Traits::crend::exists>
    inline auto crend() -> std::enable_if_t<cond, typename Traits::crend::type> {
        return Traits::crend::call(this->container, this->convert);
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
    template <bool cond = Traits::begin::exists && Traits::end::exists>
    inline auto iter() -> std::enable_if_t<
        cond, CoupledIterator<typename Traits::begin::type>
    > {
        return CoupledIterator<typename Traits::begin::type>(begin(), end());
    }

    /* Create a coupled iterator over the container using the cbegin()/cend() methods. */
    template <bool cond = Traits::cbegin::exists && Traits::cend::exists>
    inline auto citer() -> std::enable_if_t<
        cond, CoupledIterator<typename Traits::cbegin::type>
    > {
        return CoupledIterator<typename Traits::cbegin::type>(cbegin(), cend());
    }

    /* Create a coupled iterator over the container using the rbegin()/rend() methods. */
    template <bool cond = Traits::rbegin::exists && Traits::rend::exists>
    inline auto reverse() -> std::enable_if_t<
        cond, CoupledIterator<typename Traits::rbegin::type>
    > {
        return CoupledIterator<typename Traits::rbegin::type>(rbegin(), rend());
    }

    /* Create a coupled iterator over the container using the crbegin()/crend() methods. */
    template <bool cond = Traits::crbegin::exists && Traits::crend::exists>
    inline auto creverse() -> std::enable_if_t<
        cond, CoupledIterator<typename Traits::crbegin::type>
    > {
        return CoupledIterator<typename Traits::crbegin::type>(crbegin(), crend());
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
    template <bool cond = Traits::begin::exists && Traits::end::exists>
    inline auto python() -> std::enable_if_t<cond, PyObject*> {
        using Iter = PyIterator<typename Traits::begin::type>;
        return Iter::init(begin(), end());
    }

    /* Create a forward Python iterator over the container using the cbegin()/cend()
    methods. */
    template <bool cond = Traits::cbegin::exists && Traits::cend::exists>
    inline auto cpython() -> std::enable_if_t<cond, PyObject*> {
        using Iter = PyIterator<typename Traits::cbegin::type>;
        return Iter::init(cbegin(), cend());
    }

    /* Create a backward Python iterator over the container using the rbegin()/rend()
    methods. */
    template <bool cond = Traits::rbegin::exists && Traits::rend::exists>
    inline auto rpython() -> std::enable_if_t<cond, PyObject*> {
        using Iter = PyIterator<typename Traits::rbegin::type>;
        return Iter::init(rbegin(), rend());
    }

    /* Create a backward Python iterator over the container using the crbegin()/crend()
    methods. */
    template <bool cond = Traits::crbegin::exists && Traits::crend::exists>
    inline auto crpython() -> std::enable_if_t<cond, PyObject*> {
        using Iter = PyIterator<typename Traits::crbegin::type>;
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
    template <bool cond = !rvalue, std::enable_if_t<cond, int> = 0>
    IterProxy(Container& c) : container(c), convert(Func{}) {}
    template <bool cond = !rvalue, std::enable_if_t<cond, int> = 0>
    IterProxy(Container& c, Func f) : container(c), convert(f) {}

    /* Construct an iterator proxy around an rvalue container. */
    template <bool cond = rvalue, std::enable_if_t<cond, int> = 0>
    IterProxy(Container&& c) : container(std::move(c)), convert(Func{}) {}
    template <bool cond = rvalue, std::enable_if_t<cond, int> = 0>
    IterProxy(Container&& c, Func f) : container(std::move(c)), convert(f) {}
};


///////////////////////////////
////    PYTHON BINDINGS    ////
///////////////////////////////


/* Python bindings involve retrieving a forward or backward Python iterator directly
 * from the CPython API and exposing it to C++ using a standard iterator interface with
 * RAII semantics.  This abstracts away the CPython API (and the associated reference
 * counting/error handling) and allows for standard C++ loop constructs to be used
 * directly on Python containers.
 */


/* A wrapper around a Python iterator that manages reference counts and enables
for-each loop syntax in C++. */
template <typename Func, bool is_const>
class PyIterProxy {
    using Container = std::conditional_t<is_const, const PyObject, PyObject>;
    Container* const container;  // ptr cannot be reassigned
    Func convert;
    static constexpr bool is_identity = std::is_same_v<Func, identity>;

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

    public:
        // iterator tags for std::iterator_traits
        using iterator_category     = std::forward_iterator_tag;
        using difference_type       = std::ptrdiff_t;
        using value_type            = std::remove_reference_t<ReturnType>;
        using pointer               = value_type*;
        using reference             = value_type&;

        /* Get current item. */
        value_type operator*() const {
            if constexpr (is_identity) {
                return curr;  // no conversion necessary
            } else {
                return convert(curr);
            }
        }

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

    private:
        friend PyIterProxy;

        /* Return an iterator to the start of the sequence. */
        Iterator(PyObject* i, Func f) : convert(f), py_iterator(i), curr(nullptr) {
            // NOTE: py_iterator is a borrowed reference from PyObject_GetIter()
            if (py_iterator != nullptr) {
                curr = PyIter_Next(py_iterator);  // get first item
                if (curr == nullptr && PyErr_Occurred()) {
                    Py_DECREF(py_iterator);
                    throw catch_python<std::runtime_error>();
                }
            }
        }

        /* Return an iterator to the end of the sequence. */
        Iterator(Func f) : convert(f), py_iterator(nullptr), curr(nullptr) {}
    };

    /////////////////////////////
    ////    C++ INTERFACE    ////
    /////////////////////////////

    /* Get a forward iterator over a mutable container. */
    inline Iterator begin() const {
        return Iterator(this->python(), this->convert);
    }

    /* Get a forward iterator to terminate the loop. */
    inline Iterator end() const {
        return Iterator(this->convert);
    }

    /* Get a forward const iterator over an immutable container. */
    inline Iterator cbegin() const {
        return begin();
    }

    /* Get a forward const iterator to terminate the loop. */
    inline Iterator cend() const {
        return end();
    }

    /* Get a reverse iterator over a mutable container. */
    inline Iterator rbegin() const {
        return Iterator(this->rpython(), this->convert);
    }

    /* Get a reverse iterator to terminate the loop. */
    inline Iterator rend() const {
        return Iterator(this->convert);
    }

    /* Get a reverse const iterator over an immutable container. */
    inline Iterator crbegin() const {
        return rbegin();
    }

    /* Get a reverse const iterator to terminate the loop. */
    inline Iterator crend() const {
        return rend();
    }

    /////////////////////////////////
    ////    COUPLED ITERATORS    ////
    /////////////////////////////////

    /* Create a coupled iterator over the container using the begin()/end() methods. */
    inline auto iter() const {
        return CoupledIterator<Iterator>(begin(), end());
    }

    /* Create a coupled iterator over the container using the cbegin()/cend() methods. */
    inline auto citer() const {
        return CoupledIterator<Iterator>(cbegin(), cend());
    }

    /* Create a coupled iterator over the container using the rbegin()/rend() methods. */
    inline auto reverse() const {
        return CoupledIterator<Iterator>(rbegin(), rend());
    }

    /* Create a coupled iterator over the container using the crbegin()/crend() methods. */
    inline auto creverse() const {
        return CoupledIterator<Iterator>(crbegin(), crend());
    }

    ////////////////////////////////
    ////    PYTHON INTERFACE    ////
    ////////////////////////////////

    /* Get a forward Python iterator over a mutable container. */
    inline PyObject* python() const {
        PyObject* iter = PyObject_GetIter(this->container);
        if (iter == nullptr && PyErr_Occurred()) {
            throw catch_python<type_error>();
        }
        return iter;
    }

    /* Get a forward Python iterator over an immutable container. */
    inline PyObject* cpython() const {
        return this->python();
    }

    /* Get a reverse Python iterator over a mutable container. */
    inline PyObject* rpython() const {
        PyObject* attr = PyObject_GetAttrString(this->container, "__reversed__");
        if (attr == nullptr && PyErr_Occurred()) {
            throw catch_python<type_error>();
        }
        PyObject* iter = PyObject_CallObject(attr, nullptr);
        Py_DECREF(attr);
        if (iter == nullptr && PyErr_Occurred()) {
            throw catch_python<type_error>();
        }
        return iter;  // new reference
    }

    /* Get a reverse Python iterator over an immutable container. */
    inline PyObject* crpython() const {
        return this->rpython();
    }

private:
    /* PyIterProxies can only be constructed through `iter()` factory function. */
    friend PyIterProxy<identity, false> iter(PyObject* container);
    friend PyIterProxy<identity, true> iter(const PyObject* container);
    template <typename _Func>
    friend PyIterProxy<_Func, false> iter(PyObject* container, _Func convert);
    template <typename _Func>
    friend PyIterProxy<_Func, true> iter(const PyObject* container, _Func convert);

    /* Construct an iterator proxy around a python container. */
    PyIterProxy(Container* c) : container(c), convert(Func{}) {}
    PyIterProxy(Container* c, Func f) : container(c), convert(f) {}

};


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
to insert a scalar conversion in between the iterator dereference and the return of the
`next()` method on the Python side.  For example, if the C++ iterator dereferences to a
custom struct, the user can provide an inline lambda that translates the struct into a
valid PyObject*, which is returned to Python like normal.

When called with a Python container, the `iter()` method produces an equivalent proxy
that wraps the `PyObject_GetIter()` C API function and exposes a standard C++ interface
on the other side.  Just like the C++ to Python conversion, custom conversion functions
can be added in between the result of the `next()` method on the Python side and the
iterator dereference on the C++ side.

Note that due to the dynamic nature of Python's type system, conversions of this sort
will require foreknowledge of the container's specific element type in order to perform
the casts necessary to narrow the types to their C++ equivalents.  To facilitate this,
all the data structures exposed in the `bertrand::structs` namespace support optional
Python-side type specialization, which can be used to enforce homogeneity at the
container level.
*/


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


/* Create a Python to C++ iterator proxy for a mutable Python container. */
inline PyIterProxy<identity, false> iter(PyObject* container) {
    return PyIterProxy<identity, false>(container);
}


/* Create a Python to C++ iterator proxy for a const Python container. */
inline PyIterProxy<identity, true> iter(const PyObject* container) {
    return PyIterProxy<identity, true>(container);
}


/* Create a Python to C++ iterator proxy for a mutable Python container. */
template <typename Func>
inline PyIterProxy<Func, false> iter(PyObject* container, Func convert) {
    return PyIterProxy<Func, false>(container, convert);
}


/* Create a Python to C++ iterator proxy for a const Python container. */
template <typename Func>
inline PyIterProxy<Func, true> iter(const PyObject* container, Func convert) {
    return PyIterProxy<Func, true>(container, convert);
}


}  // namespace util
}  // namespace structs
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_UTIL_ITER_H
