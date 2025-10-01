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


namespace meta {

    template <typename C, typename T = void, size_t... N>
    concept Array =
        inherits<C, impl::array_tag> &&
        (is_void<T> || ::std::same_as<typename unqualify<C>::type, T>) &&
        (sizeof...(N) == 0 || unqualify<C>::template shape<N...>());

}


namespace impl {

    /* Multidimensional arrays can take structured data in their constructors, which
    consists of a series of nested arrays of 1 fewer dimension representing the major
    axis of the parent array.  The nested arrays will then be flattened into the outer
    array buffer, completing the constructor. */
    template <typename T, size_t...>
    struct _array_init { using type = T; };
    template <typename T, size_t N, size_t... Ns>
    struct _array_init<T, N, Ns...> : _array_init<Array<T, N>, Ns...> {};
    template <typename T, size_t N, size_t... Ns>
    struct array_init : _array_init<T, Ns...> {};

    /* The total length of each nested array provided in `array_init`, or the length
    of each array view yielded by a nested iterator.  `N` is ignored, but must be
    supplied regardless. */
    template <size_t N, size_t... Ns>
    constexpr size_t array_step = (Ns * ... * 1);

    /* Indices are always computed in C (row-major) memory order for simplicity and
    compatibility with C++.  If an index is signed, then Python-style wraparound will
    be applied to handle negative values.  A bounds check is then applied as a debug
    assertion, which throws an `IndexError` if the index is out of bounds. For unsigned
    integers in release builds, the indexing is zero-cost. */
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
        if constexpr (sizeof...(Is) == 0) {
            return size_t(i) * (Ms * ... * 1);
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
        if constexpr (sizeof...(Is) == 0) {
            return size_t(j) * (Ms * ... * 1);
        } else {
            return size_t(j) * (Ms * ... * 1) + array_index<Ms...>(is...);
        }
    }

    /* Array elements are stored as unions in order to sidestep default constructors,
    and allow the array to be allocated in an uninitialized state.  This is crucial to
    allow efficient construction from iterables, etc. */
    template <typename T>
    union array_storage {
        using type = T;
        using size_type = size_t;
        using index_type = ssize_t;
        using value_type = meta::remove_reference<T>;
        using reference = meta::as_lvalue<T>;
        using pointer = meta::as_pointer<T>;

        impl::ref<T> value;

        constexpr array_storage() noexcept {}
        template <meta::convertible_to<T> U>
        constexpr array_storage(U&& u) noexcept (meta::nothrow::convertible_to<U, T>) :
            value(std::forward<U>(u))
        {}
        constexpr ~array_storage() noexcept {
            if constexpr (!meta::trivially_destructible<T>) {
                std::destroy_at(&value);
            }
        }

        [[nodiscard]] constexpr auto data() noexcept {
            if constexpr (meta::lvalue<T>) {
                return reinterpret_cast<pointer*>(&value);
            } else {
                return std::addressof(*value);
            }
        }

        [[nodiscard]] constexpr auto data() const noexcept {
            if constexpr (meta::lvalue<T>) {
                return reinterpret_cast<const pointer*>(&value);
            } else {
                return std::addressof(*value);
            }
        }
    };

    template <meta::not_reference store, size_t N, size_t... Ns>
    struct array_view;

    template <typename T>
    constexpr bool _is_array_view = false;
    template <typename T, size_t... N>
    constexpr bool _is_array_view<array_view<T, N...>> = true;
    template <typename T>
    concept is_array_view = _is_array_view<meta::unqualify<T>>;

    template <meta::not_reference store, size_t... Ns>
    struct array_view_value { using type = store::value_type; };
    template <meta::not_reference store, size_t N, size_t... Ns>
    struct array_view_value<store, N, Ns...> { using type = array_view<store, N, Ns...>; };

    template <meta::not_reference store, size_t I, size_t N, size_t... Ns>
    struct array_index_type : array_index_type<store, I - 1, Ns...> {};
    template <meta::not_reference store, size_t N, size_t... Ns>
    struct array_index_type<store, 0, N, Ns...> { using type = array_view<store, N, Ns...>; };

    template <typename... T>
    concept valid_array_indices =
        (meta::integer<T> && ...) &&
        ((meta::unsigned_integer<T> ?
            meta::explicitly_convertible_to<T, size_t> :
            meta::explicitly_convertible_to<T, ssize_t>
        ) && ...);

    template <typename... T>
    concept nothrow_array_indices =
        !DEBUG &&
        (meta::integer<T> && ...) &&
        ((meta::unsigned_integer<T> ?
            meta::nothrow::explicitly_convertible_to<T, size_t> :
            meta::nothrow::explicitly_convertible_to<T, ssize_t>
        ) && ...);

    /* Array iterators are implemented as raw pointers into the array buffer.  Due to
    aggressive UB sanitization during constant evaluation, an extra count is required
    to avoid overstepping the end of the array.  The index is ignored at run time,
    giving zero-cost iteration. */
    template <meta::not_reference store, size_t N, size_t... Ns>
    struct array_iterator {
        using iterator_category = std::random_access_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = array_view_value<store, Ns...>::type;
        using reference = meta::as_lvalue<value_type>;
        using pointer = meta::as_pointer<value_type>;

    private:
        using view = value_type;

    public:
        store* ptr = nullptr;
        difference_type count = 0;
        static constexpr difference_type step = (Ns * ... * 1);

        [[nodiscard]] constexpr decltype(auto) operator*() const noexcept {
            if constexpr (sizeof...(Ns) == 0) {
                return (*ptr->value);
            } else {
                return view{ptr};
            }
        }

        [[nodiscard]] constexpr auto operator->() const noexcept {
            if constexpr (sizeof...(Ns) == 0) {
                return std::addressof(**this);
            } else {
                return impl::arrow{**this};
            }
        }

        [[nodiscard]] constexpr decltype(auto) operator[](difference_type n) const noexcept {
            if constexpr (sizeof...(Ns) == 0) {
                return (*ptr[n].value);
            } else {
                return view{ptr + n * step};
            }
        }

        constexpr array_iterator& operator++() noexcept {
            if consteval {
                ++count;
                if (count > 0 && count < N) {
                    ptr += step;
                }
            } else {
                ptr += step;
            }
            return *this;
        }

        [[nodiscard]] constexpr array_iterator operator++(int) noexcept {
            array_iterator temp = *this;
            ++*this;
            return temp;
        }

        [[nodiscard]] friend constexpr array_iterator operator+(
            const array_iterator& self,
            difference_type n
        ) noexcept {
            if consteval {
                difference_type new_count = self.count + n;
                if (new_count < 0) {
                    return {self.ptr - self.count * (self.count > 0) * step, new_count};
                } else if (new_count >= N) {
                    return {self.ptr + (N - self.count - 1) * (self.count < N) * step, new_count};
                } else {
                    return {self.ptr + n * step, new_count};
                }
            } else {
                return {self.ptr + n * step};
            }
        }

        [[nodiscard]] friend constexpr array_iterator operator+(
            difference_type n,
            const array_iterator& self
        ) noexcept {
            if consteval {
                difference_type new_count = self.count + n;
                if (new_count < 0) {
                    return {self.ptr - self.count * (self.count > 0) * step, new_count};
                } else if (new_count >= N) {
                    return {self.ptr + (N - self.count - 1) * (self.count < N) * step, new_count};
                } else {
                    return {self.ptr + n * step, new_count};
                }
            } else {
                return {self.ptr + n * step};
            }
        }

        constexpr array_iterator& operator+=(difference_type n) noexcept {
            if consteval {
                difference_type new_count = count + n;
                if (new_count < 0) {
                    ptr -= count * (count > 0) * step;
                } else if (new_count >= N) {
                    ptr += (N - count - 1) * (count < N) * step;
                } else {
                    ptr += n * step;
                }
                count = new_count;
            } else {
                ptr += n * step;
            }
            return *this;
        }

        constexpr array_iterator& operator--() noexcept {
            if consteval {
                if (count > 0 && count < N) {
                    ptr -= step;
                }
                --count;
            } else {
                ptr -= step;
            }
            return *this;
        }

        [[nodiscard]] constexpr array_iterator operator--(int) noexcept {
            array_iterator temp = *this;
            --*this;
            return temp;
        }

        [[nodiscard]] constexpr array_iterator operator-(difference_type n) const noexcept {
            if consteval {
                difference_type new_count = count - n;
                if (new_count < 0) {
                    return {ptr - count * (count > 0) * step, new_count};
                } else if (new_count >= N) {
                    return {ptr + (N - count - 1) * (count < N) * step, new_count};
                } else {
                    return {ptr - n * step, new_count};
                }
            } else {
                return {ptr - n * step};
            }
        }

        [[nodiscard]] constexpr difference_type operator-(
            const array_iterator& other
        ) const noexcept {
            if consteval {
                return count - other.count;
            } else {
                return (ptr - other.ptr) / step;
            }
        }

        constexpr array_iterator& operator-=(difference_type n) noexcept {
            if consteval {
                difference_type new_count = count - n;
                if (new_count < 0) {
                    ptr -= count * (count > 0) * step;
                } else if (new_count >= N) {
                    ptr += (N - count - 1) * (count < N) * step;
                } else {
                    ptr -= n * step;
                }
                count = new_count;
            } else {
                ptr -= n * step;
            }
            return *this;
        }

        [[nodiscard]] constexpr bool operator==(const array_iterator& other) const noexcept {
            if consteval {
                return count == other.count;
            } else {
                return ptr == other.ptr;
            }
        }

        [[nodiscard]] constexpr auto operator<=>(const array_iterator& other) const noexcept {
            if consteval {
                return count <=> other.count;
            } else {
                return ptr <=> other.ptr;
            }
        }
    };

    /* Iteration over as well as incomplete indices into a multidimensional array will
    yield views over virtual sub-arrays of reduced dimension.  Views are implemented as
    simple pointers into the outer array's data buffer, coupled with a reduced shape
    fitting the sub-array's rank (which is computed and held at compile time).

    Array views can be used just like normal arrays in most respects, but they do not
    own their data, and do not extend lifetimes in any way.  It is therefore user's
    responsibility to ensure that a view never outlives its parent array, lest it
    dangle.  In order to facilitate this, all views are implicitly convertible into
    full arrays by copying the underlying data into a new buffer.  A `CTAD` guide is
    also provided to allow inference of the proper shape and type.

    Additionally, because views do not own their data, any changes made to an element
    of the view will be reflected in the parent array, and vice versa.  Once again,
    this can be avoided by copying the view into a proper array, or extracting a view
    over the const array instead. */
    template <meta::not_reference store, size_t N, size_t... Ns>
    struct array_view {
        using size_type = size_t;
        using index_type = ssize_t;
        using value_type = array_view_value<store, Ns...>::type;
        using reference = meta::as_lvalue<value_type>;
        using pointer = meta::as_pointer<value_type>;
        using iterator = array_iterator<store, N, Ns...>;
        using reverse_iterator = std::reverse_iterator<iterator>;

    private:
        using view = value_type;

    public:
        store* ptr = nullptr;

        [[nodiscard]] static constexpr size_type size() noexcept {
            return N;
        }

        [[nodiscard]] static constexpr index_type ssize() noexcept {
            return N;
        }

        [[nodiscard]] constexpr bool empty() const noexcept {
            return N == 0;
        }

        [[nodiscard]] static constexpr size_type ndim() noexcept {
            return sizeof...(Ns) + 1;
        }

        [[nodiscard]] static constexpr Array<size_type, ndim()> shape() noexcept {
            return {N, Ns...};
        }
    
        template <size_type M, size_type... Ms> requires ((sizeof...(Ms) + 1) == ndim())
        [[nodiscard]] static constexpr bool shape() noexcept {
            return ((N == M) && ... && (Ns == Ms));
        }

        [[nodiscard]] static constexpr size_type total() noexcept {
            return (N * ... * Ns);
        }

        [[nodiscard]] static constexpr index_type step() noexcept {
            return (Ns * ... * 1);
        }

        [[nodiscard]] constexpr auto data() const noexcept {
            return ptr->data();
        }

        [[nodiscard]] constexpr decltype(auto) front() const noexcept {
            if constexpr (sizeof...(Ns) == 0) {
                return (*ptr->value);
            } else {
                return view{ptr};
            }
        }

        [[nodiscard]] constexpr decltype(auto) back() const noexcept {
            if constexpr (sizeof...(Ns) == 0) {
                return (*ptr[(N - 1) * step()].value);
            } else {
                return view{ptr + (N - 1) * step()};
            }
        }

    private:
        template <size_t I> requires (I < ndim())
        using subarray = impl::array_index_type<store, I, N, Ns...>::type;

    public:
        /// TODO: indexing, tuple access should probably apply Python-style wraparound
        /// and allow multidimensional indexing just like the main `Array` type.

        template <index_type... I> requires (sizeof...(I) == ndim())
        [[nodiscard]] constexpr decltype(auto) get() const noexcept {
            // return ptr[I];
        }

        template <meta::integer... I> requires (sizeof...(I) <= ndim())
        [[nodiscard]] constexpr decltype(auto) operator[](const I&... i) const
            noexcept (!DEBUG && ((meta::unsigned_integer<I> ?
                meta::explicitly_convertible_to<I, size_t> :
                meta::explicitly_convertible_to<I, ssize_t>
            ) && ...))
            requires ((meta::unsigned_integer<I> ?
                meta::explicitly_convertible_to<I, size_t> :
                meta::explicitly_convertible_to<I, ssize_t>
            ) && ...)
        {
            if constexpr (sizeof...(I) == ndim()) {
                return (*ptr[array_index<N, Ns...>(i...)].value);
            } else {
                return subarray<sizeof...(I)>{ptr + array_index<N, Ns...>(i...)};
            }
        }

        [[nodiscard]] constexpr iterator begin() const noexcept {
            return {ptr};
        }

        [[nodiscard]] constexpr iterator end() const noexcept {
            if consteval {
                return {ptr + (N - 1) * step(), N};
            } else {
                return {ptr + N * step()};
            }
        }

        [[nodiscard]] constexpr reverse_iterator rbegin() const noexcept {
            return std::make_reverse_iterator(end());
        }

        [[nodiscard]] constexpr reverse_iterator rend() const noexcept {
            return std::make_reverse_iterator(begin());
        }
    };

    /// TODO: transposing the array requires some mathematical thinking.

    /* Transposing an array effectively flips it from row-major to column-major order,
    reversing the shape that was used to specialize the `Array` type. */
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
/// -> Union can just be included here and those specializations would be implemented
/// inline.



/* A generalized, multidimensional array type with a fixed shape known at compile time.

Arrays can be of any shape and dimension as long as none are zero or negative.  The
only exception is the one-dimensional case, where a size of zero is allowed to
represent a trivial, empty array.  Similarly, arrays can be of arbitrary type and/or
qualification (including lvalue references), except for rvalue references and `void`.

The array elements are always stored in a contiguous block of memory in C (row-major)
order, with an overall size equal to the product of the shape dimensions.  Iterating
over the array will yield a series of views over virtual sub-arrays of reduced
dimension (stripping dimensions from left to right), until a 1-D view is reached, which
yields the underlying elements.  Each view reduces to a simple pointer into the
flattened data buffer, whose shape is stored at compile time.  Values are therefore
accessed indirectly, and will not extend the lifetime of the parent array, possibly
leading to dangling references if the parent is destroyed first.  In the 1-D case, no
views will be generated, and the elements will be yielded directly.

Arrays also support multidimensional indexing, both at compile time via a tuple-like
`get<I, J, K, ...>()` method, and at run time via `[i, j, k, ...]`.  The indices are
always interpreted in C (row-major) order, meaning that the last index varies the
fastest.  Signed indices will be interpreted with Python-style wraparound, allowing
negative indices to count backwards from the end of the respective dimension.  In
debug builds, all indices will be bounds-checked, throwing an `IndexError` if any are
out of range after normalization.  For unsigned indices in release builds, indexing is
always zero-cost.

If fewer indices are provided than the number of dimensions, then a view over the
corresponding sub-array will be returned instead, subject to the same rules as
iteration.  Additionally, arrays can be trivially flattened or reshaped by returning a
view of a different shape (of equal size), which is implemented via the `flatten()` and
`reshape<M...>()` methods, respectively, both of which are zero-cost.

Lastly, array views can always be converted into full arrays by copying the underlying
data into a new buffer.  In order to facilitate this, a CTAD guide is provided allowing
the `Array` class to infer the proper shape and type at compile time, like so:

    ```
    Array<int, 3, 2> arr {
        Array{1, 2},
        Array{3, 4},
        Array{5, 6}
    };

    Array sub = arr[1];
    assert(sub == Array{3, 4});

    sub[0] = 7;
    assert(sub == Array{7, 4});
    assert(sub != arr[1]);
    ```
*/
template <typename T, size_t... N> requires (impl::array_concept<T, N...>)
struct Array {
private:
    using init = impl::array_init<T, N...>::type;
    using store = impl::array_storage<T>;

public:
    using type = T;
    using size_type = size_t;
    using index_type = ssize_t;
    using value_type = meta::remove_reference<T>;
    using reference = meta::as_lvalue<T>;
    using pointer = meta::as_pointer<T>;
    using iterator = impl::array_iterator<store, N...>;
    using const_iterator = impl::array_iterator<const store, N...>;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    /* The number of dimensions for a multidimensional array.  This is always equal
    to the number of integer template parameters, and is never zero. */
    [[nodiscard]] static constexpr size_type ndim() noexcept { return sizeof...(N); }

    /* The size of the array along each dimension, reported as another
    `Array<size_t, ndim()>`. */
    [[nodiscard]] static constexpr Array<size_type, ndim()> shape() noexcept { return {N...}; }

    /* Check whether this array's shape exactly matches an externally-supplied index
    sequence. */
    template <size_type... M> requires (sizeof...(M) == ndim())
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
    template <size_t... M>
    using view = impl::array_view<store, M...>;
    template <size_t... M>
    using const_view = impl::array_view<const store, M...>;

    using flat_view = view<total()>;
    using const_flat_view = const_view<total()>;

    template <size_t I> requires (I < ndim())
    using subarray = impl::array_index_type<store, I, N...>::type;
    template <size_t I> requires (I < ndim())
    using const_subarray = impl::array_index_type<const store, I, N...>::type;

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
        for (size_type j = 0; j < impl::array_step<N...>; ++j, ++i) {
            std::construct_at(&__data[i].value, v.__data[j].value);
        }
    }

    constexpr void build(size_type& i, init&& v)
        noexcept (meta::nothrow::movable<impl::ref<T>>)
        requires (sizeof...(N) > 1 && meta::movable<impl::ref<T>>)
    {
        for (size_type j = 0; j < impl::array_step<N...>; ++j, ++i) {
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

    [[nodiscard]] constexpr Array(const view<N...>& view)
        noexcept (meta::nothrow::copyable<impl::ref<T>>)
        requires (meta::copyable<impl::ref<T>>)
    {
        for (size_type i = 0; i < total(); ++i) {
            std::construct_at(&__data[i].value, view.ptr[i].value);
        }
    }

    [[nodiscard]] constexpr Array(const const_view<N...>& view)
        noexcept (meta::nothrow::copyable<impl::ref<T>>)
        requires (meta::copyable<impl::ref<T>>)
    {
        for (size_type i = 0; i < total(); ++i) {
            std::construct_at(&__data[i].value, view.ptr[i].value);
        }
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

    /* Swap the contents of this array with another array. */
    constexpr void swap(Array& other)
        noexcept (meta::lvalue<T> || meta::nothrow::swappable<T>)
        requires (meta::lvalue<T> || meta::swappable<T>)
    {
        for (size_type i = 0; i < total(); ++i) {
            std::ranges::swap(__data[i].value, other.__data[i].value);
        }
    }

    /* Get a pointer to the underlying data buffer.  Note that this is always a flat,
    contiguous, row-major region of length `total()`, where lvalue types are converted
    into nested pointers.  Due to the way the array is laid out in memory, this pointer
    is guaranteed to be accurate at run time, but technically invokes undefined
    behavior that prevents it from being used in constant expressions. */
    [[nodiscard]] constexpr auto data() noexcept {
        if constexpr (meta::lvalue<T>) {
            return reinterpret_cast<pointer*>(&__data[0].value);
        } else {
            return std::addressof(*__data[0].value);
        }
    }

    /* Get a pointer to the underlying data buffer.  Note that this is always a flat,
    contiguous, row-major region of length `total()`, where lvalue types are converted
    into nested pointers. */
    [[nodiscard]] constexpr auto data() const noexcept {
        if constexpr (meta::lvalue<T>) {
            return reinterpret_cast<pointer*>(&__data[0].value);
        } else {
            return std::addressof(*__data[0].value);
        }
    }

    [[nodiscard]] constexpr flat_view flatten() noexcept {
        return {__data};
    }

    [[nodiscard]] constexpr const_flat_view flatten() const noexcept {
        return {__data};
    }

    template <size_t... M> requires ((M * ... * 1) == total())
    [[nodiscard]] constexpr view<M...> reshape() noexcept {
        return {__data};
    }

    template <size_t... M> requires ((M * ... * 1) == total())
    [[nodiscard]] constexpr const_view<M...> reshape() const noexcept {
        return {__data};
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


    /// TODO: reflect across the diagonal without changing shape.  This can possibly
    /// be an in-place operation, doing elementwise swaps.


    /// TODO: front()/back()


    /// TODO: a tuple indexing operator just like below


    /* Access an element of the array by its multidimensional index.  The number
    of indices must match the number of dimensions. */
    template <typename Self, meta::integer... I> requires (sizeof...(I) <= ndim())
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
        if constexpr (sizeof...(I) == ndim()) {
            return (*std::forward<Self>(self).__data[impl::array_index<N...>(i...)].value);
        } else {
            if constexpr (meta::is_const<Self>) {
                return const_subarray<sizeof...(I)>{self.__data + impl::array_index<N...>(i...)};
            } else {
                return subarray<sizeof...(I)>{self.__data + impl::array_index<N...>(i...)};
            }
        }
    }

    [[nodiscard]] constexpr iterator begin() noexcept {
        return {__data};
    }

    [[nodiscard]] constexpr const_iterator begin() const noexcept {
        return {__data};
    }

    [[nodiscard]] constexpr const_iterator cbegin() const noexcept {
        return begin();
    }

    [[nodiscard]] constexpr iterator end() noexcept {
        if constexpr (total() == 0) {
            return {__data};
        } else {
            if consteval {
                return {__data + (total() - 1), size()};
            } else {
                return {__data + total()};
            }
        }
    }

    [[nodiscard]] constexpr const_iterator end() const noexcept {
        if constexpr (total() == 0) {
            return {__data};
        } else {
            if consteval {
                return {__data + (total() - 1), size()};
            } else {
                return {__data + total()};
            }
        }
    }

    [[nodiscard]] constexpr const_iterator cend() const noexcept {
        return end();
    }

    [[nodiscard]] constexpr reverse_iterator rbegin() noexcept {
        return std::make_reverse_iterator(end());
    }

    [[nodiscard]] constexpr const_reverse_iterator rbegin() const noexcept {
        return std::make_reverse_iterator(end());
    }

    [[nodiscard]] constexpr const_reverse_iterator crbegin() const noexcept {
        return rbegin();
    }

    [[nodiscard]] constexpr reverse_iterator rend() noexcept {
        return std::make_reverse_iterator(begin());
    }

    [[nodiscard]] constexpr const_reverse_iterator rend() const noexcept {
        return std::make_reverse_iterator(begin());
    }

    [[nodiscard]] constexpr const_reverse_iterator crend() const noexcept {
        return rend();
    }

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


template <typename... Ts>
    requires (!impl::is_array_view<Ts> && ... && meta::has_common_type<Ts...>)
Array(Ts&&...) -> Array<meta::remove_rvalue<meta::common_type<Ts...>>, sizeof...(Ts)>;


template <typename T, size_t... N>
Array(const impl::array_view<impl::array_storage<T>, N...>&) -> Array<T, N...>;


template <typename T, size_t... N>
Array(const impl::array_view<const impl::array_storage<T>, N...>&) -> Array<T, N...>;


}


_LIBCPP_BEGIN_NAMESPACE_STD


template <bertrand::meta::Array T>
struct tuple_size<T> : integral_constant<size_t, bertrand::meta::unqualify<T>::size()> {};


template <size_t I, bertrand::meta::Array T> requires (I < bertrand::meta::unqualify<T>::size())
struct tuple_element<I, T> {
    using type = decltype((bertrand::meta::unqualify<T>::template get<I>()));
};


template <ssize_t... Is, bertrand::meta::Array T>
constexpr decltype(auto) get(T&& self, index_sequence<Is...>)
    noexcept (requires{{std::forward<T>(self).template get<Is...>()} noexcept;})
    requires (requires{{std::forward<T>(self).template get<Is...>()};})
{
    return (std::forward<T>(self).template get<Is...>());
}


_LIBCPP_END_NAMESPACE_STD


namespace bertrand {


    // static constexpr std::array<std::array<int, 2>, 2> test {
    //     {1, 2},
    //     {3, 4}
    // };


    static constexpr Array test1 {1, 2};

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

        if (arr.shape()[-1] != 2) return false;

        auto x = arr.data();
        if (*x != 1) return false;
        ++x;

        return true;
    }());


    static_assert([] {
        Array<int, 3, 2> arr {Array{1, 2}, Array{3, 4}, Array{5, 6}};
        auto x = arr[0];
        if (x[0] != 1) return false;
        if (x[1] != 2) return false;
        for (auto&& i : arr) {
            // if (i < 1 || i > 6) {
            //     return false;
            // }
            for (auto& j : i) {
                if (j < 1 || j > 6) {
                    return false;
                }
            }
        }
        return true;
    }());


    inline void test() {
        int x = 1;
        int y = 2;
        int z = 3;
        int w = 4;
        Array<int&, 2, 2> arr {
            Array{x, y},
            Array{z, w}
        };
        auto p = arr.data();
    }

}


#endif  // BERTRAND_ARRAY_H
