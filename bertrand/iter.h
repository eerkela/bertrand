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
    template <size_t size, ssize_t I>
    consteval bool valid_index() noexcept {
        return
            (I + ssize_t(size * (I < 0)) >= ssize_t(0)) &&
            (I + ssize_t(size * (I < 0))) < ssize_t(size);
    }

    /* Check to see if applying Python-style wraparound to a runtime index would yield
    a valid index into a container of a given size.  Returns false if the index would
    be out of bounds after normalizing. */
    inline constexpr bool valid_index(size_t size, ssize_t i) noexcept {
        return
            (i + ssize_t(size * (i < 0)) >= ssize_t(0)) &&
            (i + ssize_t(size * (i < 0))) < ssize_t(size);
    }

    /* Apply Python-style wraparound to a compile-time index. Fails to compile if the
    index would be out of bounds after normalizing. */
    template <size_t size, ssize_t I> requires (valid_index<size, I>())
    consteval ssize_t normalize_index() noexcept {
        return I + ssize_t(size * (I < 0));
    }

    /* Apply python-style wraparound to a runtime index, throwing an `IndexError` if it
    is out of bounds after normalizing. */
    inline constexpr ssize_t normalize_index(size_t size, ssize_t i) {
        ssize_t n = static_cast<ssize_t>(size);
        ssize_t j = i + n * (i < 0);
        if (j < 0 || j >= n) {
            throw IndexError(std::to_string(i));
        }
        return j;
    }

    /* Apply python-style wraparound to a compile-time index, truncating to the nearest
    edge if it is out of bounds after normalizing. */
    template <size_t size, ssize_t I>
    consteval ssize_t truncate_index() noexcept {
        if constexpr (valid_index<size, I>()) {
            return I + ssize_t(size * (I < 0));
        } else {
            return I < 0 ? 0 : ssize_t(size);
        }
    }

    /* Apply python-style wraparound to a runtime index, truncating to the nearest edge
    if it is out of bounds after normalizing. */
    inline constexpr ssize_t truncate_index(size_t size, ssize_t i) noexcept {
        ssize_t n = static_cast<ssize_t>(size);
        i += n * (i < 0);
        if (i < 0) {
            return 0;
        } else if (i >= n) {
            return n;
        }
        return i;
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
        [[nodiscard]] constexpr size_t size() const noexcept { return size_t(size()); }
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

    /* Check whether the tuple type at index `I` are truth-testable as part of an
    `any()` or `all()` operation. */
    template <size_t I, typename T, typename F>
    concept broadcast_truthy = I >= meta::tuple_size<T> || (
        meta::has_get<T, I> &&
        meta::invocable<F, meta::get_type<T, I>> &&
        meta::explicitly_convertible_to<
            meta::invoke_type<F, meta::get_type<T, I>>,
            bool
        >
    );

    template <size_t I, typename T, typename F>
    concept nothrow_broadcast_truthy = I >= meta::tuple_size<T> || (
        meta::nothrow::has_get<T, I> &&
        meta::nothrow::invocable<F, meta::nothrow::get_type<T, I>> &&
        meta::nothrow::explicitly_convertible_to<
            meta::nothrow::invoke_type<F, meta::nothrow::get_type<T, I>>,
            bool
        >
    );

    /* Implement `any()` for tuples. */
    template <size_t I = 0, meta::tuple_like T, typename F>
    constexpr bool broadcast_any(T&& t, F&& func)
        noexcept(nothrow_broadcast_truthy<I, T, F>)
        requires(broadcast_truthy<I, T, F>)
    {
        if constexpr (I < std::tuple_size_v<meta::unqualify<T>>) {
            if constexpr (meta::has_member_get<T, I>) {
                if (std::forward<F>(func)(std::forward<T>(t).template get<I>(t))) {
                    return true;
                }
            } else if constexpr (meta::has_adl_get<T, I>) {
                using std::get;
                if (std::forward<F>(func)(get<I>(std::forward<T>(t)))) {
                    return true;
                }
            } else {
                if (std::forward<F>(func)(std::get<I>(std::forward<T>(t)))) {
                    return true;
                }
            }
            return broadcast_any<I + 1>(std::forward<T>(t), std::forward<F>(func));
        } else {
            return false;
        }
    }

    /* Implement `all()` for tuples. */
    template <size_t I = 0, meta::tuple_like T, typename F>
    constexpr bool broadcast_all(T&& t, F&& func)
        noexcept(nothrow_broadcast_truthy<I, T, F>)
        requires (broadcast_truthy<I, T, F>)
    {
        if constexpr (I < std::tuple_size_v<meta::unqualify<T>>) {
            if constexpr (meta::has_member_get<T, I>) {
                if (!std::forward<F>(func)(std::forward<T>(t).template get<I>(t))) {
                    return false;
                }
            } else if constexpr (meta::has_adl_get<T, I>) {
                using std::get;
                if (!std::forward<F>(func)(get<I>(std::forward<T>(t)))) {
                    return false;
                }
            } else {
                if (!std::forward<F>(func)(std::get<I>(std::forward<T>(t)))) {
                    return false;
                }
            }
            return broadcast_all<I + 1>(std::forward<T>(t), std::forward<F>(func));
        } else {
            return true;
        }
    }

    /* Apply a pairwise function over the arguments to determine the return type and
    enabled/noexcept status of a `fold_left()` function call. */
    template <typename F, typename out, typename...>
    struct fold_left {
        using type = out;
        static constexpr bool enable = true;
        static constexpr bool nothrow = true;
    };
    template <typename F, typename out, typename curr, typename... next>
        requires (
            meta::invocable<F, out, curr> &&
            meta::has_common_type<out, meta::invoke_type<F, out, curr>>
        )
    struct fold_left<F, out, curr, next...> {
        using result = fold_left<
            F,
            meta::common_type<out, meta::invoke_type<F, out, curr>>,
            next...
        >;
        using type = result::type;
        static constexpr bool enable = result::enable;
        template <typename G>
        static constexpr bool _nothrow = false;
        template <meta::nothrow::invocable<out, curr> G>
        static constexpr bool _nothrow<G> =
            meta::nothrow::convertible_to<
                meta::nothrow::invoke_type<G, out, curr>,
                type
            > && result::nothrow;
        static constexpr bool nothrow = _nothrow<F>;
    };
    template <typename F, typename out, typename curr, typename... next>
        requires (
            !meta::invocable<F, out, curr> ||
            !meta::has_common_type<out, meta::invoke_type<F, out, curr>>
        )
    struct fold_left<F, out, curr, next...> {
        using type = void;
        static constexpr bool enable = false;
        static constexpr bool nothrow = false;
    };

    /* Apply a pairwise function over the arguments to determine the return type and
    enabled/noexcept status of a `fold_right()` function call. */
    template <typename F, typename out, typename...>
    struct fold_right {
        using type = out;
        static constexpr bool enable = true;
        static constexpr bool nothrow = true;
    };
    template <typename F, typename out, typename curr, typename... next>
        requires (
            meta::invocable<F, typename fold_right<F, curr, next...>::type, out> &&
            meta::has_common_type<
                out,
                meta::invoke_type<F, typename fold_right<F, curr, next...>::type, out>
            >
        )
    struct fold_right<F, out, curr, next...> {
        using result = fold_right<F, curr, next...>;
        using type =
            meta::common_type<out, meta::invoke_type<F, typename result::type, out>>;
        static constexpr bool enable = result::enable;
        template <typename G>
        static constexpr bool _nothrow = false;
        template <meta::nothrow::invocable<typename result::type, out> G>
        static constexpr bool _nothrow<G> =
            meta::nothrow::convertible_to<
                meta::nothrow::invoke_type<G, typename result::type, out>,
                type
            > && result::nothrow;
        static constexpr bool nothrow = _nothrow<F>;
    };
    template <typename F, typename out, typename curr, typename... next>
        requires (
            !meta::invocable<F, typename fold_right<F, curr, next...>::type, out> ||
            !meta::has_common_type<
                out,
                meta::invoke_type<F, typename fold_right<F, curr, next...>::type, out>
            >
        )
    struct fold_right<F, out, curr, next...> {
        using type = void;
        static constexpr bool enable = false;
        static constexpr bool nothrow = false;
    };

    /* Convert a binary boolean predicate into an accumulator for use with `fold_left`
    and `fold_right`.  If the function returns true, the left operand will be
    chosen. */
    template <meta::default_constructible F>
    struct choose_left {
        F func;
        template <typename L, typename R>
        constexpr auto operator()(L&& lhs, R&& rhs)
            noexcept(
                meta::nothrow::invocable<F, L, R> &&
                meta::nothrow::explicitly_convertible_to<
                    meta::nothrow::invoke_type<F, L, R>,
                    bool
                > &&
                meta::nothrow::has_common_type<L, R>
            )
            -> meta::common_type<L, R>
            requires(
                meta::invocable<F, L, R> &&
                meta::explicitly_convertible_to<
                    meta::invoke_type<F, L, R>,
                    bool
                > &&
                meta::has_common_type<L, R>
            )
        {
            if (func(std::forward<L>(lhs), std::forward<R>(rhs))) {
                return std::forward<L>(lhs);
            } else {
                return std::forward<R>(rhs);
            }
        }
    };

    /* Convert a binary boolean predicate into an accumulator for use with `fold_left`
    and `fold_right`.  If the function returns true, the right operand will be
    chosen. */
    template <meta::default_constructible F>
    struct choose_right {
        F func;
        template <typename L, typename R>
        constexpr auto operator()(L&& lhs, R&& rhs)
            noexcept(
                meta::nothrow::invocable<F, L, R> &&
                meta::nothrow::explicitly_convertible_to<
                    meta::nothrow::invoke_type<F, L, R>,
                    bool
                > &&
                meta::nothrow::has_common_type<L, R>
            )
            -> meta::common_type<L, R>
            requires(
                meta::invocable<F, L, R> &&
                meta::explicitly_convertible_to<
                    meta::invoke_type<F, L, R>,
                    bool
                > &&
                meta::has_common_type<L, R>
            )
        {
            if (func(std::forward<L>(lhs), std::forward<R>(rhs))) {
                return std::forward<R>(rhs);
            } else {
                return std::forward<L>(lhs);
            }
        }
    };

    /* Flip the order of arguments for a binary predicate function.  This is applied to
    the `choose_left` wrapper for a `min()` operation in order to prefer earlier
    results in the case of equal comparisons. */
    template <meta::default_constructible F>
    struct reverse_arguments {
        F func;
        template <typename L, typename R>
        constexpr decltype(auto) operator()(L&& lhs, R&& rhs)
            noexcept(meta::nothrow::invocable<F, R, L>)
            requires(meta::invocable<F, R, L>)
        {
            return (func(std::forward<R>(rhs), std::forward<L>(lhs)));
        }
    };







    /* Check whether a `<` operator can be broadcasted over a set of types as part
    of a `min()`, `max()`, or `minmax()` operation. */
    template <typename... Ts>
    constexpr bool broadcast_lt = true;
    template <typename T, typename... Ts>
    constexpr bool broadcast_lt<T, Ts...> = false;
    template <typename T, typename... Ts> requires (
        (
            meta::has_lt<T, Ts> &&
            meta::explicitly_convertible_to<meta::lt_type<T, Ts>, bool>
        ) && ...
    )
    constexpr bool broadcast_lt<T, Ts...> = broadcast_lt<Ts...>;

    template <typename... Ts>
    constexpr bool nothrow_broadcast_lt = true;
    template <typename T, typename... Ts>
    constexpr bool nothrow_broadcast_lt<T, Ts...> = false;
    template <typename T, typename... Ts> requires (
        (
            meta::has_lt<T, Ts> &&
            meta::explicitly_convertible_to<meta::lt_type<T, Ts>, bool>
        ) && ...
    )
    constexpr bool nothrow_broadcast_lt<T, Ts...> = nothrow_broadcast_lt<Ts...>;

    /* Do the same for the contents of a tuple-like container by first accumulating
    the types yielded by series of `get<I>(T)` calls up to `std::tuple_size<T>`. */
    template <meta::tuple_like T, size_t I = 0, typename... Ts>
    constexpr bool tuple_broadcast_lt = broadcast_lt<Ts...>;
    template <meta::tuple_like T, size_t I, typename... Ts>
        requires (I < meta::tuple_size<T>)
    constexpr bool tuple_broadcast_lt<T, I, Ts...> =
        tuple_broadcast_lt<T, I + 1, Ts..., meta::get_type<T, I>>;

    template <meta::tuple_like T, size_t I = 0, typename... Ts>
    constexpr bool tuple_nothrow_broadcast_lt = nothrow_broadcast_lt<Ts...>;
    template <meta::tuple_like T, size_t I, typename... Ts>
        requires (I < meta::tuple_size<T>)
    constexpr bool tuple_nothrow_broadcast_lt<T, I, Ts...> =
        tuple_nothrow_broadcast_lt<T, I + 1, Ts..., meta::get_type<T, I>>;

    /* Check whether the tuple type at index `I` is less-than comparable to the given
    intermediate result type as part of a `min()`, `max()`, or `minmax()` operation. */
    template <size_t I, typename T, typename Result>
    concept broadcast_min_max = I >= meta::tuple_size<T> || (
        meta::has_get<T, I> &&
        meta::has_lt<meta::get_type<T, I>, Result> &&
        meta::explicitly_convertible_to<meta::lt_type<meta::get_type<T, I>, Result>, bool>
    );

    template <size_t I, typename T, typename Result>
    concept nothrow_broadcast_min_max = I >= meta::tuple_size<T> || (
        meta::nothrow::has_get<T, I> &&
        meta::nothrow::has_lt<meta::nothrow::get_type<T, I>, Result> &&
        meta::nothrow::explicitly_convertible_to<
            meta::nothrow::lt_type<meta::nothrow::get_type<T, I>, Result>,
            bool
        >
    );

    /* Implement `minmax()` for tuples, using `min` and `max` as intermediate
    results. */
    template <size_t I = 1, meta::tuple_like T, typename Min, typename Max>
        requires (broadcast_min_max<I, T, Min> && broadcast_min_max<I, T, Max>)
    constexpr auto broadcast_minmax(T&& t, Min&& min, Max&& max) noexcept(
        nothrow_broadcast_min_max<I, T, Min> &&
        nothrow_broadcast_min_max<I, T, Max>
    ) -> std::pair<meta::common_tuple_type<T>, meta::common_tuple_type<T>> {
        if constexpr (I < meta::tuple_size<T>) {
            if constexpr (meta::has_member_get<T, I>) {
                if (std::forward<T>(t).template get<I>(t) < std::forward<Min>(min)) {
                    return broadcast_minmax<I + 1>(
                        std::forward<T>(t),
                        std::forward<T>(t).template get<I>(t),
                        std::forward<Max>(max)
                    );
                }
                if (std::forward<Max>(max) < std::forward<T>(t).template get<I>(t)) {
                    return broadcast_minmax<I + 1>(
                        std::forward<T>(t),
                        std::forward<Min>(min),
                        std::forward<T>(t).template get<I>(t)
                    );
                }
            } else if constexpr (meta::has_adl_get<T, I>) {
                if (get<I>(std::forward<T>(t)) < std::forward<Min>(min)) {
                    return broadcast_minmax<I + 1>(
                        std::forward<T>(t),
                        get<I>(std::forward<T>(t)),
                        std::forward<Max>(max)
                    );
                }
                if (std::forward<Max>(max) < get<I>(std::forward<T>(t))) {
                    return broadcast_minmax<I + 1>(
                        std::forward<T>(t),
                        std::forward<Min>(min),
                        get<I>(std::forward<T>(t))
                    );
                }
            } else {
                if (std::get<I>(std::forward<T>(t)) < std::forward<Min>(min)) {
                    return broadcast_minmax<I + 1>(
                        std::forward<T>(t),
                        std::get<I>(std::forward<T>(t)),
                        std::forward<Max>(max)
                    );
                }
                if (std::forward<Max>(max) < std::get<I>(std::forward<T>(t))) {
                    return broadcast_minmax<I + 1>(
                        std::forward<T>(t),
                        std::forward<Min>(min),
                        std::get<I>(std::forward<T>(t))
                    );
                }
            }
            return broadcast_minmax<I + 1>(
                std::forward<T>(t),
                std::forward<Min>(min),
                std::forward<Max>(max)
            );
        } else {
            return {std::forward<Min>(min), std::forward<Max>(max)};
        }
    }

    /* Implement `minmax()` for tuples, initializing the intermediate results to
    the first value in the tuple. */
    template <meta::tuple_like T> requires (meta::tuple_size<T> > 0)
    constexpr auto broadcast_minmax(T&& t) noexcept(
        meta::nothrow::has_get<T, 0> &&
        noexcept(broadcast_minmax(
            std::forward<T>(t),
            std::declval<meta::get_type<T, 0>>(),
            std::declval<meta::get_type<T, 0>>()
        ))
    ) -> std::pair<meta::common_tuple_type<T>, meta::common_tuple_type<T>> {
        if constexpr (meta::has_member_get<T, 0>) {
            return broadcast_minmax(
                std::forward<T>(t),
                std::forward<T>(t).template get<0>(t),
                std::forward<T>(t).template get<0>(t)
            );
        } else if constexpr (meta::has_adl_get<T, 0>) {
            return broadcast_minmax(
                std::forward<T>(t),
                get<0>(std::forward<T>(t)),
                get<0>(std::forward<T>(t))
            );
        } else {
            return broadcast_minmax(
                std::forward<T>(t),
                std::get<0>(std::forward<T>(t)),
                std::get<0>(std::forward<T>(t))
            );
        }
    }

}


/* Get the length of an arbitrary sequence in constant time as a signed integer.
Equivalent to calling `std::ranges::ssize(range)`. */
template <meta::has_size Range>
[[nodiscard]] constexpr decltype(auto) len(Range&& r)
    noexcept(noexcept(std::ranges::ssize(r)))
{
    return (std::ranges::ssize(r));
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
    requires (
        sizeof...(Args) > 1 &&
        (meta::invocable<F, Args> && ...) &&
        (meta::explicitly_convertible_to<meta::invoke_type<F, Args>, bool> && ...)
    )
[[nodiscard]] constexpr bool any(Args&&... args)
    noexcept(noexcept((F{}(std::forward<Args>(args)) || ...)))
{
    F func;
    return (func(std::forward<Args>(args)) || ...);
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
    noexcept(noexcept(impl::broadcast_any(std::forward<T>(t), F{})))
    requires(requires{impl::broadcast_any(std::forward<T>(t), F{});})
{
    return impl::broadcast_any(std::forward<T>(t), F{});
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
    requires (
        sizeof...(Args) > 1 &&
        (meta::invocable<F, Args> && ...) &&
        (meta::explicitly_convertible_to<meta::invoke_type<F, Args>, bool> && ...)
    )
[[nodiscard]] constexpr bool all(Args&&... args)
    noexcept(noexcept((F{}(std::forward<Args>(args)) || ...)))
{
    F func;
    return (func(std::forward<Args>(args)) && ...);
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
    noexcept(noexcept(impl::broadcast_all(std::forward<T>(t), F{})))
    requires(requires{impl::broadcast_all(std::forward<T>(t), F{});})
{
    return impl::broadcast_all(std::forward<T>(t), F{});
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
the accumulated result.  This is a generalization of `min()`, `max()`, `sum()`, etc.,
which simply alias to specializations of this method for the proper reduction.
User-defined algorithms can be provided to inject custom behavior, as long as they are
default constructible and invocable with each pair of arguments.  The return type is
deduced as the common type between each invocation, assuming one exists.  The function
will fail to compile if these requirements are not met. */
template <meta::default_constructible F, typename... Ts>
    requires (sizeof...(Ts) > 1)
[[nodiscard]] constexpr impl::fold_left<F, Ts...>::type fold_left(Ts&&... args)
    noexcept(impl::fold_left<F, Ts...>::nothrow)
    requires(impl::fold_left<F, Ts...>::enable)
{
    return [](
        this auto&& self,
        auto&& func,
        auto&& out,
        auto&& curr,
        auto&&... next
    )
        noexcept(impl::fold_left<F, Ts...>::nothrow) -> decltype(auto)
    {
        if constexpr (sizeof...(next) > 0) {
            return (std::forward<decltype(self)>(self)(
                std::forward<decltype(func)>(func),
                std::forward<decltype(func)>(func)(
                    std::forward<decltype(out)>(out),
                    std::forward<decltype(curr)>(curr)
                ),
                std::forward<decltype(next)>(next)...
            ));
        } else {
            return (std::forward<decltype(func)>(func)(
                std::forward<decltype(out)>(out),
                std::forward<decltype(curr)>(curr)
            ));
        }
    }(F{}, std::forward<Ts>(args)...);
}


/* Apply a pairwise reduction function over an iterable range from left to right,
returning the accumulated result.  This is a generalization of `min()`, `max()`,
`sum()`, etc., which simply alias to specializations of this method for the proper
reduction.  User-defined algorithms can be provided to inject custom behavior, as
long as they are default constructible and invocable with each pair of arguments.
The return type is deduced as `Optional<T>`, where `T` is the common type between each
invocation, assuming one exists.  If the input range is empty, then the result will
be in the empty state.  Otherwise, it will contain the accumulated result.  This is
equivalent to a `std::ranges::fold_left()` call under the hood, but explicitly checks
for an empty range and encodes the result as an optional, rather than invoking
undefined behavior. */
template <meta::default_constructible F, meta::iterable T>
    requires (!meta::tuple_like<T>)
[[nodiscard]] constexpr auto fold_left(T&& r)
    noexcept(
        noexcept(std::ranges::begin(r) == std::ranges::end(r)) &&
        noexcept(std::ranges::fold_left(
            std::ranges::begin(r),
            std::ranges::end(r),
            *std::ranges::begin(r),
            F{}
        )) &&
        meta::nothrow::convertible_to<
            decltype(std::ranges::fold_left(
                std::ranges::begin(r),
                std::ranges::end(r),
                *std::ranges::begin(r),
                F{}
            )),
            Optional<decltype(std::ranges::fold_left(
                std::ranges::begin(r),
                std::ranges::end(r),
                *std::ranges::begin(r),
                F{}
            ))>
        >
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


namespace impl {

    template <typename F, size_t I, meta::tuple_like tuple, typename... Ts>
    constexpr bool enable_tuple_fold_left =
        requires(Ts... ts) {bertrand::fold_left<F>(ts...);};
    template <typename F, size_t I, meta::tuple_like tuple, typename... Ts>
        requires (I < meta::tuple_size<tuple>)
    constexpr bool enable_tuple_fold_left<F, I, tuple, Ts...> =
        enable_tuple_fold_left<F, I + 1, tuple, Ts..., meta::get_type<tuple, I>>;

    template <typename F, size_t I, meta::tuple_like tuple, typename... Ts>
    constexpr bool nothrow_tuple_fold_left =
        noexcept(noexcept(bertrand::fold_left<F>(std::declval<Ts>()...)));
    template <typename F, size_t I, meta::tuple_like tuple, typename... Ts>
        requires (I < meta::tuple_size<tuple>)
    constexpr bool nothrow_tuple_fold_left<F, I, tuple, Ts...> =
        nothrow_tuple_fold_left<F, I + 1, tuple, Ts..., meta::get_type<tuple, I>>;

    template <meta::default_constructible F, meta::tuple_like T>
    [[nodiscard]] constexpr decltype(auto) tuple_fold_left(T&& t)
        noexcept(noexcept(meta::tuple_get<0>(std::forward<T>(t))))
        requires(meta::tuple_size<T> == 1 && meta::has_get<T, 0>)
    {
        return (meta::tuple_get<0>(std::forward<T>(t)));
    }

    template <meta::default_constructible F, meta::tuple_like T>
        requires (meta::tuple_size<T> > 1)
    [[nodiscard]] constexpr decltype(auto) tuple_fold_left(T&& t)
        noexcept(nothrow_tuple_fold_left<F, 0, T>)
        requires(enable_tuple_fold_left<F, 0, T>)
    {
        return []<size_t... Is>(std::index_sequence<Is...>, auto&& t)
            noexcept(nothrow_tuple_fold_left<F, 0, T>) -> decltype(auto)
        {
            return (bertrand::fold_left<F>(meta::tuple_get<Is>(std::forward<T>(t))...));
        }(std::make_index_sequence<meta::tuple_size<T>>{}, std::forward<T>(t));
    }

}


/* Apply a pairwise reduction function over a non-empty, tuple-like container that is
indexable via `get<I>()` (as a member method, ADL function, or `std::get<I>()`) from
left to right, returning the accumulated result.  This is a generalization of `min()`,
`max()`, `sum()`, etc., which simply alias to specializations of this method for the
proper reduction.  User-defined algorithms can be provided to inject custom behavior,
as long as they are default constructible and invocable with each pair of arguments.
The return type deduces to the common type between each invocation, assuming one
exists.  The function will fail to compile if these requirements are not met. */
template <meta::default_constructible F, meta::tuple_like T>
    requires (meta::tuple_size<T> > 0)
[[nodiscard]] constexpr decltype(auto) fold_left(T&& t)
    noexcept(noexcept(impl::tuple_fold_left<F>(std::forward<T>(t))))
    requires(requires{impl::tuple_fold_left<F>(std::forward<T>(t));})
{
    if constexpr (meta::tuple_size<T> == 1) {
        return (meta::tuple_get<0>(std::forward<T>(t)));
    } else {
        return []<size_t... Is>(std::index_sequence<Is...>, auto&& t) noexcept(
            impl::fold_left<F, meta::get_type<T, Is>...>::nothrow
        ) -> decltype(auto) {
            return (fold_left<F>(meta::tuple_get<Is>(std::forward<T>(t))...));
        }(std::make_index_sequence<meta::tuple_size<T>>{}, std::forward<T>(t));
    }
}


/* Apply a pairwise reduction function over the arguments from right to left, returning
the accumulated result.  This is a generalization of `min()`, `max()`, `sum()`, etc.,
which alias to specializations of `fold_left()`, the left-to-right analogue for this
method.  User-defined algorithms can be provided to inject custom behavior, as long as
they are default constructible and invocable with each pair of arguments.  The return
type is deduced as the common type between each invocation, assuming one exists.  The
function will fail to compile if these requirements are not met. */
template <meta::default_constructible F, typename... Ts>
    requires (sizeof...(Ts) > 1)
[[nodiscard]] constexpr impl::fold_right<F, Ts...>::type fold_right(Ts&&... args)
    noexcept(impl::fold_right<F, Ts...>::nothrow)
    requires(impl::fold_right<F, Ts...>::enable)
{
    return [](
        this auto&& self,
        auto&& func,
        auto&& out,
        auto&& curr,
        auto&&... next
    )
        noexcept(impl::fold_right<F, Ts...>::nothrow) -> decltype(auto)
    {
        if constexpr (sizeof...(next) > 0) {
            return (std::forward<decltype(func)>(func)(
                std::forward<decltype(self)>(self)(
                    std::forward<decltype(func)>(func),
                    std::forward<decltype(curr)>(curr),
                    std::forward<decltype(next)>(next)...
                ),
                std::forward<decltype(out)>(out)
            ));
        } else {
            return (std::forward<decltype(func)>(func)(
                std::forward<decltype(curr)>(curr),
                std::forward<decltype(out)>(out)
            ));
        }
    }(F{}, std::forward<Ts>(args)...);
}


#ifdef __cpp_lib_ranges_fold

    /* Apply a pairwise reduction function over an iterable range from left to right,
    returning the accumulated result.  This is a generalization of `min()`, `max()`,
    `sum()`, etc., which simply alias to specializations of this method for the proper
    reduction.  User-defined algorithms can be provided to inject custom behavior, as
    long as they are default constructible and invocable with each pair of arguments.
    The return type is deduced as `Optional<T>`, where `T` is the common type between each
    invocation, assuming one exists.  If the input range is empty, then the result will
    be in the empty state.  Otherwise, it will contain the accumulated result.  This is
    equivalent to a `std::ranges::fold_right()` call under the hood, but explicitly checks
    for an empty range and encodes the result as an optional, rather than invoking
    undefined behavior. */
    template <meta::default_constructible F, meta::iterable T>
        requires (!meta::tuple_like<T>)
    [[nodiscard]] constexpr auto fold_right(T&& r)
        noexcept(
            noexcept(std::ranges::begin(r) == std::ranges::end(r)) &&
            noexcept(std::ranges::fold_right(
                std::ranges::begin(r),
                std::ranges::end(r),
                *std::ranges::begin(r),
                F{}
            )) &&
            meta::nothrow::convertible_to<
                decltype(std::ranges::fold_right(
                    std::ranges::begin(r),
                    std::ranges::end(r),
                    *std::ranges::begin(r),
                    F{}
                )),
                Optional<decltype(std::ranges::fold_right(
                    std::ranges::begin(r),
                    std::ranges::end(r),
                    *std::ranges::begin(r),
                    F{}
                ))>
            >
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


namespace impl {

    
    template <typename F, size_t I, meta::tuple_like tuple, typename... Ts>
    constexpr bool enable_tuple_fold_right =
        requires(Ts... ts) {bertrand::fold_right<F>(ts...);};
    template <typename F, size_t I, meta::tuple_like tuple, typename... Ts>
        requires (I < meta::tuple_size<tuple>)
    constexpr bool enable_tuple_fold_right<F, I, tuple, Ts...> =
        enable_tuple_fold_right<F, I + 1, tuple, Ts..., meta::get_type<tuple, I>>;

    template <typename F, size_t I, meta::tuple_like tuple, typename... Ts>
    constexpr bool nothrow_tuple_fold_right =
        noexcept(noexcept(bertrand::fold_right<F>(std::declval<Ts>()...)));
    template <typename F, size_t I, meta::tuple_like tuple, typename... Ts>
        requires (I < meta::tuple_size<tuple>)
    constexpr bool nothrow_tuple_fold_right<F, I, tuple, Ts...> =
        nothrow_tuple_fold_right<F, I + 1, tuple, Ts..., meta::get_type<tuple, I>>;

    template <meta::default_constructible F, meta::tuple_like T>
    [[nodiscard]] constexpr decltype(auto) tuple_fold_right(T&& t)
        noexcept(noexcept(meta::tuple_get<0>(std::forward<T>(t))))
        requires(meta::tuple_size<T> == 1 && meta::has_get<T, 0>)
    {
        return (meta::tuple_get<0>(std::forward<T>(t)));
    }

    template <meta::default_constructible F, meta::tuple_like T>
        requires (meta::tuple_size<T> > 1)
    [[nodiscard]] constexpr decltype(auto) tuple_fold_right(T&& t)
        noexcept(nothrow_tuple_fold_right<F, 0, T>)
        requires(enable_tuple_fold_right<F, 0, T>)
    {
        return []<size_t... Is>(std::index_sequence<Is...>, auto&& t)
            noexcept(nothrow_tuple_fold_right<F, 0, T>) -> decltype(auto)
        {
            return (bertrand::fold_right<F>(meta::tuple_get<Is>(std::forward<T>(t))...));
        }(std::make_index_sequence<meta::tuple_size<T>>{}, std::forward<T>(t));
    }

}


/* Apply a pairwise reduction function over a non-empty, tuple-like container that is
indexable via `get<I>()` (as a member method, ADL function, or `std::get<I>()`) from
right to left, returning the accumulated result.  This is a generalization of `min()`,
`max()`, `sum()`, etc., which simply alias to specializations of this method for the
proper reduction.  User-defined algorithms can be provided to inject custom behavior,
as long as they are default constructible and invocable with each pair of arguments.
The return type deduces to the common type between each invocation, assuming one
exists.  The function will fail to compile if these requirements are not met. */
template <meta::default_constructible F, meta::tuple_like T>
    requires (meta::tuple_size<T> > 0)
[[nodiscard]] constexpr decltype(auto) fold_right(T&& t)
    noexcept(noexcept(impl::tuple_fold_right<F>(std::forward<T>(t))))
    requires(requires{impl::tuple_fold_right<F>(std::forward<T>(t));})
{
    if constexpr (meta::tuple_size<T> == 1) {
        return (meta::tuple_get<0>(std::forward<T>(t)));
    } else {
        return []<size_t... Is>(std::index_sequence<Is...>, auto&& t) noexcept(
            impl::fold_right<F, meta::get_type<T, Is>...>::nothrow
        ) -> decltype(auto) {
            return (fold_right<F>(meta::tuple_get<Is>(std::forward<T>(t))...));
        }(std::make_index_sequence<meta::tuple_size<T>>{}, std::forward<T>(t));
    }
}


/// TODO: it's possible that if I replicate the logic from `fold_left()` internally,
/// then `min()` and `max()` can be slightly more efficient, since they won't need
/// to convert to the common type until the very end of the template recursion.


/* Left-fold to obtain the minimum value of a sequence of arguments, a tuple-like
container, or an iterable range.  This is equivalent to a `fold_left(...)` call
under the hood, using a boolean choice predicate that defaults to a `<` comparison
between each element.  User-defined predicates can be provided to customize the
comparison, as long as they are default constructible, invocable with each pair of
elements, and return a contextually convertible boolean value.  If the predicate
returns true, then the left-hand side is considered to be less than the right-hand
side.  The overall return type is deduced as the common type for all elements, assuming
such a type exists.  If an iterable sequence is passed as a unary argument, the result
type will be promoted to an `Optional<T>`, where an empty container correlates with an
empty optional.  The function will fail to compile if any of these requirements are not
met. */
template <meta::default_constructible F = impl::Less, typename... Ts>
[[nodiscard]] constexpr decltype(auto) min(Ts&&... args)
    noexcept(noexcept(fold_left<impl::reverse_arguments<impl::choose_left<F>>>(
        std::forward<Ts>(args)...
    )))
    requires(requires{fold_left<impl::reverse_arguments<impl::choose_left<F>>>(
        std::forward<Ts>(args)...
    );})
{
    return (fold_left<impl::reverse_arguments<impl::choose_left<F>>>(
        std::forward<Ts>(args)...
    ));
}


/* Left-fold to obtain the maximum value of a sequence of arguments, a tuple-like
container, or an iterable range.  This is equivalent to a `fold_left(...)` call
under the hood, using a boolean choice predicate that defaults to a `<` comparison
between each element.  User-defined predicates can be provided to customize the
comparison, as long as they are default constructible, invocable with each pair of
elements, and return a contextually convertible boolean value.  If the predicate
returns true, then the left-hand side is considered to be less than the right-hand
side.  The overall return type is deduced as the common type for all elements, assuming
such a type exists.  If an iterable sequence is passed as a unary argument, the result
type will be promoted to an `Optional<T>`, where an empty container correlates with an
empty optional.  The function will fail to compile if any of these requirements are not
met. */
template <meta::default_constructible F = impl::Less, typename... Ts>
[[nodiscard]] constexpr decltype(auto) max(Ts&&... args)
    noexcept(noexcept(fold_left<impl::choose_right<F>>(std::forward<Ts>(args)...)))
    requires(requires{fold_left<impl::choose_right<F>>(std::forward<Ts>(args)...);})
{
    return (fold_left<impl::choose_right<F>>(std::forward<Ts>(args)...));
}







/// TODO: minmax() may require a separate implementation in order to efficiently track
/// both results.
/// TODO: make sure this works as intended with respect to order stability as well.
/// It might also be beneficial to write out the full comparison matrix, to minimize
/// comparisons.  If `x < min`, then we don't need to check `max < x`, and vice versa.



template <meta::default_constructible F = impl::Less, typename... Ts>
    requires (sizeof...(Ts) > 1)
[[nodiscard]] constexpr decltype(auto) minmax(Ts&&... args)
    noexcept(
        impl::fold_left<impl::reverse_arguments<impl::choose_left<F>>, Ts...>::nothrow &&
        impl::fold_left<impl::choose_right<F>, Ts...>::nothrow
    )
    requires(
        impl::fold_left<impl::reverse_arguments<impl::choose_left<F>>, Ts...>::enable &&
        impl::fold_left<impl::choose_right<F>, Ts...>::enable
    )
{
    return ([](
        this auto&& self,
        auto&& func,
        auto&& min_val,
        auto&& max_val,
        auto&& curr,
        auto&&... next
    )
        noexcept(
            impl::fold_left<impl::reverse_arguments<impl::choose_left<F>>, Ts...>::nothrow &&
            impl::fold_left<impl::choose_right<F>, Ts...>::nothrow
        ) -> decltype(auto)
    {
        if constexpr (sizeof...(next) > 0) {
            if (std::forward<decltype(func)>(func)(
                std::forward<decltype(curr)>(curr),
                std::forward<decltype(min_val)>(min_val)
            )) {
                return (std::forward<decltype(self)>(self)(
                    std::forward<decltype(func)>(func),
                    std::forward<decltype(curr)>(curr),
                    std::forward<decltype(max_val)>(max_val),
                    std::forward<decltype(next)>(next)...
                ));
            }
            if (std::forward<decltype(func)>(func)(
                std::forward<decltype(max_val)>(max_val),
                std::forward<decltype(curr)>(curr)
            )) {
                return (std::forward<decltype(self)>(self)(
                    std::forward<decltype(func)>(func),
                    std::forward<decltype(min_val)>(min_val),
                    std::forward<decltype(curr)>(curr),
                    std::forward<decltype(next)>(next)...
                ));
            }
            return (std::forward<decltype(self)>(self)(
                std::forward<decltype(func)>(func),
                std::forward<decltype(min_val)>(min_val),
                std::forward<decltype(max_val)>(max_val),
                std::forward<decltype(next)>(next)...
            ));
        } else {
            using min = impl::fold_left<
                impl::reverse_arguments<impl::choose_left<F>>,
                Ts...
            >::type;
            using max = impl::fold_left<impl::choose_right<F>, Ts...>::type;
            if (std::forward<decltype(func)>(func)(
                std::forward<decltype(curr)>(curr),
                std::forward<decltype(min_val)>(min_val)
            )) {
                return std::pair<min, max>{
                    std::forward<decltype(curr)>(curr),
                    std::forward<decltype(max_val)>(max_val)
                };
            }
            if (std::forward<decltype(func)>(func)(
                std::forward<decltype(max_val)>(max_val),
                std::forward<decltype(curr)>(curr)
            )) {
                return std::pair<min, max>{
                    std::forward<decltype(min_val)>(min_val),
                    std::forward<decltype(curr)>(curr)
                };
            }
            return std::pair<min, max>{
                std::forward<decltype(min_val)>(min_val),
                std::forward<decltype(max_val)>(max_val)
            };
        }
    }(
        F{},
        meta::unpack_arg<0>(std::forward<Ts>(args)...),
        std::forward<Ts>(args)...
    ));
}


/// TODO: range and tuple-like minmax() should be relatively straightforward
/// modifications of the above, according to the style of fold_left and fold_right







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



    static constexpr auto sum = fold_left<impl::Add>(1, 2, 3.25);
    static_assert(sum == 6.25);

    static constexpr auto min = fold_left<impl::choose_left<impl::Less>>(1, 2, 3.25);
    static_assert(min == 1);

    static constexpr auto max = fold_right<impl::choose_right<impl::Less>>(1, 2, 3.25);
    static_assert(max == 3.25);

    static constexpr std::string a = "a", b = "b", c ="c", d = "a", e = "c";
    static_assert(fold_left<impl::Add>(a, b, c) == "abc");
    static_assert(fold_right<impl::Add>(a, b, c) == "cba");
    static_assert(fold_left<impl::choose_left<impl::Less>>(a, b, c) == "a");
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





/* Get the minimum and maximum of an arbitrary list of values that share a common type.
This effectively generates an O(n) sequence of `<` comparisons between each argument at
compile time, converting the winning values to the common type at the end. */
template <typename... Ts>
    requires (
        sizeof...(Ts) > 1 &&
        meta::has_common_type<Ts...> &&
        impl::broadcast_lt<Ts...>
    )
[[nodiscard]] constexpr auto minmax(Ts&&... args)
    noexcept(
        impl::nothrow_broadcast_lt<Ts...> &&
        (meta::nothrow::convertible_to<Ts, meta::common_type<Ts...>> && ...)
    )
    -> std::pair<meta::common_type<Ts...>, meta::common_type<Ts...>>
{
    return []<size_t I = 0>(
        this auto&& self,
        auto&& min,
        auto&& max,
        auto&&... rest
    ) -> std::pair<meta::common_type<Ts...>, meta::common_type<Ts...>> {
        if constexpr (I < sizeof...(rest)) {
            if (
                meta::unpack_arg<I>(std::forward<decltype(rest)>(rest)...) <
                std::forward<decltype(min)>(min)
            ) {
                return std::forward<decltype(self)>(self).template operator()<I + 1>(
                    meta::unpack_arg<I>(std::forward<decltype(rest)>(rest)...),
                    std::forward<decltype(max)>(max),
                    std::forward<decltype(rest)>(rest)...
                );
            }
            if (
                std::forward<decltype(max)>(max) <
                meta::unpack_arg<I>(std::forward<decltype(rest)>(rest)...)
            ) {
                return std::forward<decltype(self)>(self).template operator()<I + 1>(
                    std::forward<decltype(min)>(min),
                    meta::unpack_arg<I>(std::forward<decltype(rest)>(rest)...),
                    std::forward<decltype(rest)>(rest)...
                );
            }
            return std::forward<decltype(self)>(self).template operator()<I + 1>(
                std::forward<decltype(min)>(min),
                std::forward<decltype(max)>(max),
                std::forward<decltype(rest)>(rest)...
            );
        } else {
            return {std::forward<decltype(min)>(min), std::forward<decltype(max)>(max)};
        }
    }(meta::unpack_arg<0>(std::forward<Ts>(args)...), std::forward<Ts>(args)...);
}


/* Get the minimum and maximum of the values stored within a tuple-like container.
This effectively generates an O(n) sequence of `<` comparisons between each argument
at compile time, converting the winning values to the common type at the end. */
template <meta::tuple_like T>
    requires (
        !meta::iterable<T> &&
        meta::tuple_size<T> > 0 &&
        meta::has_common_tuple_type<T> &&
        impl::tuple_broadcast_lt<T>
    )
[[nodiscard]] constexpr auto minmax(T&& t)
    noexcept(noexcept(impl::broadcast_minmax(std::forward<T>(t))))
    -> std::pair<meta::common_tuple_type<T>, meta::common_tuple_type<T>>
    requires(requires{impl::broadcast_minmax(std::forward<T>(t));})
{
    return impl::broadcast_minmax(std::forward<T>(t));
}


/* Get the minimum and maximum of a sequence of values as an input range.  This is
equivalent to a `std::ranges::minmax()` call under the hood, but explicitly checks for
an empty range and returns an empty optional, rather than invoking undefined
behavior. */
template <meta::iterable T>
[[nodiscard]] constexpr auto minmax(T&& r)
    noexcept(
        noexcept(std::ranges::empty(r)) &&
        noexcept(std::ranges::minmax(std::forward<T>(r))) &&
        meta::nothrow::convertible_to<
            decltype(std::ranges::minmax(std::forward<T>(r))),
            Optional<decltype(std::ranges::minmax(std::forward<T>(r)))>
        >
    )
    -> Optional<decltype(std::ranges::minmax(std::declval<T>()))>
    requires(requires{
        { std::ranges::empty(r) } -> meta::explicitly_convertible_to<bool>;
        { std::ranges::minmax(std::forward<T>(r)) } -> meta::convertible_to<
            Optional<decltype(std::ranges::minmax(std::forward<T>(r)))>
        >;
    })
{
    if (std::ranges::empty(r)) {
        return std::nullopt;
    }
    return std::ranges::minmax(std::forward<T>(r));
}


}


#endif