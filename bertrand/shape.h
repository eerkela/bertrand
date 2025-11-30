#ifndef BERTRAND_SHAPE_H
#define BERTRAND_SHAPE_H

#include "bertrand/common.h"
#include "bertrand/except.h"


namespace bertrand {


namespace impl {

    /* An array of integers describing the length of each dimension in a regular,
    possibly multi-dimensional container.

    For most purposes, this class behaves just like a `std::array<const size_t, N>`,
    except that it is CTAD-convertible from scalar integers, braced initializer lists,
    or any tuple-like type that yields integers, as well as providing Python-style
    wraparound for negative indices and additional utility methods that can be used to
    implement a strided iteration protocol.

    Notably, the product of all dimensions must be equal to the total number of
    (flattened) elements in the tensor.  This generally precludes zero-length
    dimensions, which can cause information loss in the product.  As a result, setting
    a dimension to zero may have special meaning depending on context.  If the `shape`
    is provided as a template parameter to a supported container type, then setting a
    dimension to zero usually indicates that it is dynamic, allowing the zero to be
    replaced with its actual length at runtime.

    By default, zero-length dimensions will be represented normally, and it is up to
    the user to handle them appropriately.  Additionally, the number of dimensions may
    itself be `0`, which can indicate a zero-dimensional tensor (i.e. a scalar), once
    again depending on context. */
    template <size_t N>
    struct shape {
        using size_type = size_t;
        using index_type = ssize_t;
        using value_type = size_type;
        using reference = const size_type&;
        using pointer = const size_type*;
        using iterator = pointer;
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

        [[nodiscard]] constexpr shape() noexcept = default;

        template <typename... T> requires (sizeof...(T) > 0 && sizeof...(T) == size())
        [[nodiscard]] constexpr shape(T&&... n)
            noexcept ((meta::nothrow::convertible_to<T, size_type> && ...))
            requires ((meta::convertible_to<T, size_type> && ...))
        {
            size_type i = 0;
            (from_int(i, std::forward<T>(n)), ...);
        }

        template <typename T>
        [[nodiscard]] constexpr shape(T&& n)
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
            auto it = meta::begin(std::forward<T>(n));
            auto end = meta::end(std::forward<T>(n));
            while (i < size() && it != end) {
                dim[i] = *it;
                ++i;
                ++it;
            }
        }

        template <typename T>
        [[nodiscard]] constexpr shape(T&& n)
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

        constexpr void swap(shape& other) noexcept {
            for (size_type i = 0; i < size(); ++i) {
                meta::swap(dim[i], other.dim[i]);
            }
        }

        [[nodiscard]] constexpr pointer data() const noexcept { return static_cast<pointer>(dim); }
        [[nodiscard]] constexpr iterator begin() const noexcept { return data(); }
        [[nodiscard]] constexpr iterator end() const noexcept { return data() + size(); }
        [[nodiscard]] constexpr reverse_iterator rbegin() const noexcept {
            return std::make_reverse_iterator(end());
        }
        [[nodiscard]] constexpr reverse_iterator rend() const noexcept {
            return std::make_reverse_iterator(begin());
        }

        template <index_type I> requires (impl::valid_index<ssize(), I>)
        [[nodiscard]] constexpr value_type get() const noexcept {
            return dim[impl::normalize_index<ssize(), I>()];
        }

        [[nodiscard]] constexpr value_type operator[](index_type i) const {
            return dim[impl::normalize_index(ssize(), i)];
        }

        template <size_type R>
        [[nodiscard]] constexpr bool operator==(const shape<R>& other) const noexcept {
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
            const shape<R>& other
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

        [[nodiscard]] constexpr shape reverse() const noexcept {
            shape r;
            for (size_type j = 0; j < size(); ++j) {
                r.dim[j] = dim[size() - 1 - j];
            }
            return r;
        }

        template <size_type M>
        [[nodiscard]] constexpr auto reduce() const noexcept {
            if constexpr (M >= size()) {
                return shape<0>{};
            } else {
                shape<size() - M> s;
                for (size_type j = M; j < size(); ++j) {
                    s.dim[j - M] = dim[j];
                }
                return s;
            }
        }

        [[nodiscard]] constexpr shape strides(bool column_major) const noexcept {
            if constexpr (size() == 0) {
                return {};
            } else {
                shape s;
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
    shape(N...) -> shape<sizeof...(N)>;

    template <typename T>
        requires (
            !meta::convertible_to<T, size_t> &&
            meta::tuple_like<T> && (
                meta::yields<T, size_t> ||
                meta::tuple_types<T>::template convertible_to<size_t>
            )
        )
    shape(T&&) -> shape<meta::tuple_size<T>>;

    template <size_t N>
    [[nodiscard]] constexpr shape<N + 1> operator|(const shape<N>& lhs, size_t rhs) noexcept {
        shape<N + 1> s;
        for (size_t j = 0; j < N; ++j) {
            s.dim[j] = lhs.dim[j];
        }
        s.dim[N] = rhs;
        return s;
    }

    template <size_t N>
    [[nodiscard]] constexpr shape<N + 1> operator|(size_t lhs, const shape<N>& rhs) noexcept {
        shape<N + 1> s;
        s.dim[0] = lhs;
        for (size_t j = 1; j <= N; ++j) {
            s.dim[j] = rhs.dim[j - 1];
        }
        return s;
    }

}


namespace meta {

    /// TODO: try to eliminate `meta::static_shape()` if possible.  I won't truly know
    /// until after ranges are complete with respect to shapes and other
    /// multidimensional constructs.

    namespace detail {

        namespace adl {

            template <typename T>
            concept has_static_shape = requires{
                {shape<T>()};
                {impl::shape(shape<T>())};
            };

            template <typename T>
            concept has_shape = requires(T t) {
                {shape(::std::forward<T>(t))};
                {impl::shape(shape(::std::forward<T>(t)))};
            };

        }

        namespace member {

            template <typename T>
            concept has_static_shape = requires{
                {unqualify<T>::shape()};
                {impl::shape(unqualify<T>::shape())};
            };

            template <typename T>
            concept has_shape = requires(T t) {
                {::std::forward<T>(t).shape()};
                {impl::shape(::std::forward<T>(t).shape())};
            };

        }

        template <typename T>
        struct static_shape_fn {
            static constexpr auto operator()()
                noexcept (requires{{impl::shape(unqualify<T>::shape())} noexcept;})
                requires (member::has_static_shape<T>)
            {
                return impl::shape(unqualify<T>::shape());
            }

            static constexpr auto operator()()
                noexcept (requires{{impl::shape(shape<T>())} noexcept;})
                requires (!member::has_static_shape<T> && adl::has_static_shape<T>)
            {
                return impl::shape(shape<T>());
            }

            static constexpr auto operator()()
                noexcept (requires{{unqualify<T>::shape()} noexcept;})
                requires (
                    !member::has_static_shape<T> &&
                    !adl::has_static_shape<T> &&
                    member::has_shape<T>
                )
            {
                return meta::unqualify<decltype(impl::shape{::std::declval<T>().shape()})>{};
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
                return meta::unqualify<decltype(impl::shape{shape(::std::declval<T>())})>{};
            }

            static constexpr impl::shape<1> operator()() noexcept
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
                    decltype(impl::shape(::std::forward<T>(t).shape()))::size() ==
                    decltype(static_shape_fn<T>{}())::size()
                ))
            {
                return impl::shape(::std::forward<T>(t).shape());
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
                        decltype(impl::shape(shape(::std::forward<T>(t))))::size() ==
                        decltype(static_shape_fn<T>{}())::size()
                    )
                )
            {
                return impl::shape(shape(::std::forward<T>(t)));
            }

            template <typename T>
            static constexpr impl::shape<1> operator()(T&& t)
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
    produce a dynamic shape of the same rank as `t.shape()` or `shape(t)` filled with
    `None` if either of those are present as instance methods.  If neither are present,
    then it will attempt to deduce a 1D shape for tuple-like or iterable `T`, where the
    single element is equal to the tuple size or `None`, respectively.

    The result from this method can be used to specialize a corresponding class
    template.  Note that shapes containing `None` for one or more dimensions indicate
    dynamic shapes, where the actual size must be determined at runtime.
    
    The result is always returned as an `impl::shape<N>` object, where `N` is equal to
    the number of dimensions in the shape. */
    template <typename T>
    inline constexpr detail::static_shape_fn<T> static_shape;

    /* Retrieve the `shape()` of a generic object `t` of type `T`.

    This will invoke a member `t.shape()` method or an ADL `shape(t)` method if
    available.  Otherwise, it will deduce a 1D shape for tuple-like or iterable `t`,
    where the single element is equal to the tuple size or size of the iterable,
    respectively.  If an iterable does not supply a `size()` or `ssize()` method, then
    the shape may require a full traversal to determine its length.

    The result is always returned as an `impl::shape<N>` object, where `N` is
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

    /* Get the standardized `impl::shape` type that is returned by
    `meta::static_shape<T>()`, assuming that expression is well-formed. */
    template <has_static_shape T>
    using static_shape_type = decltype(static_shape<T>());

    /* Get the standardized `impl::shape` type that is returned by `meta::shape(t)`,
    assuming that expression is well-formed. */
    template <has_shape T>
    using shape_type = decltype(shape(::std::declval<T>()));

    /* Detect whether `meta::static_shape<T>()` is well-formed and then check for an
    implicit conversion from the normalized `impl::shape` type to the given return
    type.  See `meta::has_static_shape<T>` and `meta::static_shape_type<T>` for more
    details. */
    template <typename Ret, typename T>
    concept static_shape_returns =
        has_static_shape<T> && convertible_to<static_shape_type<T>, Ret>;

    /* Detect whether `meta::shape(t)` is well-formed and then check for an implicit
    conversion from the normalized `impl::shape` type to the given return type.  See
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

}


}


_LIBCPP_BEGIN_NAMESPACE_STD

    template <size_t N>
    struct tuple_size<bertrand::impl::shape<N>> : std::integral_constant<size_t, N> {};

    template <size_t I, size_t N>
    struct tuple_element<I, bertrand::impl::shape<N>> {
        using type = bertrand::impl::shape<N>::value_type;
    };

_LIBCPP_END_NAMESPACE_STD


#endif  // BERTRAND_SHAPE_H