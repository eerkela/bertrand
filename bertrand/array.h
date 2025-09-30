#ifndef BERTRAND_ARRAY_H
#define BERTRAND_ARRAY_H

#include "bertrand/common.h"
#include "bertrand/except.h"


namespace bertrand {


namespace impl {
    struct array_tag {};

    template <typename T, size_t... N>
    concept array_concept =
        meta::not_void<T> &&
        meta::not_rvalue<T> &&
        (sizeof...(N) > 0) &&
        (sizeof...(N) == 1 || ((N > 0) && ...));

}


template <typename T, size_t... N> requires (impl::array_concept<T, N...>)
struct Array;


template <typename... Ts> requires (meta::has_common_type<Ts...>)
Array(Ts...) -> Array<meta::remove_rvalue<meta::common_type<Ts...>>, sizeof...(Ts)>;


namespace meta {

    template <typename C, typename T = void, size_t... N>
    concept Array =
        inherits<C, impl::array_tag> &&
        (is_void<T> || ::std::same_as<typename unqualify<C>::type, T>) &&
        (sizeof...(N) == 0 || unqualify<C>::template shape<N...>());

}


namespace impl {

    template <typename T, size_t...>
    struct _array_init { using type = T; };
    template <typename T, size_t N, size_t... Ns>
    struct _array_init<T, N, Ns...> : _array_init<Array<T, N>, Ns...> {};
    template <typename T, size_t N, size_t... Ns>
    struct array_init : _array_init<T, Ns...> {};

    template <size_t N, size_t... Ns>
    constexpr size_t array_init_size = (Ns * ... * 1);

    template <size_t M, size_t... Ms, meta::unsigned_integer I, typename... Is>
    constexpr size_t array_index(const I& i, const Is&... is)
        noexcept (!DEBUG && meta::nothrow::explicitly_convertible_to<I, size_t>)
        requires (meta::explicitly_convertible_to<I, size_t>)
    {
        if constexpr (DEBUG) {
            if (i >= M) {
                /// TODO: figure out a better error message, and possibly centralize
                /// it in `impl::` to prevent duplication.
                throw IndexError(::std::to_string(i));
            }
        }
        if constexpr (sizeof...(Ms) == 0) {
            return size_t(i);
        } else {
            return size_t(i) * (Ms * ... * 1) + array_index<Ms...>(is...);
        }
    }

    template <size_t M, size_t... Ms, meta::signed_integer I, typename... Is>
    constexpr size_t array_index(const I& i, const Is&... is)
        noexcept (!DEBUG && meta::nothrow::explicitly_convertible_to<I, ssize_t>)
        requires (meta::explicitly_convertible_to<I, ssize_t>)
    {
        ssize_t j = ssize_t(i) + ssize_t(M) * (i < 0);
        if constexpr (DEBUG) {
            if (j < 0 || j > ssize_t(M)) {
                /// TODO: figure out a better error message, and possibly centralize
                /// it in `impl::` to prevent duplication.
                throw IndexError(::std::to_string(i));
            }
        }
        if constexpr (sizeof...(Ms) == 0) {
            return size_t(j);
        } else {
            return size_t(j) * (Ms * ... * 1) + array_index<Ms...>(is...);
        }
    }

    template <typename T, size_t... N>
    struct array_transpose {
        template <size_t... M>
        using type = Array<T, M...>;
    };
    template <typename T, size_t N, size_t... Ns>
    struct array_transpose<T, N, Ns...> {
        template <size_t... M>
        using type = array_transpose<T, Ns...>::template type<N, M...>;
    };

}



/// TODO: it might be possible later in `Union` to provide a specialization of
/// `Array<Optional<T>>` where the boolean flags are stored in a packed format
/// alongside the data, rather than requiring padding between each element.  Indexing
/// will return an optional reference where `None` represents a missing value.



template <typename T, size_t... N> requires (impl::array_concept<T, N...>)
struct Array {
    using type = T;
    using size_type = size_t;
    using index_type = ssize_t;
    using value_type = T;
    using reference = T&;
    using pointer = T*;

    /* The number of dimensions for a multidimensional array.  This is always equal
    to the number of integer template parameters, and is never zero. */
    [[nodiscard]] static constexpr size_type ndim() noexcept { return sizeof...(N); }

    /* The size of the array along each dimension, reported as another
    `Array<size_t, ndim()>`. */
    [[nodiscard]] static constexpr Array<size_type, ndim()> shape() noexcept { return {N...}; }

    /* Check whether this array's shape exactly matches an externally-supplied index
    sequence. */
    template <size_t... M> requires (sizeof...(M) == ndim())
    [[nodiscard]] static constexpr bool shape() noexcept { return ((N == M) && ...); }

    /* The total number of elements in the array across all dimensions.  This is
    always equivalent to the cartesian product of `shape()`. */
    [[nodiscard]] static constexpr size_type total() noexcept { return (N * ... * 1); }

    /* The top-level size of the array (i.e. the size of the first dimension).
    This is always equal to the first index of `shape()`, and indicates the number
    of subranges that will be yielded when the array is iterated over. */
    [[nodiscard]] static constexpr size_type size() noexcept { return meta::unpack_value<0, N...>; }

    /* Equivalent to `size()`, but as a signed rather than unsigned integer. */
    [[nodiscard]] static constexpr index_type ssize() noexcept { return index_type(size()); }

    /* True if the array has precisely zero elements.  False otherwise. */
    [[nodiscard]] static constexpr bool empty() noexcept { return size() == 0; }

private:
    using init = impl::array_init<T, N...>::type;
    static constexpr size_t init_size = impl::array_init_size<N...>;

    union store { impl::ref<T> value; };

    constexpr Array(store) noexcept {}

    template <typename V> requires (sizeof...(N) == 1)
    constexpr void build(size_type& i, V&& v)
        noexcept (meta::nothrow::convertible_to<V, T>)
        requires (meta::convertible_to<V, T>)
    {
        std::construct_at(&__data[i].value, std::forward<V>(v));
        ++i;
    }

    constexpr void build(size_type& i, const init& v)
        noexcept (meta::nothrow::copyable<impl::ref<T>>)
        requires (sizeof...(N) > 1 && meta::copyable<impl::ref<T>>)
    {
        for (size_type j = 0; j < init_size; ++j, ++i) {
            std::construct_at(&__data[i].value, v.__data[j].value);
        }
    }

    constexpr void build(size_type& i, init&& v)
        noexcept (meta::nothrow::movable<impl::ref<T>>)
        requires (sizeof...(N) > 1 && meta::movable<impl::ref<T>>)
    {
        for (size_type j = 0; j < init_size; ++j, ++i) {
            std::construct_at(&__data[i].value, std::move(v.__data[j].value));
        }
    }

public:
    store __data[total()];

    /* Reserve an uninitialized array, bypassing default constructors.  This operation
    is considered to be unsafe in most circumstances, except when the uninitialized
    elements are constructed in-place immediately afterwards, via either
    `std::construct_at()`, placement new, or a trivial assignment. */
    [[nodiscard]] static constexpr Array reserve() noexcept {
        return {store{}};
    }

    /* Default-construct all elements of the array. */
    [[nodiscard]] constexpr Array()
        noexcept (meta::nothrow::default_constructible<T>)
        requires (meta::default_constructible<T>)
    {
        for (size_type i = 0; i < total(); ++i) {
            std::construct_at(&__data[i].value);
        }
    }

    /* Construct a one-dimensional array from flat inputs.  The number of arguments
    must always match the `size()` of the top-level array.  In the one-dimensional
    case, the elements will directly initialize the array without any further
    overhead. */
    template <meta::convertible_to<init>... Ts> requires (ndim() == 1)
    [[nodiscard]] constexpr Array(Ts&&... v)
        noexcept ((meta::nothrow::convertible_to<Ts, init> && ...))
        requires (sizeof...(Ts) > 0 && sizeof...(Ts) == size())
    :
        __data{store{std::forward<Ts>(v)}...}
    {}

    /* Construct a multidimensional array from structured inputs.  The number of
    arguments must always match the `size()` of the top-level array.  Each argument
    must be convertible to a nested array type, which has one fewer dimension than
    the outer array, and whose element type may be a further nested array until the
    dimensions are exhausted.  Each nested array will be flattened into the outer
    array in row-major order. */
    template <meta::convertible_to<init>... Ts> requires (ndim() > 1)
    [[nodiscard]] constexpr Array(Ts&&... v)
        noexcept (requires(size_type i) {{(build(i, std::forward<Ts>(v)), ...)} noexcept;})
        requires (
            sizeof...(Ts) > 0 &&
            sizeof...(Ts) == size() &&
            requires(size_type i) {{(build(i, std::forward<Ts>(v)), ...)};}
        )
    {
        size_type i = 0;
        (build(i, std::forward<Ts>(v)), ...);
    }

    /// TODO: a `std::from_range` constructor that takes an iterable that yields
    /// a type that is convertible to `T` and fills the array in row-major order.
    /// A debug error can be issued if the size of the iterable does not match
    /// `total()`, or I can default-initialize any remaining elements.  Forcing an
    /// exact size match might be better though.

    /// TODO: another constructor that takes a begin()/end() iterator pair and
    /// basically applies the same logic as the from_range constructor.

    [[nodiscard]] constexpr Array(const Array& other)
        noexcept (meta::nothrow::copyable<T>)
        requires (meta::copyable<T>)
    {
        for (size_type i = 0; i < total(); ++i) {
            std::construct_at(&__data[i].value, other.__data[i].value);
        }
    }

    [[nodiscard]] constexpr Array(Array&& other)
        noexcept (meta::nothrow::movable<T>)
        requires (meta::movable<T>)
    {
        for (size_type i = 0; i < total(); ++i) {
            std::construct_at(&__data[i].value, std::move(other.__data[i].value));
        }
    }

    constexpr Array& operator=(const Array& other)
        noexcept (meta::nothrow::copy_assignable<T>)
        requires (meta::copy_assignable<T>)
    {
        if (this != &other) {
            for (size_type i = 0; i < total(); ++i) {
                __data[i].value = other.__data[i].value;
            }
        }
        return *this;
    }

    constexpr Array& operator=(Array&& other)
        noexcept (meta::nothrow::move_assignable<T>)
        requires (meta::move_assignable<T>)
    {
        if (this != &other) {
            for (size_type i = 0; i < total(); ++i) {
                __data[i].value = std::move(other.__data[i].value);
            }
        }
        return *this;
    }

    constexpr ~Array()
        noexcept (meta::nothrow::destructible<T>)
        requires (meta::destructible<T>)
    {
        if constexpr (!meta::trivially_destructible<T>) {
            for (size_type i = 0; i < total(); ++i) {
                std::destroy_at(&__data[i].value);
            }
        }
    }


    /* Swap the contents of this array with another array. */
    constexpr void swap(Array& other)
        noexcept (meta::lvalue<T> || meta::nothrow::swappable<T>)
        requires (meta::lvalue<T> || meta::swappable<T>)
    {
        for (size_type i = 0; i < total(); ++i) {
            std::ranges::swap(__data[i].value, other.__data[i].value);
        }
    }

    /// TODO: data() is going to have to reinterpret the raw __data buffer.

    /* Get a pointer to the underlying data buffer.  Note that this is always a flat,
    contiguous, row-major region of length `total()`, where lvalue types are converted
    into nested pointers. */
    [[nodiscard]] constexpr auto data() noexcept { return __data; }

    /* Get a pointer to the underlying data buffer.  Note that this is always a flat,
    contiguous, row-major region of length `total()`, where lvalue types are converted
    into nested pointers. */
    [[nodiscard]] constexpr auto data() const noexcept { return __data; }


    /// TODO: a tuple indexing operator just like below


    /// TODO: allow indexing to not fill in all of the required dimensions, in
    /// which case it would return a subrange over the remaining dimensions.
    /// Getting the compiler to be cool with that is kind of a pain though.

    /* Access an element of the array by its multidimensional index.  The number
    of indices must match the number of dimensions. */
    template <typename Self, meta::integer... I> requires (sizeof...(I) == ndim())
    [[nodiscard]] constexpr decltype(auto) operator[](this Self&& self, const I&... i)
        noexcept (!DEBUG && ((meta::unsigned_integer<I> ?
            meta::explicitly_convertible_to<I, size_t> :
            meta::explicitly_convertible_to<I, ssize_t>
        ) && ...))
        requires ((meta::unsigned_integer<I> ?
            meta::explicitly_convertible_to<I, size_t> :
            meta::explicitly_convertible_to<I, ssize_t>
        ) && ...)
    {
        return (*std::forward<Self>(self).__data[impl::array_index<N...>(i...)].value);
    }

    using transpose_type = impl::array_transpose<T, N...>::template type<>;


    [[nodiscard]] constexpr transpose_type transpose() const
    {
        transpose_type result;
        /// TODO: figure out the indexing needed to transpose the array.
        return result;
    }

    [[nodiscard]] constexpr transpose_type transpose() &&
    {
        transpose_type result;
        /// TODO: figure out the indexing needed to transpose the array.
        return result;
    }

    template <size_t... M> requires ((M * ... * 1) == total())
    using reshape_type = Array<T, M...>;

    template <size_t... M> requires ((M * ... * 1) == total())
    [[nodiscard]] constexpr Array<T, M...> reshape() const
        noexcept (meta::nothrow::copyable<T>)
        requires (meta::copyable<T>)
    {
        auto result = Array<T, M...>::reserve();
        for (size_type i = 0; i < total(); ++i) {
            std::construct_at(&result.__data[i].value, __data[i].value);
        }
        return result;
    }

    template <size_t... M> requires ((M * ... * 1) == total())
    [[nodiscard]] constexpr Array<T, M...> reshape() &&
        noexcept (meta::nothrow::movable<T>)
        requires (meta::movable<T>)
    {
        auto result = Array<T, M...>::reserve();
        for (size_type i = 0; i < total(); ++i) {
            std::construct_at(&result.__data[i].value, std::move(__data[i].value));
        }
        return result;
    }


    /// TODO: reflect across the diagonal without changing shape.  This can possibly
    /// be an in-place operation, doing elementwise swaps.



    /// TODO: iteration, where iterating over every dimension except the last
    /// yields contiguous subranges.

    template <typename U>
    [[nodiscard]] constexpr bool operator==(const Array<U, N...>& other) const
        noexcept (
            meta::nothrow::has_eq<meta::as_const_ref<T>, meta::as_const_ref<U>> &&
            meta::nothrow::truthy<meta::eq_type<meta::as_const_ref<T>, meta::as_const_ref<U>>>
        )
        requires (
            meta::has_eq<meta::as_const_ref<T>, meta::as_const_ref<U>> &&
            meta::truthy<meta::eq_type<meta::as_const_ref<T>, meta::as_const_ref<U>>>
        )
    {
        for (size_type i = 0; i < total(); ++i) {
            if (!bool(*__data[i].value == *other.__data[i].value)) {
                return false;
            }
        }
        return true;
    }

    template <typename U>
    [[nodiscard]] constexpr auto operator<=>(const Array<U, N...>& other) const
        noexcept (
            meta::nothrow::has_lt<meta::as_const_ref<T>, meta::as_const_ref<U>> &&
            meta::nothrow::has_lt<meta::as_const_ref<U>, meta::as_const_ref<T>> &&
            meta::nothrow::truthy<meta::lt_type<meta::as_const_ref<T>, meta::as_const_ref<U>>> &&
            meta::nothrow::truthy<meta::lt_type<meta::as_const_ref<U>, meta::as_const_ref<T>>>
        )
        requires (
            meta::has_lt<meta::as_const_ref<T>, meta::as_const_ref<U>> &&
            meta::has_lt<meta::as_const_ref<U>, meta::as_const_ref<T>> &&
            meta::truthy<meta::lt_type<meta::as_const_ref<T>, meta::as_const_ref<U>>> &&
            meta::truthy<meta::lt_type<meta::as_const_ref<U>, meta::as_const_ref<T>>>
        )
    {
        for (size_t i = 0; i < total(); ++i) {
            if (*__data[i].value < *other.__data[i].value) {
                return std::strong_ordering::less;
            } else if (*other.__data[i].value < *__data[i].value) {
                return std::strong_ordering::greater;
            }
        }
        return std::strong_ordering::equal;
    }
};


// static constexpr std::array<std::array<int, 2>, 2> test {
//     {1, 2},
//     {3, 4}
// };


static constexpr Array test {1, 2};

static constexpr Array test2 = Array<int, 2, 2>::reserve();

static constexpr Array test3 = Array<int, 2, 3>{
    Array{0, 1, 2},
    Array{3, 4, 5}
}.reshape<3, 2>();
static_assert(test3[0, 0] == 0);
static_assert(test3[0, 1] == 1);
static_assert(test3[1, 0] == 2);
static_assert(test3[1, 1] == 3);
static_assert(test3[2, 0] == 4);
static_assert(test3[2, 1] == 5);


static_assert([] {
    Array<int, 2, 2> arr {
        Array{1, 2},
        Array{3, 4}
    };
    if (arr[0, 0] != 1) return false;
    if (arr[0, 1] != 2) return false;
    if (arr[1, 0] != 3) return false;
    if (arr[1, -1] != 4) return false;

    auto arr2 = Array<int, 2, 2>{Array{1, 2}, Array{3, 3}};
    arr = arr2;
    if (arr[1, 1] != 3) return false;

    return true;
}());


}


#endif
