#ifndef BERTRAND_ARRAY_H
#define BERTRAND_ARRAY_H

#include "bertrand/common.h"
#include "bertrand/except.h"
#include "bertrand/union.h"


namespace bertrand {


namespace impl {
    struct array_tag {};
    struct array_view_tag {};

    /* Compile-time shape and stride specifiers use CTAD to allow simple braced
    initializer syntax, which permits custom strides. */
    template <size_t rank>
    struct extent {
        ssize_t dim[rank];

        constexpr extent() noexcept = default;

        template <meta::integer... T> requires (sizeof...(T) == rank)
        constexpr extent(T&&... n) noexcept : dim{ssize_t(n)...} {}

        [[nodiscard]] static constexpr size_t size() noexcept { return rank; }
        [[nodiscard]] static constexpr ssize_t ssize() noexcept { return ssize_t(rank); }
        [[nodiscard]] static constexpr bool empty() noexcept { return rank == 0; }

        [[nodiscard]] constexpr ssize_t operator[](ssize_t i) const noexcept {
            return dim[impl::normalize_index(ssize(), i)];
        }

        [[nodiscard]] constexpr extent reverse() const noexcept {
            extent r;
            for (size_t j = 0; j < size(); ++j) {
                r.dim[j] = dim[size() - 1 - j];
            }
            return r;
        }

        template <size_t I = 1> requires (I <= size())
        [[nodiscard]] constexpr extent<size() - I> left_strip() const noexcept {
            extent<size() - I> s;
            for (size_t j = I; j < size(); ++j) {
                s.dim[j - I] = dim[j];
            }
            return s;
        }

        template <size_t I = 1> requires (I <= size())
        [[nodiscard]] constexpr extent<size() - I> right_strip() const noexcept {
            extent<size() - I> s;
            for (size_t j = 0; j < size() - I; ++j) {
                s.dim[j] = dim[j];
            }
            return s;
        }

        [[nodiscard]] constexpr ssize_t product() const noexcept {
            ssize_t p = 1;
            for (ssize_t j = 0; j < ssize(); ++j) {
                p *= dim[j];
            }
            return p;
        }

        template <ssize_t I> requires (impl::valid_index<ssize(), I>)
        [[nodiscard]] constexpr ssize_t left_product() const noexcept {
            ssize_t p = 1;
            for (ssize_t j = 0, k = impl::normalize_index<ssize(), I>(); j < k; ++j) {
                p *= dim[j];
            }
            return p;
        }

        template <ssize_t I> requires (impl::valid_index<ssize(), I>)
        [[nodiscard]] constexpr ssize_t right_product() const noexcept {
            ssize_t p = 1;
            for (ssize_t j = impl::normalize_index<ssize(), I>(); j < ssize(); ++j) {
                p *= dim[j];
            }
            return p;
        }

        [[nodiscard]] constexpr extent c_strides() const noexcept {
            if constexpr (size() == 0) {
                return {};
            } else {
                extent s;
                size_t j = size() - 1;
                s.dim[j] = 1;
                while (j-- > 0) {
                    s.dim[j] = s.dim[j + 1] * dim[j + 1];
                }
                return s;
            }
        }

        [[nodiscard]] constexpr extent fortran_strides() const noexcept {
            if constexpr (size() == 0) {
                return {};
            } else {
                extent s;
                s.dim[0] = 1;
                for (size_t j = 1; j < size(); ++j) {
                    s.dim[j] = s.dim[j - 1] * dim[j - 1];
                }
                return s;
            }
        }

        template <size_t R>
        [[nodiscard]] constexpr bool operator==(const extent<R>& other) const noexcept {
            if constexpr (R == size()) {
                for (size_t i = 0; i < size(); ++i) {
                    if (dim[i] != other.dim[i]) {
                        return false;
                    }
                }
                return true;
            } else {
                return false;
            }
        }
    };

    template <meta::integer... N>
    extent(N...) -> extent<sizeof...(N)>;

    /* A configuration struct that holds miscellaneous flags for an array or array
    view definition. */
    template <size_t rank>
    struct array_flags {
        /* If false (the default), then the strides will be computed in C (row-major)
        order, with the last index varying the fastest.  Setting this to true specifies
        that indices should be interpreted in Fortran (column-major) order instead,
        where the first index varies the fastest.  Toggling this flag for a view
        effectively transposes the array. */
        [[no_unique_address]] int8_t fortran = -1;

        /* A set of custom strides to use for each dimension.  If given, the strides
        will always match their corresponding shape dimension. */
        [[no_unique_address]] extent<rank> strides;

        [[nodiscard]] constexpr bool empty() const noexcept {
            return fortran < 0;
        }

        [[nodiscard]] constexpr array_flags normalize() const noexcept {
            if (empty()) {
                return {.fortran = false};
            } else {
                return *this;
            }
        }

        [[nodiscard]] constexpr bool operator==(const array_flags& other) const noexcept {
            return fortran == other.fortran && strides == other.strides;
        }
    };

}


template <meta::not_reference T, impl::extent Shape, impl::array_flags<Shape.size()> flags = {}>
    requires (
        meta::not_void<T> &&
        !Shape.empty() &&
        (Shape.size() > 1 ? Shape.product() > 0 : Shape.product() >= 0)
    )
struct Array;


template <meta::not_reference T, impl::extent Shape = {}, impl::array_flags<Shape.size()> flags = {}>
    requires (
        meta::not_void<T> &&
        (Shape.size() > 1 ? Shape.product() > 0 : Shape.product() >= 0)
    )
struct ArrayView;


namespace meta {

    template <
        typename C,
        typename T = impl::array_tag,
        impl::extent shape = {},
        impl::array_flags<shape.size()> flags = {}
    >
    concept Array =
        inherits<C, impl::array_tag> &&
        (
            ::std::same_as<T, impl::array_tag> ||
            ::std::same_as<typename unqualify<C>::type, T>
        ) &&
        (shape.empty() || shape == unqualify<C>::shape()) &&
        (flags.empty() || flags == unqualify<C>::flags());

    template <
        typename C,
        typename T = impl::array_view_tag,
        impl::extent shape = {},
        impl::array_flags<shape.size()> flags = {}
    >
    concept ArrayView =
        inherits<C, impl::array_view_tag> &&
        (
            ::std::same_as<T, impl::array_view_tag> ||
            ::std::same_as<typename unqualify<C>::type, T>
        ) &&
        (shape.empty() || shape == unqualify<C>::shape()) &&
        (flags.empty() || flags == unqualify<C>::flags());

}


namespace impl {

    /* Multidimensional arrays can take structured data in their constructors, which
    consists of a series of nested arrays of 1 fewer dimension representing the major
    axis of the parent array.  The nested arrays will then be flattened into the outer
    buffer, completing the constructor. */
    template <typename T, extent>
    struct _array_init { using type = T; };
    template <typename T, extent shape> requires (!shape.empty())
    struct _array_init<T, shape> : _array_init<Array<T, shape.dim[0]>, shape.left_strip()> {};
    template <typename T, extent shape>
    using array_init = _array_init<T, shape.left_strip()>::type;

    template <extent shape, bool fortran>
    constexpr extent array_strides = shape.c_strides();
    template <extent shape>
    constexpr extent array_strides<shape, true> = shape.fortran_strides();

    /// TODO: array_storage can be trivial after this paper is implemented, which also
    /// possibly affects union types (especially together with reflection):
    /// https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p3074r7.html

    /* Array elements are stored as unions in order to sidestep default constructors,
    and allow the array to be allocated in an uninitialized state.  This is crucial to
    allow efficient construction from iterables, etc. */
    template <meta::not_reference T>
    union array_storage {
        using type = T;

        T value;

        constexpr array_storage() noexcept {}

        template <meta::convertible_to<T> U>
        constexpr array_storage(U&& u) noexcept (meta::nothrow::convertible_to<U, T>) :
            value(std::forward<U>(u))
        {}

        constexpr array_storage(const array_storage& other)
            noexcept (meta::nothrow::copyable<T>)
            requires (meta::copyable<T>)
        :
            value(other.value)
        {}

        constexpr array_storage(array_storage&& other)
            noexcept (meta::nothrow::movable<T>)
            requires (meta::movable<T>)
        :
            value(std::move(other).value)
        {}

        constexpr array_storage& operator=(const array_storage& other)
            noexcept (meta::nothrow::copy_assignable<T>)
            requires (meta::copy_assignable<T>)
        {
            value = other.value;
            return *this;
        }

        constexpr array_storage& operator=(array_storage&& other)
            noexcept (meta::nothrow::move_assignable<T>)
            requires (meta::move_assignable<T>)
        {
            value = std::move(other).value;
            return *this;
        }

        constexpr ~array_storage() noexcept {
            if constexpr (!meta::trivially_destructible<T>) {
                std::destroy_at(&value);
            }
        }
    };

    template <typename T>
    constexpr bool _is_array_storage = false;
    template <typename T>
    constexpr bool _is_array_storage<array_storage<T>> = true;
    template <typename T>
    concept is_array_storage = _is_array_storage<meta::unqualify<T>>;

    /* Because of the `array_storage` normalization, accessors need to account for the
    extra layer of indirection. */
    template <typename T>
    [[nodiscard]] constexpr decltype(auto) array_access(T* ptr) noexcept {
        if constexpr (is_array_storage<T>) {
            return (ptr->value);
        } else {
            return (*ptr);
        }
    }
    template <typename T>
    [[nodiscard]] constexpr decltype(auto) array_access(T* ptr, std::ptrdiff_t i) noexcept {
        if constexpr (is_array_storage<T>) {
            return (ptr[i].value);
        } else {
            return (ptr[i]);
        }
    }

    template <meta::not_reference T>
    struct _array_type { using type = T; };
    template <meta::not_reference T> requires (is_array_storage<T>)
    struct _array_type<T> { using type = meta::unqualify<T>::type; };
    template <meta::not_reference T>
    using array_type = _array_type<T>::type;


    /// TODO: here's where the extent refactor needs to 

    /* The outermost array value type also needs to be promoted to an array view if
    it is multidimensional, and needs to correct for the `array_storage` normalization
    as well. */
    template <typename T, size_t... Ns>
    struct _array_value { using type = array_type<T>; };
    template <typename T, size_t N, size_t... Ns>
    struct _array_value<T, N, Ns...> { using type = ArrayView<T, N, Ns...>; };
    template <typename T, size_t... Ns>
    using array_value = _array_value<T, Ns...>::type;

    /* Array iterators are implemented as raw pointers into the array buffer.  Due to
    aggressive UB sanitization during constant evaluation, an extra count is required
    to avoid overstepping the end of the array.  This index is ignored at run time,
    ensuring zero-cost iteration, without interfering with compile-time uses. */
    template <meta::not_reference T, size_t N, size_t... Ns>
    struct array_iterator {
        /// TODO: the iterator tag should always be contiguous, but is this always
        /// the case for transpose views?
        using iterator_category = std::conditional_t<
            sizeof...(Ns) == 0,
            std::contiguous_iterator_tag,
            std::random_access_iterator_tag
        >;
        using difference_type = std::ptrdiff_t;
        using value_type = array_value<T, Ns...>;
        using reference = meta::as_lvalue<value_type>;
        using pointer = meta::as_pointer<value_type>;

    private:
        using view = value_type;

    public:
        T* ptr = nullptr;
        difference_type count = 0;
        static constexpr difference_type stride = (Ns * ... * 1);

        [[nodiscard]] constexpr decltype(auto) operator*() const noexcept {
            if constexpr (sizeof...(Ns) == 0) {
                return array_access(ptr);
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
                return array_access(ptr, n);
            } else {
                return view{ptr + n * stride};
            }
        }

        constexpr array_iterator& operator++() noexcept {
            if consteval {
                ++count;
                if (count > 0 && count < N) {
                    ptr += stride;
                }
            } else {
                ptr += stride;
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
                    return {self.ptr - self.count * (self.count > 0) * stride, new_count};
                } else if (new_count >= N) {
                    return {self.ptr + (N - self.count - 1) * (self.count < N) * stride, new_count};
                } else {
                    return {self.ptr + n * stride, new_count};
                }
            } else {
                return {self.ptr + n * stride};
            }
        }

        [[nodiscard]] friend constexpr array_iterator operator+(
            difference_type n,
            const array_iterator& self
        ) noexcept {
            if consteval {
                difference_type new_count = self.count + n;
                if (new_count < 0) {
                    return {self.ptr - self.count * (self.count > 0) * stride, new_count};
                } else if (new_count >= N) {
                    return {self.ptr + (N - self.count - 1) * (self.count < N) * stride, new_count};
                } else {
                    return {self.ptr + n * stride, new_count};
                }
            } else {
                return {self.ptr + n * stride};
            }
        }

        constexpr array_iterator& operator+=(difference_type n) noexcept {
            if consteval {
                difference_type new_count = count + n;
                if (new_count < 0) {
                    ptr -= count * (count > 0) * stride;
                } else if (new_count >= N) {
                    ptr += (N - count - 1) * (count < N) * stride;
                } else {
                    ptr += n * stride;
                }
                count = new_count;
            } else {
                ptr += n * stride;
            }
            return *this;
        }

        constexpr array_iterator& operator--() noexcept {
            if consteval {
                if (count > 0 && count < N) {
                    ptr -= stride;
                }
                --count;
            } else {
                ptr -= stride;
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
                    return {ptr - count * (count > 0) * stride, new_count};
                } else if (new_count >= N) {
                    return {ptr + (N - count - 1) * (count < N) * stride, new_count};
                } else {
                    return {ptr - n * stride, new_count};
                }
            } else {
                return {ptr - n * stride};
            }
        }

        [[nodiscard]] constexpr difference_type operator-(
            const array_iterator& other
        ) const noexcept {
            if consteval {
                return count - other.count;
            } else {
                return (ptr - other.ptr) / stride;
            }
        }

        constexpr array_iterator& operator-=(difference_type n) noexcept {
            if consteval {
                difference_type new_count = count - n;
                if (new_count < 0) {
                    ptr -= count * (count > 0) * stride;
                } else if (new_count >= N) {
                    ptr += (N - count - 1) * (count < N) * stride;
                } else {
                    ptr -= n * stride;
                }
                count = new_count;
            } else {
                ptr -= n * stride;
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

    /// TODO: index errors are defined after filling in int <-> str conversions.

    inline constexpr IndexError array_index_error(size_t i) noexcept;

    /* Indices are always computed in C (row-major) memory order for simplicity and
    compatibility with C++.  If an index is signed, then Python-style wraparound will
    be applied to handle negative values.  A bounds check is then applied as a debug
    assertion, which throws an `IndexError` if the index is out of bounds. */
    template <size_t M, size_t... Ms, meta::unsigned_integer I, typename... Is>
    constexpr size_t array_index(const I& i, const Is&... is)
        noexcept (!DEBUG && meta::nothrow::explicitly_convertible_to<I, size_t>)
        requires (meta::explicitly_convertible_to<I, size_t>)
    {
        if constexpr (DEBUG) {
            if (i >= M) {
                throw array_index_error(i);
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
                throw array_index_error(i);
            }
        }
        if constexpr (sizeof...(Is) == 0) {
            return size_t(j) * (Ms * ... * 1);
        } else {
            return size_t(j) * (Ms * ... * 1) + array_index<Ms...>(is...);
        }
    }

    /* Tuple access for array types always takes signed integers and applies both
    wraparound and bounds-checking at compile time. */
    template <ssize_t... I>
    struct valid_array_index {
        template <size_t... N>
        static constexpr bool value = true;
        template <size_t... N>
        static constexpr size_t index = 0;
    };
    template <ssize_t I, ssize_t... Is>
    struct valid_array_index<I, Is...> {
        template <size_t N, size_t... Ns>
        static constexpr bool value = false;
        template <size_t N, size_t... Ns> requires (impl::valid_index<N, I>)
        static constexpr bool value<N, Ns...> = valid_array_index<Is...>::template value<Ns...>;

        template <size_t N, size_t... Ns>
        static constexpr size_t index =
            array_index<N, Ns...>(I) + valid_array_index<Is...>::template index<Ns...>;
    };

    /* When indexing into a multidimensional array, the result might be returned as a
    view or full array with reduced shape based on the number of indices that were
    provided.  If fewer indices than dimensions are given, this helper will deduce a
    reduced shape. */
    template <typename T, size_t I, size_t N, size_t... Ns>
    struct subarray : subarray<T, I - 1, Ns...> {};
    template <typename T, size_t N, size_t... Ns>
    struct subarray<T, 0, N, Ns...> {
        using array = Array<T, N, Ns...>;
        using view = ArrayView<T, N, Ns...>;
    };

    /* Squeezing an array removes all singleton dimensions unless that is the only
    remaining dimension. */
    template <typename T, size_t... Ns>
    struct _array_squeeze {
        template <size_t... M>
        using array = Array<T, M...>;
        template <size_t... M>
        using view = ArrayView<T, M...>;
    };
    template <typename T, size_t N, size_t... Ns>
    struct _array_squeeze<T, N, Ns...> {
        template <size_t... M>
        using array = _array_squeeze<T, Ns...>::template array<M..., N>;
        template <size_t... M>
        using view = _array_squeeze<T, Ns...>::template view<M..., N>;
    };
    template <typename T, size_t... Ns>
    struct _array_squeeze<T, 1, Ns...> {
        template <size_t... M>
        struct type {
            using array = _array_squeeze<T, Ns...>::template array<M...>;
            using view = _array_squeeze<T, Ns...>::template view<M...>;
        };
        template <size_t... M> requires (sizeof...(Ns) == 0 && sizeof...(M) == 0)
        struct type<M...> {
            using array = Array<T, 1>;
            using view = ArrayView<T, 1>;
        };
        template <size_t... M>
        using array = type<M...>::array;
        template <size_t... M>
        using view = type<M...>::view;
    };
    template <typename T, size_t... Ns>
    using array_squeeze = _array_squeeze<T, Ns...>;

    /// TODO: transposing the array can be done by returning a special kind of view
    /// + iterator that effectively reverses the ordering of the shape dimensions,
    /// and applies the same transformation to the indexing operations, such that
    /// the first index varies the fastest instead of the last.  This will yield a
    /// view that is still low-cost, but has the opposite memory access pattern,
    /// which is still much cheaper than returning a new array.  The transpose view is
    /// still convertible to an array, so extracting the transpose array is trivial.

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


/* A non-owning view into a contiguous memory buffer that interprets it as a
multidimensional array with a fixed shape.

Array views consist of a simple pointer to the start of the underlying data buffer,
which can come from any source, similar to `std::span`.  Their shape is held entirely
at compile time, meaning that they are essentially free to construct and copy, and
can be passed around by value without any overhead.

Views can be used just like normal arrays in most respects, and behave identically
under iteration, indexing, reshaping, etc.  The only difference is that they do not
own their data, and do not extend their lifetimes in any way.  It is therefore user's
responsibility to ensure that a view never outlives its buffer, lest it dangle.  In
order to facilitate this, all views are implicitly convertible into full arrays,
possibly via a CTAD guide that infers the proper shape and type, which equates to a
copy of the underlying data into a new buffer. */
template <meta::not_reference T, impl::extent Shape, impl::array_flags<Shape.size()> flags>
    requires (
        meta::not_void<T> &&
        (Shape.size() > 1 ? Shape.product() > 0 : Shape.product() >= 0)
    )
struct ArrayView : impl::array_view_tag {
private:
    static constexpr size_t outer_stride = Shape.product(1);

public:
    using size_type = size_t;
    using index_type = ssize_t;
    using value_type = impl::array_value<T, Ns...>;
    using reference = meta::as_lvalue<value_type>;
    using const_reference = meta::as_const<reference>;
    using pointer = meta::as_pointer<reference>;
    using const_pointer = meta::as_pointer<const_reference>;
    using iterator = impl::array_iterator<T, N, Ns...>;
    using const_iterator = impl::array_iterator<meta::as_const<T>, N, Ns...>;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    T* ptr = nullptr;

    [[nodiscard]] constexpr ArrayView() noexcept = default;
    [[nodiscard]] constexpr ArrayView(T* p) noexcept : ptr(p) {}

    template <typename A> requires (meta::data_returns<T*, A>)
    [[nodiscard]] constexpr ArrayView(A& arr) noexcept:
        ptr(std::ranges::data(arr))
    {}

    constexpr void swap(ArrayView& other) noexcept {
        std::swap(ptr, other.ptr);
    }

    [[nodiscard]] static constexpr size_type ndim() noexcept {
        return sizeof...(Ns) + 1;
    }

    template <size_type M, size_type... Ms> requires ((sizeof...(Ms) + 1) == ndim())
    [[nodiscard]] static constexpr bool has_shape() noexcept {
        return ((N == M) && ... && (Ns == Ms));
    }

    [[nodiscard]] static constexpr const Array<size_type, ndim()>& shape() noexcept {
        return impl::array_shape<N, Ns...>;
    }

    [[nodiscard]] static constexpr const Array<size_type, ndim()>& stride() noexcept {
        return impl::array_strides<std::make_index_sequence<ndim()>, N, Ns...>;
    }

    [[nodiscard]] static constexpr size_type total() noexcept {
        return (N * ... * Ns);
    }

    [[nodiscard]] static constexpr size_type size() noexcept {
        return N;
    }

    [[nodiscard]] static constexpr index_type ssize() noexcept {
        return index_type(N);
    }

    [[nodiscard]] static constexpr bool empty() noexcept {
        return N == 0;
    }

    template <size_t... Ms> requires (sizeof...(Ms) > 0 && (Ms * ... * 1) == total())
    using view = ArrayView<T, Ms...>;

    template <size_t... Ms> requires (sizeof...(Ms) > 0 && (Ms * ... * 1) == total())
    using const_view = ArrayView<meta::as_const<T>, Ms...>;

    [[nodiscard]] constexpr auto data() noexcept {
        if constexpr (impl::is_array_storage<T>) {
            return std::addressof(ptr->value);
        } else {
            return ptr;
        }
    }

    [[nodiscard]] constexpr auto data() const noexcept {
        if constexpr (impl::is_array_storage<T>) {
            return std::addressof(static_cast<meta::as_const<T>*>(ptr)->value);
        } else {
            return static_cast<meta::as_const<T>*>(ptr);
        }
    }

    [[nodiscard]] constexpr iterator begin() noexcept {
        return {ptr};
    }

    [[nodiscard]] constexpr const_iterator begin() const noexcept {
        return {ptr};
    }

    [[nodiscard]] constexpr const_iterator cbegin() const noexcept {
        return begin();
    }

    [[nodiscard]] constexpr iterator end() noexcept {
        if consteval {
            return {ptr + (N - 1) * outer_stride, N};
        } else {
            return {ptr + N * outer_stride};
        }
    }

    [[nodiscard]] constexpr const_iterator end() const noexcept {
        if consteval {
            return {ptr + (N - 1) * outer_stride, N};
        } else {
            return {ptr + N * outer_stride};
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

    [[nodiscard]] constexpr view<total()> flatten() noexcept {
        return {ptr};
    }

    [[nodiscard]] constexpr const_view<total()> flatten() const noexcept {
        return {ptr};
    }

    template <size_type M, size_type... Ms> requires ((M * ... * Ms) == total())
    [[nodiscard]] constexpr view<M, Ms...> reshape() noexcept {
        return {ptr};
    }

    template <size_type M, size_type... Ms> requires ((M * ... * Ms) == total())
    [[nodiscard]] constexpr const_view<M, Ms...> reshape() const noexcept {
        return {ptr};
    }

    [[nodiscard]] constexpr auto squeeze() noexcept {
        using view = impl::array_squeeze<T, N, Ns...>::template view<>;
        return view{ptr};
    }

    [[nodiscard]] constexpr auto squeeze() const noexcept {
        using const_view = impl::array_squeeze<meta::as_const<T>, N, Ns...>::template view<>;
        return const_view{static_cast<meta::as_const<T>*>(ptr)};
    }

    /// TODO: transpose()

    [[nodiscard]] constexpr decltype(auto) front() noexcept {
        if constexpr (sizeof...(Ns) == 0) {
            return (impl::array_access(ptr));
        } else {
            using view = value_type;
            return view{ptr};
        }
    }

    [[nodiscard]] constexpr decltype(auto) front() const noexcept {
        if constexpr (sizeof...(Ns) == 0) {
            return (impl::array_access(static_cast<meta::as_const<T>*>(ptr)));
        } else {
            using const_view = ArrayView<meta::as_const<T>, Ns...>;
            return const_view{static_cast<meta::as_const<T>*>(ptr)};
        }
    }

    [[nodiscard]] constexpr decltype(auto) back() noexcept {
        if constexpr (sizeof...(Ns) == 0) {
            return (impl::array_access(ptr, (N - 1) * outer_stride));
        } else {
            using view = value_type;
            return view{ptr + (N - 1) * outer_stride};
        }
    }

    [[nodiscard]] constexpr decltype(auto) back() const noexcept {
        if constexpr (sizeof...(Ns) == 0) {
            return (impl::array_access(
                static_cast<meta::as_const<T>*>(ptr),
                (N - 1) * outer_stride
            ));
        } else {
            using const_view = ArrayView<meta::as_const<T>, Ns...>;
            return const_view{static_cast<meta::as_const<T>*>(ptr) + (N - 1) * outer_stride};
        }
    }

    template <index_type... I>
        requires (
            sizeof...(I) <= ndim() &&
            impl::valid_array_index<I...>::template value<N, Ns...>
        )
    [[nodiscard]] constexpr decltype(auto) get() noexcept {
        constexpr size_type j = impl::valid_array_index<I...>::template index<N, Ns...>;
        if constexpr (sizeof...(I) == ndim()) {
            return (array_access(ptr, j));
        } else {
            using view = impl::subarray<T, sizeof...(I), N, Ns...>::view;
            return view{ptr + j};
        }
    }

    template <index_type... I>
        requires (
            sizeof...(I) <= ndim() &&
            impl::valid_array_index<I...>::template value<N, Ns...>
        )
    [[nodiscard]] constexpr decltype(auto) get() const noexcept {
        constexpr size_type j = impl::valid_array_index<I...>::template index<N, Ns...>;
        if constexpr (sizeof...(I) == ndim()) {
            return (array_access(static_cast<meta::as_const<T>*>(ptr), j));
        } else {
            using view = impl::subarray<meta::as_const<T>, sizeof...(I), N, Ns...>::view;
            return view{static_cast<meta::as_const<T>*>(ptr) + j};
        }
    }

    template <meta::integer... I> requires (sizeof...(I) <= ndim())
    [[nodiscard]] constexpr decltype(auto) operator[](const I&... i)
        noexcept (!DEBUG && ((meta::unsigned_integer<I> ?
            meta::nothrow::explicitly_convertible_to<I, size_t> :
            meta::nothrow::explicitly_convertible_to<I, ssize_t>
        ) && ...))
        requires ((meta::unsigned_integer<I> ?
            meta::explicitly_convertible_to<I, size_t> :
            meta::explicitly_convertible_to<I, ssize_t>
        ) && ...)
    {
        size_type j = impl::array_index<N, Ns...>(i...);
        if constexpr (sizeof...(I) == ndim()) {
            return (array_access(ptr, j));
        } else {
            using view = impl::subarray<T, sizeof...(I), N, Ns...>::view;
            return view{ptr + j};
        }
    }

    template <meta::integer... I> requires (sizeof...(I) <= ndim())
    [[nodiscard]] constexpr decltype(auto) operator[](const I&... i) const
        noexcept (!DEBUG && ((meta::unsigned_integer<I> ?
            meta::nothrow::explicitly_convertible_to<I, size_t> :
            meta::nothrow::explicitly_convertible_to<I, ssize_t>
        ) && ...))
        requires ((meta::unsigned_integer<I> ?
            meta::explicitly_convertible_to<I, size_t> :
            meta::explicitly_convertible_to<I, ssize_t>
        ) && ...)
    {
        size_type j = impl::array_index<N, Ns...>(i...);
        if constexpr (sizeof...(I) == ndim()) {
            return (array_access(static_cast<meta::as_const<T>*>(ptr), j));
        } else {
            using const_view = impl::subarray<meta::as_const<T>, sizeof...(I), N, Ns...>::view;
            return const_view{static_cast<meta::as_const<T>*>(ptr) + j};
        }
    }

    template <typename U>
    [[nodiscard]] constexpr operator Array<U, N, Ns...>() const
        noexcept (meta::nothrow::convertible_to<const_reference, U>)
        requires (meta::convertible_to<const_reference, U>)
    {
        auto result = Array<U, N, Ns...>::reserve();
        for (size_type i = 0; i < total(); ++i) {
            std::construct_at(
                &result.ptr[i].value,
                impl::array_access(ptr, i)
            );
        }
        return result;
    }

    /* Compare two arrays of the same shape for lexicographic equality.  Iteration
    always begins at index 0 across all dimensions and advances the last index first,
    following row-major order. */
    template <typename U>
    [[nodiscard]] constexpr bool operator==(const Array<U, N, Ns...>& other) const
        noexcept (
            meta::nothrow::has_eq<
                const_reference,
                typename Array<U, N, Ns...>::const_reference
            > &&
            meta::nothrow::truthy<meta::eq_type<
                const_reference,
                typename Array<U, N, Ns...>::const_reference
            >>
        )
        requires (
            meta::has_eq<
                const_reference,
                typename Array<U, N, Ns...>::const_reference
            > &&
            meta::truthy<meta::eq_type<
                const_reference,
                typename Array<U, N, Ns...>::const_reference
            >>
        )
    {
        for (size_type i = 0; i < total(); ++i) {
            if (!bool(impl::array_access(ptr, i) == impl::array_access(other.ptr, i))) {
                return false;
            }
        }
        return true;
    }

    /* Compare two arrays of the same shape for lexicographic equality.  Iteration
    always begins at index 0 across all dimensions and advances the last index first,
    following row-major order. */
    template <typename U>
    [[nodiscard]] constexpr bool operator==(const ArrayView<U, N, Ns...>& other) const
        noexcept (
            meta::nothrow::has_eq<
                const_reference,
                typename ArrayView<U, N, Ns...>::const_reference
            > &&
            meta::nothrow::truthy<meta::eq_type<
                const_reference,
                typename ArrayView<U, N, Ns...>::const_reference
            >>
        )
        requires (
            meta::has_eq<
                const_reference,
                typename ArrayView<U, N, Ns...>::const_reference
            > &&
            meta::truthy<meta::eq_type<
                const_reference,
                typename ArrayView<U, N, Ns...>::const_reference
            >>
        )
    {
        for (size_type i = 0; i < total(); ++i) {
            if (!bool(impl::array_access(ptr, i) == impl::array_access(other.ptr, i))) {
                return false;
            }
        }
        return true;
    }

    /* Compare two arrays of the same shape for lexicographic equality.  Iteration
    always begins at index 0 across all dimensions and advances the last index first,
    following row-major order. */
    template <typename U>
    [[nodiscard]] constexpr auto operator<=>(const Array<U, N, Ns...>& other) const
        noexcept (
            meta::nothrow::has_lt<
                const_reference,
                typename Array<U, N, Ns...>::const_reference
            > &&
            meta::nothrow::has_lt<
                typename Array<U, N, Ns...>::const_reference,
                const_reference
            > &&
            meta::nothrow::truthy<meta::lt_type<
                const_reference,
                typename Array<U, N, Ns...>::const_reference
            >> &&
            meta::nothrow::truthy<meta::lt_type<
                typename Array<U, N, Ns...>::const_reference,
                const_reference
            >>
        )
        requires (
            meta::has_lt<
                const_reference,
                typename Array<U, N, Ns...>::const_reference
            > &&
            meta::has_lt<
                typename Array<U, N, Ns...>::const_reference,
                const_reference
            > &&
            meta::truthy<meta::lt_type<
                const_reference,
                typename Array<U, N, Ns...>::const_reference
            >> &&
            meta::truthy<meta::lt_type<
                typename Array<U, N, Ns...>::const_reference,
                const_reference
            >>
        )
    {
        for (size_t i = 0; i < total(); ++i) {
            if (impl::array_access(ptr, i) < impl::array_access(other.ptr, i)) {
                return std::strong_ordering::less;
            } else if (impl::array_access(other.ptr, i) < impl::array_access(ptr, i)) {
                return std::strong_ordering::greater;
            }
        }
        return std::strong_ordering::equal;
    }

    /* Compare two arrays of the same shape for lexicographic equality.  Iteration
    always begins at index 0 across all dimensions and advances the last index first,
    following row-major order. */
    template <typename U>
    [[nodiscard]] constexpr auto operator<=>(const ArrayView<U, N, Ns...>& other) const
        noexcept (
            meta::nothrow::has_lt<
                const_reference,
                typename ArrayView<U, N, Ns...>::const_reference
            > &&
            meta::nothrow::has_lt<
                typename ArrayView<U, N, Ns...>::const_reference,
                const_reference
            > &&
            meta::nothrow::truthy<meta::lt_type<
                const_reference,
                typename ArrayView<U, N, Ns...>::const_reference
            >> &&
            meta::nothrow::truthy<meta::lt_type<
                typename ArrayView<U, N, Ns...>::const_reference,
                const_reference
            >>
        )
        requires (
            meta::has_lt<
                const_reference,
                typename ArrayView<U, N, Ns...>::const_reference
            > &&
            meta::has_lt<
                typename ArrayView<U, N, Ns...>::const_reference,
                const_reference
            > &&
            meta::truthy<meta::lt_type<
                const_reference,
                typename ArrayView<U, N, Ns...>::const_reference
            >> &&
            meta::truthy<meta::lt_type<
                typename ArrayView<U, N, Ns...>::const_reference,
                const_reference
            >>
        )
    {
        for (size_t i = 0; i < total(); ++i) {
            if (impl::array_access(ptr, i) < impl::array_access(other.ptr, i)) {
                return std::strong_ordering::less;
            } else if (impl::array_access(other.ptr, i) < impl::array_access(ptr, i)) {
                return std::strong_ordering::greater;
            }
        }
        return std::strong_ordering::equal;
    }
};


template <typename T, size_t N>
ArrayView(T(&)[N]) -> ArrayView<T, N>;


template <typename T, size_t N>
ArrayView(const std::array<T, N>&) -> ArrayView<const T, N>;


template <typename T, size_t... N>
ArrayView(Array<T, N...>&) -> ArrayView<impl::array_storage<T>, N...>;


template <typename T, size_t... N>
ArrayView(const Array<T, N...>&) -> ArrayView<const impl::array_storage<T>, N...>;


/* A generalized, multidimensional array type with fixed shape known at compile time.

Arrays can be of any shape and dimension as long as none are zero or negative.  The
only exception is the one-dimensional case, where a size of zero is allowed to
represent a trivial, empty array.  Arrays may not store references, but permit cv
qualifications if specified.

The array elements are always stored in a contiguous block of memory in C (row-major)
order, with an overall size equal to the cartesian product of the shape.  Iterating
over an array will yield a series of trivial views over sub-arrays of reduced dimension
(stripping axes from left to right), until a 1-D view is reached, which yields the
underlying elements.  Each view reduces to a simple pointer into the flattened data
buffer - See the `ArrayView` class for more details.

Arrays support multidimensional indexing, both at compile time via a tuple-like
`get<I, J, K, ...>()` method, and at run time via `[i, j, k, ...]`.  The indices are
always interpreted in row-major order, meaning that the last index varies the fastest,
and therefore has the smallest stride.  Signed indices will be interpreted using
Python-style wraparound, meaning negative indices will count backwards from the end of
the corresponding dimension.  In debug builds, all indices will be bounds-checked,
throwing an `IndexError` if any are out of range after normalization.  For unsigned
indices in release builds, indexing is always zero-cost.

If fewer indices are provided than the number of dimensions, then a view over the
corresponding sub-array will be returned instead, subject to the same rules as
iteration.  Additionally, arrays can be trivially flattened or reshaped by returning a
view of a different shape (of equal size), which is implemented via the `flatten()`,
`reshape<M...>()`, and `transpose()` methods, respectively, all of which are zero-cost.
Note that transposing an array effectively reverses its shape, switching it from
row-major to column-major order.

Lastly, both arrays and views are mutually interconvertible, with conversion to a view
being zero-cost, and conversion back to an array incurring a copy of the referenced
data.  Both directions are also covered by CTAD guides, which allow the shape and type
to be inferred at compile time, like so:

    ```
    Array<int, 3, 2> arr {
        Array{1, 2},
        Array{3, 4},
        Array{5, 6}
    };

    ArrayView view = arr;  // array -> view
    Array copy = view;  // view -> array (copy)

    Array flat = arr.flatten();  // array -> view -> array (1x6)
    assert(flat == Array{1, 2, 3, 4, 5, 6});

    arr.flatten()[3] = 7;
    assert(arr[1] == Array{3, 7});
    assert(arr != copy);
    ```
*/
template <meta::not_reference T, impl::extent Shape, impl::array_flags<Shape.size()> Flags>
    requires (
        meta::not_void<T> &&
        !Shape.empty() &&
        (Shape.size() > 1 ? Shape.product() > 0 : Shape.product() >= 0)
    )
struct Array : impl::array_tag {
private:
    using init = impl::array_init<T, Shape>;
    using storage = impl::array_storage<T>;

    static constexpr size_t outer_stride = Shape.product(1);
    static constexpr impl::array_flags config = Flags.normalize();
    static constexpr size_t _total = Shape.product();
    static constexpr size_t _size = _total < 0 ? 0 : size_t(_total);

public:
    using type = T;
    using size_type = size_t;
    using index_type = ssize_t;
    using value_type = impl::array_value<T, Ns...>;
    using reference = meta::as_lvalue<value_type>;
    using const_reference = meta::as_const<reference>;
    using pointer = meta::as_pointer<reference>;
    using const_pointer = meta::as_pointer<const_reference>;
    using iterator = impl::array_iterator<storage, N, Ns...>;
    using const_iterator = impl::array_iterator<const storage, N, Ns...>;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    /* The number of dimensions for a multidimensional array.  This is always equal
    to the number of integer template parameters, and is never zero. */
    [[nodiscard]] static constexpr size_type ndim() noexcept {
        return Shape.size();
    }

    /* Check whether this array's shape exactly matches an externally-supplied sequence
    of indices.  Used for compile-time bounds checking. */
    template <impl::extent other>
    [[nodiscard]] static constexpr bool has_shape() noexcept {
        return Shape == other;
    }

    /* The size of the array along each dimension, returned as an
    `Array<size_type, ndim()>`.  The values are always equivalent to the template
    signature, and the array itself is stored statically at compile time. */
    [[nodiscard]] static constexpr ArrayView<const size_type, ndim()> shape() noexcept {
        return {Shape.data};
    }

    /* The step size of the array along each dimension in units of `T`, returned as an
    `Array<size_type, ndim()>`.  The array itself is stored statically at compile time.
    Since the strides are interpreted in row-major order, the first stride will always
    be the largest, consisting of the cartesian product of all subsequent dimensions.
    The last stride is always equal to `1`. */
    [[nodiscard]] static constexpr ArrayView<const size_type, ndim()> stride() noexcept {
        return {impl::array_strides<Shape, *config.fortran>.data};
    }

    [[nodiscard]] static constexpr const impl::array_flags& flags() noexcept {
        return config;
    }

    /* The total number of elements in the array across all dimensions.  This is
    always equivalent to the cartesian product of `shape()`. */
    [[nodiscard]] static constexpr size_type total() noexcept {
        return _total;
    }

    /* The top-level size of the array (i.e. the size of the first dimension).
    This is always equal to the first index of `shape()`, and indicates the number
    of subarrays that will be yielded when the array is iterated over. */
    [[nodiscard]] static constexpr size_type size() noexcept {
        return _size;
    }

    /* Equivalent to `size()`, but as a signed rather than unsigned integer. */
    [[nodiscard]] static constexpr index_type ssize() noexcept {
        return index_type(_size);
    }

    /* True if the array has precisely zero elements.  False otherwise.  This can only
    occur for one-dimensional arrays. */
    [[nodiscard]] static constexpr bool empty() noexcept {
        return _size == 0;
    }

    /* Construct a view of arbitrary shape over the array, as long as the overall sizes
    are the same.  This helper also accounts for the storage type, safely propagating
    the `impl::array_storage` normalization. */
    template <impl::extent S> requires (S.product() == total())
    using view = ArrayView<storage, S>;

    /* Construct an immutable view of arbitrary shape over the array, as long as the
    overall sizes are the same.  This helper also accounts for the storage type, safely
    propagating the `impl::array_storage` normalization, and ensuring that the contents
    are `const`-qualified. */
    template <impl::extent S> requires (S.product() == total())
    using const_view = ArrayView<const storage, S>;

    /* The raw data buffer backing the array.  Note that this is stored as a normalized
    `impl::array_storage` class that allows for trivial default construction, which is
    crucial for conversion from iterable types, etc. */
    storage ptr[total()];

    /* Reserve an uninitialized array, bypassing default constructors.  This operation
    is considered to be unsafe in most circumstances, except when the uninitialized
    elements are constructed in-place immediately afterwards using either
    `std::construct_at()`, placement new, or a trivial assignment.  Operating on an
    array that contains uninitialized values results in undefined behavior. */
    [[nodiscard]] static constexpr Array reserve() noexcept {
        return {storage{}};
    }

private:
    constexpr Array(storage) noexcept {}

    template <typename V> requires (sizeof...(Ns) == 0)
    constexpr void build(size_type& i, V&& v)
        noexcept (meta::nothrow::convertible_to<V, T>)
        requires (meta::convertible_to<V, T>)
    {
        std::construct_at(&ptr[i].value, std::forward<V>(v));
        ++i;
    }

    constexpr void build(size_type& i, const init& v)
        noexcept (meta::nothrow::copyable<impl::ref<T>>)
        requires (sizeof...(Ns) > 0 && meta::copyable<impl::ref<T>>)
    {
        for (size_type j = 0; j < outer_stride; ++j, ++i) {
            std::construct_at(&ptr[i].value, v.ptr[j].value);
        }
    }

    constexpr void build(size_type& i, init&& v)
        noexcept (meta::nothrow::movable<impl::ref<T>>)
        requires (sizeof...(Ns) > 0 && meta::movable<impl::ref<T>>)
    {
        for (size_type j = 0; j < outer_stride; ++j, ++i) {
            std::construct_at(&ptr[i].value, std::move(v.ptr[j].value));
        }
    }

public:
    /* Default-construct all elements of the array. */
    [[nodiscard]] constexpr Array()
        noexcept (meta::nothrow::default_constructible<T>)
        requires (meta::default_constructible<T>)
    {
        for (size_type i = 0; i < total(); ++i) {
            std::construct_at(&ptr[i].value);
        }
    }

    /* Construct a one-dimensional array from flat inputs.  The number of arguments
    must always match the `size()` of the top-level array, and will directly initialize
    the array without any further overhead. */
    template <meta::convertible_to<init>... Ts> requires (ndim() == 1)
    [[nodiscard]] constexpr Array(Ts&&... v)
        noexcept ((meta::nothrow::convertible_to<Ts, init> && ...))
        requires (!empty() && sizeof...(Ts) == size())
    :
        ptr{std::forward<Ts>(v)...}
    {}

    /* Construct a multidimensional array from structured inputs.  The number of
    arguments must always match the `size()` of the top-level array.  Each argument
    must be convertible to a nested array type, which has one fewer dimension than
    the outer array, and whose element type may be a further nested array until all
    dimensions are exhausted.  Each nested array will be flattened into the outer
    array in row-major order. */
    template <meta::convertible_to<init>... Ts> requires (ndim() > 1)
    [[nodiscard]] constexpr Array(Ts&&... v)
        noexcept (requires(size_type i) {{(build(i, std::forward<Ts>(v)), ...)} noexcept;})
        requires (
            !empty() &&
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

    /* Swap the contents of two arrays. */
    constexpr void swap(Array& other)
        noexcept (meta::lvalue<T> || meta::nothrow::swappable<T>)
        requires (meta::lvalue<T> || meta::swappable<T>)
    {
        for (size_type i = 0; i < total(); ++i) {
            std::ranges::swap(ptr[i].value, other.ptr[i].value);
        }
    }

    /* Get a pointer to the underlying data buffer.  Note that this is always a flat,
    contiguous, row-major block of length `total()`, where lvalue types are converted
    into nested pointers.  Due to the `impl::array_storage` normalization, this pointer
    is guaranteed to be accurate at run time, but technically invokes undefined
    behavior, which prevents it from being used in constant expressions. */
    [[nodiscard]] constexpr auto data() noexcept {
        return std::addressof(ptr->value);
    }

    /* Get a pointer to the underlying data buffer.  Note that this is always a flat,
    contiguous, row-major block of length `total()`, where lvalue types are converted
    into nested pointers.  Due to the way the array is laid out in memory, this pointer
    is guaranteed to be accurate at run time, but technically invokes undefined
    behavior that prevents it from being used in constant expressions.  A future
    revision of C++ may eliminate this issue, and obviate the need for normalization to
    begin with. */
    [[nodiscard]] constexpr auto data() const noexcept {
        return std::addressof(ptr->value);
    }


    /* Get a forward iterator over the first dimension of the array.  For
    multidimensional arrays, the iterator will yield recursively nested views over
    reduced subarrays for each dimension, until a 1-D view is reached. */
    [[nodiscard]] constexpr iterator begin() noexcept {
        return {ptr};
    }

    /* Get a forward iterator over the first dimension of the array.  For
    multidimensional arrays, the iterator will yield recursively nested views over
    reduced subarrays for each dimension, until a 1-D view is reached. */
    [[nodiscard]] constexpr const_iterator begin() const noexcept {
        return {ptr};
    }

    /* Get a forward iterator over the first dimension of the array.  For
    multidimensional arrays, the iterator will yield recursively nested views over
    reduced subarrays for each dimension, until a 1-D view is reached. */
    [[nodiscard]] constexpr const_iterator cbegin() const noexcept {
        return begin();
    }

    /* Get a forward sentinel to one-past the end of the array. */
    [[nodiscard]] constexpr iterator end() noexcept {
        if constexpr (total() == 0) {
            return {ptr};
        } else {
            if consteval {
                return {ptr + total() - outer_stride, size()};
            } else {
                return {ptr + total()};
            }
        }
    }

    /* Get a forward sentinel to one-past the end of the array. */
    [[nodiscard]] constexpr const_iterator end() const noexcept {
        if constexpr (total() == 0) {
            return {ptr};
        } else {
            if consteval {
                return {ptr + total() - outer_stride, size()};
            } else {
                return {ptr + total()};
            }
        }
    }

    /* Get a forward sentinel to one-past the end of the array. */
    [[nodiscard]] constexpr const_iterator cend() const noexcept {
        return end();
    }

    /* Get a reverse iterator over the first dimension of the array.  For
    multidimensional arrays, the iterator will yield recursively nested views over
    reduced subarrays for each dimension, until a 1-D view is reached. */
    [[nodiscard]] constexpr reverse_iterator rbegin() noexcept {
        return std::make_reverse_iterator(end());
    }

    /* Get a reverse iterator over the first dimension of the array.  For
    multidimensional arrays, the iterator will yield recursively nested views over
    reduced subarrays for each dimension, until a 1-D view is reached. */
    [[nodiscard]] constexpr const_reverse_iterator rbegin() const noexcept {
        return std::make_reverse_iterator(end());
    }

    /* Get a reverse iterator over the first dimension of the array.  For
    multidimensional arrays, the iterator will yield recursively nested views over
    reduced subarrays for each dimension, until a 1-D view is reached. */
    [[nodiscard]] constexpr const_reverse_iterator crbegin() const noexcept {
        return rbegin();
    }

    /* Get a reverse sentinel to one-before the beginning of the array. */
    [[nodiscard]] constexpr reverse_iterator rend() noexcept {
        return std::make_reverse_iterator(begin());
    }

    /* Get a reverse sentinel to one-before the beginning of the array. */
    [[nodiscard]] constexpr const_reverse_iterator rend() const noexcept {
        return std::make_reverse_iterator(begin());
    }

    /* Get a reverse sentinel to one-before the beginning of the array. */
    [[nodiscard]] constexpr const_reverse_iterator crend() const noexcept {
        return rend();
    }

    /* Get a view over the array.  This backs by a CTAD guide to allow inference of
    type and shape. */
    [[nodiscard]] constexpr operator ArrayView<storage, N, Ns...>() noexcept {
        return {ptr};
    }

    /* Get a view over the array.  This backs by a CTAD guide to allow inference of
    type and shape. */
    [[nodiscard]] constexpr operator ArrayView<const storage, N, Ns...>() const noexcept {
        return {ptr};
    }

    /* Return a 1-dimensional view over the array, which equates to a flat view over
    the raw data buffer. */
    [[nodiscard]] constexpr view<total()> flatten() & noexcept {
        return {ptr};
    }

    /* Return a 1-dimensional view over the array, which equates to a flat view over
    the raw data buffer. */
    [[nodiscard]] constexpr const_view<total()> flatten() const & noexcept {
        return {static_cast<const storage*>(ptr)};
    }

    /* Return a 1-dimensional version of the array, moving the current contents to a
    new array. */
    [[nodiscard]] constexpr Array<T, total()> flatten() &&
        noexcept (meta::nothrow::movable<T>)
        requires (meta::movable<T>)
    {
        auto result = Array<T, total()>::reserve();
        for (size_type i = 0; i < total(); ++i) {
            std::construct_at(
                &result.ptr[i].value,
                std::move(ptr[i].value)
            );
        }
        return result;
    }

    /* Return a 1-dimensional version of the array, copying the current contents to a
    new array. */
    [[nodiscard]] constexpr Array<T, total()> flatten() const &&
        noexcept (meta::nothrow::copyable<T>)
        requires (meta::copyable<T>)
    {
        auto result = Array<T, total()>::reserve();
        for (size_type i = 0; i < total(); ++i) {
            std::construct_at(
                &result.ptr[i].value,
                ptr[i].value
            );
        }
        return result;
    }

    /* Return a view over the array with a different shape, as long as the total number
    of elements is the same. */
    template <size_type M, size_type... Ms> requires ((M * ... * Ms) == total())
    [[nodiscard]] constexpr view<M, Ms...> reshape() & noexcept {
        return {ptr};
    }

    /* Return a view over the array with a different shape, as long as the total number
    of elements is the same. */
    template <size_type M, size_type... Ms> requires ((M * ... * Ms) == total())
    [[nodiscard]] constexpr const_view<M, Ms...> reshape() const & noexcept {
        return {static_cast<const storage*>(ptr)};
    }

    /* Return a new array with a different shape, as long as the total number of
    elements is the same.  This overload moves the current contents of the existing
    array. */
    template <size_type M, size_type... Ms> requires ((M * ... * Ms) == total())
    [[nodiscard]] constexpr Array<T, M, Ms...> reshape() &&
        noexcept (meta::nothrow::movable<T>)
        requires (meta::movable<T>)
    {
        auto result = Array<T, M, Ms...>::reserve();
        for (size_type i = 0; i < total(); ++i) {
            std::construct_at(
                &result.ptr[i].value,
                std::move(ptr[i].value)
            );
        }
        return result;
    }

    /* Return a new array with a different shape, as long as the total number of
    elements is the same.  This overload copies the current contents of the existing
    array. */
    template <size_type M, size_type... Ms> requires ((M * ... * Ms) == total())
    [[nodiscard]] constexpr Array<T, M, Ms...> reshape() const &&
        noexcept (meta::nothrow::copyable<T>)
        requires (meta::copyable<T>)
    {
        auto result = Array<T, M, Ms...>::reserve();
        for (size_type i = 0; i < total(); ++i) {
            std::construct_at(
                &result.ptr[i].value,
                ptr[i].value
            );
        }
        return result;
    }


    /* Return a view over the array with all singleton dimensions removed, unless that
    is the only remaining dimension. */
    [[nodiscard]] constexpr auto squeeze() & noexcept {
        using view = impl::array_squeeze<storage, N, Ns...>::template view<>;
        return view{ptr};
    }

    /* Return a view over the array with all singleton dimensions removed, unless that
    is the only remaining dimension. */
    [[nodiscard]] constexpr auto squeeze() const & noexcept {
        using const_view = impl::array_squeeze<const storage, N, Ns...>::template view<>;
        return const_view{static_cast<const storage*>(ptr)};
    }

    /* Return a new array with a different shape, as long as the total number of
    elements is the same.  This overload moves the current contents of the existing
    array. */
    [[nodiscard]] constexpr auto squeeze() &&
        noexcept (meta::nothrow::movable<T>)
        requires (meta::movable<T>)
    {
        using subarray = impl::array_squeeze<T, N, Ns...>::template array<>;
        auto result = subarray::reserve();
        for (size_type i = 0; i < total(); ++i) {
            std::construct_at(
                &result.ptr[i].value,
                std::move(ptr[i].value)
            );
        }
        return result;
    }

    /* Return a new array with a different shape, as long as the total number of
    elements is the same.  This overload copies the current contents of the existing
    array. */
    [[nodiscard]] constexpr auto squeeze() const &&
        noexcept (meta::nothrow::copyable<T>)
        requires (meta::copyable<T>)
    {
        using subarray = impl::array_squeeze<meta::as_const<T>, N, Ns...>::template array<>;
        auto result = subarray::reserve();
        for (size_type i = 0; i < total(); ++i) {
            std::construct_at(
                &result.ptr[i].value,
                ptr[i].value
            );
        }
        return result;
    }





    // using transpose_type = impl::array_transpose<T, N...>::template type<>;

    // /// TODO: transpose would need to copy/move the data into a new array if called on
    // /// an rvalue.

    // [[nodiscard]] constexpr transpose_type transpose() const
    // {
    //     transpose_type result;
    //     /// TODO: figure out the indexing needed to transpose the array.
    //     return result;
    // }

    // [[nodiscard]] constexpr transpose_type transpose() &&
    // {
    //     transpose_type result;
    //     /// TODO: figure out the indexing needed to transpose the array.
    //     return result;
    // }


    /// TODO: squeeze




    /* Access the first item in the array, assuming it isn't empty.  If the array is
    one-dimensional, then this will be a reference to the first underlying value.
    Otherwise, if the array is an lvalue, then it will be a view over the first
    sub-array.  If the array is an rvalue, then the view will be replaced by a full
    array containing the moved contents of the sub-array, to prevent lifetime
    issues. */
    template <typename Self> requires (!empty())
    [[nodiscard]] constexpr decltype(auto) front(this Self&& self) noexcept {
        if constexpr (sizeof...(Ns) == 0) {
            return ((*std::forward<Self>(self).ptr).value);
        } else if constexpr (meta::lvalue<Self>) {
            if constexpr (meta::is_const<Self>) {
                using const_view = ArrayView<const storage, Ns...>;
                return const_view{static_cast<const storage*>(self.ptr)};
            } else {
                using view = ArrayView<storage, Ns...>;
                return view{self.ptr};
            }
        } else {
            auto result = Array<T, Ns...>::reserve();
            for (size_type i = 0; i < outer_stride; ++i) {
                std::construct_at(
                    &result.ptr[i].value,
                    std::move(self.ptr[i].value)
                );
            }
            return result;
        }
    }

    /* Access the last item in the array, assuming it isn't empty.  If the array is
    one-dimensional, then this will be a reference to the last underlying value.
    Otherwise, if the array is an lvalue, then it will be a view over the last
    sub-array.  If the array is an rvalue, then the view will be replaced by a full
    array containing the moved contents of the sub-array, to prevent lifetime
    issues. */
    template <typename Self> requires (!empty())
    [[nodiscard]] constexpr decltype(auto) back(this Self&& self) noexcept {
        constexpr size_type j = (N - 1) * outer_stride;
        if constexpr (sizeof...(Ns) == 0) {
            return (std::forward<Self>(self).ptr[j].value);
        } else if constexpr (meta::lvalue<Self>) {
            if constexpr (meta::is_const<Self>) {
                using const_view = ArrayView<const storage, Ns...>;
                return const_view{static_cast<const storage*>(self.ptr) + j};
            } else {
                using view = ArrayView<storage, Ns...>;
                return view{self.ptr + j};
            }
        } else {
            auto result = Array<T, Ns...>::reserve();
            for (size_type i = 0; i < outer_stride; ++i) {
                std::construct_at(
                    &result.ptr[i].value,
                    std::move(self.ptr[i + j].value)
                );
            }
            return result;
        }
    }

    /* Access an element of the array by its multidimensional index.  The total number
    of indices must be less than or equal to the number of dimensions in the array.
    If fewer indices are provided, then a view over the corresponding sub-array will
    be returned instead, which will be promoted to a full array if the indexing is done
    on an rvalue to avoid lifetime issues.  Signed indices are interpreted using
    Python-style wraparound, meaning negative indices count backwards from the end of
    their respective dimension.  This method will fail to compile if any of the
    provided indices are out of range, and is always zero-cost at run time. */
    template <index_type... I, typename Self>
        requires (
            sizeof...(I) <= ndim() &&
            impl::valid_array_index<I...>::template value<N, Ns...>
        )
    [[nodiscard]] constexpr decltype(auto) get(this Self&& self) noexcept {
        constexpr size_type j = impl::valid_array_index<I...>::template index<N, Ns...>;
        if constexpr (sizeof...(I) == ndim()) {
            return (std::forward<Self>(self).ptr[j].value);
        } else if constexpr (meta::lvalue<Self>) {
            if constexpr (meta::is_const<Self>) {
                using const_view = impl::subarray<const storage, sizeof...(I), N, Ns...>::view;
                return const_view{static_cast<const storage*>(self.ptr) + j};
            } else {
                using view = impl::subarray<storage, sizeof...(I), N, Ns...>::view;
                return view{self.ptr + j};
            }
        } else {
            using array = impl::subarray<T, sizeof...(I), N, Ns...>::array;
            auto result = array::reserve();
            for (size_type k = 0; k < array::total(); ++k) {
                std::construct_at(
                    &result.ptr[k].value,
                    std::move(self.ptr[j + k].value)
                );
            }
            return result;
        }
    }

    /* Access an element of the array by its multidimensional index.  The number
    of indices must be less than or equal to the number of dimensions in the array.
    If fewer indices are provided, then a view over the corresponding sub-array will
    be returned instead, which will be promoted to a full array if the indexing is done
    on an rvalue to avoid lifetime issues.  Signed indices are interpreted using
    Python-style wraparound, meaning negative indices count backwards from the end of
    their respective dimension.  In debug builds, all indices will be bounds-checked,
    throwing an `IndexError` if any are out of range after normalization.  For unsigned
    indices in release builds, this operator is always zero-cost at run time. */
    template <typename Self, typename... I> requires (sizeof...(I) <= ndim())
    [[nodiscard]] constexpr decltype(auto) operator[](this Self&& self, const I&... i)
        noexcept (!DEBUG && ((meta::unsigned_integer<I> ?
            meta::nothrow::explicitly_convertible_to<I, size_t> :
            meta::nothrow::explicitly_convertible_to<I, ssize_t>
        ) && ...))
        requires ((meta::unsigned_integer<I> ?
            meta::explicitly_convertible_to<I, size_t> :
            meta::explicitly_convertible_to<I, ssize_t>
        ) && ...)
    {
        size_type j = impl::array_index<N, Ns...>(i...);
        if constexpr (sizeof...(I) == ndim()) {
            return (std::forward<Self>(self).ptr[j].value);
        } else if constexpr (meta::lvalue<Self>) {
            if constexpr (meta::is_const<Self>) {
                using const_view = impl::subarray<const storage, sizeof...(I), N, Ns...>::view;
                return const_view{static_cast<const storage*>(self.ptr) + j};
            } else {
                using view = impl::subarray<storage, sizeof...(I), N, Ns...>::view;
                return view{self.ptr + j};
            }
        } else {
            using array = impl::subarray<T, sizeof...(I), N, Ns...>::array;
            auto result = array::reserve();
            for (size_type k = 0; k < array::total(); ++k) {
                std::construct_at(
                    &result.ptr[k].value,
                    std::move(self.ptr[j + k].value)
                );
            }
            return result;
        }
    }

    /* Compare two arrays of the same shape for lexicographic equality.  Iteration
    always begins at index 0 across all dimensions and advances the last index first,
    following row-major order. */
    template <typename U>
    [[nodiscard]] constexpr bool operator==(const Array<U, N, Ns...>& other) const
        noexcept (
            meta::nothrow::has_eq<
                const_reference,
                typename Array<U, N, Ns...>::const_reference
            > &&
            meta::nothrow::truthy<meta::eq_type<
                const_reference,
                typename Array<U, N, Ns...>::const_reference
            >>
        )
        requires (
            meta::has_eq<
                const_reference,
                typename Array<U, N, Ns...>::const_reference
            > &&
            meta::truthy<meta::eq_type<
                const_reference,
                typename Array<U, N, Ns...>::const_reference
            >>
        )
    {
        for (size_type i = 0; i < total(); ++i) {
            if (!bool(impl::array_access(ptr, i) == impl::array_access(other.ptr, i))) {
                return false;
            }
        }
        return true;
    }

    /* Compare two arrays of the same shape for lexicographic equality.  Iteration
    always begins at index 0 across all dimensions and advances the last index first,
    following row-major order. */
    template <typename U>
    [[nodiscard]] constexpr bool operator==(const ArrayView<U, N, Ns...>& other) const
        noexcept (
            meta::nothrow::has_eq<
                const_reference,
                typename ArrayView<U, N, Ns...>::const_reference
            > &&
            meta::nothrow::truthy<meta::eq_type<
                const_reference,
                typename ArrayView<U, N, Ns...>::const_reference
            >>
        )
        requires (
            meta::has_eq<
                const_reference,
                typename ArrayView<U, N, Ns...>::const_reference
            > &&
            meta::truthy<meta::eq_type<
                const_reference,
                typename ArrayView<U, N, Ns...>::const_reference
            >>
        )
    {
        for (size_type i = 0; i < total(); ++i) {
            if (!bool(impl::array_access(ptr, i) == impl::array_access(other.ptr, i))) {
                return false;
            }
        }
        return true;
    }

    /* Compare two arrays of the same shape for lexicographic equality.  Iteration
    always begins at index 0 across all dimensions and advances the last index first,
    following row-major order. */
    template <typename U>
    [[nodiscard]] constexpr auto operator<=>(const Array<U, N, Ns...>& other) const
        noexcept (
            meta::nothrow::has_lt<
                const_reference,
                typename Array<U, N, Ns...>::const_reference
            > &&
            meta::nothrow::has_lt<
                typename Array<U, N, Ns...>::const_reference,
                const_reference
            > &&
            meta::nothrow::truthy<meta::lt_type<
                const_reference,
                typename Array<U, N, Ns...>::const_reference
            >> &&
            meta::nothrow::truthy<meta::lt_type<
                typename Array<U, N, Ns...>::const_reference,
                const_reference
            >>
        )
        requires (
            meta::has_lt<
                const_reference,
                typename Array<U, N, Ns...>::const_reference
            > &&
            meta::has_lt<
                typename Array<U, N, Ns...>::const_reference,
                const_reference
            > &&
            meta::truthy<meta::lt_type<
                const_reference,
                typename Array<U, N, Ns...>::const_reference
            >> &&
            meta::truthy<meta::lt_type<
                typename Array<U, N, Ns...>::const_reference,
                const_reference
            >>
        )
    {
        for (size_t i = 0; i < total(); ++i) {
            if (impl::array_access(ptr, i) < impl::array_access(other.ptr, i)) {
                return std::strong_ordering::less;
            } else if (impl::array_access(other.ptr, i) < impl::array_access(ptr, i)) {
                return std::strong_ordering::greater;
            }
        }
        return std::strong_ordering::equal;
    }

    /* Compare two arrays of the same shape for lexicographic equality.  Iteration
    always begins at index 0 across all dimensions and advances the last index first,
    following row-major order. */
    template <typename U>
    [[nodiscard]] constexpr auto operator<=>(const ArrayView<U, N, Ns...>& other) const
        noexcept (
            meta::nothrow::has_lt<
                const_reference,
                typename ArrayView<U, N, Ns...>::const_reference
            > &&
            meta::nothrow::has_lt<
                typename ArrayView<U, N, Ns...>::const_reference,
                const_reference
            > &&
            meta::nothrow::truthy<meta::lt_type<
                const_reference,
                typename ArrayView<U, N, Ns...>::const_reference
            >> &&
            meta::nothrow::truthy<meta::lt_type<
                typename ArrayView<U, N, Ns...>::const_reference,
                const_reference
            >>
        )
        requires (
            meta::has_lt<
                const_reference,
                typename ArrayView<U, N, Ns...>::const_reference
            > &&
            meta::has_lt<
                typename ArrayView<U, N, Ns...>::const_reference,
                const_reference
            > &&
            meta::truthy<meta::lt_type<
                const_reference,
                typename ArrayView<U, N, Ns...>::const_reference
            >> &&
            meta::truthy<meta::lt_type<
                typename ArrayView<U, N, Ns...>::const_reference,
                const_reference
            >>
        )
    {
        for (size_t i = 0; i < total(); ++i) {
            if (impl::array_access(ptr, i) < impl::array_access(other.ptr, i)) {
                return std::strong_ordering::less;
            } else if (impl::array_access(other.ptr, i) < impl::array_access(ptr, i)) {
                return std::strong_ordering::greater;
            }
        }
        return std::strong_ordering::equal;
    }
};


template <typename... Ts>
    requires (!meta::ArrayView<Ts> && ... && meta::has_common_type<Ts...>)
Array(Ts...) -> Array<meta::common_type<Ts...>, sizeof...(Ts)>;


template <typename T, size_t N, size_t... Ns>
Array(ArrayView<T, N, Ns...>) -> Array<impl::array_type<T>, {N, Ns...}>;


/// TODO: it might be possible later in `Union` to provide a specialization of
/// `Array<Optional<T>>` where the boolean flags are stored in a packed format
/// alongside the data, rather than requiring padding between each element.  Indexing
/// will return an optional reference where `None` represents a missing value.
/// -> Union can just be included here and those specializations would be implemented
/// inline.
/// -> This would require some way to easily vectorize a union type, such that I can
/// provide a requested size for the data buffer, and produce a struct of arrays
/// representation.


}


_LIBCPP_BEGIN_NAMESPACE_STD


template <bertrand::meta::Array T>
struct tuple_size<T> : integral_constant<size_t, bertrand::meta::unqualify<T>::size()> {};


template <bertrand::meta::ArrayView T>
struct tuple_size<T> : integral_constant<size_t, bertrand::meta::unqualify<T>::size()> {};


template <size_t I, bertrand::meta::Array T> requires (I < bertrand::meta::unqualify<T>::size())
struct tuple_element<I, T> {
    using type = bertrand::meta::remove_rvalue<decltype((std::declval<T>().template get<I>()))>;
};


template <size_t I, bertrand::meta::ArrayView T> requires (I < bertrand::meta::unqualify<T>::size())
struct tuple_element<I, T> {
    using type = bertrand::meta::remove_rvalue<decltype((std::declval<T>().template get<I>()))>;
};


template <ssize_t... Is, bertrand::meta::Array T>
constexpr decltype(auto) get(T&& self, index_sequence<Is...>)
    noexcept (requires{{std::forward<T>(self).template get<Is...>()} noexcept;})
    requires (requires{{std::forward<T>(self).template get<Is...>()};})
{
    return (std::forward<T>(self).template get<Is...>());
}


template <ssize_t... Is, bertrand::meta::ArrayView T>
constexpr decltype(auto) get(T&& self, index_sequence<Is...>)
    noexcept (requires{{std::forward<T>(self).template get<Is...>()} noexcept;})
    requires (requires{{std::forward<T>(self).template get<Is...>()};})
{
    return (std::forward<T>(self).template get<Is...>());
}


_LIBCPP_END_NAMESPACE_STD


// namespace bertrand {


//     // static constexpr std::array<std::array<int, 2>, 2> test {
//     //     {1, 2},
//     //     {3, 4}
//     // };


//     static constexpr Array test1 {1, 2};

//     static constexpr Array test2 = Array<int, 2, 2>::reserve();

//     static constexpr auto test3 = Array<int, 2, 3>{
//         Array{0, 1, 2},
//         Array{3, 4, 5}
//     }.reshape<3, 2>();
//     static_assert(test3[0, 0] == 0);
//     static_assert(test3[0, 1] == 1);
//     static_assert(test3[1, 0] == 2);
//     static_assert(test3[1, 1] == 3);
//     static_assert(test3[2, 0] == 4);
//     static_assert(test3[2, 1] == 5);

//     static constexpr auto test4 = Array<int, 2, 3>{
//         Array{0, 1, 2},
//         Array{3, 4, 5}
//     }[-1];
//     static_assert(test4[0] == 3);
//     static_assert(test4[1] == 4);
//     static_assert(test4[2] == 5);

//     static constexpr auto test5 = meta::to_const(Array<int, 2, 3>{
//         Array{0, 1, 2},
//         Array{3, 4, 5}
//     }).flatten();


//     static constexpr Array test6 = test3.flatten();
//     static constexpr ArrayView test7 = test3;


//     static constexpr auto test8 = Array<int, 1, 3>{Array{1, 2, 3}};
//     static constexpr auto test9 = test8.squeeze();
//     static constexpr auto test10 = Array<int, 1, 3>{Array{1, 2, 3}}.squeeze();

//     static_assert([] {
//         Array<int, 2, 2> arr {
//             Array{1, 2},
//             Array{3, 4}
//         };
//         if (arr[0, 0] != 1) return false;
//         if (arr[0, 1] != 2) return false;
//         if (arr[1, 0] != 3) return false;
//         if (arr[1, -1] != 4) return false;

//         auto arr2 = Array<int, 2, 2>{Array{1, 2}, Array{3, 3}};
//         arr = arr2;
//         if (arr[1, 1] != 3) return false;

//         if (arr.shape()[-1] != 2) return false;

//         auto x = arr.data();
//         if (*x != 1) return false;
//         ++x;

//         return true;
//     }());


//     static_assert([] {
//         Array<int, 3, 2> arr {Array{1, 2}, Array{3, 4}, Array{5, 6}};
//         auto x = arr[0];
//         if (x[0] != 1) return false;
//         if (x[1] != 2) return false;
//         for (auto&& i : arr) {
//             // if (i < 1 || i > 6) {
//             //     return false;
//             // }
//             for (auto& j : i) {
//                 if (j < 1 || j > 6) {
//                     return false;
//                 }
//             }
//         }
//         return true;
//     }());

//     static_assert([] {
//         Array<int, 3, 2> arr {Array{1, 2}, Array{3, 4}, Array{5, 6}};
//         auto it = arr.rbegin();
//         if ((*it) != Array{5, 6}) return false;
//         ++it;
//         if (*it != Array{3, 4}) return false;
//         ++it;
//         if (*it != Array{1, 2}) return false;
//         ++it;
//         if (it != arr.rend()) return false;

//         it = arr.rbegin();
//         auto end = arr.rend();
//         while (it != end) {

//             ++it;
//         }

//         return true;
//     }());

//     static_assert([] {
//         Array<int, 2, 3> arr {
//             Array{1, 2, 3},
//             Array{4, 5, 6}
//         };
//         const auto view = arr[0];
//         auto& x = view[1];
//         for (auto& y : view) {

//         }
//         auto z = view.data();

//         auto f = view.back();

//         return true;
//     }());


//     inline void test() {
//         int x = 1;
//         int y = 2;
//         int z = 3;
//         int w = 4;
//         Array<int, 2, 2> arr {
//             Array{x, y},
//             Array{z, w}
//         };
//         auto p = arr.data();
//     }

// }


#endif  // BERTRAND_ARRAY_H
