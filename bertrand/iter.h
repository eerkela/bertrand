#ifndef BERTRAND_ITER_H
#define BERTRAND_ITER_H

#include "bertrand/common.h"
#include "bertrand/except.h"


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

    /* A generic sentinel type to simplify iterator implementations. */
    struct sentinel {};

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

        template <typename V> requires (meta::assignable<reference, V>)
        constexpr contiguous_iterator& operator=(V&& value) && noexcept(
            noexcept(*ptr = std::forward<V>(value))
        ) {
            *ptr = std::forward<V>(value);
            return *this;
        }

        template <typename V> requires (meta::convertible_to<reference, V>)
        [[nodiscard]] constexpr operator V() && noexcept(noexcept(V(**this))) {
            return **this;
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

        template <meta::more_qualified_than<T> U>
        [[nodiscard]] constexpr operator contiguous_iterator<U>() noexcept {
            return {ptr};
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

        template <typename V> requires (meta::assignable<reference, V>)
        constexpr contiguous_iterator& operator=(V&& value) && {
            check(*this);
            *ptr = std::forward<V>(value);
            return *this;
        }

        template <typename V> requires (meta::convertible_to<reference, V>)
        [[nodiscard]] constexpr operator V() && {
            check(*this);
            return **this;
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

        template <meta::more_qualified_than<T> U>
        [[nodiscard]] constexpr operator contiguous_iterator<U>() {
            return {ptr, check};
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

        constexpr contiguous_slice(const contiguous_slice&) = default;
        constexpr contiguous_slice(contiguous_slice&&) = default;
        constexpr contiguous_slice& operator=(const contiguous_slice&) = default;
        constexpr contiguous_slice& operator=(contiguous_slice&&) = default;

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


    /* Check whether the tuple type at index `I` are truth-testable as part of an
    `any()` or `all()` operation. */
    template <size_t I, typename T>
    concept broadcast_truthy = I >= meta::tuple_size<T> || (
        meta::has_get<T, I> &&
        meta::explicitly_convertible_to<meta::get_type<T, I>, bool>
    );

    template <size_t I, typename T>
    concept nothrow_broadcast_truthy = I >= meta::tuple_size<T> || (
        meta::nothrow::has_get<T, I> &&
        meta::nothrow::explicitly_convertible_to<meta::nothrow::get_type<T, I>, bool>
    );

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

    /* Check whether the intermediate result is addable to the tuple type at index `I`
    as part of a `sum()` operation.  */
    template <size_t I, typename T, typename Result>
    concept broadcast_plus = I >= meta::tuple_size<T> || (
        meta::has_get<T, I> &&
        meta::has_add<Result, meta::get_type<T, I>>
    );

    template <size_t I, typename T, typename Result>
    concept nothrow_broadcast_plus = I >= meta::tuple_size<T> || (
        meta::nothrow::has_get<T, I> &&
        meta::nothrow::has_add<Result, meta::nothrow::get_type<T, I>>
    );

    /* Implement `any()` for tuples. */
    template <size_t I = 0, meta::tuple_like T> requires (broadcast_truthy<I, T>)
    constexpr bool broadcast_any(T&& t) noexcept(nothrow_broadcast_truthy<I, T>) {
        if constexpr (I < std::tuple_size_v<meta::unqualify<T>>) {
            if constexpr (meta::has_member_get<T, I>) {
                if (std::forward<T>(t).template get<I>(t)) {
                    return true;
                }
            } else if constexpr (meta::has_adl_get<T, I>) {
                if (get<I>(std::forward<T>(t))) {
                    return true;
                }
            } else {
                if (std::get<I>(std::forward<T>(t))) {
                    return true;
                }
            }
            return broadcast_any<I + 1>(std::forward<T>(t));
        } else {
            return false;
        }
    }

    /* Implement `all()` for tuples. */
    template <size_t I = 0, meta::tuple_like T> requires (broadcast_truthy<I, T>)
    constexpr bool broadcast_all(T&& t) noexcept(nothrow_broadcast_truthy<I, T>) {
        if constexpr (I < std::tuple_size_v<meta::unqualify<T>>) {
            if constexpr (meta::has_member_get<T, I>) {
                if (!std::forward<T>(t).template get<I>(t)) {
                    return false;
                }
            } else if constexpr (meta::has_adl_get<T, I>) {
                if (!get<I>(std::forward<T>(t))) {
                    return false;
                }
            } else {
                if (!std::get<I>(std::forward<T>(t))) {
                    return false;
                }
            }
            return broadcast_all<I + 1>(std::forward<T>(t));
        } else {
            return true;
        }
    }

    /* Implement `max()` for tuples, using `out` as an intermediate result. */
    template <size_t I = 1, meta::tuple_like T, typename Result>
        requires (broadcast_min_max<I, T, Result>)
    constexpr meta::common_tuple_type<T> broadcast_max(T&& t, Result&& out) noexcept(
        nothrow_broadcast_min_max<I, T, Result>
    ) {
        if constexpr (I < meta::tuple_size<T>) {
            if constexpr (meta::has_member_get<T, I>) {
                if (std::forward<Result>(out) < std::forward<T>(t).template get<I>(t)) {
                    return broadcast_max<I + 1>(
                        std::forward<T>(t),
                        std::forward<T>(t).template get<I>(t)
                    );
                }
            } else if constexpr (meta::has_adl_get<T, I>) {
                if (std::forward<Result>(out) < get<I>(std::forward<T>(t))) {
                    return broadcast_max<I + 1>(
                        std::forward<T>(t),
                        get<I>(std::forward<T>(t))
                    );
                }
            } else {
                if (std::forward<Result>(out) < std::get<I>(std::forward<T>(t))) {
                    return broadcast_max<I + 1>(
                        std::forward<T>(t),
                        std::get<I>(std::forward<T>(t))
                    );
                }
            }
            return broadcast_max<I + 1>(
                std::forward<T>(t),
                std::forward<Result>(out)
            );
        } else {
            return std::forward<Result>(out);
        }
    }

    /* Implement `max()` for tuples, initializing the intermediate result to the
    first value in the tuple. */
    template <meta::tuple_like T> requires (meta::tuple_size<T> > 0)
    constexpr meta::common_tuple_type<T> broadcast_max(T&& t) noexcept(
        meta::nothrow::has_get<T, 0> &&
        noexcept(broadcast_max(
            std::forward<T>(t),
            std::declval<meta::get_type<T, 0>>()
        ))
    ) {
        if constexpr (meta::has_member_get<T, 0>) {
            return broadcast_max(
                std::forward<T>(t),
                std::forward<T>(t).template get<0>(t)
            );
        } else if constexpr (meta::has_adl_get<T, 0>) {
            return broadcast_max(
                std::forward<T>(t),
                get<0>(std::forward<T>(t))
            );
        } else {
            return broadcast_max(
                std::forward<T>(t),
                std::get<0>(std::forward<T>(t))
            );
        }
    }

    /* Implement `min()` for tuples, using `out` as an intermediate result. */
    template <size_t I = 1, meta::tuple_like T, typename Result>
        requires (broadcast_min_max<I, T, Result>)
    constexpr meta::common_tuple_type<T> broadcast_min(T&& t, Result&& out) noexcept(
        nothrow_broadcast_min_max<I, T, Result>
    ) {
        if constexpr (I < meta::tuple_size<T>) {
            if constexpr (meta::has_member_get<T, I>) {
                if (std::forward<T>(t).template get<I>(t) < std::forward<Result>(out)) {
                    return broadcast_min<I + 1>(
                        std::forward<T>(t),
                        std::forward<T>(t).template get<I>(t)
                    );
                }
            } else if constexpr (meta::has_adl_get<T, I>) {
                if (get<I>(std::forward<T>(t)) < std::forward<Result>(out)) {
                    return broadcast_min<I + 1>(
                        std::forward<T>(t),
                        get<I>(std::forward<T>(t))
                    );
                }
            } else {
                if (std::get<I>(std::forward<T>(t)) < std::forward<Result>(out)) {
                    return broadcast_min<I + 1>(
                        std::forward<T>(t),
                        std::get<I>(std::forward<T>(t))
                    );
                }
            }
            return broadcast_min<I + 1>(
                std::forward<T>(t),
                std::forward<Result>(out)
            );
        } else {
            return std::forward<Result>(out);
        }
    }

    /* Implement `min()` for tuples, initializing the intermediate result to the
    first value in the tuple. */
    template <meta::tuple_like T> requires (meta::tuple_size<T> > 0)
    constexpr meta::common_tuple_type<T> broadcast_min(T&& t) noexcept(
        meta::nothrow::has_get<T, 0> &&
        noexcept(broadcast_min(
            std::forward<T>(t),
            std::declval<meta::get_type<T, 0>>()
        ))
    ) {
        if constexpr (meta::has_member_get<T, 0>) {
            return broadcast_min(
                std::forward<T>(t),
                std::forward<T>(t).template get<0>(t)
            );
        } else if constexpr (meta::has_adl_get<T, 0>) {
            return broadcast_min(
                std::forward<T>(t),
                get<0>(std::forward<T>(t))
            );
        } else {
            return broadcast_min(
                std::forward<T>(t),
                std::get<0>(std::forward<T>(t))
            );
        }
    }

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

    /* Implement `sum()` for tuples, using `out` as an intermediate result. */
    template <size_t I = 1, meta::tuple_like T, typename Result>
        requires (broadcast_plus<I, T, Result>)
    constexpr auto broadcast_sum(T&& t, Result&& result) noexcept(
        nothrow_broadcast_plus<I, T, Result>
    ) {
        if constexpr (I < meta::tuple_size<T>) {
            if constexpr (meta::has_member_get<T, I>) {
                return broadcast_sum<I + 1>(
                    std::forward<T>(t),
                    std::forward<Result>(result) +
                        std::forward<T>(t).template get<I>(t)
                );
            } else if constexpr (meta::has_adl_get<T, I>) {
                return broadcast_sum<I + 1>(
                    std::forward<T>(t),
                    std::forward<Result>(result) +
                        get<I>(std::forward<T>(t))
                );
            } else {
                return broadcast_sum<I + 1>(
                    std::forward<T>(t),
                    std::forward<Result>(result) +
                        std::get<I>(std::forward<T>(t))
                );
            }
        } else {
            return std::forward<Result>(result);
        }
    }

    /* Implement `sum()` for tuples, initializing the intermediate result to the
    first value in the tuple. */
    template <meta::tuple_like T> requires (meta::tuple_size<T> > 0)
    constexpr decltype(auto) broadcast_sum(T&& t) noexcept(
        meta::nothrow::has_get<T, 0> &&
        noexcept(broadcast_sum(
            std::forward<T>(t),
            std::declval<meta::get_type<T, 0>>()
        ))
    ) {
        if constexpr (meta::has_member_get<T, 0>) {
            return broadcast_sum(
                std::forward<T>(t),
                std::forward<T>(t).template get<0>(t)
            );
        } else if constexpr (meta::has_adl_get<T, 0>) {
            return broadcast_sum(
                std::forward<T>(t),
                get<0>(std::forward<T>(t))
            );
        } else {
            return broadcast_sum(
                std::forward<T>(t),
                std::get<0>(std::forward<T>(t))
            );
        }
    }

}


/* Get the length of an arbitrary sequence in constant time as a signed integer.
Equivalent to calling `std::ranges::ssize(range)`. */
template <meta::has_size Range>
[[nodiscard]] constexpr auto len(Range&& r) noexcept(
    noexcept(std::ranges::ssize(r))
) {
    return std::ranges::ssize(r);
}


/* Get the distance between two iterators as a signed integer.  Equivalent to calling
`std::ranges::distance(begin, end)`.  This may run in O(n) time if the iterators do
not support constant-time distance measurements. */
template <meta::iterator Begin, meta::sentinel_for<Begin> End>
[[nodiscard]] constexpr auto len(Begin&& begin, End&& end) noexcept(
    noexcept(std::ranges::distance(begin, end))
) {
    return std::ranges::distance(begin, end);
}


/* Produce a simple range starting at a default-constructed instance of `End` (zero if
`End` is an integer type), similar to Python's built-in `range()` operator.  This is
equivalent to a  `std::views::iota()` call under the hood. */
template <typename End>
    requires (requires(End stop) {
        std::views::iota(End{}, std::forward<End>(stop));
    })
[[nodiscard]] constexpr auto range(End&& stop) noexcept(
    noexcept(std::views::iota(End{}, std::forward<End>(stop)))
) {
    return std::views::iota(End{}, std::forward<End>(stop));
}


/* Produce a simple range from `start` to `stop`, similar to Python's built-in
`range()` operator.  This is equivalent to a `std::views::iota()` call under the
hood. */
template <typename Begin, typename End>
    requires (
        !(meta::iterator<Begin> && meta::sentinel_for<End, Begin>) &&
        requires(Begin start, End stop) {
            std::views::iota(std::forward<Begin>(start), std::forward<End>(stop));
        }
    )
[[nodiscard]] constexpr auto range(Begin&& start, End&& stop) noexcept(
    noexcept(std::views::iota(std::forward<Begin>(start), std::forward<End>(stop)))
) {
    return std::views::iota(std::forward<Begin>(start), std::forward<End>(stop));
}


/// TODO: views::stride() is not available, so this is commented out for now.  I may
/// be able to implement it myself, but that would require a lot of unnecessary work.


// /* Produce a simple integer range, similar to Python's built-in `range()` operator.
// This is equivalent to a  `std::views::iota()` call under the hood. */
// template <typename T = ssize_t>
// inline constexpr auto range(T start, T stop, T step) {
//     return std::views::iota(start, stop) | std::views::stride(step);
// }


/* Produce a simple range object that encapsulates a `start` and `stop` iterator as a
range adaptor.  This is equivalent to a `std::ranges::subrange()` call under the
hood. */
template <meta::iterator Begin, meta::sentinel_for<Begin> End>
    requires (requires(Begin start, End stop) {
        std::ranges::subrange(std::forward<Begin>(start), std::forward<End>(stop));
    })
[[nodiscard]] constexpr auto range(Begin&& start, End&& stop) noexcept(
    noexcept(std::ranges::subrange(std::forward<Begin>(start), std::forward<End>(stop)))
) {
    return std::ranges::subrange(std::forward<Begin>(start), std::forward<End>(stop));
}


/// TODO: a stride view range() accepting begin/end iterators would be very nice.


/* Produce a view over a reverse iterable range that can be used in range-based for
loops.  This is equivalent to a `std::views::reverse()` call under the hood. */
template <meta::reverse_iterable T>
[[nodiscard]] constexpr auto reversed(T&& obj) noexcept(
    noexcept(std::views::reverse(std::forward<T>(obj)))
) {
    return std::views::reverse(std::forward<T>(obj));
}


/// TODO: there's no std::views::enumerate() yet, so this is commented out for now.
/// I may be able to implement it myself, but that would require a lot of unnecessary
/// work


// /* Produce a view over a given range that yields tuples consisting of each item's
// index and ordinary value_type.  This is equivalent to a `std::views::enumerate()` call
// under the hood, but is easier to remember, and closer to Python syntax. */
// template <meta::iterable T>
//     requires (requires(T r) { std::views::enumerate(std::forward<T>(r)); })
// [[nodiscard]] constexpr decltype(auto) enumerate(T&& r) noexcept(
//     noexcept(std::views::enumerate(std::forward<T>(r)))
// ) {
//     return std::views::enumerate(std::forward<T>(r));
// }


/* Combine several ranges into a view that yields tuple-like values consisting of the
`i` th element of each range.  This is equivalent to a `std::views::zip()` call under
the hood. */
template <meta::iterable... Ts>
    requires (requires(Ts... rs) { std::views::zip(std::forward<Ts>(rs)...); })
[[nodiscard]] constexpr decltype(auto) zip(Ts&&... rs) noexcept(
    noexcept(std::views::zip(std::forward<Ts>(rs)...))
) {
    return std::views::zip(std::forward<Ts>(rs)...);
}


/* Returns true if and only if one or more of the arguments evaluate to true under
boolean logic. */
template <meta::explicitly_convertible_to<bool>... Args> requires (sizeof...(Args) > 1)
[[nodiscard]] constexpr bool any(Args&&... args) noexcept(
    noexcept((static_cast<bool>(std::forward<Args>(args)) || ...))
) {
    return (static_cast<bool>(std::forward<Args>(args)) || ...);
}


/* Returns true if and only if one or more elements of a tuple-like container evaluate
to true under boolean logic. */
template <meta::tuple_like T>
    requires (
        !meta::iterable<T> &&
        requires(T t) { impl::broadcast_any(std::forward<T>(t)); }
    )
[[nodiscard]] constexpr bool any(T&& t) noexcept(
    noexcept(impl::broadcast_any(std::forward<T>(t)))
) {
    return impl::broadcast_any(std::forward<T>(t));
}


/* Returns true if and only if one ore more elements of a range evaluate to true under
boolean logic.  This is equivalent to a `std::ranges::any_of()` call under the hood. */
template <meta::iterable T>
    requires (requires(T r) { std::ranges::any_of(std::forward<T>(r)); })
[[nodiscard]] constexpr bool any(T&& r) noexcept(
    noexcept(std::ranges::any_of(std::forward<T>(r)))
) {
    return std::ranges::any_of(std::forward<T>(r));
}


/* Returns true if and only if all of the arguments evaluate to true under boolean
logic. */
template <meta::explicitly_convertible_to<bool>... Args> requires (sizeof...(Args) > 1)
[[nodiscard]] constexpr bool all(Args&&... args) noexcept(
    noexcept((static_cast<bool>(std::forward<Args>(args)) || ...))
) {
    return (static_cast<bool>(std::forward<Args>(args)) && ...);
}


/* Returns true if and only if any elements of a tuple-like container evaluate to true
under boolean logic. */
template <meta::tuple_like T>
    requires (
        !meta::iterable<T> &&
        requires(T t) { impl::broadcast_all(std::forward<T>(t)); }
    )
[[nodiscard]] constexpr bool all(T&& t) noexcept(
    noexcept(impl::broadcast_all(std::forward<T>(t)))
) {
    return impl::broadcast_all(std::forward<T>(t));
}


/* Returns true if and only if all elements of a range evaluate to true under boolean
logic.  This is equivalent to a `std::ranges::all_of()` call under the hood. */
template <meta::iterable T>
    requires (requires(T r) { std::ranges::all_of(std::forward<T>(r)); })
[[nodiscard]] constexpr bool all(T&& r) noexcept(
    noexcept(std::ranges::all_of(std::forward<T>(r)))
) {
    return std::ranges::all_of(std::forward<T>(r));
}


/* Get the minimum of an arbitrary list of values that share a common type.  This
effectively generates an O(n) sequence of `<` comparisons between each argument at
compile time, converting the winning value to the common type at the end. */
template <typename... Ts>
    requires (
        sizeof...(Ts) > 1 &&
        meta::has_common_type<Ts...> &&
        impl::broadcast_lt<Ts...>
    )
[[nodiscard]] constexpr meta::common_type<Ts...> min(Ts&&... args) noexcept(
    impl::nothrow_broadcast_lt<Ts...> &&
    (meta::nothrow::convertible_to<Ts, meta::common_type<Ts...>> && ...)
) {
    return []<size_t I = 0>(
        this auto&& self,
        auto&& out,
        auto&&... rest
    ) -> meta::common_type<Ts...> {
        if constexpr (I < sizeof...(rest)) {
            if (
                meta::unpack_arg<I>(std::forward<decltype(rest)>(rest)...) <
                std::forward<decltype(out)>(out)
            ) {
                return std::forward<decltype(self)>(self).template operator()<I + 1>(
                    meta::unpack_arg<I>(std::forward<decltype(rest)>(rest)...),
                    std::forward<decltype(rest)>(rest)...
                );
            } else {
                return std::forward<decltype(self)>(self).template operator()<I + 1>(
                    std::forward<decltype(out)>(out),
                    std::forward<decltype(rest)>(rest)...
                );
            }
        } else {
            return std::forward<decltype(out)>(out);
        }
    }(std::forward<Ts>(args)...);
}


/* Get the minimum of the values stored within a tuple-like container.  This
effectively generates an O(n) sequence of `<` comparisons between each argument at
compile time, converting the winning value to the common type at the end. */
template <meta::tuple_like T>
    requires (
        !meta::iterable<T> &&
        meta::tuple_size<T> > 0 &&
        meta::has_common_tuple_type<T> &&
        impl::tuple_broadcast_lt<T> &&
        requires(T t) { impl::broadcast_min(std::forward<T>(t)); }
    )
[[nodiscard]] constexpr meta::common_tuple_type<T> min(T&& t) noexcept(
    noexcept(impl::broadcast_min(std::forward<T>(t)))
) {
    return impl::broadcast_min(std::forward<T>(t));
}


/* Get the minimum of a sequence of values as an input range.  This is equivalent
to a `std::ranges::min()` call under the hood, but explicitly checks for an empty
range and throws a `ValueError`, rather than invoking undefined behavior. */
template <meta::iterable T>
    requires (requires(T r) { std::ranges::min(std::forward<T>(r)); })
[[nodiscard]] constexpr decltype(auto) min(T&& r) noexcept(
    !DEBUG && noexcept(std::ranges::min(std::forward<T>(r)))
) {
    if constexpr (DEBUG) {
        if (std::ranges::empty(r)) {
            throw ValueError("empty range has no max()");
        }
    }
    return std::ranges::min(std::forward<T>(r));
}


/* Get the maximum of an arbitrary list of values that share a common type.  This
effectively generates an O(n) sequence of `<` comparisons between each argument at
compile time, converting the winning value to the common type at the end. */
template <typename... Ts>
    requires (
        sizeof...(Ts) > 1 &&
        meta::has_common_type<Ts...> &&
        impl::broadcast_lt<Ts...>
    )
[[nodiscard]] constexpr meta::common_type<Ts...> max(Ts&&... args) noexcept(
    impl::nothrow_broadcast_lt<Ts...> &&
    (meta::nothrow::convertible_to<Ts, meta::common_type<Ts...>> && ...)
) {
    return []<size_t I = 0>(
        this auto&& self,
        auto&& out,
        auto&&... rest
    ) -> meta::common_type<Ts...> {
        if constexpr (I < sizeof...(rest)) {
            if (
                std::forward<decltype(out)>(out) <
                meta::unpack_arg<I>(std::forward<decltype(rest)>(rest)...)
            ) {
                return std::forward<decltype(self)>(self).template operator()<I + 1>(
                    meta::unpack_arg<I>(std::forward<decltype(rest)>(rest)...),
                    std::forward<decltype(rest)>(rest)...
                );
            } else {
                return std::forward<decltype(self)>(self).template operator()<I + 1>(
                    std::forward<decltype(out)>(out),
                    std::forward<decltype(rest)>(rest)...
                );
            }
        } else {
            return std::forward<decltype(out)>(out);
        }
    }(std::forward<Ts>(args)...);
}


/* Get the maximum of the values stored within a tuple-like container.  This
effectively generates an O(n) sequence of `<` comparisons between each argument at
compile time, converting the winning value to the common type at the end. */
template <meta::tuple_like T>
    requires (
        !meta::iterable<T> &&
        meta::tuple_size<T> > 0 &&
        meta::has_common_tuple_type<T> &&
        impl::tuple_broadcast_lt<T> &&
        requires(T t) { impl::broadcast_max(std::forward<T>(t)); }
    )
[[nodiscard]] constexpr meta::common_tuple_type<T> max(T&& t) noexcept(
    noexcept(impl::broadcast_max(std::forward<T>(t)))
) {
    return impl::broadcast_max(std::forward<T>(t));
}


/* Get the maximum of a sequence of values as an input range.  This is equivalent
to a `std::ranges::max()` call under the hood, but explicitly checks for an empty
range and throws a `ValueError`, rather than invoking undefined behavior. */
template <meta::iterable T>
    requires (requires(T r) { std::ranges::max(std::forward<T>(r)); })
[[nodiscard]] constexpr decltype(auto) max(T&& r) noexcept(
    !DEBUG && noexcept(std::ranges::max(std::forward<T>(r)))
) {
    if constexpr (DEBUG) {
        if (std::ranges::empty(r)) {
            throw ValueError("empty range has no max()");
        }
    }
    return std::ranges::max(std::forward<T>(r));
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
[[nodiscard]] constexpr auto minmax(Ts&&... args) noexcept(
    impl::nothrow_broadcast_lt<Ts...> &&
    (meta::nothrow::convertible_to<Ts, meta::common_type<Ts...>> && ...)
) -> std::pair<meta::common_type<Ts...>, meta::common_type<Ts...>> {
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
        impl::tuple_broadcast_lt<T> &&
        requires(T t) { impl::broadcast_minmax(std::forward<T>(t)); }
    )
[[nodiscard]] constexpr auto minmax(T&& t) noexcept(
    noexcept(impl::broadcast_minmax(std::forward<T>(t)))
) -> std::pair<meta::common_tuple_type<T>, meta::common_tuple_type<T>> {
    return impl::broadcast_minmax(std::forward<T>(t));
}


/* Get the minimum and maximum of a sequence of values as an input range.  This is
equivalent to a `std::ranges::minmax()` call under the hood, but explicitly checks for
an empty range and throws a `ValueError`, rather than invoking undefined behavior. */
template <meta::iterable T>
    requires (requires(T r) { std::ranges::minmax(std::forward<T>(r)); })
[[nodiscard]] constexpr decltype(auto) minmax(T&& r) noexcept(
    !DEBUG && noexcept(std::ranges::minmax(std::forward<T>(r)))
) {
    if constexpr (DEBUG) {
        if (std::ranges::empty(r)) {
            throw ValueError("empty range has no min() or max()");
        }
    }
    return std::ranges::minmax(std::forward<T>(r));
}


/* Calculate the sum of an arbitrary list of values that can be added from left to
right.  This effectively generates an O(n) sequence of `+` operators between each
argument, equivalent to a fold expression. */
template <typename... Ts>
    requires (
        sizeof...(Ts) > 1 &&
        requires(Ts... args) { (std::forward<Ts>(args) + ...); }
    )
[[nodiscard]] constexpr decltype(auto) sum(Ts&&... args) noexcept(
    noexcept((std::forward<Ts>(args) + ...))
) {
    return (std::forward<Ts>(args) + ...);
}


/* Calculate the sum of the values stored within a tuple-like container.  This
effectively generates an O(n) sequence of `+` operators between each value at compile
time, equivalent to a fold expression over the tuple's contents. */
template <meta::tuple_like T>
    requires (
        !meta::iterable<T> &&
        meta::tuple_size<T> > 0 &&
        requires(T t) {impl::broadcast_sum(std::forward<T>(t)); }
    )
[[nodiscard]] constexpr auto sum(T&& t) noexcept(
    noexcept(impl::broadcast_sum(std::forward<T>(t)))
) {
    return impl::broadcast_sum(std::forward<T>(t));
}


/* Calculate the sum of a sequence of values as an input range.  This is equivalent
to a `std::ranges::fold_left()` call under the hood, but uses the first value in the
range as an initializer, and throws a `ValueError` if it is empty.  Use the full
ranges algorithm if you need to set an explicit start value. */
template <meta::iterable T>
    requires (requires(T r) {
        std::ranges::fold_left(
            std::ranges::begin(r),
            std::ranges::end(r),
            *std::ranges::begin(r),
            std::plus<>{}
        );
    })
[[nodiscard]] constexpr decltype(auto) sum(T&& r) noexcept(
    !DEBUG &&
    noexcept(std::ranges::fold_left(
        std::ranges::begin(r),
        std::ranges::end(r),
        *std::ranges::begin(r),
        std::plus<>{}
    ))
) {
    auto it = std::ranges::begin(r);
    auto end = std::ranges::end(r);
    if constexpr (DEBUG) {
        if (it == end) {
            throw ValueError("cannot sum an empty range");
        }
    }
    auto init = *it;
    ++it;
    return std::ranges::fold_left(
        it,
        end,
        std::move(init),
        std::plus<>{}
    );
}


}


#endif