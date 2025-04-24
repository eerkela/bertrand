#ifndef BERTRAND_ITER_H
#define BERTRAND_ITER_H

#include "bertrand/common.h"
#include "bertrand/except.h"
#include "bertrand/union.h"


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
        std::optional<ssize_t> start = std::nullopt,
        std::optional<ssize_t> stop = std::nullopt,
        std::optional<ssize_t> step = std::nullopt
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
    private:
        friend slice;

        constexpr normalized(
            ssize_t start,
            ssize_t stop,
            ssize_t step,
            ssize_t length
        ) noexcept :
            start(start),
            stop(stop),
            step(step),
            length(length)
        {}

    public:
        ssize_t start;
        ssize_t stop;
        ssize_t step;
        ssize_t length;
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

    /* Check to see if applying Python-style wraparound to a compile-time index would
    yield a valid index into a container of a given size.  Returns false if the
    index would be out of bounds after normalizing. */
    template <ssize_t size, ssize_t I>
    concept valid_index = ((I + size * (I < 0)) >= 0) && ((I + size * (I < 0)) < size);

    /* Apply Python-style wraparound to a compile-time index. Fails to compile if the
    index would be out of bounds after normalizing. */
    template <ssize_t size, ssize_t I> requires (valid_index<size, I>)
    constexpr ssize_t normalize_index() noexcept { return I + size * (I < 0); }

    /* Apply python-style wraparound to a runtime index, potentially truncating it to
    the nearest valid index.  The second return value is set 0 if the index was valid,
    or +/- 1 to indicate overflow to the right or left, respectively. */
    inline constexpr std::pair<ssize_t, ssize_t> normalize_index(
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

    template <meta::not_void T> requires (!meta::reference<T>)
    struct contiguous_slice {
    private:

        struct initializer {
            std::initializer_list<T>& items;
            [[nodiscard]] constexpr size_t size() const noexcept { return items.size(); }
            [[nodiscard]] constexpr auto begin() const noexcept { return items.begin(); }
            [[nodiscard]] constexpr auto end() const noexcept { return items.end(); }
        };

        template <typename V>
        struct iter {
            using iterator_category = std::input_iterator_tag;
            using difference_type = ssize_t;
            using value_type = V;
            using reference = meta::as_lvalue<value_type>;
            using const_reference = meta::as_lvalue<meta::as_const<value_type>>;
            using pointer = meta::as_pointer<value_type>;
            using const_pointer = meta::as_pointer<meta::as_const<value_type>>;

            pointer data = nullptr;
            ssize_t index = 0;
            ssize_t step = 1;

            [[nodiscard]] constexpr reference operator*() noexcept {
                return data[index];
            }

            [[nodiscard]] constexpr const_reference operator*() const noexcept {
                return data[index];
            }

            [[nodiscard]] constexpr pointer operator->() noexcept {
                return data + index;
            }

            [[nodiscard]] constexpr const_pointer operator->() const noexcept {
                return data + index;
            }

            [[maybe_unused]] iter& operator++() noexcept {
                index += step;
                return *this;
            }

            [[nodiscard]] iter operator++(int) noexcept {
                iterator copy = *this;
                ++(*this);
                return copy;
            }

            [[nodiscard]] constexpr bool operator==(const iter& other) noexcept {
                return step > 0 ? index >= other.index : index <= other.index;
            }

            [[nodiscard]] constexpr bool operator!=(const iter& other) noexcept {
                return !(*this == other);
            }
        };

    public:
        using value_type = T;
        using reference = meta::as_lvalue<value_type>;
        using const_reference = meta::as_lvalue<meta::as_const<value_type>>;
        using pointer = meta::as_pointer<value_type>;
        using const_pointer = meta::as_pointer<meta::as_const<value_type>>;
        using iterator = iter<value_type>;
        using const_iterator = iter<meta::as_const<value_type>>;

        constexpr contiguous_slice(
            pointer data,
            bertrand::slice::normalized indices
        ) noexcept :
            m_data(data),
            m_indices(indices)
        {}

        constexpr contiguous_slice(const contiguous_slice&) = delete;
        constexpr contiguous_slice(contiguous_slice&&) = delete;
        constexpr contiguous_slice& operator=(const contiguous_slice&) = delete;
        constexpr contiguous_slice& operator=(contiguous_slice&&) = delete;

        [[nodiscard]] constexpr pointer data() const noexcept { return m_data; }
        [[nodiscard]] constexpr ssize_t start() const noexcept { return m_indices.start; }
        [[nodiscard]] constexpr ssize_t stop() const noexcept { return m_indices.stop; }
        [[nodiscard]] constexpr ssize_t step() const noexcept { return m_indices.step; }
        [[nodiscard]] constexpr ssize_t ssize() const noexcept { return m_indices.length; }
        [[nodiscard]] constexpr size_t size() const noexcept { return size_t(ssize()); }
        [[nodiscard]] constexpr bool empty() const noexcept { return !ssize(); }
        [[nodiscard]] explicit operator bool() const noexcept { return ssize(); }

        [[nodiscard]] constexpr iterator begin() noexcept {
            return {m_data, m_indices.start, m_indices.step};
        }

        [[nodiscard]] constexpr const_iterator begin() const noexcept {
            return {m_data, m_indices.start, m_indices.step};
        }

        [[nodiscard]] constexpr const_iterator cbegin() noexcept {
            return {m_data, m_indices.start, m_indices.step};
        }

        [[nodiscard]] constexpr iterator end() noexcept {
            return {m_data, m_indices.stop, m_indices.step};
        }

        [[nodiscard]] constexpr const_iterator end() const noexcept {
            return {m_data, m_indices.stop, m_indices.step};
        }

        [[nodiscard]] constexpr const_iterator cend() const noexcept {
            return {m_data, m_indices.stop, m_indices.step};
        }

        template <typename V>
            requires (meta::constructible_from<V, std::from_range_t, contiguous_slice&>)
        [[nodiscard]] constexpr operator V() && noexcept(noexcept(V(std::from_range, *this))) {
            return V(std::from_range, *this);
        }

        template <typename V>
            requires (
                !meta::constructible_from<V, std::from_range_t, contiguous_slice&> &&
                meta::constructible_from<V, iterator, iterator>
            )
        [[nodiscard]] constexpr operator V() && noexcept(noexcept(V(begin(), end()))) {
            return V(begin(), end());
        }

        template <typename Dummy = value_type>
            requires (
                meta::not_const<Dummy> &&
                meta::destructible<Dummy> &&
                meta::copyable<Dummy>
            )
        [[maybe_unused]] constexpr contiguous_slice& operator=(
            std::initializer_list<value_type> items
        ) && {
            return std::move(*this) = initializer{items};
        }

        template <meta::yields<value_type> Range>
            requires (
                meta::not_const<value_type> &&
                meta::destructible<value_type> &&
                meta::constructible_from<value_type, meta::yield_type<Range>>
            )
        [[maybe_unused]] constexpr contiguous_slice& operator=(Range&& range) && {
            using type = meta::unqualify<value_type>;
            constexpr bool has_size = meta::has_size<meta::as_lvalue<Range>>;
            auto it = std::ranges::begin(range);
            auto end = std::ranges::end(range);

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
            if (step() > 0) {
                for (ssize_t i = start(); i < stop(); i += step()) {
                    if constexpr (!has_size) {
                        if (it == end) {
                            throw ValueError(
                                "not enough values to fill slice of size " +
                                std::to_string(size())
                            );
                        }
                    }
                    m_data[i].~type();
                    new (m_data + i) type(*it);
                    ++it;
                }
            } else {
                for (ssize_t i = start(); i > stop(); i += step()) {
                    if constexpr (!has_size) {
                        if (it == end) {
                            throw ValueError(
                                "not enough values to fill slice of size " +
                                std::to_string(size())
                            );
                        }
                    }
                    m_data[i].~type();
                    new (m_data + i) type(*it);
                    ++it;
                }
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

    private:
        pointer m_data;
        bertrand::slice::normalized m_indices;
    };

    /* A generic sentinel type to simplify iterator implementations. */
    struct sentinel {
        constexpr bool operator==(sentinel) const noexcept { return true; }
        constexpr auto operator<=>(sentinel) const noexcept {
            return std::strong_ordering::equal;
        }
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
            using type = out;
            static constexpr bool enable = true;
            static constexpr bool nothrow = true;
        };
        template <typename F, typename T>
        static constexpr out operator()(F&&, T&& arg) noexcept {
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
            using type = out;
            static constexpr bool enable = true;
            static constexpr bool nothrow = true;
        };
        template <typename F, typename T>
        static constexpr out operator()(F&&, T&& arg) noexcept {
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
            using type = out;
            static constexpr bool enable = true;
            static constexpr bool nothrow = true;
        };
        template <typename F, typename T>
        static constexpr out operator()(F&&, T&& arg) noexcept(
            meta::nothrow::convertible_to<T, out>
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
            using type = out;
            static constexpr bool enable = true;
            static constexpr bool nothrow = true;
        };
        template <typename F, typename T>
        static constexpr out operator()(F&&, T&& arg) noexcept(
            meta::nothrow::convertible_to<T, out>
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
            using type = std::pair<out, out>;
            static constexpr bool enable = true;
            static constexpr bool nothrow = true;
        };
        template <typename F, typename T1, typename T2>
        static constexpr std::pair<out, out> operator()(
            F&&,
            T1&& min,
            T2&& max
        ) noexcept(
            meta::nothrow::constructible_from<std::pair<out, out>, T1, T2>
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


/* Get the length of an arbitrary sequence in constant time as a signed integer.
Equivalent to calling `std::ranges::ssize(range)`. */
template <meta::has_size Range>
[[nodiscard]] constexpr decltype(auto) len(Range&& r)
    noexcept(noexcept(std::ranges::ssize(r)))
{
    return (std::ranges::ssize(r));
}


/* Get the length of a tuple-like container as a compile-time constant signed integer.
Equivalent to evaluating `meta::tuple_size<T>` on the given type `T`. */
template <meta::tuple_like T> requires (!meta::has_size<T>)
[[nodiscard]] constexpr ssize_t len(T&& r) noexcept {
    return ssize_t(meta::tuple_size<T>);
}


/* Get the distance between two iterators as a signed integer.  Equivalent to calling
`std::ranges::distance(begin, end)`.  This may run in O(n) time if the iterators do
not support constant-time distance measurements. */
template <meta::iterator Begin, meta::sentinel_for<Begin> End>
[[nodiscard]] constexpr decltype(auto) len(Begin&& begin, End&& end)
    noexcept(noexcept(std::ranges::distance(begin, end)))
{
    return (std::ranges::distance(begin, end));
}


/// TODO: the begin/end overloads for range() might conflict with the integer
/// overloads.  Also a stride overload would be very nice for both cases.  Formalize
/// that even before stride views come out.


/* Produce a simple range starting at a default-constructed instance of `End` (zero if
`End` is an integer type), similar to Python's built-in `range()` operator.  This is
equivalent to a  `std::views::iota()` call under the hood. */
template <typename End>
[[nodiscard]] constexpr decltype(auto) range(End&& stop)
    noexcept(noexcept(std::views::iota(End{}, std::forward<End>(stop))))
    requires(requires{std::views::iota(End{}, std::forward<End>(stop));})
{
    return (std::views::iota(End{}, std::forward<End>(stop)));
}


/* Produce a simple range from `start` to `stop`, similar to Python's built-in
`range()` operator.  This is equivalent to a `std::views::iota()` call under the
hood. */
template <typename Begin, typename End>
    requires (!meta::iterator<Begin> || !meta::sentinel_for<End, Begin>)
[[nodiscard]] constexpr decltype(auto) range(Begin&& start, End&& stop)
    noexcept(noexcept(
        std::views::iota(std::forward<Begin>(start), std::forward<End>(stop))
    ))
    requires(requires{
        std::views::iota(std::forward<Begin>(start), std::forward<End>(stop));
    })
{
    return (std::views::iota(std::forward<Begin>(start), std::forward<End>(stop)));
}


#ifdef __cpp_lib_ranges_stride

    /// TODO: a stride view range() accepting begin/end iterators would be very nice.

    /* Produce a simple integer range, similar to Python's built-in `range()` operator.
    This is equivalent to a  `std::views::iota()` call under the hood. */
    template <typename T = ssize_t>
    inline constexpr decltype(auto) range(T start, T stop, T step) {
        return (std::views::iota(start, stop) | std::views::stride(step));
    }

#endif


/* Produce a simple range object that encapsulates a `start` and `stop` iterator as a
range adaptor.  This is equivalent to a `std::ranges::subrange()` call under the
hood. */
template <meta::iterator Begin, meta::sentinel_for<Begin> End>
[[nodiscard]] constexpr decltype(auto) range(Begin&& start, End&& stop)
    noexcept(noexcept(
        std::ranges::subrange(std::forward<Begin>(start), std::forward<End>(stop))
    ))
    requires(requires{
        std::ranges::subrange(std::forward<Begin>(start), std::forward<End>(stop));
    })
{
    return (std::ranges::subrange(std::forward<Begin>(start), std::forward<End>(stop)));
}


/// TODO: if the range is not an lvalue, preface with a `std::views::all(std::move(r))`
/// to capture the contents, and then pipe that with reverse, enumerate, zip, etc.


/* Produce a view over a reverse iterable range that can be used in range-based for
loops.  This is equivalent to a `std::views::reverse()` call under the hood. */
template <meta::reverse_iterable T>
[[nodiscard]] constexpr decltype(auto) reversed(T&& obj)
    noexcept(noexcept(std::views::reverse(std::forward<T>(obj))))
{
    return (std::views::reverse(std::forward<T>(obj)));
}


#ifdef __cpp_lib_ranges_enumerate

    /* Produce a view over a given range that yields tuples consisting of each item's
    index and ordinary value_type.  This is equivalent to a `std::views::enumerate()` call
    under the hood, but is easier to remember, and closer to Python syntax. */
    template <meta::iterable T>
    [[nodiscard]] constexpr decltype(auto) enumerate(T&& r)
        noexcept(noexcept(std::views::enumerate(std::forward<T>(r))))
        requires(requires{std::views::enumerate(std::forward<T>(r));})
    {
        return (std::views::enumerate(std::forward<T>(r)));
    }

#endif


/* Combine several ranges into a view that yields tuple-like values consisting of the
`i` th element of each range.  This is equivalent to a `std::views::zip()` call under
the hood. */
template <meta::iterable... Ts>
[[nodiscard]] constexpr decltype(auto) zip(Ts&&... rs)
    noexcept(noexcept(std::views::zip(std::forward<Ts>(rs)...)))
    requires(requires{std::views::zip(std::forward<Ts>(rs)...);})
{
    return (std::views::zip(std::forward<Ts>(rs)...));
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
    noexcept(meta::nothrow::apply_func<
        typename meta::tuple_types<T>::template eval<impl::_fold_left>,
        F,
        T
    >)
    requires(meta::apply_func<
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
        return std::nullopt;
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
    noexcept(meta::nothrow::apply_func<
        typename meta::tuple_types<T>::template eval<impl::_fold_right>,
        F,
        T
    >)
    requires(meta::apply_func<
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
            return std::nullopt;
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


/// TODO: min()/max()/minmax() may accidentally return dangling references to an rvalue
/// range.  If the range is an rvalue and the output type is an lvalue, remove
/// references from the return type and force a copy/move. 


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
    noexcept(meta::nothrow::apply_func<
        typename meta::tuple_types<T>::template eval<impl::_min>,
        F,
        T
    >)
    requires(meta::apply_func<
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
        return std::nullopt;
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
    noexcept(meta::nothrow::apply_func<
        typename meta::tuple_types<T>::template eval<impl::_max>,
        F,
        T
    >)
    requires(meta::apply_func<
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
        return std::nullopt;
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
    noexcept(meta::nothrow::apply_func<
        typename meta::tuple_types<T>::template eval<impl::minmax_helper>,
        F,
        T
    >)
    requires(meta::apply_func<
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
        return std::nullopt;
    }
    return std::ranges::minmax(std::forward<T>(r), F{});
}








inline void test() {
    struct Cmp {
        static constexpr bool operator()(int x) noexcept { return x > 1; }
    };

    static constexpr std::tuple<int, int, int> tup {0, 0, 2};
    static constexpr std::array<int, 3> arr = {1, 2, 3};
    static_assert(any<Cmp>(0, 0, 2));
    static_assert(any<Cmp>(tup));
    static_assert(any<Cmp>(arr));
    static_assert(fold_left<impl::Add>(arr) == 6);
    static_assert(bertrand::min(arr) == 1);
    static_assert(bertrand::max(arr) == 3);
    static_assert(bertrand::minmax(arr).first == 1);
    static_assert(bertrand::minmax(arr).second == 3);
    static_assert(len(arr) == 3);
    static_assert(len(tup) == 3);

    static constexpr auto sum = fold_left<impl::Add>(1, 2, 3.25);
    static_assert(sum == 6.25);

    static constexpr auto min = bertrand::min(1, 2, 3.25);
    static_assert(min == 1);

    static constexpr auto max = bertrand::max(1, 2, 3.25);
    static_assert(max == 3.25);

    static constexpr std::string a = "a", b = "b", c ="c", d = "a", e = "c";
    static_assert(fold_left<impl::Add>(a, b, c) == "abc");
    static_assert(fold_right<impl::Add>(a, b, c) == "abc");
    static_assert(bertrand::min(a, d, c) == "a");
    static_assert(&bertrand::min(a, d, c) == &a);
    static_assert(bertrand::max(a, c, e) == "c");
    static_assert(&bertrand::max(a, c, e) == &c);

    static constexpr auto minmax = bertrand::minmax(a, d, b, c, e);
    static_assert(minmax.first == "a");
    static_assert(&minmax.first == &a);
    static_assert(minmax.second == "c");
    static_assert(&minmax.second == &c);
}


}


#endif