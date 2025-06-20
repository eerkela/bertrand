#ifndef BERTRAND_ITER_H
#define BERTRAND_ITER_H

#include "bertrand/common.h"
#include "bertrand/except.h"
#include "bertrand/union.h"
#include "bertrand/math.h"


namespace bertrand {


/* A convenience class describing the indices provided to a slice-style subscript
operator.  Generally, an instance of this class will be implicitly constructed using an
initializer list to condense the syntax and provide compile-time guarantees when it
comes to slice shape. */
struct slice {
private:
    static constexpr ssize_t missing = std::numeric_limits<ssize_t>::min();

public:
    ssize_t start;
    ssize_t stop;
    ssize_t step;

    [[nodiscard]] explicit constexpr slice(
        const Optional<ssize_t>& start = None,
        const Optional<ssize_t>& stop = None,
        const Optional<ssize_t>& step = None
    ) :
        start(start.value_or(missing)),
        stop(stop.value_or(missing)),
        step(step.value_or(ssize_t(1)))
    {
        if (this->step == 0) {
            throw ValueError("slice step cannot be zero");
        }
    }

    /* Result of the `normalize` method.  Indices of this form can be passed to
    per-container slice implementations to implement the correct traversal logic. */
    struct normalized {
        ssize_t start = 0;
        ssize_t stop = 0;
        ssize_t step = 0;
        ssize_t length = 0;
    };

    /* Normalize the provided indices against a container of a given size, returning a
    4-tuple with members `start`, `stop`, `step`, and `length` in that order, and
    supporting structured bindings.  If either of the original `start` or `stop`
    indices were given as negative values or `nullopt`, they will be normalized
    according to the size, and will be truncated to the nearest end if they are out
    of bounds.  `length` stores the total number of elements that will be included in
    the slice */
    [[nodiscard]] constexpr normalized normalize(ssize_t size) const noexcept {
        bool neg = step < 0;
        ssize_t zero = 0;
        normalized result {zero, zero, step, zero};

        // normalize start, correcting for negative indices and truncating to bounds
        if (start == missing) {
            result.start = neg ? size - ssize_t(1) : zero;  // neg: size - 1 | pos: 0
        } else {
            result.start = start;
            result.start += size * (result.start < zero);
            if (result.start < zero) {
                result.start = -neg;  // neg: -1 | pos: 0
            } else if (result.start >= size) {
                result.start = size - neg;  // neg: size - 1 | pos: size
            }
        }

        // normalize stop, correcting for negative indices and truncating to bounds
        if (stop == missing) {
            result.stop = neg ? ssize_t(-1) : size;  // neg: -1 | pos: size
        } else {
            result.stop = stop;
            result.stop += size * (result.stop < zero);
            if (result.stop < zero) {
                result.stop = -neg;  // neg: -1 | pos: 0
            } else if (result.stop >= size) {
                result.stop = size - neg;  // neg: size - 1 | pos: size
            }
        }

        // compute number of included elements
        ssize_t bias = result.step + (result.step < zero) - (result.step > zero);
        result.length = (result.stop - result.start + bias) / result.step;
        result.length *= (result.length > zero);
        return result;
    }
};


namespace impl {
    struct slice_tag {};
}


namespace meta {

    template <typename T>
    concept slice = meta::inherits<T, impl::slice_tag>;

    template <typename T>
    concept sliceable = requires(T t, bertrand::slice s) {
        { t[s] } -> meta::slice;
    };

    template <sliceable T>
    using slice_type = decltype(::std::declval<T>()[::std::declval<bertrand::slice>()]);

}


namespace impl {

    /* Check to see if applying Python-style wraparound to a compile-time index would
    yield a valid index into a container of a given size.  Returns false if the
    index would be out of bounds after normalizing. */
    template <ssize_t size, ssize_t I>
    concept valid_index = ((I + size * (I < 0)) >= 0) && ((I + size * (I < 0)) < size);

    /* Apply Python-style wraparound to a compile-time index. Fails to compile if the
    index would be out of bounds after normalizing. */
    template <ssize_t size, ssize_t I> requires (valid_index<size, I>)
    constexpr ssize_t normalize_index() noexcept { return I + size * (I < 0); }

    /* Apply Python_style wraparound to a runtime index.  Throws an `IndexError` if the
    index would be out of bounds after normalizing. */
    inline constexpr ssize_t normalize_index(ssize_t size, ssize_t i) noexcept(!DEBUG) {
        ssize_t j = i + size * (i < 0);
        if constexpr (DEBUG) {
            if (j < 0 || j >= size) {
                throw IndexError(std::to_string(i));
            }
        }
        return j;
    }

    /* Apply python-style wraparound to a runtime index, truncating it to the nearest
    valid index.  The second return value is set 0 if the index was valid, or +/- 1 to
    indicate overflow to the right or left, respectively. */
    inline constexpr std::pair<ssize_t, ssize_t> truncate_index(
        ssize_t size,
        ssize_t i
    ) noexcept {
        i += size * (i < 0);
        if (i < 0) {
            return {0, -1};
        }
        if (i >= size) {
            return {size, 1};
        }
        return {i, 0};
    }

    /* A generic sentinel type to simplify iterator implementations. */
    struct sentinel {
        constexpr bool operator==(sentinel) const noexcept { return true; }
        constexpr auto operator<=>(sentinel) const noexcept {
            return std::strong_ordering::equal;
        }
    };

    /* A wrapper around a bidirectional iterator that yields a subset of a given
    container within a specified start and stop interval, with an arbitrary step
    size.  Containers can expose an `operator[]` overload that returns one of these
    objects to allow Python-style slicing semantics in conjunction with the
    `bertrand::slice` helper class, including basic iteration, assignment and
    extraction via an implicit conversion operator. */
    template <meta::unqualified T> requires (meta::bidirectional_iterator<T>)
    struct slice : slice_tag {
    private:
        using normalized = bertrand::slice::normalized;

        T m_begin;
        normalized m_indices;

    public:
        using value_type = std::iterator_traits<T>::value_type;
        using reference = std::iterator_traits<T>::reference;
        using const_reference = meta::as_const<reference>;
        using pointer = std::iterator_traits<T>::pointer;
        using const_pointer = meta::as_pointer<meta::as_const<meta::remove_pointer<pointer>>>;

        struct iterator {
            using iterator_category = std::conditional_t<
                meta::output_iterator<T, slice::value_type>,
                std::output_iterator_tag,
                std::input_iterator_tag
            >;
            using difference_type = std::iterator_traits<T>::difference_type;
            using value_type = slice::value_type;
            using reference = slice::reference;
            using const_reference = slice::const_reference;
            using pointer = slice::pointer;
            using const_pointer = slice::const_pointer;

        private:
            T iter;
            ssize_t step;
            ssize_t length;

        public:
            constexpr iterator() = default;
            constexpr iterator(const T& iter, ssize_t step, ssize_t length)
                noexcept(meta::nothrow::copyable<T>)
                requires(meta::copyable<T>)
            :
                iter(iter),
                step(step),
                length(length)
            {}

            constexpr iterator(T&& iter, ssize_t step, ssize_t length)
                noexcept(meta::nothrow::movable<T>)
                requires(meta::movable<T>)
            :
                iter(std::move(iter)),
                step(step),
                length(length)
            {}

            [[nodiscard]] constexpr decltype(auto) operator*()
                noexcept(meta::nothrow::has_dereference<T>)
                requires(meta::has_dereference<T>)
            {
                return (*iter);
            }

            [[nodiscard]] constexpr decltype(auto) operator*() const
                noexcept(meta::nothrow::has_dereference<const T>)
                requires(meta::has_dereference<const T>)
            {
                return (*iter);
            }

            [[nodiscard]] constexpr decltype(auto) operator->()
                noexcept(meta::nothrow::has_arrow<T>)
                requires(meta::has_arrow<T>)
            {
                return (iter.operator->());
            }

            [[nodiscard]] constexpr decltype(auto) operator->() const
                noexcept(meta::nothrow::has_arrow<const T>)
                requires(meta::has_arrow<const T>)
            {
                return (iter.operator->());
            }

            constexpr iterator& operator++()
                noexcept(
                    meta::nothrow::has_preincrement<T> &&
                    meta::nothrow::has_predecrement<T>
                )
                requires(
                    meta::random_access_iterator<T> &&
                    meta::has_iadd<T, ssize_t>
                )
            {
                iter += step;
                --length;
                return *this;
            }

            constexpr iterator& operator++()
                noexcept(
                    meta::nothrow::has_preincrement<T> &&
                    meta::nothrow::has_predecrement<T>
                )
                requires(
                    !meta::random_access_iterator<T> &&
                    meta::has_preincrement<T> &&
                    meta::has_predecrement<T>
                )
            {
                if (step > 0) {
                    for (ssize_t i = 0; i < step; ++i) ++iter;
                } else {
                    for (ssize_t i = step; i < 0; ++i) --iter;
                }
                --length;
                return *this;
            }

            [[nodiscard]] constexpr iterator operator++(int)
                noexcept(
                    meta::nothrow::copyable<iterator> &&
                    meta::nothrow::has_preincrement<iterator>
                )
                requires(
                    meta::copyable<iterator> &&
                    meta::has_preincrement<iterator>
                )
            {
                iterator copy = *this;
                ++(*this);
                return copy;
            }

            [[nodiscard]] constexpr bool operator==(const iterator& other) const
                noexcept
            {
                return length == other.length;
            }

            [[nodiscard]] constexpr bool operator!=(const iterator& other) const
                noexcept
            {
                return length != other.length;
            }
        };

        using const_iterator = iterator;

        template <meta::at_returns<T> C>
        [[nodiscard]] constexpr slice(C& container, const normalized& indices)
            noexcept(meta::nothrow::at_returns<C, T>)
        :
            m_begin(container.at(indices.start)),
            m_indices(indices)
        {}

        constexpr slice(const slice&) = delete;
        constexpr slice(slice&&) = delete;
        constexpr slice& operator=(const slice&) = delete;
        constexpr slice& operator=(slice&&) = delete;

        [[nodiscard]] constexpr const normalized& indices() const noexcept { return m_indices; }
        [[nodiscard]] constexpr ssize_t start() const noexcept { return m_indices.start; }
        [[nodiscard]] constexpr ssize_t stop() const noexcept { return m_indices.stop; }
        [[nodiscard]] constexpr ssize_t step() const noexcept { return m_indices.step; }
        [[nodiscard]] constexpr ssize_t ssize() const noexcept { return m_indices.length; }
        [[nodiscard]] constexpr size_t size() const noexcept { return size_t(ssize()); }
        [[nodiscard]] constexpr bool empty() const noexcept { return !ssize(); }
        [[nodiscard]] constexpr iterator begin() const
            noexcept(noexcept(iterator{m_begin, m_indices.step, m_indices.length}))
        {
            return {m_begin, m_indices.step, m_indices.length};
        }
        [[nodiscard]] constexpr iterator begin() &&
            noexcept(noexcept(iterator{
                std::move(m_begin),
                m_indices.step,
                m_indices.length
            }))
        {
            return {std::move(m_begin), m_indices.step, m_indices.length};
        }
        [[nodiscard]] constexpr iterator cbegin() const
            noexcept(noexcept(iterator{m_begin, m_indices.step, m_indices.length}))
        {
            return {m_begin, m_indices.step, m_indices.length};
        }
        [[nodiscard]] constexpr iterator cbegin() &&
            noexcept(noexcept(iterator{
                std::move(m_begin),
                m_indices.step,
                m_indices.length
            }))
        {
            return {std::move(m_begin), m_indices.step, m_indices.length};
        }
        [[nodiscard]] constexpr iterator end() const
            noexcept(noexcept(iterator{T{}, m_indices.stop, 0}))
        {
            return {T{}, m_indices.step, 0};
        }
        [[nodiscard]] constexpr iterator cend() const
            noexcept(noexcept(iterator{T{}, m_indices.stop, 0}))
        {
            return {T{}, m_indices.step, 0};
        }

        template <typename V>
        [[nodiscard]] constexpr operator V() const
            noexcept (requires{{V(std::from_range, *this)} noexcept;})
            requires (requires{{V(std::from_range, *this)};})
        {
            return V(std::from_range, *this);
        }

        template <typename V>
        [[nodiscard]] constexpr operator V() const
            noexcept (requires{{V(begin(), end())} noexcept;})
            requires (!requires{{V(std::from_range, *this)};} && requires{{V(begin(), end())};})
        {
            return V(begin(), end());
        }

        template <meta::iterable Range>
        constexpr slice& operator=(Range&& range)
            requires (meta::output_iterator<T, meta::yield_type<Range>>)
        {
            constexpr bool has_size = meta::has_size<meta::as_lvalue<Range>>;

            // if the range has an explicit size, then we can check it ahead of time
            // to ensure that it exactly matches that of the slice
            if constexpr (has_size) {
                if (std::ranges::size(range) != size()) {
                    throw ValueError(
                        "cannot assign a range of size " +
                        std::to_string(std::ranges::size(range)) +
                        " to a slice of size " + std::to_string(size())
                    );
                }
            }

            // If we checked the size above, we can avoid checking it again on each
            // iteration
            auto it = std::ranges::begin(range);
            auto end = std::ranges::end(range);
            auto output = begin();
            for (ssize_t i = 0; i < m_indices.length; ++i) {
                if constexpr (!has_size) {
                    if (it == end) {
                        throw ValueError(
                            "not enough values to fill slice of size " +
                            std::to_string(size())
                        );
                    }
                }
                *output = *it;
                ++it;
                ++output;
            }

            if constexpr (!has_size) {
                if (it != end) {
                    throw ValueError(
                        "range length exceeds slice of size " +
                        std::to_string(size())
                    );
                }
            }
            return *this;
        }
    };

    template <typename T>
    concept contiguous_iterator_arg = meta::not_void<T> && !meta::reference<T>;

    template <contiguous_iterator_arg T>
    struct contiguous_iterator {
        using iterator_category = std::contiguous_iterator_tag;
        using difference_type = ssize_t;
        using value_type = T;
        using reference = meta::as_lvalue<value_type>;
        using const_reference = meta::as_lvalue<meta::as_const<value_type>>;
        using pointer = meta::as_pointer<value_type>;
        using const_pointer = meta::as_pointer<meta::as_const<value_type>>;

        constexpr contiguous_iterator(pointer ptr = nullptr) noexcept : ptr(ptr) {};
        constexpr contiguous_iterator(const contiguous_iterator&) noexcept = default;
        constexpr contiguous_iterator(contiguous_iterator&&) noexcept = default;
        constexpr contiguous_iterator& operator=(const contiguous_iterator&) noexcept = default;
        constexpr contiguous_iterator& operator=(contiguous_iterator&&) noexcept = default;

        template <meta::more_qualified_than<T> U>
        [[nodiscard]] constexpr operator contiguous_iterator<U>() noexcept {
            return {ptr};
        }

        [[nodiscard]] constexpr reference operator*() noexcept {
            return *ptr;
        }

        [[nodiscard]] constexpr const_reference operator*() const noexcept {
            return *ptr;
        }

        [[nodiscard]] constexpr pointer operator->() noexcept {
            return ptr;
        }

        [[nodiscard]] constexpr const_pointer operator->() const noexcept {
            return ptr;
        }

        [[nodiscard]] constexpr reference operator[](difference_type n) noexcept {
            return ptr[n];
        }

        [[nodiscard]] constexpr const_reference operator[](difference_type n) const noexcept {
            return ptr[n];
        }

        constexpr contiguous_iterator& operator++() noexcept {
            ++ptr;
            return *this;
        }

        [[nodiscard]] constexpr contiguous_iterator operator++(int) noexcept {
            contiguous_iterator copy = *this;
            ++(*this);
            return copy;
        }

        constexpr contiguous_iterator& operator+=(difference_type n) noexcept {
            ptr += n;
            return *this;
        }

        [[nodiscard]] constexpr contiguous_iterator operator+(difference_type n) const noexcept {
            return {ptr + n};
        }

        constexpr contiguous_iterator& operator--() noexcept {
            --ptr;
            return *this;
        }

        [[nodiscard]] constexpr contiguous_iterator operator--(int) noexcept {
            contiguous_iterator copy = *this;
            --(*this);
            return copy;
        }

        constexpr contiguous_iterator& operator-=(difference_type n) noexcept {
            ptr -= n;
            return *this;
        }

        [[nodiscard]] constexpr contiguous_iterator operator-(difference_type n) const noexcept {
            return {ptr - n};
        }

        template <meta::is<T> U>
        [[nodiscard]] constexpr difference_type operator-(
            const contiguous_iterator<U>& rhs
        ) const noexcept {
            return ptr - rhs.ptr;
        }

        template <meta::is<T> U>
        [[nodiscard]] constexpr std::strong_ordering operator<=>(
            const contiguous_iterator<U>& rhs
        ) const noexcept {
            return ptr <=> rhs.ptr;
        }

        template <meta::is<T> U>
        [[nodiscard]] constexpr bool operator==(
            const contiguous_iterator<U>& rhs
        ) const noexcept {
            return ptr == rhs.ptr;
        }

    private:
        pointer ptr;
    };

    /// TODO: store a reference to the container type within the iterator, and assume
    /// it has a len() that the iterator can check against.  That should be slightly
    /// more efficient in debug builds, which is actually relevant.  Not entirely sure.

    template <contiguous_iterator_arg T> requires (DEBUG)
    struct contiguous_iterator<T> {
        using boundscheck = std::function<void(const contiguous_iterator&)>;

        using iterator_category = std::contiguous_iterator_tag;
        using difference_type = ssize_t;
        using value_type = T;
        using reference = meta::as_lvalue<value_type>;
        using const_reference = meta::as_lvalue<meta::as_const<value_type>>;
        using pointer = meta::as_pointer<value_type>;
        using const_pointer = meta::as_pointer<meta::as_const<value_type>>;

        constexpr contiguous_iterator(
            pointer ptr = nullptr,
            boundscheck check = [](const contiguous_iterator&) noexcept {}
        ) :
            ptr(ptr),
            check(std::move(check))
        {};

        constexpr contiguous_iterator(const contiguous_iterator&) noexcept = default;
        constexpr contiguous_iterator(contiguous_iterator&&) noexcept = default;
        constexpr contiguous_iterator& operator=(const contiguous_iterator&) noexcept = default;
        constexpr contiguous_iterator& operator=(contiguous_iterator&&) noexcept = default;

        template <meta::more_qualified_than<T> U>
        [[nodiscard]] constexpr operator contiguous_iterator<U>() {
            return {ptr, check};
        }

        [[nodiscard]] constexpr reference operator*() {
            check(*this);
            return *ptr;
        }

        [[nodiscard]] constexpr const_reference operator*() const {
            check(*this);
            return *ptr;
        }

        [[nodiscard]] constexpr pointer operator->() {
            check(*this);
            return ptr;
        }

        [[nodiscard]] constexpr const_pointer operator->() const {
            check(*this);
            return ptr;
        }

        [[nodiscard]] constexpr reference operator[](difference_type n) {
            return *contiguous_iterator{ptr + n, check};
        }

        [[nodiscard]] constexpr const_reference operator[](difference_type n) const {
            return *contiguous_iterator{ptr + n, check};
        }

        constexpr contiguous_iterator& operator++() noexcept {
            ++ptr;
            return *this;
        }

        [[nodiscard]] constexpr contiguous_iterator operator++(int) noexcept {
            contiguous_iterator copy = *this;
            ++(*this);
            return copy;
        }

        constexpr contiguous_iterator& operator+=(difference_type n) noexcept {
            ptr += n;
            return *this;
        }

        [[nodiscard]] constexpr contiguous_iterator operator+(difference_type n) const {
            return {ptr + n, check};
        }

        constexpr contiguous_iterator& operator--() noexcept {
            --ptr;
            return *this;
        }

        [[nodiscard]] constexpr contiguous_iterator operator--(int) noexcept {
            contiguous_iterator copy = *this;
            --(*this);
            return copy;
        }

        constexpr contiguous_iterator& operator-=(difference_type n) noexcept {
            ptr -= n;
            return *this;
        }

        [[nodiscard]] constexpr contiguous_iterator operator-(difference_type n) const {
            return {ptr - n, check};
        }

        template <meta::is<T> U>
        [[nodiscard]] constexpr difference_type operator-(
            const contiguous_iterator<U>& rhs
        ) const {
            check(rhs);
            return ptr - rhs.ptr;
        }

        template <meta::is<T> U>
        [[nodiscard]] constexpr std::strong_ordering operator<=>(
            const contiguous_iterator<U>& rhs
        ) const {
            check(rhs);
            return ptr <=> rhs.ptr;
        }

        template <meta::is<T> U>
        [[nodiscard]] constexpr bool operator==(
            const contiguous_iterator<U>& rhs
        ) const {
            check(rhs);
            return ptr == rhs.ptr;
        }

    private:
        pointer ptr;
        boundscheck check;
    };

    /* Enable/disable and optimize the `any()`/`all()` operators such that they fail
    fast where possible, unlike other reductions. */
    template <typename... Ts>
    struct truthy {
        template <typename F>
        static constexpr bool enable = ((
            meta::invocable<F, Ts> &&
            meta::explicitly_convertible_to<
                meta::invoke_type<F, Ts>,
                bool
            >
        ) && ...);
        template <typename F>
        static constexpr bool nothrow = ((
            meta::nothrow::invocable<F, Ts> &&
            meta::nothrow::explicitly_convertible_to<
                meta::nothrow::invoke_type<F, Ts>,
                bool
            >
        ) && ...);

        template <typename F, typename... A> requires (sizeof...(A) > 1 && enable<F>)
        static constexpr bool all(F&& func, A&&... args) noexcept(nothrow<F>) {
            return (std::forward<F>(func)(std::forward<A>(args)) && ...);
        }

        template <size_t I = 0, typename F, meta::tuple_like T>
            requires (I < meta::tuple_size<T> && meta::has_get<T, I> && enable<F>)
        static constexpr bool all(F&& func, T&& t) noexcept(
            nothrow<F> &&
            meta::nothrow::has_get<T, I> &&
            noexcept(all<I + 1>(std::forward<F>(func), std::forward<T>(t)))
        ) {
            if (!std::forward<F>(func)(meta::tuple_get<I>(std::forward<T>(t)))) {
                return false;
            }
            return all<I + 1>(std::forward<F>(func), std::forward<T>(t));;
        }

        template <size_t I = 0, typename F, meta::tuple_like T>
            requires (I >= meta::tuple_size<T> && enable<F>)
        static constexpr bool all(F&&, T&&) noexcept {
            return true;
        }

        template <typename F, typename... A> requires (sizeof...(A) > 1 && enable<F>)
        static constexpr bool any(F&& func, A&&... args) noexcept(nothrow<F>) {
            return (std::forward<F>(func)(std::forward<A>(args)) || ...);
        }

        template <size_t I = 0, typename F, meta::tuple_like T>
            requires (I < meta::tuple_size<T> && meta::has_get<T, I> && enable<F>)
        static constexpr bool any(F&& func, T&& t) noexcept(
            meta::tuple_types<T>::template eval<truthy>::template nothrow<F> &&
            meta::nothrow::has_get<T, I> &&
            noexcept(any<I + 1>(std::forward<F>(func), std::forward<T>(t)))
        ) {
            if (std::forward<F>(func)(meta::tuple_get<I>(std::forward<T>(t)))) {
                return true;
            }
            return any<I + 1>(std::forward<F>(func), std::forward<T>(t));;
        }

        template <size_t I = 0, typename F, meta::tuple_like T>
            requires (I >= meta::tuple_size<T> && enable<F>)
        static constexpr bool any(F&&, T&&) noexcept {
            return false;
        }
    };

    /* Apply a pairwise function over the arguments to implement a `fold_left()`
    function call. */
    template <typename out, typename...>
    struct _fold_left {
        template <typename F>
        struct traits {
            using type = meta::remove_rvalue<out>;
            static constexpr bool enable = true;
            static constexpr bool nothrow = true;
        };
        template <typename F, typename T>
        static constexpr traits<F>::type operator()(F&&, T&& arg) noexcept(
            meta::nothrow::convertible_to<T, typename traits<F>::type>
        ) {
            return std::forward<T>(arg);
        }
    };
    template <typename prev, typename curr, typename... next>
    struct _fold_left<prev, curr, next...> {
        template <typename F>
        struct traits {
            using type = void;
            static constexpr bool enable = false;
            static constexpr bool nothrow = false;
        };
        template <meta::invocable<prev, curr> F>
            requires (meta::has_common_type<prev, meta::invoke_type<F, prev, curr>>)
        struct traits<F> {
            using recur = _fold_left<
                meta::common_type<prev, meta::invoke_type<F, prev, curr>>,
                next...
            >::template traits<F>;
            using type = recur::type;
            static constexpr bool enable = recur::enable;
            static constexpr bool nothrow =
                meta::nothrow::invocable<F, prev, curr> &&
                meta::nothrow::convertible_to<meta::invoke_type<F, prev, curr>, type> &&
                recur::nothrow;
        };
        template <typename F, typename L, typename R, typename... Ts>
        static constexpr decltype(auto) operator()(
            F&& func,
            L&& lhs,
            R&& rhs,
            Ts&&... rest
        ) noexcept(traits<F>::nothrow) {
            return (_fold_left<
                meta::common_type<prev, meta::invoke_type<F, prev, curr>>,
                next...
            >{}(
                std::forward<F>(func),
                std::forward<F>(func)(std::forward<L>(lhs), std::forward<R>(rhs)),
                std::forward<Ts>(rest)...
            ));
        }
    };
    template <typename F, typename... Ts>
    using fold_left = _fold_left<Ts...>::template traits<F>;

    /* Apply a pairwise function over the arguments to implement a `fold_right()`
    function call. */
    template <typename out, typename...>
    struct _fold_right {
        template <typename F>
        struct traits {
            using type = meta::remove_rvalue<out>;
            static constexpr bool enable = true;
            static constexpr bool nothrow = true;
        };
        template <typename F, typename T>
        static constexpr traits<F>::type operator()(F&&, T&& arg) noexcept(
            meta::nothrow::convertible_to<T, typename traits<F>::type>
        ) {
            return std::forward<T>(arg);
        }
    };
    template <typename prev, typename curr, typename... next>
    struct _fold_right<prev, curr, next...> {
        template <typename F>
        using recur = _fold_right<curr, next...>::template traits<F>;

        template <typename F>
        struct traits {
            using type = void;
            static constexpr bool enable = false;
            static constexpr bool nothrow = false;
        };
        template <typename F>
            requires (
                meta::invocable<F, prev, typename recur<F>::type> &&
                meta::has_common_type<
                prev,
                    meta::invoke_type<F, prev, typename recur<F>::type>
                >
            )
        struct traits<F> {
            using type = meta::common_type<
                prev,
                meta::invoke_type<F, prev, typename recur<F>::type>
            >;
            static constexpr bool enable = recur<F>::enable;
            static constexpr bool nothrow =
                recur<F>::nothrow &&
                meta::nothrow::invocable<F, prev, typename recur<F>::type> &&
                meta::nothrow::convertible_to<
                    meta::invoke_type<F, prev, typename recur<F>::type>,
                    type
                >;
        };
        template <typename F, typename L, typename R, typename... Ts>
        static constexpr decltype(auto) operator()(
            F&& func,
            L&& lhs,
            R&& rhs,
            Ts&&... rest
        ) noexcept(traits<F>::nothrow) {
            return (std::forward<F>(func)(
                std::forward<L>(lhs),
                _fold_right<curr, next...>{}(
                    std::forward<F>(func),
                    std::forward<R>(rhs),
                    std::forward<Ts>(rest)...
                )
            ));
        }
    };
    template <typename F, typename... Ts>
    using fold_right = _fold_right<Ts...>::template traits<F>;

    /* Apply a boolean less-than predicate over the arguments to implement a `min()`
    function call. */
    template <typename out, typename...>
    struct _min {
        template <typename F>
        struct traits {
            using type = meta::remove_rvalue<out>;
            static constexpr bool enable = true;
            static constexpr bool nothrow = true;
        };
        template <typename F, typename T>
        static constexpr traits<F>::type operator()(F&&, T&& arg) noexcept(
            meta::nothrow::convertible_to<T, typename traits<F>::type>
        ) {
            return std::forward<T>(arg);
        }
    };
    template <typename prev, typename curr, typename... next>
    struct _min<prev, curr, next...> {
        template <typename F>
        struct traits {
            using type = void;
            static constexpr bool enable = false;
            static constexpr bool nothrow = false;
        };
        template <meta::invocable<prev, curr> F>
            requires (meta::explicitly_convertible_to<
                meta::invoke_type<F, prev, curr>,
                bool
            >)
        struct traits<F> {
            using recur =
                _min<meta::common_type<prev, curr>, next...>::template traits<F>;
            using type = recur::type;
            static constexpr bool enable = recur::enable;
            static constexpr bool nothrow =
                meta::nothrow::invocable<F, prev, curr> &&
                meta::nothrow::explicitly_convertible_to<
                    meta::invoke_type<F, prev, curr>,
                    bool
                > &&
                meta::nothrow::convertible_to<curr, type> &&
                recur::nothrow;
        };
        template <typename F, typename T, typename R, typename... Ts>
        static constexpr decltype(auto) operator()(
            F&& func,
            T&& min,
            R&& rhs,
            Ts&&... rest
        ) noexcept(traits<F>::nothrow) {
            // R < L
            if (std::forward<F>(func)(std::forward<R>(rhs), std::forward<T>(min))) {
                return (_min<meta::common_type<prev, curr>, next...>{}(
                    std::forward<F>(func),
                    std::forward<R>(rhs),  // forward R
                    std::forward<Ts>(rest)...
                ));
            } else {
                return (_min<meta::common_type<prev, curr>, next...>{}(
                    std::forward<F>(func),
                    std::forward<T>(min),  // retain min
                    std::forward<Ts>(rest)...
                ));
            }
        }
    };
    template <typename F, typename... Ts>
    using min = _min<Ts...>::template traits<F>;

    /* Apply a boolean less-than predicate over the argument to implement a `max()`
    function call. */
    template <typename out, typename...>
    struct _max {
        template <typename F>
        struct traits {
            using type = meta::remove_rvalue<out>;
            static constexpr bool enable = true;
            static constexpr bool nothrow = true;
        };
        template <typename F, typename T>
        static constexpr traits<F>::type operator()(F&&, T&& arg) noexcept(
            meta::nothrow::convertible_to<T, typename traits<F>::type>
        ) {
            return std::forward<T>(arg);
        }
    };
    template <typename prev, typename curr, typename... next>
    struct _max<prev, curr, next...> {
        template <typename F>
        struct traits {
            using type = void;
            static constexpr bool enable = false;
            static constexpr bool nothrow = false;
        };
        template <meta::invocable<prev, curr> F>
            requires (meta::explicitly_convertible_to<
                meta::invoke_type<F, prev, curr>,
                bool
            >)
        struct traits<F> {
            using recur =
                _max<meta::common_type<prev, curr>, next...>::template traits<F>;
            using type = recur::type;
            static constexpr bool enable = recur::enable;
            static constexpr bool nothrow =
                meta::nothrow::invocable<F, prev, curr> &&
                meta::nothrow::explicitly_convertible_to<
                    meta::invoke_type<F, prev, curr>,
                    bool
                > &&
                meta::nothrow::convertible_to<curr, type> &&
                recur::nothrow;
        };
        template <typename F, typename T, typename R, typename... Ts>
        static constexpr decltype(auto) operator()(
            F&& func,
            T&& max,
            R&& rhs,
            Ts&&... rest
        ) noexcept(traits<F>::nothrow) {
            // L < R
            if (std::forward<F>(func)(std::forward<T>(max), std::forward<R>(rhs))) {
                return (_max<meta::common_type<prev, curr>, next...>{}(
                    std::forward<F>(func),
                    std::forward<R>(rhs),  // forward R
                    std::forward<Ts>(rest)...
                ));
            } else {
                return (_max<meta::common_type<prev, curr>, next...>{}(
                    std::forward<F>(func),
                    std::forward<T>(max),  // retain max
                    std::forward<Ts>(rest)...
                ));
            }
        }
    };
    template <typename F, typename... Ts>
    using max = _max<Ts...>::template traits<F>;

    /* Apply a boolean less-than predicate over the argument to implement a `minmax()`
    function call. */
    template <typename out, typename...>
    struct _minmax {
        template <typename F>
        struct traits {
            using type = std::pair<meta::remove_rvalue<out>, meta::remove_rvalue<out>>;
            static constexpr bool enable = true;
            static constexpr bool nothrow = true;
        };
        template <typename F, typename T1, typename T2>
        static constexpr traits<F>::type operator()(
            F&&,
            T1&& min,
            T2&& max
        ) noexcept(
            meta::nothrow::constructible_from<typename traits<F>::type, T1, T2>
        ) {
            return {std::forward<T1>(min), std::forward<T2>(max)};
        }
    };
    template <typename prev, typename curr, typename... next>
    struct _minmax<prev, curr, next...> {
        template <typename F>
        struct traits {
            using type = void;
            static constexpr bool enable = false;
            static constexpr bool nothrow = false;
        };
        template <meta::invocable<prev, curr> F>
            requires (meta::explicitly_convertible_to<
                meta::invoke_type<F, prev, curr>,
                bool
            >)
        struct traits<F> {
            using recur =
                _minmax<meta::common_type<prev, curr>, next...>::template traits<F>;
            using type = recur::type;
            static constexpr bool enable = recur::enable;
            static constexpr bool nothrow =
                meta::nothrow::invocable<F, prev, curr> &&
                meta::nothrow::explicitly_convertible_to<
                    meta::invoke_type<F, prev, curr>,
                    bool
                > &&
                meta::nothrow::convertible_to<curr, type> &&
                recur::nothrow;
        };
        template <typename F, typename T1, typename T2, typename R, typename... Ts>
        static constexpr decltype(auto) operator()(
            F&& func,
            T1&& min,
            T2&& max,
            R&& rhs,
            Ts&&... rest
        ) noexcept(traits<F>::nothrow) {
            // R < min
            if (std::forward<F>(func)(std::forward<R>(rhs), std::forward<T1>(min))) {
                return (_minmax<meta::common_type<prev, curr>, next...>{}(
                    std::forward<F>(func),
                    std::forward<R>(rhs),  // forward R
                    std::forward<T2>(max),  // retain max
                    std::forward<Ts>(rest)...
                ));

            // max < R
            } else if (std::forward<F>(func)(std::forward<T2>(max), std::forward<R>(rhs))) {
                return (_minmax<meta::common_type<prev, curr>, next...>{}(
                    std::forward<F>(func),
                    std::forward<T1>(min),  // retain min
                    std::forward<R>(rhs),  // forward R
                    std::forward<Ts>(rest)...
                ));

            // min <= R <= max
            } else {
                return (_minmax<meta::common_type<prev, curr>, next...>{}(
                    std::forward<F>(func),
                    std::forward<T1>(min),  // retain min
                    std::forward<T2>(max),  // retain max
                    std::forward<Ts>(rest)...
                ));
            }
        }
    };
    template <typename F, typename... Ts>
    using minmax = _minmax<Ts...>::template traits<F>;

    /* Helper entry point for minmax(), which repeats the first element of the tuple
    before passing to _minmax proper. */
    template <typename... Ts>
    struct minmax_helper {
        template <typename F, typename First, typename... Rest>
        static constexpr decltype(auto) operator()(
            F&& func,
            First&& first,
            Rest&&... rest
        )
            noexcept(minmax<F, Ts...>::nothrow)
            requires(minmax<F, Ts...>::enable)
        {
            return _minmax<Ts...>{}(
                std::forward<F>(func),
                std::forward<First>(first),
                std::forward<First>(first),
                std::forward<Rest>(rest)...
            );
        }
    };

}


/// TODO: if the object has an ssize() method, call that instead of std::ranges::ssize().
/// Same with an adl ssize() method


/* Get the length of an arbitrary sequence in constant time as a signed integer.
Equivalent to calling `std::ranges::ssize(range)`. */
template <meta::has_ssize Range>
[[nodiscard]] constexpr decltype(auto) len(Range&& r)
    noexcept(meta::nothrow::has_ssize<Range>)
{
    return (std::ranges::ssize(std::forward<Range>(r)));
}


/* Get the length of a tuple-like container as a compile-time constant signed integer.
Equivalent to evaluating `meta::tuple_size<T>` on the given type `T`. */
template <meta::tuple_like T> requires (!meta::has_ssize<T>)
[[nodiscard]] constexpr ssize_t len(T&& r) noexcept {
    return ssize_t(meta::tuple_size<T>);
}


/* Get the distance between two iterators as a signed integer.  Equivalent to calling
`std::ranges::distance(begin, end)`.  This may run in O(n) time if the iterators do
not support constant-time distance measurements. */
template <meta::iterator Begin, meta::sentinel_for<Begin> End>
[[nodiscard]] constexpr decltype(auto) len(Begin&& begin, End&& end)
    noexcept(noexcept(std::ranges::distance(
        std::forward<Begin>(begin),
        std::forward<End>(end)
    )))
{
    return (std::ranges::distance(
        std::forward<Begin>(begin),
        std::forward<End>(end)
    ));
}


/* Produce a simple range starting at a default-constructed instance of `End` (zero if
`End` is an integer type), similar to Python's built-in `range()` operator.  This is
equivalent to a  `std::views::iota()` call under the hood. */
template <meta::default_constructible Stop>
[[nodiscard]] constexpr decltype(auto) range(Stop&& stop)
    noexcept(noexcept(std::views::iota(Stop{}, std::forward<Stop>(stop))))
    requires(requires{std::views::iota(Stop{}, std::forward<Stop>(stop));})
{
    return (std::views::iota(Stop{}, std::forward<Stop>(stop)));
}


/* Produce a simple range from `start` to `stop`, similar to Python's built-in
`range()` operator.  This is equivalent to a `std::views::iota()` call under the
hood. */
template <typename Start, typename Stop>
[[nodiscard]] constexpr decltype(auto) range(Start&& start, Stop&& stop)
    noexcept(noexcept(
        std::views::iota(std::forward<Start>(start), std::forward<Stop>(stop))
    ))
    requires(requires{
        std::views::iota(std::forward<Start>(start), std::forward<Stop>(stop));
    })
{
    return (std::views::iota(std::forward<Start>(start), std::forward<Stop>(stop)));
}


/* Produce a simple range object that encapsulates a `start` and `stop` iterator as a
range adaptor.  This is equivalent to a `std::ranges::subrange()` call under the
hood. */
template <meta::iterator Start, meta::sentinel_for<Start> Stop>
[[nodiscard]] constexpr decltype(auto) range(Start&& start, Stop&& stop)
    noexcept(noexcept(
        std::ranges::subrange(std::forward<Start>(start), std::forward<Stop>(stop))
    ))
    requires(
        !requires{
            std::views::iota(std::forward<Start>(start), std::forward<Stop>(stop));
        } &&
        requires{
            std::ranges::subrange(std::forward<Start>(start), std::forward<Stop>(stop));
        }
    )
{
    return (std::ranges::subrange(std::forward<Start>(start), std::forward<Stop>(stop)));
}


#ifdef __cpp_lib_ranges_stride

    /* Produce a simple range object from `start` to `stop` in intervals of `step`,
    similar to Python's built-in `range()` operator.  This is equivalent to a
    `std::views::iota() | std::views::stride()` call under the hood. */
    template <typename Start, typename Stop, typename Step>
    [[nodiscard]] constexpr decltype(auto) range(Start&& start, Stop&& stop, Step&& step)
        noexcept(noexcept(
            std::views::iota(std::forward<Start>(start), std::forward<Stop>(stop)) |
            std::views::stride(std::forward<Step>(step))
        ))
        requires(requires{
            std::views::iota(std::forward<Start>(start), std::forward<Stop>(stop)) |
            std::views::stride(std::forward<Step>(step));
        })
    {
        return (std::views::iota(start, stop) | std::views::stride(step));
    }

    /* Produce a simple range object that encapsulates a `start` and `stop` iterator
    with a stride of `step` as a range adaptor.  This is equivalent to a
    `std::ranges::subrange() | std::views::stride()` call under the hood. */
    template <meta::iterator Start, meta::sentinel_for<Start> Stop, typename Step>
    [[nodiscard]] constexpr decltype(auto) range(
        Start&& start,
        Stop&& stop,
        Step&& step
    )
        noexcept(noexcept(
            std::ranges::subrange(std::forward<Start>(start), std::forward<Stop>(stop)) |
            std::views::stride(std::forward<Step>(step))
        ))
        requires(
            !requires{
                std::views::iota(std::forward<Start>(start), std::forward<Stop>(stop)) |
                std::views::stride(std::forward<Step>(step));
            } &&
            requires{
                std::ranges::subrange(std::forward<Start>(start), std::forward<Stop>(stop)) |
                std::views::stride(std::forward<Step>(step));
            }
        )
    {
        return (std::ranges::subrange(start, stop) | std::views::stride(step));
    }

#endif


/* Produce a view over a reverse iterable range that can be used in range-based for
loops.  This is equivalent to a `std::views::reverse()` call under the hood. */
template <meta::reverse_iterable T>
[[nodiscard]] constexpr decltype(auto) reversed(T&& r)
    noexcept(noexcept(std::views::reverse(std::views::all(std::forward<T>(r)))))
    requires(requires{std::views::reverse(std::views::all(std::forward<T>(r)));})
{
    return (std::views::reverse(std::views::all(std::forward<T>(r))));
}


#ifdef __cpp_lib_ranges_enumerate

    /* Produce a view over a given range that yields tuples consisting of each item's
    index and ordinary value_type.  This is equivalent to a `std::views::enumerate()` call
    under the hood, but is easier to remember, and closer to Python syntax. */
    template <meta::iterable T>
    [[nodiscard]] constexpr decltype(auto) enumerate(T&& r)
        noexcept(noexcept(std::views::enumerate(std::views::all(std::forward<T>(r)))))
        requires(requires{std::views::enumerate(std::views::all(std::forward<T>(r)));})
    {
        return (std::views::enumerate(std::views::all(std::forward<T>(r))));
    }

#endif


/* Combine several ranges into a view that yields tuple-like values consisting of the
`i` th element of each range.  This is equivalent to a `std::views::zip()` call under
the hood. */
template <meta::iterable... Ts>
[[nodiscard]] constexpr decltype(auto) zip(Ts&&... rs)
    noexcept(noexcept(std::views::zip(std::views::all(std::forward<Ts>(rs))...)))
    requires(requires{std::views::zip(std::views::all(std::forward<Ts>(rs))...);})
{
    return (std::views::zip(std::views::all(std::forward<Ts>(rs))...));
}


/* Returns true if and only if the predicate function returns true for one or more of
the arguments.  The predicate must be default-constructible and invocable with
the argument types.  It defaults to a contextual boolean conversion, according to
the conversion semantics of the argument types. */
template <
    meta::default_constructible F = impl::ExplicitConvertTo<bool>,
    typename... Args
>
    requires (sizeof...(Args) > 1)
[[nodiscard]] constexpr bool any(Args&&... args)
    noexcept(((
        meta::nothrow::invocable<F, Args> &&
        meta::nothrow::explicitly_convertible_to<
            meta::invoke_type<F, Args>,
            bool
        >
    ) && ...))
    requires(((
        meta::invocable<F, Args> &&
        meta::explicitly_convertible_to<
            meta::invoke_type<F, Args>,
            bool
        >
    ) && ...))
{
    return impl::truthy<Args...>::any(F{}, std::forward<Args>(args)...);
}


/* Returns true if and only if the predicate function returns true for one or more
elements of a tuple-like container.  The predicate must be default-constructible and
invocable with the tuple's element types.  It defaults to a contextual boolean
conversion, according to the conversion semantics of the element types. */
template <
    meta::default_constructible F = impl::ExplicitConvertTo<bool>,
    meta::tuple_like T
>
    requires (!meta::iterable<T>)
[[nodiscard]] constexpr bool any(T&& t)
    noexcept(noexcept(meta::tuple_types<T>::template eval<impl::truthy>::any(
        F{},
        std::forward<T>(t)
    )))
    requires(requires{meta::tuple_types<T>::template eval<impl::truthy>::any(
        F{},
        std::forward<T>(t)
    );})
{
    return meta::tuple_types<T>::template eval<impl::truthy>::any(
        F{},
        std::forward<T>(t)
    );;
}


/* Returns true if and only if the predicate function returns true for one ore more
elements of a range.  The predicate must be default-constructible and invocable with
the range's yield type.  It defaults to a contextual boolean conversion, according to
the conversion semantics of the yield type.  This is equivalent to a
`std::ranges::any_of()` call under the hood. */
template <
    meta::default_constructible F = impl::ExplicitConvertTo<bool>,
    meta::iterable T
>
    requires (
        meta::invocable<F, meta::yield_type<T>> &&
        meta::explicitly_convertible_to<meta::invoke_type<F, meta::yield_type<T>>, bool>
    )
[[nodiscard]] constexpr bool any(T&& r)
    noexcept(noexcept(std::ranges::any_of(std::forward<T>(r), F{})))
    requires(requires{std::ranges::any_of(std::forward<T>(r), F{});})
{
    return std::ranges::any_of(std::forward<T>(r), F{});
}


/* Returns true if and only if the predicate function returns true for all of the
arguments.  The predicate must be default-constructible and invocable with the argument
types.  It defaults to a contextual boolean conversion, according to the conversion
semantics of the argument types. */
template <
    meta::default_constructible F = impl::ExplicitConvertTo<bool>,
    typename... Args
>
    requires (sizeof...(Args) > 1)
[[nodiscard]] constexpr bool all(Args&&... args)
    noexcept(((
        meta::nothrow::invocable<F, Args> &&
        meta::nothrow::explicitly_convertible_to<
            meta::invoke_type<F, Args>,
            bool
        >
    ) && ...))
    requires(((
        meta::invocable<F, Args> &&
        meta::explicitly_convertible_to<
            meta::invoke_type<F, Args>,
            bool
        >
    ) && ...))
{
    return impl::truthy<Args...>::all(F{}, std::forward<Args>(args)...);
}


/* Returns true if and only if the predicate function returns true for all elements of
a tuple-like container.  The predicate must be default-constructible and invocable with
the tuple's element types.  It defaults to a contextual boolean conversion, according
to the conversion semantics of the element types. */
template <
    meta::default_constructible F = impl::ExplicitConvertTo<bool>,
    meta::tuple_like T
>
    requires (!meta::iterable<T>)
[[nodiscard]] constexpr bool all(T&& t)
    noexcept(noexcept(meta::tuple_types<T>::template eval<impl::truthy>::all(
        F{},
        std::forward<T>(t)
    )))
    requires(requires{meta::tuple_types<T>::template eval<impl::truthy>::all(
        F{},
        std::forward<T>(t)
    );})
{
    return meta::tuple_types<T>::template eval<impl::truthy>::all(
        F{},
        std::forward<T>(t)
    );
}


/* Returns true if and only if the predicate function returns true for all elements of
a range.  The predicate must be default-constructible and invocable with the range's
yield type.  It defaults to a contextual boolean conversion, according to the
conversion semantics of the yield type.  This is equivalent to a
`std::ranges::all_of()` call under the hood. */
template <
    meta::default_constructible F = impl::ExplicitConvertTo<bool>,
    meta::iterable T
>
    requires (
        meta::invocable<F, meta::yield_type<T>> &&
        meta::explicitly_convertible_to<meta::invoke_type<F, meta::yield_type<T>>, bool>
    )
[[nodiscard]] constexpr bool all(T&& r)
    noexcept(noexcept(std::ranges::all_of(std::forward<T>(r), F{})))
    requires(requires{std::ranges::all_of(std::forward<T>(r), F{});})
{
    return std::ranges::all_of(std::forward<T>(r), F{});
}


/* Apply a pairwise reduction function over the arguments from left to right, returning
the accumulated result.  Formally evaluates to a recursive call chain of the form
`F(F(F(F(x_1, x_2), x_3), ...), x_n)`, where `F` is the reduction function and `x_i`
are the individual arguments.  The return type is deduced as the common type
between each invocation, assuming one exists.

This is effectively a generalization of the Python standard library `min()`, `max()`,
`sum()`, and similar functions, which describe specializations of this method for
particular reduction functions.  User-defined reductions can be provided as a template
parameter to inject custom behavior, as long as it is default constructible and
invocable with each pair of arguments.  The algorithm will fail to compile if any of
these requirements are not met. */
template <meta::default_constructible F, typename... Ts>
    requires (sizeof...(Ts) > 1)
[[nodiscard]] constexpr decltype(auto) fold_left(Ts&&... args)
    noexcept(impl::fold_left<F, Ts...>::nothrow)
    requires(impl::fold_left<F, Ts...>::enable)
{
    return (impl::_fold_left<Ts...>{}(F{}, std::forward<Ts>(args)...));
}


/* Apply a pairwise reduction function over a non-empty, tuple-like container that is
indexable via `get<I>()` (as a member method, ADL function, or `std::get<I>()`) from
left to right, returning the accumulated result.  Formally evaluates to a recursive
call chain of the form `F(F(F(F(x_1, x_2), x_3), ...), x_n)`, where `F` is the
reduction function and `x_i` are the tuple elements.  The return type deduces to the
common type between each invocation, assuming one exists.

This is effectively a generalization of the Python standard library `min()`, `max()`,
`sum()`, and similar functions, which describe specializations of this method for
particular reduction functions.  User-defined reductions can be provided as a template
parameter to inject custom behavior, as long as it is default constructible and
invocable with each pair of elements.  The algorithm will fail to compile if any of
these requirements are not met. */
template <meta::default_constructible F, meta::tuple_like T>
    requires (meta::tuple_size<T> > 0)
[[nodiscard]] constexpr decltype(auto) fold_left(T&& t)
    noexcept(meta::nothrow::apply<
        typename meta::tuple_types<T>::template eval<impl::_fold_left>,
        F,
        T
    >)
    requires(meta::apply<
        typename meta::tuple_types<T>::template eval<impl::_fold_left>,
        F,
        T
    >)
{
    return (apply(
        typename meta::tuple_types<T>::template eval<impl::_fold_left>{},
        F{},
        std::forward<T>(t)
    ));
}


/* Apply a pairwise reduction function over an iterable range from left to right,
returning the accumulated result.  Formally evaluates to a recursive call chain of the
form `F(F(F(F(x_1, x_2), x_3), ...), x_n)`, where `F` is the reduction function and
`x_i` are the elements of the range.  The return type deduces to `Optional<T>`, where
`T` is the common type between each invocation, assuming one exists.  The empty state
corresponds to an empty range, which cannot be reduced.

This is effectively a generalization of the Python standard library `min()`, `max()`,
`sum()`, and similar functions, which describe specializations of this method for
particular reduction functions.  User-defined reductions can be provided as a template
parameter to inject custom behavior, as long as it is default constructible and
invocable with each pair of elements.  The algorithm will fail to compile if any of
these requirements are not met. */
template <meta::default_constructible F, meta::iterable T>
    requires (!meta::tuple_like<T>)
[[nodiscard]] constexpr auto fold_left(T&& r)
    noexcept(
        noexcept(bool(std::ranges::begin(r) == std::ranges::end(r))) &&
        noexcept(Optional<decltype(std::ranges::fold_left(
            std::ranges::begin(r),
            std::ranges::end(r),
            *std::ranges::begin(r),
            F{}
        ))>(std::ranges::fold_left(
            std::ranges::begin(r),
            std::ranges::end(r),
            *std::ranges::begin(r),
            F{}
        )))
    )
    -> Optional<decltype(std::ranges::fold_left(
        std::ranges::begin(r),
        std::ranges::end(r),
        *std::ranges::begin(r),
        F{}
    ))>
    requires(requires{
        { std::ranges::begin(r) == std::ranges::end(r) }
            -> meta::explicitly_convertible_to<bool>;
        { std::ranges::fold_left(
            std::ranges::begin(r),
            std::ranges::end(r),
            *std::ranges::begin(r),
            F{}
        ) } -> meta::convertible_to<
            Optional<decltype(std::ranges::fold_left(
                std::ranges::begin(r),
                std::ranges::end(r),
                *std::ranges::begin(r),
                F{}
            ))>
        >;
    })
{
    auto it = std::ranges::begin(r);
    auto end = std::ranges::end(r);
    if (it == end) {
        return None;
    }
    return [](auto&& init, auto& it, auto& end) {
        ++it;
        return std::ranges::fold_left(
            it,
            end,
            std::forward<decltype(init)>(init),
            F{}
        );
    }(*it, it, end);
}


/* Apply a pairwise reduction function over the arguments from left to right, returning
the accumulated result.  Formally evaluates to a recursive call chain of the form
`F(x_1, F(x_2, F(..., F(x_n-1, x_n)))`, where `F` is the reduction function and `x_i`
are the individual arguments.  The return type is deduced as the common type
between each invocation, assuming one exists.

This is effectively a generalization of the Python standard library `min()`, `max()`,
`sum()`, and similar functions, which describe specializations of this method for
particular reduction functions.  User-defined reductions can be provided as a template
parameter to inject custom behavior, as long as it is default constructible and
invocable with each pair of arguments.  The algorithm will fail to compile if any of
these requirements are not met. */
template <meta::default_constructible F, typename... Ts>
    requires (sizeof...(Ts) > 1)
[[nodiscard]] constexpr decltype(auto) fold_right(Ts&&... args)
    noexcept(impl::fold_right<F, Ts...>::nothrow)
    requires(impl::fold_right<F, Ts...>::enable)
{
    return (impl::_fold_right<Ts...>{}(F{}, std::forward<Ts>(args)...));
}


/* Apply a pairwise reduction function over a non-empty, tuple-like container that is
indexable via `get<I>()` (as a member method, ADL function, or `std::get<I>()`) from
left to right, returning the accumulated result.  Formally evaluates to a recursive
call chain of the form `F(x_1, F(x_2, F(..., F(x_n-1, x_n)))`, where `F` is the
reduction function and `x_i` are the tuple elements.  The return type deduces to the
common type between each invocation, assuming one exists.

This is effectively a generalization of the Python standard library `min()`, `max()`,
`sum()`, and similar functions, which describe specializations of this method for
particular reduction functions.  User-defined reductions can be provided as a template
parameter to inject custom behavior, as long as it is default constructible and
invocable with each pair of elements.  The algorithm will fail to compile if any of
these requirements are not met. */
template <meta::default_constructible F, meta::tuple_like T>
    requires (meta::tuple_size<T> > 0)
[[nodiscard]] constexpr decltype(auto) fold_right(T&& t)
    noexcept(meta::nothrow::apply<
        typename meta::tuple_types<T>::template eval<impl::_fold_right>,
        F,
        T
    >)
    requires(meta::apply<
        typename meta::tuple_types<T>::template eval<impl::_fold_right>,
        F,
        T
    >)
{
    return (apply(
        typename meta::tuple_types<T>::template eval<impl::_fold_right>{},
        F{},
        std::forward<T>(t)
    ));
}


#ifdef __cpp_lib_ranges_fold

    /* Apply a pairwise reduction function over an iterable range from left to right,
    returning the accumulated result.  Formally evaluates to a recursive call chain of
    the form `F(x_1, F(x_2, F(..., F(x_n-1, x_n)))`, where `F` is the reduction
    function and `x_i` are the elements of the range.  The return type deduces to
    `Optional<T>`, where `T` is the common type between each invocation, assuming one
    exists.  The empty state corresponds to an empty range, which cannot be reduced.

    This is effectively a generalization of the Python standard library `min()`,
    `max()`, `sum()`, and similar functions, which describe specializations of this
    method for particular reduction functions.  User-defined reductions can be provided
    as a template parameter to inject custom behavior, as long as it is default
    constructible and invocable with each pair of elements.  The algorithm will fail to
    compile if any of these requirements are not met. */
    template <meta::default_constructible F, meta::iterable T>
        requires (!meta::tuple_like<T>)
    [[nodiscard]] constexpr auto fold_right(T&& r)
        noexcept(
            noexcept(bool(std::ranges::begin(r) == std::ranges::end(r))) &&
            noexcept(Optional<decltype(std::ranges::fold_right(
                std::ranges::begin(r),
                std::ranges::end(r),
                *std::ranges::begin(r),
                F{}
            ))>(std::ranges::fold_right(
                std::ranges::begin(r),
                std::ranges::end(r),
                *std::ranges::begin(r),
                F{}
            )))
        )
        -> Optional<decltype(std::ranges::fold_right(
            std::ranges::begin(r),
            std::ranges::end(r),
            *std::ranges::begin(r),
            F{}
        ))>
        requires(requires{
            { std::ranges::begin(r) == std::ranges::end(r) }
                -> meta::explicitly_convertible_to<bool>;
            { std::ranges::fold_right(
                std::ranges::begin(r),
                std::ranges::end(r),
                *std::ranges::begin(r),
                F{}
            ) } -> meta::convertible_to<
                Optional<decltype(std::ranges::fold_right(
                    std::ranges::begin(r),
                    std::ranges::end(r),
                    *std::ranges::begin(r),
                    F{}
                ))>
            >;
        })
    {
        auto it = std::ranges::begin(r);
        auto end = std::ranges::end(r);
        if (it == end) {
            return None;
        }
        return [](auto&& init, auto& it, auto& end) {
            ++it;
            return std::ranges::fold_right(
                it,
                end,
                std::forward<decltype(init)>(init),
                F{}
            );
        }(*it, it, end);
    }

#endif


/* Left-fold to obtain the minimum value over a sequence of arguments.  This is similar
to a `fold_left<F>(...)` call, except that `F` is expected to be a boolean predicate
corresponding to a less-than comparison between each pair of arguments, and conversion
to a common type is deferred until the end of the fold.  The return type is deduced as
the common type for all elements, assuming such a type exists.

User-defined predicates can be provided to customize the comparison as long as they are
default constructible, invocable with each pair of arguments, and return a contextually
convertible boolean value.  The function will fail to compile if any of these
requirements are not met. */
template <meta::default_constructible F = impl::Less, typename... Ts>
    requires (sizeof...(Ts) > 1)
[[nodiscard]] constexpr decltype(auto) min(Ts&&... args)
    noexcept(impl::min<F, Ts...>::nothrow)
    requires(impl::min<F, Ts...>::enable)
{
    return (impl::_min<Ts...>{}(F{}, std::forward<Ts>(args)...));
}


/* Left-fold to obtain the minimum value within a non-empty, tuple-like container that
is indexable via `get<I>()` (as a member method, ADL function, or `std::get<I>()`).
This is similar to a `fold_left<F>(...)` call, except that `F` is expected to be a
boolean predicate corresponding to a less-than comparison between each pair of
elements, and conversion to a common type is deferred until the end of the fold.  The
return type is deduced as the common type for all elements, assuming such a type
exists.

User-defined predicates can be provided to customize the comparison as long as they are
default constructible, invocable with each pair of elements, and return a contextually
convertible boolean value.  The function will fail to compile if any of these
requirements are not met. */
template <meta::default_constructible F = impl::Less, meta::tuple_like T>
    requires (meta::tuple_size<T> > 0)
[[nodiscard]] constexpr decltype(auto) min(T&& t)
    noexcept(meta::nothrow::apply<
        typename meta::tuple_types<T>::template eval<impl::_min>,
        F,
        T
    >)
    requires(meta::apply<
        typename meta::tuple_types<T>::template eval<impl::_min>,
        F,
        T
    >)
{
    return (apply(
        typename meta::tuple_types<T>::template eval<impl::_min>{},
        F{},
        std::forward<T>(t)
    ));
}


/* Left-fold to obtain the minimum value within an iterable range.  This is similar to
a `fold_left<F>(...)` call, except that `F` is expected to be a boolean predicate
corresponding to a less-than comparison between each pair of elements, and conversion
to a common type is deferred until the end of the fold.  The return type deduces to
`Optional<T>`, where `T` is the common type between each invocation, assuming one
exists.  The empty state corresponds to an empty range, which cannot be reduced.

User-defined predicates can be provided to customize the comparison as long as they are
default constructible, invocable with each pair of elements, and return a contextually
convertible boolean value.  The function will fail to compile if any of these
requirements are not met. */
template <meta::default_constructible F = impl::Less, meta::iterable T>
    requires (!meta::tuple_like<T>)
[[nodiscard]] constexpr auto min(T&& r)
    noexcept(
        noexcept(bool(std::ranges::empty(r))) &&
        noexcept(Optional<decltype(std::ranges::min(std::forward<T>(r), F{}))>(
            std::ranges::min(std::forward<T>(r), F{})
        ))
    )
    -> Optional<decltype(std::ranges::min(std::forward<T>(r), F{}))>
    requires(requires{
        { std::ranges::empty(r) } -> meta::explicitly_convertible_to<bool>;
        { std::ranges::min(std::forward<T>(r), F{}) } -> meta::convertible_to<
            Optional<decltype(std::ranges::min(std::forward<T>(r), F{}))>
        >;
    })
{
    if (std::ranges::empty(r)) {
        return None;
    }
    return std::ranges::min(std::forward<T>(r), F{});
}


/* Left-fold to obtain the maximum value over a sequence of arguments.  This is similar
to a `fold_left<F>(...)` call, except that `F` is expected to be a boolean predicate
corresponding to a less-than comparison between each pair of arguments, and conversion
to a common type is deferred until the end of the fold.  The return type is deduced as
the common type for all elements, assuming such a type exists.

User-defined predicates can be provided to customize the comparison as long as they are
default constructible, invocable with each pair of arguments, and return a contextually
convertible boolean value.  The function will fail to compile if any of these
requirements are not met. */
template <meta::default_constructible F = impl::Less, typename... Ts>
    requires (sizeof...(Ts) > 1)
[[nodiscard]] constexpr decltype(auto) max(Ts&&... args)
    noexcept(impl::max<F, Ts...>::nothrow)
    requires(impl::max<F, Ts...>::enable)
{
    return (impl::_max<Ts...>{}(F{}, std::forward<Ts>(args)...));
}


/* Left-fold to obtain the maximum value within a non-empty, tuple-like container that
is indexable via `get<I>()` (as a member method, ADL function, or `std::get<I>()`).
This is similar to a `fold_left<F>(...)` call, except that `F` is expected to be a
boolean predicate corresponding to a less-than comparison between each pair of
elements, and conversion to a common type is deferred until the end of the fold.  The
return type is deduced as the common type for all elements, assuming such a type
exists.

User-defined predicates can be provided to customize the comparison as long as they are
default constructible, invocable with each pair of elements, and return a contextually
convertible boolean value.  The function will fail to compile if any of these
requirements are not met. */
template <meta::default_constructible F = impl::Less, meta::tuple_like T>
    requires (meta::tuple_size<T> > 0)
[[nodiscard]] constexpr decltype(auto) max(T&& t)
    noexcept(meta::nothrow::apply<
        typename meta::tuple_types<T>::template eval<impl::_max>,
        F,
        T
    >)
    requires(meta::apply<
        typename meta::tuple_types<T>::template eval<impl::_max>,
        F,
        T
    >)
{
    return (apply(
        typename meta::tuple_types<T>::template eval<impl::_max>{},
        F{},
        std::forward<T>(t)
    ));
}


/* Left-fold to obtain the maximum value within an iterable range.  This is similar to
a `fold_left<F>(...)` call, except that `F` is expected to be a boolean predicate
corresponding to a less-than comparison between each pair of elements, and conversion
to a common type is deferred until the end of the fold.  The return type deduces to
`Optional<T>`, where `T` is the common type between each invocation, assuming one
exists.  The empty state corresponds to an empty range, which cannot be reduced.

User-defined predicates can be provided to customize the comparison as long as they are
default constructible, invocable with each pair of elements, and return a contextually
convertible boolean value.  The function will fail to compile if any of these
requirements are not met. */
template <meta::default_constructible F = impl::Less, meta::iterable T>
    requires (!meta::tuple_like<T>)
[[nodiscard]] constexpr auto max(T&& r)
    noexcept(
        noexcept(bool(std::ranges::empty(r))) &&
        noexcept(Optional<decltype(std::ranges::max(std::forward<T>(r), F{}))>(
            std::ranges::max(std::forward<T>(r), F{})
        ))
    )
    -> Optional<decltype(std::ranges::max(std::forward<T>(r), F{}))>
    requires(requires{
        { std::ranges::empty(r) } -> meta::explicitly_convertible_to<bool>;
        { std::ranges::max(std::forward<T>(r), F{}) } -> meta::convertible_to<
            Optional<decltype(std::ranges::max(std::forward<T>(r), F{}))>
        >;
    })
{
    if (std::ranges::empty(r)) {
        return None;
    }
    return std::ranges::max(std::forward<T>(r), F{});
}


/* Left-fold to obtain the minimum and maximum values over a sequence of arguments
simultaneously.  This is similar to a `fold_left<F>(...)` call, except that `F` is
expected to be a boolean predicate corresponding to a less-than comparison between each
pair of arguments, and conversion to a common type is deferred until the end of the
fold.  The return type is `std::pair<T, T>`, where `T` is the common type for all
elements, assuming such a type exists.  The first element of the pair is the minimum
value, and the second element is the maximum value.

User-defined predicates can be provided to customize the comparison as long as they are
default constructible, invocable with each pair of arguments, and return a contextually
convertible boolean value.  The function will fail to compile if any of these
requirements are not met. */
template <meta::default_constructible F = impl::Less, typename... Ts>
    requires (sizeof...(Ts) > 1)
[[nodiscard]] constexpr decltype(auto) minmax(Ts&&... args)
    noexcept(impl::minmax<F, Ts...>::nothrow)
    requires(impl::minmax<F, Ts...>::enable)
{
    return (impl::_minmax<Ts...>{}(
        F{},
        meta::unpack_arg<0>(std::forward<Ts>(args)...),
        std::forward<Ts>(args)...
    ));
}


/* Left-fold to obtain the minimum and maximum values within a non-empty, tuple-like
container that is indexable via `get<I>()` (as a member method, ADL function, or
`std::get<I>()`).  This is similar to a `fold_left<F>(...)` call, except that `F` is
expected to be a boolean predicate corresponding to a less-than comparison between each
pair of elements, and conversion to a common type is deferred until the end of the
fold.  The return type is `std::pair<T, T>`, where `T` is the common type for all
elements, assuming such a type exists.  The first element of the pair is the minimum
value, and the second element is the maximum value.

User-defined predicates can be provided to customize the comparison as long as they are
default constructible, invocable with each pair of elements, and return a contextually
convertible boolean value.  The function will fail to compile if any of these
requirements are not met. */
template <meta::default_constructible F = impl::Less, meta::tuple_like T>
    requires (meta::tuple_size<T> > 0)
[[nodiscard]] constexpr decltype(auto) minmax(T&& t)
    noexcept(meta::nothrow::apply<
        typename meta::tuple_types<T>::template eval<impl::minmax_helper>,
        F,
        T
    >)
    requires(meta::apply<
        typename meta::tuple_types<T>::template eval<impl::minmax_helper>,
        F,
        T
    >)
{
    return (apply(
        typename meta::tuple_types<T>::template eval<impl::minmax_helper>{},
        F{},
        std::forward<T>(t)
    ));
}


/* Left-fold to obtain the minimum and maximum values within an iterable range.  This
is similar to a `fold_left<F>(...)` call, except that `F` is expected to be a boolean
predicate corresponding to a less-than comparison between each pair of elements, and
conversion to a common type is deferred until the end of the fold.  The return type is
`Optional<std::pair<T, T>>`, where `T` is the common type for all elements, assuming
such a type exists.  The first element of the pair is the minimum value, and the second
element is the maximum value.  The empty state corresponds to an empty range, which
cannot be reduced.

User-defined predicates can be provided to customize the comparison as long as they are
default constructible, invocable with each pair of elements, and return a contextually
convertible boolean value.  The function will fail to compile if any of these
requirements are not met. */
template <meta::default_constructible F = impl::Less, meta::iterable T>
    requires (!meta::tuple_like<T>)
[[nodiscard]] constexpr auto minmax(T&& r)
    noexcept(
        noexcept(bool(std::ranges::empty(r))) &&
        noexcept(Optional<decltype(std::ranges::minmax(std::forward<T>(r), F{}))>(
            std::ranges::minmax(std::forward<T>(r), F{})
        ))
    )
    -> Optional<decltype(std::ranges::minmax(std::forward<T>(r), F{}))>
    requires(requires{
        { std::ranges::empty(r) } -> meta::explicitly_convertible_to<bool>;
        { std::ranges::minmax(std::forward<T>(r), F{}) } -> meta::convertible_to<
            Optional<decltype(std::ranges::minmax(std::forward<T>(r), F{}))>
        >;
    })
{
    if (std::ranges::empty(r)) {
        return None;
    }
    return std::ranges::minmax(std::forward<T>(r), F{});
}


namespace meta {

    template <typename Less, typename Begin, typename End>
    concept iter_sortable =
        meta::iterator<Begin> &&
        meta::sentinel_for<End, Begin> &&
        meta::copyable<Begin> &&
        meta::output_iterator<Begin, meta::as_rvalue<meta::dereference_type<Begin>>> &&
        meta::movable<meta::remove_reference<meta::dereference_type<Begin>>> &&
        meta::move_assignable<meta::remove_reference<meta::dereference_type<Begin>>> &&
        meta::destructible<meta::remove_reference<meta::dereference_type<Begin>>> &&
        (
            (
                meta::member_object_of<
                    Less,
                    meta::remove_reference<meta::dereference_type<Begin>>
                > &&
                meta::has_lt<meta::remove_member<Less>, meta::remove_member<Less>>
            ) || (
                meta::member_function_of<
                    Less,
                    meta::remove_reference<meta::dereference_type<Begin>>
                > &&
                meta::invocable<Less, meta::dereference_type<Begin>> &&
                meta::has_lt<
                    meta::invoke_type<Less, meta::dereference_type<Begin>>,
                    meta::invoke_type<Less, meta::dereference_type<Begin>>
                >
            ) || (
                !meta::member<Less> &&
                meta::invocable<
                    meta::as_lvalue<Less>,
                    meta::dereference_type<Begin>,
                    meta::dereference_type<Begin>
                > &&
                meta::explicitly_convertible_to<
                    meta::invoke_type<
                        meta::as_lvalue<Less>,
                        meta::dereference_type<Begin>,
                        meta::dereference_type<Begin>
                    >,
                    bool
                >
            )
        );

    template <typename Less, typename Range>
    concept sortable =
        meta::iterable<Range> &&
        iter_sortable<Less, meta::begin_type<Range>, meta::end_type<Range>>;

}


namespace impl {

    /* A stable, adaptive, k-way merge sort algorithm for arbitrary input/output ranges
    based on work by Gelling, Nebel, Smith, and Wild ("Multiway Powersort", 2023),
    requiring O(n) scratch space for rotations.  A full description of the algorithm
    and its benefits can be found at:

        [1] https://www.wild-inter.net/publications/html/cawley-gelling-nebel-smith-wild-2023.pdf.html

    An earlier, 2-way version of this algorithm is currently in use as the default
    CPython sorting backend for the `sorted()` operator and `list.sort()` method as of
    Python 3.11.  A more complete introduction to that algorithm and how it relates to
    the newer 4-way version can be found in Munro, Wild ("Nearly-Optimal Mergesorts:
    Fast, Practical Sorting Methods That Optimally Adapt to Existing Runs", 2018):

        [2] https://www.wild-inter.net/publications/html/munro-wild-2018.pdf.html

    A full reference implementation for both of these algorithms is available at:

        [3] https://github.com/sebawild/powersort

    The k-way version presented here is adapted from the above implementations with the
    following changes:

        a)  The algorithm works on arbitrary ranges, not just random access iterators.
            If the iterator type does not support O(1) distance calculations, then a
            `std::ranges::distance()` call will be used to determine the initial size
            of the range.  All other iterator operations will be done in constant time.
        b)  A custom `less_than` predicate can be provided to the algorithm, which
            allows for sorting based on custom comparison functions, including
            lambdas, user-defined comparators, and pointers to members.
        c)  Merges are safe against exceptions thrown by the comparison function, and
            will attempt to transfer partially-sorted runs back into the output range
            via RAII.
        d)  Proper move semantics are used to transfer objects to and from the scratch
            space, instead of requiring the type to be default constructible and/or
            copyable.
        e)  The algorithm is generalized to arbitrary `k >= 2`, with a default value of
            4, in accordance with [2].  Higher `k` will asymptotically reduce the
            number of comparisons needed to sort the array by a factor of `log2(k)`, at
            the expense of deeper tournament trees.  There is likely an architecture-
            dependent sweet spot based on the size of the data and the cost of
            comparisons for a given type.  Further investigation is needed to determine
            the optimal value of `k` for a given situation, as well as possibly allow
            dynamic tuning based on the input data.
        f)  All tournament trees are swapped from winner trees to loser trees, which
            reduces branching in the inner loop and simplifies the implementation.
        g)  Sentinel values will be used by default if
            `std::numeric_limits<T>::has_infinity == true`, which maximizes performance
            as demonstrated in [2].

    Otherwise, the algorithm is designed to be a drop-in replacement for `std::sort`
    and `std::stable_sort`, and is generally competitive with or better than those
    algorithms, sometimes by a significant margin if any of the following conditions
    are true:

        1.  The data is already partially sorted, or is naturally ordered in
            ascending/descending runs.
        2.  Data movement and/or comparisons are expensive, such as for strings or
            user-defined types.
        3.  The data has a sentinel value, expressed as
            `std::numeric_limits<T>::infinity()`.

    NOTE: the `min_run` template parameter dictates the minimum run length under
    which insertion sort will be used to grow the run.  [2] sets this to a default of
    24, which is replicated here.  Like `k`, it can be tuned at compile time. */
    template <size_t k = 4, size_t min_run = 24> requires (k >= 2 && min_run > 0)
    struct powersort {
    private:
        template <typename Begin>
        using value_type = meta::remove_reference<meta::dereference_type<Begin>>;
        template <typename Begin>
        using pointer = meta::as_pointer<value_type<Begin>>;

        /* The less-than comparator may be given as either a boolean predicate function
        or a simple pointer to member, which will be wrapped in a boolean predicate. */
        template <typename Less> requires (!meta::rvalue<Less>)
        struct sort_by {
            Less member;

            constexpr bool operator()(auto&& l, auto&& r) const noexcept {
                if constexpr (meta::member_function<Less>) {
                    return ((l.*member)()) < ((r.*member)());
                } else {
                    return (l.*member) < (r.*member);
                }
            }

            template <meta::is<Less> L>
            static constexpr decltype(auto) fn(L&& less_than) noexcept {
                return (std::forward<L>(less_than));
            }

            template <meta::is<Less> L> requires (meta::member<Less>)
            static constexpr sort_by fn(L&& less_than)
                noexcept(meta::nothrow::convertible_to<L, Less>)
            {
                return {std::forward<L>(less_than)};
            }
        };

        /* Scratch space is allocated as a raw buffer using
        `std::allocator<T>::allocate()` and `std::allocator<T>::deallocate()  so as
        not to impose default constructibility on `value_type`, while still allowing
        the sort to be done at compile time via transient allocation. */
        template <typename Begin> requires (!meta::reference<Begin>)
        struct scratch {
            using value_type = powersort::value_type<Begin>;
            using pointer = powersort::pointer<Begin>;
            struct deleter {
                size_t size;
                constexpr void operator()(pointer p) noexcept {
                    std::allocator<value_type>{}.deallocate(p, size);
                }
            };
            using buffer = std::unique_ptr<value_type, deleter>;
            buffer data;

            constexpr scratch() noexcept = default;
            constexpr scratch(size_t size) : data(
                std::allocator<value_type>{}.allocate(size + k + 1),
                deleter{size + k + 1}
            ) {}

            constexpr void allocate(size_t size) {
                data = buffer{
                    std::allocator<value_type>{}.allocate(size + k + 1),
                    deleter{size + k + 1}
                };
            }
        };

        template <typename Begin, typename Less>
        struct run {
            using value_type = powersort::value_type<Begin>;
            using pointer = powersort::pointer<Begin>;
            Begin iter;  // iterator to start of run
            size_t start;  // first index of the run
            size_t stop = start;  // one past last index of the run
            size_t power = 0;

            /* Detect the next run beginning at `start` and not exceeding `size`.
            `iter` is an iterator to the start index, which will be advanced to
            the end of the detected run as an out parameter.

            If the run is strictly decreasing, then it will also be reversed in-place.
            If it is less than the minimum run length, then it will be grown to that
            length or to the end of the range using insertion sort.  After this method
            is called, `stop` will be an index one past the last element of the run.
            
            Forward-only iterators require the scratch space to be allocated early,
            so that it can be used for reversal and insertion sort rotations.  This can
            be avoided in the bidirectional case as an optimization. */
            constexpr void detect(
                Less& less_than,
                Begin& iter,
                size_t size,
                powersort::scratch<Begin>& scratch  // initialized to null
            )
                requires (!meta::bidirectional_iterator<Begin>)
            {
                if (stop < size && ++stop < size) {
                    Begin next = iter;
                    ++next;
                    if (less_than(*next, *iter)) {  // strictly decreasing
                        do {
                            ++iter;
                            ++next;
                        } while (++stop < size && less_than(*next, *iter));
                        ++iter;

                        // otherwise, if the iterator is forward-only, we have to do an
                        // O(2 * n) move into scratch space and then move back.
                        scratch.allocate(size);  // lazy initialization
                        pointer begin = scratch;
                        pointer end = scratch + (stop - start);
                        Begin i = this->iter;
                        while (begin < end) {
                            std::construct_at(begin++, std::move(*i++));
                        }
                        Begin j = this->iter;
                        while (end-- > scratch) {
                            *j = std::move(*(end));
                            destroy(end);
                            ++j;
                        }

                    } else {  // weakly increasing
                        do {
                            ++iter;
                            ++next;
                        } while (++stop < size && !less_than(*next, *iter));
                        ++iter;
                    }
                }

                if (stop == size) {
                    return;
                }
                if (scratch.data == nullptr) {
                    scratch.allocate(size);  // lazy initialization
                }

                // Grow the run to the minimum length using insertion sort.  If the
                // iterator is forward-only, then we have to scan the sorted portion
                // from left to right and move it into scratch space to do a proper
                // rotation.
                size_t limit = bertrand::min(start + min_run, size);
                while (stop < limit) {
                    // scan sorted portion for insertion point
                    Begin curr = this->iter;
                    size_t idx = start;
                    while (idx < stop) {
                        // stop at the first element that is strictly greater
                        // than the unsorted element
                        if (less_than(*iter, *curr)) {
                            // move subsequent elements into scratch space
                            Begin temp = curr;
                            pointer p = scratch;
                            pointer p2 = scratch + stop - idx;
                            while (p < p2) {
                                std::construct_at(
                                    p++,
                                    std::move(*temp++)
                                );
                            }

                            // move unsorted element to insertion point
                            *curr++ = std::move(*iter);

                            // move intervening elements back
                            p = scratch;
                            p2 = scratch + stop - idx;
                            while (p < p2) {
                                *curr++ = std::move(*p);
                                destroy(p);
                                ++p;
                            }
                            break;
                        }
                        ++curr;
                        ++idx;
                    }
                    ++iter;
                    ++stop;
                }
            }
        };

        template <meta::bidirectional_iterator Begin, typename Less>
        struct run<Begin, Less> {
            using value_type = powersort::value_type<Begin>;
            using pointer = powersort::pointer<Begin>;
            Begin iter;  // iterator to start of run
            size_t start;  // first index of the run
            size_t stop = start;  // one past last index of the run
            size_t power = 0;

            /* Detect the next run beginning at `start` and not exceeding `size`.
            `iter` is an iterator to the start index, which will be advanced to
            the end of the detected run as an out parameter.

            If the run is strictly decreasing, then it will also be reversed in-place.
            If it is less than the minimum run length, then it will be grown to that
            length or to the end of the range using insertion sort.  After this method
            is called, `stop` will be an index one past the last element of the run. */
            constexpr void detect(Less& less_than, Begin& iter, size_t size) {
                if (stop < size && ++stop < size) {
                    Begin next = iter;
                    ++next;
                    if (less_than(*next, *iter)) {  // strictly decreasing
                        do {
                            ++iter;
                            ++next;
                        } while (++stop < size && less_than(*next, *iter));
                        ++iter;

                        // if the iterator is bidirectional, then we can do an O(n / 2)
                        // pairwise swap
                        std::ranges::reverse(this->iter, iter);

                    } else {  // weakly increasing
                        do {
                            ++iter;
                            ++next;
                        } while (++stop < size && !less_than(*next, *iter));
                        ++iter;
                    }
                }

                // Grow the run to the minimum length using insertion sort.  If the
                // iterator is bidirectional, then we can rotate in-place from right to
                // left to avoid any extra allocations.
                size_t limit = bertrand::min(start + min_run, size);
                while (stop < limit) {
                    // if the unsorted element is less than the previous
                    // element, we need to rotate it into the correct position
                    Begin prev = iter;
                    --prev;
                    if (less_than(*iter, *prev)) {
                        size_t idx = stop;
                        Begin curr = iter;
                        value_type temp = std::move(*curr);

                        // rotate hole to the left until we find a proper
                        // insertion point.
                        while (true) {
                            *curr = std::move(*prev);
                            --curr;
                            try {
                                if (--idx == start) {
                                    break;
                                }
                                --prev;
                                if (!less_than(temp, *prev)) {
                                    break;  // found insertion point
                                }
                            } catch (...) {
                                *curr = std::move(temp);  // fill hole
                                throw;
                            }
                        };

                        // fill hole at insertion point
                        *curr = std::move(temp);
                    }
                    ++iter;
                    ++stop;
                }
            }
        };

        template <typename Begin, typename Less>
            requires (!meta::reference<Begin> && !meta::reference<Less>)
        struct merge_tree {
        private:
            using value_type = powersort::value_type<Begin>;
            using pointer = powersort::pointer<Begin>;
            using run = powersort::run<Begin, Less>;
            using numeric = std::numeric_limits<meta::unqualify<value_type>>;
            static constexpr size_t empty = std::numeric_limits<size_t>::max();

            static constexpr size_t ceil_logk(size_t n) noexcept {
                // fast path where `k` is a power of two
                if constexpr (impl::is_power2(k)) {
                    return (impl::log2(n - 1) / std::countr_zero(k)) + 1;
    
                // otherwise, we repeatedly multiply until we reach or exceed `n`,
                // which is O(log(n)), but negligible compared to the sort.
                } else {
                    size_t m = 0;
                    size_t product = 1;
                    while (product < n) {
                        ++m;
                        if (product > std::numeric_limits<size_t>::max() / k) {
                            break;  // k^m > any representable n
                        }
                        product *= k;
                    }
                    return m;
                }
            }

            static constexpr size_t get_power(
                size_t size,
                const run& curr,
                const run& next
            ) noexcept {
                /// NOTE: this implementation is taken straight from the reference.
                /// The only generalization is the use of `std::countl_zero` instead
                /// of compiler intrinsics, and converting to guaranteed 64-bit
                /// arithmetic for the shift.
                size_t l = curr.start + curr.stop;
                size_t r = next.start + next.stop;
                size_t a = (uint64_t(l) << 30) / size;
                size_t b = (uint64_t(r) << 30) / size;
                return ((std::countl_zero(a ^ b) - 1) >> 1) + 1;
            }

            /* Construction/destruction is done through `std::construct_at()` and
            `std::destroy_at()` so as to allow use in constexpr contexts compared to
            placement new and inplace destructor calls. */
            static constexpr void destroy(pointer p)
                noexcept(meta::nothrow::destructible<value_type>)
                requires(meta::destructible<value_type>)
            {
                if constexpr (!meta::trivially_destructible<value_type>) {
                    std::destroy_at(p);
                }
            }

            /* An exception-safe tournament tree generalized to arbitrary `N >= 2`.  If
            an error occurs during a comparison, then all runs will be transferred back
            into the output range in partially-sorted order via RAII. */
            template <size_t N, bool = numeric::has_infinity> requires (N >= 2)
            struct tournament_tree {
                static constexpr size_t M = N + (N % 2);
                Less& less_than;
                pointer scratch;
                Begin output;
                size_t size;
                std::array<pointer, M> begin;  // begin iterators for each run
                std::array<pointer, M> end;  // end iterators for each run
                std::array<size_t, M - 1> internal;  // internal nodes of tree
                size_t winner;  // leaf index of overall winner

                /// NOTE: the tree is represented as a loser tree, meaning the internal
                /// nodes store the leaf index of the losing run for that subtree, and
                /// the winner is bubbled up to the next level of the tree.  The root
                /// of the tree (runner-up) is always the first element of the
                /// `internal` buffer, and each subsequent level is compressed into the
                /// next 2^i elements, from left to right.  The last layer will be
                /// incomplete if `N` is not a power of two.

                template <meta::is<run>... Runs> requires (sizeof...(Runs) == N)
                constexpr tournament_tree(
                    Less& less_than,
                    pointer scratch,
                    Runs&... runs
                ) :
                    less_than(less_than),
                    scratch(scratch),
                    output(meta::unpack_arg<0>(runs...).iter),
                    size((0 + ... + (runs.stop - runs.start))),
                    begin(get_begin(std::make_index_sequence<N>{}, runs...)),
                    end(begin)
                {
                    construct(runs...);
                }

                /* Perform the merge. */
                constexpr void operator()() {
                    // Initialize the tournament tree
                    //                internal[0]               internal nodes store
                    //            /                \            losing leaf indices.
                    //      internal[1]          internal[2]    Leaf nodes store
                    //      /       \             /       \     scratch iterators
                    //         ...                   ...
                    // begin[0]   begin[1]   begin[2]   begin[3]   ...
                    for (size_t i = 0; i < M; ++i) {
                        winner = i;
                        size_t node = (M - 1) + winner;  // phantom leaf index
                        do {
                            size_t parent = (node - 1) / 2;  // direct parent
                            size_t loser = internal[parent];

                            // parent may be uninitialized, in which case the current
                            // node automatically loses
                            if (loser == empty) {
                                internal[parent] = winner;
                                break;  // no need to check ancestors
                            }

                            // otherwise, if the current winner loses against the
                            // parent, then we swap it and continue bubbling up
                            if (less_than(*begin[loser], *begin[winner])) {
                                internal[parent] = winner;
                                winner = loser;
                            }
                            node = parent;
                        } while (node > 0);
                    }

                    // merge runs according to tournament tree
                    for (size_t i = 0; i < size; ++i) {
                        // move the overall winner into the output range
                        *output++ = std::move(*begin[winner]);
                        destroy(begin[winner]++);

                        // bubble up next winner
                        size_t node = (M - 1) + winner;  // phantom leaf index
                        do {
                            size_t parent = (node - 1) / 2;  // direct parent
                            size_t loser = internal[parent];

                            // if next value in winner loses against the parent, then
                            // we swap them and continue bubbling up
                            if (less_than(*begin[loser], *begin[winner])) {
                                internal[parent] = winner;
                                winner = loser;
                            }
                            node = parent;
                        } while (node > 0);
                    }
                }

                /* If an error occurs during comparison, attempt to move the
                unprocessed portions of each run back into the output range and destroy
                sentinels. */
                constexpr ~tournament_tree() {
                    for (size_t i = 0; i < begin.size(); ++i) {
                        while (begin[i] != end[i]) {
                            *output++ = std::move(*begin[i]);
                            destroy(begin[i]);
                            ++begin[i];
                        }
                        destroy(end[i]);
                    }
                }

            private:

                /* If the number of runs to merge is odd, then the tournament tree will
                have exactly one unbalanced node at the end of the array.  In order to
                correct for this, we insert a phantom sentinel to ensure that every
                internal node has precisely two children.  The sentinel branch will
                just never be chosen. */
                template <size_t... Is, typename... Runs>
                constexpr std::array<pointer, M> get_begin(
                    std::index_sequence<Is...>,
                    Runs&... runs
                ) const {
                    run& first = meta::unpack_arg<0>(runs...);
                    if constexpr (N % 2) {
                        return {
                            (scratch + (runs.start - first.start) + Is)...,
                            (scratch + size + N)  // extra sentinel
                        };
                    } else {
                        return {(scratch + (runs.start - first.start) + Is)...};
                    }
                }

                /* move all runs into scratch space.  Afterwards, the end iterators
                will point to the sentinel values for each run */
                template <size_t I = 0, typename... Runs>
                constexpr void construct(Runs&... runs) {
                    if constexpr (I < M - 1) {
                        internal[I] = empty;  // nodes are initialized to empty value
                    }

                    if constexpr (I < N) {
                        run& r = meta::unpack_arg<I>(runs...);
                        for (size_t i = r.start; i < r.stop; ++i) {
                            std::construct_at(
                                end[I]++,
                                std::move(*r.iter++)
                            );
                        }
                        std::construct_at(
                            end[I],
                            numeric::infinity()
                        );
                        construct<I + 1>(runs...);

                    // if `N` is odd, then we have to insert an additional
                    // sentinel at the end of the scratch space to give each
                    // internal node exactly two children
                    } else if constexpr (I < M) {
                        std::construct_at(
                            end[I],
                            numeric::infinity()
                        );
                    }
                }

            };

            /* A specialized tournament tree for when the underlying type does not
            have a +inf sentinel value to guard comparisons.  Instead, this uses extra
            boundary checks and merges in stages, where `N` steadily decreases as runs
            are fully consumed. */
            template <size_t N> requires (N >= 2)
            struct tournament_tree<N, false> {
                Less& less_than;
                pointer scratch;
                Begin output;
                size_t size;
                std::array<pointer, N> begin;  // begin iterators for each run
                std::array<pointer, N> end;  // end iterators for each run
                std::array<size_t, N - 1> internal;  // internal nodes of tree
                size_t winner;  // leaf index of overall winner
                size_t smallest = std::numeric_limits<size_t>::max();  // length of smallest non-empty run

                /// NOTE: this specialization plays tournaments in `N - 1` distinct
                /// stages, where each stage ends when `smallest` reaches zero.  At
                /// At that point, empty runs are removed, and a smaller tournament
                /// tree is constructed with the remaining runs.  This continues until
                /// `N == 2`, in which case we proceed as for a binary merge.

                /// NOTE: because we can't pad the runs with an extra sentinel if N is
                /// odd, the last node in the `internal` array may be unbalanced, with
                /// only a single child.  This is mitigated by simply omitting that
                /// node and causing the leaf that would have been its only child to
                /// skip it during initialization/update of the tournament tree.

                template <meta::is<run>... Runs> requires (sizeof...(Runs) == N)
                constexpr tournament_tree(
                    Less& less_than,
                    pointer scratch,
                    Runs&... runs
                ) :
                    less_than(less_than),
                    scratch(scratch),
                    output(meta::unpack_arg<0>(runs...).iter),
                    size((0 + ... + (runs.stop - runs.start))),
                    begin{(scratch + (runs.start - meta::unpack_arg<0>(runs...).start))...},
                    end(begin)
                {
                    construct(runs...);
                }

                /* Perform the merge for stage `M`, where `M` indicates the current
                size of the tournament tree. */
                template <size_t M = N>
                constexpr void operator()() {
                    if constexpr (M > 2) {
                        initialize<M>();  // build tournament tree for this stage
                        merge<M>();  // continue until a run is exhausted
                        advance<M>();  // pop empty run for next stage
                        operator()<M - 1>();  // recur

                    // finish with a binary merge
                    } else {
                        while (begin[0] != end[0] && begin[1] != end[1]) {
                            bool less = static_cast<bool>(less_than(*begin[1], *begin[0]));
                            *output++ = std::move(*begin[less]);
                            destroy(begin[less]);
                            ++begin[less];
                        }
                        while (begin[0] != end[0]) {
                            *output++ = std::move(*begin[0]);
                            destroy(begin[0]);
                            ++begin[0];
                        }
                        while (begin[1] != end[1]) {
                            *output++ = std::move(*begin[1]);
                            destroy(begin[1]);
                            ++begin[1];
                        }
                    }
                }

                constexpr ~tournament_tree() {
                    for (size_t i = 0; i < begin.size(); ++i) {
                        while (begin[i] != end[i]) {
                            *output = std::move(*begin[i]);
                            ++output;
                            destroy(begin[i]);
                            ++begin[i];
                        }
                    }
                }

            private:

                /* Move all runs into scratch space, without any extra sentinels.
                Record the minimum length of each run for the the first stage. */
                template <size_t I = 0, typename... Runs>
                constexpr void construct(Runs&... runs) {
                    if constexpr (I < N) {
                        if constexpr (I < N - 1) {
                            internal[I] = empty;  // internal nodes start empty
                        }
                        run& r = meta::unpack_arg<I>(runs...);
                        for (size_t i = r.start; i < r.stop; ++i) {
                            std::construct_at(
                                end[I]++,
                                std::move(*r.iter++)
                            );
                        }
                        smallest = bertrand::min(smallest, r.stop - r.start);
                        construct<I + 1>(runs...);
                    }
                }

                /* Regenerate the tournament tree for the next stage. */
                template <size_t M>
                constexpr void initialize() {
                    for (size_t i = 0; i < M; ++i) {
                        winner = i;
                        size_t node = (M - 1) + winner;  // phantom leaf index
                        do {
                            size_t parent = (node - 1) / 2;  // direct parent
                            size_t loser = internal[parent];

                            // parent may be empty, in which case the current winner
                            // automatically loses
                            if (loser == empty) {
                                internal[parent] = winner;
                                break;  // no need to check ancestors
                            }

                            // otherwise, if the current winner loses against the
                            // parent, then we swap it and continue bubbling up
                            if (less_than(*begin[loser], *begin[winner])) {
                                internal[parent] = winner;
                                winner = loser;
                            }
                            node = parent;
                        } while (node > 0);
                    }
                }

                /* Move the winner of the tournament tree into output and update the
                tree. */
                template <size_t M>
                constexpr void merge() {
                    while (true) {
                        // move the overall winner into the output range
                        *output++ = std::move(*begin[winner]);
                        destroy(begin[winner]++);

                        // we can safely do `smallest` iterations before needing to
                        // check bounds
                        if (--smallest == 0) {
                            // update `smallest` to the minimum length of all non-empty
                            // runs
                            smallest = empty;
                            for (size_t i = 0; i < M; ++i) {
                                smallest = bertrand::min(
                                    smallest,
                                    size_t(end[i] - begin[i])
                                );
                            }

                            // if the result is zero, then it marks the end of the
                            // current stage
                            if (smallest == 0) {
                                break;
                            }
                        }

                        // bubble up next winner
                        size_t node = (M - 1) + winner;  // phantom leaf index
                        do {
                            size_t parent = (node - 1) / 2;  // direct parent
                            size_t loser = internal[parent];

                            // if next value in winner loses against the parent, then
                            // we swap them and continue bubbling up
                            if (less_than(*begin[loser], *begin[winner])) {
                                internal[parent] = winner;
                                winner = loser;
                            }
                            node = parent;
                        } while (node > 0);
                    }
                }

                /* End a merge stage by pruning empty runs, resetting the tournament
                tree, and recomputing the minimum length for the next stage.  */
                template <size_t M>
                constexpr void advance() {
                    smallest = empty;
                    for (size_t i = 0; i < M - 1; ++i) {
                        size_t len = end[i] - begin[i];

                        // if empty, pop from the array and left shift subsequent runs
                        if (!len) {
                            for (size_t j = i; j < M - 1; ++j) {
                                begin[j] = begin[j + 1];
                                end[j] = end[j + 1];
                            }
                            begin[M - 1] = end[M - 1];
                            len = end[i] - begin[i];
                        }

                        // record the minimum length and reset internal nodes
                        smallest = bertrand::min(smallest, len);
                        internal[i] = empty;
                    }
                }
            };

            /* An optimized tournament tree for binary merges with sentinel values. */
            template <>
            struct tournament_tree<2, true> {
                Less& less_than;
                pointer scratch;
                Begin output;
                size_t size;
                std::array<pointer, 2> begin;
                std::array<pointer, 2> end;

                constexpr tournament_tree(
                    Less& less_than,
                    pointer scratch,
                    run& left,
                    run& right
                ) :
                    less_than(less_than),
                    scratch(scratch),
                    output(left.iter),
                    size((left.stop - left.start) + (right.stop - right.start)),
                    begin{scratch, scratch + (left.stop - left.start) + 1},
                    end(begin)
                {
                    for (size_t i = left.start; i < left.stop; ++i) {
                        std::construct_at(
                            end[0]++,
                            std::move(*left.iter++)
                        );
                    }
                    std::construct_at(end[0], numeric::infinity());

                    for (size_t i = right.start; i < right.stop; ++i) {
                        std::construct_at(
                            end[1]++,
                            std::move(*right.iter++)
                        );
                    }
                    std::construct_at(end[1], numeric::infinity());
                }

                constexpr void operator()() {
                    for (size_t i = 0; i < size; ++i) {
                        bool less = static_cast<bool>(less_than(*begin[1], *begin[0]));
                        *output++ = std::move(*begin[less]);
                        destroy(begin[less]);
                        ++begin[less];
                    }
                }

                constexpr ~tournament_tree() {
                    for (size_t i = 0; i < begin.size(); ++i) {
                        while (begin[i] != end[i]) {
                            *output++ = std::move(*begin[i]);
                            destroy(begin[i]);
                            ++begin[i];
                        }
                        destroy(end[i]);
                    }
                }
            };

            /* An optimized tournament tree for binary merges without sentinel values. */
            template <>
            struct tournament_tree<2, false> {
                Less& less_than;
                pointer scratch;
                Begin output;
                size_t size;
                std::array<pointer, 2> begin;
                std::array<pointer, 2> end;

                constexpr tournament_tree(
                    Less& less_than,
                    pointer scratch,
                    run& left,
                    run& right
                ) :
                    less_than(less_than),
                    scratch(scratch),
                    output(left.iter),
                    size((left.stop - left.start) + (right.stop - right.start)),
                    begin{scratch, scratch + (left.stop - left.start)},
                    end(begin)
                {
                    for (size_t i = left.start; i < left.stop; ++i) {
                        std::construct_at(
                            end[0]++,
                            std::move(*left.iter++)
                        );
                    }
                    for (size_t i = right.start; i < right.stop; ++i) {
                        std::construct_at(
                            end[1]++,
                            std::move(*right.iter++)
                        );
                    }
                }

                constexpr void operator()() {
                    while (begin[0] < end[0] && begin[1] < end[1]) {
                        bool less = static_cast<bool>(less_than(*begin[1], *begin[0]));
                        *output++ = std::move(*begin[less]);
                        destroy(begin[less]++);
                    }
                    while (begin[0] < end[0]) {
                        *output++ = std::move(*begin[0]);
                        destroy(begin[0]);
                        ++begin[0];
                    }
                    while (begin[1] < end[1]) {
                        *output++ = std::move(*begin[1]);
                        destroy(begin[1]);
                        ++begin[1];
                    }
                }

                constexpr ~tournament_tree() {
                    for (size_t i = 0; i < begin.size(); ++i) {
                        while (begin[i] != end[i]) {
                            *output++ = std::move(*begin[i]);
                            destroy(begin[i]);
                            ++begin[i];
                        }
                        destroy(end[i]);
                    }
                }
            };

            std::vector<run> stack;

        public:
            /* Allocate stack space for a range of the given size. */
            constexpr merge_tree(size_t size) {
                stack.reserve((k - 1) * (ceil_logk(size) + 1));
            }

            /* Execute the sorting algorithm. */
            constexpr void operator()(
                Less& less_than,
                Begin& begin,
                size_t size,
                run& curr,
                powersort::scratch<Begin>& scratch
            ) {
                // build run stack according to powersort policy
                stack.emplace_back(begin, 0, size);  // power zero as sentinel
                do {
                    run next {begin, curr.stop};
                    if constexpr (meta::bidirectional_iterator<Begin>) {
                        next.detect(less_than, begin, size);
                    } else {
                        next.detect(less_than, begin, size, scratch);
                    }

                    // compute previous run's power with respect to next run
                    curr.power = get_power(size, curr, next);

                    // invariant: powers on stack weakly increase from bottom to top.
                    // If violated, merge runs with equal power into `curr` until
                    // invariant is restored.  Only at most the top `k - 1` runs will
                    // meet this criteria due to the structure of the merge tree and
                    // the definition of the power function.
                    while (stack.back().power > curr.power) {
                        /// NOTE: there is a small discrepancy from the description in
                        /// [1](3.2), where it states that there are at most `k - 1`
                        /// equal powers on the stack at any time.  This is technically
                        /// incorrect, as a pathological case can occur where `k - 1`
                        /// equal-power runs are followed by a run with greater power,
                        /// which does not trigger a merge, only for the next run after
                        /// that to have the same power as the prior `k - 1` runs.
                        /// This would trigger a 2-way merge with the larger power, and
                        /// then we would end up with `k` runs of equal power at the
                        /// top of the stack, against the paper's assurances.  This bug
                        /// does not occur in the provided reference implementation,
                        /// however, seemingly due to a fluke in the if ladder for the
                        /// merges, with the `else` clause converting the pathological
                        /// case into a single `k`-way merge, ignoring the oldest of
                        /// the `k` topmost runs.  This is replicated here by
                        /// terminating the detection loop early when it reaches
                        /// `k - 1`.  None of the other logic is affected, and the
                        /// merge tree remains optimal despite this minor caveat.
                        run* top = &stack.back();
                        size_t i = 1;
                        while (((top - i)->power == top->power) && (i < k - 1)) {
                            ++i;
                        }
                        using F = void(*)(Less&, pointer, std::vector<run>&, run&);
                        using VTable = std::array<F, k - 1>;
                        static constexpr VTable vtable = []<size_t... Is>(
                            std::index_sequence<Is...>
                        ) {
                            // 0: 2-way
                            // 1: 3-way
                            // 2: 4-way
                            // ...
                            // (k-2): k-way
                            return VTable{[]<size_t... Js>(std::index_sequence<Js...>) {
                                // Is... => [0, k - 2] (inclusive)
                                // Js... => [0, Is] (inclusive)
                                return +[](
                                    Less& less_than,
                                    pointer scratch,
                                    std::vector<run>& stack,
                                    run& curr
                                ) {
                                    constexpr size_t I = Is;
                                    Begin temp = stack[stack.size() - I - 1].iter;
                                    tournament_tree<I + 2>{
                                        less_than,
                                        scratch,
                                        stack[stack.size() - I + Js - 1]...,
                                        curr
                                    }();
                                    curr.iter = std::move(temp);
                                    curr.start = stack[stack.size() - I - 1].start;
                                };
                            }(std::make_index_sequence<Is + 1>{})...};
                        }(std::make_index_sequence<k - 1>{});

                        // merge runs with equal power by dispatching to vtable
                        vtable[i - 1](less_than, scratch.data.get(), stack, curr);
                        stack.erase(stack.end() - i, stack.end());  // pop merged runs
                    }

                    // push next run onto stack
                    stack.emplace_back(std::move(curr));
                    curr = std::move(next);
                } while (curr.stop < size);

                // Because runs typically increase in size exponentially as the stack
                // is emptied, we can manually merge the first few such that the stack
                // size is reduced to a multiple of `k - 1`, so that we can do `k`-way
                // merges the rest of the way.  This maximizes the benefit of the
                // tournament tree and minimizes total data movement/comparisons.
                using F = void(*)(Less&, pointer, std::vector<run>&, run&);
                using VTable = std::array<F, k - 1>;
                static constexpr VTable vtable = []<size_t... Is>(
                    std::index_sequence<Is...>
                ) {
                    return VTable{
                        // 0: do nothing
                        +[](
                            Less& less_than,
                            pointer scratch,
                            std::vector<run>& stack,
                            run& curr
                        ) {},
                        // 1: 2-way
                        // 2: 3-way,
                        // 3: 4-way,
                        // ...
                        // (k-2): (k-1)-way
                        []<size_t... Js>(std::index_sequence<Js...>) {
                            /// Is... => [0, k - 2] (inclusive)
                            // Js... => [0, Is] (inclusive)
                            return +[](
                                Less& less_than,
                                pointer scratch,
                                std::vector<run>& stack,
                                run& curr
                            ) {
                                constexpr size_t I = Is;
                                Begin temp = stack[stack.size() - I - 1].iter;
                                tournament_tree<I + 2>{
                                    less_than,
                                    scratch,
                                    stack[stack.size() - I + Js - 1]...,
                                    curr
                                }();
                                curr.iter = std::move(temp);
                                curr.start = stack[stack.size() - I - 1].start;
                                stack.erase(stack.end() - I - 1, stack.end());  // pop merged runs
                            };
                        }(std::make_index_sequence<Is + 1>{})...
                    };
                }(std::make_index_sequence<k - 2>{});

                // vtable is only consulted for the first merge, after which we
                // devolve to purely k-way merges
                vtable[(stack.size() - 1) % (k - 1)](
                    less_than,
                    scratch.data.get(),
                    stack,
                    curr
                );
                [&]<size_t... Is>(std::index_sequence<Is...>) {
                    while (stack.size() > 1) {
                        Begin temp = stack[stack.size() - (k - 1)].iter;
                        tournament_tree<k>{
                            less_than,
                            scratch.data.get(),
                            stack[stack.size() - (k - 1) + Is]...,
                            curr
                        }();
                        curr.iter = std::move(temp);
                        curr.start = stack[stack.size() - (k - 1)].start;
                        stack.erase(stack.end() - (k - 1), stack.end());  // pop merged runs
                    }
                }(std::make_index_sequence<k - 1>{});
            }
        };

    public:
        /* Execute the sort algorithm using unsorted values in the range [begin, end)
        and placing the result back into the same range.

        The `less_than` comparison function is used to determine the order of the
        elements.  It may be a pointer to an arbitrary member of the iterator's value
        type, in which case only that member will be compared.  Otherwise, it must be a
        function with the signature `bool(const T&, const T&)` where `T` is the value
        type of the iterator.  If no comparison function is given, it will default to a
        transparent `<` operator for each element.

        If an exception occurs during a comparison, the input range will be left in a
        valid but unspecified state, and may be partially sorted.  Any other exception
        (e.g. in a move constructor/assignment operator, destructor, or iterator
        operation) may result in undefined behavior. */
        template <typename Begin, typename End, typename Less = impl::Less>
            requires (meta::iter_sortable<Less, Begin, End>)
        static constexpr void operator()(Begin begin, End end, Less&& less_than = {}) {
            // get overall length of range (possibly O(n) if iterators do not support
            // O(1) distance)
            auto length = std::ranges::distance(begin, end);
            if (length < 2) {
                return;  // trivially sorted
            }
            size_t size = size_t(length);
            decltype(auto) compare = sort_by<meta::remove_rvalue<Less>>::fn(
                std::forward<Less>(less_than)
            );

            using B = meta::remove_reference<Begin>;
            using L = meta::remove_reference<decltype(compare)>;

            // identify first run and early return if trivially sorted.  Delay the
            // scratch allocation as long as possible.
            run<B, L> curr {begin, 0};
            if constexpr (meta::bidirectional_iterator<B>) {
                curr.detect(compare, begin, size);
                if (curr.stop == size) {
                    return;
                }
                scratch<B> buffer(size);
                merge_tree<B, L>{size}(compare, begin, size, curr, buffer);
            } else {
                scratch<B> buffer;
                curr.detect(compare, begin, size, buffer);
                if (curr.stop == size) {
                    return;
                }
                if (buffer.data == nullptr) {
                    // if no scratch space was needed for run detection, allocate it
                    // here before proceeding to build the merge tree.
                    buffer.allocate(size);
                }
                merge_tree<B, L>{size}(compare, begin, size, curr, buffer);
            }
        }

        /* An equivalent of the iterator-based call operator that accepts a range and
        uses its `size()` to deduce the length of the range. */
        template <typename Range, typename Less = impl::Less>
            requires (meta::sortable<Less, Range>)
        static constexpr void operator()(Range& range, Less&& less_than = {}) {
            // get overall length of range (possibly O(n) if the range is not
            // explicitly sized and iterators do not support O(1) distance)
            auto length = std::ranges::distance(range);
            if (length < 2) {
                return;  // trivially sorted
            }
            auto begin = std::ranges::begin(range);
            size_t size = size_t(length);
            decltype(auto) compare = sort_by<meta::remove_rvalue<Less>>::fn(
                std::forward<Less>(less_than)
            );

            using B = meta::remove_reference<meta::begin_type<Range>>;
            using L = meta::remove_reference<decltype(compare)>;

            // identify first run and early return if trivially sorted.  Delay the
            // scratch allocation as long as possible.
            run<B, L> curr {begin, 0};
            if constexpr (meta::bidirectional_iterator<B>) {
                curr.detect(compare, begin, size);
                if (curr.stop == size) {
                    return;
                }
                scratch<B> buffer(size);
                merge_tree<B, L>{size}(compare, begin, size, curr, buffer);
            } else {
                scratch<B> buffer;
                curr.detect(compare, begin, size, buffer);
                if (curr.stop == size) {
                    return;
                }
                if (buffer.data == nullptr) {
                    // if no scratch space was needed for run detection, allocate it
                    // here before proceeding to build the merge tree.
                    buffer.allocate(size);
                }
                merge_tree<B, L>{size}(compare, begin, size, curr, buffer);
            }
        }
    };

}


/* Sort an arbitrary range using an optimized, implementation-specific sorting
algorithm.

If the input range has a member `.sort()` method, this function will invoke it with the
given arguments.  Otherwise, it will fall back to a generalized sorting algorithm that
works on arbitrary output ranges, sorting them in-place.  The generalized algorithm
accepts an optional `less_than` comparison function, which can be used to provide
custom sorting criteria.  Such a function can be supplied as a function pointer,
lambda, or custom comparator type with the signature `bool(const T&, const T&)`
where `T` is the value type of the range.  Alternatively, it can also be supplied as
a pointer to a member of the value type or a member function that is callable without
arguments (i.e. a getter), in which case only that member will be considered for
comparison.  If no comparison function is given, it will default to a transparent `<`
operator for each pair of elements.

Currently, the default sorting algorithm is implemented as a heavily optimized,
run-adaptive, stable merge sort variant with a `k`-way powersort policy.  It requires
best case O(n) time due to optimal run detection and worst case O(n log n) time thanks
to a tournament tree that minimizes comparisons.  It needs O(n) extra scratch space,
and can work on arbitrary input ranges.  It is generally faster than `std::sort()` in
most cases, and has far fewer restrictions on its use.  Users should only need to
implement a custom member `.sort()` method if there is a better algorithm for a
particular type (which should be rare), or if they wish to embed it as a member method
for convenience.  In the latter case, users should call the powersort implemtation
directly to guard against infinite recursion, as follows:

```cpp

    template <bertrand::meta::sortable<MyType> Less = bertrand::impl::Less>
    void MyType::sort(Less&& less_than = {}) {
        bertrand::impl::powersort<k, min_run_length>{}(*this, std::forward<Less>(less));
    }

```

The `meta::sortable<Less, MyType>` concept encapsulates all of the requirements for
sorting based on any of the predicates described above, and enforces them at compile
time, while `impl::Less` (equivalent to `std::less<void>`) defaults to a transparent
comparison. */
template <meta::default_constructible Less = impl::Less, typename Range>
    requires (meta::sortable<Less, Range>)
constexpr void sort(Range&& range)
    noexcept(noexcept(impl::powersort{}(std::forward<Range>(range), Less{})))
    requires(
        !requires{std::forward<Range>(range).sort();} &&
        !requires{std::forward<Range>(range).template sort<Less>();}
    )
{
    impl::powersort{}(std::forward<Range>(range), Less{});
}


/* ADL version of `sort()`, which delegates to an implementation-specific
`range.sort<Less>(args...)` method.  All other arguments as well as the return type
(if any) will be perfectly forwarded to that method.  This version accepts an explicit
`Less` function type as a template parameter which will also be forwarded to the ADL
method */
template <meta::default_constructible Less = impl::Less, typename Range, typename... Args>
constexpr decltype(auto) sort(Range&& range, Args&&... args)
    noexcept(noexcept(
        std::forward<Range>(range).template sort<Less>(std::forward<Args>(args)...)
    ))
    requires(
        !requires{std::forward<Range>(range).sort(std::forward<Args>(args)...);} &&
        requires{
            std::forward<Range>(range).template sort<Less>(std::forward<Args>(args)...);
        }
    )
{
    return (std::forward<Range>(range).template sort<Less>(std::forward<Args>(args)...));
}


/* ADL version of `sort()`, which delegates to an implementation-specific
`range.sort<Less>(args...)` method.  All other arguments as well as the return type
(if any) will be perfectly forwarded to that method.  This version does not accept
any template parameters, and only forwards the arguments. */
template <typename Range, typename... Args>
constexpr decltype(auto) sort(Range&& range, Args&&... args)
    noexcept(noexcept(std::forward<Range>(range).sort(std::forward<Args>(args)...)))
    requires(requires{std::forward<Range>(range).sort(std::forward<Args>(args)...);})
{
    return (std::forward<Range>(range).sort(std::forward<Args>(args)...));
}


/* Iterator-based `sort()`, which always uses the fallback powersort implementation.
If the iterators do not support O(1) distance, the length of the range will be
computed in O(n) time before starting the sort algorithm. */
template <meta::default_constructible Less = impl::Less, typename Begin, typename End>
    requires (meta::iter_sortable<Less, Begin, End>)
constexpr void sort(Begin&& begin, End&& end)
    noexcept(noexcept(impl::powersort{}(
        std::forward<Begin>(begin),
        std::forward<End>(end),
        Less{}
    )))
{
    impl::powersort{}(std::forward<Begin>(begin), std::forward<End>(end), Less{});
}


}


namespace std {

    /* Specializing `std::ranges::enable_borrowed_range` ensures that iterators over
    slices are not tied to the lifetime of the slice itself, but rather to that of the
    underlying container. */
    template <bertrand::meta::slice T>
    constexpr bool ranges::enable_borrowed_range<T> = true;

}


#endif