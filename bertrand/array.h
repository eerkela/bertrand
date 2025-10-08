#ifndef BERTRAND_ARRAY_H
#define BERTRAND_ARRAY_H

#include "bertrand/common.h"
#include "bertrand/except.h"


namespace bertrand {


namespace impl {
    struct array_tag {};
    struct array_view_tag {};

    template <size_t ndim>
    struct extent;

    template <meta::integer... N>
    extent(N...) -> extent<sizeof...(N)>;

    template <typename T>
        requires (
            !meta::integer<T> &&
            meta::tuple_like<T> &&
            (meta::yields<size_t, T> || meta::tuple_types<T>::template convertible_to<size_t>)
        )
    extent(T&&) -> extent<meta::tuple_size<T>>;

    template <typename T>
        requires (
            !meta::integer<T> &&
            !meta::tuple_like<T> &&
            meta::yields<size_t, T> &&
            meta::has_size<T>
        )
    extent(T&&) -> extent<0>;

    /* Run-time shape and stride specifiers can represent dynamic data, and may require
    a heap allocation if they are more than 1-dimensional.  They otherwise mirror
    their compile-time equivalents exactly. */
    template <>
    struct extent<0> {
    private:
        using heap = std::allocator<const size_t>;

        constexpr void _tuple_shape(size_t& i, size_t n) noexcept {
            std::construct_at(dim + i, n);
            ++i;
        }

        template <meta::tuple_like T, size_t... Is> requires (sizeof...(Is) == meta::tuple_size<T>)
        constexpr void tuple_shape(T&& shape, std::index_sequence<Is...>)
            noexcept (requires(size_t i) {
                {(_tuple_shape(i,meta::get<Is>(std::forward<T>(shape))), ...)} noexcept;
            })
        {
            size_t i = 0;
            (_tuple_shape(i, meta::get<Is>(std::forward<T>(shape))), ...);
        }

    public:
        enum extent_kind : uint8_t {
            TRIVIAL,
            BORROWED,
            UNIQUE
        };

        size_t ndim = 0;
        const size_t* dim = nullptr;
        extent_kind kind = TRIVIAL;

        [[nodiscard]] constexpr extent() noexcept = default;
        [[nodiscard]] constexpr extent(size_t n) noexcept : ndim(n) {}
        [[nodiscard]] constexpr extent(size_t* dim, size_t ndim) noexcept :
            ndim(ndim),
            dim(static_cast<const size_t*>(dim)),
            kind(BORROWED)
        {}
        [[nodiscard]] constexpr extent(const size_t* dim, size_t ndim) noexcept :
            ndim(ndim),
            dim(dim),
            kind(BORROWED)
        {}

        [[nodiscard]] constexpr extent(std::initializer_list<size_t> n) {
            if (n.size() > 1) {
                kind = UNIQUE;
                dim = heap{}.allocate(n.size());
                if (dim == nullptr) {
                    throw MemoryError();
                }
                auto it = n.begin();
                auto end = n.end();
                while (it != end) {
                    std::construct_at(dim + ndim, *it);
                    ++ndim;
                    ++it;
                }
            } else if (n.size() == 1) {
                ndim = *n.begin();
            }
        }

        template <typename T>
            requires (
                !meta::convertible_to<T, size_t> &&
                !meta::convertible_to<T, std::initializer_list<size_t>> &&
                meta::yields<size_t, T> &&
                (meta::tuple_like<T> || meta::size_returns<size_t, T>)
            )
        [[nodiscard]] constexpr extent(T&& n) {
            size_t len;
            if constexpr (meta::tuple_like<T>) {
                len = meta::tuple_size<T>;
            } else {
                len = n.size();
            }
            if (len > 1) {
                kind = UNIQUE;
                dim = heap{}.allocate(len);
                if (dim == nullptr) {
                    throw MemoryError();
                }
                auto it = std::ranges::begin(n);
                auto end = std::ranges::end(n);
                while (it != end) {
                    std::construct_at(dim + ndim, *it);
                    ++ndim;
                    ++it;
                }
            } else if (len == 1) {
                ndim = *std::ranges::begin(n);
            }
        }

        template <typename T>
            requires (
                !meta::convertible_to<T, size_t> &&
                !meta::convertible_to<T, std::initializer_list<size_t>> &&
                !meta::yields<size_t, T> &&
                meta::tuple_like<T> &&
                meta::tuple_types<T>::template convertible_to<size_t>
            )
        [[nodiscard]] constexpr extent(T&& n) {
            constexpr size_t len = meta::tuple_size<T>;
            if constexpr (len > 1) {
                kind = UNIQUE;
                dim = heap{}.allocate(len);
                if (dim == nullptr) {
                    throw MemoryError();
                }
                tuple_shape(std::forward<T>(n), std::make_index_sequence<len>{});
            } else if constexpr (len == 1) {
                ndim = meta::get<0>(n);
            }
        }

        [[nodiscard]] constexpr extent(const extent& other) :
            ndim(other.ndim),
            kind(other.kind)
        {
            switch (other.kind) {
                case BORROWED:
                    dim = other.dim;
                    break;
                case UNIQUE:
                    dim = heap{}.allocate(ndim);
                    if (dim == nullptr) {
                        throw MemoryError();
                    }
                    for (size_t i = 0; i < ndim; ++i) {
                        std::construct_at(&dim[i], other.dim[i]);
                    }
                    break;
                default:
                    break;
            }
        }

        [[nodiscard]] constexpr extent(extent&& other) noexcept :
            ndim(other.ndim),
            dim(other.dim),
            kind(other.kind)
        {
            other.ndim = 0;
            other.dim = nullptr;
            other.kind = TRIVIAL;
        }

        constexpr extent& operator=(const extent& other) {
            if (this != &other) {
                if (kind == UNIQUE) {
                    heap{}.deallocate(dim, ndim);
                }
                ndim = other.ndim;
                kind = other.kind;
                switch (other.kind) {
                    case BORROWED:
                        dim = other.dim;
                        break;
                    case UNIQUE:
                        dim = heap{}.allocate(ndim);
                        if (dim == nullptr) {
                            throw MemoryError();
                        }
                        for (size_t i = 0; i < ndim; ++i) {
                            std::construct_at(&dim[i], other.dim[i]);
                        }
                        break;
                    default:
                        break;
                }
            }
            return *this;
        }

        constexpr extent& operator=(extent&& other) noexcept {
            if (this != &other) {
                if (kind == UNIQUE) {
                    heap{}.deallocate(dim, ndim);
                }
                ndim = other.ndim;
                dim = other.dim;
                kind = other.kind;
                other.ndim = 0;
                other.dim = nullptr;
                other.kind = TRIVIAL;
            }
            return *this;
        }

        constexpr ~extent() {
            if (kind == UNIQUE) {
                heap{}.deallocate(dim, ndim);
            }
        }

        constexpr void swap(extent& other) noexcept {
            std::swap(ndim, other.ndim);
            std::swap(dim, other.dim);
            std::swap(kind, other.kind);
        }

        [[nodiscard]] constexpr size_t size() const noexcept {
            return kind == TRIVIAL ? ndim > 0 : ndim;
        }

        [[nodiscard]] constexpr ssize_t ssize() const noexcept { return ssize_t(size()); }
        [[nodiscard]] constexpr bool empty() const noexcept { return ndim == 0; }
        [[nodiscard]] constexpr const size_t* data() const noexcept {
            return kind == TRIVIAL ? &ndim : dim;
        }
        [[nodiscard]] constexpr const size_t* begin() const noexcept { return data(); }
        [[nodiscard]] constexpr const size_t* cbegin() const noexcept { return begin(); }
        [[nodiscard]] constexpr const size_t* end() const noexcept {
            return kind == TRIVIAL ? &ndim + 1 : dim + ndim;
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

        [[nodiscard]] constexpr size_t operator[](ssize_t i) const {
            return data()[impl::normalize_index(ssize(), i)];
        }

        template <size_t R>
        [[nodiscard]] constexpr bool operator==(const extent<R>& other) const noexcept {
            if constexpr (R == 0) {
                if (ndim != other.ndim) {
                    return false;
                }
                if (kind != TRIVIAL) {
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

        [[nodiscard]] constexpr size_t product() const noexcept {
            if (kind == TRIVIAL) {
                return ndim;
            }
            size_t p = 1;
            for (size_t j = 0; j < ndim; ++j) {
                p *= dim[j];
            }
            return p;
        }

        [[nodiscard]] constexpr extent reverse() const {
            if (kind == TRIVIAL || ndim == 1) {
                return *this;
            }
            extent r;
            r.ndim = ndim;
            r.dim = heap{}.allocate(ndim);
            if (r.dim == nullptr) {
                throw MemoryError();
            }
            r.kind = UNIQUE;
            for (size_t j = 0; j < ndim; ++j) {
                std::construct_at(&r.dim[j], dim[ndim - 1 - j]);
            }
            return r;
        }

        [[nodiscard]] constexpr extent reduce(size_t n) const {
            if (n == 0) {
                return *this;
            }
            extent result;
            if (kind != TRIVIAL && n < ndim) {
                if (n + 1 == ndim) {  // result is 1D
                    result.ndim = dim[n];
                } else {  // result is > 1D
                    result.dim = dim + n;
                    result.ndim = ndim - n;
                    result.kind = BORROWED;
                }
            }
            return result;
        }

        [[nodiscard]] constexpr extent strides(bool column_major) const {
            extent result;
            if (kind == TRIVIAL) {
                result.ndim = ndim > 0;
            } else {
                result.dim = heap{}.allocate(ndim);
                if (result.dim == nullptr) {
                    throw MemoryError();
                }
                result.ndim = ndim;
                result.kind = UNIQUE;
                if (column_major) {
                    std::construct_at(&result.dim[0], 1);
                    for (size_t j = 1; j < ndim; ++j) {
                        std::construct_at(
                            &result.dim[j],
                            result.dim[j - 1] * dim[j - 1]
                        );
                    }
                } else {
                    size_t j = size() - 1;
                    std::construct_at(&result.dim[j], 1);
                    while (j-- > 0) {
                        std::construct_at(
                            &result.dim[j],
                            result.dim[j + 1] * dim[j + 1]
                        );
                    }
                }
            }
            return result;
        }
    };

    /// TODO: maybe, similar to `extent<0>` (i.e. `Span<T, {}>`), which represents a
    /// `Span` of fully dynamic shape, which can have any number of dimensions, an
    /// extent of `0` would indicate a dynamic shape in a single dimension?
    /// `Span<T, {0}>` would therefore be a 1D span of dynamic length.  Ideally, I
    /// would be able to use `Span<T, {{}}>` for this purpose, but that would break
    /// CTAD, so an explicit initializer is necessary.  The only alternative to `0`
    /// is a `bertrand::dynamic` constant of some sort, but that's likely less
    /// transferable to other languages, and I don't need to introduce any new concepts
    /// here.  The other major concern would involve what this would mean for shape
    /// deduction as it relates to tuple-like types.  Perhaps `meta::static_shape`
    /// simply rejects any tuples of length 0, and reserves that for sized iterables
    /// without a member shape?



    /* Compile-time shape and stride specifiers use CTAD to allow simple braced
    initializer syntax, which permits custom strides. */
    template <size_t ndim>
    struct extent {
    private:
        constexpr void _tuple_shape(size_t& i, size_t n) noexcept {
            std::construct_at(dim + i, n);
            ++i;
        }

        template <meta::tuple_like T, size_t... Is> requires (sizeof...(Is) == meta::tuple_size<T>)
        constexpr void tuple_shape(T&& shape, std::index_sequence<Is...>)
            noexcept (requires(size_t i) {
                {(_tuple_shape(i,meta::get<Is>(std::forward<T>(shape))), ...)} noexcept;
            })
        {
            size_t i = 0;
            (_tuple_shape(i, meta::get<Is>(std::forward<T>(shape))), ...);
        }

        struct initializer {
            size_t value = std::numeric_limits<size_t>::max();
            [[nodiscard]] constexpr initializer() noexcept = default;
            [[nodiscard]] constexpr initializer(size_t n) noexcept : value(n) {}
            [[nodiscard]] constexpr operator size_t() const noexcept { return value; }
        };

    public:
        size_t dim[ndim];

        [[nodiscard]] constexpr extent() noexcept = default;
        [[nodiscard]] constexpr extent(size_t n) noexcept requires (ndim == 1) : dim{n} {}
        [[nodiscard]] constexpr extent(std::initializer_list<size_t> n) {
            size_t i = 0;
            auto it = n.begin();
            auto end = n.end();
            while (i < ndim && it != end) {
                if (i == ndim) {
                    throw ValueError("too many dimensions in shape");
                }
                std::construct_at(dim + i, *it);
                ++i;
                ++it;
            }
            if (i != ndim) {
                throw ValueError("too few dimensions in shape");
            }
        }

        template <typename T>
        [[nodiscard]] constexpr extent(T&& n)
            noexcept (meta::nothrow::yields<T, size_t>)
            requires (
                !meta::convertible_to<T, size_t> &&
                !meta::convertible_to<T, std::initializer_list<size_t>> &&
                meta::tuple_like<T> &&
                meta::tuple_size<T> == ndim &&
                meta::yields<size_t, T>
            )
        {
            size_t i = 0;
            auto it = std::ranges::begin(n);
            auto end = std::ranges::end(n);
            while (i < ndim && it != end) {
                std::construct_at(dim + i, *it);
                ++i;
                ++it;
            }
        }

        template <typename T>
        [[nodiscard]] constexpr extent(T&& n)
            noexcept (requires{
                {tuple_shape(std::forward<T>(n), std::make_index_sequence<ndim>{})} noexcept;
            })
            requires (
                !meta::convertible_to<T, size_t> &&
                !meta::convertible_to<T, std::initializer_list<size_t>> &&
                meta::tuple_like<T> &&
                meta::tuple_size<T> == ndim &&
                !meta::yields<size_t, T> &&
                meta::tuple_types<T>::template convertible_to<size_t>
            )
        {
            tuple_shape(std::forward<T>(n), std::make_index_sequence<ndim>{});
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
        [[nodiscard]] constexpr size_t* begin() noexcept { return dim; }
        [[nodiscard]] constexpr const size_t* begin() const noexcept {
            return static_cast<const size_t*>(dim);
        }
        [[nodiscard]] constexpr const size_t* cbegin() const noexcept { return begin(); }
        [[nodiscard]] constexpr size_t* end() noexcept { return dim + ndim; }
        [[nodiscard]] constexpr const size_t* end() const noexcept {
            return static_cast<const size_t*>(dim) + ndim;
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

        template <ssize_t I> requires (impl::valid_index<ssize(), I>)
        [[nodiscard]] constexpr size_t get() const noexcept {
            return dim[impl::normalize_index<ssize(), I>()];
        }

        [[nodiscard]] constexpr size_t operator[](ssize_t i) const {
            return dim[impl::normalize_index(ssize(), i)];
        }

        template <size_t R>
        [[nodiscard]] constexpr bool operator==(const extent<R>& other) const noexcept {
            if constexpr (R == ndim) {
                for (size_t i = 0; i < ndim; ++i) {
                    if (dim[i] != other.dim[i]) {
                        return false;
                    }
                }
                return true;
            } else {
                return false;
            }
        }

        [[nodiscard]] constexpr size_t product() const noexcept {
            size_t p = 1;
            for (size_t j = 0; j < ndim; ++j) {
                p *= dim[j];
            }
            return p;
        }

        [[nodiscard]] constexpr extent reverse() const noexcept {
            extent r;
            for (size_t j = 0; j < ndim; ++j) {
                std::construct_at(&r.dim[j], dim[ndim - 1 - j]);
            }
            return r;
        }

        template <size_t N>
        [[nodiscard]] constexpr auto reduce() const noexcept {
            if constexpr (N >= ndim) {
                return extent<0>{};
            } else {
                extent<ndim - N> s;
                for (size_t j = N; j < ndim; ++j) {
                    std::construct_at(&s.dim[j - N], dim[j]);
                }
                return s;
            }
        }

        [[nodiscard]] constexpr extent strides(bool column_major) const noexcept {
            extent s;
            if (column_major) {
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
    };

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
    [[nodiscard]] constexpr extent<N + 1> operator|(size_t other, const extent<N>& self) noexcept {
        extent<N + 1> s;
        std::construct_at(&s.dim[0], other);
        for (size_t j = 1; j <= N; ++j) {
            std::construct_at(&s.dim[j], self.dim[j - 1]);
        }
        return s;
    }

    /* A configuration struct that holds miscellaneous flags for an array or array
    view.  An instance of this type can be constructed using designated initialization
    for clarity. */
    template <extent shape = {}>
    struct array_flags {
        /* If false (the default), then the strides will be computed in C (row-major)
        order, with the last index varying the fastest.  Setting this to true specifies
        that indices should be interpreted in Fortran (column-major) order instead,
        where the first index varies the fastest.  Toggling this flag for a view
        effectively transposes the array. */
        bool column_major = false;

        /* A set of custom strides to use for each dimension.  If given, the strides
        will always match their corresponding shape dimension. */
        extent<shape.size()> strides = shape.strides(column_major);
    };

    /* For some reason (likely a compiler bug), the default specialization of
    `array_flags` cannot be default-constructed unless we explicitly instanitate a
    version of it here.  Note that this value is never used anywhere, but deleting it
    surfaces a pretty cryptic set of template instantiation errors. */
    inline constexpr array_flags<> __default_array_flags {};

    template <typename T, extent shape, array_flags<shape> flags>
    concept array_concept =
        meta::not_void<T> &&
        meta::not_reference<T> &&
        !shape.empty() &&
        shape.product() > 0;

    template <typename T, extent shape, array_flags<shape> flags>
    concept array_view_concept = meta::not_void<T> && meta::not_reference<T>;

    /* Dynamic extents may not strictly know the total number of dimensions at compile
    time, and therefore require a dynamic allocation in order to produce an appropriate
    shape and stride buffer.  In order to optimize this as much as possible, both the
    shape and stride buffers will be allocated together in a single region along with a
    header that contains an atomic reference count and precomputed size.  In the 1D
    case, `extent<0>` may be able to avoid the allocation entirely and inline both
    buffers directly into the `extent` struct itself.  Note that such extents are
    trivially both row-major and column-major at the same time. */
    struct heap_extent {
        size_t ndim;
        size_t* buffer = nullptr;
        std::atomic<size_t> refcount = 1;

        /* Allocate a `heap_extent` with enough extra space to store the shape and
        stride buffers.  If the extent is created at runtime, the buffers will be
        stored inline immediately after this header struct and accessed via a
        `reinterpret_cast`.  At compile time, the buffers will be stored out-of-line
        using a second allocation to avoid the cast. */
        [[nodiscard]] static constexpr heap_extent* create(size_t ndim) {
            if consteval {
                heap_extent* self = std::allocator<heap_extent>{}.allocate(1);
                if (self == nullptr) {
                    throw MemoryError();
                }
                std::construct_at(self);
                self->ndim = ndim;
                self->buffer = std::allocator<size_t>{}.allocate(ndim * 2);
                if (self->buffer == nullptr) {
                    std::allocator<heap_extent>{}.deallocate(self, 1);
                    throw MemoryError();
                }
                return self;
            } else {
                heap_extent* self = reinterpret_cast<heap_extent*>(
                    std::allocator<std::byte>{}.allocate(
                        sizeof(heap_extent) + ndim * sizeof(size_t) * 2
                    )
                );
                if (self == nullptr) {
                    throw MemoryError();
                }
                std::construct_at(self);
                self->ndim = ndim;
                self->buffer = reinterpret_cast<size_t*>(self + 1);
                return self;
            }
        }

        /* Copy the `heap_extent` by incrementing its reference count at run time or
        doing a deep copy at compile time, since atomics are not fully constexpr as of
        C++23.  Note that due to the deep copy, the return value must not be
        discarded. */
        [[nodiscard]] constexpr heap_extent* incref() {
            if consteval {
                heap_extent* copy = std::allocator<heap_extent>{}.allocate(1);
                if (copy == nullptr) {
                    throw MemoryError();
                }
                std::construct_at(copy);
                copy->ndim = ndim;
                copy->buffer = std::allocator<size_t>{}.allocate(ndim * 2);
                if (copy->buffer == nullptr) {
                    std::allocator<heap_extent>{}.deallocate(copy, 1);
                    throw MemoryError();
                }
                std::copy_n(buffer, ndim * 2, copy->buffer);
                return copy;
            } else {
                refcount.fetch_add(1, std::memory_order_relaxed);
                return this;
            }
        }

        /* Destroy the `heap_extent` by decrementing its reference count at run time
        or unconditionally deallocating at compile time, in order to mirror
        `incref()`. */
        constexpr void decref() {
            if consteval {
                std::allocator<size_t>{}.deallocate(buffer, ndim * 2);
                std::allocator<heap_extent>{}.deallocate(this, 1);
            } else {
                if (refcount.fetch_sub(1, std::memory_order_release) == 1) {
                    std::atomic_thread_fence(std::memory_order_acquire);
                    std::allocator<std::byte>{}.deallocate(
                        reinterpret_cast<std::byte*>(this),
                        sizeof(heap_extent) + ndim * sizeof(size_t) * 2
                    );
                }
            }
        };
    };

}


/* An explicit alias to `0`, which indicates an array dimension with an extent that is
only known at run time, similar to the syntax used for `std::mdspan`.  Note that this
cannot be used to specialize the `Array` type, since they are expected to store data
locally on the stack, and therefore need to know their exact bounds at compile time.
Dynamic extents are allowed for `ArrayView`s and `Arena`s however, since they either do
not own their underlying data, or dynamically allocate it, respectively. */
static constexpr size_t dynamic_extent = 0;


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

    namespace detail {

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

        /* `shape_dim` returns a pair where the first index detects whether all values
        of the tuple `T` are also tuple-like and have a consistent size, and the second
        returns that size. */
        template <
            meta::tuple_like U,
            typename = ::std::make_index_sequence<meta::tuple_size<U>>
        >
        static constexpr ::std::pair<bool, size_t> shape_dim {meta::tuple_size<U> == 0, 0};
        template <meta::tuple_like U, size_t I, size_t... Is>
            requires ((
                meta::tuple_like<meta::get_type<U, I>> &&
                ... &&
                meta::tuple_like<meta::get_type<U, Is>>
            ) && ((
                meta::tuple_size<meta::get_type<U, I>> ==
                meta::tuple_size<meta::get_type<U, Is>>
            ) && ...))
        static constexpr ::std::pair<bool, size_t> shape_dim<U, ::std::index_sequence<I, Is...>> {
            true,
            meta::tuple_size<meta::get_type<U, I>>
        };

        /* `deduce_shape` initially takes a pack containing a single tuple-like type,
        which is either the top-level type directly or its yield type.  In the former
        case, that type's tuple size is also required as an initializer, whereas in the
        latter case, its runtime size must be provided as an argument to the call
        operator.  If all of that type's elements are also tuple-like and have a
        consistent size, then that size will be appended to `Is...`, and the type will
        be replaced by all its elements, repeating the check.  This continues until
        either a non-tuple-like element or a size mismatch is encountered, which marks
        the end of the shape. */
        template <typename, size_t... Is>
        struct deduce_shape {
            static constexpr auto operator()() noexcept { return impl::extent{Is...}; }
            static constexpr auto operator()(size_t n) noexcept {
                return impl::extent{n, Is...};
            }
        };
        template <meta::tuple_like U, meta::tuple_like... Us, size_t... Is>
            requires (
                (shape_dim<U>.first && ... && shape_dim<Us>.first) &&
                ((shape_dim<U>.second == shape_dim<Us>.second) && ...)
            )
        struct deduce_shape<meta::pack<U, Us...>, Is...> : deduce_shape<
            meta::concat<meta::tuple_types<U>, meta::tuple_types<Us>...>,
            Is...,
            shape_dim<U>.second
        > {};

        /* `static_shape_fn<T>` backs `meta::static_shape<T>()` and attempts to deduce
        a shape entirely at compile time. */
        template <typename T>
        struct static_shape_fn {
            static constexpr decltype(auto) operator()()
                noexcept (requires{{unqualify<T>::shape()} noexcept;})
                requires (member::has_static_shape<T>)
            {
                return (unqualify<T>::shape());
            }

            static constexpr decltype(auto) operator()()
                noexcept (requires{{impl::extent(shape<T>())} noexcept;})
                requires (!member::has_static_shape<T> && adl::has_static_shape<T>)
            {
                return (shape<T>());
            }

            static constexpr auto operator()() noexcept
                requires (
                    !member::has_shape<T> &&
                    !adl::has_shape<T> &&
                    meta::tuple_like<T>
                )
            {
                return deduce_shape<meta::pack<T>, meta::tuple_size<T>>{}();
            }
        };

        /* `shape_fn` backs `meta::shape(t)`, which extends `meta::static_shape` to
        take runtime information into account, allowing instance-level shape
        introspection, iterables, etc. */
        struct shape_fn {
            template <typename T>
            static constexpr decltype(auto) operator()(T&& t)
                noexcept (requires{{static_shape_fn<T>{}()} noexcept;})
                requires (requires{{static_shape_fn<T>{}()};})
            {
                return (static_shape_fn<T>{}());
            }

            template <typename T>
            static constexpr decltype(auto) operator()(T&& t)
                noexcept (requires{{::std::forward<T>(t).shape()} noexcept;})
                requires (
                    !requires{{static_shape_fn<T>{}()};} &&
                    member::has_shape<T>
                )
            {
                return (::std::forward<T>(t).shape());
            }

            template <typename T>
            static constexpr decltype(auto) operator()(T&& t)
                noexcept (requires{{shape(::std::forward<T>(t))} noexcept;})
                requires (
                    !requires{{static_shape_fn<T>{}()};} &&
                    !member::has_shape<T> &&
                    adl::has_shape<T>
                )
            {
                return (shape(::std::forward<T>(t)));
            }

            template <typename T>
            static constexpr auto operator()(T&& t)
                noexcept (meta::nothrow::size_returns<size_t, T>)
                requires (
                    !requires{{static_shape_fn<T>{}()};} &&
                    !member::has_shape<T> &&
                    !adl::has_shape<T> &&
                    meta::yields<size_t, T> &&
                    meta::size_returns<size_t, T>
                )
            {
                return deduce_shape<meta::pack<meta::yield_type<T>>>{}(::std::ranges::size(t));
            }
        };

    }

    /* Retrieve the `shape()` of a generic type `T` where such information is
    independent of any particular instance of `T`.  This relies on a `T::shape()` or
    ADL `shape<T>()` method, or `T` being a possibly-nested tuple type, for which a
    shape can be deduced.

    If the type is tuple-like, then the first element of the shape must be equal to its
    `tuple_size`.  If all of its elements are also tuple-like and have the same size,
    then that size will be appended to the shape as the next element, and so on until a
    non-tuple-like element or a size mismatch is encountered, which marks the end of
    the shape.

    The result is always returned as an instance of `impl::extent<N>`. */
    template <typename T>
    inline constexpr detail::static_shape_fn<T> static_shape;

    /* Retrieve the `shape()` of a generic object `t` of type `T` by first checking for
    a `static_shape<T>()` method, falling back to a member `t.shape()` method and then
    an ADL `shape(t)` method.  If none are present, and the type is a sized iterable
    (possibly yielding tuple-like types), then a shape may be deduced similar to
    `static_shape<T>()`, but using the iterable's size as the first element of the
    resulting shape.

    The result is always returned as an instance of `impl::extent<N>`. */
    inline constexpr detail::shape_fn shape;

    template <typename t>
    concept has_static_shape = requires{{static_shape<t>()};};

    template <typename T>
    concept has_shape = requires(T t) {{shape(t)};};

    template <has_static_shape T>
    using static_shape_type = decltype(static_shape<T>());

    template <has_shape T>
    using shape_type = decltype(shape(::std::declval<T>()));

    template <typename Ret, typename T>
    concept static_shape_returns =
        has_static_shape<T> && convertible_to<static_shape_type<T>, Ret>;

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


namespace impl {

    /* Reduce the rank of an array by stripping off `N` dimensions according to the
    flags' ordering. */
    template <extent Shape, extent Strides, size_t N = 1> requires (N > 0)
    struct array_reduce {
        static constexpr auto shape() noexcept {
            if constexpr (Shape.empty()) {
                return extent<0>{};
            } else {
                return Shape.template reduce<N>();
            }
        }

        static constexpr auto strides() noexcept {
            if constexpr (Shape.empty()) {
                return extent<0>{};
            } else {
                return Strides.template reduce<N>();
            }
        };

        template <typename T>
        using array = Array<T, shape(), {.strides = strides()}>;

        template <typename T>
        using view = ArrayView<T, shape(), {.strides = strides()}>;
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
    template <typename T, extent shape, extent strides>
    struct _array_init { using type = T; };
    template <typename T, extent shape, extent strides> requires (!shape.empty())
    struct _array_init<T, shape, strides> { using type = Array<T, shape, {.strides = strides}>; };
    template <typename T, extent shape, extent strides> requires (!shape.empty())
    using array_init = _array_init<
        T,
        array_reduce<shape, strides>::shape(),
        array_reduce<shape, strides>::strides()
    >::type;

    /* The outermost array value type also needs to be promoted to an array view if
    it is multidimensional, and needs to correct for the `array_storage` normalization
    as well. */
    template <typename T, extent shape, extent strides>
    struct _array_value { using type = array_type<T>; };
    template <typename T, extent shape, extent strides> requires (!shape.empty())
    struct _array_value<T, shape, strides> {
        using type = ArrayView<T, shape, {.strides = strides}>;
    };
    template <typename T, extent shape, extent strides> requires (!shape.empty())
    using array_value = _array_value<
        T,
        array_reduce<shape, strides>::shape(),
        array_reduce<shape, strides>::strides()
    >::type;

    template <typename CRTP>
    struct array_iterator_base {
        using difference_type = std::ptrdiff_t;

        constexpr CRTP& operator++(this CRTP& self) noexcept {
            if consteval {
                ++self.count;
                if (self.count > 0 && self.count < self.size) {
                    self.ptr += self.step;
                }
            } else {
                self.ptr += self.step;
            }
            return self;
        }

        [[nodiscard]] constexpr CRTP operator++(this CRTP& self, int)
            noexcept (meta::nothrow::copyable<CRTP>)
        {
            CRTP tmp = self;
            ++self;
            return tmp;
        }

        constexpr CRTP& operator+=(this CRTP& self, difference_type n) noexcept {
            if consteval {
                difference_type new_count = self.count + n;
                if (new_count < 0) {
                    self.ptr -= self.count * (self.count > 0) * self.step;
                } else if (new_count >= self.size) {
                    self.ptr +=
                        (self.size - self.count - 1) *
                        (self.count < self.size) *
                        self.step;
                } else {
                    self.ptr += n * self.step;
                }
                self.count = new_count;
            } else {
                self.ptr += n * self.step;
            }
            return self;
        }

        [[nodiscard]] friend constexpr CRTP operator+(const CRTP& self, difference_type n)
            noexcept (meta::nothrow::copyable<CRTP>)
        {
            CRTP tmp = self;
            tmp += n;
            return tmp;
        }

        [[nodiscard]] friend constexpr CRTP operator+(difference_type n, const CRTP& self)
            noexcept (meta::nothrow::copyable<CRTP>)
        {
            CRTP tmp = self;
            tmp += n;
            return tmp;
        }

        constexpr CRTP& operator--(this CRTP& self) noexcept {
            if consteval {
                if (self.count > 0 && self.count < self.size) {
                    self.ptr -= self.step;
                }
                --self.count;
            } else {
                self.ptr -= self.step;
            }
            return self;
        }

        [[nodiscard]] constexpr CRTP operator--(this CRTP& self, int)
            noexcept (meta::nothrow::copyable<CRTP>)
        {
            CRTP tmp = self;
            --self;
            return tmp;
        }

        constexpr CRTP& operator-=(this CRTP& self, difference_type n) noexcept {
            if consteval {
                difference_type new_count = self.count - n;
                if (new_count < 0) {
                    self.ptr -= self.count * (self.count > 0) * self.step;
                } else if (new_count >= self.size) {
                    self.ptr +=
                        (self.size - self.count - 1) *
                        (self.count < self.size) *
                        self.step;
                } else {
                    self.ptr -= n * self.step;
                }
                self.count = new_count;
            } else {
                self.ptr -= n * self.step;
            }
            return self;
        }

        [[nodiscard]] constexpr CRTP operator-(this const CRTP& self, difference_type n)
            noexcept (meta::nothrow::copyable<CRTP>)
        {
            CRTP tmp = self;
            tmp -= n;
            return tmp;
        }

        [[nodiscard]] constexpr difference_type operator-(
            this const CRTP& self,
            const CRTP& other
        ) noexcept {
            if consteval {
                return self.count - other.count;
            } else {
                return (self.ptr - other.ptr) / self.step;
            }
        }

        [[nodiscard]] constexpr bool operator==(
            this const CRTP& self,
            const CRTP& other
        ) noexcept {
            if consteval {
                return self.count == other.count;
            } else {
                return self.ptr == other.ptr;
            }
        }

        [[nodiscard]] constexpr auto operator<=>(
            this const CRTP& self,
            const CRTP& other
        ) noexcept {
            if consteval {
                return self.count <=> other.count;
            } else {
                return self.ptr <=> other.ptr;
            }
        }
    };

    /* Array iterators are implemented as raw pointers into the array buffer.  Due to
    aggressive UB sanitization during constant evaluation, an extra count is required
    to avoid overstepping the end of the array.  This index is ignored at run time,
    ensuring zero-cost iteration, without interfering with compile-time uses. */
    template <meta::not_reference T, extent Shape, extent Strides>
    struct array_iterator : array_iterator_base<array_iterator<T, Shape, Strides>> {
        using iterator_category = std::conditional_t<
            Strides[0] == 1,
            std::contiguous_iterator_tag,
            std::random_access_iterator_tag
        >;
        using difference_type = std::ptrdiff_t;
        using value_type = array_value<T, Shape, Strides>;
        using reference = meta::as_lvalue<value_type>;
        using pointer = meta::as_pointer<value_type>;

        T* ptr = nullptr;
        difference_type count = 0;
        static constexpr const size_t step = *Strides.data();
        static constexpr const size_t size = *Shape.data();

        [[nodiscard]] constexpr array_iterator() = default;
        [[nodiscard]] constexpr array_iterator(T* ptr, difference_type count = 0) :
            ptr(ptr),
            count(count)
        {}

        [[nodiscard]] constexpr decltype(auto) operator*() const noexcept {
            if constexpr (Shape.size() == 1) {
                return array_access(ptr);
            } else {
                using view = value_type;
                return view{ptr};
            }
        }

        [[nodiscard]] constexpr auto operator->() const noexcept {
            if constexpr (Shape.size() == 1) {
                return std::addressof(**this);
            } else {
                return impl::arrow{**this};
            }
        }

        [[nodiscard]] constexpr decltype(auto) operator[](difference_type n) const noexcept {
            if constexpr (Shape.size() == 1) {
                return array_access(ptr, n);
            } else {
                using view = value_type;
                return view{ptr + n * step};
            }
        }
    };

    /// TODO: another iterator type that inlines the dynamic buffers, assuming we
    /// know how many dimensions we need at compile time (e.g. 2 or 3)  The CRTP base
    /// makes this relatively easy.

    /* Iterators over dynamically-shaped arrays need to extend the liftetime of the
    shape and stride buffers, and produce further dynamic views of reduced dimension,
    bottoming out at a trivial view over a single element. */
    template <meta::not_reference T>
    struct array_iterator<T, {}, {}> : array_iterator_base<array_iterator<T, {}, {}>> {
        using iterator_category = std::random_access_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = ArrayView<T>;
        using reference = meta::as_lvalue<value_type>;
        using pointer = meta::as_pointer<value_type>;

        T* ptr = nullptr;  // data buffer
        size_t step = 0;  // in current dimension
        size_t size = 0;  // in current dimension
        size_t scope = 0;  // current dimension index
        size_t product = size;  // of all remaining dimensions
        impl::heap_extent* capsule = nullptr;  // shape/stride buffer (if not 1D)
        difference_type count = 0;  // only used at compile time (to avoid UB)
        size_t reduced_product = product / size;  // avoid integer division in operator*()

        [[nodiscard]] constexpr array_iterator() = default;
        [[nodiscard]] constexpr array_iterator(
            T* ptr,
            size_t step,
            size_t size,
            size_t scope = 0,
            size_t product = 0,
            impl::heap_extent* capsule = nullptr,
            difference_type count = 0
        ) :
            ptr(ptr),
            step(step),
            size(size),
            scope(scope),
            product(product),
            capsule(capsule),
            count(count)
        {}

        [[nodiscard]] constexpr array_iterator(const array_iterator& other) :
            ptr(other.ptr),
            step(other.step),
            size(other.size),
            scope(other.scope),
            product(other.product),
            capsule(other.capsule == nullptr ? nullptr : other.capsule->incref()),
            count(other.count),
            reduced_product(other.reduced_product)
        {}

        [[nodiscard]] constexpr array_iterator(array_iterator&& other) noexcept :
            ptr(other.ptr),
            step(other.step),
            size(other.size),
            scope(other.scope),
            product(other.product),
            capsule(other.capsule),
            count(other.count),
            reduced_product(other.reduced_product)
        {
            other.ptr = nullptr;
            other.step = 0;
            other.size = 0;
            other.scope = 0;
            other.product = 0;
            other.capsule = nullptr;
            other.count = 0;
            other.reduced_product = 0;
        }

        constexpr array_iterator& operator=(const array_iterator& other) {
            if (this != &other) {
                ptr = other.ptr;
                step = other.step;
                size = other.size;
                scope = other.scope;
                product = other.product;
                if (capsule != nullptr) {
                    capsule->decref();
                }
                capsule = other.capsule == nullptr ? nullptr : other.capsule->incref();
                count = other.count;
                reduced_product = other.reduced_product;
            }
            return *this;
        }

        constexpr array_iterator& operator=(array_iterator&& other) noexcept {
            if (this != &other) {
                ptr = other.ptr;
                step = other.step;
                size = other.size;
                scope = other.scope;
                product = other.product;
                if (capsule != nullptr) {
                    capsule->decref();
                }
                capsule = other.capsule;
                count = other.count;
                reduced_product = other.reduced_product;
                other.ptr = nullptr;
                other.step = 0;
                other.size = 0;
                other.scope = 0;
                other.product = 0;
                other.capsule = nullptr;
                other.count = 0;
                other.reduced_product = 0;
            }
            return *this;
        }

        constexpr ~array_iterator() {
            if (capsule != nullptr) {
                capsule->decref();
            }
        }

        [[nodiscard]] constexpr auto operator*() const {
            using view = value_type;
            view result;
            result.ptr = ptr;
            if (capsule == nullptr) {  // parent is 1D
                result.scope = 1;  // child is trivially contiguous
            } else if (scope + 1 == capsule->ndim) {  // child is 1D
                result.scope = step;  // maybe non-contiguous
            } else {  // child is multidimensional
                result.capsule = capsule->incref();
                result.scope = scope + 1;
            }
            result.product = reduced_product;
            return result;
        }

        [[nodiscard]] constexpr auto operator->() const {
            return impl::arrow{**this};
        }

        [[nodiscard]] constexpr auto operator[](difference_type n) const {
            using view = value_type;
            view result;
            result.ptr = ptr + n * step;
            if (capsule == nullptr) {  // parent is 1D
                result.scope = 1;  // child is trivially contiguous
            } else if (scope + 1 == capsule->ndim) {  // child is 1D
                result.scope = step;  // maybe non-contiguous
            } else {  // child is multidimensional
                result.capsule = capsule->incref();
                result.scope = scope + 1;
            }
            result.product = reduced_product;
            return result;
        }
    };

    /// TODO: index errors are defined after filling in int <-> str conversions.

    inline constexpr IndexError array_index_error(size_t i) noexcept;

    /* Indices are always computed in C (row-major) memory order for simplicity and
    compatibility with C++.  If an index is signed, then Python-style wraparound will
    be applied to handle negative values.  A bounds check is then applied as a debug
    assertion, which throws an `IndexError` if the index is out of bounds. */
    template <extent shape, extent strides, meta::unsigned_integer I, typename... Is>
    constexpr size_t array_index(const I& i, const Is&... is)
        noexcept (!DEBUG && meta::nothrow::explicitly_convertible_to<I, size_t>)
        requires (
            !shape.empty() &&
            !strides.empty() &&
            meta::explicitly_convertible_to<I, size_t>
        )
    {
        if constexpr (DEBUG) {
            if (i >= shape.data()[0]) {
                throw array_index_error(i);
            }
        }
        if constexpr (sizeof...(Is) == 0) {
            return size_t(i) * strides.data()[0];
        } else {
            return size_t(i) * strides.data()[0] + array_index<
                array_reduce<shape, strides>::shape(),
                array_reduce<shape, strides>::strides()
            >(is...);
        }
    }
    template <extent shape, extent strides, meta::signed_integer I, typename... Is>
    constexpr size_t array_index(const I& i, const Is&... is)
        noexcept (!DEBUG && meta::nothrow::explicitly_convertible_to<I, ssize_t>)
        requires (
            !shape.empty() &&
            !strides.empty() &&
            meta::explicitly_convertible_to<I, ssize_t>
        )
    {
        ssize_t j = ssize_t(i) + ssize_t(shape.data()[0]) * (i < 0);
        if constexpr (DEBUG) {
            if (j < 0 || j > ssize_t(shape.data()[0])) {
                throw array_index_error(i);
            }
        }
        if constexpr (sizeof...(Is) == 0) {
            return size_t(j) * strides.data()[0];
        } else {
            return size_t(j) * strides.data()[0] + array_index<
                array_reduce<shape, strides>::shape(),
                array_reduce<shape, strides>::strides()
            >(is...);
        }
    }

    /* Tuple access for array types always takes signed integers and applies both
    wraparound and bounds-checking at compile time. */
    template <ssize_t... I>
    struct valid_array_index {
        template <extent shape, extent strides>
        static constexpr bool value = true;
        template <extent shape, extent strides>
        static constexpr size_t index = 0;
    };
    template <ssize_t I, ssize_t... Is>
    struct valid_array_index<I, Is...> {
        template <extent shape, extent strides>
        static constexpr bool value = false;
        template <extent shape, extent strides>
            requires (!shape.empty() && impl::valid_index<shape[0], I>)
        static constexpr bool value<shape, strides> = valid_array_index<Is...>::template value<
            array_reduce<shape, strides>::shape(),
            array_reduce<shape, strides>::strides()
        >;

        template <extent shape, extent strides>
        static constexpr size_t index =
            array_index<shape, strides>(I) +
            valid_array_index<Is...>::template index<
                array_reduce<shape, strides>::shape(),
                array_reduce<shape, strides>::strides()
            >;
    };

    /* Squeezing an array removes all singleton dimensions unless that is the only
    remaining dimension. */
    template <typename T, extent shape, extent strides>
    struct _array_squeeze {
        template <extent new_shape, extent new_strides>
        using array = Array<T, new_shape, {.strides = new_strides}>;
        template <extent new_shape, extent new_strides>
        using view = ArrayView<T, new_shape, {.strides = new_strides}>;
    };
    template <typename T, extent shape, extent strides>
        requires (!shape.empty() && shape[0] > 1)
    struct _array_squeeze<T, shape, strides> {
        template <extent new_shape, extent new_strides>
        using array = _array_squeeze<
            T,
            array_reduce<shape, strides>::shape(),
            array_reduce<shape, strides>::strides()
        >::template array<new_shape | shape[0], new_strides | strides[0]>;

        template <extent new_shape, extent new_strides>
        using view = _array_squeeze<
            T,
            array_reduce<shape, strides>::shape(),
            array_reduce<shape, strides>::strides()
        >::template view<new_shape | shape[0], new_strides | strides[0]>;
    };
    template <typename T, extent shape, extent strides>
        requires (!shape.empty() && shape[0] == 1)
    struct _array_squeeze<T, shape, strides> {
        template <extent new_shape, extent new_strides>
        struct type {
            using array = _array_squeeze<
                T,
                array_reduce<shape, strides>::shape(),
                array_reduce<shape, strides>::strides()
            >::template array<new_shape, new_strides>;
            using view = _array_squeeze<
                T,
                array_reduce<shape, strides>::shape(),
                array_reduce<shape, strides>::strides()
            >::template view<new_shape, new_strides>;
        };
        template <extent new_shape, extent new_strides>
            requires (shape.size() == 1 && new_strides.empty())
        struct type<new_shape, new_strides> {
            using array = Array<T, 1>;
            using view = ArrayView<T, 1>;
        };
        template <extent new_shape, extent new_strides>
        using array = type<new_shape, new_strides>::array;
        template <extent new_shape, extent new_strides>
        using view = type<new_shape, new_strides>::view;
    };
    template <typename T, extent shape, extent strides>
    using array_squeeze = _array_squeeze<T, shape, strides>;

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





    template <meta::has_data T>
    using array_data = meta::remove_reference<
        decltype(*std::ranges::data(std::declval<meta::as_lvalue<T>>()))
    >;

    template <typename... Ts>
    using array_common = meta::remove_reference<meta::common_type<Ts...>>;

}


/* A non-owning view into a contiguous memory buffer that interprets it as a
multidimensional array with a fixed shape.

Array views consist of a raw pointer to the start of the data buffer, along with a
shape and strides describing its length and step size in each dimension, both of which
may be specified at compile time.  If the shape is not empty and all of its dimensions
are greater than zero, then the resulting view will be statically-sized, and will only
store the data pointer at run time.  Indexing into or iterating over such a view will
yield either scalars or another statically-sized view of reduced dimension, giving
optimal performance and parity with `Array`.  Reshaping a statically-sized view is
also trivial, consisting of a pointer copy with the new shape and/or strides being
computed entirely at compile time, with strong safety guarantees.  This type of view
should be used as often as possible where performance and memory efficiency matter.

If the shape of a view is not empty, but contains one or more zeros, then it indicates
a view where the number of dimensions is known statically at compile time, but not
their precise lengths.  This is particularly relevant for 1D sequences of unknown
length (which describes most iterables), but may be generalized to higher-dimensional
sequences as well, including those where some dimensions are static and others are
dynamic.  In this case, the shape and stride buffers must be stored within the view at
run time, and must be initialized using constructor arguments.  Because the number of
dimensions is encoded at compile time, these buffers can be statically sized and
inlined into the view itself, avoiding any allocations or indirections.  Indexing into
or iterating over such a view can also yield either scalars or another view of
reduced dimension, just with slightly higher overhead due to the need to copy the
shape and stride buffers and reduced optimization opportunities due to a lack of
compiler knowledge.  Reshaping a view of this type is still relatively efficient, but
sacrifices some of the type safety guarantees of the statically-sized case, since a
`TypeError` may be raised if the new shape is incompatible with the old shape.

Finally, empty shapes are also supported, which represent fully dynamic array views of
unknown rank and shape.  This is similar to the second case, except that the shape
and stride buffers must be dynamically allocated, and the number of dimensions cannot
be known at compile time.  This effectively represents a form of type erasure, where
the shape information is only exposed at run time, meaning that arbitrary sequences of
such views may have different ranks and shapes without violating the type system.  The
extra allocation means that such views are more expensive to create, although still
reasonable due to atomic reference counting of the buffer object, dynamic scoping
during iteration and indexing (instead of nested allocations), and small-buffer
optimizations for 1D views, which never need to allocate.  The downside is that
indexing into or iterating over such a view must always yield another dynamic view
(possibly of length 1), which is a direct consequence of erasing the dimensionality of
the underlying data.  Reshaping such a view is also more expensive, possibly requiring
a new shape to be allocated, and a `TypeError` may be raised if the new shape is
incompatible with the old shape.

Note that in all cases, the data buffer is not owned by the view, and it is the user's
responsibility to ensure that it remains valid for the full lifetime of the view.
Copying and moving the view is always efficient, therefore, and it is common practice
to pass them by value.  Additionally, this class makes no assumptions about the origin
of the buffer, meaning that it can be used as a building block for higher-level data
structures that wish to expose an efficient, array-like interface, such as `Arena`,
`List`, and arbitrary user-defined types. */
template <typename T, impl::extent Shape, impl::array_flags<Shape> Flags>
    requires (impl::array_view_concept<T, Shape, Flags>)
struct ArrayView : impl::array_view_tag {
private:
    static constexpr size_t _total = Shape.product();
    static constexpr size_t step = Flags.strides[0];

public:
    using size_type = size_t;
    using index_type = ssize_t;
    using value_type = impl::array_value<T, Shape, Flags.strides>;
    using reference = meta::as_lvalue<value_type>;
    using const_reference = meta::as_const<reference>;
    using pointer = meta::as_pointer<reference>;
    using const_pointer = meta::as_pointer<const_reference>;
    using iterator = impl::array_iterator<T, Shape, Flags.strides>;
    using const_iterator = impl::array_iterator<meta::as_const<T>, Shape, Flags.strides>;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    [[nodiscard]] static constexpr bool dynamic() noexcept { return false; }
    [[nodiscard]] static constexpr bool opaque() noexcept { return false; }
    [[nodiscard]] static constexpr size_t itemsize() noexcept { return sizeof(T); }
    [[nodiscard]] static constexpr size_type ndim() noexcept { return Shape.size(); }
    [[nodiscard]] static constexpr const auto& shape() noexcept { return Shape; }
    [[nodiscard]] static constexpr const auto& strides() noexcept { return Flags.strides; }
    [[nodiscard]] static constexpr size_type total() noexcept { return _total; }
    [[nodiscard]] static constexpr size_type size() noexcept { return Shape.data()[0]; }
    [[nodiscard]] static constexpr index_type ssize() noexcept { return index_type(size()); }
    [[nodiscard]] static constexpr bool empty() noexcept { return size() == 0; }

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

    template <meta::integer... I> requires (sizeof...(I) <= ndim())
    [[nodiscard]] constexpr auto data(const I&... i) noexcept {
        if constexpr (sizeof...(I) == 0) {
            if constexpr (impl::is_array_storage<T>) {
                return std::addressof(ptr->value);
            } else {
                return ptr;
            }
        } else {
            size_type j = impl::array_index<shape(), strides()>(i...);
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
            size_type j = impl::array_index<shape(), strides()>(i...);
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
        using view = impl::array_squeeze<T, shape(), strides()>::template view<{}, {}>;
        return view{ptr};
    }

    [[nodiscard]] constexpr auto squeeze() const noexcept {
        using view = impl::array_squeeze<meta::as_const<T>, shape(), strides()>::template view<{}, {}>;
        return view{static_cast<meta::as_const<T>*>(ptr)};
    }

    /// TODO: transpose()

    [[nodiscard]] constexpr auto& operator*() noexcept {
        return impl::array_access(ptr);
    }

    [[nodiscard]] constexpr const auto& operator*() const noexcept {
        return impl::array_access(static_cast<meta::as_const<T>*>(ptr));
    }

    [[nodiscard]] constexpr auto* operator->() noexcept {
        return std::addressof(impl::array_access(ptr));
    }

    [[nodiscard]] constexpr const auto* operator->() const noexcept {
        return std::addressof(impl::array_access(static_cast<meta::as_const<T>*>(ptr)));
    }

    [[nodiscard]] constexpr decltype(auto) front() noexcept {
        if constexpr (ndim() == 1) {
            return (impl::array_access(ptr));
        } else {
            using view = impl::array_reduce<shape(), strides()>::template view<T>;
            return view{ptr};
        }
    }

    [[nodiscard]] constexpr decltype(auto) front() const noexcept {
        if constexpr (ndim() == 1) {
            return (impl::array_access(static_cast<meta::as_const<T>*>(ptr)));
        } else {
            using view = impl::array_reduce<shape(), strides()>::template view<meta::as_const<T>>;
            return view{static_cast<meta::as_const<T>*>(ptr)};
        }
    }

    [[nodiscard]] constexpr decltype(auto) back() noexcept {
        if constexpr (ndim() == 1) {
            return (impl::array_access(ptr, (size() - 1) * step));
        } else {
            using view = value_type;
            return view{ptr + (size() - 1) * step};
        }
    }

    [[nodiscard]] constexpr decltype(auto) back() const noexcept {
        if constexpr (ndim() == 1) {
            return (impl::array_access(static_cast<meta::as_const<T>*>(ptr), (size() - 1) * step));
        } else {
            using view = impl::array_reduce<shape(), strides()>::template view<meta::as_const<T>>;
            return view{static_cast<meta::as_const<T>*>(ptr) + (size() - 1) * step};
        }
    }

    template <index_type... I>
        requires (
            sizeof...(I) <= ndim() &&
            impl::valid_array_index<I...>::template value<shape(), strides()>
        )
    [[nodiscard]] constexpr decltype(auto) get() noexcept {
        constexpr size_type j = impl::valid_array_index<I...>::template index<shape(), strides()>;
        if constexpr (sizeof...(I) == ndim()) {
            return (impl::array_access(ptr, j));
        } else {
            using view = impl::array_reduce<shape(), strides(), sizeof...(I)>::template view<T>;
            return view{ptr + j};
        }
    }

    template <index_type... I>
        requires (
            sizeof...(I) <= ndim() &&
            impl::valid_array_index<I...>::template value<shape(), strides()>
        )
    [[nodiscard]] constexpr decltype(auto) get() const noexcept {
        constexpr size_type j = impl::valid_array_index<I...>::template index<shape(), strides()>;
        if constexpr (sizeof...(I) == ndim()) {
            return (impl::array_access(static_cast<meta::as_const<T>*>(ptr), j));
        } else {
            using view = impl::array_reduce<shape(), strides(), sizeof...(I)>::template view<
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
        size_type j = impl::array_index<shape(), strides()>(i...);
        if constexpr (sizeof...(I) == ndim()) {
            return (impl::array_access(ptr, j));
        } else {
            using view = impl::array_reduce<shape(), strides(), sizeof...(I)>::template view<T>;
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
        size_type j = impl::array_index<shape(), strides()>(i...);
        if constexpr (sizeof...(I) == ndim()) {
            return (impl::array_access(static_cast<meta::as_const<T>*>(ptr), j));
        } else {
            using view = impl::array_reduce<shape(), strides(), sizeof...(I)>::template view<
                meta::as_const<T>
            >;
            return view{static_cast<meta::as_const<T>*>(ptr) + j};
        }
    }

    template <typename U>
    [[nodiscard]] constexpr operator Array<U, shape(), {.strides = strides()}>() const
        noexcept (meta::nothrow::convertible_to<const_reference, U>)
        requires (meta::convertible_to<const_reference, U>)
    {
        auto result = Array<U, shape(), {.strides = strides()}>::reserve();
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
    template <typename U, impl::extent S, impl::array_flags<S> F> requires (S.product() == total())
    [[nodiscard]] constexpr bool operator==(const Array<U, S, F>& other) const
        noexcept (
            meta::nothrow::has_eq<const_reference, typename Array<U, S, F>::const_reference> &&
            meta::nothrow::truthy<meta::eq_type<
                const_reference,
                typename Array<U, S, F>::const_reference
            >>
        )
        requires (
            meta::has_eq<const_reference, typename Array<U, S, F>::const_reference> &&
            meta::truthy<meta::eq_type<const_reference, typename Array<U, S, F>::const_reference>>
        )
    {
        auto it1 = flatten().begin();
        auto end1 = flatten().end();
        auto it2 = other.flatten().begin();
        auto end2 = other.flatten().end();
        while (it1 != end1 && it2 != end2) {
            if (!bool(*it1 == *it2)) {
                return false;
            }
            ++it1;
            ++it2;
        }
        return true;
    }

    /* Compare two arrays of the same shape for lexicographic equality.  Iteration
    always begins at index 0 across all dimensions and advances the last index first,
    following row-major order. */
    template <typename U, impl::extent S, impl::array_flags<S> F> requires (S.product() == total())
    [[nodiscard]] constexpr bool operator==(const ArrayView<U, S, F>& other) const
        noexcept (
            meta::nothrow::has_eq<const_reference, typename ArrayView<U, S, F>::const_reference> &&
            meta::nothrow::truthy<meta::eq_type<
                const_reference,
                typename ArrayView<U, S, F>::const_reference
            >>
        )
        requires (
            meta::has_eq<const_reference, typename ArrayView<U, S, F>::const_reference> &&
            meta::truthy<meta::eq_type<
                const_reference,
                typename ArrayView<U, S, F>::const_reference
            >>
        )
    {
        auto it1 = flatten().begin();
        auto end1 = flatten().end();
        auto it2 = other.flatten().begin();
        auto end2 = other.flatten().end();
        while (it1 != end1 && it2 != end2) {
            if (!bool(*it1 == *it2)) {
                return false;
            }
            ++it1;
            ++it2;
        }
        return true;
    }

    /* Compare two arrays of the same shape for lexicographic equality.  Iteration
    always begins at index 0 across all dimensions and advances the last index first,
    following row-major order. */
    template <typename U, impl::extent S, impl::array_flags<S> F> requires (S.product() == total())
    [[nodiscard]] constexpr auto operator<=>(const Array<U, S, F>& other) const
        noexcept (
            meta::nothrow::has_lt<const_reference, typename Array<U, S, F>::const_reference> &&
            meta::nothrow::has_lt<typename Array<U, S, F>::const_reference, const_reference> &&
            meta::nothrow::truthy<meta::lt_type<
                const_reference,
                typename Array<U, S, F>::const_reference
            >> &&
            meta::nothrow::truthy<meta::lt_type<
                typename Array<U, S, F>::const_reference,
                const_reference
            >>
        )
        requires (
            meta::has_lt<const_reference, typename Array<U, S, F>::const_reference> &&
            meta::has_lt<typename Array<U, S, F>::const_reference, const_reference> &&
            meta::truthy<meta::lt_type<
                const_reference,
                typename Array<U, S, F>::const_reference
            >> &&
            meta::truthy<meta::lt_type<
                typename Array<U, S, F>::const_reference,
                const_reference
            >>
        )
    {
        auto it1 = flatten().begin();
        auto end1 = flatten().end();
        auto it2 = other.flatten().begin();
        auto end2 = other.flatten().end();
        while (it1 != end1 && it2 != end2) {
            if (bool(*it1 < *it2)) {
                return std::strong_ordering::less;
            } else if (bool(*it2 < *it1)) {
                return std::strong_ordering::greater;
            }
            ++it1;
            ++it2;
        }
        return std::strong_ordering::equal;
    }

    /* Compare two arrays of the same shape for lexicographic equality.  Iteration
    always begins at index 0 across all dimensions and advances the last index first,
    following row-major order. */
    template <typename U, impl::extent S, impl::array_flags<S> F> requires (S.product() == total())
    [[nodiscard]] constexpr auto operator<=>(const ArrayView<U, S, F>& other) const
        noexcept (
            meta::nothrow::has_lt<const_reference, typename ArrayView<U, S, F>::const_reference> &&
            meta::nothrow::has_lt<typename ArrayView<U, S, F>::const_reference, const_reference> &&
            meta::nothrow::truthy<meta::lt_type<
                const_reference,
                typename ArrayView<U, S, F>::const_reference
            >> &&
            meta::nothrow::truthy<meta::lt_type<
                typename ArrayView<U, S, F>::const_reference,
                const_reference
            >>
        )
        requires (
            meta::has_lt<const_reference, typename ArrayView<U, S, F>::const_reference> &&
            meta::has_lt<typename ArrayView<U, S, F>::const_reference, const_reference> &&
            meta::truthy<meta::lt_type<
                const_reference,
                typename ArrayView<U, S, F>::const_reference
            >> &&
            meta::truthy<meta::lt_type<
                typename ArrayView<U, S, F>::const_reference,
                const_reference
            >>
        )
    {
        auto it1 = flatten().begin();
        auto end1 = flatten().end();
        auto it2 = other.flatten().begin();
        auto end2 = other.flatten().end();
        while (it1 != end1 && it2 != end2) {
            if (bool(*it1 < *it2)) {
                return std::strong_ordering::less;
            } else if (bool(*it2 < *it1)) {
                return std::strong_ordering::greater;
            }
            ++it1;
            ++it2;
        }
        return std::strong_ordering::equal;
    }
};


/* A specialization of `ArrayView` that represents a dynamically-sized view with a
fixed number of dimensions.  This specialization is chosen when the shape is not empty,
but one or more of its dimensions are zero, indicating a dynamic length.

See the primary `ArrayView` documentation for more details on the behavior of this
type of view. */
template <typename T, impl::extent Shape, impl::array_flags<Shape> Flags>
    requires (impl::array_view_concept<T, Shape, Flags> && !Shape.empty() && Shape.product() == 0)
struct ArrayView<T, Shape, Flags> : impl::array_view_tag {
private:


public:
    using size_type = size_t;
    using index_type = ssize_t;
    using value_type = impl::array_value<T, Shape, Flags.strides>;
    using reference = meta::as_lvalue<value_type>;
    using const_reference = meta::as_const<reference>;
    using pointer = meta::as_pointer<reference>;
    using const_pointer = meta::as_pointer<const_reference>;
    using iterator = impl::array_iterator<T, Shape, Flags.strides>;
    using const_iterator = impl::array_iterator<meta::as_const<T>, Shape, Flags.strides>;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    [[nodiscard]] static constexpr bool dynamic() noexcept { return true; }
    [[nodiscard]] static constexpr bool opaque() noexcept { return false; }
    [[nodiscard]] static constexpr size_t itemsize() noexcept { return sizeof(T); }
    [[nodiscard]] static constexpr size_type ndim() noexcept { return Shape.size(); }

    T* ptr = nullptr;
    impl::extent<ndim()> __shape;
    impl::extent<ndim()> __strides;
    size_t __total = 0;

    [[nodiscard]] constexpr const auto& shape() const noexcept { return __shape; }
    [[nodiscard]] constexpr const auto& strides() const noexcept { return __strides; }
    [[nodiscard]] constexpr size_type total() const noexcept { return __total; }
    [[nodiscard]] constexpr size_type size() const noexcept { return __shape.data()[0]; }
    [[nodiscard]] constexpr index_type ssize() const noexcept { return index_type(size()); }
    [[nodiscard]] constexpr bool empty() const noexcept { return __total == 0; }

    [[nodiscard]] constexpr ArrayView() noexcept = default;


    [[nodiscard]] constexpr auto& operator*() noexcept {
        return impl::array_access(ptr);
    }

    [[nodiscard]] constexpr const auto& operator*() const noexcept {
        return impl::array_access(static_cast<meta::as_const<T>*>(ptr));
    }

    [[nodiscard]] constexpr auto* operator->() noexcept {
        return std::addressof(impl::array_access(ptr));
    }

    [[nodiscard]] constexpr const auto* operator->() const noexcept {
        return std::addressof(impl::array_access(static_cast<meta::as_const<T>*>(ptr)));
    }

};


/* A specialization of `ArrayView` that represents a fully dynamic view of unknown
shape.  This specialization is chosen when the templated shape is empty, meaning that
it must be provided at run time.

See the primary `ArrayView` documentation for more details on the behavior of this
type of view. */
template <typename T, impl::extent Shape, impl::array_flags<Shape> Flags>
    requires (impl::array_view_concept<T, Shape, Flags> && Shape.empty())
struct ArrayView<T, Shape, Flags> : impl::array_view_tag {
private:

    template <typename S>
    constexpr void from_iterable(S& shape) {
        size_t ndim;
        if constexpr (meta::tuple_like<T>) {
            ndim = meta::tuple_size<T>;
        } else {
            ndim = shape.size();
        }
        if (ndim == 0) {
            throw ValueError("shape must not be empty");
        } else if (ndim == 1) {
            product = *shape.begin();
        } else {
            capsule = impl::heap_extent::create(ndim);
            product = 1;
            size_t i = 0;
            for (auto&& x : shape) {
                std::construct_at(
                    capsule->buffer + i,
                    std::forward<decltype(x)>(x)
                );
                product *= capsule->buffer[i];
                ++i;
            }
        }
    }

    constexpr void _from_tuple(size_t& i, size_t n) noexcept {
        std::construct_at(capsule->buffer + i, n);
        product *= capsule->buffer[i];
        ++i;
    }

    template <meta::tuple_like S, size_t... Is> requires (sizeof...(Is) == meta::tuple_size<S>)
    constexpr void from_tuple(S&& shape, std::index_sequence<Is...>)
        noexcept (requires(size_t i) {
            {(_from_tuple(i,meta::get<Is>(std::forward<S>(shape))), ...)} noexcept;
        })
    {
        constexpr size_t ndim = meta::tuple_size<S>;
        if constexpr (ndim == 1) {
            product = *shape.begin();
        } else {
            capsule = impl::heap_extent::create(ndim);
            product = 1;
            size_t i = 0;
            (_from_tuple(i, meta::get<Is>(std::forward<T>(shape))), ...);
        }
    }

    constexpr void set_strides(impl::array_flags<>& flags) {
        if (capsule == nullptr) {
            if (flags.strides.ndim == 0) {
                scope = 1;
            } else if (flags.strides.kind == flags.strides.TRIVIAL) {
                scope = *flags.strides.data();
            } else {
                throw ValueError("strides must be the same length as the shape");
            }
        } else if (flags.strides.ndim == 0) {
            if (flags.column_major) {
                size_t* stride = capsule->buffer + capsule->ndim;
                std::construct_at(stride, 1);
                for (size_t i = 1; i < capsule->ndim; ++i) {
                    std::construct_at(
                        stride + i,
                        stride[i - 1] * capsule->buffer[i - 1]
                    );
                }
            } else {
                size_t* stride = capsule->buffer + capsule->ndim;
                size_t i = capsule->ndim - 1;
                std::construct_at(stride + i, 1);
                while (i-- > 0) {
                    std::construct_at(
                        stride + i,
                        stride[i + 1] * capsule->buffer[i + 1]
                    );
                }
            }
        } else {
            if (flags.strides.size() != capsule->ndim) {
                throw ValueError("strides must be the same length as the shape");
            }
            auto data = flags.strides.data();
            size_t* stride = capsule->buffer + capsule->ndim;
            for (size_t i = 0; i < capsule->ndim; ++i) {
                std::construct_at(stride + i, data[i]);
            }
        }
    }

public:
    using size_type = size_t;
    using index_type = ssize_t;
    using value_type = impl::array_value<T, {}, {}>;
    using reference = meta::as_lvalue<value_type>;
    using const_reference = meta::as_const<reference>;
    using pointer = meta::as_pointer<reference>;
    using const_pointer = meta::as_pointer<const_reference>;
    using iterator = impl::array_iterator<T, {}, {}>;
    using const_iterator = impl::array_iterator<meta::as_const<T>, {}, {}>;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    [[nodiscard]] static constexpr bool dynamic() noexcept { return true; }
    [[nodiscard]] static constexpr bool opaque() noexcept { return true; }
    [[nodiscard]] static constexpr size_type itemsize() noexcept { return sizeof(T); }

    T* ptr = nullptr;
    impl::heap_extent* capsule = nullptr;
    size_t scope = 0;  // if 1D: stride, else: current nesting level for iteration/indexing
    size_t product = 0;  // if 1D: shape, else: total number of elements (product of shape)

    [[nodiscard]] constexpr size_type ndim() const noexcept {
        return capsule == nullptr ? product > 0 : capsule->ndim;
    }

    [[nodiscard]] constexpr auto shape() const noexcept {
        if (capsule == nullptr) {
            return impl::extent<0>(&product, 1);
        }
        return impl::extent<0>(capsule->buffer, capsule->ndim);
    }

    [[nodiscard]] constexpr auto strides() const noexcept {
        if (capsule == nullptr) {
            return impl::extent<0>(&scope, 1);
        }
        return impl::extent<0>(capsule->buffer + capsule->ndim, capsule->ndim);
    }

    [[nodiscard]] constexpr size_type total() const noexcept {
        return product;
    }

    [[nodiscard]] constexpr size_type size() const noexcept {
        return capsule == nullptr ? product : capsule->buffer[0];
    }

    [[nodiscard]] constexpr index_type ssize() const noexcept {
        return index_type(size());
    }

    [[nodiscard]] constexpr bool empty() const noexcept {
        return product == 0;
    }

    [[nodiscard]] constexpr ArrayView() noexcept = default;
    [[nodiscard]] constexpr ArrayView(
        T* p,
        size_t shape,
        impl::array_flags<> flags = {}
    ) :
        ptr(p), product(shape)
    {
        if (flags.strides.ndim == 0) {
            scope = 1;
        } else if (flags.strides.kind == flags.strides.TRIVIAL) {
            scope = *flags.strides.data();
        } else {
            throw ValueError("strides must be the same length as the shape");
        }
    }

    [[nodiscard]] constexpr ArrayView(
        T* p,
        std::initializer_list<size_t> shape,
        impl::array_flags<> flags = {}
    )
        requires (!meta::convertible_to<T, size_t>)
    :
        ptr(p)
    {
        from_iterable(shape);
        set_strides(flags);
    }

    template <typename S>
    [[nodiscard]] constexpr ArrayView(T* p, S&& shape, impl::array_flags<> flags = {})
        requires (
            !meta::convertible_to<T, size_t> &&
            meta::yields<size_t, T> &&
            (meta::tuple_like<T> || meta::size_returns<size_t, T>)
        )
    :
        ptr(p)
    {
        from_iterable(shape);
        set_strides(flags);
    }

    template <typename S>
    [[nodiscard]] constexpr ArrayView(T* p, S&& shape, impl::array_flags<> flags = {})
        noexcept (meta::tuple_types<T>::template nothrow_convertible_to<size_t>)
        requires (
            !meta::convertible_to<T, size_t> &&
            !meta::yields<size_t, T> &&
            meta::tuple_like<T> &&
            meta::tuple_size<T> > 0 &&
            meta::tuple_types<T>::template convertible_to<size_t>
        )
    :
        ptr(p)
    {
        from_tuple(std::forward<S>(shape), std::make_index_sequence<meta::tuple_size<S>>{});
        set_strides(flags);
    }

    [[nodiscard]] constexpr ArrayView(const ArrayView& other) :
        capsule(other.capsule == nullptr ? nullptr : other.capsule->incref()),
        scope(other.scope),
        product(other.product)
    {}

    [[nodiscard]] constexpr ArrayView(ArrayView&& other) noexcept :
        capsule(other.capsule),
        scope(other.scope),
        product(other.product)
    {
        other.capsule = nullptr;
        other.scope = 0;
        other.product = 0;
    }

    constexpr ArrayView& operator=(const ArrayView& other) {
        if (this != &other) {
            if (capsule) {
                capsule->decref();
            }
            capsule = other.capsule == nullptr ? nullptr : other.capsule->incref();
            scope = other.scope;
            product = other.total;
        }
        return *this;
    }

    constexpr ArrayView& operator=(ArrayView&& other) noexcept {
        if (this != &other) {
            if (capsule) {
                capsule->decref();
            }
            capsule = other.capsule;
            scope = other.scope;
            product = other.product;
            other.capsule = nullptr;
            other.scope = 0;
            other.product = 0;
        }
        return *this;
    }

    constexpr ~ArrayView() {
        if (capsule) {
            capsule->decref();
        }
    }

    constexpr void swap(ArrayView& other) noexcept {
        std::swap(capsule, other.capsule);
        std::swap(scope, other.scope);
        std::swap(product, other.product);
    }

    // template <meta::integer... I> requires 
    // [[nodiscard]] constexpr auto data(const I&... i) noexcept {
    //     if constexpr (sizeof...(I) == 0) {
    //         if constexpr (impl::is_array_storage<T>) {
    //             return std::addressof(ptr->value);
    //         } else {
    //             return ptr;
    //         }
    //     } else {
    //         /// TODO: array_index() would need to be updated to take a runtime shape
    //         /// instead of a compile-time one, and possibly throw an error if too
    //         /// many indices are given.  It should also only require the strides, not
    //         /// the full `flags`.
    //         size_type j = impl::array_index<Shape, Flags.strides>(i...);
    //         if constexpr (impl::is_array_storage<T>) {
    //             return std::addressof(ptr[j].value);
    //         } else {
    //             return ptr + j;
    //         }
    //     }
    // }

    /// TODO: same concerns as above

    // template <meta::integer... I> requires (sizeof...(I) <= ndim())
    // [[nodiscard]] constexpr auto data(const I&... i) const noexcept {
    //     if constexpr (sizeof...(I) == 0) {
    //         if constexpr (impl::is_array_storage<T>) {
    //             return std::addressof(static_cast<meta::as_const<T>*>(ptr)->value);
    //         } else {
    //             return static_cast<meta::as_const<T>*>(ptr);
    //         }
    //     } else {
    //         size_type j = impl::array_index<Shape, Flags.strides>(i...);
    //         if constexpr (impl::is_array_storage<T>) {
    //             return std::addressof(static_cast<meta::as_const<T>*>(ptr)[j].value);
    //         } else {
    //             return static_cast<meta::as_const<T>*>(ptr) + j;
    //         }
    //     }
    // }

    [[nodiscard]] constexpr iterator begin() noexcept {
        if (capsule == nullptr) {
            return {ptr, scope, product};
        } else {
            return {
                ptr,
                capsule->buffer[capsule->ndim + scope],
                capsule->buffer[scope],
                scope,
                product,
                capsule->incref()
            };
        }
    }

    [[nodiscard]] constexpr const_iterator begin() const noexcept {
        if (capsule == nullptr) {
            return {static_cast<meta::as_const<T>*>(ptr), scope, product};
        } else {
            return {
                static_cast<meta::as_const<T>*>(ptr),
                capsule->buffer[capsule->ndim + scope],
                capsule->buffer[scope],
                scope,
                product,
                capsule->incref()
            };
        }
    }

    [[nodiscard]] constexpr const_iterator cbegin() const noexcept {
        return begin();
    }

    [[nodiscard]] constexpr iterator end() noexcept {
        if consteval {
            if (capsule == nullptr) {
                return {
                    ptr + (product - 1) * scope,
                    scope,
                    product,
                    scope,
                    product,
                    nullptr,
                    product
                };
            } else {
                return {
                    ptr + (capsule->buffer[scope] - 1) * capsule->buffer[capsule->ndim + scope],
                    capsule->buffer[capsule->ndim + scope],
                    capsule->buffer[scope],
                    scope,
                    product,
                    capsule->incref(),
                    capsule->buffer[scope]
                };
            }
        } else {
            if (capsule == nullptr) {
                return {ptr + product * scope, scope, product};
            } else {
                return {
                    ptr + capsule->buffer[scope] * capsule->buffer[capsule->ndim + scope],
                    capsule->buffer[capsule->ndim + scope],
                    capsule->buffer[scope],
                    scope,
                    product,
                    capsule->incref()
                };
            }
        }
    }

    [[nodiscard]] constexpr const_iterator end() const noexcept {
        if consteval {
            if (capsule == nullptr) {
                return {
                    static_cast<meta::as_const<T>>(ptr) + (product - 1) * scope,
                    scope,
                    product,
                    scope,
                    product,
                    nullptr,
                    product,
                };
            } else {
                return {
                    (
                        static_cast<meta::as_const<T>>(ptr) +
                        (capsule->buffer[scope] - 1) * capsule->buffer[capsule->ndim + scope]
                    ),
                    capsule->buffer[capsule->ndim + scope],
                    capsule->buffer[scope],
                    scope,
                    product,
                    capsule->incref(),
                    capsule->buffer[scope],
                };
            }
        } else {
            if (capsule == nullptr) {
                return {static_cast<meta::as_const<T>>(ptr) + product * scope, scope, product};
            } else {
                return {
                    (
                        static_cast<meta::as_const<T>>(ptr) +
                        capsule->buffer[scope] * capsule->buffer[capsule->ndim + scope]
                    ),
                    capsule->buffer[capsule->ndim + scope],
                    capsule->buffer[scope],
                    scope,
                    product,
                    capsule->incref(),
                };
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

    /// TODO: views like flatten(), reshape(), squeeze(), and transpose() are
    /// intimately tied to the constructors and scoping logic, so they will need a
    /// fair amount of thought as well.


    [[nodiscard]] constexpr auto& operator*() noexcept {
        return impl::array_access(ptr);
    }

    [[nodiscard]] constexpr const auto& operator*() const noexcept {
        return impl::array_access(static_cast<meta::as_const<T>*>(ptr));
    }

    [[nodiscard]] constexpr auto* operator->() noexcept {
        return std::addressof(impl::array_access(ptr));
    }

    [[nodiscard]] constexpr const auto* operator->() const noexcept {
        return std::addressof(impl::array_access(static_cast<meta::as_const<T>*>(ptr)));
    }

    /// TODO: front(), back(), operator[] require scoping rules for dynamic shapes

    /// TODO: lexicographic comparison operators require flatten() and iterators,
    /// plus some extra logic to access the underlying items via the dereference
    /// operator, since dynamic views yield further dynamic views when iterated.

};


template <typename T, impl::extent shape, impl::array_flags<shape> flags>
ArrayView(Array<T, shape, flags>&) -> ArrayView<impl::array_storage<T>, shape, flags>;


template <typename T, impl::extent shape, impl::array_flags<shape> flags>
ArrayView(const Array<T, shape, flags>&) -> ArrayView<const impl::array_storage<T>, shape, flags>;


template <typename T, size_t N>
ArrayView(T(&)[N]) -> ArrayView<T, N>;


template <meta::has_data T> requires (!meta::Array<T> && meta::has_static_shape<T>)
ArrayView(T&) -> ArrayView<impl::array_data<T>, meta::static_shape<T>()>;


template <meta::has_data T>
    requires (
        !meta::Array<T> &&
        !meta::has_static_shape<T> &&
        (meta::has_shape<T> || meta::has_size<T>)
    )
ArrayView(T&) -> ArrayView<impl::array_data<T>>;


/// TODO: providing more than one argument will probably create a view with dynamic
/// shape




/* A generalized, multidimensional array of a given shape.

The shape of an array is defined at compile time via the `impl::extent` template
parameter, which can be brace-initialized directly within the template signature or
implicitly converted from a non-negative integer or any iterable or tuple-like sequence
that yields them.  Custom strides and flags may also be specified via the
`impl::array_flags` template parameter, which can also be brace-initialized using
C-style designated initializers for clarity.  If no strides are given, then they will
be computed from the array's shape according to row-major order (if
`flags.column_major == false` - the default) or column-major order (if
`flags.column_major == true`).  Arrays cannot store references, but will preserve cv
qualifications if given.

The array elements are always stored in a contiguous block of memory with length equal
to the cartesian product of the shape dimensions, which must be non-zero.  This buffer
will be inlined into the surrounding context, placing it on the stack unless otherwise
specified.  The elements themselves will be stored as trivial unions, allowing them
to represent uninitialized memory via the `::reserve()` method, and allowing efficient
construction from iterables and tuple-like types, without invoking default
constructors.

Iterating over an array will yield a series of trivial views over sub-arrays of reduced
dimension (stripping axes from left to right), until a 1-D view is reached, which
yields the underlying elements.  Each view reduces to a simple pointer into the
flattened data buffer.  See the `ArrayView` class for more details.

Arrays also support multidimensional indexing, both at compile time via a tuple-like
`get<I, J, K, ...>()` method, and at run time via `array[i, j, k, ...]`.  Note that
neither operator depends on the `flags.column_major` setting, which only controls the
strides used for mapping the indices to the underlying data buffer.  Signed indices
will be interpreted using Python-style wraparound, meaning negative values will count
backwards from the end of the corresponding dimension.  In debug builds, all indices
will also be bounds-checked, throwing an `IndexError` if any are out of range after
normalization.  For unsigned indices in release builds, the index operator is always
zero-cost, just like built-in arrays.

If fewer indices are provided than the number of dimensions, then a view over the
corresponding sub-array will be returned instead of a scalar value, subject to the same
rules as iteration.  Additionally, arrays can be trivially flattened or reshaped by
returning a view of a different shape (of equal size), which is implemented via the
`flatten()`, `reshape<M...>()`, and `transpose()` methods, respectively, all of which
are zero-cost.  Note that transposing an array effectively reverses its shape and
strides, switching it from row-major to column-major order.

Lastly, both arrays and array views are mutually interconvertible, with conversion to a
view being zero-cost, and conversion back to an array incurring a copy of the
referenced data.  Both directions are also covered by CTAD guides, which allow the
shape and type to be inferred at compile time, like so:

    ```
    Array<int, 3, 2> arr {
        Array{1, 2},
        Array{3, 4},
        Array{5, 6}
    };

    ArrayView view = arr;  // array -> view
    Array copy = view;  // view -> array (copy)
    arr.flatten()[4] = 7;  // `flatten()` returns a 1D view
    assert(arr[2] == {7, 6});  // modification is reflected in original array
    assert(arr == view);  // view references mutated data
    assert(arr != copy);  // copy is unchanged
    ```
*/
template <typename T, impl::extent Shape, impl::array_flags<Shape> Flags>
    requires (impl::array_concept<T, Shape, Flags>)
struct Array : impl::array_tag {
private:
    using init = impl::array_init<T, Shape, Flags.strides>;
    using storage = impl::array_storage<T>;
    static constexpr size_t step = Flags.strides[0];

public:
    using type = T;
    using size_type = size_t;
    using index_type = ssize_t;
    using value_type = impl::array_value<T, Shape, Flags.strides>;
    using reference = meta::as_lvalue<value_type>;
    using const_reference = meta::as_const<reference>;
    using pointer = meta::as_pointer<reference>;
    using const_pointer = meta::as_pointer<const_reference>;
    using iterator = impl::array_iterator<storage, Shape, Flags.strides>;
    using const_iterator = impl::array_iterator<const storage, Shape, Flags.strides>;
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

    /* The number of dimensions for a multidimensional array.  This is always equal
    to the number of indices in its shape, and is never zero. */
    [[nodiscard]] static constexpr size_type ndim() noexcept {
        return Shape.size();
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
    to row-major order (if `flags.column_major == false`) or column-major order (if
    `flags.column_major == true`).  The return type is an `impl::extent` object that
    can be indexed, iterated over, and destructured like a `std::array`, but with
    Python-style wraparound. */
    [[nodiscard]] static constexpr const auto& strides() noexcept {
        return Flags.strides;
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
        return Shape.data()[0];
    }

    /* Equivalent to `size()`, but as a signed rather than unsigned integer. */
    [[nodiscard]] static constexpr index_type ssize() noexcept {
        return index_type(size());
    }

    /* True if the array has precisely zero elements.  This is always false for arrays
    due to restrictions in the template signature. */
    [[nodiscard]] static constexpr bool empty() noexcept {
        return false;
    }

    /* The raw data buffer backing the array.  Note that this is stored as a normalized
    `impl::array_storage` union that allows for trivial default construction, which is
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

    template <typename V> requires (ndim() == 1)
    constexpr void build(size_type& i, V&& v)
        noexcept (meta::nothrow::convertible_to<V, T>)
        requires (meta::convertible_to<V, T>)
    {
        std::construct_at(&ptr[i].value, std::forward<V>(v));
        ++i;
    }

    constexpr void build(size_type& i, const init& v)
        noexcept (meta::nothrow::copyable<impl::ref<T>>)
        requires (ndim() > 1 && meta::copyable<impl::ref<T>>)
    {
        for (size_type j = 0; j < step; ++j, ++i) {
            std::construct_at(&ptr[i].value, v.ptr[j].value);
        }
    }

    constexpr void build(size_type& i, init&& v)
        noexcept (meta::nothrow::movable<impl::ref<T>>)
        requires (ndim() > 1 && meta::movable<impl::ref<T>>)
    {
        for (size_type j = 0; j < step; ++j, ++i) {
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

    /// TODO: maybe these constructors are required to be explicit?


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
                return {ptr + total() - step, size()};
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
                return {ptr + total() - step, size()};
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
    [[nodiscard]] constexpr operator ArrayView<storage, shape(), {.strides = strides()}>()
        & noexcept
    {
        return {ptr};
    }

    /* Get a view over the array.  This backs by a CTAD guide to allow inference of
    type and shape. */
    [[nodiscard]] constexpr operator ArrayView<const storage, shape(), {.strides = strides()}>()
        const & noexcept
    {
        return {ptr};
    }

    /* Return a 1-dimensional view over the array, which equates to a flat view over
    the raw data buffer. */
    [[nodiscard]] constexpr auto flatten() & noexcept {
        using view = ArrayView<storage, total()>;
        return view{ptr};
    }

    /* Return a 1-dimensional view over the array, which equates to a flat view over
    the raw data buffer. */
    [[nodiscard]] constexpr auto flatten() const & noexcept {
        using view = ArrayView<const storage, total()>;
        return view{static_cast<const storage*>(ptr)};
    }

    /* Return a 1-dimensional version of the array, moving the current contents to a
    new array. */
    [[nodiscard]] constexpr Array<T, total()> flatten() &&
        noexcept (meta::nothrow::movable<T>)
        requires (meta::movable<T>)
    {
        using array = Array<T, total()>;
        array result = array::reserve();
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
        using array = Array<T, total()>;
        array result = array::reserve();
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
    template <impl::extent S, impl::array_flags<S> F = {}> requires (S.product() == total())
    [[nodiscard]] constexpr auto reshape() & noexcept {
        using view = ArrayView<storage, S, F>;
        return view{ptr};
    }

    /* Return a view over the array with a different shape, as long as the total number
    of elements is the same. */
    template <impl::extent S, impl::array_flags<S> F = {}> requires (S.product() == total())
    [[nodiscard]] constexpr auto reshape() const & noexcept {
        using view = ArrayView<const storage, S, F>;
        return view{static_cast<const storage*>(ptr)};
    }

    /* Return a new array with a different shape, as long as the total number of
    elements is the same.  This overload moves the current contents of the existing
    array. */
    template <impl::extent S, impl::array_flags<S> F = {}> requires (S.product() == total())
    [[nodiscard]] constexpr auto reshape() &&
        noexcept (meta::nothrow::movable<T>)
        requires (meta::movable<T>)
    {
        using array = Array<T, S, F>;
        array result = array::reserve();
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
    template <impl::extent S, impl::array_flags<S> F = {}> requires (S.product() == total())
    [[nodiscard]] constexpr auto reshape() const &&
        noexcept (meta::nothrow::copyable<T>)
        requires (meta::copyable<T>)
    {
        using array = Array<T, S, F>;
        array result = array::reserve();
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
        using view = impl::array_squeeze<storage, shape(), strides()>::template view<{}, {}>;
        return view{ptr};
    }

    /* Return a view over the array with all singleton dimensions removed, unless that
    is the only remaining dimension. */
    [[nodiscard]] constexpr auto squeeze() const & noexcept {
        using view = impl::array_squeeze<const storage, shape(), strides()>::template view<{}, {}>;
        return view{static_cast<const storage*>(ptr)};
    }

    /* Return a new array with a different shape, as long as the total number of
    elements is the same.  This overload moves the current contents of the existing
    array. */
    [[nodiscard]] constexpr auto squeeze() &&
        noexcept (meta::nothrow::movable<T>)
        requires (meta::movable<T>)
    {
        using array = impl::array_squeeze<T, shape(), strides()>::template array<{}, {}>;
        array result = array::reserve();
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
        using array = impl::array_squeeze<meta::as_const<T>, shape(), strides()>::template array<{}, {}>;
        array result = array::reserve();
        for (size_type i = 0; i < total(); ++i) {
            std::construct_at(
                &result.ptr[i].value,
                ptr[i].value
            );
        }
        return result;
    }



    /// TODO: transpose

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




    /* Access the first item in the array, assuming it isn't empty.  If the array is
    one-dimensional, then this will be a reference to the first underlying value.
    Otherwise, if the array is an lvalue, then it will be a view over the first
    sub-array.  If the array is an rvalue, then the view will be replaced by a full
    array containing the moved contents of the sub-array, to prevent lifetime
    issues. */
    template <typename Self> requires (!empty())
    [[nodiscard]] constexpr decltype(auto) front(this Self&& self) noexcept {
        if constexpr (ndim() == 1) {
            return ((*std::forward<Self>(self).ptr).value);
        } else if constexpr (meta::lvalue<Self>) {
            if constexpr (meta::is_const<Self>) {
                using view = impl::array_reduce<shape(), strides()>::template view<const storage>;
                return view{static_cast<const storage*>(self.ptr)};
            } else {
                using view = impl::array_reduce<shape(), strides()>::template view<storage>;
                return view{self.ptr};
            }
        } else {
            using array = impl::array_reduce<shape(), strides()>::template array<T>;
            array result = array::reserve();
            for (size_type i = 0; i < step; ++i) {
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
        constexpr size_type j = (size() - 1) * step;
        if constexpr (ndim() == 1) {
            return (std::forward<Self>(self).ptr[j].value);
        } else if constexpr (meta::lvalue<Self>) {
            if constexpr (meta::is_const<Self>) {
                using view = impl::array_reduce<shape(), strides()>::template view<const storage>;
                return view{static_cast<const storage*>(self.ptr) + j};
            } else {
                using view = impl::array_reduce<shape(), strides()>::template view<storage>;
                return view{self.ptr + j};
            }
        } else {
            using array = impl::array_reduce<shape(), strides()>::template array<T>;
            array result = array::reserve();
            for (size_type i = 0; i < step; ++i) {
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
            impl::valid_array_index<I...>::template value<shape(), strides()>
        )
    [[nodiscard]] constexpr decltype(auto) get(this Self&& self) noexcept {
        constexpr size_type j = impl::valid_array_index<I...>::template index<shape(), strides()>;
        if constexpr (sizeof...(I) == ndim()) {
            return (std::forward<Self>(self).ptr[j].value);
        } else if constexpr (meta::lvalue<Self>) {
            if constexpr (meta::is_const<Self>) {
                using view = impl::array_reduce<shape(), strides(), sizeof...(I)>::template view<
                    const storage
                >;
                return view{static_cast<meta::as_const<T>*>(self.ptr) + j};
            } else {
                using view = impl::array_reduce<shape(), strides(), sizeof...(I)>::template view<
                    storage
                >;
                return view{self.ptr + j};
            }
        } else {
            using array = impl::array_reduce<shape(), strides(), sizeof...(I)>::template array<T>;
            array result = array::reserve();
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
        size_type j = impl::array_index<shape(), strides()>(i...);
        if constexpr (sizeof...(I) == ndim()) {
            return (std::forward<Self>(self).ptr[j].value);
        } else if constexpr (meta::lvalue<Self>) {
            if constexpr (meta::is_const<Self>) {
                using view = impl::array_reduce<shape(), strides(), sizeof...(I)>::template view<
                    const storage
                >;
                return view{static_cast<const storage*>(self.ptr) + j};
            } else {
                using view = impl::array_reduce<shape(), strides(), sizeof...(I)>::template view<
                    storage
                >;
                return view{self.ptr + j};
            }
        } else {
            using array = impl::array_reduce<shape(), strides(), sizeof...(I)>::template array<T>;
            array result = array::reserve();
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
    template <typename U, impl::extent S, impl::array_flags<S> F> requires (S.product() == total())
    [[nodiscard]] constexpr bool operator==(const Array<U, S, F>& other) const
        noexcept (
            meta::nothrow::has_eq<const_reference, typename Array<U, S, F>::const_reference> &&
            meta::nothrow::truthy<meta::eq_type<
                const_reference,
                typename Array<U, S, F>::const_reference
            >>
        )
        requires (
            meta::has_eq<const_reference, typename Array<U, S, F>::const_reference> &&
            meta::truthy<meta::eq_type<const_reference, typename Array<U, S, F>::const_reference>>
        )
    {
        auto it1 = flatten().begin();
        auto end1 = flatten().end();
        auto it2 = other.flatten().begin();
        auto end2 = other.flatten().end();
        while (it1 != end1 && it2 != end2) {
            if (!bool(*it1 == *it2)) {
                return false;
            }
            ++it1;
            ++it2;
        }
        return true;
    }

    /* Compare two arrays of the same shape for lexicographic equality.  Iteration
    always begins at index 0 across all dimensions and advances the last index first,
    following row-major order. */
    template <typename U, impl::extent S, impl::array_flags<S> F> requires (S.product() == total())
    [[nodiscard]] constexpr bool operator==(const ArrayView<U, S, F>& other) const
        noexcept (
            meta::nothrow::has_eq<const_reference, typename ArrayView<U, S, F>::const_reference> &&
            meta::nothrow::truthy<meta::eq_type<
                const_reference,
                typename ArrayView<U, S, F>::const_reference
            >>
        )
        requires (
            meta::has_eq<const_reference, typename ArrayView<U, S, F>::const_reference> &&
            meta::truthy<meta::eq_type<
                const_reference,
                typename ArrayView<U, S, F>::const_reference
            >>
        )
    {
        auto it1 = flatten().begin();
        auto end1 = flatten().end();
        auto it2 = other.flatten().begin();
        auto end2 = other.flatten().end();
        while (it1 != end1 && it2 != end2) {
            if (!bool(*it1 == *it2)) {
                return false;
            }
            ++it1;
            ++it2;
        }
        return true;
    }

    /* Compare two arrays of the same shape for lexicographic equality.  Iteration
    always begins at index 0 across all dimensions and advances the last index first,
    following row-major order. */
    template <typename U, impl::extent S, impl::array_flags<S> F> requires (S.product() == total())
    [[nodiscard]] constexpr auto operator<=>(const Array<U, S, F>& other) const
        noexcept (
            meta::nothrow::has_lt<const_reference, typename Array<U, S, F>::const_reference> &&
            meta::nothrow::has_lt<typename Array<U, S, F>::const_reference, const_reference> &&
            meta::nothrow::truthy<meta::lt_type<
                const_reference,
                typename Array<U, S, F>::const_reference
            >> &&
            meta::nothrow::truthy<meta::lt_type<
                typename Array<U, S, F>::const_reference,
                const_reference
            >>
        )
        requires (
            meta::has_lt<const_reference, typename Array<U, S, F>::const_reference> &&
            meta::has_lt<typename Array<U, S, F>::const_reference, const_reference> &&
            meta::truthy<meta::lt_type<
                const_reference,
                typename Array<U, S, F>::const_reference
            >> &&
            meta::truthy<meta::lt_type<
                typename Array<U, S, F>::const_reference,
                const_reference
            >>
        )
    {
        auto it1 = flatten().begin();
        auto end1 = flatten().end();
        auto it2 = other.flatten().begin();
        auto end2 = other.flatten().end();
        while (it1 != end1 && it2 != end2) {
            if (bool(*it1 < *it2)) {
                return std::strong_ordering::less;
            } else if (bool(*it2 < *it1)) {
                return std::strong_ordering::greater;
            }
            ++it1;
            ++it2;
        }
        return std::strong_ordering::equal;
    }

    /* Compare two arrays of the same shape for lexicographic equality.  Iteration
    always begins at index 0 across all dimensions and advances the last index first,
    following row-major order. */
    template <typename U, impl::extent S, impl::array_flags<S> F> requires (S.product() == total())
    [[nodiscard]] constexpr auto operator<=>(const ArrayView<U, S, F>& other) const
        noexcept (
            meta::nothrow::has_lt<const_reference, typename ArrayView<U, S, F>::const_reference> &&
            meta::nothrow::has_lt<typename ArrayView<U, S, F>::const_reference, const_reference> &&
            meta::nothrow::truthy<meta::lt_type<
                const_reference,
                typename ArrayView<U, S, F>::const_reference
            >> &&
            meta::nothrow::truthy<meta::lt_type<
                typename ArrayView<U, S, F>::const_reference,
                const_reference
            >>
        )
        requires (
            meta::has_lt<const_reference, typename ArrayView<U, S, F>::const_reference> &&
            meta::has_lt<typename ArrayView<U, S, F>::const_reference, const_reference> &&
            meta::truthy<meta::lt_type<
                const_reference,
                typename ArrayView<U, S, F>::const_reference
            >> &&
            meta::truthy<meta::lt_type<
                typename ArrayView<U, S, F>::const_reference,
                const_reference
            >>
        )
    {
        auto it1 = flatten().begin();
        auto end1 = flatten().end();
        auto it2 = other.flatten().begin();
        auto end2 = other.flatten().end();
        while (it1 != end1 && it2 != end2) {
            if (bool(*it1 < *it2)) {
                return std::strong_ordering::less;
            } else if (bool(*it2 < *it1)) {
                return std::strong_ordering::greater;
            }
            ++it1;
            ++it2;
        }
        return std::strong_ordering::equal;
    }
};


template <typename T, impl::extent shape, impl::array_flags<shape> flags>
Array(ArrayView<T, shape, flags>) -> Array<meta::unqualify<impl::array_type<T>>, shape, flags>;


template <typename T, size_t N>
Array(T(&)[N]) -> Array<T, N>;


/// TODO: both of these CTAD guides may need to take more care in how they instantiate
/// the underlying type.  This should probably always be the type corresponding to the
/// yield type at a depth equal to the number of dimensions.  This would require a
/// lot of thinking, most likely.


template <meta::has_static_shape T>
    requires (
        !meta::ArrayView<T> &&
        meta::iterable<T>
    )
Array(T&&) -> Array<meta::remove_reference<meta::yield_type<T>>, meta::static_shape<T>()>;


template <meta::has_static_shape T>
    requires (
        !meta::ArrayView<T> &&
        !meta::iterable<T> &&
        meta::tuple_like<T> &&
        meta::tuple_types<T>::has_common_type
    )
Array(T&&) -> Array<
    meta::remove_reference<typename meta::tuple_types<T>::template eval<meta::common_type>>,
    meta::static_shape<T>()
>;


/// TODO: add another version of the above CTAD guide that takes a tuple-like type that
/// has a static shape.


/// TODO: this last CTAD guide should be maximally constrained to avoid ambiguity with
/// conversions from C-style arrays, C++ array types with static shapes, and direct
/// initialization.  In fact, perhaps I can constrain this to a `std::initializer_list`,
/// which is a good idea, most likely.  That would avoid all ambiguity issues.

/// TODO: If you provide structured data, then this should be able to infer the
/// dimensionality of the array from the nested structure.  This should be covered
/// similarly to the init case.  Perhaps I can just check for convertibility to the
/// `array_init` type, or something like that?  This also has echoes of the static
/// shape deduction, and may even be able to be merged with those cases, which would
/// be a huge win.


template <typename... Ts>
    requires (
        (sizeof...(Ts) > 1 || ((
            !meta::ArrayView<Ts> &&
            !meta::has_static_shape<Ts>
        ) && ...)) &&
        meta::has_common_type<Ts...>
    )
Array(Ts&&...) -> Array<impl::array_common<Ts...>, sizeof...(Ts)>;






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
    constexpr bool enable_borrowed_range<T> = true;

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


namespace bertrand {


    // static constexpr std::array<std::array<int, 2>, 2> test {
    //     {1, 2},
    //     {3, 4}
    // };


    static constexpr Array test1 {Array{1, 2}, Array{3, 4}};

    static constexpr Array test2 = Array<int, {2, 2}>::reserve();

    static constexpr auto test3 = Array<int, {2, 3}>{
        Array{0, 1, 2},
        Array{3, 4, 5}
    }.reshape<{3, 2}>();
    static_assert(test3[0, 0] == 0);
    static_assert(test3[0, 1] == 1);
    static_assert(test3[1, 0] == 2);
    static_assert(test3[1, 1] == 3);
    static_assert(test3[2, 0] == 4);
    static_assert(test3[2, 1] == 5);

    static constexpr auto test4 = Array<int, {2, 3}>{
        Array{0, 1, 2},
        Array{3, 4, 5}
    }[-1];
    static_assert(test4[0] == 3);
    static_assert(test4[1] == 4);
    static_assert(test4[2] == 5);

    static constexpr auto test5 = meta::to_const(Array<int, {2, 3}>{
        Array{0, 1, 2},
        Array{3, 4, 5}
    }).flatten();


    static constexpr Array test6 = test3.flatten();
    static constexpr ArrayView test7 = test3;


    static constexpr auto test8 = Array<int, {1, 3}>{Array{1, 2, 3}};
    static constexpr auto test9 = test8.squeeze();
    static constexpr auto test10 = Array<int, {1, 3}>{Array{1, 2, 3}}.squeeze();

    static_assert([] {
        Array<int, {2, 2}> arr {
            Array{1, 2},
            Array{3, 4}
        };
        if (arr[0, 0] != 1) return false;
        if (arr[0, 1] != 2) return false;
        if (arr[1, 0] != 3) return false;
        if (arr[1, -1] != 4) return false;

        auto arr2 = Array<int, {2, 2}>{Array{1, 2}, Array{3, 3}};
        arr = arr2;
        if (arr[1, 1] != 3) return false;

        if (arr.shape()[-1] != 2) return false;

        auto x = arr.data();
        if (*x != 1) return false;
        ++x;

        return true;
    }());


    static_assert([] {
        Array<int, {3, 2}> arr {Array{1, 2}, Array{3, 4}, Array{5, 6}};
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

    static_assert([] {
        Array<int, {3, 2}> arr {Array{1, 2}, Array{3, 4}, Array{5, 6}};
        auto it = arr.rbegin();
        if ((*it) != Array{5, 6}) return false;
        ++it;
        if (*it != Array{3, 4}) return false;
        ++it;
        if (*it != Array{1, 2}) return false;
        ++it;
        if (it != arr.rend()) return false;

        it = arr.rbegin();
        auto end = arr.rend();
        while (it != end) {

            ++it;
        }

        return true;
    }());

    static_assert([] {
        Array<int, {2, 3}> arr {
            Array{1, 2, 3},
            Array{4, 5, 6}
        };
        const auto view = arr[0];
        auto& x = view[1];
        for (auto& y : view) {

        }
        auto z = view.data();

        auto f = view.back();

        return true;
    }());


    inline void test() {
        int x = 1;
        int y = 2;
        int z = 3;
        int w = 4;
        Array<int, {2, 2}> arr {
            Array{x, y},
            Array{z, w}
        };
        auto p = arr.data();
    }

    static constexpr std::array test_arr {1, 2, 3};
    static constexpr auto test_shape = meta::static_shape<decltype(test_arr)>();
    static_assert(test_shape == impl::extent{3});
    static constexpr ArrayView test_view {test_arr};
    static_assert(test_view[-1] == 3);

    // static constexpr Array test_arr2 = std::array{1, 2, 3};

}


#endif  // BERTRAND_ARRAY_H
