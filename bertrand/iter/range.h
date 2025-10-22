#ifndef BERTRAND_ITER_RANGE_H
#define BERTRAND_ITER_RANGE_H

#include "bertrand/common.h"
#include "bertrand/except.h"
#include "bertrand/math.h"
#include "bertrand/union.h"


namespace bertrand {


namespace impl {
    struct extent_tag {};
    struct range_tag {};
    struct empty_range_tag {};
    struct scalar_tag {};
    struct tuple_range_tag {};
    struct iota_tag {};
    struct subrange_tag {};
    struct sequence_tag {};

    template <typename T>
    constexpr bool range_transparent = false;
    template <meta::inherits<scalar_tag> T>
    constexpr bool range_transparent<T> = true;
    template <meta::inherits<tuple_range_tag> T>
    constexpr bool range_transparent<T> = true;
    template <meta::inherits<sequence_tag> T>
    constexpr bool range_transparent<T> = true;

    /* An array of integers describing the length of each dimension in a regular,
    possibly multi-dimensional container.

    For most purposes, this class behaves just like a `std::array<const size_t, N>`,
    except that it is CTAD-convertible from scalar integers, braced initializer lists,
    or any tuple-like type that yields integers, as well as providing Python-style
    wraparound for negative indices, and additional utility methods that can be used to
    implement a strided iteration protocol.

    Notably, the product of all dimensions must be equal to the total number of
    (flattened) elements in the tensor.  This generally precludes zero-length
    dimensions, which can cause information loss in the product.  As a result, setting
    a dimension to zero has special meaning depending on context.  If the `extent` is
    provided as a template parameter used to specialize a supported container type,
    then setting a dimension to zero indicates that it is dynamic, allowing the zero to
    be replaced with its actual length at runtime.

    In all other cases, zero-length dimensions will be represented normally, and it is
    up to the user to handle them appropriately.  Additionally, the number of
    dimensions may itself be `0`, which indicates a zero-dimensional tensor (i.e. a
    scalar). */
    template <size_t N>
    struct extent : extent_tag {
        using size_type = size_t;
        using index_type = ssize_t;
        using value_type = size_type;
        using reference = const size_type&;
        using pointer = const size_type*;
        using iterator = const size_t*;
        using reverse_iterator = std::reverse_iterator<iterator>;

        [[nodiscard]] static constexpr size_type size() noexcept { return N; }
        [[nodiscard]] static constexpr index_type ssize() noexcept { return index_type(N); }
        [[nodiscard]] static constexpr bool empty() noexcept { return N == 0; }

    private:
        constexpr void from_int(size_type& i, size_type n) noexcept {
            dim[i] = n;
            ++i;
        }

        template <meta::tuple_like T, size_type... Is>
            requires (sizeof...(Is) == meta::tuple_size<T>)
        constexpr void from_tuple(T&& shape, std::index_sequence<Is...>)
            noexcept (requires(size_type i) {
                {(from_int(i, meta::get<Is>(std::forward<T>(shape))), ...)} noexcept;
            })
        {
            size_type i = 0;
            (from_int(i, meta::get<Is>(std::forward<T>(shape))), ...);
        }

    public:
        size_type dim[size()] {};

        [[nodiscard]] constexpr extent() noexcept = default;

        template <typename... T> requires (sizeof...(T) > 0 && sizeof...(T) == size())
        [[nodiscard]] constexpr extent(T&&... n)
            noexcept ((meta::nothrow::convertible_to<T, size_type> && ...))
            requires ((meta::convertible_to<T, size_type> && ...))
        {
            size_type i = 0;
            (from_int(i, std::forward<T>(n)), ...);
        }

        template <typename T>
        [[nodiscard]] constexpr extent(T&& n)
            noexcept (meta::nothrow::yields<T, size_type>)
            requires (
                !meta::convertible_to<T, size_type> &&
                !meta::convertible_to<T, NoneType> &&
                meta::tuple_like<T> &&
                meta::tuple_size<T> == size() &&
                meta::yields<T, size_type>
            )
        {
            size_type i = 0;
            auto it = meta::begin(n);
            auto end = meta::end(n);
            while (i < size() && it != end) {
                dim[i] = *it;
                ++i;
                ++it;
            }
        }

        template <typename T>
        [[nodiscard]] constexpr extent(T&& n)
            noexcept (requires{
                {from_tuple(std::forward<T>(n), std::make_index_sequence<size()>{})} noexcept;
            })
            requires (
                !meta::convertible_to<T, size_type> &&
                !meta::convertible_to<T, NoneType> &&
                meta::tuple_like<T> &&
                meta::tuple_size<T> == size() &&
                !meta::yields<T, size_type> &&
                meta::tuple_types<T>::template convertible_to<size_type>
            )
        {
            from_tuple(std::forward<T>(n), std::make_index_sequence<size()>{});
        }

        constexpr void swap(extent& other) noexcept {
            for (size_type i = 0; i < size(); ++i) {
                std::swap(dim[i], other.dim[i]);
            }
        }

        [[nodiscard]] constexpr pointer data() const noexcept { return static_cast<pointer>(dim); }
        [[nodiscard]] constexpr iterator begin() const noexcept { return data(); }
        [[nodiscard]] constexpr iterator cbegin() const noexcept { return begin(); }
        [[nodiscard]] constexpr iterator end() const noexcept { return data() + size(); }
        [[nodiscard]] constexpr iterator cend() const noexcept { return end(); }
        [[nodiscard]] constexpr reverse_iterator rbegin() const noexcept {
            return std::make_reverse_iterator(end());
        }
        [[nodiscard]] constexpr reverse_iterator crbegin() const noexcept { return rbegin(); }
        [[nodiscard]] constexpr reverse_iterator rend() const noexcept {
            return std::make_reverse_iterator(begin());
        }
        [[nodiscard]] constexpr reverse_iterator crend() const noexcept { return rend(); }

        template <index_type I> requires (impl::valid_index<ssize(), I>)
        [[nodiscard]] constexpr reference get() const noexcept {
            return dim[impl::normalize_index<ssize(), I>()];
        }

        [[nodiscard]] constexpr reference operator[](index_type i) const {
            return dim[impl::normalize_index(ssize(), i)];
        }

        template <size_type R>
        [[nodiscard]] constexpr bool operator==(const extent<R>& other) const noexcept {
            if constexpr (R == size()) {
                for (size_type i = 0; i < size(); ++i) {
                    if (dim[i] != other.dim[i]) {
                        return false;
                    }
                }
                return true;
            } else {
                return false;
            }
        }

        template <size_type R>
        [[nodiscard]] constexpr std::strong_ordering operator<=>(
            const extent<R>& other
        ) const noexcept {
            size_type min = size() < R ? size() : R;
            for (size_type i = 0; i < min; ++i) {
                if (auto cmp = dim[i] <=> other.dim[i]; cmp != 0) {
                    return cmp;
                }
            }
            if (size() < R) {
                return std::strong_ordering::less;
            } else if (size() > R) {
                return std::strong_ordering::greater;
            }
            return std::strong_ordering::equal;
        }

        [[nodiscard]] constexpr size_type product() const noexcept {
            size_type p = 1;
            for (size_type j = 0; j < size(); ++j) {
                p *= dim[j];
            }
            return p;
        }

        [[nodiscard]] constexpr extent reverse() const noexcept {
            extent r;
            for (size_type j = 0; j < size(); ++j) {
                r.dim[j] = dim[size() - 1 - j];
            }
            return r;
        }

        template <size_type M>
        [[nodiscard]] constexpr auto reduce() const noexcept {
            if constexpr (M >= size()) {
                return extent<0>{};
            } else {
                extent<size() - M> s;
                for (size_type j = M; j < size(); ++j) {
                    s.dim[j - M] = dim[j];
                }
                return s;
            }
        }

        [[nodiscard]] constexpr extent strides(bool column_major) const noexcept {
            if constexpr (size() == 0) {
                return {};
            } else {
                extent s;
                if (column_major) {
                    s.dim[0] = 1;
                    for (size_type j = 1; j < size(); ++j) {
                        s.dim[j] = s.dim[j - 1] * dim[j - 1];
                    }
                } else {
                    size_type j = size() - 1;
                    s.dim[j] = 1;
                    while (j-- > 0) {
                        s.dim[j] = s.dim[j + 1] * dim[j + 1];
                    }
                }
                return s;
            }
        }
    };

    template <meta::convertible_to<size_t>... N>
    extent(N...) -> extent<sizeof...(N)>;

    template <typename T>
        requires (
            !meta::convertible_to<T, size_t> &&
            meta::tuple_like<T> && (
                meta::yields<T, size_t> ||
                meta::tuple_types<T>::template convertible_to<size_t>
            )
        )
    extent(T&&) -> extent<meta::tuple_size<T>>;

    template <size_t N>
    [[nodiscard]] constexpr extent<N + 1> operator|(const extent<N>& lhs, size_t rhs) noexcept {
        extent<N + 1> s;
        for (size_t j = 0; j < N; ++j) {
            s.dim[j] = lhs.dim[j];
        }
        s.dim[N] = rhs;
        return s;
    }

    template <size_t N>
    [[nodiscard]] constexpr extent<N + 1> operator|(size_t lhs, const extent<N>& rhs) noexcept {
        extent<N + 1> s;
        s.dim[0] = lhs;
        for (size_t j = 1; j <= N; ++j) {
            s.dim[j] = rhs.dim[j - 1];
        }
        return s;
    }

}


namespace meta {

    /* Detect whether a type is an `impl::extent` object, which describes the number
    and length of each dimension of a regular, possibly multidimensional, iterable.  If
    `ndim` is provided, then the concept will only match if it is equal to the number
    of dimensions in the extent. */
    template <typename T, bertrand::Optional<size_t> ndim = bertrand::None>
    concept extent = inherits<T, impl::extent_tag> && (
        ndim == bertrand::None || unqualify<T>::size() == *ndim
    );

    /* Detect whether a type is a `range`.  If additional types are provided, then they
    equate to a convertibility check against the range's yield type.  If more than one
    type is provided, then the yield type must be tuple-like, and destructurable to the
    enumerated types.  Note that because ranges always yield other ranges when iterated
    over, the convertibility check will always take range conversion semantics into
    account.  See the `range` class for more details. */
    template <typename T, typename... Rs>
    concept range = inherits<T, impl::range_tag> && (
        sizeof...(Rs) == 0 ||
        (sizeof...(Rs) == 1 && convertible_to<yield_type<T>, first_type<Rs...>>) ||
        structured_with<yield_type<T>, Rs...>
    );

    /* Perfectly forward the argument or retrieve its underlying value if it is a
    range.  This is equivalent to conditionally compiling an extra dereference based on
    the state of the `meta::range<T>` concept, and always returns the same type as
    `meta::remove_range<T>`.  It can be useful in generic algorithms that may accept
    ranges or other containers, but always want to operate on the underlying value.  It
    is used internally to implement various range adaptors. */
    template <typename T>
    [[nodiscard]] constexpr decltype(auto) strip_range(T&& t)
        noexcept (!meta::range<T> || requires{{*::std::forward<T>(t)} noexcept;})
    {
        if constexpr (meta::range<T>) {
            return (*::std::forward<T>(t));
        } else {
            return (::std::forward<T>(t));
        }
    }

    /* Get the type backing a range monad, assuming `T` satisfies `meta::range`.
    Otherwise, forward the original type unchanged.  This can never be another range,
    and is equivalent to the type returned by dereferencing a perfectly-forwarded
    instance of `T`.  The result may be used to specialize the `iter::range<T>`
    template if needed. */
    template <typename T>
    using remove_range = remove_rvalue<decltype((strip_range(::std::declval<T>())))>;

    /* Returns `true` if `T` is a trivial range of just a single element, or `false`
    if it wraps an iterable or tuple-like container.  Iterating over a scalar always
    yields another scalar referencing the same value. */
    template <typename T, typename R = void>
    concept scalar = range<T> && requires(T r) {
        {*r.__value} -> inherits<impl::scalar_tag>;
    } && (is_void<R> || convertible_to<T, R>);

    /* A refinement of `meta::range<T, Rs...>` that only matches iota ranges (i.e.
    those of the form `[start, stop[, step]]`, where `start` is not an iterator).
    Dereferencing the range reveals the inner iota type. */
    template <typename T, typename... Rs>
    concept iota = range<T, Rs...> && requires(T r) {
        {*r.__value} -> inherits<impl::iota_tag>;
    };

    /* A refinement of `meta::range<T, Rs...>` that only matches subranges (i.e.
    those of the form `[start, stop[, step]]`, where `start` is an iterator type).
    Dereferencing the range reveals the inner subrange type. */
    template <typename T, typename... Rs>
    concept subrange = range<T, Rs...> && requires(T r) {
        {*r.__value} -> inherits<impl::subrange_tag>;
    };

    /* A refinement of `meta::range<T, Rs...>` that only matches type-erased sequences,
    where the underlying container type is hidden from the user.  Such ranges cannot be
    dereferenced. */
    template <typename T, typename... Rs>
    concept sequence = range<T, Rs...> && inherits<T, impl::sequence_tag>;

    /* A refinement of `meta::range<T, Rs...>` that specifies that the range's begin
    and end iterators are the same type.  Ranges of this form may be required for
    legacy algorithms, and simplify some iterator access patterns. */
    template <typename T, typename... Rs>
    concept common_range = range<T, Rs...> && ::std::ranges::common_range<T>;

    /* A refinement of `meta::range<T>` that specifies that the range's begin iterator
    satisfies `std::output_iterator`, meaning that its dereference type can be assigned
    to. */
    template <typename T, typename V>
    concept output_range = range<T> && output_iterator<begin_type<T>, V>;

    /* A refinement of `meta::range<T, Rs...>` that specifies that the range's begin
    iterator is equality comparable against itself. */
    template <typename T, typename... Rs>
    concept forward_range = range<T, Rs...> && forward_iterator<begin_type<T>>;

    /* A refinement of `meta::forward_range<T, Rs...>` that specifies that the range's
    begin iterator can be decremented as well as incremented. */
    template <typename T, typename... Rs>
    concept bidirectional_range =
        forward_range<T, Rs...> && bidirectional_iterator<begin_type<T>>;

    /* A refinement of `meta::bidirectional_range<T, Rs...>` that specifies that the
    range's begin iterator can be randomly accessed (i.e. advanced by more than one
    index at a time and supports distance, subscripting, etc.). */
    template <typename T, typename... Rs>
    concept random_access_range =
        bidirectional_range<T, Rs...> && random_access_iterator<begin_type<T>>;

    /* A refinement of `meta::random_access_range<T, Rs...>` that specifies that the
    range's begin iterator is contiguous (i.e. the elements are laid out in a single
    contiguous block of memory). */
    template <typename T, typename... Rs>
    concept contiguous_range =
        random_access_range<T, Rs...> && contiguous_iterator<begin_type<T>>;

    namespace detail {

        template <meta::range T>
        constexpr bool prefer_constructor<T> = true;

        template <meta::range T>
        constexpr bool wraparound<T> = true;

        /// TODO: declare wraparound<T> for each of the underlying containers, so that
        /// `range` never needs to do it more than once.
        /// -> wraparound may not be needed if ranges just forward to the container's
        /// indexing operator.

        namespace adl {

            template <typename T>
            concept has_static_shape = requires{
                {shape<T>()};
                {impl::extent(shape<T>())};
            };

            template <typename T>
            concept has_shape = requires(T t) {
                {shape(::std::forward<T>(t))};
                {impl::extent(shape(::std::forward<T>(t)))};
            };

        }

        namespace member {

            template <typename T>
            concept has_static_shape = requires{
                {unqualify<T>::shape()};
                {impl::extent(unqualify<T>::shape())};
            };

            template <typename T>
            concept has_shape = requires(T t) {
                {::std::forward<T>(t).shape()};
                {impl::extent(::std::forward<T>(t).shape())};
            };

        }

        template <typename T>
        struct static_shape_fn {
            static constexpr auto operator()()
                noexcept (requires{{impl::extent(unqualify<T>::shape())} noexcept;})
                requires (member::has_static_shape<T>)
            {
                return impl::extent(unqualify<T>::shape());
            }

            static constexpr auto operator()()
                noexcept (requires{{impl::extent(shape<T>())} noexcept;})
                requires (!member::has_static_shape<T> && adl::has_static_shape<T>)
            {
                return impl::extent(shape<T>());
            }

            static constexpr auto operator()()
                noexcept (requires{{unqualify<T>::shape()} noexcept;})
                requires (
                    !member::has_static_shape<T> &&
                    !adl::has_static_shape<T> &&
                    member::has_shape<T>
                )
            {
                return meta::unqualify<decltype(impl::extent{::std::declval<T>().shape()})>{};
            }

            static constexpr auto operator()()
                noexcept (requires{{unqualify<T>::shape()} noexcept;})
                requires (
                    !member::has_static_shape<T> &&
                    !adl::has_static_shape<T> &&
                    !member::has_shape<T> &&
                    adl::has_shape<T>
                )
            {
                return meta::unqualify<decltype(impl::extent{shape(::std::declval<T>())})>{};
            }

            static constexpr impl::extent<1> operator()() noexcept
                requires (
                    !member::has_static_shape<T> &&
                    !adl::has_static_shape<T> &&
                    !member::has_shape<T> &&
                    !adl::has_shape<T> &&
                    (meta::tuple_like<T> || meta::iterable<T>)
                )
            {
                if constexpr (meta::tuple_like<T>) {
                    return {meta::tuple_size<T>};
                } else {
                    return {0};
                }
            }
        };

        struct shape_fn {
            template <typename T>
            static constexpr auto operator()(T&& t)
                noexcept (requires{{::std::forward<T>(t).shape()} noexcept;})
                requires (member::has_shape<T> && (
                    decltype(impl::extent(::std::forward<T>(t).shape()))::size() ==
                    decltype(static_shape_fn<T>{}())::size()
                ))
            {
                return impl::extent(::std::forward<T>(t).shape());
            }

            template <typename T>
            static constexpr auto operator()(T&& t)
                noexcept (adl::has_shape<T> ?
                    requires{{shape(::std::forward<T>(t))} noexcept;} :
                    requires{{shape<T>()} noexcept;}
                )
                requires (
                    !member::has_shape<T> &&
                    adl::has_shape<T> && (
                        decltype(impl::extent(shape(::std::forward<T>(t))))::size() ==
                        decltype(static_shape_fn<T>{}())::size()
                    )
                )
            {
                return impl::extent(shape(::std::forward<T>(t)));
            }

            template <typename T>
            static constexpr impl::extent<1> operator()(T&& t)
                noexcept (requires{{shape(::std::forward<T>(t))} noexcept;})
                requires (
                    !member::has_shape<T> &&
                    !adl::has_shape<T> &&
                    (meta::tuple_like<T> || meta::iterable<T>) &&
                    decltype(static_shape_fn<T>{}())::size() == 1
                )
            {
                if constexpr (meta::tuple_like<T>) {
                    return {meta::tuple_size<T>};
                } else {
                    return {size_t(meta::distance(t))};
                }
            }
        };

    }

    /* Retrieve the `shape()` of a generic type `T` where such information is
    independent of any particular instance of `T`.

    This will invoke a static `T::shape()` or `shape<T>()` method if available, or
    produce a dynamic extent of the same rank as `t.shape()` or `shape(t)` filled with
    `None` if either of those are present as instance methods.  If neither are present,
    then it will attempt to deduce a 1D shape for tuple-like or iterable `T`, where the
    single element is equal to the tuple size or `None`, respectively.

    The result from this method can be used to specialize a corresponding class
    template.  Note that shapes containing `None` for one or more dimensions indicate
    dynamic shapes, where the actual size must be determined at runtime.
    
    The result is always returned as an `impl::extent<N>` object, where `N` is equal to
    the number of dimensions in the shape. */
    template <typename T>
    inline constexpr detail::static_shape_fn<T> static_shape;

    /* Retrieve the `shape()` of a generic object `t` of type `T`.

    This will invoke a member `t.shape()` method or an ADL `shape(t)` method if
    available.  Otherwise, it will deduce a 1D shape for tuple-like or iterable `t`,
    where the single element is equal to the tuple size or size of the iterable,
    respectively.  If an iterable does not supply a `size()` or `ssize()` method, then
    the shape may require a full traversal to determine its length.

    The result is always returned as an `impl::extent<N>` object, where `N` is
    equal to the number of dimensions in the shape.  Note that `N` will always be the
    same between the result of this function and `meta::static_shape<T>()`, although
    any `None` values in that result will be replaced by their actual values. */
    inline constexpr detail::shape_fn shape;

    /* Detect whether `meta::static_shape<T>()` is well-formed.  See that method for
    more details. */
    template <typename T>
    concept has_static_shape = requires{{static_shape<T>()};};

    /* Detect whether `meta::shape(t)` is well-formed.  See that method for more
    details. */
    template <typename T>
    concept has_shape = requires(T t) {{shape(t)};};

    /* Get the standardized `impl::extent` type that is returned by
    `meta::static_shape<T>()`, assuming that expression is well-formed. */
    template <has_static_shape T>
    using static_shape_type = decltype(impl::extent(static_shape<T>()));

    /* Get the standardized `impl::extent` type that is returned by `meta::shape(t)`,
    assuming that expression is well-formed. */
    template <has_shape T>
    using shape_type = decltype(impl::extent(shape(::std::declval<T>())));

    /* Detect whether `meta::static_shape<T>()` is well-formed and then check for an
    implicit conversion from the normalized `impl::extent` type to the given return
    type.  See `meta::has_static_shape<T>` and `meta::static_shape_type<T>` for more
    details. */
    template <typename Ret, typename T>
    concept static_shape_returns =
        has_static_shape<T> && convertible_to<static_shape_type<T>, Ret>;

    /* Detect whether `meta::shape(t)` is well-formed and then check for an implicit
    conversion from the normalized `impl::extent` type to the given return type.  See
    `meta::has_static_shape<T>` and `meta::static_shape_type<T>` for more details. */
    template <typename Ret, typename T>
    concept shape_returns = has_shape<T> && convertible_to<shape_type<T>, Ret>;

    namespace nothrow {

        template <typename T>
        concept has_static_shape =
            meta::has_static_shape<T> && requires{{static_shape<T>()} noexcept;};

        template <typename T>
        concept has_shape = meta::has_shape<T> && requires(T t) {{shape(t)} noexcept;};

        template <nothrow::has_static_shape T>
        using static_shape_type = meta::static_shape_type<T>;

        template <nothrow::has_shape T>
        using shape_type = meta::shape_type<T>;

        template <typename Ret, typename T>
        concept static_shape_returns =
            nothrow::has_static_shape<T> &&
            nothrow::convertible_to<nothrow::static_shape_type<T>, Ret>;

        template <typename Ret, typename T>
        concept shape_returns =
            nothrow::has_shape<T> && nothrow::convertible_to<nothrow::shape_type<T>, Ret>;

    }

    namespace detail {

        namespace member {

            template <typename K, typename A>
            concept has_contains = requires(
                meta::remove_range<meta::as_const_ref<K>> key,
                meta::remove_range<meta::as_const_ref<A>> arg
            ) {
                {arg.contains(key)} -> meta::convertible_to<bool>;
            };

            template <typename K, typename A>
            concept nothrow_contains = requires(
                meta::remove_range<meta::as_const_ref<K>> key,
                meta::remove_range<meta::as_const_ref<A>> arg
            ) {
                {arg.contains(key)} noexcept -> nothrow::convertible_to<bool>;
            };

        }

        namespace adl {

            template <typename K, typename A>
            concept has_contains = requires(
                meta::remove_range<meta::as_const_ref<K>> key,
                meta::remove_range<meta::as_const_ref<A>> arg
            ) {
                {contains(arg, key)} -> meta::convertible_to<bool>;
            };

            template <typename K, typename A>
            concept nothrow_contains = requires(
                meta::remove_range<meta::as_const_ref<K>> key,
                meta::remove_range<meta::as_const_ref<A>> arg
            ) {
                {contains(arg, key)} noexcept -> nothrow::convertible_to<bool>;
            };

        }

        template <typename K, typename A>
        constexpr bool invoke_contains(const K& key, const A& a)
            noexcept (member::nothrow_contains<K, A>)
            requires (member::has_contains<K, A>)
        {
            return meta::strip_range(a).contains(meta::strip_range(key));
        }

        template <typename K, typename A>
        constexpr bool invoke_contains(const K& key, const A& a)
            noexcept (adl::nothrow_contains<K, A>)
            requires (!member::has_contains<K, A> && adl::has_contains<K, A>)
        {
            return contains(meta::strip_range(a), meta::strip_range(key));
        }

    }

}


namespace impl {

    template <typename C>
    concept range_concept = meta::not_void<C> && meta::not_rvalue<C> && !meta::range<C>;

    /* A trivial range with zero elements.  This is the default type for the `range`
    class template, allowing it to be default-constructed. */
    struct empty_range : empty_range_tag {
        [[nodiscard]] constexpr empty_range() noexcept = default;
        [[nodiscard]] constexpr empty_range(const empty_range&) noexcept = default;
        [[nodiscard]] constexpr empty_range(empty_range&&) noexcept = default;
        static constexpr void swap(empty_range&) noexcept {}
        [[nodiscard]] static constexpr size_t size() noexcept { return 0; }
        [[nodiscard]] static constexpr ssize_t ssize() noexcept { return 0; }
        [[nodiscard]] static constexpr impl::extent<0> shape() noexcept { return {}; }
        [[nodiscard]] static constexpr bool empty() noexcept { return true; }
        [[nodiscard]] static constexpr auto begin() noexcept {
            return empty_iterator<const NoneType&>{};
        }
        [[nodiscard]] static constexpr auto end() noexcept {
            return empty_iterator<const NoneType&>{};
        }
        [[nodiscard]] static constexpr auto rbegin() noexcept {
            return std::make_reverse_iterator(end());
        }
        [[nodiscard]] static constexpr auto rend() noexcept {
            return std::make_reverse_iterator(begin());
        }
    };

    /* A range over just a single scalar element.  Indexing the range perfectly
    forwards that element, and iterating over it is akin to taking its address.  A
    CTAD guide chooses this type when a single element is passed to the `range()`
    constructor. */
    template <range_concept T>
    struct scalar_range : scalar_tag {
        [[no_unique_address]] Optional<T> __value;

        [[nodiscard]] constexpr scalar_range() = default;

        template <typename... A> requires (sizeof...(A) > 0)
        [[nodiscard]] constexpr scalar_range(A&&... args)
            noexcept (meta::nothrow::constructible_from<T, A...>)
            requires (meta::constructible_from<T, A...>)
        :
            __value{std::forward<A>(args)...}
        {}

        constexpr void swap(scalar_range& other)
            noexcept (requires{{std::ranges::swap(__value, other.__value)} noexcept;})
            requires (requires{{std::ranges::swap(__value, other.__value)};})
        {
            std::ranges::swap(__value, other.__value);
        }

        [[nodiscard]] constexpr size_t size() const noexcept { return __value != None; }
        [[nodiscard]] constexpr ssize_t ssize() const noexcept { return __value != None; }
        [[nodiscard]] constexpr bool empty() const noexcept { return __value == None; }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator*(this Self&& self) noexcept {
            return (*std::forward<Self>(self).__value);
        }

        [[nodiscard]] constexpr auto operator->()
            noexcept (requires{{meta::to_arrow(*__value)} noexcept;})
            requires (requires{{meta::to_arrow(*__value)};})
        {
            return meta::to_arrow(*__value);
        }

        [[nodiscard]] constexpr auto operator->() const
            noexcept (requires{{meta::to_arrow(*__value)} noexcept;})
            requires (requires{{meta::to_arrow(*__value)};})
        {
            return meta::to_arrow(*__value);
        }

        [[nodiscard]] constexpr auto data()
            noexcept (requires{{__value == None ? nullptr : std::addressof(*__value)} noexcept;})
            requires (requires{{__value == None ? nullptr : std::addressof(*__value)};})
        {
            return __value == None ? nullptr : std::addressof(*__value);
        }

        [[nodiscard]] constexpr auto data() const
            noexcept (requires{{__value == None ? nullptr : std::addressof(*__value)} noexcept;})
            requires (requires{{__value == None ? nullptr : std::addressof(*__value)};})
        {
            return __value == None ? nullptr : std::addressof(*__value);
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) front(this Self&& self)
            noexcept (requires{{*std::forward<Self>(self).__value} noexcept;})
            requires (requires{{*std::forward<Self>(self).__value};})
        {
            return (*std::forward<Self>(self).__value);
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) back(this Self&& self)
            noexcept (requires{{*std::forward<Self>(self).__value} noexcept;})
            requires (requires{{*std::forward<Self>(self).__value};})
        {
            return (*std::forward<Self>(self).__value);
        }

        template <size_t I, typename Self> requires (I == 0)
        [[nodiscard]] constexpr decltype(auto) get(this Self&& self)
            noexcept (requires{{meta::get<I>(*std::forward<Self>(self).__value)} noexcept;})
            requires (requires{{meta::get<I>(*std::forward<Self>(self).__value)};})
        {
            return (meta::get<I>(*std::forward<Self>(self).__value));
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator[](this Self&& self, size_t i) noexcept {
            return (*std::forward<Self>(self).__value);
        }

        [[nodiscard]] constexpr auto begin()
            noexcept (requires{{__value.begin()} noexcept;})
            requires (requires{{__value.begin()};})
        {
            return __value.begin();
        }

        [[nodiscard]] constexpr auto begin() const
            noexcept (requires{{__value.begin()} noexcept;})
            requires (requires{{__value.begin()};})
        {
            return __value.begin();
        }

        [[nodiscard]] constexpr auto end()
            noexcept (requires{{__value.end()} noexcept;})
            requires (requires{{__value.end()};})
        {
            return __value.end();
        }

        [[nodiscard]] constexpr auto end() const
            noexcept (requires{{__value.end()} noexcept;})
            requires (requires{{__value.end()};})
        {
            return __value.end();
        }

        [[nodiscard]] constexpr auto rbegin()
            noexcept (requires{{__value.rbegin()} noexcept;})
            requires (requires{{__value.rbegin()};})
        {
            return __value.rbegin();
        }

        [[nodiscard]] constexpr auto rbegin() const
            noexcept (requires{{__value.rbegin()} noexcept;})
            requires (requires{{__value.rbegin()};})
        {
            return __value.rbegin();
        }

        [[nodiscard]] constexpr auto rend()
            noexcept (requires{{__value.rend()} noexcept;})
            requires (requires{{__value.rend()};})
        {
            return __value.rend();
        }

        [[nodiscard]] constexpr auto rend() const
            noexcept (requires{{__value.rend()} noexcept;})
            requires (requires{{__value.rend()};})
        {
            return __value.rend();
        }
    };
    template <typename T>
    scalar_range(T&&) -> scalar_range<meta::remove_rvalue<T>>;

    enum class tuple_kind : uint8_t {
        EMPTY,
        TRIVIAL,
        VTABLE,
    };

    /* A generic iterator over an arbitrary tuple type embedded in a `tuple_range<C>`
    wrapper.  The iterator works by traversing a separate array, which may either
    contain references to the tuple's elements if they all happen to be the same type,
    or a vtable of function pointers that yield each value dynamically. */
    template <meta::lvalue T>
    struct tuple_iterator {
        using iterator_category = std::random_access_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = meta::remove_reference<decltype((std::declval<T>()[0]))>;
        using reference = meta::as_lvalue<value_type>;
        using pointer = meta::address_type<reference>;

        meta::as_pointer<T> dispatch;
        difference_type index;

        [[nodiscard]] constexpr decltype(auto) operator*() const
            noexcept (requires{{(*dispatch)[size_t(index)]} noexcept;})
        {
            return ((*dispatch)[size_t(index)]);
        }

        [[nodiscard]] constexpr auto operator->() const
            noexcept (requires{{impl::arrow(**this)} noexcept;})
        {
            return impl::arrow(**this);
        }

        [[nodiscard]] constexpr decltype(auto) operator[](difference_type n) const
            noexcept (requires{{(*dispatch)[size_t(index + n)]} noexcept;})
        {
            return ((*dispatch)[size_t(index + n)]);
        }

        constexpr tuple_iterator& operator++() noexcept {
            ++index;
            return *this;
        }

        [[nodiscard]] constexpr tuple_iterator operator++(int) noexcept {
            tuple_iterator tmp = *this;
            ++index;
            return tmp;
        }

        [[nodiscard]] friend constexpr tuple_iterator operator+(
            const tuple_iterator& self,
            difference_type n
        ) noexcept {
            return {self.dispatch, self.index + n};
        }

        [[nodiscard]] friend constexpr tuple_iterator operator+(
            difference_type n,
            const tuple_iterator& self
        ) noexcept {
            return {self.dispatch, self.index + n};
        }

        constexpr tuple_iterator& operator+=(difference_type n) noexcept {
            index += n;
            return *this;
        }

        constexpr tuple_iterator& operator--() noexcept {
            --index;
            return *this;
        }

        [[nodiscard]] constexpr tuple_iterator operator--(int) noexcept {
            tuple_iterator tmp = *this;
            --index;
            return tmp;
        }

        [[nodiscard]] constexpr tuple_iterator operator-(difference_type n) const noexcept {
            return {dispatch, index - n};
        }

        [[nodiscard]] constexpr difference_type operator-(
            const tuple_iterator& other
        ) const noexcept {
            return index - other.index;
        }

        constexpr tuple_iterator& operator-=(difference_type n) noexcept {
            index -= n;
            return *this;
        }

        [[nodiscard]] constexpr auto operator<=>(const tuple_iterator& other) const noexcept {
            return index <=> other.index;
        }

        [[nodiscard]] constexpr bool operator==(const tuple_iterator& other) const noexcept {
            return index == other.index;
        }
    };

    /* A unique vtable has to be emitted for each observed qualification of the tuple
    type, in order to perfectly forward the results. */
    template <meta::tuple_like C>
    struct tuple_vtable {
        using type = meta::tuple_types<C>::template eval<meta::make_union>;
        template <size_t I>
        struct fn {
            static constexpr type operator()(meta::forward<C> c)
                noexcept (requires{
                    {meta::get<I>(c)} noexcept -> meta::nothrow::convertible_to<type>;
                })
            {
                return meta::get<I>(c);
            }
        };
        using dispatch = impl::basic_vtable<fn, meta::tuple_size<C>>;
    };

    template <typename>
    struct _tuple_range : tuple_range_tag {
        using type = const NoneType&;
        static constexpr tuple_kind kind = tuple_kind::EMPTY;
    };
    template <typename T, typename... Ts>
    struct _tuple_range<meta::pack<T, Ts...>> : tuple_range_tag {
        using type = meta::make_union<T, Ts...>;
        static constexpr tuple_kind kind =
            meta::trivial_union<T, Ts...> ? tuple_kind::TRIVIAL : tuple_kind::VTABLE;
    };

    /* A wrapper around a generic tuple type that allows it to be indexed and iterated
    over at runtime, by dispatching to a reference array or vtable.  If the tuple
    consists of multiple types, then the subscript and yield types will be promoted to
    unions of all the possible results. */
    template <meta::tuple_like C>
    struct tuple_range : _tuple_range<meta::tuple_types<C>> {
        [[no_unique_address]] impl::ref<C> __value;

        template <typename... A>
        [[nodiscard]] tuple_range(A&&... args)
            noexcept (requires{{C(std::forward<A>(args)...)} noexcept;})
            requires (requires{{C(std::forward<A>(args)...)};})
        :
            __value{C(std::forward<A>(args)...)}
        {}

        constexpr void swap(tuple_range& other)
            noexcept (requires{{std::ranges::swap(__value, other.__value)} noexcept;})
            requires (requires{{std::ranges::swap(__value, other.__value)};})
        {
            std::ranges::swap(__value, other.__value);
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator*(this Self&& self) noexcept {
            return (*std::forward<Self>(self).__value);
        }

        [[nodiscard]] constexpr auto operator->()
            noexcept (requires{{meta::to_arrow(*__value)} noexcept;})
            requires (requires{{meta::to_arrow(*__value)};})
        {
            return meta::to_arrow(*__value);
        }

        [[nodiscard]] constexpr auto operator->() const
            noexcept (requires{{meta::to_arrow(*__value)} noexcept;})
            requires (requires{{meta::to_arrow(*__value)};})
        {
            return meta::to_arrow(*__value);
        }

        [[nodiscard]] static constexpr size_t size() noexcept { return 0; }
        [[nodiscard]] static constexpr ssize_t ssize() noexcept { return 0; }
        [[nodiscard]] static constexpr bool empty() noexcept { return true; }

        [[nodiscard]] static constexpr auto begin() noexcept {
            return empty_iterator<const NoneType&>{};
        }

        [[nodiscard]] static constexpr auto end() noexcept {
            return empty_iterator<const NoneType&>{};
        }

        [[nodiscard]] static constexpr auto rbegin() noexcept {
            return std::make_reverse_iterator(end());
        }

        [[nodiscard]] static constexpr auto rend() noexcept {
            return std::make_reverse_iterator(begin());
        }
    };
    template <meta::tuple_like C>
        requires (_tuple_range<meta::tuple_types<C>>::kind == tuple_kind::TRIVIAL)
    struct tuple_range<C> : _tuple_range<meta::tuple_types<C>> {
        [[no_unique_address]] impl::ref<C> __value;

        [[nodiscard]] static constexpr size_t size() noexcept { return meta::tuple_size<C>; }
        [[nodiscard]] static constexpr ssize_t ssize() noexcept { return ssize_t(size()); }
        [[nodiscard]] static constexpr bool empty() noexcept { return false; }

    private:
        using base = _tuple_range<meta::tuple_types<C>>;
        using ref = impl::ref<typename base::type>;
        using array = std::array<ref, size()>;

        array elements {};

        template <size_t... Is>
        constexpr array init(std::index_sequence<Is...>)
            noexcept ((requires{
                {meta::get<Is>(*__value)} noexcept -> meta::nothrow::convertible_to<ref>;
            } && ...))
        {
            return {meta::get<Is>(*__value)...};
        }

    public:
        [[nodiscard]] constexpr tuple_range() = default;

        template <typename... A> requires (sizeof...(A) > 0)
        [[nodiscard]] constexpr tuple_range(A&&... args)
            noexcept (requires{{impl::ref<C>{{std::forward<A>(args)...}}} noexcept;})
            requires (requires{{impl::ref<C>{{std::forward<A>(args)...}}};})
        :
            __value{{std::forward<A>(args)...}},
            elements(init(std::make_index_sequence<size()>{}))
        {}

        constexpr void swap(tuple_range& other)
            noexcept (requires{
                {std::ranges::swap(__value, other.__value)} noexcept;
                {std::ranges::swap(elements, other.elements)} noexcept;
            })
            requires (requires{
                {std::ranges::swap(__value, other.__value)};
                {std::ranges::swap(elements, other.elements)};
            })
        {
            std::ranges::swap(__value, other.__value);
            std::ranges::swap(elements, other.elements);
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator*(this Self&& self) noexcept {
            return (*std::forward<Self>(self).__value);
        }

        [[nodiscard]] constexpr auto operator->()
            noexcept (requires{{std::addressof(*__value)} noexcept;})
            requires (requires{{std::addressof(*__value)};})
        {
            return std::addressof(*__value);
        }

        [[nodiscard]] constexpr auto operator->() const
            noexcept (requires{{std::addressof(*__value)} noexcept;})
            requires (requires{{std::addressof(*__value)};})
        {
            return std::addressof(*__value);
        }

        template <size_t I, typename Self>
        [[nodiscard]] constexpr decltype(auto) get(this Self&& self)
            noexcept (requires{{meta::get<I>(*std::forward<Self>(self).__value)} noexcept;})
            requires (requires{{meta::get<I>(*std::forward<Self>(self).__value)};})
        {
            return (meta::get<I>(*std::forward<Self>(self).__value));
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator[](this Self&& self, size_t i) noexcept {
            if constexpr (meta::lvalue<Self>) {
                return (*std::forward<Self>(self).elements[i]);
            } else {
                return (*std::move(std::forward<Self>(self).elements[i]));
            }
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) front(this Self&& self) noexcept {
            if constexpr (meta::lvalue<Self>) {
                return (*std::forward<Self>(self).elements[0]);
            } else {
                return (*std::move(std::forward<Self>(self).elements[0]));
            }
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) back(this Self&& self) noexcept {
            if constexpr (meta::lvalue<Self>) {
                return (*std::forward<Self>(self).elements[size() - 1]);
            } else {
                return (*std::move(std::forward<Self>(self).elements[size() - 1]));
            }
        }

        [[nodiscard]] constexpr auto begin()
            noexcept (requires{{tuple_iterator<tuple_range&>{this, 0}} noexcept;})
            requires (requires{{tuple_iterator<tuple_range&>{this, 0}};})
        {
            return tuple_iterator<tuple_range&>{this, 0};
        }

        [[nodiscard]] constexpr auto begin() const
            noexcept (requires{{tuple_iterator<const tuple_range&>{this, 0}} noexcept;})
            requires (requires{{tuple_iterator<const tuple_range&>{this, 0}};})
        {
            return tuple_iterator<const tuple_range&>{this, 0};
        }

        [[nodiscard]] constexpr auto end()
            noexcept (requires{{tuple_iterator<tuple_range&>{this, ssize()}} noexcept;})
            requires (requires{{tuple_iterator<tuple_range&>{this, ssize()}};})
        {
            return tuple_iterator<tuple_range&>{this, ssize()};
        }

        [[nodiscard]] constexpr auto end() const
            noexcept (requires{{tuple_iterator<const tuple_range&>{this, ssize()}} noexcept;})
            requires (requires{{tuple_iterator<const tuple_range&>{this, ssize()}};})
        {
            return tuple_iterator<const tuple_range&>{this, ssize()};
        }

        [[nodiscard]] constexpr auto rbegin()
            noexcept (requires{{std::make_reverse_iterator(end())} noexcept;})
            requires (requires{{std::make_reverse_iterator(end())};})
        {
            return std::make_reverse_iterator(end());
        }

        [[nodiscard]] constexpr auto rbegin() const
            noexcept (requires{{std::make_reverse_iterator(end())} noexcept;})
            requires (requires{{std::make_reverse_iterator(end())};})
        {
            return std::make_reverse_iterator(end());
        }

        [[nodiscard]] constexpr auto rend()
            noexcept (requires{{std::make_reverse_iterator(begin())} noexcept;})
            requires (requires{{std::make_reverse_iterator(begin())};})
        {
            return std::make_reverse_iterator(begin());
        }

        [[nodiscard]] constexpr auto rend() const
            noexcept (requires{{std::make_reverse_iterator(begin())} noexcept;})
            requires (requires{{std::make_reverse_iterator(begin())};})
        {
            return std::make_reverse_iterator(begin());
        }
    };
    template <meta::tuple_like C>
        requires (_tuple_range<meta::tuple_types<C>>::kind == tuple_kind::VTABLE)
    struct tuple_range<C> : _tuple_range<meta::tuple_types<C>> {
        [[no_unique_address]] impl::ref<C> __value;

        [[nodiscard]] static constexpr size_t size() noexcept { return meta::tuple_size<C>; }
        [[nodiscard]] static constexpr ssize_t ssize() noexcept { return ssize_t(size()); }
        [[nodiscard]] static constexpr bool empty() noexcept { return false; }

    private:
        using base = _tuple_range<meta::tuple_types<C>>;

        template <typename Self>
        using dispatch = tuple_vtable<decltype((*std::declval<Self>().__value))>::dispatch;

    public:
        [[nodiscard]] constexpr tuple_range() = default;

        template <typename... A> requires (sizeof...(A) > 0)
        [[nodiscard]] constexpr tuple_range(A&&... args)
            noexcept (requires{{impl::ref<C>{{std::forward<A>(args)...}}} noexcept;})
            requires (requires{{impl::ref<C>{{std::forward<A>(args)...}}};})
        :
            __value{{std::forward<A>(args)...}}
        {}

        constexpr void swap(tuple_range& other)
            noexcept (requires{{std::ranges::swap(__value, other.__value)} noexcept;})
            requires (requires{{std::ranges::swap(__value, other.__value)};})
        {
            std::ranges::swap(__value, other.__value);
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator*(this Self&& self) noexcept {
            return (*std::forward<Self>(self).__value);
        }

        [[nodiscard]] constexpr auto operator->()
            noexcept (requires{{meta::to_arrow(*__value)} noexcept;})
            requires (requires{{meta::to_arrow(*__value)};})
        {
            return meta::to_arrow(*__value);
        }

        [[nodiscard]] constexpr auto operator->() const
            noexcept (requires{{meta::to_arrow(*__value)} noexcept;})
            requires (requires{{meta::to_arrow(*__value)};})
        {
            return meta::to_arrow(*__value);
        }

        template <size_t I, typename Self>
        [[nodiscard]] constexpr decltype(auto) get(this Self&& self)
            noexcept (requires{{meta::get<I>(*std::forward<Self>(self).__value)} noexcept;})
            requires (requires{{meta::get<I>(*std::forward<Self>(self).__value)};})
        {
            return (meta::get<I>(*std::forward<Self>(self).__value));
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator[](this Self&& self, size_t i)
            noexcept (requires{{dispatch<Self>{i}(*std::forward<Self>(self).__value)} noexcept;})
            requires (requires{{dispatch<Self>{i}(*std::forward<Self>(self).__value)};})
        {
            return (dispatch<Self>{i}(*std::forward<Self>(self).__value));
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) front(this Self&& self)
            noexcept (requires{{dispatch<Self>{0}(*std::forward<Self>(self).__value)} noexcept;})
            requires (requires{{dispatch<Self>{0}(*std::forward<Self>(self).__value)};})
        {
            return (dispatch<Self>{0}(*std::forward<Self>(self).__value));
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) back(this Self&& self)
            noexcept (requires{
                {dispatch<Self>{size() - 1}(*std::forward<Self>(self).__value)} noexcept;
            })
            requires (requires{{dispatch<Self>{size() - 1}(*std::forward<Self>(self).__value)};})
        {
            return (dispatch<Self>{size() - 1}(*std::forward<Self>(self).__value));
        }

        [[nodiscard]] constexpr auto begin()
            noexcept (requires{{tuple_iterator<tuple_range&>{this, 0}} noexcept;})
            requires (requires{{tuple_iterator<tuple_range&>{this, 0}};})
        {
            return tuple_iterator<tuple_range&>{this, 0};
        }

        [[nodiscard]] constexpr auto begin() const
            noexcept (requires{{tuple_iterator<const tuple_range&>{this, 0}} noexcept;})
            requires (requires{{tuple_iterator<const tuple_range&>{this, 0}};})
        {
            return tuple_iterator<const tuple_range&>{this, 0};
        }

        [[nodiscard]] constexpr auto end()
            noexcept (requires{{tuple_iterator<tuple_range&>{this, ssize()}} noexcept;})
            requires (requires{{tuple_iterator<tuple_range&>{this, ssize()}};})
        {
            return tuple_iterator<tuple_range&>{this, ssize()};
        }

        [[nodiscard]] constexpr auto end() const
            noexcept (requires{{tuple_iterator<const tuple_range&>{this, ssize()}} noexcept;})
            requires (requires{{tuple_iterator<const tuple_range&>{this, ssize()}};})
        {
            return tuple_iterator<const tuple_range&>{this, ssize()};
        }

        [[nodiscard]] constexpr auto rbegin()
            noexcept (requires{{std::make_reverse_iterator(end())} noexcept;})
            requires (requires{{std::make_reverse_iterator(end())};})
        {
            return std::make_reverse_iterator(end());
        }

        [[nodiscard]] constexpr auto rbegin() const
            noexcept (requires{{std::make_reverse_iterator(end())} noexcept;})
            requires (requires{{std::make_reverse_iterator(end())};})
        {
            return std::make_reverse_iterator(end());
        }

        [[nodiscard]] constexpr auto rend()
            noexcept (requires{{std::make_reverse_iterator(begin())} noexcept;})
            requires (requires{{std::make_reverse_iterator(begin())};})
        {
            return std::make_reverse_iterator(begin());
        }

        [[nodiscard]] constexpr auto rend() const
            noexcept (requires{{std::make_reverse_iterator(begin())} noexcept;})
            requires (requires{{std::make_reverse_iterator(begin())};})
        {
            return std::make_reverse_iterator(begin());
        }
    };

    template <typename T>
    tuple_range(T&&) -> tuple_range<meta::remove_rvalue<T>>;

    template <typename T>
    concept strictly_positive = meta::unsigned_integer<T> || !requires(meta::as_const_ref<T> t) {
        {t < 0} -> meta::truthy;
    };

    /* Iota iterators will use the difference type between `stop` and `start` if
    available and integer-like.  Otherwise, if `start` or `stop` has an integer
    difference with respect to itself, then we use that type.  Lastly, we default to
    `std::ptrdiff_t` as a fallback. */
    template <typename Start, typename Stop>
    struct _iota_difference { using type = std::ptrdiff_t; };
    template <typename Start, typename Stop>
        requires (requires(meta::as_const_ref<Start> start, meta::as_const_ref<Stop> stop) {
            {stop - start} -> meta::signed_integer;
        })
    struct _iota_difference<Start, Stop> {
        using type = meta::unqualify<decltype(
            std::declval<meta::as_const_ref<Stop>>() -
            std::declval<meta::as_const_ref<Start>>()
        )>;
    };
    template <typename Start, meta::None Stop>
        requires (!requires(meta::as_const_ref<Start> start, meta::as_const_ref<Stop> stop) {
            {stop - start};
        } && requires(meta::as_const_ref<Start> start) {
            {start - start} -> meta::signed_integer;
        })
    struct _iota_difference<Start, Stop> {
        using type = meta::unqualify<decltype(
            std::declval<meta::as_const_ref<Start>>() -
            std::declval<meta::as_const_ref<Start>>()
        )>;
    };
    template <typename Start, meta::None Stop>
        requires (!requires(meta::as_const_ref<Start> start, meta::as_const_ref<Stop> stop) {
            {stop - start};
            {start - start} -> meta::signed_integer;
        } && requires(meta::as_const_ref<Stop> stop) {
            {stop - stop} -> meta::signed_integer;
        })
    struct _iota_difference<Start, Stop> {
        using type = meta::unqualify<decltype(
            std::declval<meta::as_const_ref<Stop>>() -
            std::declval<meta::as_const_ref<Stop>>()
        )>;
    };
    template <typename Start, typename Stop>
    using iota_difference = _iota_difference<Start, Stop>::type;

    template <typename T>
    concept iota_empty = meta::is<T, iota_tag>;

    template <typename Stop>
    concept iota_infinite = iota_empty<Stop>;

    template <typename Start, typename Stop, typename Step>
    concept iota_bounded =
        !iota_infinite<Stop> &&
        requires(meta::as_const_ref<Start> start, meta::as_const_ref<Stop> stop) {
            {start < stop} -> meta::truthy;
        } && (strictly_positive<Step> || requires(
            meta::as_const_ref<Start> start,
            meta::as_const_ref<Stop> stop
        ) {
            {start > stop} -> meta::truthy;
        });

    template <typename Start, typename Stop, typename Step>
    concept iota_conditional =
        !iota_infinite<Stop> &&
        !iota_bounded<Start, Stop, Step> &&
        requires(meta::as_const_ref<Start> start, meta::as_const_ref<Stop> stop) {
            {stop(start)} -> meta::truthy;
        };

    template <typename Start, typename Step>
    concept iota_simple =
        iota_empty<Step> &&
        requires(meta::unqualify<Start>& start) {
            {++start};
        };

    template <typename Start, typename Step>
    concept iota_linear =
        !iota_simple<Start, Step> &&
        requires(meta::unqualify<Start>& start, meta::as_const_ref<Step> step) {
            {start += step};
        };

    template <typename Start, typename Step>
    concept iota_nonlinear =
        !iota_simple<Start, Step> &&
        !iota_linear<Start, Step> &&
        requires(meta::unqualify<Start>& start, meta::as_const_ref<Step> step) {
            {step(start)} -> meta::is_void;
        };

    constexpr AssertionError zero_step_error() noexcept {
        return AssertionError("step size cannot be zero");
    }

    /* A simple, half-open range from `[start, stop)` that increments by `step` on each
    iteration.

    The `start`, `stop`, and `step` indices must satisfy the following criteria:

        1.  `start` must be copyable, while `stop` and `step` must either be copyable
            or lvalue references (whose lifetimes will not be extended).
        2.  `stop` must be either empty (indicating an infinite range), or satisfy
            one of the following (in order of preference):
                a.  `start < stop`.  If `step` is not empty and `step < 0` is
                    well-formed, then `start > stop` must also be valid.
                b.  `stop(start) -> bool`, where `false` terminates the range.
        3.  If `step` is empty, then `++start` must be valid.  Otherwise, one of
            the following must be well-formed (in order of preference):
                a.  `start += step`.  If `step == 0` is well-formed and evaluates to
                    true during the constructor, then a debug assertion will be thrown.
                b.  `step(start) -> void`, which modifies `start` in-place.

    Ranges of this form expose `size()` and `ssize()` methods as long as
    `(stop - start) / step` is a valid expression whose result can be explicitly
    converted to `difference_type`, and will also support indexing via the subscript
    operator if possible.  If `start` is decrementable, then the iterators will model
    `std::bidirectional_iterator`, and possibly also `std::random_access_iterator` if
    it supports random-access addition and subtraction with the step size.  Since the
    `end()` iterator is an empty sentinel, the range will never model
    `std::common_range` (but the sentinel may model `std::sized_sentinel_for<Begin>` if
    `ssize()` is available).

    The indices are meant to reflect typical loop syntax in a variety of languages,
    and can effectively replace any C-style `for` or `while` loop with zero overhead.
    The begin iterator over the range effectively equates to an explicit copy of the
    `start` index, while the end iterator is a sentinel that triggers the `stop`
    condition.  Incrementing the iterator updates the copy in-place according to
    `step`.

    The only possible regressions over a hand-rolled loop occur when:

        1.  The iota supports subscripting, which equates to a `start + n * step`
            expression.  In that case, the dereference operator must convert the result
            to a common type in order to satisfy `std::random_access_iterator`.  This
            may cause an extra copy on each dereference, which is usually not a problem
            for simple arithmetic types, but could be for more complex user-defined
            types.  Breaking the random-access constraint (such as by using an unsized
            `stop` condition or non-empty `step`) avoids this issue.
        2.  `step` is signed and `stop` is an absolute bound, in which case an extra
            branch must be emitted to confirm the direction of comparison.  In a future
            revision, this branch could be elided by either determining the signedness
            of `step` at compile-time if it is a constant, or by JIT compiling the
            comparison function during construction.  For now, the only way to avoid
            this is to replace the bound with a conditional `stop` function.
    */
    template <range_concept Start, range_concept Stop, range_concept Step>
        requires (
            meta::copyable<Start> &&
            (meta::lvalue<Stop> || meta::copyable<Stop>) &&
            (meta::lvalue<Step> || meta::copyable<Step>) &&
            (
                iota_infinite<Stop> ||
                iota_bounded<Start, Stop, Step> ||
                iota_conditional<Start, Stop, Step>
            ) && (
                iota_simple<Start, Step> ||
                iota_linear<Start, Step> ||
                iota_nonlinear<Start, Step>
            )
        )
    struct iota : iota_tag {
        using start_type = Start;
        using stop_type = Stop;
        using step_type = Step;
        using difference_type = iota_difference<Start, Stop>;
        using size_type = meta::as_unsigned<difference_type>;
        using value_type = meta::unqualify<Start>;
        using reference = meta::as_lvalue<value_type>;
        using pointer = meta::address_type<reference>;
        using iterator_category = std::conditional_t<
            (iota_empty<Stop> && requires(
                meta::as_const_ref<Start> start,
                meta::as_const_ref<Stop> stop
            ) {
                {difference_type(start - stop)};
            }) || (!iota_empty<Stop> && requires(
                meta::as_const_ref<Start> start,
                meta::as_const_ref<Stop> stop,
                meta::as_const_ref<Step> step
            ) {
                {difference_type(math::div::ceil<
                    meta::unqualify<decltype(stop - start)>,
                    meta::unqualify<Step>
                >{}(stop - start, step))};
            }),
            std::conditional_t<
                (iota_empty<Step> && requires(meta::unqualify<Start>& start) {
                    {--start};
                }) || (!iota_empty<Step> && requires(
                    meta::unqualify<Start>& start,
                    meta::as_const_ref<Step> step
                ) {
                    {start -= step};
                }),
                std::conditional_t<
                    (iota_empty<Step> && requires(
                        meta::unqualify<Start>& start,
                        difference_type i
                    ) {
                        {start + i} -> meta::has_common_type<meta::as_lvalue<Start>>;
                        {start += i};
                        {start -= i};
                    }) || (!iota_empty<Step> && requires(
                        meta::unqualify<Start>& start,
                        meta::as_const_ref<Step> step,
                        difference_type i
                    ) {
                        {start + i * step} -> meta::has_common_type<meta::as_lvalue<Start>>;
                        {start += i * step};
                        {start -= i * step};
                    }),
                    std::random_access_iterator_tag,
                    std::bidirectional_iterator_tag
                >,
                std::forward_iterator_tag
            >,
            std::input_iterator_tag
        >;

    private:
        using copy = iota<
            meta::unqualify<start_type>,
            meta::as_const_ref<stop_type>,
            meta::as_const_ref<step_type>
        >;

        [[no_unique_address]] impl::ref<start_type> m_start {};
        [[no_unique_address]] impl::ref<stop_type> m_stop {};
        [[no_unique_address]] impl::ref<step_type> m_step {};

    public:
        [[nodiscard]] constexpr iota() = default;
        [[nodiscard]] constexpr iota(
            meta::forward<Start> start,
            meta::forward<Stop> stop,
            meta::forward<Step> step = {}
        )
            noexcept (requires{
                {impl::ref<start_type>{std::forward<Start>(start)}} noexcept;
                {impl::ref<stop_type>{std::forward<Stop>(stop)}} noexcept;
                {impl::ref<step_type>{std::forward<Step>(step)}} noexcept;
            } && (
                !DEBUG ||
                !iota_linear<Start, Step> ||
                !requires{{*m_step == 0} -> meta::truthy;}
            ))
            requires (requires{
                {impl::ref<start_type>{std::forward<Start>(start)}};
                {impl::ref<stop_type>{std::forward<Stop>(stop)}};
                {impl::ref<step_type>{std::forward<Step>(step)}};
            })
        :
            m_start{std::forward<Start>(start)},
            m_stop{std::forward<Stop>(stop)},
            m_step{std::forward<Step>(step)}
        {
            if constexpr (
                DEBUG &&
                iota_linear<Start, Step> &&
                requires{{*m_step == 0} -> meta::truthy;}
            ) {
                if (*m_step == 0) {
                    throw zero_step_error();
                }
            }
        }
        [[nodiscard]] constexpr iota(
            const meta::unqualify<Start>& start,
            const meta::unqualify<Stop>& stop,
            const meta::unqualify<Step>& step
        )
            noexcept (requires{
                {impl::ref<Start>{start}} noexcept;
                {impl::ref<Stop>{stop}} noexcept;
                {impl::ref<Step>{step}} noexcept;
            })
            requires (requires{
                {impl::ref<Start>{start}};
                {impl::ref<Stop>{stop}};
                {impl::ref<Step>{step}};
            })
        :
            m_start{start},
            m_stop{stop},
            m_step{step}
        {}

        constexpr void swap(iota& other)
            noexcept (requires{
                {std::ranges::swap(m_start, other.m_start)} noexcept;
                {std::ranges::swap(m_stop, other.m_stop)} noexcept;
                {std::ranges::swap(m_step, other.m_step)} noexcept;
            })
            requires (requires{
                {std::ranges::swap(m_start, other.m_start)};
                {std::ranges::swap(m_stop, other.m_stop)};
                {std::ranges::swap(m_step, other.m_step)};
            })
        {
            std::ranges::swap(m_start, other.m_start);
            std::ranges::swap(m_stop, other.m_stop);
            std::ranges::swap(m_step, other.m_step);
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) start(this Self&& self) noexcept {
            return (*std::forward<Self>(self).m_start);
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) stop(this Self&& self) noexcept {
            return (*std::forward<Self>(self).m_stop);
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) step(this Self&& self) noexcept {
            return (*std::forward<Self>(self).m_step);
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) front(this Self&& self) noexcept {
            return (std::forward<Self>(self).start());
        }

        [[nodiscard]] constexpr copy begin() const
            noexcept (requires{{copy{start(), stop(), step()}} noexcept;})
            requires (requires{{copy{start(), stop(), step()}};})
        {
            return copy{start(), stop(), step()};
        }

        [[nodiscard]] static constexpr NoneType end() noexcept {
            return {};
        }

        template <typename Self>
        [[nodiscard]] constexpr auto operator[](this Self&& self, difference_type i)
            noexcept (requires{{
                std::forward<Self>(self).start() + i
            } noexcept -> meta::nothrow::convertible_to<meta::common_type<
                meta::remove_rvalue<decltype((std::forward<Self>(self).start()))>,
                meta::remove_rvalue<decltype(
                    (std::forward<Self>(self).start() + i)
                )>
            >>;})
            -> meta::common_type<
                meta::remove_rvalue<decltype((std::forward<Self>(self).start()))>,
                meta::remove_rvalue<decltype(
                    (std::forward<Self>(self).start() + i)
                )>
            >
            requires (iota_empty<Step> && requires{
                {self.ssize()};
                {
                    std::forward<Self>(self).start() + i
                } -> meta::convertible_to<meta::common_type<
                    meta::remove_rvalue<decltype((std::forward<Self>(self).start()))>,
                    meta::remove_rvalue<decltype(
                        (std::forward<Self>(self).start() + i)
                    )>
                >>;
            })
        {
            return std::forward<Self>(self).start() + i;
        }

        template <typename Self>
        [[nodiscard]] constexpr auto operator[](this Self&& self, difference_type i)
            noexcept (requires{{
                std::forward<Self>(self).start() + i * std::forward<Self>(self).step()
            } noexcept -> meta::nothrow::convertible_to<meta::common_type<
                meta::remove_rvalue<decltype((std::forward<Self>(self).start()))>,
                meta::remove_rvalue<decltype(
                    (std::forward<Self>(self).start() + i * std::forward<Self>(self).step())
                )>
            >>;})
            -> meta::common_type<
                meta::remove_rvalue<decltype((std::forward<Self>(self).start()))>,
                meta::remove_rvalue<decltype(
                    (std::forward<Self>(self).start() + i * std::forward<Self>(self).step())
                )>
            >
            requires (!iota_empty<Step> && requires{
                {self.ssize()};
                {
                    std::forward<Self>(self).start() + i * std::forward<Self>(self).step()
                } -> meta::convertible_to<meta::common_type<
                    meta::remove_rvalue<decltype((std::forward<Self>(self).start()))>,
                    meta::remove_rvalue<decltype(
                        (std::forward<Self>(self).start() + i * std::forward<Self>(self).step())
                    )>
                >>;
            })
        {
            return std::forward<Self>(self).start() + i * std::forward<Self>(self).step();
        }

        template <typename Self>
        [[nodiscard]] constexpr auto operator*(this Self&& self)
            noexcept (requires(difference_type i) {{
                std::forward<Self>(self)[i]
            } noexcept -> meta::nothrow::convertible_to<
                meta::remove_rvalue<decltype((std::forward<Self>(self)[i]))>
            >;})
            -> meta::remove_rvalue<decltype(
                (std::forward<Self>(self)[std::declval<difference_type>()])
            )>
            requires (requires(difference_type i) {
                {std::forward<Self>(self)[i]};
                {std::forward<Self>(self).start()} -> meta::convertible_to<
                    meta::remove_rvalue<decltype((std::forward<Self>(self)[i]))>
                >;
            })
        {
            return std::forward<Self>(self).start();
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator*(this Self&& self) noexcept
            requires (!requires(difference_type i) {{std::forward<Self>(self)[i]};})
        {
            return (std::forward<Self>(self).start());
        }

        [[nodiscard]] constexpr auto operator->()
            noexcept (requires{{meta::to_arrow(start())} noexcept;})
            requires (requires{{meta::to_arrow(start())};})
        {
            return meta::to_arrow(start());
        }

        [[nodiscard]] constexpr auto operator->() const
            noexcept (requires{{meta::to_arrow(start())} noexcept;})
            requires (requires{{meta::to_arrow(start())};})
        {
            return meta::to_arrow(start());
        }

        [[nodiscard]] static constexpr bool empty() noexcept requires (iota_infinite<Stop>) {
            return false;
        }

        [[nodiscard]] constexpr bool empty() const
            noexcept (requires{
                {start() < stop()} noexcept -> meta::nothrow::truthy;
            } && (strictly_positive<Step> || requires{
                {step() < 0} noexcept -> meta::nothrow::truthy;
                {start() > stop()} noexcept -> meta::nothrow::truthy;
            }))
            requires (iota_bounded<Start, Stop, Step>)
        {
            if constexpr (strictly_positive<Step>) {
                return !bool(start() < stop());
            } else {
                if (step() < 0) {
                    return !bool(start() > stop());
                } else {
                    return !bool(start() < stop());
                }
            }
        }

        [[nodiscard]] constexpr bool empty() const
            noexcept (requires{{stop()(start())} noexcept -> meta::nothrow::truthy;})
            requires (iota_conditional<Start, Stop, Step>)
        {
            return !bool(stop()(start()));
        }

        [[nodiscard]] explicit constexpr operator bool() const
            noexcept (requires{{empty()} noexcept;})
            requires (requires{{empty()};})
        {
            return !empty();
        }

        [[nodiscard]] friend constexpr bool operator<(const iota& self, NoneType)
            noexcept (requires{{!self.empty()} noexcept;})
            requires (requires{{!self.empty()};})
        {
            return !self.empty();
        }

        [[nodiscard]] friend constexpr bool operator<(NoneType, const iota& self) noexcept {
            return false;
        }

        [[nodiscard]] friend constexpr bool operator<=(const iota& self, NoneType) noexcept {
            return true;
        }

        [[nodiscard]] friend constexpr bool operator<=(NoneType, const iota& self)
            noexcept (requires{{self.empty()} noexcept;})
            requires (requires{{self.empty()};})
        {
            return self.empty();
        }

        [[nodiscard]] friend constexpr bool operator==(const iota& self, NoneType)
            noexcept (requires{{self.empty()} noexcept;})
            requires (requires{{self.empty()};})
        {
            return self.empty();
        }

        [[nodiscard]] friend constexpr bool operator==(NoneType, const iota& self)
            noexcept (requires{{self.empty()} noexcept;})
            requires (requires{{self.empty()};})
        {
            return self.empty();
        }

        [[nodiscard]] friend constexpr bool operator!=(const iota& self, NoneType)
            noexcept (requires{{!self.empty()} noexcept;})
            requires (requires{{!self.empty()};})
        {
            return !self.empty();
        }

        [[nodiscard]] friend constexpr bool operator!=(NoneType, const iota& self)
            noexcept (requires{{!self.empty()} noexcept;})
            requires (requires{{!self.empty()};})
        {
            return !self.empty();
        }

        [[nodiscard]] friend constexpr bool operator>=(const iota& self, NoneType)
            noexcept (requires{{self.empty()} noexcept;})
            requires (requires{{self.empty()};})
        {
            return self.empty();
        }

        [[nodiscard]] friend constexpr bool operator>=(NoneType, const iota& self) noexcept {
            return true;
        }

        [[nodiscard]] friend constexpr bool operator>(const iota& self, NoneType) noexcept {
            return false;
        }

        [[nodiscard]] friend constexpr bool operator>(NoneType, const iota& self)
            noexcept (requires{{!self.empty()} noexcept;})
            requires (requires{{!self.empty()};})
        {
            return !self.empty();
        }

        [[nodiscard]] constexpr difference_type ssize() const
            noexcept (requires{{difference_type(stop() - start())} noexcept;})
            requires (iota_empty<Step> && requires{{difference_type(stop() - start())};})
        {
            return difference_type(stop() - start());
        }

        [[nodiscard]] constexpr difference_type ssize() const
            noexcept (requires{{
                difference_type(math::div::ceil<
                    meta::unqualify<decltype(stop() - start())>,
                    meta::unqualify<Step>
                >{}(stop() - start(), step()))
            } noexcept -> meta::nothrow::convertible_to<difference_type>;})
            requires (!iota_empty<Step> && requires{{
                difference_type(math::div::ceil<
                    meta::unqualify<decltype(stop() - start())>,
                    meta::unqualify<Step>
                >{}(stop() - start(), step()))
            };})
        {
            return difference_type(math::div::ceil<
                meta::unqualify<decltype(stop() - start())>,
                meta::unqualify<Step>
            >{}(stop() - start(), step()));
        }

        [[nodiscard]] constexpr size_type size() const
            noexcept (requires{{size_type(ssize())} noexcept;})
            requires (requires{{size_type(ssize())};})
        {
            return size_type(ssize());
        }

        [[nodiscard]] constexpr bool operator==(const iota& other) const
            noexcept (requires{{
                (other.stop() - other.start()) == (stop() - start())
            } noexcept -> meta::nothrow::truthy;})
            requires (requires{{
                (other.stop() - other.start()) == (stop() - start())
            } -> meta::truthy;})
        {
            return bool((other.stop() - other.start()) == (stop() - start()));
        }

        [[nodiscard]] constexpr auto operator<=>(const iota& other) const
            noexcept (requires{{(other.stop() - other.start()) <=> (stop() - start())} noexcept;})
            requires (requires{{(other.stop() - other.start()) <=> (stop() - start())};})
        {
            return (other.stop() - other.start()) <=> (stop() - start());
        }

        [[nodiscard]] constexpr difference_type operator-(const iota& other) const
            noexcept (requires{{
                (other.stop() - other.start()) - (stop() - start())
            } noexcept -> meta::nothrow::convertible_to<difference_type>;})
            requires (iota_empty<Step> && requires{{
                (other.stop() - other.start()) - (stop() - start())
            } -> meta::convertible_to<difference_type>;})
        {
            return (other.stop() - other.start()) - (stop() - start());
        }

        [[nodiscard]] constexpr difference_type operator-(const iota& other) const
            noexcept (requires{{
                difference_type(math::div::ceil<
                    meta::unqualify<decltype(stop() - start())>,
                    meta::unqualify<Step>
                >{}((other.stop() - other.start()) - (stop() - start()), step()))
            } noexcept -> meta::nothrow::convertible_to<difference_type>;})
            requires (!iota_empty<Step> && requires{{
                difference_type(math::div::ceil<
                    meta::unqualify<decltype(stop() - start())>,
                    meta::unqualify<Step>
                >{}((other.stop() - other.start()) - (stop() - start()), step()))
            };})
        {
            return difference_type(math::div::ceil<
                meta::unqualify<decltype(stop() - start())>,
                meta::unqualify<Step>
            >{}((other.stop() - other.start()) - (stop() - start()), step()));
        }

        [[nodiscard]] friend constexpr difference_type operator-(const iota& self, NoneType)
            noexcept (requires{
                {-self.ssize()} noexcept -> meta::nothrow::convertible_to<difference_type>;
            })
            requires (requires{{-self.ssize()} -> meta::convertible_to<difference_type>;})
        {
            return -self.ssize();
        }

        [[nodiscard]] friend constexpr difference_type operator-(NoneType, const iota& self)
            noexcept (requires{{self.ssize()} noexcept;})
            requires (requires{{self.ssize()};})
        {
            return self.ssize();
        }

        constexpr void increment()
            noexcept (requires{{++start()} noexcept;})
            requires (iota_simple<Start, Step>)
        {
            ++start();
        }

        constexpr void increment()
            noexcept (requires{{start() += step()} noexcept;})
            requires (iota_linear<Start, Step>)
        {
            start() += step();
        }

        constexpr void increment()
            noexcept (requires{{step()(start())} noexcept;})
            requires (iota_nonlinear<Start, Step>)
        {
            step()(start());
        }

        constexpr iota& operator++()
            noexcept (requires{{increment()} noexcept;})
            requires (requires{{increment()};})
        {
            increment();
            return *this;
        }

        [[nodiscard]] constexpr iota operator++(int)
            noexcept (requires(copy tmp) {
                {begin()} noexcept;
                {tmp.increment()} noexcept;
            })
            requires (requires(copy tmp) {
                {begin()};
                {tmp.increment()};
            })
        {
            copy tmp = begin();
            tmp.increment();
            return tmp;
        }

        constexpr void increment(difference_type n)
            noexcept (requires{{start() += n} noexcept;})
            requires (iota_empty<Step> && requires{{start() += n};})
        {
            start() += n;
        }

        constexpr void increment(difference_type n)
            noexcept (requires{{start() += n * step()} noexcept;})
            requires (!iota_empty<Step> && requires{{start() += n * step()};})
        {
            start() += n * step();
        }

        constexpr iota& operator+=(difference_type n)
            noexcept (requires{{increment(n)} noexcept;})
            requires (requires{{increment(n)};})
        {
            increment(n);
            return *this;
        }

        [[nodiscard]] friend constexpr copy operator+(const iota& self, difference_type n)
            noexcept (requires(copy tmp) {
                {self.begin()} noexcept;
                {tmp.increment(n)} noexcept;
            })
            requires (requires(copy tmp) {
                {self.begin()};
                {tmp.increment(n)};
            })
        {
            copy tmp = self.begin();
            tmp.increment(n);
            return tmp;
        }

        [[nodiscard]] friend constexpr copy operator+(difference_type n, const iota& self)
            noexcept (requires(copy tmp) {
                {self.begin()} noexcept;
                {tmp.increment(n)} noexcept;
            })
            requires (requires(copy tmp) {
                {self.begin()};
                {tmp.increment(n)};
            })
        {
            copy tmp = self.begin();
            tmp.increment(n);
            return tmp;
        }

        constexpr void decrement()
            noexcept (requires{{--start()} noexcept;})
            requires (iota_empty<Step> && requires{{--start()};})
        {
            --start();
        }

        constexpr void decrement()
            noexcept (requires{{start() -= step()} noexcept;})
            requires (!iota_empty<Step> && requires{{start() -= step()};})
        {
            start() -= step();
        }

        constexpr iota& operator--()
            noexcept (requires{{decrement()} noexcept;})
            requires (requires{{decrement()};})
        {
            decrement();
            return *this;
        }

        [[nodiscard]] constexpr iota operator--(int)
            noexcept (requires(copy tmp) {
                {begin()} noexcept;
                {tmp.decrement()} noexcept;
            })
            requires (requires(copy tmp) {
                {begin()};
                {tmp.decrement()};
            })
        {
            copy tmp = begin();
            tmp.decrement();
            return tmp;
        }

        constexpr void decrement(difference_type n)
            noexcept (requires{{start() -= n} noexcept;})
            requires (iota_empty<Step> && requires{{start() -= n};})
        {
            start() -= n;
        }

        constexpr void decrement(difference_type n)
            noexcept (requires{{start() -= n * step()} noexcept;})
            requires (!iota_empty<Step> && requires{{start() -= n * step()};})
        {
            start() -= n * step();
        }

        constexpr iota& operator-=(difference_type n)
            noexcept (requires{{decrement(n)} noexcept;})
            requires (requires{{decrement(n)};})
        {
            decrement(n);
            return *this;
        }

        [[nodiscard]] constexpr copy operator-(difference_type n) const
            noexcept (requires(copy tmp) {
                {begin()} noexcept;
                {tmp.decrement(n)} noexcept;
            })
            requires (requires(copy tmp) {
                {begin()};
                {tmp.decrement(n)};
            })
        {
            copy tmp = begin();
            tmp.decrement(n);
            return tmp;
        }
    };

    template <typename Start, typename Stop = iota_tag, typename Step = iota_tag>
    iota(Start&&, Stop&& stop, Step&& step = {}) -> iota<
        meta::remove_rvalue<Start>,
        meta::remove_rvalue<Stop>,
        meta::remove_rvalue<Step>
    >;

    template <typename Start, typename Stop>
    using subrange_difference = iota_difference<Start, Stop>;

    template <typename T>
    concept subrange_empty = meta::is<T, subrange_tag>;

    template <typename Stop>
    concept subrange_infinite = subrange_empty<Stop>;

    template <typename Start, typename Stop, typename Step>
    concept subrange_bounded =
        !subrange_infinite<Stop> &&
        requires(meta::as_const_ref<Start> start, meta::as_const_ref<Stop> stop) {
            {start < stop} -> meta::truthy;
        } && (strictly_positive<Step> || requires(
            meta::as_const_ref<Start> start,
            meta::as_const_ref<Stop> stop
        ) {
            {start > stop} -> meta::truthy;
        });

    template <typename Start, typename Stop, typename Step>
    concept subrange_equal =
        !subrange_infinite<Stop> &&
        !subrange_bounded<Start, Stop, Step> &&
        subrange_empty<Step> &&
        requires(meta::as_const_ref<Start> start, meta::as_const_ref<Stop> stop) {
            {start == stop} -> meta::truthy;
        };

    template <typename Start, typename Stop, typename Step>
    concept subrange_counted =
        !subrange_infinite<Stop> &&
        !subrange_bounded<Start, Stop, Step> &&
        !subrange_equal<Start, Stop, Step> &&
        meta::integer<Stop> &&
        requires(
            subrange_difference<Start, Stop> index,
            meta::as_const_ref<Stop> stop
        ) {
            {index >= subrange_difference<Start, Stop>(stop)} -> meta::truthy;
        };

    template <typename Start, typename Stop, typename Step>
    concept subrange_conditional =
        !subrange_infinite<Stop> &&
        !subrange_bounded<Start, Stop, Step> &&
        !subrange_equal<Start, Stop, Step> &&
        !subrange_counted<Start, Stop, Step> &&
        requires(meta::as_const_ref<Start> start, meta::as_const_ref<Stop> stop) {
            {stop(start)} -> meta::truthy;
        };

    template <typename Start, typename Step>
    concept subrange_simple =
        subrange_empty<Step> &&
        requires(meta::unqualify<Start>& start) {
            {++start};
        };

    /// TODO: subrange_linear can only be used if the stop condition supports distance
    /// checks?

    template <typename Start, typename Stop, typename Step>
    concept subrange_linear =
        !subrange_simple<Start, Step> &&
        meta::convertible_to<Step, subrange_difference<Start, Stop>> &&
        requires(meta::unqualify<Start>& start, subrange_difference<Start, Stop> step) {
            {start += step};
        };

    template <typename Start, typename Stop, typename Step>
    concept subrange_loop =
        !subrange_simple<Start, Step> &&
        !subrange_linear<Start, Stop, Step> &&
        meta::convertible_to<Step, subrange_difference<Start, Stop>> &&
        meta::default_constructible<subrange_difference<Start, Stop>> &&
        requires(
            meta::unqualify<Start>& start,
            subrange_difference<Start, Stop> step,
            subrange_difference<Start, Stop> i
        ) {
            {i < step};
            {++i};
            {++start};
        } && (strictly_positive<Step> || requires(
            meta::unqualify<Start>& start,
            subrange_difference<Start, Stop> step,
            subrange_difference<Start, Stop> i
        ) {
            {i > step};
            {--i};
            {--start};
        });

    template <typename Start, typename Stop, typename Step>
    concept subrange_nonlinear =
        !subrange_simple<Start, Step> &&
        !subrange_linear<Start, Stop, Step> &&
        !subrange_loop<Start, Stop, Step> &&
        requires(meta::unqualify<Start>& start, meta::as_const_ref<Step> step) {
            {step(start)} -> meta::is_void;
        };

    constexpr AssertionError negative_count_error() noexcept {
        return AssertionError("count cannot be negative");
    }

    enum class subrange_check {
        NEVER,
        CONSTEVAL,
        ALWAYS
    };

    template <typename Start, typename Stop, typename Step>
    concept subrange_concept =
        range_concept<Start> &&
        range_concept<Stop> &&
        range_concept<Step> &&
        meta::iterator<Start> &&
        meta::copyable<Start> &&
        (meta::lvalue<Stop> || meta::copyable<Stop>) &&
        (meta::lvalue<Step> || meta::copyable<Step>) &&
        (
            subrange_infinite<Stop> ||
            subrange_bounded<Start, Stop, Step> ||
            subrange_equal<Start, Stop, Step> ||
            subrange_counted<Start, Stop, Step> ||
            subrange_conditional<Start, Stop, Step>
        ) && (
            subrange_simple<Start, Step> ||
            subrange_linear<Start, Stop, Step> ||
            subrange_loop<Start, Stop, Step> ||
            subrange_nonlinear<Start, Stop, Step>
        );

    /* A simple subrange that yields successive values in the interval `[start, stop)`,
    incrementing by `step` on each iteration.

    This class behaves similarly to `iota` in most respects, but differs in the
    following:

        1.  `start` must be an iterator type, rather than a value type.
        2.  `stop` can be given as a positive integer count rather than an absolute
            bound or conditional function, which converts the subrange into a counted
            range.  `start == stop` may also be used instead of `start < stop` to
            detect the end of the range, since iterators are always incremented in
            discrete steps, and equality comparisons are therefore reliable.
        3.  The `step` size must be empty, a function object taking the range as an
            argument, or convertible to the subrange's difference type (a signed
            integer).  If `start += step` is not a valid expression, then `++start`
            and/or `--start` may be called in a loop depending on the sign of `step`.
        4.  Subrange iterators are always totally-ordered with respect to each other
            and the `end()` sentinel, even if the underlying iterator is not.
        5.  Extra bounds-checking may be performed to ensure that the `start` iterator
            does not exceed the `stop` bound during constant evaluation, and remains
            captured within the interval.  Additional tracking indices are used to
            ensure this, which may add a small amount of overhead to iteration.  The
            bounds checking may be elided in non-constant-evaluation contexts if
            comparisons against `stop` are either infinite, ordered, or counted.

    Ranges of this form expose `size()` and `ssize()` methods as long as
    `(stop - start) / step` is a valid expression, and will also support indexing via
    the subscript operator if the underlying iterator supports it.  If the underlying
    iterator is also bidirectional, then the range will model
    `std::bidirectional_range` as well.  If the iterator is random-access, then the
    range will model `std::random_access_range`, and if it is contiguous and no step
    size is given, then the range will model `std::contiguous_range` and provide a
    `data()` method as well.  Since the `end()` iterator is an empty sentinel, the
    range will never model `std::common_range` (but the sentinel may model
    `std::sized_sentinel_for<Begin>` if `ssize()` is available). */
    template <typename Start, typename Stop = subrange_tag, typename Step = subrange_tag>
        requires (subrange_concept<Start, Stop, Step>)
    struct subrange : subrange_tag {
        using difference_type = subrange_difference<Start, Stop>;
        using size_type = meta::as_unsigned<difference_type>;
        using value_type = meta::iterator_value<Start>;
        using reference = meta::iterator_reference<Start>;
        using pointer = meta::iterator_pointer<Start>;
        using iterator_category = std::conditional_t<
            !subrange_nonlinear<Start, Stop, Step> && meta::bidirectional_iterator<Start>,
            std::conditional_t<
                meta::random_access_iterator<Start>,
                std::conditional_t<
                    meta::contiguous_iterator<Start> && subrange_empty<Step> && (
                        subrange_infinite<Stop> ||
                        (subrange_counted<Start, Stop, Step> && requires(
                            meta::as_const_ref<Stop> stop,
                            difference_type index
                        ) {{
                            difference_type(stop) - index
                        } -> meta::convertible_to<difference_type>;}) ||
                        (!subrange_counted<Start, Stop, Step> && requires(
                            meta::as_const_ref<Stop> stop,
                            meta::as_const_ref<Start> start
                        ) {{stop - start} -> meta::convertible_to<difference_type>;})
                    ),
                    std::contiguous_iterator_tag,
                    std::random_access_iterator_tag
                >,
                std::bidirectional_iterator_tag
            >,
            std::forward_iterator_tag
        >;
        using start_type = Start;
        using stop_type = Stop;
        using step_type = std::conditional_t<
            subrange_linear<Start, Stop, Step> || subrange_loop<Start, Stop, Step>,
            difference_type,
            Step
        >;

    private:
        using copy = subrange<
            meta::unqualify<start_type>,
            meta::as_const_ref<stop_type>,
            meta::as_const_ref<step_type>
        >;

        static constexpr subrange_check check =
            (subrange_infinite<Stop> || subrange_simple<Start, Step>) ?
                subrange_check::NEVER :
                (subrange_bounded<Start, Stop, Step> || subrange_counted<Start, Stop, Step>) &&
                (subrange_linear<Start, Stop, Step> || subrange_loop<Start, Stop, Step>) ?
                    subrange_check::CONSTEVAL :
                    subrange_check::ALWAYS;

        using overflow_type = std::conditional_t<
            check == subrange_check::NEVER,
            subrange_tag,
            difference_type
        >;

        [[no_unique_address]] impl::ref<start_type> m_start {};
        [[no_unique_address]] impl::ref<stop_type> m_stop {};
        [[no_unique_address]] impl::ref<step_type> m_step {};
        [[no_unique_address]] difference_type m_index {};
        [[no_unique_address]] overflow_type m_overflow {};

    public:
        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) start(this Self&& self) noexcept {
            return (*std::forward<Self>(self).m_start);
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) stop(this Self&& self) noexcept {
            return (*std::forward<Self>(self).m_stop);
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) step(this Self&& self) noexcept {
            return (*std::forward<Self>(self).m_step);
        }

        [[nodiscard]] constexpr difference_type& index() noexcept
            requires (check == subrange_check::NEVER)
        {
            return m_index;
        }

        [[nodiscard]] constexpr const difference_type& index() const noexcept
            requires (check == subrange_check::NEVER)
        {
            return m_index;
        }

        [[nodiscard]] constexpr difference_type index() const noexcept
            requires (check != subrange_check::NEVER)
        {
            if constexpr (check == subrange_check::ALWAYS) {
                return m_index + m_overflow;
            } else {
                if consteval {
                    return m_index + m_overflow;
                } else {
                    return m_index;
                }
            }
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) front(this Self&& self)
            noexcept (requires{{*std::forward<Self>(self).start()} noexcept;})
            requires (requires{{*std::forward<Self>(self).start()};})
        {
            return (*std::forward<Self>(self).start());
        }

        [[nodiscard]] constexpr auto data()
            noexcept (requires{{std::addressof(front())} noexcept;})
            requires (meta::is<iterator_category, std::contiguous_iterator_tag> && requires{
                {std::addressof(front())};
            })
        {
            return std::addressof(front());
        }

        [[nodiscard]] constexpr auto data() const
            noexcept (requires{{std::addressof(front())} noexcept;})
            requires (meta::is<iterator_category, std::contiguous_iterator_tag> && requires{
                {std::addressof(front())};
            })
        {
            return std::addressof(front());
        }

        [[nodiscard]] constexpr subrange() = default;
        [[nodiscard]] constexpr subrange(
            meta::forward<Start> start,
            meta::forward<Stop> stop,
            meta::forward<Step> step = {}
        )
            noexcept (requires{
                {impl::ref<start_type>{std::forward<Start>(start)}} noexcept;
                {impl::ref<stop_type>{std::forward<Stop>(stop)}} noexcept;
                {impl::ref<step_type>{std::forward<Step>(step)}} noexcept;
            } && (
                !DEBUG ||
                (!subrange_linear<Start, Stop, Step> && !subrange_loop<Start, Stop, Step>) ||
                !requires{{this->step() == 0} -> meta::truthy;}
            ) && (
                !DEBUG ||
                !subrange_counted<Start, Stop, Step> ||
                !requires{{this->stop() < 0} -> meta::truthy;}
            ))
            requires (requires{
                {impl::ref<start_type>{std::forward<Start>(start)}};
                {impl::ref<stop_type>{std::forward<Stop>(stop)}};
                {impl::ref<step_type>{std::forward<Step>(step)}};
            })
        :
            m_start{std::forward<Start>(start)},
            m_stop{std::forward<Stop>(stop)},
            m_step{std::forward<Step>(step)}
        {
            if constexpr (DEBUG) {
                if constexpr (
                    (subrange_linear<Start, Stop, Step> || subrange_loop<Start, Stop, Step>) &&
                    requires{{this->step() == 0} -> meta::truthy;}
                ) {
                    if (this->step() == 0) {
                        throw zero_step_error();
                    }
                }
                if constexpr (
                    subrange_counted<Start, Stop, Step> &&
                    requires{{this->stop() < 0} -> meta::truthy;}
                ) {
                    if (this->stop() < 0) {
                        throw negative_count_error();
                    }
                }
            }
        }
        [[nodiscard]] constexpr subrange(
            const meta::unqualify<start_type>& start,
            const meta::unqualify<stop_type>& stop,
            const meta::unqualify<step_type>& step,
            const difference_type& index,
            const overflow_type& overflow
        )
            noexcept (requires{
                {impl::ref<start_type>{start}} noexcept;
                {impl::ref<stop_type>{stop}} noexcept;
                {impl::ref<step_type>{step}} noexcept;
                {difference_type(index)} noexcept;
                {overflow_type(overflow)} noexcept;
            })
            requires (requires{
                {impl::ref<start_type>{start}};
                {impl::ref<stop_type>{stop}};
                {impl::ref<step_type>{step}};
                {difference_type(index)};
                {overflow_type(overflow)};
            })
        :
            m_start{start},
            m_stop{stop},
            m_step{step},
            m_index(index),
            m_overflow(overflow)
        {}

        constexpr void swap(subrange& other)
            noexcept (requires{
                {std::ranges::swap(m_start, other.m_start)} noexcept;
                {std::ranges::swap(m_stop, other.m_stop)} noexcept;
                {std::ranges::swap(m_step, other.m_step)} noexcept;
                {std::ranges::swap(m_index, other.m_index)} noexcept;
                {std::ranges::swap(m_overflow, other.m_overflow)} noexcept;
            })
            requires (requires{
                {std::ranges::swap(m_start, other.m_start)};
                {std::ranges::swap(m_stop, other.m_stop)};
                {std::ranges::swap(m_step, other.m_step)};
                {std::ranges::swap(m_index, other.m_index)};
                {std::ranges::swap(m_overflow, other.m_overflow)};
            })
        {
            std::ranges::swap(m_start, other.m_start);
            std::ranges::swap(m_stop, other.m_stop);
            std::ranges::swap(m_step, other.m_step);
            std::ranges::swap(m_index, other.m_index);
            std::ranges::swap(m_overflow, other.m_overflow);
        }

        [[nodiscard]] constexpr copy begin() const
            noexcept (requires{{copy{start(), stop(), step(), m_index, m_overflow}} noexcept;})
            requires (requires{{copy{start(), stop(), step(), m_index, m_overflow}};})
        {
            return copy{start(), stop(), step(), m_index, m_overflow};
        }

        [[nodiscard]] static constexpr NoneType end() noexcept {
            return {};
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator[](this Self&& self, difference_type n)
            noexcept (requires{{std::forward<Self>(self).start()[n]} noexcept;})
            requires (subrange_empty<Step> && requires{{std::forward<Self>(self).start()[n]};})
        {
            return (std::forward<Self>(self).start()[n]);
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator[](this Self&& self, difference_type n)
            noexcept (requires{
                {std::forward<Self>(self).start()[n + std::forward<Self>(self).step()]} noexcept;
            })
            requires (!subrange_empty<Step> && requires{
                {std::forward<Self>(self).start()[n * std::forward<Self>(self).step()]};
            })
        {
            return (std::forward<Self>(self).start()[n * std::forward<Self>(self).step()]);
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator*(this Self&& self)
            noexcept (requires{{std::forward<Self>(self).front()} noexcept;})
            requires (requires{{std::forward<Self>(self).front()};})
        {
            return (std::forward<Self>(self).front());
        }

        template <typename Self>
        [[nodiscard]] constexpr auto operator->(this Self&& self)
            noexcept (requires{{impl::arrow{*std::forward<Self>(self)}} noexcept;})
            requires (requires{{impl::arrow{*std::forward<Self>(self)}};})
        {
            return impl::arrow{*std::forward<Self>(self)};
        }

        [[nodiscard]] static constexpr bool empty() noexcept requires (subrange_infinite<Stop>) {
            return false;
        }

        [[nodiscard]] constexpr bool empty() const
            noexcept (requires{
                {start() < stop()} noexcept -> meta::nothrow::truthy;
            } && (strictly_positive<Step> || requires{
                {step() < 0} noexcept -> meta::nothrow::truthy;
                {start() > stop()} noexcept -> meta::nothrow::truthy;
            }))
            requires (subrange_bounded<Start, Stop, Step>)
        {
            if constexpr (strictly_positive<Step>) {
                return !bool(start() < stop());
            } else {
                if (step() < 0) {
                    return !bool(start() > stop());
                } else {
                    return !bool(start() < stop());
                }
            }
        }

        [[nodiscard]] constexpr bool empty() const
            noexcept (requires{{start() == stop()} noexcept -> meta::nothrow::truthy;})
            requires (subrange_equal<Start, Stop, Step>)
        {
            return bool(start() == stop());
        }

        [[nodiscard]] constexpr bool empty() const
            noexcept (requires{
                {index() >= difference_type(stop())} noexcept -> meta::nothrow::truthy;
            })
            requires (subrange_counted<Start, Stop, Step>)
        {
            return bool(index() >= difference_type(stop()));
        }

        [[nodiscard]] constexpr bool empty() const
            noexcept (requires{{stop()(start())} noexcept -> meta::nothrow::truthy;})
            requires (subrange_conditional<Start, Stop, Step>)
        {
            return !bool(stop()(start()));
        }

        [[nodiscard]] explicit constexpr operator bool() const
            noexcept (requires{{!empty()} noexcept;})
            requires (requires{{!empty()};})
        {
            return !empty();
        }

        [[nodiscard]] friend constexpr bool operator<(const subrange& self, NoneType)
            noexcept (requires{{!self.empty()} noexcept;})
            requires (requires{{!self.empty()};})
        {
            return !self.empty();
        }

        [[nodiscard]] friend constexpr bool operator<(NoneType, const subrange& self) noexcept {
            return false;
        }

        [[nodiscard]] friend constexpr bool operator<=(const subrange& self, NoneType) noexcept {
            return true;
        }

        [[nodiscard]] friend constexpr bool operator<=(NoneType, const subrange& self)
            noexcept (requires{{self.empty()} noexcept;})
            requires (requires{{self.empty()};})
        {
            return self.empty();
        }

        [[nodiscard]] friend constexpr bool operator==(const subrange& self, NoneType)
            noexcept (requires{{self.empty()} noexcept;})
            requires (requires{{self.empty()};})
        {
            return self.empty();
        }

        [[nodiscard]] friend constexpr bool operator==(NoneType, const subrange& self)
            noexcept (requires{{self.empty()} noexcept;})
            requires (requires{{self.empty()};})
        {
            return self.empty();
        }

        [[nodiscard]] friend constexpr bool operator!=(const subrange& self, NoneType)
            noexcept (requires{{!self.empty()} noexcept;})
            requires (requires{{!self.empty()};})
        {
            return !self.empty();
        }

        [[nodiscard]] friend constexpr bool operator!=(NoneType, const subrange& self)
            noexcept (requires{{!self.empty()} noexcept;})
            requires (requires{{!self.empty()};})
        {
            return !self.empty();
        }

        [[nodiscard]] friend constexpr bool operator>=(const subrange& self, NoneType)
            noexcept (requires{{self.empty()} noexcept;})
            requires (requires{{self.empty()};})
        {
            return self.empty();
        }

        [[nodiscard]] friend constexpr bool operator>=(NoneType, const subrange& self) noexcept {
            return true;
        }

        [[nodiscard]] friend constexpr bool operator>(const subrange& self, NoneType) noexcept {
            return false;
        }

        [[nodiscard]] friend constexpr bool operator>(NoneType, const subrange& self)
            noexcept (requires{{!self.empty()} noexcept;})
            requires (requires{{!self.empty()};})
        {
            return !self.empty();
        }

        [[nodiscard]] constexpr bool operator==(const subrange& other) const
            noexcept (requires{{index() == other.index()} noexcept -> meta::nothrow::truthy;})
            requires (requires{{index() == other.index()} -> meta::truthy;})
        {
            return bool(index() == other.index());
        }

        [[nodiscard]] constexpr auto operator<=>(const subrange& other) const
            noexcept (requires{{index() <=> other.index()} noexcept;})
            requires (requires{{index() <=> other.index()};})
        {
            return index() <=> other.index();
        }

        [[nodiscard]] constexpr difference_type operator-(const subrange& other) const
            noexcept (requires{
                {index() - other.index()} noexcept -> meta::nothrow::convertible_to<difference_type>;
            })
            requires (subrange_empty<Step> && requires{
                {index() - other.index()} -> meta::convertible_to<difference_type>;
            })
        {
            return index() - other.index();
        }

        [[nodiscard]] constexpr difference_type operator-(const subrange& other) const
            noexcept (requires{{
                (index() - other.index()) / step()
            } noexcept -> meta::nothrow::convertible_to<difference_type>;})
            requires (!subrange_empty<Step> && requires{{
                (index() - other.index()) / step()
            } -> meta::convertible_to<difference_type>;})
        {
            return (index() - other.index()) / step();
        }

    private:
        constexpr difference_type remaining() const
            noexcept (requires{{
                difference_type(stop()) - index()
            } noexcept -> meta::nothrow::convertible_to<difference_type>;})
            requires (subrange_counted<Start, Stop, Step> && requires{{
                difference_type(stop()) - index()
            } -> meta::convertible_to<difference_type>;})
        {
            return difference_type(stop()) - index();
        }

        constexpr difference_type remaining() const
            noexcept (requires{
                {stop() - start()} noexcept -> meta::nothrow::convertible_to<difference_type>;
            })
            requires (!subrange_counted<Start, Stop, Step> && requires{
                {stop() - start()} -> meta::convertible_to<difference_type>;
            })
        {
            return stop() - start();
        }

        constexpr void unsafe_increment()
            noexcept (requires{{++start()} noexcept;})
            requires (check != subrange_check::ALWAYS && requires{{++start()};})
        {
            ++start();
            ++m_index;
        }

        constexpr void safe_increment()
            noexcept (requires{
                {empty()} noexcept;
                {++start()} noexcept;
            })
            requires (check != subrange_check::NEVER && requires{
                {empty()};
                {++start()};
            })
        {
            if (empty() || m_overflow < 0) {
                ++m_overflow;
            } else {
                ++start();
                ++m_index;
            }
        }

        constexpr void unsafe_decrement()
            noexcept (requires{{--start()} noexcept;})
            requires (check != subrange_check::ALWAYS && requires{{--start()};})
        {
            --start();
            --m_index;
        }

        constexpr void safe_decrement()
            noexcept (requires{{--start()} noexcept;})
            requires (check != subrange_check::NEVER && requires{{--start()};})
        {
            if (m_index == 0 || m_overflow > 0) {
                --m_overflow;
            } else {
                --start();
                --m_index;
            }
        }

        constexpr void unsafe_increment_by(difference_type n)
            noexcept (requires{{start() += n} noexcept;})
            requires (check != subrange_check::ALWAYS && requires{{start() += n};})
        {
            start() += n;
            m_index += n;
        }

        constexpr void safe_increment_by(difference_type n)
            noexcept (requires(difference_type delta) {
                {start() -= m_index} noexcept;
                {start() += n} noexcept;
                {empty()} noexcept;
                {remaining()} noexcept;
            })
            requires (check != subrange_check::NEVER && requires{
                {start() -= m_index};
                {start() += n};
                {empty()};
                {remaining()};
            })
        {
            if (n < 0) {
                if (m_index == 0) {
                    m_overflow += n;
                    return;
                }
                if (m_overflow > 0) {
                    m_overflow += n;
                    if (m_overflow > 0) {
                        return;
                    }
                    n = m_overflow;
                    m_overflow = 0;
                }
                if (-n > m_index) {
                    start() -= m_index;
                    m_overflow += m_index + n;
                    m_index = 0;
                } else {
                    start() += n;
                    m_index += n;
                }
            } else {
                if (empty()) {
                    m_overflow += n;
                    return;
                }
                if (m_overflow < 0) {
                    m_overflow += n;
                    if (m_overflow < 0) {
                        return;
                    }
                    n = m_overflow;
                    m_overflow = 0;
                }
                difference_type delta = remaining();
                if (n > delta) {
                    start() += delta;
                    m_overflow += n - delta;
                    m_index += delta;
                } else {
                    start() += n;
                    m_index += n;
                }
            }
        }

        constexpr void increment_by(difference_type n)
            noexcept ((
                check == subrange_check::NEVER ||
                requires{{safe_increment_by(n)} noexcept;}
            ) && (
                check == subrange_check::ALWAYS ||
                requires{{unsafe_increment_by(n)} noexcept;}
            ))
            requires ((
                check == subrange_check::NEVER ||
                requires{{safe_increment_by(n)};}
            ) && (
                check == subrange_check::ALWAYS ||
                requires{{unsafe_increment_by(n)};}
            ))
        {
            if constexpr (check == subrange_check::NEVER) {
                unsafe_increment_by(n);
            } else if constexpr (check == subrange_check::ALWAYS) {
                safe_increment_by(n);
            } else {
                if consteval {
                    safe_increment_by(n);
                } else {
                    unsafe_increment_by(n);
                }
            }
        }

        constexpr void unsafe_decrement_by(difference_type n)
            noexcept (requires{{start() -= n} noexcept;})
            requires (check != subrange_check::ALWAYS && requires{{start() -= n};})
        {
            start() -= n;
            m_index -= n;
        }

        constexpr void safe_decrement_by(difference_type n)
            noexcept (requires(difference_type delta) {
                {empty()} noexcept;
                {remaining()} noexcept;
                {start() += delta} noexcept;
                {start() -= n} noexcept;
            })
            requires (check != subrange_check::NEVER && requires(difference_type delta) {
                {empty()};
                {remaining()};
                {start() += delta};
                {start() -= n};
            })
        {
            if (n < 0) {
                if (empty()) {
                    m_overflow -= n;
                    return;
                }
                if (m_overflow < 0) {
                    m_overflow -= n;
                    if (m_overflow < 0) {
                        return;
                    }
                    n = m_overflow;
                    m_overflow = 0;
                }
                difference_type delta = remaining();
                if (-n > delta) {
                    start() += delta;
                    m_overflow -= n + delta;
                    m_index += delta;
                } else {
                    start() -= n;
                    m_index -= n;
                }
            } else {
                if (m_index == 0) {
                    m_overflow -= n;
                    return;
                }
                if (m_overflow > 0) {
                    m_overflow -= n;
                    if (m_overflow > 0) {
                        return;
                    }
                    n = -m_overflow;
                    m_overflow = 0;
                }
                if (n > m_index) {
                    start() -= m_index;
                    m_overflow += m_index - n;
                    m_index = 0;
                } else {
                    start() -= n;
                    m_index -= n;
                }
            }
        }

        constexpr void decrement_by(difference_type n)
            noexcept ((
                check == subrange_check::NEVER ||
                requires{{safe_decrement_by(n)} noexcept;}
            ) && (
                check == subrange_check::ALWAYS ||
                requires{{unsafe_decrement_by(n)} noexcept;}
            ))
            requires ((
                check == subrange_check::NEVER ||
                requires{{safe_decrement_by(n)};}
            ) && (
                check == subrange_check::ALWAYS ||
                requires{{unsafe_decrement_by(n)};}
            ))
        {
            if constexpr (check == subrange_check::NEVER) {
                unsafe_decrement_by(n);
            } else if constexpr (check == subrange_check::ALWAYS) {
                safe_decrement_by(n);
            } else {
                if consteval {
                    safe_decrement_by(n);
                } else {
                    unsafe_decrement_by(n);
                }
            }
        }

        constexpr void unsafe_increment_for_positive(difference_type n)
            noexcept (requires{{++start()} noexcept;})
            requires (check != subrange_check::ALWAYS && requires{{++start()};})
        {
            for (difference_type i {}; i < n; ++i) ++start();
            m_index += n;
        }

        constexpr void safe_increment_for_positive(difference_type n)
            noexcept (requires{
                {empty()} noexcept;
                {++start()} noexcept;
            })
            requires (requires{
                {empty()};
                {++start()};
            })
        {
            for (difference_type i {}; i < n; ++i) {
                if (empty() || m_overflow < 0) {
                    ++m_overflow;
                } else {
                    ++m_index;
                    ++start();
                }
            }
        }

        constexpr void increment_for_positive(difference_type n)
            noexcept ((
                check == subrange_check::NEVER ||
                requires{{safe_increment_for_positive(n)} noexcept;}
            ) && (
                check == subrange_check::ALWAYS ||
                requires{{unsafe_increment_for_positive(n)} noexcept;}
            ))
            requires ((
                check == subrange_check::NEVER ||
                requires{{safe_increment_for_positive(n)};}
            ) && (
                check == subrange_check::ALWAYS ||
                requires{{unsafe_increment_for_positive(n)};}
            ))
        {
            if constexpr (check == subrange_check::NEVER) {
                unsafe_increment_for_positive(n);
            } else if constexpr (check == subrange_check::ALWAYS) {
                safe_increment_for_positive(n);
            } else {
                if consteval {
                    safe_increment_for_positive(n);
                } else {
                    unsafe_increment_for_positive(n);
                }
            }
        }

        constexpr void unsafe_increment_for_negative(difference_type n)
            noexcept (requires{{--start()} noexcept;})
            requires (check != subrange_check::ALWAYS && requires{{--start()};})
        {
            for (difference_type i {}; i > n; --i) --start();
            m_index -= n;
        }

        constexpr void safe_increment_for_negative(difference_type n)
            noexcept (requires{
                {empty()} noexcept;
                {--start()} noexcept;
            })
            requires (check != subrange_check::NEVER && requires{
                {empty()};
                {--start()};
            })
        {
            for (difference_type i {}; i > n; --i) {
                if (empty() || m_overflow < 0) {
                    ++m_overflow;
                } else {
                    ++m_index;
                    --start();
                }
            }
        }

        constexpr void increment_for_negative(difference_type n)
            noexcept ((
                check == subrange_check::NEVER ||
                requires{{safe_increment_for_negative(n)} noexcept;}
            ) && (
                check == subrange_check::ALWAYS ||
                requires{{unsafe_increment_for_negative(n)} noexcept;}
            ))
            requires ((
                check == subrange_check::NEVER ||
                requires{{safe_increment_for_negative(n)};}
            ) && (
                check == subrange_check::ALWAYS ||
                requires{{unsafe_increment_for_negative(n)};}
            ))
        {
            if constexpr (check == subrange_check::NEVER) {
                unsafe_increment_for_negative(n);
            } else if constexpr (check == subrange_check::ALWAYS) {
                safe_increment_for_negative(n);
            } else {
                if consteval {
                    safe_increment_for_negative(n);
                } else {
                    unsafe_increment_for_negative(n);
                }
            }
        }

        constexpr void unsafe_decrement_for_positive(difference_type n)
            noexcept (requires{{--start()} noexcept;})
            requires (check != subrange_check::ALWAYS && requires{{--start()};})
        {
            for (difference_type i {}; i < n; ++i) --start();
            m_index -= n;
        }

        constexpr void safe_decrement_for_positive(difference_type n)
            noexcept (requires{{--start()} noexcept;})
            requires (check != subrange_check::NEVER && requires{{--start()};})
        {
            for (difference_type i {}; i < n; ++i) {
                if (m_index == 0 || m_overflow > 0) {
                    --m_overflow;
                } else {
                    --m_index;
                    --start();
                }
            }
        }

        constexpr void decrement_for_positive(difference_type n)
            noexcept ((
                check == subrange_check::NEVER ||
                requires{{safe_decrement_for_positive(n)} noexcept;}
            ) && (
                check == subrange_check::ALWAYS ||
                requires{{unsafe_decrement_for_positive(n)} noexcept;}
            ))
            requires ((
                check == subrange_check::NEVER ||
                requires{{safe_decrement_for_positive(n)};}
            ) && (
                check == subrange_check::ALWAYS ||
                requires{{unsafe_decrement_for_positive(n)};}
            ))
        {
            if constexpr (check == subrange_check::NEVER) {
                unsafe_decrement_for_positive(n);
            } else if constexpr (check == subrange_check::ALWAYS) {
                safe_decrement_for_positive(n);
            } else {
                if consteval {
                    safe_decrement_for_positive(n);
                } else {
                    unsafe_decrement_for_positive(n);
                }
            }
        }

        constexpr void unsafe_decrement_for_negative(difference_type n)
            noexcept (requires{{++start()} noexcept;})
            requires (check != subrange_check::ALWAYS && requires{{++start()};})
        {
            for (difference_type i {}; i > n; --i) ++start();
            m_index += n;
        }

        constexpr void safe_decrement_for_negative(difference_type n)
            noexcept (requires{{++start()} noexcept;})
            requires (check != subrange_check::NEVER && requires{{++start()};})
        {
            for (difference_type i {}; i > n; --i) {
                if (m_index == 0 || m_overflow > 0) {
                    --m_overflow;
                } else {
                    --m_index;
                    ++start();
                }
            }
        }

        constexpr void decrement_for_negative(difference_type n)
            noexcept ((
                check == subrange_check::NEVER ||
                requires{{safe_decrement_for_negative(n)} noexcept;}
            ) && (
                check == subrange_check::ALWAYS ||
                requires{{unsafe_decrement_for_negative(n)} noexcept;}
            ))
            requires ((
                check == subrange_check::NEVER ||
                requires{{safe_decrement_for_negative(n)};}
            ) && (
                check == subrange_check::ALWAYS ||
                requires{{unsafe_decrement_for_negative(n)};}
            ))
        {
            if constexpr (check == subrange_check::NEVER) {
                unsafe_decrement_for_negative(n);
            } else if constexpr (check == subrange_check::ALWAYS) {
                safe_decrement_for_negative(n);
            } else {
                if consteval {
                    safe_decrement_for_negative(n);
                } else {
                    unsafe_decrement_for_negative(n);
                }
            }
        }

        constexpr void unsafe_increment_func()
            noexcept (requires{{step()(start())} noexcept;})
            requires (check != subrange_check::ALWAYS && requires{{step()(start())};})
        {
            step()(start());
            ++m_index;
        }

        constexpr void safe_increment_func()
            noexcept (requires{
                {empty()} noexcept;
                {step()(start())} noexcept;
            })
            requires (check != subrange_check::NEVER && requires{
                {empty()};
                {step()(start())};
            })
        {
            if (empty() || m_overflow < 0) {
                ++m_overflow;
            } else {
                step()(start());
                ++m_index;
            }
        }

    public:
        [[nodiscard]] constexpr difference_type ssize() const
            noexcept (requires{{remaining()} noexcept;})
            requires (subrange_empty<Step> && requires{{remaining()};})
        {
            return remaining();
        }

        [[nodiscard]] constexpr difference_type ssize() const
            noexcept (requires{{
                difference_type(math::div::ceil<
                    difference_type,
                    meta::unqualify<step_type>
                >{}(remaining(), step()))
            } noexcept -> meta::nothrow::convertible_to<difference_type>;})
            requires (!subrange_empty<Step> && requires{{
                difference_type(math::div::ceil<
                    difference_type,
                    meta::unqualify<step_type>
                >{}(remaining(), step()))
            };})
        {
            return difference_type(math::div::ceil<
                difference_type,
                meta::unqualify<step_type>
            >{}(remaining(), step()));
        }

        [[nodiscard]] constexpr size_type size() const
            noexcept (requires{{size_type(ssize())} noexcept;})
            requires (requires{{size_type(ssize())};})
        {
            return size_type(ssize());
        }

        constexpr void increment()
            noexcept ((
                check == subrange_check::NEVER ||
                requires{{safe_increment()} noexcept;}
            ) && (
                check == subrange_check::ALWAYS ||
                requires{{unsafe_increment()} noexcept;}
            ))
            requires (subrange_simple<Start, Step>)
        {
            if constexpr (check == subrange_check::NEVER) {
                unsafe_increment();
            } else if constexpr (check == subrange_check::ALWAYS) {
                safe_increment();
            } else {
                if consteval {
                    safe_increment();
                } else {
                    unsafe_increment();
                }
            }
        }

        constexpr void increment()
            noexcept (requires{{increment_by(step())} noexcept;})
            requires (subrange_linear<Start, Stop, Step>)
        {
            increment_by(step());
        }

        constexpr void increment()
            noexcept (requires{{increment_for_positive(step())} noexcept;} && (
                strictly_positive<Step> ||
                requires{{increment_for_negative(step())} noexcept;}
            ))
            requires (subrange_loop<Start, Stop, Step>)
        {
            if constexpr (strictly_positive<Step>) {
                increment_for_positive(step());
            } else {
                if (step() < 0) {
                    increment_for_negative(step());
                } else {
                    increment_for_positive(step());
                }
            }
        }

        constexpr void increment()
            noexcept ((
                check == subrange_check::NEVER ||
                requires{{safe_increment_func()} noexcept;}
            ) && (
                check == subrange_check::ALWAYS ||
                requires{{unsafe_increment_func()} noexcept;}
            ))
            requires (subrange_nonlinear<Start, Stop, Step>)
        {
            if constexpr (check == subrange_check::NEVER) {
                unsafe_increment_func();
            } else if constexpr (check == subrange_check::ALWAYS) {
                safe_increment_func();
            } else {
                if consteval {
                    safe_increment_func();
                } else {
                    unsafe_increment_func();
                }
            }
        }

        constexpr subrange& operator++()
            noexcept (requires{{increment()} noexcept;})
            requires (requires{{increment()};})
        {
            increment();
            return *this;
        }

        [[nodiscard]] constexpr copy operator++(int)
            noexcept (requires(copy tmp) {
                {begin()} noexcept;
                {tmp.increment()} noexcept;
            })
            requires (requires(copy tmp) {
                {begin()};
                {tmp.increment()};
            })
        {
            copy tmp = begin();
            tmp.increment();
            return tmp;
        }

        constexpr subrange& operator+=(difference_type n)
            noexcept (requires{{increment_by(n)} noexcept;})
            requires (subrange_empty<Step> && requires{{increment_by(n)};})
        {
            increment_by(n);
            return *this;
        }

        constexpr subrange& operator+=(difference_type n)
            noexcept (requires{{increment_by(n * step())} noexcept;})
            requires (!subrange_empty<Step> && requires{{increment_by(n * step())};})
        {
            increment_by(n * step());
            return *this;
        }

        [[nodiscard]] friend constexpr copy operator+(const subrange& self, difference_type n)
            noexcept (requires(copy tmp) {
                {self.begin()} noexcept;
                {tmp += n} noexcept;
            })
            requires (requires(copy tmp) {
                {self.begin()};
                {tmp += n};
            })
        {
            copy tmp = self.begin();
            tmp += n;
            return tmp;
        }

        [[nodiscard]] friend constexpr copy operator+(difference_type n, const subrange& self)
            noexcept (requires(copy tmp) {
                {self.begin()} noexcept;
                {tmp += n} noexcept;
            })
            requires (requires(copy tmp) {
                {self.begin()};
                {tmp += n};
            })
        {
            copy tmp = self.begin();
            tmp += n;
            return tmp;
        }

        constexpr void increment(difference_type n)
            noexcept (requires{{*this += n} noexcept;})
            requires (requires{{*this += n};})
        {
            *this += n;
        }

        constexpr void increment(difference_type n)
            noexcept (requires{
                {increment_for_negative(n)} noexcept;
                {increment_for_positive(n)} noexcept;
            })
            requires (subrange_empty<Step> && !requires{{*this += n};} && requires{
                {increment_for_negative(n)};
                {increment_for_positive(n)};
            })
        {
            if (n < 0) {
                increment_for_negative(n);
            } else {
                increment_for_positive(n);
            }
        }

        constexpr void increment(difference_type n)
            noexcept (requires{
                {increment_for_negative(n * step())} noexcept;
                {increment_for_positive(n * step())} noexcept;
            })
            requires (!subrange_empty<Step> && !requires{{*this += n};} && requires{
                {increment_for_negative(n * step())};
                {increment_for_positive(n * step())};
            })
        {
            n *= step();
            if (n < 0) {
                increment_for_negative(n);
            } else {
                increment_for_positive(n);
            }
        }

        constexpr void decrement()
            noexcept ((
                check == subrange_check::NEVER ||
                requires{{safe_decrement()} noexcept;}
            ) && (
                check == subrange_check::ALWAYS ||
                requires{{unsafe_decrement()} noexcept;}
            ))
            requires (subrange_empty<Step> && (
                check == subrange_check::NEVER ||
                requires{{safe_decrement()};}
            ) && (
                check == subrange_check::ALWAYS ||
                requires{{unsafe_decrement()};}
            ))
        {
            if constexpr (check == subrange_check::NEVER) {
                unsafe_decrement();
            } else if constexpr (check == subrange_check::ALWAYS) {
                safe_decrement();
            } else {
                if consteval {
                    safe_decrement();
                } else {
                    unsafe_decrement();
                }
            }
        }

        constexpr void decrement()
            noexcept (requires{{decrement_by(step())} noexcept;})
            requires (!subrange_empty<Step> && requires{{decrement_by(step())};})
        {
            decrement_by(step());
        }

        constexpr void decrement()
            noexcept (requires{{decrement_for_positive(step())};} && (
                strictly_positive<Step> ||
                requires{{decrement_for_negative(step())};
            }))
            requires (!subrange_empty<Step> && !requires{{decrement_by(step())};} && requires{
                {decrement_for_positive(step())};
            } && (
                strictly_positive<Step> ||
                requires{{decrement_for_negative(step())};
            }))
        {
            if constexpr (strictly_positive<Step>) {
                decrement_for_positive(step());
            } else {
                if (step() < 0) {
                    decrement_for_negative(step());
                } else {
                    decrement_for_positive(step());
                }
            }
        }

        constexpr subrange& operator--()
            noexcept (requires{{decrement()} noexcept;})
            requires (requires{{decrement()};})
        {
            decrement();
            return *this;
        }

        [[nodiscard]] constexpr copy operator--(int)
            noexcept (requires(copy tmp) {
                {begin()} noexcept;
                {tmp.decrement()} noexcept;
            })
            requires (requires(copy tmp) {
                {begin()};
                {tmp.decrement()};
            })
        {
            copy tmp = begin();
            tmp.decrement();
            return tmp;
        }

        constexpr subrange& operator-=(difference_type n)
            noexcept (requires{{decrement_by(n)} noexcept;})
            requires (subrange_empty<Step> && requires{{decrement_by(n)};})
        {
            decrement_by(n);
            return *this;
        }

        constexpr subrange& operator-=(difference_type n)
            noexcept (requires{{decrement_by(n * step())} noexcept;})
            requires (!subrange_empty<Step> && requires{{decrement_by(n * step())};})
        {
            decrement_by(n * step());
            return *this;
        }

        [[nodiscard]] constexpr copy operator-(difference_type n) const
            noexcept (requires(copy tmp) {
                {begin()} noexcept;
                {tmp -= n} noexcept;
            })
            requires (requires(copy tmp) {
                {begin()};
                {tmp -= n};
            })
        {
            copy tmp = begin();
            tmp -= n;
            return tmp;
        }

        constexpr void decrement(difference_type n)
            noexcept (requires{{*this -= n} noexcept;})
            requires (requires{{*this -= n};})
        {
            *this -= n;
        }

        constexpr void decrement(difference_type n)
            noexcept (requires{
                {decrement_for_negative(n)} noexcept;
                {decrement_for_positive(n)} noexcept;
            })
            requires (subrange_empty<Step> && !requires{{*this -= n};} && requires{
                {decrement_for_negative(n)};
                {decrement_for_positive(n)};
            })
        {
            if (n < 0) {
                decrement_for_negative(n);
            } else {
                decrement_for_positive(n);
            }
        }

        constexpr void decrement(difference_type n)
            noexcept (requires{
                {decrement_for_negative(n * step())} noexcept;
                {decrement_for_positive(n * step())} noexcept;
            })
            requires (!subrange_empty<Step> && !requires{{*this -= n};} && requires{
                {decrement_for_negative(n * step())};
                {decrement_for_positive(n * step())};
            })
        {
            n *= step();
            if (n < 0) {
                decrement_for_negative(n);
            } else {
                decrement_for_positive(n);
            }
        }
    };

    template <typename Start, typename Stop = subrange_tag, typename Step = subrange_tag>
    subrange(Start&&, Stop&& stop, Step&& step = {}) -> subrange<
        meta::remove_rvalue<Start>,
        meta::remove_rvalue<Stop>,
        meta::remove_rvalue<Step>
    >;

    template <typename T, extent Shape, typename Category>
    concept sequence_concept =
        range_concept<T> &&
        meta::unqualified<Category> &&
        meta::inherits<Category, std::input_iterator_tag>;

    template <typename T, extent Shape, typename Category>
        requires (sequence_concept<T, Shape, Category>)
    struct sequence;

    template <typename T, extent Shape, typename Category>
        requires (sequence_concept<T, Shape, Category>)
    struct sequence_iterator;

    /// TODO: sequence iterator dereference and subscript operations (plus those on
    /// the `sequence` container - including `front()` and `back()`) need to return
    /// other sequences to maintain symmetry with ranges.

    /// TODO: also, for `shape()` to be supported on all sequences, `meta::shape(c)`
    /// would have to be amended to return an empty shape for containers for which
    /// one cannot be deduced by either a `size()`, tuple destructuring, or a delegated
    /// `shape()` method.  That way, no matter what the container is, the sequence
    /// should be able to provide a shape, and thereby maintain symmetry with ranges.
    /// It's also crucial for supporting the buffer protocol in Python.
    /// -> Actually, what I'll do is just cache the shape in the control block, which
    /// will always store the `size()` as the first element so that it's always
    /// constant time on access.  That also means the shape will never be truly empty.


    /// TODO: the control block should store the shape as an optional, which gets
    /// lazily initialized the first time `shape()`, `size()`, or `empty()` is called
    /// on the sequence.  If that never happens, then the shape is never computed,
    /// which never risks an infinite loop if the container has no shape, and its
    /// iterators never terminate, as long as those methods are never called (which
    /// makes sense, and means the `sequence` is not limited to sized iterables or
    /// ).



    /* Sequences use the classic type erasure mechanism internally, consisting of a
    heap-allocated control block and an immutable void pointer to the underlying
    container.  If the container is provided as an lvalue, then the pointer will simply
    reference its current address, and no heap allocation will be performed for the
    container itself.  Otherwise, the container will be moved into a contiguous region
    immediately after the control block in order to consolidate allocations and improve
    cache locality.  In both cases, the final sequence represents a const view over the
    container, and does not allow for mutation of its elements.

    In addition to the container pointer, the control block also stores a family of
    immutable function pointers that implement the standard range interface for the
    type-erased container, according to the specified shape and iterator category.
    Each function pointer casts the container pointer back to its original,
    const-qualified type before invoking the corresponding operation, usually by
    delegating to a generic algorithm in the `bertrand::meta` or `bertrand::iter`
    namespaces.  Some operations may not be available for certain combinations of shape
    and category, in which case the corresponding function will be replaced with
    `None`, and will not contribute to the control block's overall size.

    The control block's lifetime (along with that of the container, if it was an
    rvalue) is gated by an atomic reference count, which allows the sequence to be
    cheaply copied and moved even if the underlying container is prohibitively
    expensive to copy, or is move-only.  Note that this means that copying a sequence
    does not yield an independent copy of the underlying container like it would for
    ranges - both sequences will instead reference the same container internally.
    This should be fine in practice since sequences are designed to be immutable, but
    unexpected behavior can occur if the original container was supplied as an lvalue,
    and is subsequently modified or destroyed while the sequence is still in use, or if
    user code casts away the constness of an element and mutates it directly.  If a
    deep copy is required, the user can implicitly convert the sequence to a container
    of the desired type using a range conversion operator, which triggers a loop over
    the sequence. */
    template <typename T, extent Shape, typename Category>
        requires (sequence_concept<T, Shape, Category>)
    struct sequence_control {
        template <size_t N>
        using reduce = std::conditional_t<
            (Shape.size() <= N),
            T,
            sequence<T, Shape.template reduce<N>(), Category>
        >;

        using dtor_ptr = void(*)(sequence_control*);
        using data_ptr = std::conditional_t<
            meta::inherits<Category, std::contiguous_iterator_tag>,
            meta::as_pointer<T>(*)(sequence_control*),
            NoneType
        >;
        using subscript_ptr = T(*)(sequence_control*, ssize_t);
        using begin_ptr = sequence_iterator<T, Shape, Category>(*)(sequence_control*);
        using end_ptr = sequence_iterator<T, Shape, Category>(*)(sequence_control*);
        using iter_copy_ptr = void(*)(
            sequence_iterator<T, Shape, Category>&,
            const sequence_iterator<T, Shape, Category>&
        );
        using iter_assign_ptr = void(*)(
            sequence_iterator<T, Shape, Category>&,
            const sequence_iterator<T, Shape, Category>&
        );
        using iter_dtor_ptr = void(*)(sequence_iterator<T, Shape, Category>&);
        using iter_deref_ptr = reduce<1>(*)(const sequence_iterator<T, Shape, Category>&);
        using iter_subscript_ptr = std::conditional_t<
            meta::inherits<Category, std::random_access_iterator_tag>,
            reduce<1>(*)(const sequence_iterator<T, Shape, Category>&, ssize_t),
            NoneType
        >;
        using iter_increment_ptr = void(*)(sequence_iterator<T, Shape, Category>&);
        using iter_decrement_ptr = std::conditional_t<
            meta::inherits<Category, std::bidirectional_iterator_tag>,
            void(*)(sequence_iterator<T, Shape, Category>&),
            NoneType
        >;
        using iter_advance_ptr = std::conditional_t<
            meta::inherits<Category, std::random_access_iterator_tag>,
            void(*)(sequence_iterator<T, Shape, Category>&, ssize_t),
            NoneType
        >;
        using iter_retreat_ptr = iter_advance_ptr;
        using iter_distance_ptr = std::conditional_t<
            meta::inherits<Category, std::random_access_iterator_tag>,
            ssize_t(*)(
                const sequence_iterator<T, Shape, Category>&,
                const sequence_iterator<T, Shape, Category>&
            ),
            NoneType
        >;
        using iter_compare_ptr = std::strong_ordering(*)(
            const sequence_iterator<T, Shape, Category>&,
            const sequence_iterator<T, Shape, Category>&
        );

        std::atomic<size_t> refcount = 1;
        const void* const container;
        Optional<meta::unqualify<decltype(Shape)>> shape;
        const dtor_ptr dtor;
        [[no_unique_address]] const data_ptr data;
        const subscript_ptr subscript;
        const begin_ptr begin;
        const end_ptr end;
        const iter_copy_ptr iter_copy;
        const iter_assign_ptr iter_assign;
        const iter_dtor_ptr iter_dtor;
        const iter_deref_ptr iter_deref;
        [[no_unique_address]] const iter_subscript_ptr iter_subscript;
        const iter_increment_ptr iter_increment;
        [[no_unique_address]] const iter_decrement_ptr iter_decrement;
        [[no_unique_address]] const iter_advance_ptr iter_advance;
        [[no_unique_address]] const iter_retreat_ptr iter_retreat;
        [[no_unique_address]] const iter_distance_ptr iter_distance;
        const iter_compare_ptr iter_compare;

        template <typename C>
        static constexpr data_ptr get_data() noexcept {
            if constexpr (meta::inherits<Category, std::contiguous_iterator_tag>) {
                return &data_fn<C>;
            } else {
                return {};
            }
        }

        template <typename C>
        static constexpr iter_decrement_ptr get_iter_decrement() noexcept {
            if constexpr (meta::inherits<Category, std::bidirectional_iterator_tag>) {
                return &iter_decrement_fn<C>;
            } else {
                return {};
            }
        }

        template <typename C>
        static constexpr iter_subscript_ptr get_iter_subscript() noexcept {
            if constexpr (meta::inherits<Category, std::random_access_iterator_tag>) {
                return &iter_subscript_fn<C>;
            } else {
                return {};
            }
        }

        template <typename C>
        static constexpr iter_subscript_ptr get_iter_advance() noexcept {
            if constexpr (meta::inherits<Category, std::random_access_iterator_tag>) {
                return &iter_advance_fn<C>;
            } else {
                return {};
            }
        }

        template <typename C>
        static constexpr iter_subscript_ptr get_iter_retreat() noexcept {
            if constexpr (meta::inherits<Category, std::random_access_iterator_tag>) {
                return &iter_retreat_fn<C>;
            } else {
                return {};
            }
        }

        template <typename C>
        static constexpr iter_subscript_ptr get_iter_distance() noexcept {
            if constexpr (meta::inherits<Category, std::random_access_iterator_tag>) {
                return &iter_distance_fn<C>;
            } else {
                return {};
            }
        }

        template <typename C>
            requires (
                meta::lvalue<C> &&
                meta::yields<meta::as_const<C>, T> &&
                meta::shape_returns<meta::unqualify<decltype(Shape)>, C>
            )
        [[nodiscard]] static constexpr sequence_control* create(C&& c) {
            // if the container is an lvalue, then just store a pointer to it within
            // the control block
            if constexpr (meta::lvalue<C>) {
                return new sequence_control{
                    .container = std::addressof(c),
                    .dtor = &dtor_fn<C>,
                    .data = get_data<C>(),
                    .subscript = &subscript_fn<C>,
                    .begin = &begin_fn<C>,
                    .end = &end_fn<C>,
                    .iter_copy = &iter_copy_fn<C>,
                    .iter_assign = &iter_assign_fn<C>,
                    .iter_dtor = &iter_dtor_fn<C>,
                    .iter_deref = &iter_deref_fn<C>,
                    .iter_subscript = get_iter_subscript<C>(),
                    .iter_increment = &iter_increment_fn<C>,
                    .iter_decrement = get_iter_decrement<C>(),
                    .iter_advance = get_iter_advance<C>(),
                    .iter_retreat = get_iter_retreat<C>(),
                    .iter_distance = get_iter_distance<C>(),
                    .iter_compare = &iter_compare_fn<C>,
                };

            // otherwise, it is possible to consolidate allocations such that the
            // container is stored immediately after the control block
            } else {
                void* control = ::operator new(
                    sizeof(sequence_control) + sizeof(meta::unqualify<C>)
                );
                if (control == nullptr) {
                    throw MemoryError();
                }
                void* container = static_cast<sequence_control*>(control) + 1;
                try {
                    new (container) meta::unqualify<C>(std::forward<C>(c));
                } catch (...) {
                    ::operator delete(control);
                    throw;
                }
                new (control) sequence_control {
                    .container = static_cast<const void*>(container),
                    .dtor = &dtor_fn<C>,
                    .data = get_data<C>(),
                    .subscript = &subscript_fn<C>,
                    .begin = &begin_fn<C>,
                    .end = &end_fn<C>,
                    .iter_copy = &iter_copy_fn<C>,
                    .iter_assign = &iter_assign_fn<C>,
                    .iter_dtor = &iter_dtor_fn<C>,
                    .iter_deref = &iter_deref_fn<C>,
                    .iter_subscript = get_iter_subscript<C>(),
                    .iter_increment = &iter_increment_fn<C>,
                    .iter_decrement = get_iter_decrement<C>(),
                    .iter_advance = get_iter_advance<C>(),
                    .iter_retreat = get_iter_retreat<C>(),
                    .iter_distance = get_iter_distance<C>(),
                    .iter_compare = &iter_compare_fn<C>,
                };
                return static_cast<sequence_control*>(control);
            }
        }

        constexpr void incref() noexcept {
            refcount.fetch_add(1, std::memory_order_relaxed);
        }

        constexpr void decref() noexcept {
            if (refcount.fetch_sub(1, std::memory_order_release) == 1) {
                std::atomic_thread_fence(std::memory_order_acquire);
                dtor(this);
            }
        }

        template <typename C>
        using Container = meta::as_const<C>;

        template <typename C>
        static constexpr void dtor_fn(sequence_control* control) {
            // if the container is an lvalue, then only the control block needs to be
            // deallocated
            if constexpr (meta::lvalue<C>) {
                delete control;

            // otherwise, the container buffer immediately follows the control block,
            // and must also be destroyed before deallocating
            } else {
                std::destroy_at(static_cast<const meta::unqualify<C>*>(control->container));
                ::operator delete(control);
            }
        }

        template <typename C> requires (meta::inherits<Category, std::contiguous_iterator_tag>)
        static constexpr meta::as_pointer<T> data_fn(sequence_control* control) {
            return meta::data(*static_cast<meta::as_pointer<Container<C>>>(control->container));
        }

        template <typename C>
        static constexpr T subscript_fn(sequence_control* control, ssize_t n);

        template <typename C>
        using Begin = meta::unqualify<meta::begin_type<Container<C>>>;
        template <typename C>
        using End = meta::unqualify<meta::end_type<Container<C>>>;

        template <typename C>
        static constexpr sequence_iterator<T, Shape, Category> begin_fn(sequence_control* control) {
            // if the category is at least forward, then the begin and end iterators
            // are guaranteed to be the same type, and are stored separately to
            // maintain multi-pass requirements
            if constexpr (meta::inherits<Category, std::forward_iterator_tag>) {
                sequence_iterator<T, Shape, Category> result {
                    .control = control,
                    .iter = new Begin(std::ranges::begin(
                        *static_cast<meta::as_pointer<Container<C>>>(control->container)
                    ))
                };
                if (result.iter == nullptr) {
                    throw MemoryError();
                }
                control->incref();
                return result;

            // otherwise, the begin and end iterators may be different types that are
            // stored together, and can be allocated at the same time
            } else {
                sequence_iterator<T, Shape, Category> result {
                    .control = control,
                    .iter = ::operator new(sizeof(Begin<C>) + sizeof(End<C>)),
                    .sentinel = static_cast<Begin<C>*>(result.iter) + 1
                };
                if (result.iter == nullptr) {
                    throw MemoryError();
                }
                try {
                    new (result.iter) Begin(std::ranges::begin(
                        *static_cast<meta::as_pointer<Container<C>>>(control->container)
                    ));
                    new (result.sentinel) End(std::ranges::end(
                        *static_cast<meta::as_pointer<Container<C>>>(control->container)
                    ));
                } catch (...) {
                    ::operator delete(result.iter);
                    throw;
                }
                control->incref();
                return result;
            }
        }

        template <typename C>
        static constexpr sequence_iterator<T, Shape, Category> end_fn(sequence_control* control) {
            // if the category is at least forward, then the end iterator uses the
            // same layout as the begin iterator
            if constexpr (meta::inherits<Category, std::forward_iterator_tag>) {
                sequence_iterator<T, Shape, Category> result {
                    .control = control,
                    .iter = new End<C>(std::ranges::end(
                        *static_cast<meta::as_pointer<Container<C>>>(control->container)
                    ))
                };
                if (result.iter == nullptr) {
                    throw MemoryError();
                }
                control->incref();
                return result;

            // otherwise, the end iterator is trivially represented by a
            // default-constructed sequence iterator, which maintains the
            // `common_range` requirement
            } else {
                return {};
            }
        }

        /// NOTE: assumes `other` is not trivial
        template <typename C>
        static constexpr void iter_copy_fn(
            sequence_iterator<T, Shape, Category>& self,
            const sequence_iterator<T, Shape, Category>& other
        ) {
            // if the category is at least forward, then the begin and end iterators
            // are guaranteed to be the same type, and both invoke the same copy logic
            if constexpr (meta::inherits<Category, std::forward_iterator_tag>) {
                self.control = other.control;
                self.iter = new Begin<C>(
                    *static_cast<meta::as_pointer<Begin<C>>>(other.iter)
                );
                if (self.iter == nullptr) {
                    throw MemoryError();
                }
                self.control->incref();

            // otherwise, we need to only copy if the other iterator is not a trivial
            // sentinel, and apply the same allocation strategy as the constructor
            } else {
                if (other.iter != nullptr) {
                    self.control = other.control;
                    self.iter = ::operator new(sizeof(Begin<C>) + sizeof(End<C>));
                    self.sentinel = static_cast<Begin<C>*>(self.iter) + 1;
                    if (self.iter == nullptr) {
                        throw MemoryError();
                    }
                    try {
                        new (self.iter) Begin<C>(
                            *static_cast<meta::as_pointer<Begin<C>>>(other.iter)
                        );
                        new (self.sentinel) End<C>(
                            *static_cast<meta::as_pointer<End<C>>>(other.sentinel)
                        );
                    } catch (...) {
                        ::operator delete(self.iter);
                        throw;
                    }
                    self.control->incref();
                }
            }
        }

        /// NOTE: assumes `self` and `other` are not the same
        template <typename C>
        static constexpr void iter_assign_fn(
            sequence_iterator<T, Shape, Category>& self,
            const sequence_iterator<T, Shape, Category>& other
        ) {
            // either iterator may be in a trivial state, giving 4 cases:
            //      1.  `self` and `other` are both trivial => do nothing
            //      3.  `self` is not trivial, `other` is => deallocate
            //      2.  `self` is trivial, `other` is not => allocate and copy
            //      4.  `self` and `other` are both not trivial => direct assign
            if (other.iter == nullptr) {
                if (self.iter != nullptr) {
                    iter_dtor_fn<C>(self);
                }
            } else if (self.iter == nullptr) {
                iter_copy_fn<C>(self, other);
            } else {
                *static_cast<meta::as_pointer<Begin<C>>>(self.iter) =
                    *static_cast<meta::as_pointer<Begin<C>>>(other.iter);
                if constexpr (!meta::inherits<Category, std::forward_iterator_tag>) {
                    *static_cast<meta::as_pointer<End<C>>>(self.sentinel) =
                        *static_cast<meta::as_pointer<End<C>>>(other.sentinel);
                }
                self.control->decref();
                self.control = other.control;
                self.control->incref();
            }
        }

        /// NOTE: assumes `self` is not trivial
        template <typename C>
        static constexpr void iter_dtor_fn(sequence_iterator<T, Shape, Category>& self) {
            if constexpr (meta::inherits<Category, std::forward_iterator_tag>) {
                delete static_cast<meta::as_pointer<Begin<C>>>(self.iter);
                self.iter = nullptr;
                self.control->decref();
                self.control = nullptr;
            } else {
                std::destroy_at(static_cast<Begin<C>*>(self.iter));
                std::destroy_at(static_cast<End<C>*>(self.sentinel));
                ::operator delete(self.iter);
                self.iter = nullptr;
                self.control->decref();
                self.control = nullptr;
            }
        }

        /// NOTE: assumes `self` is not trivial
        template <typename C>
        static constexpr reduce<1> iter_deref_fn(sequence_iterator<T, Shape, Category>& self) {
            return **static_cast<meta::as_pointer<Begin<C>>>(self.iter);
        }

        /// NOTE: assumes `self` is not trivial
        template <typename C> requires (meta::inherits<Category, std::random_access_iterator_tag>)
        static constexpr reduce<1> iter_subscript_fn(
            sequence_iterator<T, Shape, Category>& self,
            ssize_t n
        ) {
            return (*static_cast<meta::as_pointer<Begin<C>>>(self.iter))[n];
        }

        /// NOTE: assumes `self` is not trivial
        template <typename C>
        static constexpr void iter_increment_fn(sequence_iterator<T, Shape, Category>& self) {
            ++*static_cast<meta::as_pointer<Begin<C>>>(self.iter);
        }

        /// NOTE: assumes `self` is not trivial
        template <typename C> requires (meta::inherits<Category, std::bidirectional_iterator_tag>)
        static constexpr void iter_decrement_fn(sequence_iterator<T, Shape, Category>& self) {
            --*static_cast<meta::as_pointer<Begin<C>>>(self.iter);
        }

        /// NOTE: assumes `self` is not trivial
        template <typename C> requires (meta::inherits<Category, std::random_access_iterator_tag>)
        static constexpr void iter_advance_fn(
            sequence_iterator<T, Shape, Category>& self,
            ssize_t n
        ) {
            *static_cast<meta::as_pointer<Begin<C>>>(self.iter) += n;
        }

        /// NOTE: assumes `self` is not trivial
        template <typename C> requires (meta::inherits<Category, std::random_access_iterator_tag>)
        static constexpr void iter_retreat_fn(
            sequence_iterator<T, Shape, Category>& self,
            ssize_t n
        ) {
            *static_cast<meta::as_pointer<Begin<C>>>(self.iter) -= n;
        }

        /// NOTE: assumes `self` and `other` are not trivial
        template <typename C> requires (meta::inherits<Category, std::random_access_iterator_tag>)
        static constexpr ssize_t iter_distance_fn(
            const sequence_iterator<T, Shape, Category>& lhs,
            const sequence_iterator<T, Shape, Category>& rhs
        ) {
            return *static_cast<meta::as_pointer<Begin<C>>>(lhs.iter) -
                *static_cast<meta::as_pointer<Begin<C>>>(rhs.iter);
        }

        /// NOTE: for forward iterators and higher, assumes `self` and `other` are not
        /// trivial
        template <typename C>
        static constexpr std::strong_ordering iter_compare_fn(
            const sequence_iterator<T, Shape, Category>& lhs,
            const sequence_iterator<T, Shape, Category>& rhs
        ) {
            if constexpr (meta::inherits<Category, std::forward_iterator_tag>) {
                return iter_compare_impl(
                    *static_cast<meta::as_pointer<Begin<C>>>(lhs.iter),
                    *static_cast<meta::as_pointer<Begin<C>>>(rhs.iter)
                );
            } else {
                if (lhs.iter == rhs.iter) {
                    return std::strong_ordering::equal;
                }
                if (lhs.iter == nullptr) {
                    return iter_compare_impl(
                        *static_cast<meta::as_pointer<Begin<C>>>(rhs.sentinel),
                        *static_cast<meta::as_pointer<End<C>>>(rhs.iter)
                    );
                }
                if (rhs.iter == nullptr) {
                    return iter_compare_impl(
                        *static_cast<meta::as_pointer<Begin<C>>>(lhs.iter),
                        *static_cast<meta::as_pointer<End<C>>>(lhs.sentinel)
                    );
                }
                return std::strong_ordering::less;  // converted into `false` in sequence_iterator
            }
        }

        template <typename LHS, typename RHS>
        static constexpr std::strong_ordering iter_compare_impl(const LHS& lhs, const RHS& rhs) {
            if constexpr (requires{{lhs <=> rhs};}) {
                return *lhs <=> *rhs;
            } else if constexpr (requires{{lhs < rhs}; {lhs > rhs};}) {
                if (*lhs < *rhs) {
                    return std::strong_ordering::less;
                }
                if (*lhs > *rhs) {
                    return std::strong_ordering::greater;
                }
                return std::strong_ordering::equal;
            } else {
                if (*lhs == *rhs) {
                    return std::strong_ordering::equal;
                }
                return std::strong_ordering::less;  // converted into `false` in sequence_iterator
            }
        }
    };

    /* A const iterator over a type-erased sequence, as implemented via the
    `sequence_control` block.  Iterators come in two varieties depending on the
    specified iterator category.

    If the category is `input_iterator_tag` (or an equivalent), then the iterator will
    be capable of modeling non-common ranges, where the begin and end types may differ.
    In that case, it will store void pointers to both the begin and end iterators
    internally, and the sentinel for the overall sequence will be represented by a
    default-constructed iterator where both pointers are null.  Comparison operations
    will always compare the internal iterators using the corresponding function pointer
    from the control block, after checking for trivial sentinels.

    If the category is at least `forward_iterator_tag`, then the underlying range must
    be a common range, and both iterators will store a single void pointer to their
    corresponding begin or end iterator.  Comparison operations can then directly
    compare the internal iterators without needing to check for trivial sentinels, and
    may permit three-way comparisons if the category is at least
    `random_access_iterator_tag`.

    Note that because of the type erasure, the overall sequence will always trivially
    be both a borrowed and common range, even if the underlying container is not.  This
    also means that the iterator category for the underlying sequence may not exactly
    match the category specified by its template signature, and will always be at least
    `std::forward_iterator_tag`, owing to the common range guarantee.  Similarly, if
    the category is specified as contiguous by the template signature, but its shape
    has more than 1 dimension, then the category will be downgraded to
    `std::random_access_tag` instead. */
    template <typename T, extent Shape, typename Category>
        requires (sequence_concept<T, Shape, Category>)
    struct sequence_iterator {
        using iterator_category = std::forward_iterator_tag;
        using difference_type = ssize_t;
        using value_type = meta::remove_reference<T>;
        using reference = meta::as_lvalue<T>;
        using pointer = meta::as_pointer<T>;

        sequence_control<T, Shape, Category>* control = nullptr;
        void* iter = nullptr;
        void* sentinel = nullptr;

        [[nodiscard]] constexpr sequence_iterator() noexcept = default;

        [[nodiscard]] constexpr sequence_iterator(const sequence_iterator& other) :
            control(other.control)
        {
            if (control != nullptr) {
                control->iter_copy(other);
            }
        }

        [[nodiscard]] constexpr sequence_iterator(sequence_iterator&& other) noexcept :
            control(other.control),
            iter(other.iter),
            sentinel(other.sentinel)
        {
            other.control = nullptr;
            other.iter = nullptr;
            other.sentinel = nullptr;
        }

        constexpr sequence_iterator& operator=(const sequence_iterator& other) {
            if (this != &other) {
                control->iter_assign(*this, other);
            }
            return *this;
        }

        constexpr sequence_iterator& operator=(sequence_iterator&& other) noexcept {
            if (this != &other) {
                if (control) {
                    control->iter_dtor(*this);
                }
                control = other.control;
                iter = other.iter;
                sentinel = other.sentinel;
                other.control = nullptr;
                other.iter = nullptr;
                other.sentinel = nullptr;
            }
            return *this;
        }

        constexpr ~sequence_iterator() {
            if (control) {
                control->iter_dtor(*this);
            }
        }

        constexpr void swap(sequence_iterator& other) noexcept {
            std::swap(control, other.control);
            std::swap(iter, other.iter);
            std::swap(sentinel, other.sentinel);
        }

        [[nodiscard]] constexpr T operator*() const {
            return control->iter_deref(*this);
        }

        [[nodiscard]] constexpr auto operator->() const {
            return impl::arrow(control->iter_deref(*this));
        }

        constexpr sequence_iterator& operator++() {
            control->iter_increment(*this);
            return *this;
        }

        [[nodiscard]] constexpr sequence_iterator operator++(int) {
            sequence_iterator tmp;
            control->iter_copy(tmp, *this);
            control->iter_increment(tmp);
            return tmp;
        }

        [[nodiscard]] constexpr bool operator==(const sequence_iterator& other) const {
            return control->iter_compare(*this, other) == 0;
        }

        [[nodiscard]] constexpr bool operator!=(const sequence_iterator& other) const {
            return control->iter_compare(*this, other) != 0;
        }

        [[nodiscard]] constexpr auto operator<=>(const sequence_iterator& other) const
            requires (meta::inherits<Category, std::random_access_iterator_tag>)
        {
            return control->iter_compare(*this, other);
        }
    };
    template <typename T, extent Shape, typename Category>
        requires (
            sequence_concept<T, Shape, Category> &&
            meta::inherits<Category, std::forward_iterator_tag>
        )
    struct sequence_iterator<T, Shape, Category> {
        using iterator_category = std::conditional_t<
            meta::inherits<Category, std::contiguous_iterator_tag>,
            std::conditional_t<
                Shape.size() <= 1,
                std::contiguous_iterator_tag,
                std::random_access_iterator_tag
            >,
            Category
        >;
        using difference_type = ssize_t;
        using value_type = meta::remove_reference<T>;
        using reference = meta::as_lvalue<T>;
        using pointer = meta::as_pointer<T>;

        sequence_control<T, Shape, Category>* control = nullptr;
        void* iter = nullptr;

        [[nodiscard]] constexpr sequence_iterator() noexcept = default;

        [[nodiscard]] constexpr sequence_iterator(const sequence_iterator& other) :
            control(other.control)
        {
            if (control != nullptr) {
                control->iter_copy(*this, other);
            }
        }

        [[nodiscard]] constexpr sequence_iterator(sequence_iterator&& other) noexcept :
            control(other.control),
            iter(other.iter)
        {
            other.control = nullptr;
            other.iter = nullptr;
        }

        constexpr sequence_iterator& operator=(const sequence_iterator& other) {
            if (this != &other) {
                control->iter_assign(*this, other);
            }
            return *this;
        }

        constexpr sequence_iterator& operator=(sequence_iterator&& other) noexcept {
            if (this != &other) {
                if (control) {
                    control->iter_dtor(*this);
                }
                control = other.control;
                iter = other.iter;
                other.control = nullptr;
                other.iter = nullptr;
            }
            return *this;
        }

        constexpr ~sequence_iterator() {
            if (control) {
                control->iter_dtor(*this);
            }
        }

        constexpr void swap(sequence_iterator& other) noexcept {
            std::swap(control, other.control);
            std::swap(iter, other.iter);
        }

        [[nodiscard]] constexpr T operator*() const {
            return control->iter_deref(*this);
        }

        [[nodiscard]] constexpr auto operator->() const {
            return impl::arrow(control->iter_deref(*this));
        }

        [[nodiscard]] constexpr T operator[](difference_type n) const
            requires (meta::inherits<Category, std::random_access_iterator_tag>)
        {
            return control->iter_subscript(*this, n);
        }

        constexpr sequence_iterator& operator++() {
            control->iter_increment(*this);
            return *this;
        }

        [[nodiscard]] constexpr sequence_iterator operator++(int) {
            sequence_iterator tmp;
            control->iter_copy(tmp, *this);
            control->iter_increment(tmp);
            return tmp;
        }

        constexpr sequence_iterator& operator--()
            requires (meta::inherits<Category, std::bidirectional_iterator_tag>)
        {
            control->iter_decrement(*this);
            return *this;
        }

        [[nodiscard]] constexpr sequence_iterator operator--(int)
            requires (meta::inherits<Category, std::bidirectional_iterator_tag>)
        {
            sequence_iterator tmp;
            control->iter_copy(tmp, *this);
            control->iter_decrement(tmp);
            return tmp;
        }

        constexpr sequence_iterator& operator+=(difference_type n)
            requires (meta::inherits<Category, std::random_access_iterator_tag>)
        {
            control->iter_advance(*this, n);
            return *this;
        }

        [[nodiscard]] friend constexpr sequence_iterator operator+(
            const sequence_iterator& self,
            difference_type n
        )
            requires (meta::inherits<Category, std::random_access_iterator_tag>)
        {
            sequence_iterator tmp;
            self.control->iter_copy(tmp, self);
            self.control->iter_advance(tmp, n);
            return tmp;
        }

        [[nodiscard]] friend constexpr sequence_iterator operator+(
            difference_type n,
            const sequence_iterator& self
        )
            requires (meta::inherits<Category, std::random_access_iterator_tag>)
        {
            sequence_iterator tmp;
            self.control->iter_copy(tmp, self);
            self.control->iter_advance(tmp, n);
            return tmp;
        }

        [[nodiscard]] constexpr sequence_iterator& operator-=(difference_type n)
            requires (meta::inherits<Category, std::random_access_iterator_tag>)
        {
            control->iter_retreat(*this, n);
            return *this;
        }

        [[nodiscard]] constexpr sequence_iterator operator-(difference_type n) const
            requires (meta::inherits<Category, std::random_access_iterator_tag>)
        {
            sequence_iterator tmp;
            control->iter_copy(tmp, *this);
            control->iter_retreat(tmp, n);
            return tmp;
        }

        [[nodiscard]] constexpr difference_type operator-(const sequence_iterator& other) const
            requires (meta::inherits<Category, std::random_access_iterator_tag>)
        {
            return control->iter_distance(*this, other);
        }

        [[nodiscard]] constexpr bool operator==(const sequence_iterator& other) const {
            return control->iter_compare(*this, other) == 0;
        }

        [[nodiscard]] constexpr bool operator!=(const sequence_iterator& other) const {
            return control->iter_compare(*this, other) != 0;
        }

        [[nodiscard]] constexpr auto operator<=>(const sequence_iterator& other) const
            requires (meta::inherits<Category, std::random_access_iterator_tag>)
        {
            return control->iter_compare(*this, other);
        }
    };

    /// TODO: sequences now only need to consider shapes where at least the number of
    /// dimensions are known at compile time.  This will simplify a lot of the logic
    /// here, and also avoids the need for size() and empty() function pointers, since
    /// they'll just check the first dimension of the shape (or 1 if 0-dimensional).
    /// Also, the iterator type can probably be moved into the sequence class itself,
    /// since these types have been simplified so heavily.

    /// TODO: then, I just need to write the logic to reduce the shape when indexing or
    /// iterating, and I can maybe provide up to 4 indexing operators to optimize
    /// multidimensional indexing, and reduce the overall number of allocations needed.
    /// `iter::at{}` will fill in any details by concatenating the results.  I would
    /// only compile all 4 pointers if the shape has 4 or more dimensions.


    /* The public-facing sequence type comes in two flavors depending on whether the
    dimensionality of its shape is known at compile time.  If so, that information will
    be carried over (in reduced form) to the index and yield types, allowing the
    compiler to optimize accordingly.  If not, then the scalar case cannot be
    statically differentiated from higher-dimensional shapes, meaning the index and
    yield types must be generalized to handle both cases.

    In particular, this means that the dereference operator in the dynamic case (which
    always yields a value of type `T`) is only valid when the shape has zero dimensions
    (i.e. the sequence is a scalar).  If a sequence with 1 or more dimensions is
    dereferenced, it will result in a `TypeError` at runtime, which must be avoided by
    checking the number of dimensions in its shape (e.g. `seq.shape().empty()`).  For
    sequences of static shape, the `TypeError` will be promoted to a compilation error
    instead, enforcing correctness at compile time. */
    template <typename T, extent Shape, typename Category>
        requires (sequence_concept<T, Shape, Category>)
    struct sequence {
        sequence_control<T, Shape, Category>* control = nullptr;

        [[nodiscard]] constexpr sequence() noexcept = default;

        template <typename C> requires (meta::yields<meta::as_const<C>, T>)
        [[nodiscard]] constexpr sequence(C&& c) :
            control(sequence_control<T, Shape, Category>::create(std::forward<C>(c)))
        {}

        [[nodiscard]] constexpr sequence(const sequence& other) noexcept : control(other.control) {
            if (control) {
                control->incref();
            }
        }

        [[nodiscard]] constexpr sequence(sequence&& other) noexcept : control(other.control) {
            other.control = nullptr;
        }

        constexpr sequence& operator=(const sequence& other) {
            if (this != &other) {
                if (control) {
                    control->decref();
                }
                control = other.control;
                if (control) {
                    control->incref();
                }
            }
            return *this;
        }

        constexpr sequence& operator=(sequence&& other) {
            if (this != &other) {
                if (control) {
                    control->decref();
                }
                control = other.control;
                other.control = nullptr;
            }
            return *this;
        }

        constexpr ~sequence() {
            if (control) {
                control->decref();
            }
        }

        constexpr void swap(sequence& other) noexcept {
            std::swap(control, other.control);
        }

        [[nodiscard]] constexpr meta::as_pointer<T> data() const
            requires (meta::inherits<Category, std::contiguous_iterator_tag>)
        {
            return control->data(control);
        }

        /// TODO: I need to store a function pointer to retrieve the shape when needed.

        [[nodiscard]] constexpr meta::as_const_ref<decltype(Shape)> shape() const noexcept {
            // if (control->shape == None) {
            //     // control->shape = meta::shape()
            // }
            return control->shape;
        }

        [[nodiscard]] constexpr size_t size() const {
            return size_t(control->size(control));
        }

        [[nodiscard]] constexpr ssize_t ssize() const {
            return control->size(control);
        }

        [[nodiscard]] constexpr bool empty() const {
            return control->empty(control);
        }

        /// TODO: front() and back().  Also, the subscript operator should not return
        /// `T` directly, but rather another `sequence<T>` with reduced shape, and
        /// only `T` if the shape is fully indexed.
        /// -> How would `back()` be standardized in this context?  Is it even
        /// possible?  It might require a full traversal of the range to get there.


        /// TODO: the subscript operator should have 4 overloads to optimize for
        /// multidimensional indexing since the number of dimensions is known at
        /// compile time.

        [[nodiscard]] constexpr decltype(auto) operator[](ssize_t i) const {
            return (control->subscript(control, i));
        }

        [[nodiscard]] constexpr auto begin() const {
            return control->begin(control);
        }

        [[nodiscard]] constexpr auto end() const {
            return control->end(control);
        }
    };

    template <typename T, extent Shape>
    struct _sequence_type { using type = meta::remove_rvalue<T>; };
    template <typename T, extent Shape> requires (!Shape.empty())
    struct _sequence_type<T, Shape> :
        _sequence_type<meta::yield_type<T>, Shape.template reduce<1>()>
    {};
    template <typename T, extent Shape>
    using sequence_type = _sequence_type<T, Shape>::type;

    template <meta::iterable C>
    using sequence_category = std::conditional_t<
        std::same_as<
            meta::begin_type<meta::as_const<C>>,
            meta::end_type<meta::as_const<C>>
        >,
        std::conditional_t<
            meta::has_data<meta::as_const<C>> && meta::inherits<
                meta::iterator_category<meta::as_const<C>>,
                std::random_access_iterator_tag
            >,
            std::contiguous_iterator_tag,
            meta::iterator_category<meta::begin_type<meta::as_const<C>>>
        >,
        std::input_iterator_tag
    >;

}


namespace iter {

    /* A generalized `swap()` operator that allows any type in the `bertrand::iter`
    namespace that exposes a `.swap()` member method to be used in conjunction with
    `std::ranges::swap()`. */
    template <typename T>
    constexpr void swap(T& lhs, T& rhs)
        noexcept (requires{{lhs.swap(rhs)} noexcept;})
        requires (requires{{lhs.swap(rhs)};})
    {
        lhs.swap(rhs);
    }

    template <impl::range_concept C>
    struct range;

    template <typename C>
    range(C&&) -> range<meta::remove_rvalue<C>>;

    template <typename Start, typename Stop = impl::iota_tag, typename Step = impl::iota_tag>
        requires (!meta::iterator<Start>)
    range(Start&&, Stop&&, Step&& = {}) -> range<impl::iota<
        meta::remove_rvalue<Start>,
        meta::remove_rvalue<Stop>,
        meta::remove_rvalue<Step>
    >>;

    template <typename Start, typename Stop = impl::subrange_tag, typename Step = impl::subrange_tag>
        requires (meta::iterator<Start>)
    range(Start&&, Stop&&, Step&& = {}) -> range<impl::subrange<
        meta::remove_rvalue<Start>,
        meta::remove_rvalue<Stop>,
        meta::remove_rvalue<Step>
    >>;

    /* A special case of `range` that allows it to adapt to tuple-like types that are
    not otherwise iterable.  This works by dispatching to a reference array that gets
    populated when the range is constructed as long as the tuple contains only a single
    type, or a static vtable filled with function pointers that extract the
    corresponding value when called.  In the latter case, the return type will be
    promoted to a `Union` in order to model heterogenous tuples. */
    template <impl::range_concept C> requires (!meta::iterable<C> && meta::tuple_like<C>)
    struct range<C> : range<impl::tuple_range<C>> {
        using range<impl::tuple_range<C>>::range;
        using range<impl::tuple_range<C>>::operator=;
    };

    /* A special case of `range` that contains only a single, non-iterable element.
    Default-constructing this range will create a range of zero elements instead. */
    template <impl::range_concept C> requires (!meta::iterable<C> && !meta::tuple_like<C>)
    struct range<C> : range<impl::scalar_range<C>> {
        using range<impl::scalar_range<C>>::range;
        using range<impl::scalar_range<C>>::operator=;
    };

    /* A type-erased range that forgets the underlying container type, and presents only as
    a sequence of some type.

    Because of their use as monadic expression templates, ranges can quickly become deeply
    nested and brittle, especially when used with conditionals which may return slightly
    different types for each branch.  A similar problem exists with C++ function objects,
    which can be mitigated by using `std::function`, which `sequence<T>` is perfectly
    analogous to, but for iterable containers.

    The sequence constructor works by taking an arbitrary container type `C` that meets
    the criteria and moving it onto the heap as a reference-counted void pointer.  It then
    generates a table of function pointers that emulate the standard range interface for
    `C`, including `size()`, `empty()`, `operator[]`, and basic input/output iterators.
    Users should note that erasing the container type in this way can substantially reduce
    iteration performance, especially for large containers and/or hot loops.  Non-erased
    ranges should therefore be preferred whenever possible, and erasure should be
    considered only as a last resort to satisfy the type checker.

    Bertrand instantiates this type internally when generating bindings for ranges that
    have no direct equivalent in the target language, and therefore cannot be translated
    normally.  In that case, as long as the range's yield type is a valid expression in
    the other language, then the rest of the range interface can be abstracted away, and
    the binding can be generated anyway, albeit at a performance cost. */
    template <
        impl::range_concept C,
        impl::extent Shape = 0,
        meta::inherits<std::input_iterator_tag> Category = std::input_iterator_tag
    >
    struct sequence : range<impl::sequence<meta::const_yield_type<C>, Shape, Category>> {
        using range<impl::sequence<meta::const_yield_type<C>, Shape, Category>>::range;
        using range<impl::sequence<meta::const_yield_type<C>, Shape, Category>>::operator=;
    };

    /// TODO: tuple and scalar deduction guides?

    template <meta::iterable C>
    sequence(C&& c) -> sequence<
        impl::sequence_type<C, meta::static_shape<C>()>,
        meta::static_shape<C>(),
        impl::sequence_category<C>
    >;

}


namespace impl {

    /* A wrapper for an iterator over an `iter::range()` object.  This acts as a
    transparent adaptor for the underlying iterator type, and does not change its
    behavior in any way, except to promote the dereference type to another range. */
    template <range_concept T> requires (meta::iterator<T>)
    struct range_iterator {
        using iterator_category = meta::iterator_category<T>;
        using difference_type = meta::iterator_difference<T>;
        using value_type = meta::iterator_value<T>;
        using reference = meta::as_lvalue<value_type>;
        using pointer = meta::as_pointer<reference>;

        T iter;

        template <typename Self>
        [[nodiscard]] constexpr auto operator*(this Self&& self)
            noexcept (requires{{iter::range(*std::forward<Self>(self).iter)} noexcept;})
            requires (requires{{iter::range(*std::forward<Self>(self).iter)};})
        {
            return iter::range(*std::forward<Self>(self).iter);
        }

        template <typename Self>
        [[nodiscard]] constexpr auto operator->(this Self&& self)
            noexcept (requires{{impl::arrow{*std::forward<Self>(self)}} noexcept;})
            requires (requires{{impl::arrow{*std::forward<Self>(self)}};})
        {
            return impl::arrow{**std::forward<Self>(self)};
        }

        template <typename Self>
        [[nodiscard]] constexpr auto operator[](this Self&& self, difference_type n)
            noexcept (requires{{iter::range(std::forward<Self>(self).iter[n])} noexcept;})
            requires (requires{{iter::range(std::forward<Self>(self).iter[n])};})
        {
            return iter::range(std::forward<Self>(self).iter[n]);
        }

        constexpr range_iterator& operator++()
            noexcept (requires{{++iter} noexcept;})
            requires (requires{{++iter};})
        {
            ++iter;
            return *this;
        }

        [[nodiscard]] range_iterator operator++(int)
            noexcept (requires{{range_iterator{iter++}} noexcept;})
            requires (requires{{range_iterator{iter++}};})
        {
            return {iter++};
        }

        [[nodiscard]] friend constexpr range_iterator operator+(
            const range_iterator& self,
            difference_type n
        ) noexcept (requires{{range_iterator{self.iter + n}} noexcept;})
            requires (requires{{range_iterator{self.iter + n}};})
        {
            return {self.iter + n};
        }

        [[nodiscard]] friend constexpr range_iterator operator+(
            difference_type n,
            const range_iterator& self
        ) noexcept (requires{{range_iterator{self.iter + n}} noexcept;})
            requires (requires{{range_iterator{self.iter + n}};})
        {
            return {self.iter + n};
        }

        constexpr range_iterator& operator+=(difference_type n)
            noexcept (requires{{iter += n} noexcept;})
            requires (requires{{iter += n};})
        {
            iter += n;
            return *this;
        }

        constexpr range_iterator& operator--()
            noexcept (requires{{--iter} noexcept;})
            requires (requires{{--iter};})
        {
            --iter;
            return *this;
        }

        [[nodiscard]] constexpr range_iterator operator--(int)
            noexcept (requires{{range_iterator{iter--}} noexcept;})
            requires (requires{{range_iterator{iter--}};})
        {
            return {iter--};
        }

        [[nodiscard]] constexpr range_iterator operator-(difference_type n) const
            noexcept (requires{{range_iterator{iter - n}} noexcept;})
            requires (requires{{range_iterator{iter - n}};})
        {
            return {iter - n};
        }

        template <typename U>
        [[nodiscard]] constexpr difference_type operator-(const range_iterator<U>& other) const
            noexcept (requires{
                {iter - other.iter} noexcept -> meta::nothrow::convertible_to<difference_type>;
            })
            requires (requires{
                {iter - other.iter} -> meta::convertible_to<difference_type>;
            })
        {
            return iter - other.iter;
        }

        constexpr range_iterator& operator-=(difference_type n)
            noexcept (requires{{iter -= n} noexcept;})
            requires (requires{{iter -= n};})
        {
            iter -= n;
            return *this;
        }

        template <typename U>
        [[nodiscard]] constexpr bool operator<(const range_iterator<U>& other) const
            noexcept (requires{{iter < other.iter} noexcept -> meta::nothrow::truthy;})
            requires (requires{{iter < other.iter} -> meta::truthy;})
        {
            return bool(iter < other.iter);
        }

        template <typename U>
        [[nodiscard]] constexpr bool operator<=(const range_iterator<U>& other) const
            noexcept (requires{{iter <= other.iter} noexcept -> meta::nothrow::truthy;})
            requires (requires{{iter <= other.iter} -> meta::truthy;})
        {
            return bool(iter <= other.iter);
        }

        template <typename U>
        [[nodiscard]] constexpr bool operator==(const range_iterator<U>& other) const
            noexcept (requires{{iter == other.iter} noexcept -> meta::nothrow::truthy;})
            requires (requires{{iter == other.iter} -> meta::truthy;})
        {
            return bool(iter == other.iter);
        }

        template <typename U>
        [[nodiscard]] constexpr bool operator!=(const range_iterator<U>& other) const
            noexcept (requires{{iter != other.iter} noexcept -> meta::nothrow::truthy;})
            requires (requires{{iter != other.iter} -> meta::truthy;})
        {
            return bool(iter != other.iter);
        }

        template <typename U>
        [[nodiscard]] constexpr bool operator>=(const range_iterator<U>& other) const
            noexcept (requires{{iter >= other.iter} noexcept -> meta::nothrow::truthy;})
            requires (requires{{iter >= other.iter} -> meta::truthy;})
        {
            return bool(iter >= other.iter);
        }

        template <typename U>
        [[nodiscard]] constexpr bool operator>(const range_iterator<U>& other) const
            noexcept (requires{{iter > other.iter} noexcept -> meta::nothrow::truthy;})
            requires (requires{{iter > other.iter} -> meta::truthy;})
        {
            return bool(iter > other.iter);
        }

        template <typename U>
        [[nodiscard]] constexpr auto operator<=>(const range_iterator<U>& other) const
            noexcept (requires{{iter <=> other.iter} noexcept;})
            requires (requires{{iter <=> other.iter};})
        {
            return iter <=> other.iter;
        }
    };

    template <typename T>
    range_iterator(T&&) -> range_iterator<meta::remove_rvalue<T>>;

    namespace range_index {

        template <typename... Ts>
        concept runtime = (meta::type_identity<Ts> && ...);

        template <typename... Ts>
        concept comptime = (!meta::type_identity<Ts> && ...);

        template <typename... Ts>
        concept valid = runtime<Ts...> || comptime<Ts...>;

        template <meta::iterable C, meta::integer A>
        constexpr decltype(auto) offset(C&& c, A&& a)
            noexcept (requires{{std::forward<C>(c).begin()[std::forward<A>(a)]} noexcept;})
            requires (requires{{std::forward<C>(c).begin()[std::forward<A>(a)]};})
        {
            return (std::forward<C>(c).begin()[std::forward<A>(a)]);
        }

        template <meta::iterable C, meta::integer A>
        constexpr decltype(auto) offset(C&& c, A&& a)
            noexcept (requires(meta::begin_type<C> it) {
                {std::forward<C>(c).begin()} noexcept;
                {it += std::forward<A>(a)} noexcept;
                {*it} noexcept;
            })
            requires (
                !requires{{std::forward<C>(c).begin()[std::forward<A>(a)]};} &&
                requires(meta::begin_type<C> it) {
                    {std::forward<C>(c).begin()};
                    {it += std::forward<A>(a)};
                    {*it};
                }
            )
        {
            auto it = std::forward<C>(c).begin();
            it += std::forward<A>(a);
            return (*it);
        }

        template <meta::iterable C, meta::integer A>
        constexpr decltype(auto) offset(C&& c, A&& a)
            noexcept (requires(meta::begin_type<C> it) {
                {std::forward<C>(c).begin()} noexcept;
                {a > 0} noexcept -> meta::nothrow::truthy;
                {++it} noexcept;
                {--a} noexcept;
                {a < 0} noexcept -> meta::nothrow::truthy;
                {--it} noexcept;
                {++a} noexcept;
                {*it} noexcept;
            })
            requires (
                !requires{{std::forward<C>(c).begin()[std::forward<A>(a)]};} &&
                !requires(meta::begin_type<C> it) {
                    {std::forward<C>(c).begin()};
                    {it += std::forward<A>(a)};
                    {*it};
                } &&
                requires(meta::begin_type<C> it) {
                    {std::forward<C>(c).begin()};
                    {a > 0} -> meta::truthy;
                    {++it};
                    {--a};
                    {a < 0} -> meta::truthy;
                    {--it};
                    {++a};
                    {*it};
                }
            )
        {
            auto it = std::forward<C>(c).begin();
            while (a > 0) {
                ++it;
                --a;
            }
            while (a < 0) {
                --it;
                ++a;
            }
            return (*it);
        }

        template <meta::iterable C, meta::integer A>
        constexpr decltype(auto) offset(C&& c, A&& a)
            noexcept (requires(meta::begin_type<C> it) {
                {std::forward<C>(c).begin()} noexcept;
                {a > 0} noexcept -> meta::nothrow::truthy;
                {++it} noexcept;
                {--a} noexcept;
                {*it} noexcept;
            })
            requires (
                !requires{{std::forward<C>(c).begin()[std::forward<A>(a)]};} &&
                !requires(meta::begin_type<C> it) {
                    {std::forward<C>(c).begin()};
                    {it += std::forward<A>(a)};
                    {*it};
                } &&
                !requires(meta::begin_type<C> it) {
                    {std::forward<C>(c).begin()};
                    {a > 0} -> meta::truthy;
                    {++it};
                    {--a};
                    {a < 0} -> meta::truthy;
                    {--it};
                    {++a};
                    {*it};
                } &&
                requires(meta::begin_type<C> it) {
                    {std::forward<C>(c).begin()};
                    {a > 0} -> meta::truthy;
                    {++it};
                    {--a};
                    {*it};
                }
            )
        {
            auto it = std::forward<C>(c).begin();
            while (a > 0) {
                ++it;
                --a;
            }
            return (*it);
        }

    }

    namespace range_convert {

        template <typename Self, typename to>
        concept direct = requires(Self self) {
            {*std::forward<Self>(self)} -> meta::convertible_to<to>;
        };

        template <typename Self, typename to>
        concept construct =
            requires(Self self) {{to(std::from_range, std::forward<Self>(self))};} ||
            requires(Self self) {{to(std::from_range, *std::forward<Self>(self).__value)};};

        template <typename Self, typename to>
        concept traverse =
            requires(Self self) {{to(self.begin(), self.end())};} ||
            requires(Self self) {{to(meta::begin(*self.__value), meta::end(*self.__value))};};

        template <meta::tuple_like to, meta::range R, size_t... Is>
            requires (
                meta::tuple_like<R> &&
                meta::tuple_size<R> == meta::tuple_size<to> &&
                sizeof...(Is) == meta::tuple_size<to>
            )
        constexpr to tuple_to_tuple_fn(R&& r, std::index_sequence<Is...>)
            noexcept (requires{{to{std::forward<R>(r).template get<Is>()...}} noexcept;} || (
                !requires{{to{std::forward<R>(r).template get<Is>()...}};} &&
                requires{{to{meta::get<Is>(*std::forward<R>(r).__value)...}} noexcept;}
            ))
            requires (
                requires{{to{std::forward<R>(r).template get<Is>()...}};} ||
                requires{{to{meta::get<Is>(*std::forward<R>(r).__value)...}};}
            )
        {
            if constexpr (requires{{to{std::forward<R>(r).template get<Is>()...}};}) {
                return to{std::forward<R>(r).template get<Is>()...};
            } else {
                return to{meta::get<Is>(*std::forward<R>(r).__value)...};
            }
        }

        template <typename Self, typename to>
        concept tuple_to_tuple =
            meta::tuple_like<Self> &&
            meta::tuple_size<to> == meta::tuple_size<Self> &&
            requires(Self self) {
                {tuple_to_tuple_fn<to>(
                    std::forward<Self>(self),
                    std::make_index_sequence<meta::tuple_size<to>>{}
                )};
            };

        /// TODO: iter_to_tuple may also need updates to account for the iterator always
        /// returning nested ranges.

        /// TODO: forward-declare these errors and backfill them when I've defined `repr()`
        /// and integer <-> string conversions.

        template <size_t Expected>
        constexpr TypeError iter_to_tuple_size_error(size_t actual) noexcept {
            return TypeError(
                "Cannot convert range of size " + std::to_string(actual) +
                " to tuple of size " + std::to_string(Expected)
            );
        }

        template <size_t I, size_t N>
        constexpr TypeError iter_to_tuple_too_small() noexcept {
            return TypeError(
                "Cannot convert range of size " + std::to_string(I) +
                " to tuple of size " + std::to_string(N)
            );
        }

        template <size_t N>
        constexpr TypeError iter_to_tuple_too_big() noexcept {
            return TypeError(
                "Cannot convert range of more than " + std::to_string(N) +
                " elements to tuple of size " + std::to_string(N)
            );
        }

        template <meta::iterator Iter>
        constexpr decltype(auto) _iter_to_tuple_fn(Iter& it)
            requires (requires{
                {*it};
                {++it};
            })
        {
            decltype(auto) val = *it;
            ++it;
            return val;
        }

        template <size_t I, size_t N, meta::iterator Iter, meta::sentinel_for<Iter> End>
        constexpr decltype(auto) _iter_to_tuple_fn(Iter& it, End& end)
            requires (requires{
                {it == end} -> meta::truthy;
                {*it};
                {++it};
            })
        {
            if (it == end) {
                throw iter_to_tuple_too_small<I, N>();
            }
            decltype(auto) val = *it;
            ++it;
            return val;
        }

        template <meta::tuple_like to, meta::range Self, size_t... Is>
            requires (!meta::tuple_like<Self> && sizeof...(Is) == meta::tuple_size<to>)
        constexpr to iter_to_tuple_fn(Self& self, std::index_sequence<Is...>)
            requires (requires(decltype(self.begin()) it, decltype(self.end()) end) {
                {self.size() != sizeof...(Is)} -> meta::truthy;
                {self.begin()};
                {to{(void(Is), _iter_to_tuple_fn(it, end))...}};
            })
        {
            if (size_t size = self.size(); size != sizeof...(Is)) {
                throw iter_to_tuple_size_error<sizeof...(Is)>(size);
            }
            auto it = self.begin();
            return to{(void(Is), _iter_to_tuple_fn(it))...};
        }

        template <meta::tuple_like to, meta::range Self, size_t... Is>
            requires (!meta::tuple_like<Self> && sizeof...(Is) == meta::tuple_size<to>)
        constexpr to iter_to_tuple_fn(Self& self, std::index_sequence<Is...>)
            requires (
                !requires{{self.size()};} &&
                requires(decltype(self.begin()) it, decltype(self.end()) end) {
                    {self.begin()};
                    {self.end()};
                    {to{_iter_to_tuple_fn<Is, sizeof...(Is)>(it, end)...}};
                }
            )
        {
            auto it = self.begin();
            auto end = self.end();
            to result {_iter_to_tuple_fn<Is, sizeof...(Is)>(it, end)...};
            if (!bool(it == end)) {
                throw iter_to_tuple_too_big<sizeof...(Is)>();
            }
            return result;
        }

        template <typename Self, typename to>
        concept iter_to_tuple =
            !meta::tuple_like<Self> &&
            meta::tuple_like<to> &&
            requires(Self self) {
                {iter_to_tuple_fn<to>(
                    self,
                    std::make_index_sequence<meta::tuple_size<to>>{}
                )};
            };

    }

    /// TODO: range assignment should be updated to account for the unconditional
    /// nesting of ranges, and pointer-like dereferencing to the underlying container.
    /// Ranges will never dereference to another range.

    namespace range_assign {

        /// TODO: error messages can be improved after `repr()` and integer <-> string
        /// conversions are defined.

        inline constexpr TypeError size_error(size_t expected, size_t actual) noexcept {
            return TypeError(
                "Cannot assign range of size " + std::to_string(actual) +
                " to a range with " + std::to_string(expected) + " elements"
            );
        }

        inline constexpr TypeError iter_too_small(size_t n) noexcept {
            return TypeError(
                "Cannot assign range of size " + std::to_string(n) +
                " to a range with >" + std::to_string(n) + " elements"
            );
        }

        inline constexpr TypeError iter_too_big(size_t n) noexcept {
            return TypeError(
                "Cannot assign range of >" + std::to_string(n) +
                " elements to a range of size " + std::to_string(n)
            );
        }

        template <typename Self, typename T>
        concept direct = requires(Self self, T c) {
            {*self = meta::strip_range(std::forward<T>(c))};
        };

        template <typename Self, typename T>
        concept scalar =
            meta::iterable<Self> &&
            requires(meta::yield_type<Self> x, const T& v) {
                {std::forward<decltype(x)>(x) = v};
            };

        template <typename Self, typename T>
        concept iter_from_iter = requires(
            Self self,
            T r,
            decltype(self.begin()) s_it,
            decltype(self.end()) s_end,
            decltype(r.begin()) r_it,
            decltype(r.end()) r_end
        ) {
            {self.begin()};
            {self.end()};
            {r.begin()};
            {r.end()};
            {s_it != s_end} -> meta::truthy;
            {r_it != r_end} -> meta::truthy;
            {*s_it = *r_it};
            {++s_it};
            {++r_it};
        } && (
            !meta::tuple_like<Self> ||
            !meta::tuple_like<T> ||
            meta::tuple_size<Self> == meta::tuple_size<T>
        );

        template <typename Self, typename T>
        constexpr void iter_from_iter_fn(Self& self, T& r)
            noexcept (meta::tuple_like<Self> && meta::tuple_like<T> && requires(
                decltype(self.begin()) s_it,
                decltype(self.end()) s_end,
                decltype(r.begin()) r_it,
                decltype(r.end()) r_end
            ) {
                {self.begin()} noexcept;
                {self.end()} noexcept;
                {r.begin()} noexcept;
                {r.end()} noexcept;
                {s_it != s_end} noexcept -> meta::nothrow::truthy;
                {r_it != r_end} noexcept -> meta::nothrow::truthy;
                {*s_it = *r_it} noexcept;
                {++s_it} noexcept;
                {++r_it} noexcept;
            })
            requires (iter_from_iter<Self, T>)
        {
            static constexpr bool sized = requires{{self.size()};} && requires{{r.size()};};
            if constexpr (sized && (!meta::tuple_like<Self> || !meta::tuple_like<T>)) {
                if (self.size() != r.size()) {
                    throw impl::range_assign::size_error(self.size(), r.size());
                }
            }
            auto s_it = self.begin();
            auto s_end = self.end();
            auto r_it = r.begin();
            auto r_end = r.end();
            if constexpr (sized) {
                while (s_it != s_end && r_it != r_end) {
                    *s_it = *r_it;
                    ++s_it;
                    ++r_it;
                }
            } else {
                size_t n = 0;
                while (s_it != s_end && r_it != r_end) {
                    *s_it = *r_it;
                    ++s_it;
                    ++r_it;
                    ++n;
                }
                if (s_it != s_end) {
                    throw impl::range_assign::iter_too_small(n);
                }
                if (r_it != r_end) {
                    throw impl::range_assign::iter_too_big(n);
                }
            }
        }

        template <meta::range Self, meta::range R, size_t... Is>
        constexpr void tuple_from_tuple_fn(Self& self, R&& r, std::index_sequence<Is...>)
            noexcept (requires{
                {((self.template get<Is>() = std::forward<R>(r).template get<Is>()), ...)} noexcept;
            })
            requires (
                meta::tuple_like<Self> &&
                meta::tuple_like<R> &&
                meta::tuple_size<Self> == meta::tuple_size<R> &&
                sizeof...(Is) == meta::tuple_size<R> &&
                requires{{((self.template get<Is>() = std::forward<R>(r).template get<Is>()), ...)};}
            )
        {
            ((self.template get<Is>() = std::forward<R>(r).template get<Is>()), ...);
        }

        template <typename Self, typename T>
        concept tuple_from_tuple =
            meta::tuple_like<Self> &&
            meta::tuple_like<T> &&
            meta::tuple_size<Self> == meta::tuple_size<T> &&
            requires(Self self, T r) {
                {tuple_from_tuple_fn(
                    self,
                    std::forward<T>(r),
                    std::make_index_sequence<meta::tuple_size<Self>>{}
                )};
            };

        template <size_t I, meta::range R, meta::iterator Iter>
        constexpr void _iter_from_tuple_fn(R&& r, Iter& it)
            requires (requires{
                {*it = std::forward<R>(r).template get<I>()};
                {++it};
            })
        {
            *it = std::forward<R>(r).template get<I>();
            ++it;
        }

        template <size_t I, meta::range R, meta::iterator Iter, meta::sentinel_for<Iter> End>
        constexpr void _iter_from_tuple_fn(R&& r, Iter& it, End& end)
            requires (requires{
                {it == end} -> meta::truthy;
                {*it = std::forward<R>(r).template get<I>()};
                {++it};
            })
        {
            if (it == end) {
                throw iter_too_big(I);
            }
            *it = std::forward<R>(r).template get<I>();
            ++it;
        }

        template <size_t I, meta::range Self, meta::iterator Iter>
        constexpr void _tuple_from_iter_fn(Self& s, Iter& it)
            requires (requires{
                {s.template get<I>() = *it};
                {++it};
            })
        {
            s.template get<I>() = *it;
            ++it;
        }

        template <size_t I, meta::range Self, meta::iterator Iter, meta::sentinel_for<Iter> End>
        constexpr void _tuple_from_iter_fn(Self& s, Iter& it, End& end)
            requires (requires{
                {it == end} -> meta::truthy;
                {s.template get<I>() = *it};
                {++it};
            })
        {
            if (it == end) {
                throw iter_too_small(I);
            }
            s.template get<I>() = *it;
            ++it;
        }

        template <meta::range Self, meta::range R, size_t... Is>
        constexpr void iter_from_tuple_fn(Self& self, R&& r, std::index_sequence<Is...>)
            requires (
                !meta::tuple_like<Self> &&
                meta::tuple_like<R> &&
                sizeof...(Is) == meta::tuple_size<R> &&
                requires(decltype(self.begin()) it) {
                    {self.size() != sizeof...(Is)} -> meta::truthy;
                    {self.begin()};
                    {(_iter_from_tuple_fn<Is>(std::forward<R>(r), it), ...)};
                }
            )
        {
            if (self.size() != sizeof...(Is)) {
                throw size_error(self.size(), sizeof...(Is));
            }
            auto it = self.begin();
            (_iter_from_tuple_fn<Is>(std::forward<R>(r), it), ...);
        }

        template <meta::range Self, meta::range R, size_t... Is>
        constexpr void iter_from_tuple_fn(Self& self, R&& r, std::index_sequence<Is...>)
            requires (
                !meta::tuple_like<Self> &&
                meta::tuple_like<R> &&
                sizeof...(Is) == meta::tuple_size<R> &&
                !requires{{self.size()};} &&
                requires(decltype(self.begin()) it, decltype(self.end()) end) {
                    {self.begin()};
                    {self.end()};
                    {(_iter_from_tuple_fn<Is>(std::forward<R>(r), it, end), ...)};
                }
            )
        {
            auto it = self.begin();
            auto end = self.end();
            (_iter_from_tuple_fn<Is>(std::forward<R>(r), it, end), ...);
            if (!bool(it == end)) {
                throw iter_too_small(sizeof...(Is));
            }
        }

        template <meta::range Self, typename R, size_t... Is>
        constexpr void tuple_from_iter_fn(Self& self, R& r, std::index_sequence<Is...>)
            requires (
                meta::tuple_like<Self> &&
                !meta::tuple_like<R> &&
                sizeof...(Is) == meta::tuple_size<Self> &&
                requires(decltype(r.begin()) it) {
                    {sizeof...(Is) != r.size()} -> meta::truthy;
                    {r.begin()};
                    {(_tuple_from_iter_fn<Is>(self, it), ...)};
                }
            )
        {
            if (sizeof...(Is) != r.size()) {
                throw size_error(sizeof...(Is), r.size());
            }
            auto it = r.begin();
            (_tuple_from_iter_fn<Is>(self, it), ...);
        }

        template <meta::range Self, typename R, size_t... Is>
        constexpr void tuple_from_iter_fn(Self& self, R& r, std::index_sequence<Is...>)
            requires (
                meta::tuple_like<Self> &&
                !meta::tuple_like<R> &&
                sizeof...(Is) == meta::tuple_size<Self> &&
                !requires{{r.size()};} &&
                requires(decltype(r.begin()) it, decltype(r.end()) end) {
                    {r.begin()};
                    {r.end()};
                    {(_tuple_from_iter_fn<Is>(self, it, end), ...)};
                }
            )
        {
            auto it = r.begin();
            auto end = r.end();
            (_tuple_from_iter_fn<Is>(self, it, end), ...);
            if (!bool(it == end)) {
                throw iter_too_big(sizeof...(Is));
            }
        }

        template <typename Self, typename T>
        concept iter_from_tuple =
            !meta::tuple_like<Self> &&
            meta::tuple_like<T> &&
            requires(Self self, T r) {
                {iter_from_tuple_fn(
                    self,
                    std::forward<T>(r),
                    std::make_index_sequence<meta::tuple_size<T>>{}
                )};
            };

        template <typename Self, typename T>
        concept tuple_from_iter =
            meta::tuple_like<Self> &&
            !meta::tuple_like<T> &&
            requires(Self self, T r) {
                {tuple_from_iter_fn(
                    self,
                    r,
                    std::make_index_sequence<meta::tuple_size<Self>>{}
                )};
            };

    }

}


namespace iter {

    /* Range-based logical disjunction operator.  Accepts any number of arguments that
    are explicitly convertible to `bool` and returns true if at least one evaluates to
    true.  A custom function predicate may be supplied as an initializer, which will be
    applied to each value.  If a range is given and the function is not immediately
    callable with it or its underlying value, then the function may be broadcasted over
    all its elements before advancing to the next argument.  Because ranges always
    yield other ranges, this process may recur until either the function becomes
    callable or a scalar value is reached. */
    template <meta::not_rvalue F = impl::ExplicitConvertTo<bool>>
    struct any {
        [[no_unique_address]] F func;

        template <typename A>
        constexpr bool operator()(A&& a) const
            noexcept (requires{{func(std::forward<A>(a))} noexcept -> meta::nothrow::truthy;})
            requires (requires{{func(std::forward<A>(a))} -> meta::truthy;})
        {
            return bool(func(std::forward<A>(a)));
        }

        template <meta::range A>
        constexpr bool operator()(A&& a) const
            noexcept (requires{{func(*std::forward<A>(a))} noexcept -> meta::nothrow::truthy;} || (
                !requires{{func(*std::forward<A>(a))} -> meta::truthy;} &&
                meta::nothrow::iterable<A> &&
                requires(meta::yield_type<A> x) {
                    {operator()(std::forward<decltype(x)>(x))} noexcept -> meta::nothrow::truthy;
                }
            ))
            requires (!requires{{func(std::forward<A>(a))} -> meta::truthy;} && (
                requires{{func(*std::forward<A>(a))} -> meta::truthy;} ||
                !meta::scalar<A> &&
                meta::iterable<A> &&
                requires(meta::yield_type<A> x) {
                    {operator()(std::forward<decltype(x)>(x))} -> meta::truthy;
                }
            ))
        {
            if constexpr (requires{{func(*std::forward<A>(a))} -> meta::truthy;}) {
                return bool(func(*std::forward<A>(a)));
            } else {
                for (auto&& x : a) {
                    if (operator()(std::forward<decltype(x)>(x))) {
                        return true;
                    }
                }
                return false;
            }
        }

        template <typename... A> requires (sizeof...(A) != 1)
        [[nodiscard]] constexpr bool operator()(A&&... a) const
            noexcept (requires{{(operator()(std::forward<A>(a)) || ...)} noexcept;})
            requires (requires{{(operator()(std::forward<A>(a)) || ...)};})
        {
            return (operator()(std::forward<A>(a)) || ...);
        }
    };

    template <typename F>
    any(F&&) -> any<meta::remove_rvalue<F>>;

    /* Range-based logical conjunction operator.  Accepts any number of arguments that
    are explicitly convertible to `bool` and returns true if all of them evaluate to
    true.  A custom function predicate may be supplied as an initializer, which will be
    applied to each value.  If a range is given and the function is not immediately
    callable with it or its underlying value, then the function may be broadcasted over
    all its elements before advancing to the next argument.  Because ranges always
    yield other ranges, this process may recur until either the function becomes
    callable or a scalar value is reached. */
    template <meta::not_rvalue F = impl::ExplicitConvertTo<bool>>
    struct all {
        [[no_unique_address]] F func;

        template <typename T>
        constexpr bool operator()(T&& v) const
            noexcept (requires{{func(std::forward<T>(v))} noexcept -> meta::nothrow::truthy;})
            requires (requires{{func(std::forward<T>(v))} -> meta::truthy;})
        {
            return bool(func(std::forward<T>(v)));
        }

        template <meta::range T>
        constexpr bool operator()(T&& v) const
            noexcept (requires{{func(*std::forward<T>(v))} noexcept -> meta::nothrow::truthy;} || (
                !requires{{func(*std::forward<T>(v))} -> meta::truthy;} &&
                meta::nothrow::iterable<T> &&
                requires(meta::yield_type<T> x) {
                    {operator()(std::forward<decltype(x)>(x))} noexcept -> meta::nothrow::truthy;
                }
            ))
            requires (!requires{{func(std::forward<T>(v))} -> meta::truthy;} && (
                requires{{func(*std::forward<T>(v))} -> meta::truthy;} ||
                !meta::scalar<T> &&
                meta::iterable<T> &&
                requires(meta::yield_type<T> x) {
                    {operator()(std::forward<decltype(x)>(x))} -> meta::truthy;
                }
            ))
        {
            if constexpr (requires{{func(*std::forward<T>(v))} -> meta::truthy;}) {
                return bool(func(*std::forward<T>(v)));
            } else {
                for (auto&& x : v) {
                    if (!operator()(std::forward<decltype(x)>(x))) {
                        return false;
                    }
                }
                return true;
            }
        }

        template <typename... A> requires (sizeof...(A) != 1)
        [[nodiscard]] constexpr bool operator()(A&&... a) const
            noexcept (requires{{(operator()(std::forward<A>(a)) && ...)} noexcept;})
            requires (requires{{(operator()(std::forward<A>(a)) && ...)};})
        {
            return (operator()(std::forward<A>(a)) && ...);
        }
    };

    template <typename F>
    all(F&&) -> all<meta::remove_rvalue<F>>;

    /* Check to see whether a particular value or consecutive subsequence is present
    in the arguments.  An arbitrary number of arguments may be supplied, in which case
    the search will proceed from left to right, stopping as soon as a match is found.

    The initializer may be any of the following (in order of precedence):

        1.  A scalar value for which `value == arg` or `value(arg)` is well-formed and
            returns a type that is implicitly convertible to `bool`, where `true`
            terminates the search.
        2.  A scalar input to an `arg.contains(value)` member method or ADL-enabled
            `contains(arg, value)`, if one exists, and returns a type that is
            implicitly convertible to `bool`.`
        3.  A linear search through the top-level elements of a tuple or iterable type,
            applying (1) to each result.  If the comparison value is a non-scalar
            range, then it will be interpreted as a subsequence, and will only return
            true if all of its elements are found in the proper order within at least
            one argument.  The subsequence will never cross argument boundaries.
    */
    template <meta::not_rvalue T>
    struct contains {
        [[no_unique_address]] T k;

    private:
        template <typename A>
        constexpr bool scalar(const A& a) const
            noexcept (requires{{
                meta::strip_range(k) == meta::strip_range(a)
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                meta::strip_range(k) == meta::strip_range(a)
            } -> meta::convertible_to<bool>;})
        {
            return bool(meta::strip_range(k) == meta::strip_range(a));
        }

        template <typename A>
        constexpr bool scalar(const A& a) const
            noexcept (requires{{
                meta::strip_range(k)(meta::strip_range(a))
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                meta::strip_range(k)(meta::strip_range(a))
            } -> meta::convertible_to<bool>;})
        {
            return bool(meta::strip_range(k)(meta::strip_range(a)));
        }

        template <typename A, size_t... Is>
        constexpr bool tuple(const A& a, std::index_sequence<Is...>) const
            noexcept (requires{{(scalar(meta::get<Is>(a)) || ...)} noexcept;})
            requires (requires{{(scalar(meta::get<Is>(a)) || ...)};})
        {
            return (scalar(meta::get<Is>(a)) || ...);
        }

    public:
        template <typename A>
        [[nodiscard]] constexpr bool operator()(const A& a) const
            noexcept (requires{{scalar(a)};} ? requires{{scalar(a)} noexcept;} : (
                requires{{meta::detail::invoke_contains(k, a)};} ?
                requires{{meta::detail::invoke_contains(k, a)} noexcept;} : ((
                    meta::iterable<const A&> &&
                    requires(meta::as_const_ref<meta::yield_type<const A&>> element) {
                        {scalar(element)};
                    }
                ) ? (
                    meta::nothrow::iterable<const A&> &&
                    requires(meta::as_const_ref<meta::yield_type<const A&>> element) {
                        {scalar(element)} noexcept;
                    }
                ) : (
                    meta::tuple_like<A> && requires{
                        {tuple(a, std::make_index_sequence<meta::tuple_size<A>>{})} noexcept;
                    }
                )))
            )
            requires (
                requires{{
                    meta::strip_range(k) == meta::strip_range(a)
                } -> meta::convertible_to<bool>;} ||
                requires{{
                    meta::strip_range(k)(meta::strip_range(a))
                } -> meta::convertible_to<bool>;} ||
                meta::detail::member::has_contains<T, A> ||
                meta::detail::adl::has_contains<T, A> || (
                    meta::iterable<const A&> && (
                        requires(meta::as_const_ref<meta::yield_type<const A&>> element) {{
                            meta::strip_range(k) == meta::strip_range(element)
                        } -> meta::convertible_to<bool>;} ||
                        requires(meta::as_const_ref<meta::yield_type<const A&>> element) {{
                            meta::strip_range(k)(meta::strip_range(element))
                        } -> meta::convertible_to<bool>;}
                    )
                ) || (
                    meta::tuple_like<A> && requires{
                        {tuple(a, std::make_index_sequence<meta::tuple_size<A>>{})};
                    }
                )
            )
        {
            if constexpr (requires{{scalar(a)};}) {
                return scalar(a);
            } else if constexpr (
                meta::detail::member::has_contains<T, A> ||
                meta::detail::adl::has_contains<T, A>
            ) {
                return meta::detail::invoke_contains(k, a);
            } else if constexpr (meta::iterable<const A&> && (
                requires(meta::as_const_ref<meta::yield_type<const A&>> element) {{
                    meta::strip_range(k) == meta::strip_range(element)
                } -> meta::convertible_to<bool>;} ||
                requires(meta::as_const_ref<meta::yield_type<const A&>> element) {{
                    meta::strip_range(k)(meta::strip_range(element))
                } -> meta::convertible_to<bool>;}
            )) {
                for (const auto& x : a) {
                    if (scalar(x)) {
                        return true;
                    }
                }
                return false;
            } else {
                return tuple(a, std::make_index_sequence<meta::tuple_size<A>>{});
            }
        }

        template <typename... A> requires (sizeof...(A) != 1)
        [[nodiscard]] constexpr bool operator()(const A&... a) const
            noexcept (requires{{(operator()(a) || ...)} noexcept;})
            requires (requires{{(operator()(a) || ...)};})
        {
            return (operator()(a) || ...);
        }
    };
    template <meta::not_rvalue T> requires (meta::range<T> && !meta::scalar<T>)
    struct contains<T> {
        [[no_unique_address]] T k;

    private:
        template <typename K, typename A>
        static constexpr bool scalar(const K& k, const A& a)
            noexcept (requires{{
                meta::strip_range(k) == meta::strip_range(a)
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                meta::strip_range(k) == meta::strip_range(a)
            } -> meta::convertible_to<bool>;})
        {
            return bool(meta::strip_range(k) == meta::strip_range(a));
        }

        template <typename K, typename A>
        static constexpr bool scalar(const K& k, const A& a)
            noexcept (requires{{
                meta::strip_range(k)(meta::strip_range(a))
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                meta::strip_range(k)(meta::strip_range(a))
            } -> meta::convertible_to<bool>;})
        {
            return bool(meta::strip_range(k)(meta::strip_range(a)));
        }

        template <typename A>
        constexpr bool subsequence(const A& a) const
            noexcept (requires(
                meta::begin_type<meta::as_const_ref<T>> k_begin,
                meta::begin_type<meta::as_const_ref<T>> k_it,
                meta::end_type<meta::as_const_ref<T>> k_end,
                meta::begin_type<const A&> a_it,
                meta::end_type<const A&> a_end
            ) {
                {meta::begin(k)} noexcept -> meta::nothrow::copyable;
                {meta::end(k)} noexcept;
                {k_it == k_end} noexcept -> meta::nothrow::truthy;
                {meta::begin(a)} noexcept;
                {meta::end(a)} noexcept;
                {a_it != a_end} noexcept -> meta::nothrow::truthy;
                {scalar(*k_it, *a_it)} noexcept;
                {++k_it} noexcept;
                {k_it = k_begin} noexcept;
                {++a_it} noexcept;
            })
        {
            auto k_begin = meta::begin(k);
            auto k_it = k_begin;
            auto k_end = meta::end(k);
            if (k_it == k_end) {
                return true;  // empty range
            }
            auto a_it = meta::begin(a);
            auto a_end = meta::end(a);
            while (a_it != a_end) {
                if (scalar(*k_it, *a_it)) {
                    ++k_it;
                    if (k_it == k_end) {
                        return true;
                    }
                } else {
                    k_it = k_begin;
                }
                ++a_it;
            }
            return false;
        }

    public:
        template <typename A>
        [[nodiscard]] constexpr bool operator()(const A& a) const
            noexcept (requires{{subsequence(a)} noexcept;})
            requires (requires(
                meta::begin_type<meta::as_const_ref<T>> k_begin,
                meta::begin_type<meta::as_const_ref<T>> k_it,
                meta::end_type<meta::as_const_ref<T>> k_end,
                meta::begin_type<const A&> a_it,
                meta::end_type<const A&> a_end
            ) {
                {meta::begin(k)} -> meta::copyable;
                {meta::end(k)};
                {k_it == k_end} -> meta::truthy;
                {meta::begin(a)};
                {meta::end(a)};
                {a_it != a_end} -> meta::truthy;
                {scalar(*k_it, *a_it)};
                {++k_it};
                {k_it = k_begin};
                {++a_it};
            })
        {
            return subsequence(a);
        }

        template <typename A>
        [[nodiscard]] constexpr bool operator()(const A& a) const
            noexcept (requires{{subsequence(impl::tuple_range(a))} noexcept;})
            requires (meta::tuple_like<A> && requires(
                meta::begin_type<meta::as_const_ref<T>> k_begin,
                meta::begin_type<meta::as_const_ref<T>> k_it,
                meta::end_type<meta::as_const_ref<T>> k_end,
                impl::tuple_range<const A&> norm,
                meta::begin_type<decltype(norm)> a_it,
                meta::end_type<decltype(norm)> a_end
            ) {
                {meta::begin(k)} -> meta::copyable;
                {meta::end(k)};
                {k_it == k_end} -> meta::truthy;
                {impl::tuple_range(a)};
                {meta::begin(norm)};
                {meta::end(norm)};
                {a_it != a_end} -> meta::truthy;
                {scalar(*k_it, *a_it)};
                {++k_it};
                {k_it = k_begin};
                {++a_it};
            })
        {
            return subsequence(impl::tuple_range(a));
        }

        template <typename... A> requires (sizeof...(A) != 1)
        [[nodiscard]] constexpr bool operator()(const A&... a) const
            noexcept (requires{{(operator()(a) || ...)} noexcept;})
            requires (requires{{(operator()(a) || ...)};})
        {
            return (operator()(a) || ...);
        }
    };

    template <typename T>
    contains(T&&) -> contains<meta::remove_rvalue<T>>;

    /* Range-based multidimensional indexing operator, with support for both
    compile-time (tuple-like) and runtime (subscript) indexing.

    This operator accepts any number of values as initializers, which may be given as
    either non-type template parameters (for compile-time indices) or constructor
    arguments (for runtime indices - supported by CTAD), but not both.  The resulting
    function object can be called with a generic container to execute the indexing
    operation.  The precise behavior for each index is as follows (in order of preference):

        1.  Direct indexing via `get<index>(container)` (or an equivalent
            `container.get<index>()` member method) or `container[index]`.  The tuple
            unpacking operator is preferred if all inputs are known at compile time,
            while the subscript operator is preferred for runtime indices.  The other
            operator will be used as a fallback.  Runtime subscripting of tuples is
            implemented as a dynamic dispatch, which may return a union if the tuple is
            heterogenous, and will only consider a single integer index (with
            Python-style wraparound).
        2.  Integer values that do not satisfy (1).  These will be interpreted as
            (unsigned) offsets from the `begin()` iterator of the container, and will
            be applied using iterator arithmetic, including a linear traversal if
            necessary.
        3.  Predicate functions that take the container as an argument and return an
            arbitrary value.  This includes both user-defined functions as well as
            range adaptors such as `iter::slice{}`, `iter::where{}`, etc.  This allows
            arbitrary indexing logic, assuming neither of the first two cases apply.

    If more than one index is given, they will be forwarded to the container's indexing
    operator directly where possible (e.g. `container[i, j, k...]`) and sequentially
    where not (e.g. `container[i, j][k]...`), which can occur in any combination as
    long as the underlying types support it.  Practically speaking, this means that the
    `at{}` operator behaves symmetrically for both multidimensional containers as well
    as nested "container of container" structures simultaneously.

    Note that if the input container is given as a `range` type, then the result will
    also be promoted to a (possibly scalar) `range` as well, for consistency with range
    iteration. */
    template <auto... K> requires (impl::range_index::valid<decltype(K)...>)
    struct at {
    private:
        template <size_t N, typename>
        struct _recur;
        template <size_t N, size_t... I>
        struct _recur<N, std::index_sequence<I...>> {
            using type = at<meta::unpack_value<N + I, K...>...>;
        };
        template <size_t N> requires (N < sizeof...(K))
        using recur = _recur<N, std::make_index_sequence<sizeof...(K) - N>>::type;

        template <auto... A, typename C>
        static constexpr decltype(auto) get_impl(C&& c)
            noexcept (requires{{range(meta::get<A...>(*std::forward<C>(c)))} noexcept;})
            requires (meta::range<C> && requires{{range(meta::get<A...>(*std::forward<C>(c)))};})
        {
            return (range(meta::get<A...>(*std::forward<C>(c))));
        }

        template <auto... A, typename C>
        static constexpr decltype(auto) get_impl(C&& c)
            noexcept (requires{{meta::get<A...>(std::forward<C>(c))} noexcept;})
            requires (!meta::range<C> && requires{{meta::get<A...>(std::forward<C>(c))};})
        {
            return (meta::get<A...>(std::forward<C>(c)));
        }

        template <auto... A, typename C>
        static constexpr decltype(auto) get(C&& c)
            noexcept (requires{{get_impl<A...>(std::forward<C>(c))} noexcept;})
            requires (
                sizeof...(A) == sizeof...(K) &&
                requires{{get_impl<A...>(std::forward<C>(c))};}
            )
        {
            return (get_impl<A...>(std::forward<C>(c)));
        }

        template <auto... A, typename C>
        static constexpr decltype(auto) get(C&& c)
            noexcept (requires{{get<A..., meta::unpack_value<sizeof...(A), K...>>(
                std::forward<C>(c)
            )} noexcept;})
            requires (
                sizeof...(A) < sizeof...(K) &&
                requires{{get<A..., meta::unpack_value<sizeof...(A), K...>>(
                    std::forward<C>(c)
                )};}
            )
        {
            return (get<A..., meta::unpack_value<sizeof...(A), K...>>(std::forward<C>(c)));
        }

        template <auto... A, typename C>
        static constexpr decltype(auto) get(C&& c)
            noexcept (requires{
                {recur<sizeof...(A)>{}(get_impl<A...>(std::forward<C>(c)))} noexcept;
            })
            requires (
                sizeof...(A) < sizeof...(K) &&
                !requires{{get<A..., meta::unpack_value<sizeof...(A), K...>>(
                    std::forward<C>(c)
                )};} &&
                requires{{recur<sizeof...(A)>{}(get_impl<A...>(std::forward<C>(c)))};}
            )
        {
            return (recur<sizeof...(A)>{}(get_impl<A...>(std::forward<C>(c))));
        }

        template <auto... A, typename C>
        static constexpr decltype(auto) subscript_impl(C&& c)
            noexcept (requires{{range((*std::forward<C>(c))[A...])} noexcept;})
            requires (meta::range<C> && requires{{range((*std::forward<C>(c))[A...])};})
        {
            return (range((*std::forward<C>(c))[A...]));
        }

        template <auto... A, typename C>
        static constexpr decltype(auto) subscript_impl(C&& c)
            noexcept (requires{{std::forward<C>(c)[A...]} noexcept;})
            requires (!meta::range<C> && requires{{std::forward<C>(c)[A...]};})
        {
            return (std::forward<C>(c)[A...]);
        }

        template <auto... A, typename C>
        static constexpr decltype(auto) subscript(C&& c)
            noexcept (requires{{subscript_impl<A...>(std::forward<C>(c))} noexcept;})
            requires (
                sizeof...(A) == sizeof...(K) &&
                requires{{subscript_impl<A...>(std::forward<C>(c))};}
            )
        {
            return (subscript_impl<A...>(std::forward<C>(c)));
        }

        template <auto... A, typename C>
        static constexpr decltype(auto) subscript(C&& c)
            noexcept (requires{{subscript<A..., meta::unpack_value<sizeof...(A), K...>>(
                std::forward<C>(c)
            )} noexcept;})
            requires (
                sizeof...(A) < sizeof...(K) &&
                requires{{subscript<A..., meta::unpack_value<sizeof...(A), K...>>(
                    std::forward<C>(c)
                )};}
            )
        {
            return (subscript<A..., meta::unpack_value<sizeof...(A), K...>>(std::forward<C>(c)));
        }

        template <auto... A, typename C>
        static constexpr decltype(auto) subscript(C&& c)
            noexcept (requires{
                {recur<sizeof...(A)>{}(subscript_impl<A...>(std::forward<C>(c)))} noexcept;
            })
            requires (
                sizeof...(A) < sizeof...(K) &&
                !requires{{subscript<A..., meta::unpack_value<sizeof...(A), K...>>(
                    std::forward<C>(c)
                )};} &&
                requires{{recur<sizeof...(A)>{}(subscript_impl<A...>(std::forward<C>(c)))};}
            )
        {
            return (recur<sizeof...(A)>{}(subscript_impl<A...>(std::forward<C>(c))));
        }

        template <typename C>
        static constexpr decltype(auto) offset(C&& c)
            noexcept (requires{{range(impl::range_index::offset(
                *std::forward<C>(c),
                meta::unpack_value<0, K...>
            ))} noexcept;})
            requires (
                sizeof...(K) == 1 &&
                meta::range<C> &&
                requires{{range(impl::range_index::offset(
                    *std::forward<C>(c),
                    meta::unpack_value<0, K...>
                ))};}
            )
        {
            return (range(impl::range_index::offset(
                *std::forward<C>(c),
                meta::unpack_value<0, K...>
            )));
        }

        template <typename C>
        static constexpr decltype(auto) offset(C&& c)
            noexcept (requires{{impl::range_index::offset(
                std::forward<C>(c),
                meta::unpack_value<0, K...>
            )} noexcept;})
            requires (
                sizeof...(K) == 1 &&
                !meta::range<C> &&
                requires{{impl::range_index::offset(
                    std::forward<C>(c),
                    meta::unpack_value<0, K...>
                )};}
            )
        {
            return (impl::range_index::offset(
                std::forward<C>(c),
                meta::unpack_value<0, K...>
            ));
        }

        template <typename C>
        static constexpr decltype(auto) offset(C&& c)
            noexcept (requires{{recur<1>{}(range(impl::range_index::offset(
                *std::forward<C>(c),
                meta::unpack_value<0, K...>
            )))} noexcept;})
            requires (
                sizeof...(K) > 1 &&
                meta::range<C> &&
                requires{{recur<1>{}(range(impl::range_index::offset(
                    *std::forward<C>(c),
                    meta::unpack_value<0, K...>
                )))};}
            )
        {
            return (recur<1>{}(range(impl::range_index::offset(
                *std::forward<C>(c),
                meta::unpack_value<0, K...>
            ))));
        }

        template <typename C>
        static constexpr decltype(auto) offset(C&& c)
            noexcept (requires{{recur<1>{}(impl::range_index::offset(
                std::forward<C>(c),
                meta::unpack_value<0, K...>
            ))} noexcept;})
            requires (
                sizeof...(K) > 1 &&
                !meta::range<C> &&
                requires{{recur<1>{}(impl::range_index::offset(
                    std::forward<C>(c),
                    meta::unpack_value<0, K...>
                ))};}
            )
        {
            return (recur<1>{}(impl::range_index::offset(
                std::forward<C>(c),
                meta::unpack_value<0, K...>
            )));
        }

        template <typename C>
        static constexpr decltype(auto) invoke(C&& c)
            noexcept (requires{{range(meta::unpack_value<0, K...>(std::forward<C>(c)))} noexcept;})
            requires (
                sizeof...(K) == 1 &&
                meta::range<C> &&
                requires{{range(meta::unpack_value<0, K...>(std::forward<C>(c)))} -> meta::not_void;}
            )
        {
            return (range(meta::unpack_value<0, K...>(std::forward<C>(c))));
        }

        template <typename C>
        static constexpr decltype(auto) invoke(C&& c)
            noexcept (requires{{meta::unpack_value<0, K...>(std::forward<C>(c))} noexcept;})
            requires (
                sizeof...(K) == 1 &&
                !meta::range<C> &&
                requires{{meta::unpack_value<0, K...>(std::forward<C>(c))} -> meta::not_void;}
            )
        {
            return (meta::unpack_value<0, K...>(std::forward<C>(c)));
        }

        template <typename C>
        static constexpr decltype(auto) invoke(C&& c)
            noexcept (requires{
                {recur<1>{}(range(meta::unpack_value<0, K...>(std::forward<C>(c))))} noexcept;
            })
            requires (
                sizeof...(K) > 1 &&
                meta::range<C> &&
                requires{{recur<1>{}(
                    range(meta::unpack_value<0, K...>(std::forward<C>(c)))
                )} -> meta::not_void;}
            )
        {
            return (recur<1>{}(range(meta::unpack_value<0, K...>(std::forward<C>(c)))));
        }

        template <typename C>
        static constexpr decltype(auto) invoke(C&& c)
            noexcept (requires{{recur<1>{}(
                meta::unpack_value<0, K...>(std::forward<C>(c))
            )} noexcept;})
            requires (
                sizeof...(K) > 1 &&
                !meta::range<C> &&
                requires{{recur<1>{}(
                    meta::unpack_value<0, K...>(std::forward<C>(c))
                )} -> meta::not_void;}
            )
        {
            return (recur<1>{}(meta::unpack_value<0, K...>(std::forward<C>(c))));
        }

    public:
        template <typename C>
        [[nodiscard]] static constexpr decltype(auto) operator()(C&& c)
            noexcept (
                requires{{get(std::forward<C>(c))};} ?
                requires{{get(std::forward<C>(c))} noexcept;} : (
                    requires{{subscript(std::forward<C>(c))};} ?
                    requires{{subscript(std::forward<C>(c))} noexcept;} : (
                        requires{{offset(std::forward<C>(c))};} ?
                        requires{{offset(std::forward<C>(c))} noexcept;} :
                        requires{{invoke(std::forward<C>(c))} noexcept;}
                    )
                )
            )
            requires (
                requires{{get(std::forward<C>(c))};} ||
                requires{{subscript(std::forward<C>(c))};} ||
                requires{{offset(std::forward<C>(c))};} ||
                requires{{invoke(std::forward<C>(c))};}
            )
        {
            if constexpr (requires{{get(std::forward<C>(c))};}) {
                return (get(std::forward<C>(c)));
            } else if constexpr (requires{{subscript(std::forward<C>(c))};}) {
                return (subscript(std::forward<C>(c)));
            } else if constexpr (requires{{offset(std::forward<C>(c))};}) {
                return (offset(std::forward<C>(c)));
            } else {
                return (invoke(std::forward<C>(c)));
            }
        }
    };
    template <auto... K>
        requires (
            impl::range_index::valid<decltype(K)...> &&
            impl::range_index::runtime<decltype(K)...>
        )
    struct at<K...> {
        [[no_unique_address]] impl::basic_tuple<typename decltype(K)::type...> idx;

        [[nodiscard]] constexpr at() = default;
        [[nodiscard]] constexpr at(meta::forward<typename decltype(K)::type>... k)
            noexcept (requires{{impl::basic_tuple<typename decltype(K)::type...>{
                std::forward<typename decltype(K)::type>(k)...
            }} noexcept;})
            requires (requires{{impl::basic_tuple<typename decltype(K)::type...>{
                std::forward<typename decltype(K)::type>(k)...
            }};})
        :
            idx{std::forward<typename decltype(K)::type>(k)...}
        {}

    private:
        template <typename Self, typename C, typename... A>
        constexpr decltype(auto) subscript_impl(this Self&& self, C&& c, A&&... a)
            noexcept (requires{{range((*std::forward<C>(c))[std::forward<A>(a)...])} noexcept;})
            requires (meta::range<C> && requires{
                {range((*std::forward<C>(c))[std::forward<A>(a)...])};
            })
        {
            return (range((*std::forward<C>(c))[std::forward<A>(a)...]));
        }

        template <typename Self, typename C, typename... A>
        constexpr decltype(auto) subscript_impl(this Self&& self, C&& c, A&&... a)
            noexcept (requires{{std::forward<C>(c)[std::forward<A>(a)...]} noexcept;})
            requires (!meta::range<C> && requires{{std::forward<C>(c)[std::forward<A>(a)...]};})
        {
            return (std::forward<C>(c)[std::forward<A>(a)...]);
        }

        template <size_t N, typename Self, typename C, typename... A>
        constexpr decltype(auto) subscript(this Self&& self, C&& c, A&&... a)
            noexcept (requires{{std::forward<Self>(self).subscript_impl(
                std::forward<C>(c),
                std::forward<A>(a)...
            )} noexcept;})
            requires (
                (N + sizeof...(A)) == sizeof...(K) &&
                requires{{std::forward<Self>(self).subscript_impl(
                    std::forward<C>(c),
                    std::forward<A>(a)...
                )};}
            )
        {
            return (std::forward<Self>(self).subscript_impl(
                std::forward<C>(c),
                std::forward<A>(a)...
            ));
        }

        template <size_t N, typename Self, typename C, typename... A>
        constexpr decltype(auto) subscript(this Self&& self, C&& c, A&&... a)
            noexcept (requires{{std::forward<Self>(self).template subscript<N>(
                std::forward<C>(c),
                std::forward<A>(a)...,
                std::forward<Self>(self).idx.template get<N + sizeof...(A)>()
            )} noexcept;})
            requires (
                (N + sizeof...(A)) < sizeof...(K) &&
                requires{{std::forward<Self>(self).template subscript<N>(
                    std::forward<C>(c),
                    std::forward<A>(a)...,
                    std::forward<Self>(self).idx.template get<N + sizeof...(A)>()
                )};}
            )
        {
            return (std::forward<Self>(self).template subscript<N>(
                std::forward<C>(c),
                std::forward<A>(a)...,
                std::forward<Self>(self).idx.template get<N + sizeof...(A)>()
            ));
        }

        template <size_t N, typename Self, typename C, typename... A>
        constexpr decltype(auto) subscript(this Self&& self, C&& c, A&&... a)
            noexcept (requires{{std::forward<Self>(self).template operator()<N + sizeof...(A)>(
                std::forward<Self>(self).subscript_impl(
                    std::forward<C>(c),
                    std::forward<A>(a)...
                )
            )} noexcept;})
            requires (
                (N + sizeof...(A)) < sizeof...(K) &&
                !requires{{std::forward<Self>(self).template subscript<N>(
                    std::forward<C>(c),
                    std::forward<A>(a)...,
                    std::forward<Self>(self).idx.template get<N + sizeof...(A)>()
                )};} &&
                requires{{std::forward<Self>(self).template operator()<N + sizeof...(A)>(
                    std::forward<Self>(self).subscript_impl(
                        std::forward<C>(c),
                        std::forward<A>(a)...
                    )
                )};}
            )
        {
            return (std::forward<Self>(self).template operator()<N + sizeof...(A)>(
                std::forward<Self>(self).subscript_impl(
                    std::forward<C>(c),
                    std::forward<A>(a)...
                )
            ));
        }

        template <typename C>
        using get_vtable = impl::tuple_vtable<meta::remove_rvalue<meta::remove_range<C>>>::dispatch;

        template <size_t N, typename Self, typename C>
        constexpr auto get_dispatch(this Self&& self, C&& c)
            noexcept (requires{{get_vtable<C>{
                size_t(impl::normalize_index(
                    meta::tuple_size<meta::remove_range<C>>,
                    std::forward<Self>(self).idx.template get<N>()
                ))
            }} noexcept;})
            requires (requires{{get_vtable<C>{
                size_t(impl::normalize_index(
                    meta::tuple_size<meta::remove_range<C>>,
                    std::forward<Self>(self).idx.template get<N>()
                ))
            }};})
        {
            return get_vtable<C>{
                size_t(impl::normalize_index(
                    meta::tuple_size<meta::remove_range<C>>,
                    std::forward<Self>(self).idx.template get<N>()
                ))
            };
        }

        template <size_t N, typename Self, typename C>
        constexpr decltype(auto) get(this Self&& self, C&& c)
            noexcept (requires{{range(std::forward<Self>(self).template get_dispatch<N>(
                std::forward<C>(c)
            )(*std::forward<C>(c)))} noexcept;})
            requires (
                N + 1 == sizeof...(K) &&
                meta::range<C> &&
                requires{{range(std::forward<Self>(self).template get_dispatch<N>(
                    std::forward<C>(c)
                )(*std::forward<C>(c)))};}
            )
        {
            return (range(std::forward<Self>(self).template get_dispatch<N>(
                std::forward<C>(c)
            )(*std::forward<C>(c))));
        }

        template <size_t N, typename Self, typename C>
        constexpr decltype(auto) get(this Self&& self, C&& c)
            noexcept (requires{{std::forward<Self>(self).template get_dispatch<N>(
                std::forward<C>(c)
            )(std::forward<C>(c))} noexcept;})
            requires (
                N + 1 == sizeof...(K) &&
                !meta::range<C> &&
                requires{{std::forward<Self>(self).template get_dispatch<N>(
                    std::forward<C>(c)
                )(std::forward<C>(c))};}
            )
        {
            return (std::forward<Self>(self).template get_dispatch<N>(
                std::forward<C>(c)
            )(std::forward<C>(c)));
        }

        template <size_t N, typename Self, typename C>
        constexpr decltype(auto) get(this Self&& self, C&& c)
            noexcept (requires{{std::forward<Self>(self).template operator()<N + 1>(
                range(std::forward<Self>(self).template get_dispatch<N>(
                    std::forward<C>(c)
                )(*std::forward<C>(c)))
            )} noexcept;})
            requires (
                N + 1 < sizeof...(K) &&
                meta::range<C> &&
                requires{{std::forward<Self>(self).template operator()<N + 1>(
                    range(std::forward<Self>(self).template get_dispatch<N>(
                        std::forward<C>(c)
                    )(*std::forward<C>(c)))
                )};}
            )
        {
            return (std::forward<Self>(self).template operator()<N + 1>(
                range(std::forward<Self>(self).template get_dispatch<N>(
                    std::forward<C>(c)
                )(*std::forward<C>(c)))
            ));
        }

        template <size_t N, typename Self, typename C>
        constexpr decltype(auto) get(this Self&& self, C&& c)
            noexcept (requires{{std::forward<Self>(self).template operator()<N + 1>(
                std::forward<Self>(self).template get_dispatch<N>(
                    std::forward<C>(c)
                )(std::forward<C>(c))
            )} noexcept;})
            requires (
                N + 1 < sizeof...(K) &&
                !meta::range<C> &&
                requires{{std::forward<Self>(self).template operator()<N + 1>(
                    std::forward<Self>(self).template get_dispatch<N>(
                        std::forward<C>(c)
                    )(std::forward<C>(c))
                )};}
            )
        {
            return (std::forward<Self>(self).template operator()<N + 1>(
                std::forward<Self>(self).template get_dispatch<N>(
                    std::forward<C>(c)
                )(std::forward<C>(c))
            ));
        }

        template <size_t N, typename Self, typename C>
        constexpr decltype(auto) offset(this Self&& self, C&& c)
            noexcept (requires{{range(impl::range_index::offset(
                *std::forward<C>(c),
                std::forward<Self>(self).idx.template get<N>()
            ))} noexcept;})
            requires (
                N + 1 == sizeof...(K) &&
                meta::range<C> &&
                requires{{range(impl::range_index::offset(
                    *std::forward<C>(c),
                    std::forward<Self>(self).idx.template get<N>()
                ))};}
            )
        {
            return (range(impl::range_index::offset(
                *std::forward<C>(c),
                std::forward<Self>(self).idx.template get<N>()
            )));
        }

        template <size_t N, typename Self, typename C>
        constexpr decltype(auto) offset(this Self&& self, C&& c)
            noexcept (requires{{impl::range_index::offset(
                std::forward<C>(c),
                std::forward<Self>(self).idx.template get<N>()
            )} noexcept;})
            requires (
                N + 1 == sizeof...(K) &&
                !meta::range<C> &&
                requires{{impl::range_index::offset(
                    std::forward<C>(c),
                    std::forward<Self>(self).idx.template get<N>()
                )};}
            )
        {
            return (impl::range_index::offset(
                std::forward<C>(c),
                std::forward<Self>(self).idx.template get<N>()
            ));
        }

        template <size_t N, typename Self, typename C>
        constexpr decltype(auto) offset(this Self&& self, C&& c)
            noexcept (requires{{std::forward<Self>(self).template operator()<N + 1>(
                range(impl::range_index::offset(
                    *std::forward<C>(c),
                    std::forward<Self>(self).idx.template get<N>()
                ))
            )} noexcept;})
            requires (
                N + 1 < sizeof...(K) &&
                meta::range<C> &&
                requires{{std::forward<Self>(self).template operator()<N + 1>(
                    range(impl::range_index::offset(
                        *std::forward<C>(c),
                        std::forward<Self>(self).idx.template get<N>()
                    ))
                )};}
            )
        {
            return (std::forward<Self>(self).template operator()<N + 1>(
                range(impl::range_index::offset(
                    *std::forward<C>(c),
                    std::forward<Self>(self).idx.template get<N>()
                ))
            ));
        }

        template <size_t N, typename Self, typename C>
        constexpr decltype(auto) offset(this Self&& self, C&& c)
            noexcept (requires{{std::forward<Self>(self).template operator()<N + 1>(
                impl::range_index::offset(
                    std::forward<C>(c),
                    std::forward<Self>(self).idx.template get<N>()
                ))
            } noexcept;})
            requires (
                N + 1 < sizeof...(K) &&
                !meta::range<C> &&
                requires{{std::forward<Self>(self).template operator()<N + 1>(
                    impl::range_index::offset(
                        std::forward<C>(c),
                        std::forward<Self>(self).idx.template get<N>()
                    ))
                };}
            )
        {
            return (std::forward<Self>(self).template operator()<N + 1>(
                impl::range_index::offset(
                    std::forward<C>(c),
                    std::forward<Self>(self).idx.template get<N>()
                )
            ));
        }

        template <size_t N, typename Self, typename C>
        constexpr decltype(auto) invoke(this Self&& self, C&& c)
            noexcept (requires{{range(std::forward<Self>(self).idx.template get<N>()(
                std::forward<C>(c)
            ))} noexcept;})
            requires (
                N + 1 == sizeof...(K) &&
                meta::range<C> &&
                requires{{range(std::forward<Self>(self).idx.template get<N>()(
                    std::forward<C>(c)
                ))};}
            )
        {
            return (range(std::forward<Self>(self).idx.template get<N>()(std::forward<C>(c))));
        }

        template <size_t N, typename Self, typename C>
        constexpr decltype(auto) invoke(this Self&& self, C&& c)
            noexcept (requires{{std::forward<Self>(self).idx.template get<N>()(
                std::forward<C>(c)
            )} noexcept;})
            requires (
                N + 1 == sizeof...(K) &&
                !meta::range<C> &&
                requires{{std::forward<Self>(self).idx.template get<N>()(
                    std::forward<C>(c)
                )};}
            )
        {
            return (std::forward<Self>(self).idx.template get<N>()(std::forward<C>(c)));
        }

        template <size_t N, typename Self, typename C>
        constexpr decltype(auto) invoke(this Self&& self, C&& c)
            noexcept (requires{{std::forward<Self>(self).template operator()<N + 1>(
                range(std::forward<Self>(self).idx.template get<N>()(std::forward<C>(c)))
            )} noexcept;})
            requires (
                N + 1 < sizeof...(K) &&
                meta::range<C> &&
                requires{{std::forward<Self>(self).template operator()<N + 1>(
                    range(std::forward<Self>(self).idx.template get<N>()(std::forward<C>(c)))
                )};}
            )
        {
            return (std::forward<Self>(self).template operator()<N + 1>(
                range(std::forward<Self>(self).idx.template get<N>()(std::forward<C>(c)))
            ));
        }

        template <size_t N, typename Self, typename C>
        constexpr decltype(auto) invoke(this Self&& self, C&& c)
            noexcept (requires{{std::forward<Self>(self).template operator()<N + 1>(
                std::forward<Self>(self).idx.template get<N>()(std::forward<C>(c)))
            } noexcept;})
            requires (
                N + 1 < sizeof...(K) &&
                !meta::range<C> &&
                requires{{std::forward<Self>(self).template operator()<N + 1>(
                    std::forward<Self>(self).idx.template get<N>()(std::forward<C>(c)))
                };}
            )
        {
            return (std::forward<Self>(self).template operator()<N + 1>(
                std::forward<Self>(self).idx.template get<N>()(std::forward<C>(c))
            ));
        }

    public:
        template <size_t N = 0, typename C> requires (N == sizeof...(K))
        [[nodiscard]] static constexpr decltype(auto) operator()(C&& c) noexcept {
            return (std::forward<C>(c));
        }

        template <size_t N = 0, typename Self, typename C> requires (N < sizeof...(K))
        [[nodiscard]] constexpr decltype(auto) operator()(this Self&& self, C&& c)
            noexcept (
                requires{{std::forward<Self>(self).template subscript<N>(std::forward<C>(c))};} ?
                requires{{std::forward<Self>(self).template subscript<N>(std::forward<C>(c))} noexcept;} : (
                    requires{{std::forward<Self>(self).template get<N>(std::forward<C>(c))};} ?
                    requires{{std::forward<Self>(self).template get<N>(std::forward<C>(c))} noexcept;} : (
                        requires{{std::forward<Self>(self).template offset<N>(std::forward<C>(c))};} ?
                        requires{{std::forward<Self>(self).template offset<N>(std::forward<C>(c))} noexcept;} :
                        requires{{std::forward<Self>(self).template invoke<N>(std::forward<C>(c))} noexcept;}
                    )
                )
            )
            requires (
                requires{{std::forward<Self>(self).template subscript<N>(std::forward<C>(c))};} ||
                requires{{std::forward<Self>(self).template get<N>(std::forward<C>(c))};} ||
                requires{{std::forward<Self>(self).template offset<N>(std::forward<C>(c))};} ||
                requires{{std::forward<Self>(self).template invoke<N>(std::forward<C>(c))};}
            )
        {
            if constexpr (requires{{std::forward<Self>(self).template subscript<N>(std::forward<C>(c))};}) {
                return (std::forward<Self>(self).template subscript<N>(std::forward<C>(c)));
            } else if constexpr (requires{{std::forward<Self>(self).template get<N>(std::forward<C>(c))};}) {
                return (std::forward<Self>(self).template get<N>(std::forward<C>(c)));
            } else if constexpr (requires{{std::forward<Self>(self).template offset<N>(std::forward<C>(c))};}) {
                return (std::forward<Self>(self).template offset<N>(std::forward<C>(c)));
            } else {
                return (std::forward<Self>(self).template invoke<N>(std::forward<C>(c)));
            }
        }
    };

    template <typename... K>
    at(K&&...) -> at<type<meta::remove_rvalue<K>>...>;

    /* A wrapper for an arbitrary container type that can be used to form iterable
    expressions.

    Ranges can be constructed in a variety of ways, effectively replacing each of the
    following with a unified CTAD constructor (in order of preference):

        1.  `std::views::all(container)` -> `range(container)`, where `container` is any
            iterable or tuple-like type.
        2.  `std::ranges::subrange(begin, end)` -> `range(begin, end)`, where `begin` is
            an iterator and `end` is a matching sentinel.
        3.  `std::views::iota(start, stop)` -> `range(start, stop)`, where `start` and
            `stop` are arbitrary types for which `++start` and `start < stop` are
            well-formed.  Also permits an optional third `step` argument, which represents
            a step size that will be used between increments.  If the expression
            `start += step` is well-formed, then it will be used to obtain each value in
            constant time.  Otherwise, the step size must be default-constructible and
            support `step < step`, causing an inner loop to call either `++start` or
            `--start` to obtain each value in linear time, depending on the sign of `step`.
        4.  `std::views::iota(Stop{}, stop)` -> `range(stop)`, which corresponds to a
            Python-style `range` expression, where the start index is default-constructed
            with the same type as `stop`.  All other rules from (3) still apply, except
            that no step size is permitted (use the 2-argument form if a step size is
            needed).
        5.  `std::views::iota(start)` -> `range(start, None)`, representing an infinite
            range beginning at `start` and applying `++start` at every iteration.  A step
            size can be provided as an optional third argument, according to the rules laid
            out in (3).
        6.  `std::views::counted(begin, size)` -> `range(begin, size)`, where `begin` is
            an iterator and `size` is an unsigned integer.

    If the underlying container is tuple-like, then the range will be as well, and will
    forward to the container's `get<I>()` method when accessed or destructured.  If the
    tuple is not directly iterable, then an iterator will be generated for it, which may
    yield `Union<Ts...>`, where `Ts...` are the unique return types for each index.  All
    tuples, regardless of implementation, should therefore produce iterable ranges just
    like any other container.

    `range` has a number of subclasses, all of which extend the basic iteration interface
    in some way:

    /// TODO: `unpack` no longer exists

        1.  `unpack`: a trivial extension of `range` that behaves identically, and is
            returned by the prefix `*` operator for iterable and tuple-like containers.
            This is essentially equivalent to the standard `range` constructor; the only
            difference is that when an `unpack` range is provided to a Bertrand function
            (e.g. a `def` statement), it will be destructured into individual arguments,
            emulating Python-style container unpacking.  Applying a second prefix `*`
            promotes the `unpack` range into a keyword range, which destructures to
            keyword arguments if the function supports them.  Otherwise, when used in any
            context other than function calls, the prefix `*` operator simply provides a
            convenient entry point for range-based expressions over supported container
            types, separate from the `range` constructor.
        2.  `slice`: an extension of `range` that includes only a subset of the elements in
            a range, according to Python-style slicing semantics.  This can fully replace
            `std::views::take`, `std::views::drop`, and `std::views::stride`, as well as
            some uses of `std::views::reverse`, which can be implemented as a `slice` with
            a negative step size.  Note that `slice` cannot be tuple-like, since the
            included indices are only known at run time.
        3.  `mask`: an extension of `range` that includes only the elements that correspond
            to the `true` indices in a boolean mask.  This provides a finer level of
            control than `slice`, and can replace many uses of `std::views::filter` when a
            boolean mask is already available or can be easily generated.
        4.  `comprehension`: an extension of `range` that stores a function that will be
            applied elementwise over each value in the range.  This is similar to
            `std::views::transform`, but allows for more complex transformations, including
            destructuring and visitation for union and tuple elements consistent with the
            rest of Bertrand's pattern matching interface.  `comprehension`s also serve as
            the basic building blocks for arbitrary expression trees, and can replace
            `std::views::repeat` and any uses of `std::views::filter` that do not fall
            under the `mask` criteria simply by returning a nested `range`, which will be
            flattened into the result.
        5.  `zip`: an extension of `range` that fuses multiple ranges, and yields tuples of
            the corresponding elements, terminating when the shortest range has been
            exhausted.  This effectively replaces `std::views::zip` and
            `std::views::enumerate`.

        /// TODO: update these with the rest of the range interface when implemented

    Each subclass of `range` is also exposed to Bertrand's monadic operator interface,
    which returns lazily-evaluated `comprehension`s that encode each operation into an
    expression tree.  The tree will only be evaluated when the range is indexed, iterated
    over, or converted to a compatible type, which reduces it to a single loop that can be
    aggressively optimized by the compiler. */
    template <impl::range_concept C = impl::empty_range>
    struct range : impl::range_tag {
        [[no_unique_address]] impl::ref<C> __value;

        [[nodiscard]] constexpr range() = default;
        [[nodiscard]] constexpr range(const range&) = default;
        [[nodiscard]] constexpr range(range&&) = default;

        template <typename... A> requires (sizeof...(A) > 0)
        [[nodiscard]] constexpr range(A&&... args)
            noexcept (requires{{impl::ref<C>{C(std::forward<A>(args)...)}} noexcept;})
            requires (requires{{impl::ref<C>{C(std::forward<A>(args)...)}};})
        :
            __value{C(std::forward<A>(args)...)}
        {}

        template <
            typename Start,
            typename Stop = impl::iota_tag,
            typename Step = impl::iota_tag
        >
        [[nodiscard]] constexpr explicit range(
            Start&& start,
            Stop&& stop,
            Step&& step = {}
        )
            requires (!meta::iterator<Start> && meta::inherits<C, impl::iota_tag>)
        :
            __value{C(
                std::forward<Start>(start),
                std::forward<Stop>(stop),
                std::forward<Step>(step)
            )}
        {}

        template <
            typename Start,
            typename Stop = impl::subrange_tag,
            typename Step = impl::subrange_tag
        >
        [[nodiscard]] constexpr explicit range(
            Start&& start,
            Stop&& stop,
            Step&& step = {}
        )
            requires (meta::iterator<Start> && meta::inherits<C, impl::subrange_tag>)
        :
            __value{C(
                std::forward<Start>(start),
                std::forward<Stop>(stop),
                std::forward<Step>(step)
            )}
        {}

        /* `swap()` operator between ranges. */
        constexpr void swap(range& other)
            noexcept (requires{{std::ranges::swap(__value, other.__value)} noexcept;})
            requires (requires{{std::ranges::swap(__value, other.__value)};})
        {
            std::ranges::swap(__value, other.__value);
        }

        /* Perfectly forward the underlying container or scalar value. */
        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator*(this Self&& self)
            noexcept (
                !impl::range_transparent<C> ||
                requires{{**std::forward<Self>(self).__value} noexcept;}
            )
            requires (
                !impl::range_transparent<C> ||
                requires{{**std::forward<Self>(self).__value};}
            )
        {
            if constexpr (impl::range_transparent<C>) {
                return (**std::forward<Self>(self).__value);
            } else {
                return (*std::forward<Self>(self).__value);
            }
        }

        /* Indirectly access a member of the underlying container or scalar value. */
        [[nodiscard]] constexpr auto operator->()
            noexcept (!impl::range_transparent<C> || requires{{**__value} noexcept;})
            requires (!impl::range_transparent<C> || requires{{**__value};})
        {
            return std::addressof(**this);
        }

        /* Indirectly access a member of the underlying container or scalar value. */
        [[nodiscard]] constexpr auto operator->() const
            noexcept (!impl::range_transparent<C> || requires{{**__value} noexcept;})
            requires (!impl::range_transparent<C> || requires{{**__value};})
        {
            return std::addressof(**this);
        }

        /* Get a forward iterator to the start of the range. */
        [[nodiscard]] constexpr auto begin()
            noexcept (requires{{impl::range_iterator{meta::begin(*__value)}} noexcept;})
            requires (requires{{impl::range_iterator{meta::begin(*__value)}};})
        {
            return impl::range_iterator{meta::begin(*__value)};
        }

        /* Get a forward iterator to the start of the range. */
        [[nodiscard]] constexpr auto begin() const
            noexcept (requires{{impl::range_iterator{meta::begin(*__value)}} noexcept;})
            requires (requires{{impl::range_iterator{meta::begin(*__value)}};})
        {
            return impl::range_iterator{meta::begin(*__value)};
        }

        /* Get a forward iterator to the start of the range. */
        [[nodiscard]] constexpr auto cbegin() const
            noexcept (requires{{impl::range_iterator{meta::cbegin(*__value)}} noexcept;})
            requires (requires{{impl::range_iterator{meta::cbegin(*__value)}};})
        {
            return impl::range_iterator{meta::cbegin(*__value)};
        }

        /* Get a forward iterator to one past the last element of the range. */
        [[nodiscard]] constexpr auto end()
            noexcept (requires{{impl::range_iterator{meta::end(*__value)}} noexcept;})
            requires (requires{{impl::range_iterator{meta::end(*__value)}};})
        {
            return impl::range_iterator{meta::end(*__value)};
        }

        /* Get a forward iterator to one past the last element of the range. */
        [[nodiscard]] constexpr auto end() const
            noexcept (requires{{impl::range_iterator{meta::end(*__value)}} noexcept;})
            requires (requires{{impl::range_iterator{meta::end(*__value)}};})
        {
            return impl::range_iterator{meta::end(*__value)};
        }

        /* Get a forward iterator to one past the last element of the range. */
        [[nodiscard]] constexpr auto cend() const
            noexcept (requires{{impl::range_iterator{meta::cend(*__value)}} noexcept;})
            requires (requires{{impl::range_iterator{meta::cend(*__value)}};})
        {
            return impl::range_iterator{meta::cend(*__value)};
        }

        /* Get a reverse iterator to the last element of the range. */
        [[nodiscard]] constexpr auto rbegin()
            noexcept (requires{{impl::range_iterator{meta::rbegin(*__value)}} noexcept;})
            requires (requires{{impl::range_iterator{meta::rbegin(*__value)}};})
        {
            return impl::range_iterator{meta::rbegin(*__value)};
        }

        /* Get a reverse iterator to the last element of the range. */
        [[nodiscard]] constexpr auto rbegin() const
            noexcept (requires{{impl::range_iterator{meta::rbegin(*__value)}} noexcept;})
            requires (requires{{impl::range_iterator{meta::rbegin(*__value)}};})
        {
            return impl::range_iterator{meta::rbegin(*__value)};
        }

        /* Get a reverse iterator to the last element of the range. */
        [[nodiscard]] constexpr auto crbegin() const
            noexcept (requires{{impl::range_iterator{meta::crbegin(*__value)}} noexcept;})
            requires (requires{{impl::range_iterator{meta::crbegin(*__value)}};})
        {
            return impl::range_iterator{meta::crbegin(*__value)};
        }

        /* Get a reverse iterator to one before the first element of the range. */
        [[nodiscard]] constexpr auto rend()
            noexcept (requires{{impl::range_iterator{meta::rend(*__value)}} noexcept;})
            requires (requires{{impl::range_iterator{meta::rend(*__value)}};})
        {
            return impl::range_iterator{meta::rend(*__value)};
        }

        /* Get a reverse iterator to one before the first element of the range. */
        [[nodiscard]] constexpr auto rend() const
            noexcept (requires{{impl::range_iterator{meta::rend(*__value)}} noexcept;})
            requires (requires{{impl::range_iterator{meta::rend(*__value)}};})
        {
            return impl::range_iterator{meta::rend(*__value)};
        }

        /* Get a reverse iterator to one before the first element of the range. */
        [[nodiscard]] constexpr auto crend() const
            noexcept (requires{{impl::range_iterator{meta::crend(*__value)}} noexcept;})
            requires (requires{{impl::range_iterator{meta::crend(*__value)}};})
        {
            return impl::range_iterator{meta::crend(*__value)};
        }

        /* Get a pointer to the underlying data array, if one exists.  This is
        identical to a `std::ranges::data()` call on the underlying value, assuming
        that expression is well-formed.  For scalars, this returns a pointer to the
        underlying value. */
        [[nodiscard]] constexpr auto data()
            noexcept (requires{{meta::data(*__value)} noexcept;})
            requires (requires{{meta::data(*__value)};})
        {
            return meta::data(*__value);
        }

        /* Get a pointer to the underlying data array, if one exists.  This is
        identical to a `std::ranges::data()` call on the underlying value, assuming
        that expression is well-formed.  For scalars, this returns a pointer to the
        underlying value. */
        [[nodiscard]] constexpr auto data() const
            noexcept (requires{{meta::data(*__value)} noexcept;})
            requires (requires{{meta::data(*__value)};})
        {
            return meta::data(*__value);
        }

        /* Get a pointer to the underlying data array, if one exists.  This is
        identical to a `std::ranges::cdata()` call on the underlying value, assuming
        that expression is well-formed.  For scalars, this returns a pointer to the
        underlying value. */
        [[nodiscard]] constexpr auto cdata() const
            noexcept (requires{{meta::cdata(*__value)} noexcept;})
            requires (requires{{meta::cdata(*__value)};})
        {
            return meta::cdata(*__value);
        }

        /* Forwarding `shape()` operator for the underlying container, provided one can
        be deduced using the `meta::shape()` metafunction.  Note that sized iterables
        always produce a shape of at least one dimension.

        This method always returns an `impl::extent<rank>` object, where `rank` is
        equal to the number of dimensions in the container's shape, or `None` if they
        cannot be determined at compile time (therefore yielding a dynamic shape).  In
        both cases, the `extent` object behaves like a read-only
        `std::array<size_t, N>` or `std::vector<size_t>`, respectively. */
        [[nodiscard]] constexpr auto shape() const
            noexcept (requires{{impl::extent(meta::shape(**this))} noexcept;})
            requires (requires{{impl::extent(meta::shape(**this))};})
        {
            return impl::extent(meta::shape(**this));
        }

        /* Forwarding `size()` operator for the underlying container, provided the
        container supports it.  This is identical to a `std::ranges::size()` call on
        the underlying value.  Scalars always have a size of 1. */
        [[nodiscard]] constexpr decltype(auto) size() const
            noexcept (requires{{meta::size(*__value)} noexcept;})
            requires (requires{{meta::size(*__value)};})
        {
            return (meta::size(*__value));
        }

        /* Forwarding `ssize()` operator for the underlying container, provided the
        container supports it.  This is identical to a `std::ranges::ssize()` call
        unless the underlying container exposes a `val.ssize()` member method or
        `ssize(val)` ADL method.  Scalars always have a size of 1. */
        [[nodiscard]] constexpr decltype(auto) ssize() const
            noexcept (requires{{meta::ssize(*__value)} noexcept;})
            requires (
                !requires{{__value->ssize()} -> meta::signed_integer;} &&
                requires{{meta::ssize(*__value)};}
            )
        {
            return (meta::ssize(*__value));
        }

        /* Forwarding `empty()` operator for the underlying container, provided the
        container supports it.  This is identical to a `std::ranges::empty()` call on
        the underlying value.  It is always false for scalar ranges. */
        [[nodiscard]] constexpr bool empty() const
            noexcept (requires{{std::ranges::empty(*__value)} noexcept;})
            requires (requires{{std::ranges::empty(*__value)};})
        {
            return std::ranges::empty(*__value);
        }

        /* Access the first element in the underlying container by searching for an
        appropriate `front()` member or ADL method, or dereferencing the `begin()`
        iterator.  The result will always be returned as a (possibly scalar) range,
        just like the indexing and tuple access operators.

        Note that no extra bounds checking is performed to guard against empty ranges,
        maintaining the zero-cost guarantee for the underlying container.  Individual
        containers may implement bounds checking in their `front()` method if desired,
        which is the case for all of Bertrand's core container types (via a debug
        assertion). */
        template <typename Self>
        [[nodiscard]] constexpr auto front(this Self&& self)
            noexcept (requires{
                {iter::range(meta::front(*std::forward<Self>(self).__value))} noexcept;
            })
            requires (requires{{iter::range(meta::front(*std::forward<Self>(self).__value))};})
        {
            return iter::range(meta::front(*std::forward<Self>(self).__value));
        }

        /* Access the last element in the underlying container by searching for an
        appropriate `back()` member or ADL method, dereferencing the `rbegin()`
        iterator, advancing `begin()` to the last element via `it += ssize() - 1`, or
        decrementing the `end()` iterator, as appropriate.  The result will always be
        returned as a (possibly scalar) range, just like the indexing and tuple access
        operators.

        Note that no extra bounds checking is performed to guard against empty ranges,
        maintaining the zero-cost guarantee for the underlying container.  Individual
        containers may implement bounds checking in their `back()` method if desired,
        which is the case for all of Bertrand's core container types (via a debug
        assertion). */
        template <typename Self>
        [[nodiscard]] constexpr auto back(this Self&& self)
            noexcept (requires{
                {iter::range(meta::back(*std::forward<Self>(self).__value))} noexcept;
            })
            requires (requires{{iter::range(meta::back(*std::forward<Self>(self).__value))};})
        {
            return iter::range(meta::back(*std::forward<Self>(self).__value));
        }

        /* Range-based multidimensional tuple accessor.  This is expression-equivalent
        to `iter::at<K...>{}(self)`.  See that algorithm for more information. */
        template <auto... K, typename Self>
        [[nodiscard]] constexpr decltype(auto) get(this Self&& self)
            noexcept (requires{{at<K...>{}(std::forward<Self>(self))} noexcept;})
            requires (requires{{at<K...>{}(std::forward<Self>(self))};})
        {
            return (at<K...>{}(std::forward<Self>(self)));
        }

        /* Range-based multidimensional indexing operator.  This is
        expression-equivalent to `iter::at{k...}(self)`.  See that algorithm for more
        information. */
        template <typename Self, typename... K>
        [[nodiscard]] constexpr decltype(auto) operator[](this Self&& self, K&&... k)
            noexcept (requires{{at{std::forward<K>(k)...}(std::forward<Self>(self))} noexcept;})
            requires (requires{{at{std::forward<K>(k)...}(std::forward<Self>(self))};})
        {
            return (at{std::forward<K>(k)...}(std::forward<Self>(self)));
        }

        /* Range-based membership test.  This is expression-equivalent to
        `iter::contains{k}(self)`.  See that algorithm for more information. */
        template <typename T>
        [[nodiscard]] constexpr bool contains(const T& k) const
            noexcept (requires{{iter::contains{k}(*this)} noexcept;})
            requires (requires{{iter::contains{k}(*this)};})
        {
            return iter::contains{k}(*this);
        }

        /* Prefer direct conversions for the underlying container if possible. */
        template <typename Self, typename to> requires (!meta::prefer_constructor<to>)
        [[nodiscard]] constexpr operator to(this Self&& self)
            noexcept (requires{
                {*std::forward<Self>(self)} noexcept -> meta::nothrow::convertible_to<to>;
            })
            requires (impl::range_convert::direct<Self, to>)
        {
            return *std::forward<Self>(self);
        }

        /* If no direct conversion exists, allow conversion to any type `T` that
        implements a constructor of the form `T(std::from_range, self)`. */
        template <typename Self, typename to> requires (!meta::prefer_constructor<to>)
        [[nodiscard]] constexpr operator to(this Self&& self)
            noexcept (requires{{to(std::from_range, std::forward<Self>(self))} noexcept;} || (
                !requires{{to(std::from_range, std::forward<Self>(self))};} &&
                requires{{to(std::from_range, *std::forward<Self>(self).__value)} noexcept;}
            ))
            requires (
                !impl::range_convert::direct<Self, to> &&
                impl::range_convert::construct<Self, to>
            )
        {
            if constexpr (requires{{to(std::from_range, std::forward<Self>(self))};}) {
                return to(std::from_range, std::forward<Self>(self));
            } else {
                return to(std::from_range, *std::forward<Self>(self).__value);
            }
        }

        /* If neither a direct conversion nor a `std::from_range` constructor exist,
        check for a constructor of the form `T(begin(), end())`. */
        template <typename Self, typename to> requires (!meta::prefer_constructor<to>)
        [[nodiscard]] constexpr operator to(this Self&& self)
            noexcept (requires{{to(self.begin(), self.end())} noexcept;} || (
                !requires{{to(self.begin(), self.end())};} &&
                requires{{to(self.__value->begin(), self.__value->end())} noexcept;}
            ))
            requires (
                !impl::range_convert::direct<Self, to> &&
                !impl::range_convert::construct<Self, to> &&
                impl::range_convert::traverse<Self, to>
            )
        {
            if constexpr (requires{{to(self.begin(), self.end())};}) {
                return to(self.begin(), self.end());
            } else {
                return to(meta::begin(*self.__value), meta::end(*self.__value));
            }
        }

        /* If no direct conversion or constructor exists, and both the range and the
        target type are tuple-like, then a fold expression may be used to directly
        construct the target (via braced initialization) from the elements of the
        range. */
        template <typename Self, meta::tuple_like to> requires (!meta::prefer_constructor<to>)
        [[nodiscard]] constexpr operator to(this Self&& self)
            noexcept (requires{{impl::range_convert::tuple_to_tuple_fn<to>(
                std::forward<Self>(self),
                std::make_index_sequence<meta::tuple_size<to>>{}
            )} noexcept;})
            requires (
                !impl::range_convert::direct<Self, to> &&
                !impl::range_convert::construct<Self, to> &&
                !impl::range_convert::traverse<Self, to> &&
                impl::range_convert::tuple_to_tuple<Self, to>
            )
        {
            return impl::range_convert::tuple_to_tuple_fn<to>(
                std::forward<Self>(self),
                std::make_index_sequence<meta::tuple_size<to>>{}
            );
        }

        /* If no direct conversion or constructor exists, and the target type is
        tuple-like but the range is not, then an iterator-based fold expression may be
        used to directly construct the target (via braced initialization), which
        dereferences the iterator at each step.  If the range size does not exactly
        match the target tuple size, then a `TypeError` will be thrown. */
        template <typename Self, meta::tuple_like to> requires (!meta::prefer_constructor<to>)
        [[nodiscard]] constexpr operator to(this Self&& self)
            requires (
                !impl::range_convert::direct<Self, to> &&
                !impl::range_convert::construct<Self, to> &&
                !impl::range_convert::traverse<Self, to> &&
                !impl::range_convert::tuple_to_tuple<Self, to> &&
                impl::range_convert::iter_to_tuple<Self, to>
            )
        {
            return impl::range_convert::iter_to_tuple_fn<to>(
                self,
                std::make_index_sequence<meta::tuple_size<to>>{}
            );
        }

        /* If no implicit conversion would be valid, then explicit conversions are
        allowed if and only if they represent a direct conversion from the underlying
        container.  Note that this includes things like contextual boolean conversions,
        which are only enabled if they would be valid for the underlying value.
        Otherwise, ranges are not considered to be boolean testable, in order to
        prevent common bugs related to that behavior. */
        template <typename Self, typename to> requires (!meta::prefer_constructor<to>)
        [[nodiscard]] explicit constexpr operator to(this Self&& self)
            requires (
                !impl::range_convert::direct<Self, to> &&
                !impl::range_convert::construct<Self, to> &&
                !impl::range_convert::traverse<Self, to> &&
                !impl::range_convert::tuple_to_tuple<Self, to> &&
                !impl::range_convert::iter_to_tuple<Self, to> &&
                requires{{*std::forward<Self>(self)} -> meta::explicitly_convertible_to<to>;}
            )
        {
            return static_cast<to>(*std::forward<Self>(self));
        }

        constexpr range& operator=(const range&) = default;
        constexpr range& operator=(range&&) = default;

        /// TODO: assignment operators may need to account for ranges always being
        /// nested.  I'm not sure exactly how that needs to be done at the moment, and
        /// it's possible assignment doesn't even require it.

        /* Prefer direct assignment to the underlying container if possible.  Note that
        this skips over any intermediate ranges if the type is nested. */
        template <typename Self, typename T>
        constexpr Self operator=(this Self&& self, T&& c)
            noexcept (requires{{*self = meta::strip_range(std::forward<T>(c))} noexcept;})
            requires (impl::range_assign::direct<Self, T>)
        {
            *self = meta::strip_range(std::forward<T>(c));
            if constexpr (meta::rvalue<Self>) {
                return std::move(self);
            } else {
                return self;
            }
        }

        /* A special case of direct assignment from an initializer list, assuming the
        container supports it. */
        template <typename Self, typename T>
        constexpr Self operator=(this Self&& self, std::initializer_list<T> il)
            noexcept (requires{{*self = std::move(il)} noexcept;})
            requires (impl::range_assign::direct<Self, std::initializer_list<T>>)
        {
            *self = std::move(il);
            if constexpr (meta::rvalue<Self>) {
                return std::move(self);
            } else {
                return self;
            }
        }

        /* If no direct assignment exists, and the other operand is not a range, then
        it must be a scalar that can be assigned across the range using `begin()` as
        an output iterator. */
        template <typename Self, typename T> requires (!meta::range<T>)
        constexpr Self operator=(this Self&& self, const T& v)
            noexcept (meta::nothrow::iterable<Self> && requires(meta::yield_type<Self> x) {
                {std::forward<decltype(x)>(x) = v} noexcept;
            })
            requires (
                !impl::range_assign::direct<Self, T> &&
                impl::range_assign::scalar<Self, T>
            )
        {
            for (auto&& x : self) std::forward<decltype(x)>(x) = v;
            if constexpr (meta::rvalue<Self>) {
                return std::move(self);
            } else {
                return self;
            }
        }

        /* If the other operand is a range and direct assignment is not available, then
        fall back to elementwise assignment by iterating over both ranges using
        `begin()` as an output iterator.  If the ranges are not the same size, then a
        `TypeError` will be thrown.  If both ranges are tuple-like, then the size check
        will be performed at compile-time instead, causing this method to fail
        compilation. */
        template <typename Self, meta::range T>
        constexpr Self operator=(this Self&& self, T&& r)
            noexcept (requires{{impl::range_assign::iter_from_iter_fn(self, r)} noexcept;})
            requires (
                !impl::range_assign::direct<Self, T> &&
                !impl::range_assign::scalar<Self, T> &&
                impl::range_assign::iter_from_iter<Self, T>
            )
        {
            impl::range_assign::iter_from_iter_fn(self, r);
            if constexpr (meta::rvalue<Self>) {
                return std::move(self);
            } else {
                return self;
            }
        }

        /* A special case of iterator-based, elementwise assignment from an initializer
        list.  If the list is not the same size as the range, then a `TypeError` will
        be thrown. */
        template <typename Self, typename T>
        constexpr Self operator=(this Self&& self, std::initializer_list<T> il)
            noexcept (requires{{impl::range_assign::iter_from_iter_fn(self, il)} noexcept;})
            requires (
                !impl::range_assign::direct<Self, std::initializer_list<T>> &&
                !impl::range_assign::scalar<Self, std::initializer_list<T>> &&
                impl::range_assign::iter_from_iter<Self, std::initializer_list<T>>
            )
        {
            impl::range_assign::iter_from_iter_fn(self, il);
            if constexpr (meta::rvalue<Self>) {
                return std::move(self);
            } else {
                return self;
            }
        }

        /* If the other operand is a range and neither direct assignment nor
        elementwise assignment using iterators is available, then check for
        tuple-based elementwise assignment using a fold expression.  This will only
        be chosen if both ranges are tuple-like, and their sizes match exactly. */
        template <typename Self, meta::range T>
        constexpr Self operator=(this Self&& self, T&& r)
            noexcept (requires{{impl::range_assign::tuple_from_tuple_fn(
                self,
                std::forward<T>(r),
                std::make_index_sequence<meta::tuple_size<C>>{}
            )} noexcept;})
            requires (
                !impl::range_assign::direct<Self, T> &&
                !impl::range_assign::scalar<Self, T> &&
                !impl::range_assign::iter_from_iter<Self, T> &&
                impl::range_assign::tuple_from_tuple<Self, T>
            )
        {
            impl::range_assign::tuple_from_tuple_fn(
                self,
                std::forward<T>(r),
                std::make_index_sequence<meta::tuple_size<C>>{}
            );
            if constexpr (meta::rvalue<Self>) {
                return std::move(self);
            } else {
                return self;
            }
        }

        /* If the other operand is a tuple-like range but this range is not, and
        elementwise assignment using their iterators is not possible, then check for an
        algorithm that uses this range's `begin()` iterator as an output iterator and
        assigns elements from the other range using tuple indexing.  If the ranges are
        not the same size, then a `TypeError` will be thrown. */
        template <typename Self, meta::range T>
        constexpr Self operator=(this Self&& self, T&& r)
            noexcept (requires{{impl::range_assign::iter_from_tuple_fn(
                self,
                std::forward<T>(r),
                std::make_index_sequence<meta::tuple_size<C>>{}
            )} noexcept;})
            requires (
                !impl::range_assign::direct<Self, T> &&
                !impl::range_assign::scalar<Self, T> &&
                !impl::range_assign::iter_from_iter<Self, T> &&
                !impl::range_assign::tuple_from_tuple<Self, T> &&
                impl::range_assign::iter_from_tuple<Self, T>
            )
        {
            impl::range_assign::iter_from_tuple_fn(
                self,
                std::forward<T>(r),
                std::make_index_sequence<meta::tuple_size<T>>{}
            );
            if constexpr (meta::rvalue<Self>) {
                return std::move(self);
            } else {
                return self;
            }
        }

        /* If this range is tuple-like but the other operand is not, and elementwise
        assignment using their iterators is not possible, then check for an algorithm
        that uses the other range's `begin()` iterator as an input iterator and assigns
        each element to this range using tuple indexing.  If the ranges are not the
        same size, then a `TypeError` will be thrown. */
        template <typename Self, meta::range T>
        constexpr Self operator=(this Self&& self, T&& r)
            noexcept (requires{{impl::range_assign::tuple_from_iter_fn(
                self,
                r,
                std::make_index_sequence<meta::tuple_size<C>>{}
            )} noexcept;})
            requires (
                !impl::range_assign::direct<Self, T> &&
                !impl::range_assign::scalar<Self, T> &&
                !impl::range_assign::iter_from_iter<Self, T> &&
                !impl::range_assign::tuple_from_tuple<Self, T> &&
                !impl::range_assign::iter_from_tuple<Self, T> &&
                impl::range_assign::tuple_from_iter<Self, T>
            )
        {
            impl::range_assign::tuple_from_iter_fn(
                self,
                r,
                std::make_index_sequence<meta::tuple_size<C>>{}
            );
            if constexpr (meta::rvalue<Self>) {
                return std::move(self);
            } else {
                return self;
            }
        }

        /* A special case of tuple-based, elementwise assignment from an initializer
        list.  If the list is not the same size as the range, then a `TypeError` will
        be thrown. */
        template <typename Self, typename T>
        constexpr Self operator=(this Self&& self, std::initializer_list<T> il)
            noexcept (requires{{impl::range_assign::tuple_from_iter_fn(
                self,
                il,
                std::make_index_sequence<meta::tuple_size<C>>{}
            )} noexcept;})
            requires (
                !impl::range_assign::direct<Self, std::initializer_list<T>> &&
                !impl::range_assign::scalar<Self, std::initializer_list<T>> &&
                !impl::range_assign::iter_from_iter<Self, std::initializer_list<T>> &&
                !impl::range_assign::tuple_from_tuple<Self, std::initializer_list<T>> &&
                !impl::range_assign::iter_from_tuple<Self, std::initializer_list<T>> &&
                impl::range_assign::tuple_from_iter<Self, std::initializer_list<T>>
            )
        {
            impl::range_assign::tuple_from_iter_fn(
                self,
                il,
                std::make_index_sequence<meta::tuple_size<C>>{}
            );
            if constexpr (meta::rvalue<Self>) {
                return std::move(self);
            } else {
                return self;
            }
        }

        /// TODO: monadic call operator, which may be forward-declared and filled in
        /// later in zip.h, once `zip{}` has been defined.

    };

}


namespace impl {

    template <typename T, extent Shape, typename Category>
        requires (sequence_concept<T, Shape, Category>)
    template <typename C>
    constexpr T sequence_control<T, Shape, Category>::subscript_fn(
        sequence_control* control,
        ssize_t n
    ) {
        return iter::at{n}(*static_cast<meta::as_pointer<Container<C>>>(control->container));
    }

}


}


_LIBCPP_BEGIN_NAMESPACE_STD

    namespace ranges {

        template <typename C>
        constexpr bool enable_borrowed_range<bertrand::iter::range<C>> = borrowed_range<C>;

        template <>
        inline constexpr bool enable_borrowed_range<bertrand::impl::empty_range> = true;

        /// TODO: borrowed range support for single ranges and optionals if the
        /// underlying type is an lvalue or models borrowed_range.

        template <typename T, bertrand::impl::extent Shape, typename Category>
        constexpr bool enable_borrowed_range<bertrand::impl::sequence<T, Shape, Category>> = true;

    }

    template <size_t N>
    struct tuple_size<bertrand::impl::extent<N>> : std::integral_constant<size_t, N> {};

    template <>
    struct tuple_size<bertrand::impl::empty_range> : std::integral_constant<size_t, 0> {};

    template <typename T>
    struct tuple_size<bertrand::impl::scalar_range<T>> : std::integral_constant<size_t, 1> {};

    template <typename T>
    struct tuple_size<bertrand::impl::tuple_range<T>> : std::integral_constant<
        size_t,
        bertrand::meta::tuple_size<T>
    > {};

    template <bertrand::meta::tuple_like T>
    struct tuple_size<bertrand::iter::range<T>> : std::integral_constant<
        size_t,
        bertrand::meta::tuple_size<T>
    > {};

    template <size_t I, size_t N>
    struct tuple_element<I, bertrand::impl::extent<N>> {
        using type = size_t;
    };

    template <size_t I>
    struct tuple_element<I, bertrand::impl::empty_range> {
        using type = const bertrand::NoneType&;
    };

    template <size_t I, typename T> requires (I == 0)
    struct tuple_element<I, bertrand::impl::scalar_range<T>> {
        using type = T;
    };

    template <size_t I, typename T> requires (I < bertrand::meta::tuple_size<T>)
    struct tuple_element<I, bertrand::impl::tuple_range<T>> {
        using type = decltype((bertrand::meta::get<I>(std::declval<T>())));
    };

    template <size_t I, bertrand::meta::tuple_like T> requires (I < bertrand::meta::tuple_size<T>)
    struct tuple_element<I, bertrand::iter::range<T>> {
        using type = decltype((bertrand::meta::get<I>(std::declval<T>())));
    };

    template <ssize_t I, bertrand::meta::extent T>
        requires (bertrand::impl::valid_index<bertrand::meta::tuple_size<T>, I>)
    constexpr decltype(auto) get(T&& t)
        noexcept (requires{{bertrand::meta::get<I>(std::forward<T>(t))} noexcept;})
        requires (requires{{bertrand::meta::get<I>(std::forward<T>(t))};})
    {
        return (bertrand::meta::get<I>(std::forward<T>(t)));
    }

    template <ssize_t I, bertrand::meta::range R>
        requires (
            bertrand::meta::tuple_like<R> &&
            bertrand::impl::valid_index<bertrand::meta::tuple_size<R>, I>
        )
    constexpr decltype(auto) get(R&& r)
        noexcept (requires{{bertrand::meta::get<I>(std::forward<R>(r))} noexcept;})
        requires (requires{{bertrand::meta::get<I>(std::forward<R>(r))};})
    {
        return (bertrand::meta::get<I>(std::forward<R>(r)));
    }

    /// TODO: all these CTAD guide should account for ranges always yielding nested
    /// ranges, and unwrap them automatically.


    template <bertrand::meta::range R>
        requires (bertrand::meta::character<bertrand::meta::yield_type<R>>)
    basic_string(R&& r) -> basic_string<
        bertrand::meta::unqualify<bertrand::meta::yield_type<R>>
    >;

    template <bertrand::meta::contiguous_range R>
        requires (bertrand::meta::character<bertrand::meta::yield_type<R>>)
    basic_string_view(R&& r) -> basic_string_view<
        bertrand::meta::unqualify<bertrand::meta::yield_type<R>>
    >;

    template <bertrand::meta::range R> requires (bertrand::meta::tuple_like<R>)
    array(R&& r) -> array<
        bertrand::meta::remove_reference<bertrand::meta::yield_type<R>>,
        bertrand::meta::tuple_size<R>
    >;

    template <bertrand::meta::range R>
        requires (bertrand::meta::tuple_like<R> && bertrand::meta::tuple_size<R> == 2)
    pair(R&& r) -> pair<bertrand::meta::get_type<R, 0>, bertrand::meta::get_type<R, 1>>;

    /// TODO: tuple, but that will have to be bounded within a finite range or omitted
    /// entirely, since C++ currently doesn't allow me to unpack types directly into
    /// a CTAD guide.
    /// -> Unions and variants have the same problem.

    template <bertrand::meta::range R>
    vector(R&& r) -> vector<bertrand::meta::remove_reference<bertrand::meta::yield_type<R>>>;

    template <bertrand::meta::range R>
    deque(R&& r) -> deque<bertrand::meta::remove_reference<bertrand::meta::yield_type<R>>>;

    template <bertrand::meta::range R>
    list(R&& r) -> list<bertrand::meta::remove_reference<bertrand::meta::yield_type<R>>>;

    template <bertrand::meta::range R>
    forward_list(R&& r) -> forward_list<
        bertrand::meta::remove_reference<bertrand::meta::yield_type<R>>
    >;

    template <bertrand::meta::range R>
    set(R&& r) -> set<bertrand::meta::remove_reference<bertrand::meta::yield_type<R>>>;

    template <bertrand::meta::range R>
    multiset(R&& r) -> multiset<bertrand::meta::remove_reference<bertrand::meta::yield_type<R>>>;

    template <bertrand::meta::range R>
    unordered_set(R&& r) -> unordered_set<
        bertrand::meta::remove_reference<bertrand::meta::yield_type<R>>
    >;

    template <bertrand::meta::range R>
    unordered_multiset(R&& r) -> unordered_multiset<
        bertrand::meta::remove_reference<bertrand::meta::yield_type<R>>
    >;

    template <bertrand::meta::range R>
        requires (
            bertrand::meta::tuple_like<bertrand::meta::yield_type<R>> &&
            bertrand::meta::tuple_size<bertrand::meta::yield_type<R>> == 2
        )
    map(R&& r) -> map<
        bertrand::meta::remove_reference<
            bertrand::meta::get_type<bertrand::meta::yield_type<R>, 0>
        >,
        bertrand::meta::remove_reference<
            bertrand::meta::get_type<bertrand::meta::yield_type<R>, 1>
        >
    >;

    template <bertrand::meta::range R>
        requires (
            bertrand::meta::tuple_like<bertrand::meta::yield_type<R>> &&
            bertrand::meta::tuple_size<bertrand::meta::yield_type<R>> == 2
        )
    multimap(R&& r) -> multimap<
        bertrand::meta::remove_reference<
            bertrand::meta::get_type<bertrand::meta::yield_type<R>, 0>
        >,
        bertrand::meta::remove_reference<
            bertrand::meta::get_type<bertrand::meta::yield_type<R>, 1>
        >
    >;

    template <bertrand::meta::range R>
        requires (
            bertrand::meta::tuple_like<bertrand::meta::yield_type<R>> &&
            bertrand::meta::tuple_size<bertrand::meta::yield_type<R>> == 2
        )
    unordered_map(R&& r) -> unordered_map<
        bertrand::meta::remove_reference<
            bertrand::meta::get_type<bertrand::meta::yield_type<R>, 0>
        >,
        bertrand::meta::remove_reference<
            bertrand::meta::get_type<bertrand::meta::yield_type<R>, 1>
        >
    >;

    template <bertrand::meta::range R>
        requires (
            bertrand::meta::tuple_like<bertrand::meta::yield_type<R>> &&
            bertrand::meta::tuple_size<bertrand::meta::yield_type<R>> == 2
        )
    unordered_multimap(R&& r) -> unordered_multimap<
        bertrand::meta::remove_reference<
            bertrand::meta::get_type<bertrand::meta::yield_type<R>, 0>
        >,
        bertrand::meta::remove_reference<
            bertrand::meta::get_type<bertrand::meta::yield_type<R>, 1>
        >
    >;

    template <bertrand::meta::range R>
    stack(R&& r) -> stack<bertrand::meta::remove_reference<bertrand::meta::yield_type<R>>>;

    template <bertrand::meta::range R>
    queue(R&& r) -> queue<bertrand::meta::remove_reference<bertrand::meta::yield_type<R>>>;

    template <bertrand::meta::range R>
    priority_queue(R&& r) -> priority_queue<
        bertrand::meta::remove_reference<bertrand::meta::yield_type<R>>
    >;

    /// TODO: a deduction guide for `std::mdspan` which gathers the correct extents
    /// based on the shape() of the range, where such a thing is possible, and where
    /// that information can be known at compile time.

_LIBCPP_END_NAMESPACE_STD


namespace bertrand::iter {


    static_assert(any{}(range(std::array{false, false, true})));


    static_assert(range(std::array{std::tuple{1, 2, 3}}).shape()[0] == 1);
    static_assert(std::same_as<meta::remove_range<const range<std::vector<int>&>>, const std::vector<int>&>);
    static_assert(meta::scalar<range<int>>);

    // static_assert([] {
    //     std::tuple arr {'a', 'b'};
    //     range{arr} = {'b', 'a'};
    //     // if (meta::get<0>(arr) != 3) return false;
    //     // if (meta::get<1>(arr) != 2) return false;
    //     // if (meta::get<2>(arr) != 1) return false;

    //     std::string arr2 = range(arr);
    //     if (arr2 != "ba") return false;

    //     return true;
    // }());




    /// TODO: conversion to nested containers seems not to work, at least not for
    /// vectors.  It does work for pairs, weirdly enough

    // static_assert([] {
    //     std::array arr {std::array{1, 2}, std::array{3, 4}};

    //     std::pair<std::pair<int, int>, std::pair<int, int>> p = range(range(arr));
    //     if (p.first.first != 1) return false;
    //     if (p.first.second != 2) return false;
    //     if (p.second.first != 3) return false;
    //     if (p.second.second != 4) return false;

    //     std::vector<std::vector<int>> vec = range(arr);
    //     return true;
    // }());



    // static_assert([] {
    //     std::array arr {
    //         std::array{1, 2, 3},
    //         std::array{4, 5, 6}
    //     };

    //     for (auto&& r : range(arr)) {
    //         if (*r.front() != 1 && *r.front() != 4) return false;
    //     }

    //     for (auto r : range(std::array{1, 2, 3})) {
    //         if (*r != 1 && *r != 2 && *r != 3) return false;
    //     }
    //     for (int r : range(std::array{1, 2, 3})) {
    //         if (r != 1 && r != 2 && r != 3) return false;
    //     }

    //     return true;
    // }());


    // static_assert(range(std::array{1, 2, 3}).contains(2));

    // static_assert(range(range(std::array{1, 2, 3}))->size() == 3);


    // static constexpr int x = 2;
    // static constexpr impl::ref r {x};


    // static_assert([] {
    //     impl::ref f {[](int i) { return i * 2; }};
    //     const impl::ref g = f;
    //     f = g;

    //     // impl::ref f {x};
    //     // const impl::ref g = f;
    //     // f = g;

    //     return true;
    // }());


    // static_assert([] {
    //     std::vector vec {1, 2, 3};
    //     std::tuple<int, int, int> tup = range(1, 4);
    //     auto [a, b, c] = tup;
    //     if (a != 1 || b != 2 || c != 3) {
    //         return false;
    //     }

    //     return true;
    // }());


    // static constexpr auto r10 = range<std::tuple<int, int, double>>(1, 2, 3);

    // static constexpr auto r11 = range(0, 5);

    // static_assert([] {
    //     for (auto&& x : r11) {
    //         (void)x;
    //     }

    //     return true;
    // }());


}


#endif  // BERTRAND_ITER_RANGE_H