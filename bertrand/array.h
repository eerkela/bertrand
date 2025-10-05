#ifndef BERTRAND_ARRAY_H
#define BERTRAND_ARRAY_H

#include "bertrand/common.h"
#include "bertrand/except.h"
#include "bertrand/union.h"


namespace bertrand {


namespace impl {
    struct array_tag {};
    struct array_view_tag {};

    template <size_t ndim>
    struct extent;

    /// TODO: operator| for concatenating dynamic extents?

    /* Run-time shape and stride specifiers can represent dynamic data, and may require
    a heap allocation if they are more than 1-dimensional.  They otherwise mirror
    their compile-time equivalents exactly. */
    template <>
    struct extent<0> {
    private:
        using heap = std::allocator<size_t>;

    public:
        size_t* dim = nullptr;
        size_t ndim = 0;

        [[nodiscard]] constexpr extent() noexcept = default;
        [[nodiscard]] constexpr extent(size_t n) noexcept : dim(nullptr), ndim(n) {}
        [[nodiscard]] constexpr extent(std::initializer_list<size_t> n) :
            dim(heap{}.allocate(n.size())),
            ndim(n.size())
        {
            if (dim == nullptr) {
                throw MemoryError();
            }
            size_t i = 0;
            auto it = n.begin();
            auto end = n.end();
            while (it != end) {
                std::construct_at(&dim[i], *it);
                ++i;
                ++it;
            }
        }
        template <typename T>
        [[nodiscard]] constexpr extent(T&& n)
            requires (
                !meta::convertible_to<T, size_t> &&
                !meta::convertible_to<T, std::initializer_list<size_t>> &&
                meta::iterable<T> &&
                meta::has_size<T> &&
                meta::convertible_to<meta::yield_type<T>, size_t>
            )
        :
            dim(heap{}.allocate(n.size())),
            ndim(n.size())
        {
            if (dim == nullptr) {
                throw MemoryError();
            }
            size_t i = 0;
            auto it = std::ranges::begin(n);
            auto end = std::ranges::end(n);
            while (i < ndim && it != end) {
                std::construct_at(&dim[i], *it);
                ++i;
                ++it;
            }
        }

        [[nodiscard]] constexpr extent(const extent& other) :
            dim(heap{}.allocate(other.ndim)),
            ndim(other.ndim)
        {
            if (dim == nullptr) {
                throw MemoryError();
            }
            for (size_t i = 0; i < ndim; ++i) {
                std::construct_at(&dim[i], other.dim[i]);
            }
        }

        [[nodiscard]] constexpr extent(extent&& other) noexcept :
            dim(other.dim),
            ndim(other.ndim)
        {
            other.dim = nullptr;
            other.ndim = 0;
        }

        constexpr extent& operator=(const extent& other) {
            if (this != &other) {
                if (ndim != other.ndim) {
                    if (dim) {
                        heap{}.deallocate(dim, ndim);
                    }
                    dim = heap{}.allocate(other.ndim);
                    if (dim == nullptr) {
                        throw MemoryError();
                    }
                    ndim = other.ndim;
                }
                for (size_t i = 0; i < ndim; ++i) {
                    std::construct_at(&dim[i], other.dim[i]);
                }
            }
            return *this;
        }

        constexpr extent& operator=(extent&& other) noexcept {
            if (this != &other) {
                if (dim) {
                    heap{}.deallocate(dim, ndim);
                }
                dim = other.dim;
                ndim = other.ndim;
                other.dim = nullptr;
                other.ndim = 0;
            }
            return *this;
        }

        constexpr ~extent() {
            if (dim) {
                heap{}.deallocate(dim, ndim);
            }
        }

        constexpr void swap(extent& other) noexcept {
            std::swap(dim, other.dim);
            std::swap(ndim, other.ndim);
        }

        [[nodiscard]] constexpr size_t size() const noexcept {
            return dim == nullptr ? ndim > 0 : ndim;
        }
        [[nodiscard]] constexpr ssize_t ssize() const noexcept { return ssize_t(size()); }
        [[nodiscard]] constexpr bool empty() const noexcept { return ndim == 0; }
        [[nodiscard]] constexpr size_t* data() noexcept { return dim; }
        [[nodiscard]] constexpr const size_t* data() const noexcept { return dim; }
    
        [[nodiscard]] constexpr size_t operator[](ssize_t i) const {
            ssize_t j = impl::normalize_index(ssize(), i);
            if (dim == nullptr) {
                return ndim;
            }
            return dim[j];
        }

        [[nodiscard]] constexpr extent reverse() const {
            extent r;
            r.ndim = ndim;
            if (dim) {
                r.dim = heap{}.allocate(ndim);
                if (r.dim == nullptr) {
                    throw MemoryError();
                }
                for (size_t j = 0; j < ndim; ++j) {
                    std::construct_at(&r.dim[j], dim[ndim - 1 - j]);
                }
            }
            return r;
        }

        [[nodiscard]] constexpr extent strip(size_t n, bool fortran) const {
            if (n == 0) {
                return *this;
            }
            extent s;
            if (dim && n < ndim) {
                if (ndim == n + 1) {
                    s.ndim = fortran ? dim[0] : dim[n];
                } else {
                    s.dim = heap{}.allocate(ndim - n);
                    if (s.dim == nullptr) {
                        throw MemoryError();
                    }
                    s.ndim = ndim - n;
                    if (fortran) {
                        for (size_t j = 0; j < s.ndim; ++j) {
                            std::construct_at(&s.dim[j], dim[j]);
                        }
                    } else {
                        for (size_t j = n; j < ndim; ++j) {
                            std::construct_at(&s.dim[j - n], dim[j]);
                        }
                    }
                }
            }
            return s;
        }

        [[nodiscard]] constexpr size_t product() const noexcept {
            if (dim == nullptr) {
                return ndim;
            }
            size_t p = 1;
            for (size_t j = 0; j < ndim; ++j) {
                p *= dim[j];
            }
            return p;
        }

        [[nodiscard]] constexpr size_t product(size_t n, bool fortran) const noexcept {
            size_t len = size();
            if (n >= len) {
                return 0;
            }
            size_t p = 1;
            if (dim == nullptr) {
                p = ndim;
            } else if (fortran) {
                for (size_t j = 0; j < len - n; ++j) {
                    p *= dim[j];
                }
            } else {
                for (size_t j = n; j < len; ++j) {
                    p *= dim[j];
                }
            }
            return p;
        }

        [[nodiscard]] constexpr extent strides(bool fortran) const {
            if (dim == nullptr) {
                return 1;
            }
            extent s;
            s.dim = heap{}.allocate(ndim);
            if (s.dim == nullptr) {
                throw MemoryError();
            }
            s.ndim = ndim;
            if (fortran) {
                std::construct_at(&s.dim[0], 1);
                for (size_t j = 1; j < ndim; ++j) {
                    std::construct_at(&s.dim[j], s.dim[j - 1] * dim[j - 1]);
                }
            } else {
                size_t j = size() - 1;
                std::construct_at(&s.dim[j], 1);
                while (j-- > 0) {
                    std::construct_at(&s.dim[j], s.dim[j + 1] * dim[j + 1]);
                }
            }
            return s;
        }

        [[nodiscard]] constexpr size_t* begin() noexcept { return dim; }
        [[nodiscard]] constexpr const size_t* begin() const noexcept {
            return static_cast<const size_t*>(dim);
        }
        [[nodiscard]] constexpr const size_t* cbegin() const noexcept { return begin(); }
        [[nodiscard]] constexpr size_t* end() noexcept { return dim + size(); }
        [[nodiscard]] constexpr const size_t* end() const noexcept {
            return static_cast<const size_t*>(dim) + size();
        }
        [[nodiscard]] constexpr const size_t* cend() const noexcept { return end(); }
        [[nodiscard]] constexpr auto rbegin() noexcept {
            return std::make_reverse_iterator(end());
        }
        [[nodiscard]] constexpr auto rbegin() const noexcept {
            return std::make_reverse_iterator(end());
        }
        [[nodiscard]] constexpr auto crbegin() const noexcept { return rbegin(); }
        [[nodiscard]] constexpr auto rend() noexcept {
            return std::make_reverse_iterator(begin());
        }
        [[nodiscard]] constexpr auto rend() const noexcept {
            return std::make_reverse_iterator(begin());
        }
        [[nodiscard]] constexpr auto crend() const noexcept { return rend(); }

        template <size_t R>
        [[nodiscard]] constexpr bool operator==(const extent<R>& other) const noexcept {
            if constexpr (R == 0) {
                if (ndim != other.ndim) {
                    return false;
                }
                if (dim) {
                    for (size_t i = 0; i < ndim; ++i) {
                        if (dim[i] != other.dim[i]) {
                            return false;
                        }
                    }
                }
                return true;
            } else {
                return false;
            }
        }

    };

    /* Compile-time shape and stride specifiers use CTAD to allow simple braced
    initializer syntax, which permits custom strides. */
    template <size_t ndim>
    struct extent {
        size_t dim[ndim];

        [[nodiscard]] constexpr extent() noexcept = default;
        [[nodiscard]] constexpr extent(size_t n) noexcept requires (ndim == 1) : dim{n} {}
        [[nodiscard]] constexpr extent(std::initializer_list<size_t> n) noexcept {
            size_t i = 0;
            auto it = n.begin();
            auto end = n.end();
            while (i < ndim && it != end) {
                std::construct_at(&dim[i], *it);
                ++i;
                ++it;
            }
        }
        template <meta::yields<size_t> T>
        [[nodiscard]] constexpr extent(T&& n)
            noexcept (meta::nothrow::yields<T, size_t>)
            requires (
                !meta::convertible_to<T, size_t> &&
                !meta::convertible_to<T, std::initializer_list<size_t>> &&
                meta::tuple_like<T> &&
                meta::tuple_size<T> == ndim
            )
        {
            size_t i = 0;
            auto it = std::ranges::begin(n);
            auto end = std::ranges::end(n);
            while (i < ndim && it != end) {
                std::construct_at(&dim[i], *it);
                ++i;
                ++it;
            }
        }

        constexpr void swap(extent& other) noexcept {
            for (size_t i = 0; i < ndim; ++i) {
                std::swap(dim[i], other.dim[i]);
            }
        }

        [[nodiscard]] static constexpr size_t size() noexcept { return ndim; }
        [[nodiscard]] static constexpr ssize_t ssize() noexcept { return ssize_t(ndim); }
        [[nodiscard]] static constexpr bool empty() noexcept { return false; }
        [[nodiscard]] constexpr size_t* data() noexcept { return dim; }
        [[nodiscard]] constexpr const size_t* data() const noexcept { return dim; }

        template <ssize_t I> requires (impl::valid_index<ssize(), I>)
        [[nodiscard]] constexpr size_t get() const noexcept {
            return dim[impl::normalize_index<ssize(), I>()];
        }

        [[nodiscard]] constexpr size_t operator[](ssize_t i) const {
            return dim[impl::normalize_index(ssize(), i)];
        }

        [[nodiscard]] constexpr extent reverse() const noexcept {
            extent r;
            for (size_t j = 0; j < size(); ++j) {
                std::construct_at(&r.dim[j], dim[size() - 1 - j]);
            }
            return r;
        }

        template <size_t N>
        [[nodiscard]] constexpr auto strip(bool fortran) const noexcept {
            if constexpr (N >= size()) {
                return extent<0>{};
            } else {
                extent<size() - N> s;
                if (fortran) {
                    for (size_t j = 0; j < size() - N; ++j) {
                        std::construct_at(&s.dim[j], dim[j]);
                    }
                } else {
                    for (size_t j = N; j < size(); ++j) {
                        std::construct_at(&s.dim[j - N], dim[j]);
                    }
                }
                return s;
            }
        }

        [[nodiscard]] constexpr size_t product() const noexcept {
            size_t p = 1;
            for (size_t j = 0; j < size(); ++j) {
                p *= dim[j];
            }
            return p;
        }

        [[nodiscard]] constexpr size_t product(size_t n, bool fortran) const noexcept {
            size_t len = size();
            if (n >= len) {
                return 0;
            }
            size_t p = 1;
            if (fortran) {
                for (size_t j = 0; j < len - n; ++j) {
                    p *= dim[j];
                }
            } else {
                for (size_t j = n; j < len; ++j) {
                    p *= dim[j];
                }
            }
            return p;
        }

        [[nodiscard]] constexpr extent strides(bool fortran) const noexcept {
            extent s;
            if (fortran) {
                std::construct_at(&s.dim[0], 1);
                for (size_t j = 1; j < size(); ++j) {
                    std::construct_at(&s.dim[j], s.dim[j - 1] * dim[j - 1]);
                }
            } else {
                size_t j = size() - 1;
                std::construct_at(&s.dim[j], 1);
                while (j-- > 0) {
                    std::construct_at(&s.dim[j], s.dim[j + 1] * dim[j + 1]);
                }
            }
            return s;
        }

        [[nodiscard]] constexpr size_t* begin() noexcept { return dim; }
        [[nodiscard]] constexpr const size_t* begin() const noexcept {
            return static_cast<const size_t*>(dim);
        }
        [[nodiscard]] constexpr const size_t* cbegin() const noexcept { return begin(); }
        [[nodiscard]] constexpr size_t* end() noexcept { return dim + size(); }
        [[nodiscard]] constexpr const size_t* end() const noexcept {
            return static_cast<const size_t*>(dim) + size();
        }
        [[nodiscard]] constexpr const size_t* cend() const noexcept { return end(); }
        [[nodiscard]] constexpr auto rbegin() noexcept {
            return std::make_reverse_iterator(end());
        }
        [[nodiscard]] constexpr auto rbegin() const noexcept {
            return std::make_reverse_iterator(end());
        }
        [[nodiscard]] constexpr auto crbegin() const noexcept { return rbegin(); }
        [[nodiscard]] constexpr auto rend() noexcept {
            return std::make_reverse_iterator(begin());
        }
        [[nodiscard]] constexpr auto rend() const noexcept {
            return std::make_reverse_iterator(begin());
        }
        [[nodiscard]] constexpr auto crend() const noexcept { return rend(); }

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

    template <meta::yields<size_t> T> requires (!meta::integer<T> && meta::tuple_like<T>)
    extent(T&&) -> extent<meta::tuple_size<T>>;

    template <size_t N, size_t M>
    [[nodiscard]] constexpr extent<N + M> operator|(
        const extent<N>& lhs,
        const extent<M>& rhs
    ) noexcept {
        extent<N + M> s;
        for (size_t j = 0; j < N; ++j) {
            std::construct_at(&s.dim[j], lhs.dim[j]);
        }
        for (size_t j = 0; j < M; ++j) {
            std::construct_at(&s.dim[N + j], rhs.dim[j]);
        }
        return s;
    }

    template <size_t N>
    [[nodiscard]] constexpr extent<N + 1> operator|(const extent<N>& self, size_t other) noexcept {
        extent<N + 1> s;
        for (size_t j = 0; j < N; ++j) {
            std::construct_at(&s.dim[j], self.dim[j]);
        }
        std::construct_at(&s.dim[N], other);
        return s;
    }

    template <size_t N>
    [[nodiscard]] constexpr extent<N + 1> operator|(
        size_t other,
        const extent<N>& self
    ) noexcept {
        extent<N + 1> s;
        std::construct_at(&s.dim[0], other);
        for (size_t j = 1; j <= N; ++j) {
            std::construct_at(&s.dim[j], self.dim[j - 1]);
        }
        return s;
    }

    /* A configuration struct that holds miscellaneous flags for an array or array
    view type. */
    template <extent shape>
    struct array_flags {
        /* If false (the default), then the strides will be computed in C (row-major)
        order, with the last index varying the fastest.  Setting this to true specifies
        that indices should be interpreted in Fortran (column-major) order instead,
        where the first index varies the fastest.  Toggling this flag for a view
        effectively transposes the array. */
        bool fortran = false;

        /* A set of custom strides to use for each dimension.  If given, the strides
        will always match their corresponding shape dimension. */
        extent<shape.size()> strides = shape.strides(fortran);

        [[nodiscard]] constexpr bool operator==(const array_flags& other) const noexcept {
            return fortran == other.fortran && strides == other.strides;
        }
    };

    /* Due to a compiler bug, `array_flags<{}>` with an empty shape will not compile
    as a non-type template parameter unless an instance of that type has been
    previously constructed.  This value is not actually used anywhere. */
    static constexpr array_flags<{}> _dynamic_array_flags;

    template <typename T, extent shape, array_flags<shape> flags>
    concept array_concept =
        meta::not_void<T> &&
        meta::not_reference<T> &&
        !shape.empty() &&
        (shape.product() > 0 || shape.size() == 1);

    template <typename T, extent shape, array_flags<shape> flags>
    concept array_view_concept =
        meta::not_void<T> &&
        meta::not_reference<T> &&
        (shape.product() > 0 || shape.size() <= 1);


    static constexpr extent e {1, 2, 3};
    static constexpr auto e2 = e | 4 | 5;

}


template <typename T, impl::extent Shape, impl::array_flags<Shape> Flags = {}>
    requires (impl::array_concept<T, Shape, Flags>)
struct Array;


template <typename T, impl::extent Shape = {}, impl::array_flags<Shape> Flags = {}>
    requires (impl::array_view_concept<T, Shape, Flags>)
struct ArrayView;


namespace meta {

    template <
        typename C,
        typename T = impl::array_tag,
        impl::extent shape = {},
        impl::array_flags<shape> flags = {}
    >
    concept Array =
        inherits<C, impl::array_tag> &&
        (
            ::std::same_as<T, impl::array_tag> ||
            ::std::same_as<typename unqualify<C>::type, T>
        ) &&
        (shape.empty() || shape == unqualify<C>::shape()) &&
        (shape.empty() || flags == unqualify<C>::flags());

    template <
        typename C,
        typename T = impl::array_view_tag,
        impl::extent shape = {},
        impl::array_flags<shape> flags = {}
    >
    concept ArrayView =
        inherits<C, impl::array_view_tag> &&
        (
            ::std::same_as<T, impl::array_view_tag> ||
            ::std::same_as<typename unqualify<C>::type, T>
        ) &&
        (shape.empty() || shape == unqualify<C>::shape()) &&
        (shape.empty() || flags == unqualify<C>::flags());

}


namespace impl {

    /* Reduce the rank of an array by stripping off `N` dimensions according to the
    flags' ordering. */
    template <extent Shape, array_flags<Shape> Flags, size_t N = 1> requires (N > 0)
    struct array_reduce {
        static constexpr auto shape() noexcept {
            return Shape.template strip<N>(Flags.fortran);
        }

        static constexpr array_flags<shape()> flags() noexcept {
            return {
                .fortran = Flags.fortran,
                .strides = Flags.strides.template strip<N>(Flags.fortran)
            };
        };

        template <typename T>
        using array = Array<T, shape(), flags()>;

        template <typename T>
        using view = ArrayView<T, shape(), flags()>;
    };

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

    template <typename T>
    struct _array_type { using type = T; };
    template <is_array_storage T>
    struct _array_type<T> { using type = meta::unqualify<T>::type; };
    template <typename T>
    using array_type = _array_type<T>::type;

    /* Multidimensional arrays can take structured data in their constructors, which
    consists of a series of nested arrays of 1 fewer dimension representing the major
    axis of the parent array.  The nested arrays will then be flattened into the outer
    buffer, completing the constructor. */
    template <typename T, extent shape, array_flags<shape> flags>
    struct _array_init { using type = T; };
    template <typename T, extent shape, array_flags<shape> flags> requires (!shape.empty())
    struct _array_init<T, shape, flags> { using type = Array<T, shape, flags>; };
    template <typename T, extent shape, array_flags<shape> flags> requires (!shape.empty())
    using array_init = _array_init<
        T,
        array_reduce<shape, flags>::shape(),
        array_reduce<shape, flags>::flags()
    >::type;

    /* The outermost array value type also needs to be promoted to an array view if
    it is multidimensional, and needs to correct for the `array_storage` normalization
    as well. */
    template <typename T, extent shape, array_flags<shape> flags>
    struct _array_value { using type = array_type<T>; };
    template <typename T, extent shape, array_flags<shape> flags> requires (!shape.empty())
    struct _array_value<T, shape, flags> { using type = ArrayView<T, shape, flags>; };
    template <typename T, extent shape, array_flags<shape> flags> requires (!shape.empty())
    using array_value = _array_value<
        T,
        array_reduce<shape, flags>::shape(),
        array_reduce<shape, flags>::flags()
    >::type;

    /* Array iterators are implemented as raw pointers into the array buffer.  Due to
    aggressive UB sanitization during constant evaluation, an extra count is required
    to avoid overstepping the end of the array.  This index is ignored at run time,
    ensuring zero-cost iteration, without interfering with compile-time uses. */
    template <meta::not_reference T, extent Shape, array_flags<Shape> Flags>
    struct array_iterator {
        using iterator_category = std::conditional_t<
            Flags.strides.dim[0] == 1,
            std::contiguous_iterator_tag,
            std::random_access_iterator_tag
        >;
        using difference_type = std::ptrdiff_t;
        using value_type = array_value<T, Shape, Flags>;
        using reference = meta::as_lvalue<value_type>;
        using pointer = meta::as_pointer<value_type>;

    private:
        using view = value_type;

    public:
        T* ptr = nullptr;
        difference_type count = 0;
        static constexpr const size_t* shape = Shape.dim;
        static constexpr const size_t* strides = Flags.strides.dim;
        static constexpr const size_t ndim = Shape.size();

        [[nodiscard]] constexpr decltype(auto) operator*() const noexcept {
            if constexpr (ndim == 1) {
                return array_access(ptr);
            } else {
                return view{ptr};
            }
        }

        [[nodiscard]] constexpr auto operator->() const noexcept {
            if constexpr (ndim == 1) {
                return std::addressof(**this);
            } else {
                return impl::arrow{**this};
            }
        }

        [[nodiscard]] constexpr decltype(auto) operator[](difference_type n) const noexcept {
            if constexpr (ndim == 1) {
                return array_access(ptr, n);
            } else {
                return view{ptr + n * strides[0]};
            }
        }

        constexpr array_iterator& operator++() noexcept {
            if consteval {
                ++count;
                if (count > 0 && count < shape[0]) {
                    ptr += strides[0];
                }
            } else {
                ptr += strides[0];
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
                    return {self.ptr - self.count * (self.count > 0) * strides[0], new_count};
                } else if (new_count >= shape[0]) {
                    return {
                        self.ptr + (shape[0] - self.count - 1) * (self.count < shape[0]) * strides[0],
                        new_count
                    };
                } else {
                    return {self.ptr + n * strides[0], new_count};
                }
            } else {
                return {self.ptr + n * strides[0]};
            }
        }

        [[nodiscard]] friend constexpr array_iterator operator+(
            difference_type n,
            const array_iterator& self
        ) noexcept {
            if consteval {
                difference_type new_count = self.count + n;
                if (new_count < 0) {
                    return {self.ptr - self.count * (self.count > 0) * strides[0], new_count};
                } else if (new_count >= shape[0]) {
                    return {
                        self.ptr + (shape[0] - self.count - 1) * (self.count < shape[0]) * strides[0],
                        new_count
                    };
                } else {
                    return {self.ptr + n * strides[0], new_count};
                }
            } else {
                return {self.ptr + n * strides[0]};
            }
        }

        constexpr array_iterator& operator+=(difference_type n) noexcept {
            if consteval {
                difference_type new_count = count + n;
                if (new_count < 0) {
                    ptr -= count * (count > 0) * strides[0];
                } else if (new_count >= shape[0]) {
                    ptr += (shape[0] - count - 1) * (count < shape[0]) * strides[0];
                } else {
                    ptr += n * strides[0];
                }
                count = new_count;
            } else {
                ptr += n * strides[0];
            }
            return *this;
        }

        constexpr array_iterator& operator--() noexcept {
            if consteval {
                if (count > 0 && count < shape[0]) {
                    ptr -= strides[0];
                }
                --count;
            } else {
                ptr -= strides[0];
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
                    return {ptr - count * (count > 0) * strides[0], new_count};
                } else if (new_count >= shape[0]) {
                    return {
                        ptr + (shape[0] - count - 1) * (count < shape[0]) * strides[0],
                        new_count
                    };
                } else {
                    return {ptr - n * strides[0], new_count};
                }
            } else {
                return {ptr - n * strides[0]};
            }
        }

        [[nodiscard]] constexpr difference_type operator-(
            const array_iterator& other
        ) const noexcept {
            if consteval {
                return count - other.count;
            } else {
                return (ptr - other.ptr) / strides[0];
            }
        }

        constexpr array_iterator& operator-=(difference_type n) noexcept {
            if consteval {
                difference_type new_count = count - n;
                if (new_count < 0) {
                    ptr -= count * (count > 0) * strides[0];
                } else if (new_count >= shape[0]) {
                    ptr += (shape[0] - count - 1) * (count < shape[0]) * strides[0];
                } else {
                    ptr -= n * strides[0];
                }
                count = new_count;
            } else {
                ptr -= n * strides[0];
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


    /// TODO: array iterators for empty shapes would have to yield views of shape {},
    /// which at the bottom of the recursion will be a view of rank 1 and size 1.
    /// Maybe these yielded views could reuse the same shape and strides buffers, and
    /// just advance the pointer and/or reduce ndim accordingly.  That would be
    /// relatively efficient, and avoid an allocation when traversing multidimensional
    /// arrays, which may yield views with ndim > 1.

    /// TODO: revisit dynamic array iterators after refactoring the static ArrayView and
    /// Array types.

    template <meta::not_reference T, extent Shape, array_flags<Shape> Flags>
        requires (Shape.empty())
    struct array_iterator<T, Shape, Flags> {
        using iterator_category = std::random_access_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = ArrayView<T, Shape, Flags>;
        using reference = meta::as_lvalue<value_type>;
        using pointer = meta::as_pointer<value_type>;

    private:
        using view = value_type;

    public:
        T* ptr = nullptr;
        difference_type count = 0;
        size_t* shape = nullptr;
        size_t* strides = nullptr;
        size_t ndim = 0;

        [[nodiscard]] constexpr value_type operator*() const noexcept {
            return {ptr};
        }


    };

    /// TODO: index errors are defined after filling in int <-> str conversions.

    inline constexpr IndexError array_index_error(size_t i) noexcept;

    /* Indices are always computed in C (row-major) memory order for simplicity and
    compatibility with C++.  If an index is signed, then Python-style wraparound will
    be applied to handle negative values.  A bounds check is then applied as a debug
    assertion, which throws an `IndexError` if the index is out of bounds. */
    template <extent shape, array_flags<shape> flags, meta::unsigned_integer I, typename... Is>
    constexpr size_t array_index(const I& i, const Is&... is)
        noexcept (!DEBUG && meta::nothrow::explicitly_convertible_to<I, size_t>)
        requires (
            !shape.empty() &&
            !flags.strides.empty() &&
            meta::explicitly_convertible_to<I, size_t>
        )
    {
        if constexpr (DEBUG) {
            if (i >= shape.dim[0]) {
                throw array_index_error(i);
            }
        }
        if constexpr (sizeof...(Is) == 0) {
            return size_t(i) * flags.strides.dim[0];
        } else {
            return size_t(i) * flags.strides.dim[0] + array_index<
                array_reduce<shape, flags>::shape(),
                array_reduce<shape, flags>::flags()
            >(is...);
        }
    }
    template <extent shape, array_flags<shape> flags, meta::signed_integer I, typename... Is>
    constexpr size_t array_index(const I& i, const Is&... is)
        noexcept (!DEBUG && meta::nothrow::explicitly_convertible_to<I, ssize_t>)
        requires (
            !shape.empty() &&
            !flags.strides.empty() &&
            meta::explicitly_convertible_to<I, ssize_t>
        )
    {
        ssize_t j = ssize_t(i) + ssize_t(shape.dim[0]) * (i < 0);
        if constexpr (DEBUG) {
            if (j < 0 || j > ssize_t(shape.dim[0])) {
                throw array_index_error(i);
            }
        }
        if constexpr (sizeof...(Is) == 0) {
            return size_t(j) * flags.strides.dim[0];
        } else {
            return size_t(j) * flags.strides.dim[0] + array_index<
                array_reduce<shape, flags>::shape(),
                array_reduce<shape, flags>::flags()
            >(is...);
        }
    }

    /* Tuple access for array types always takes signed integers and applies both
    wraparound and bounds-checking at compile time. */
    template <ssize_t... I>
    struct valid_array_index {
        template <extent shape, array_flags<shape> flags>
        static constexpr bool value = true;
        template <extent shape, array_flags<shape> flags>
        static constexpr size_t index = 0;
    };
    template <ssize_t I, ssize_t... Is>
    struct valid_array_index<I, Is...> {
        template <extent shape, array_flags<shape> flags>
        static constexpr bool value = false;
        template <extent shape, array_flags<shape> flags>
            requires (!shape.empty() && impl::valid_index<shape.dim[0], I>)
        static constexpr bool value<shape, flags> = valid_array_index<Is...>::template value<
            array_reduce<shape, flags>::shape(),
            array_reduce<shape, flags>::flags()
        >;

        template <extent shape, array_flags<shape> flags>
        static constexpr size_t index =
            array_index<shape, flags>(I) +
            valid_array_index<Is...>::template index<
                array_reduce<shape, flags>::shape(),
                array_reduce<shape, flags>::flags()
            >;
    };

    /* Squeezing an array removes all singleton dimensions unless that is the only
    remaining dimension. */
    template <typename T, extent shape, array_flags<shape> flags>
    struct _array_squeeze {
        template <extent S, array_flags<S> F>
        using array = Array<T, S, F>;
        template <extent S, array_flags<S> F>
        using view = ArrayView<T, S, F>;
    };
    template <typename T, extent shape, array_flags<shape> flags>
        requires (!shape.empty() && shape.dim[0] > 1)
    struct _array_squeeze<T, shape, flags> {
        template <extent S, array_flags<S> F>
        using array = _array_squeeze<
            T,
            array_reduce<shape, flags>::shape(),
            array_reduce<shape, flags>::flags()
        >::template array<S | shape.dim[0], {
            .fortran = flags.fortran,
            .strides = F.strides | flags.strides.dim[0]
        }>;

        template <extent S, array_flags<S> F>
        using view = _array_squeeze<
            T,
            array_reduce<shape, flags>::shape(),
            array_reduce<shape, flags>::flags()
        >::template view<S | shape.dim[0], {
            .fortran = flags.fortran,
            .strides = F.strides | flags.strides.dim[0]
        }>;
    };
    template <typename T, extent shape, array_flags<shape> flags>
        requires (!shape.empty() && shape.dim[0] == 1)
    struct _array_squeeze<T, shape, flags> {
        template <extent S, array_flags<S> F>
        struct type {
            using array = _array_squeeze<
                T,
                array_reduce<shape, flags>::shape(),
                array_reduce<shape, flags>::flags()
            >::template array<S, F>;
            using view = _array_squeeze<
                T,
                array_reduce<shape, flags>::shape(),
                array_reduce<shape, flags>::flags()
            >::template view<S, F>;
        };
        template <extent S, array_flags<S> F> requires (shape.size() == 1 && S.empty())
        struct type<S, F> {
            using array = Array<T, 1, {.fortran = flags.fortran, .strides = 1}>;
            using view = ArrayView<T, 1, {.fortran = flags.fortran, .strides = 1}>;
        };
        template <extent S, array_flags<S> F>
        using array = type<S, F>::array;
        template <extent S, array_flags<S> F>
        using view = type<S, F>::view;
    };
    template <typename T, extent shape, array_flags<shape> flags>
    using array_squeeze = _array_squeeze<T, shape, flags>;

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
template <typename T, impl::extent Shape, impl::array_flags<Shape> Flags>
    requires (impl::array_view_concept<T, Shape, Flags>)
struct ArrayView : impl::array_view_tag {
private:
    static constexpr size_t step = Flags.strides.dim[0];

public:
    using size_type = size_t;
    using index_type = ssize_t;
    using value_type = impl::array_value<T, Shape, Flags>;
    using reference = meta::as_lvalue<value_type>;
    using const_reference = meta::as_const<reference>;
    using pointer = meta::as_pointer<reference>;
    using const_pointer = meta::as_pointer<const_reference>;
    using iterator = impl::array_iterator<T, Shape, Flags>;
    using const_iterator = impl::array_iterator<meta::as_const<T>, Shape, Flags>;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    [[nodiscard]] static constexpr bool dynamic() noexcept { return false; }
    [[nodiscard]] static constexpr size_t itemsize() noexcept { return sizeof(T); }
    [[nodiscard]] static constexpr size_type ndim() noexcept { return Shape.size(); }
    [[nodiscard]] static constexpr const auto& shape() noexcept { return Shape; }
    [[nodiscard]] static constexpr const auto& strides() noexcept { return Flags.strides; }
    [[nodiscard]] static constexpr size_type total() noexcept { return Shape.product(); }
    [[nodiscard]] static constexpr size_type size() noexcept { return Shape.dim[0]; }
    [[nodiscard]] static constexpr index_type ssize() noexcept { return index_type(size()); }
    [[nodiscard]] static constexpr bool empty() noexcept { return Shape.dim[0] == 0; }

    T* ptr = nullptr;

    [[nodiscard]] constexpr ArrayView() noexcept = default;
    [[nodiscard]] constexpr ArrayView(T* p) noexcept : ptr(p) {}

    template <typename A> requires (meta::data_returns<T*, A>)
    [[nodiscard]] constexpr ArrayView(A& arr) noexcept:
        ptr(std::ranges::data(arr))
    {}

    constexpr void swap(ArrayView& other) noexcept { std::swap(ptr, other.ptr); }

    template <meta::integer... I> requires (sizeof...(I) <= ndim())
    [[nodiscard]] constexpr auto data(const I&... i) noexcept {
        if constexpr (sizeof...(I) == 0) {
            if constexpr (impl::is_array_storage<T>) {
                return std::addressof(ptr->value);
            } else {
                return ptr;
            }
        } else {
            size_type j = impl::array_index<Shape, Flags>(i...);
            if constexpr (impl::is_array_storage<T>) {
                return std::addressof(ptr[j].value);
            } else {
                return ptr + j;
            }
        }
    }

    template <meta::integer... I> requires (sizeof...(I) <= ndim())
    [[nodiscard]] constexpr auto data(const I&... i) const noexcept {
        if constexpr (sizeof...(I) == 0) {
            if constexpr (impl::is_array_storage<T>) {
                return std::addressof(static_cast<meta::as_const<T>*>(ptr)->value);
            } else {
                return static_cast<meta::as_const<T>*>(ptr);
            }
        } else {
            size_type j = impl::array_index<Shape, Flags>(i...);
            if constexpr (impl::is_array_storage<T>) {
                return std::addressof(static_cast<meta::as_const<T>*>(ptr)[j].value);
            } else {
                return static_cast<meta::as_const<T>*>(ptr) + j;
            }
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
            return {ptr + (size() - 1) * step, size()};
        } else {
            return {ptr + size() * step};
        }
    }

    [[nodiscard]] constexpr const_iterator end() const noexcept {
        if consteval {
            return {ptr + (size() - 1) * step, size()};
        } else {
            return {ptr + size() * step};
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

    [[nodiscard]] constexpr auto flatten() noexcept {
        using view = ArrayView<T, total()>;
        return view{ptr};
    }

    [[nodiscard]] constexpr auto flatten() const noexcept {
        using view = ArrayView<meta::as_const<T>, total()>;
        return view{ptr};
    }

    template <impl::extent S, impl::array_flags<S> F = {}> requires (S.product() == total())
    [[nodiscard]] constexpr auto reshape() noexcept {
        using view = ArrayView<T, S, F>;
        return view{ptr};
    }

    template <impl::extent S, impl::array_flags<S> F = {}> requires (S.product() == total())
    [[nodiscard]] constexpr auto reshape() const noexcept {
        using view = ArrayView<meta::as_const<T>, S, F>;
        return view{ptr};
    }

    [[nodiscard]] constexpr auto squeeze() noexcept {
        using view = impl::array_squeeze<T, Shape, Flags>::template view<
            {},
            {.fortran = Flags.fortran}
        >;
        return view{ptr};
    }

    [[nodiscard]] constexpr auto squeeze() const noexcept {
        using view = impl::array_squeeze<meta::as_const<T>, Shape, Flags>::template view<
            {},
            {.fortran = Flags.fortran}
        >;
        return view{static_cast<meta::as_const<T>*>(ptr)};
    }

    /// TODO: transpose()

    [[nodiscard]] constexpr decltype(auto) front() noexcept {
        if constexpr (Shape.size() == 1) {
            return (impl::array_access(ptr));
        } else {
            using view = impl::array_reduce<Shape, Flags>::template view<T>;
            return view{ptr};
        }
    }

    [[nodiscard]] constexpr decltype(auto) front() const noexcept {
        if constexpr (Shape.size() == 1) {
            return (impl::array_access(static_cast<meta::as_const<T>*>(ptr)));
        } else {
            using view = impl::array_reduce<Shape, Flags>::template view<meta::as_const<T>>;
            return view{static_cast<meta::as_const<T>*>(ptr)};
        }
    }

    [[nodiscard]] constexpr decltype(auto) back() noexcept {
        if constexpr (Shape.size() == 1) {
            return (impl::array_access(ptr, (size() - 1) * step));
        } else {
            using view = value_type;
            return view{ptr + (size() - 1) * step};
        }
    }

    [[nodiscard]] constexpr decltype(auto) back() const noexcept {
        if constexpr (Shape.size() == 1) {
            return (impl::array_access(
                static_cast<meta::as_const<T>*>(ptr),
                (size() - 1) * step
            ));
        } else {
            using view = impl::array_reduce<Shape, Flags>::template view<meta::as_const<T>>;
            return view{static_cast<meta::as_const<T>*>(ptr) + (size() - 1) * step};
        }
    }

    template <index_type... I>
        requires (
            sizeof...(I) <= ndim() &&
            impl::valid_array_index<I...>::template value<Shape, Flags>
        )
    [[nodiscard]] constexpr decltype(auto) get() noexcept {
        constexpr size_type j = impl::valid_array_index<I...>::template index<Shape, Flags>;
        if constexpr (sizeof...(I) == ndim()) {
            return (impl::array_access(ptr, j));
        } else {
            using view = impl::array_reduce<Shape, Flags, sizeof...(I)>::template view<T>;
            return view{ptr + j};
        }
    }

    template <index_type... I>
        requires (
            sizeof...(I) <= ndim() &&
            impl::valid_array_index<I...>::template value<Shape, Flags>
        )
    [[nodiscard]] constexpr decltype(auto) get() const noexcept {
        constexpr size_type j = impl::valid_array_index<I...>::template index<Shape, Flags>;
        if constexpr (sizeof...(I) == ndim()) {
            return (impl::array_access(static_cast<meta::as_const<T>*>(ptr), j));
        } else {
            using view = impl::array_reduce<Shape, Flags, sizeof...(I)>::template view<
                meta::as_const<T>
            >;
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
        size_type j = impl::array_index<Shape, Flags>(i...);
        if constexpr (sizeof...(I) == ndim()) {
            return (impl::array_access(ptr, j));
        } else {
            using view = impl::array_reduce<Shape, Flags, sizeof...(I)>::template view<T>;
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
        size_type j = impl::array_index<Shape, Flags>(i...);
        if constexpr (sizeof...(I) == ndim()) {
            return (impl::array_access(static_cast<meta::as_const<T>*>(ptr), j));
        } else {
            using view = impl::array_reduce<Shape, Flags, sizeof...(I)>::template view<
                meta::as_const<T>
            >;
            return view{static_cast<meta::as_const<T>*>(ptr) + j};
        }
    }

    template <typename U>
    [[nodiscard]] constexpr operator Array<U, Shape, Flags>() const
        noexcept (meta::nothrow::convertible_to<const_reference, U>)
        requires (meta::convertible_to<const_reference, U>)
    {
        auto result = Array<U, Shape, Flags>::reserve();
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
    [[nodiscard]] constexpr bool operator==(const Array<U, Shape, Flags>& other) const
        noexcept (
            meta::nothrow::has_eq<
                const_reference,
                typename Array<U, Shape, Flags>::const_reference
            > &&
            meta::nothrow::truthy<meta::eq_type<
                const_reference,
                typename Array<U, Shape, Flags>::const_reference
            >>
        )
        requires (
            meta::has_eq<
                const_reference,
                typename Array<U, Shape, Flags>::const_reference
            > &&
            meta::truthy<meta::eq_type<
                const_reference,
                typename Array<U, Shape, Flags>::const_reference
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
    [[nodiscard]] constexpr bool operator==(const ArrayView<U, Shape, Flags>& other) const
        noexcept (
            meta::nothrow::has_eq<
                const_reference,
                typename ArrayView<U, Shape, Flags>::const_reference
            > &&
            meta::nothrow::truthy<meta::eq_type<
                const_reference,
                typename ArrayView<U, Shape, Flags>::const_reference
            >>
        )
        requires (
            meta::has_eq<
                const_reference,
                typename ArrayView<U, Shape, Flags>::const_reference
            > &&
            meta::truthy<meta::eq_type<
                const_reference,
                typename ArrayView<U, Shape, Flags>::const_reference
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
    [[nodiscard]] constexpr auto operator<=>(const Array<U, Shape, Flags>& other) const
        noexcept (
            meta::nothrow::has_lt<
                const_reference,
                typename Array<U, Shape, Flags>::const_reference
            > &&
            meta::nothrow::has_lt<
                typename Array<U, Shape, Flags>::const_reference,
                const_reference
            > &&
            meta::nothrow::truthy<meta::lt_type<
                const_reference,
                typename Array<U, Shape, Flags>::const_reference
            >> &&
            meta::nothrow::truthy<meta::lt_type<
                typename Array<U, Shape, Flags>::const_reference,
                const_reference
            >>
        )
        requires (
            meta::has_lt<
                const_reference,
                typename Array<U, Shape, Flags>::const_reference
            > &&
            meta::has_lt<
                typename Array<U, Shape, Flags>::const_reference,
                const_reference
            > &&
            meta::truthy<meta::lt_type<
                const_reference,
                typename Array<U, Shape, Flags>::const_reference
            >> &&
            meta::truthy<meta::lt_type<
                typename Array<U, Shape, Flags>::const_reference,
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
    [[nodiscard]] constexpr auto operator<=>(const ArrayView<U, Shape, Flags>& other) const
        noexcept (
            meta::nothrow::has_lt<
                const_reference,
                typename ArrayView<U, Shape, Flags>::const_reference
            > &&
            meta::nothrow::has_lt<
                typename ArrayView<U, Shape, Flags>::const_reference,
                const_reference
            > &&
            meta::nothrow::truthy<meta::lt_type<
                const_reference,
                typename ArrayView<U, Shape, Flags>::const_reference
            >> &&
            meta::nothrow::truthy<meta::lt_type<
                typename ArrayView<U, Shape, Flags>::const_reference,
                const_reference
            >>
        )
        requires (
            meta::has_lt<
                const_reference,
                typename ArrayView<U, Shape, Flags>::const_reference
            > &&
            meta::has_lt<
                typename ArrayView<U, Shape, Flags>::const_reference,
                const_reference
            > &&
            meta::truthy<meta::lt_type<
                const_reference,
                typename ArrayView<U, Shape, Flags>::const_reference
            >> &&
            meta::truthy<meta::lt_type<
                typename ArrayView<U, Shape, Flags>::const_reference,
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


template <typename T, impl::extent Shape, impl::array_flags<Shape> Flags>
    requires (impl::array_view_concept<T, Shape, Flags> && Shape.empty())
struct ArrayView<T, Shape, Flags> : impl::array_view_tag {
    [[nodiscard]] static constexpr bool dynamic() noexcept { return true; }

    /// TODO: constructor takes a pointer, a span<size_t> describing the shape (or a
    /// scalar for 1-D), and a span<size_t> describing the strides (or a scalar if 1-D).
    /// Maybe array_flags can provide a specialization for this case, which allows the
    /// same syntax as the template signature.



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
template <typename T, impl::extent Shape, impl::array_flags<Shape> Flags>
    requires (impl::array_concept<T, Shape, Flags>)
struct Array : impl::array_tag {
private:
    using init = impl::array_init<T, Shape, Flags>;
    using storage = impl::array_storage<T>;
    static constexpr size_t step = Flags.strides.dim[0];

public:
    using type = T;
    using size_type = size_t;
    using index_type = ssize_t;
    using value_type = impl::array_value<T, Shape, Flags>;
    using reference = meta::as_lvalue<value_type>;
    using const_reference = meta::as_const<reference>;
    using pointer = meta::as_pointer<reference>;
    using const_pointer = meta::as_pointer<const_reference>;
    using iterator = impl::array_iterator<storage, Shape, Flags>;
    using const_iterator = impl::array_iterator<const storage, Shape, Flags>;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    /* Check whether the array extents are dynamic (`true`) or static (`false`).  For
    the `Array` class, this will always evaluate to false, and all extents must be
    fixed at compile time. */
    [[nodiscard]] static constexpr bool dynamic() noexcept {
        return false;
    }

    /* Return the number of bytes taken up by each element.  This is
    expression-equivalent to `sizeof(T)`, and may be multiplied by `strides` in order
    to obtain the raw byte offsets for each dimension. */
    [[nodiscard]] static constexpr size_t itemsize() noexcept {
        return sizeof(T);
    }

    /* The size of the array along each dimension, which is equivalent to the shape
    given in the template signature.  The return type is an `impl::extent` object that
    can be indexed, iterated over, and destructured like a `std::array`, but with
    Python-style wraparound. */
    [[nodiscard]] static constexpr const auto& shape() noexcept {
        return Shape;
    }

    /* The step size of the array along each dimension in units of `T`, which is
    equivalent to the strides given in the flags portion of the template signature.  If
    no strides are given, then they will be computed from the array's shape according
    to row-major order (if `flags.fortran == false`) or column-major order (if
    `flags.fortran == true`).  The return type is an `impl::extent` object that
    can be indexed, iterated over, and destructured like a `std::array`, but with
    Python-style wraparound. */
    [[nodiscard]] static constexpr const auto& strides() noexcept {
        return Flags.strides;
    }

    /* Return the flags associated with the array, which include the `fortran` flag
    and strides, and can be used to rebind the `Array` or `ArrayView` classes. */
    [[nodiscard]] static constexpr const auto& flags() noexcept {
        return Flags;
    }

    /* The number of dimensions for a multidimensional array.  This is always equal
    to the number of indices in its shape, and is never zero. */
    [[nodiscard]] static constexpr size_type ndim() noexcept {
        return Shape.size();
    }

    /* The total number of elements in the array across all dimensions.  This is
    always equivalent to the cartesian product of `shape()`. */
    [[nodiscard]] static constexpr size_type total() noexcept {
        return Shape.product();
    }

    /* The top-level size of the array (i.e. the size of the first dimension).
    This is always equal to the first index of `shape()`, and indicates the number
    of subarrays that will be yielded when the array is iterated over. */
    [[nodiscard]] static constexpr size_type size() noexcept {
        return Shape.dim[0];
    }

    /* Equivalent to `size()`, but as a signed rather than unsigned integer. */
    [[nodiscard]] static constexpr index_type ssize() noexcept {
        return index_type(size());
    }

    /* True if the array has precisely zero elements.  False otherwise.  This can only
    occur for one-dimensional arrays. */
    [[nodiscard]] static constexpr bool empty() noexcept {
        return size() == 0;
    }

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

    /// TODO: these constructors should be templated on `ArrayView<Ts, shape, flags>...`
    /// where `Ts...` is convertible to `T`, and shape is computed as the 

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
        using view = impl::array_squeeze<const storage, N, Ns...>::template view<>;
        return view{static_cast<const storage*>(ptr)};
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
                using view = impl::array_reduce<Shape, Flags>::template view<const storage>;
                return view{static_cast<const storage*>(self.ptr)};
            } else {
                using view = impl::array_reduce<Shape, Flags>::template view<storage>;
                return view{self.ptr};
            }
        } else {
            using array = impl::array_reduce<Shape, Flags>::template array<T>;
            auto result = array::reserve();
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
                using view = impl::array_reduce<Shape, Flags>::template view<const storage>;
                return view{static_cast<const storage*>(self.ptr) + j};
            } else {
                using view = impl::array_reduce<Shape, Flags>::template view<storage>;
                return view{self.ptr + j};
            }
        } else {
            using array = impl::array_reduce<Shape, Flags>::template array<T>;
            auto result = array::reserve();
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
            impl::valid_array_index<I...>::template value<Shape, Flags>
        )
    [[nodiscard]] constexpr decltype(auto) get(this Self&& self) noexcept {
        constexpr size_type j = impl::valid_array_index<I...>::template index<Shape, Flags>;
        if constexpr (sizeof...(I) == ndim()) {
            return (std::forward<Self>(self).ptr[j].value);
        } else if constexpr (meta::lvalue<Self>) {
            if constexpr (meta::is_const<Self>) {
                using view = impl::array_reduce<Shape, Flags, sizeof...(I)>::template view<
                    const storage
                >;
                return view{static_cast<meta::as_const<T>*>(self.ptr) + j};
            } else {
                using view = impl::array_reduce<Shape, Flags, sizeof...(I)>::template view<storage>;
                return view{self.ptr + j};
            }
        } else {
            using array = impl::array_reduce<Shape, Flags, sizeof...(I)>::template array<T>;
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
                using view = impl::array_reduce<Shape, Flags, sizeof...(I)>::template view<
                    const storage
                >;
                return view{static_cast<const storage*>(self.ptr) + j};
            } else {
                using view = impl::array_reduce<Shape, Flags, sizeof...(I)>::template view<storage>;
                return view{self.ptr + j};
            }
        } else {
            using array = impl::array_reduce<Shape, Flags, sizeof...(I)>::template array<T>;
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


namespace ranges {

    template <bertrand::meta::ArrayView T>
    constexpr bool enable_borrowed_range<T> = !bertrand::meta::unqualify<T>::dynamic();

}


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
