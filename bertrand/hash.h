#ifndef BERTRAND_HASH_H
#define BERTRAND_HASH_H

#include "bertrand/static_str.h"


namespace bertrand {


namespace impl {

    /* A helper struct that computes a gperf-style minimal perfect hash function over
    the given strings at compile time.  Only the N most variable characters are
    considered, where N is minimized using an associative array containing relative
    weights for each character. */
    template <static_str... Keys>
    struct minimal_perfect_hash {
        static constexpr size_t table_size = sizeof...(Keys);

    private:
        template <size_t>
        struct _minmax {
            static constexpr std::pair<size_t, size_t> value =
                std::minmax({Keys.size()...});
        };
        template <>
        struct _minmax<0> {
            static constexpr std::pair<size_t, size_t> value = {0, 0};
        };
        static constexpr auto minmax = _minmax<table_size>::value;

    public:
        static constexpr size_t min_length = minmax.first;
        static constexpr size_t max_length = minmax.second;

        template <size_t I> requires (I < table_size)
        static constexpr static_str at = meta::unpack_string<I, Keys...>;

    private:
        using Weights = std::array<unsigned char, 256>;

        template <static_str...>
        struct _counts {
            static constexpr size_t operator()(unsigned char, size_t) { return 0; }
        };
        template <static_str First, static_str... Rest>
        struct _counts<First, Rest...> {
            static constexpr size_t operator()(unsigned char c, size_t pos) {
                return _counts<Rest...>{}(c, pos) + (pos < First.size() && First[pos] == c);
            }
        };
        static constexpr size_t counts(unsigned char c, size_t pos) {
            return _counts<Keys...>{}(c, pos);
        }

        template <size_t I, unsigned char C, static_str... Strings>
        static constexpr size_t first_occurrence = 0;
        template <size_t I, unsigned char C, static_str First, static_str... Rest>
        static constexpr size_t first_occurrence<I, C, First, Rest...> =
            (I < First.size() && First[I] == C) ?
                0 : first_occurrence<I, C, Rest...> + 1;

        template <size_t I, size_t J, static_str... Strings>
        static constexpr size_t _variation = 0;
        template <size_t I, size_t J, static_str First, static_str... Rest>
        static constexpr size_t _variation<I, J, First, Rest...> =
            (I < First.size() && J == first_occurrence<I, First[I], Keys...>) +
            _variation<I, J + 1, Rest...>;
        template <size_t I, static_str... Strings>
        static constexpr size_t variation = _variation<I, 0, Strings...>;

        /* An array holding the number of unique characters across each index of the
        input keys, up to `max_length`. */
        static constexpr std::array<size_t, max_length> frequencies =
            []<size_t... Is>(std::index_sequence<Is...>) {
                return std::array<size_t, max_length>{variation<Is, Keys...>...};
            }(std::make_index_sequence<max_length>{});

        /* A sorted array holding indices into the frequencies table, with the highest
        variation indices coming first. */
        static constexpr std::array<size_t, max_length> sorted_freq_indices =
            []<size_t... Is>(std::index_sequence<Is...>) {
                std::array<size_t, max_length> result {Is...};
                std::sort(result.begin(), result.end(), [](size_t a, size_t b) {
                    return frequencies[a] > frequencies[b];
                });
                return result;
            }(std::make_index_sequence<max_length>{});

        using collision = std::pair<std::string_view, std::string_view>;

        /* Check to see if the candidate weights produce any collisions for a given
        number of significant characters. */
        template <static_str...>
        struct collisions {
            static constexpr collision operator()(const Weights&, size_t) {
                return {"", ""};
            }
        };
        template <static_str First, static_str... Rest>
        struct collisions<First, Rest...> {
            template <static_str...>
            struct scan {
                static constexpr collision operator()(
                    std::string_view,
                    size_t,
                    const Weights&,
                    size_t
                ) {
                    return {"", ""};
                }
            };
            template <static_str F, static_str... Rs>
            struct scan<F, Rs...> {
                static constexpr collision operator()(
                    std::string_view orig,
                    size_t idx,
                    const Weights& weights,
                    size_t significant_chars
                ) {
                    size_t hash = 0;
                    for (size_t i = 0; i < significant_chars; ++i) {
                        size_t pos = sorted_freq_indices[i];
                        unsigned char c = pos < F.size() ? F[pos] : 0;
                        hash += weights[c];
                    }
                    if ((hash % table_size) == idx) {
                        return {orig, {F.buffer, F.size()}};
                    }
                    return scan<Rs...>{}(orig, idx, weights, significant_chars);
                }
            };

            static constexpr collision operator()(
                const Weights& weights,
                size_t significant_chars
            ) {
                size_t hash = 0;
                for (size_t i = 0; i < significant_chars; ++i) {
                    size_t pos = sorted_freq_indices[i];
                    unsigned char c = pos < First.size() ? First[pos] : 0;
                    hash += weights[c];
                }
                collision result = scan<Rest...>{}(
                    std::string_view{First.buffer, First.size()},
                    hash % table_size,
                    weights,
                    significant_chars
                );
                if (result.first != result.second) {
                    return result;
                }
                return collisions<Rest...>{}(weights, significant_chars);
            }
        };

        /* Finds an associative value array that produces perfect hashes over the input
        keywords. */
        static constexpr auto find_hash = [] -> std::tuple<size_t, Weights, bool> {
            Weights weights;

            for (size_t i = 0; i <= max_length; ++i) {
                weights.fill(1);
                for (size_t j = 0; j < TEMPLATE_RECURSION_LIMIT; ++j) {
                    collision result = collisions<Keys...>{}(weights, i);
                    if (result.first == result.second) {
                        return {i, weights, true};
                    }
                    bool identical = true;
                    for (size_t k = 0; k < i; ++k) {
                        size_t pos = sorted_freq_indices[k];
                        unsigned char c1 = pos < result.first.size() ?
                            result.first[pos] : 0;
                        unsigned char c2 = pos < result.second.size() ?
                            result.second[pos] : 0;
                        if (c1 != c2) {
                            if (counts(c1, pos) < counts(c2, pos)) {
                                ++weights[c1];
                            } else {
                                ++weights[c2];
                            }
                            identical = false;
                            break;
                        }
                    }
                    // if all significant characters are the same, widen the search
                    if (identical) {
                        break;
                    }
                }
            } 
            return {0, weights, false};
        }();

    public:
        static constexpr size_t significant_chars = std::get<0>(find_hash);
        static constexpr Weights weights = std::get<1>(find_hash);
        static constexpr bool exists = std::get<2>(find_hash);

        template <typename T>
        static constexpr bool hashable =
            meta::convertible_to<T, const char*> ||
            meta::convertible_to<T, std::string_view>;

        /* An array holding the positions of the significant characters for the
        associative value array, in traversal order. */
        static constexpr std::array<size_t, significant_chars> positions =
            []<size_t... Is>(std::index_sequence<Is...>) {
                std::array<size_t, significant_chars> positions {
                    sorted_freq_indices[Is]...
                };
                std::sort(positions.begin(), positions.end());
                return positions;
            }(std::make_index_sequence<significant_chars>{});

        /* Hash a compile-time string according to the computed perfect hash algorithm. */
        template <meta::static_str Key>
        [[nodiscard]] static constexpr size_t hash(const Key& str) noexcept {
            constexpr size_t len = std::remove_cvref_t<Key>::size();
            size_t out = 0;
            for (size_t pos : positions) {
                out += weights[pos < len ? str[pos] : 0];
            }
            return out;
        }

        /* Hash a string literal according to the computed perfect hash algorithm. */
        template <size_t N>
        [[nodiscard]] static constexpr size_t hash(const char(&str)[N]) noexcept {
            constexpr size_t M = N - 1;
            size_t out = 0;
            for (size_t pos : positions) {
                out += weights[pos < M ? str[pos] : 0];
            }
            return out;
        }

        /* Hash a string literal according to the computed perfect hash algorithm. */
        template <size_t N>
        [[nodiscard]] static constexpr size_t hash(const char(&str)[N], size_t& len) noexcept {
            constexpr size_t M = N - 1;
            size_t out = 0;
            for (size_t pos : positions) {
                out += weights[pos < M ? str[pos] : 0];
            }
            len = M;
            return out;
        }

        /* Hash a character buffer according to the computed perfect hash algorithm. */
        template <meta::convertible_to<const char*> T>
            requires (!meta::static_str<T> && !meta::string_literal<T>)
        [[nodiscard]] static constexpr size_t hash(const T& str) noexcept {
            const char* start = str;
            if constexpr (positions.empty()) {
                return 0;
            } else {
                const char* ptr = start;
                size_t out = 0;
                size_t i = 0;
                size_t next_pos = positions[i];
                while (*ptr != '\0') {
                    if ((ptr - start) == next_pos) {
                        out += weights[*ptr];
                        if (++i >= positions.size()) {
                            return out;  // early break if no characters left to probe
                        }
                        next_pos = positions[i];
                    }
                    ++ptr;
                }
                while (i < positions.size()) {
                    out += weights[0];
                    ++i;
                }
                return out;
            }
        }

        /* Hash a character buffer according to the computed perfect hash algorithm and
        record its length as an out parameter. */
        template <meta::convertible_to<const char*> T>
            requires (!meta::static_str<T> && !meta::string_literal<T>)
        [[nodiscard]] static constexpr size_t hash(const T& str, size_t& len) noexcept {
            const char* start = str;
            const char* ptr = start;
            if constexpr (positions.empty()) {
                while (*ptr != '\0') { ++ptr; }
                len = ptr - start;
                return 0;
            } else {
                size_t out = 0;
                size_t i = 0;
                size_t next_pos = positions[i];
                while (*ptr != '\0') {
                    if ((ptr - start) == next_pos) {
                        out += weights[*ptr];
                        if (++i >= positions.size()) {
                            // continue probing until end of string to get length
                            next_pos = std::numeric_limits<size_t>::max();
                        } else {
                            next_pos = positions[i];
                        }
                    }
                    ++ptr;
                }
                while (i < positions.size()) {
                    out += weights[0];
                    ++i;
                }
                len = ptr - start;
                return out;
            }
        }

        /* Hash a string view according to the computed perfect hash algorithm. */
        template <meta::convertible_to<std::string_view> T>
            requires (
                !meta::static_str<T> &&
                !meta::string_literal<T> &&
                !meta::convertible_to<T, const char*>
            )
        [[nodiscard]] static constexpr size_t hash(const T& str) noexcept {
            std::string_view s = str;
            size_t out = 0;
            for (size_t pos : positions) {
                out += weights[pos < s.size() ? s[pos] : 0];
            }
            return out;
        }
    };

    /* A standardized iterator type for `bertrand::static_map` instances, independent
    of the stored names. */
    template <typename ValueType>
    struct static_map_iterator {
        using iterator_category = std::random_access_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = ValueType;
        using reference = value_type&;
        using pointer = value_type*;

    private:
        pointer m_data = nullptr;
        const size_t* m_indices = nullptr;
        difference_type m_idx = 0;
        size_t m_length = 0;

    public:
        static_map_iterator() = default;

        static_map_iterator(
            pointer data,
            const size_t* indices,
            difference_type index,
            size_t length
        ) :
            m_data(data),
            m_indices(indices),
            m_idx(index),
            m_length(length)
        {}

        [[nodiscard]] explicit operator bool() const noexcept {
            return m_idx >= 0 && m_idx < m_length;
        }

        [[nodiscard]] reference operator*() const {
            if (m_idx >= 0 && m_idx < m_length) {
                return m_data[m_indices[m_idx]];
            }
            throw IndexError(std::to_string(m_idx));
        }

        [[nodiscard]] pointer operator->() const {
            return &**this;
        }

        [[nodiscard]] reference operator[](difference_type n) const {
            difference_type index = m_idx + n;
            if (index >= 0 && index < m_length) {
                return m_data[m_indices[index]];
            }
            throw IndexError(std::to_string(index));
        }

        static_map_iterator& operator++() noexcept {
            ++m_idx;
            return *this;
        }

        [[nodiscard]] static_map_iterator operator++(int) {
            static_map_iterator copy = *this;
            ++m_idx;
            return copy;
        }

        static_map_iterator& operator+=(difference_type n) noexcept {
            m_idx += n;
            return *this;
        }

        [[nodiscard]] friend static_map_iterator operator+(
            const static_map_iterator& self,
            difference_type n
        ) noexcept {
            return static_map_iterator(
                self.m_data,
                self.m_indices,
                self.m_idx + n,
                self.m_length
            );
        }

        [[nodiscard]] friend static_map_iterator operator+(
            difference_type n,
            const static_map_iterator& self
        ) noexcept {
            return static_map_iterator(
                self.m_data,
                self.m_indices,
                self.m_idx + n,
                self.m_length
            );
        }

        static_map_iterator& operator--() noexcept {
            --m_idx;
            return *this;
        }

        [[nodiscard]] static_map_iterator operator--(int) noexcept {
            static_map_iterator copy = *this;
            --m_idx;
            return copy;
        }

        static_map_iterator& operator-=(difference_type n) noexcept {
            m_idx -= n;
            return *this;
        }

        [[nodiscard]] friend static_map_iterator operator-(
            const static_map_iterator& self,
            difference_type n
        ) noexcept {
            return static_map_iterator(
                self.m_data,
                self.m_indices,
                self.m_idx - n,
                self.m_length
            );
        }

        [[nodiscard]] friend static_map_iterator operator-(
            difference_type n,
            const static_map_iterator& self
        ) noexcept {
            return static_map_iterator(
                self.m_data,
                self.m_indices,
                self.m_idx - n,
                self.m_length
            );
        }

        [[nodiscard]] friend difference_type operator-(
            const static_map_iterator& lhs,
            const static_map_iterator& rhs
        ) noexcept {
            return lhs.m_idx - rhs.m_idx;
        }

        [[nodiscard]] friend bool operator<(
            const static_map_iterator& lhs,
            const static_map_iterator& rhs
        ) noexcept {
            return lhs.m_data == rhs.m_data && lhs.m_idx < rhs.m_idx;
        }

        [[nodiscard]] friend bool operator<=(
            const static_map_iterator& lhs,
            const static_map_iterator& rhs
        ) noexcept {
            return lhs.m_data == rhs.m_data && lhs.m_idx <= rhs.m_idx;
        }

        [[nodiscard]] friend bool operator==(
            const static_map_iterator& lhs,
            const static_map_iterator& rhs
        ) noexcept {
            return lhs.m_data == rhs.m_data && lhs.m_idx == rhs.m_idx;
        }

        [[nodiscard]] friend bool operator!=(
            const static_map_iterator& lhs,
            const static_map_iterator& rhs
        ) noexcept {
            return !(lhs == rhs);
        }

        [[nodiscard]] friend bool operator>=(
            const static_map_iterator& lhs,
            const static_map_iterator& rhs
        ) noexcept {
            return lhs.m_data == rhs.m_data && lhs.m_idx >= rhs.m_idx;
        }

        [[nodiscard]] friend bool operator>(
            const static_map_iterator& lhs,
            const static_map_iterator& rhs
        ) noexcept {
            return lhs.m_data == rhs.m_data && lhs.m_idx > rhs.m_idx;
        }
    };

    /* A standardized iterator type for immutable `bertrand::static_map` instances,
    indpendent of the stored names. */
    template <typename ValueType>
    struct const_static_map_iterator {
        using iterator_category = std::random_access_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = ValueType;
        using reference = value_type&;
        using pointer = value_type*;

    private:
        pointer m_data = nullptr;
        const size_t* m_indices = nullptr;
        difference_type m_idx = 0;
        size_t m_length = 0;

    public:
        const_static_map_iterator() = default;

        const_static_map_iterator(
            pointer data,
            const size_t* indices,
            difference_type index,
            size_t length
        ) :
            m_data(data),
            m_indices(indices),
            m_idx(index),
            m_length(length)
        {}

        [[nodiscard]] explicit operator bool() const noexcept {
            return m_idx >= 0 && m_idx < m_length;
        }

        [[nodiscard]] reference operator*() const {
            if (m_idx >= 0 && m_idx < m_length) {
                return m_data[m_indices[m_idx]];
            }
            throw IndexError(std::to_string(m_idx));
        }

        [[nodiscard]] pointer operator->() const {
            return &**this;
        }

        [[nodiscard]] reference operator[](difference_type n) const {
            difference_type index = m_idx + n;
            if (index >= 0 && index < m_length) {
                return m_data[m_indices[index]];
            }
            throw IndexError(std::to_string(index));
        }

        const_static_map_iterator& operator++() noexcept {
            ++m_idx;
            return *this;
        }

        [[nodiscard]] const_static_map_iterator operator++(int) {
            const_static_map_iterator copy = *this;
            ++m_idx;
            return copy;
        }

        const_static_map_iterator& operator+=(difference_type n) noexcept {
            m_idx += n;
            return *this;
        }

        [[nodiscard]] friend const_static_map_iterator operator+(
            const const_static_map_iterator& self,
            difference_type n
        ) noexcept {
            return const_static_map_iterator(
                self.m_data,
                self.m_indices,
                self.m_idx + n,
                self.m_length
            );
        }

        [[nodiscard]] friend const_static_map_iterator operator+(
            difference_type n,
            const const_static_map_iterator& self
        ) noexcept {
            return const_static_map_iterator(
                self.m_data,
                self.m_indices,
                self.m_idx + n,
                self.m_length
            );
        }

        const_static_map_iterator& operator--() noexcept {
            --m_idx;
            return *this;
        }

        [[nodiscard]] const_static_map_iterator operator--(int) noexcept {
            const_static_map_iterator copy = *this;
            --m_idx;
            return copy;
        }

        const_static_map_iterator& operator-=(difference_type n) noexcept {
            m_idx -= n;
            return *this;
        }

        [[nodiscard]] friend const_static_map_iterator operator-(
            const const_static_map_iterator& self,
            difference_type n
        ) noexcept {
            return const_static_map_iterator(
                self.m_data,
                self.m_indices,
                self.m_idx - n,
                self.m_length
            );
        }

        [[nodiscard]] friend const_static_map_iterator operator-(
            difference_type n,
            const const_static_map_iterator& self
        ) noexcept {
            return const_static_map_iterator(
                self.m_data,
                self.m_indices,
                self.m_idx - n,
                self.m_length
            );
        }

        [[nodiscard]] friend difference_type operator-(
            const const_static_map_iterator& lhs,
            const const_static_map_iterator& rhs
        ) noexcept {
            return lhs.m_idx - rhs.m_idx;
        }

        [[nodiscard]] friend bool operator<(
            const const_static_map_iterator& lhs,
            const const_static_map_iterator& rhs
        ) noexcept {
            return lhs.m_data == rhs.m_data && lhs.m_idx < rhs.m_idx;
        }

        [[nodiscard]] friend bool operator<=(
            const const_static_map_iterator& lhs,
            const const_static_map_iterator& rhs
        ) noexcept {
            return lhs.m_data == rhs.m_data && lhs.m_idx <= rhs.m_idx;
        }

        [[nodiscard]] friend bool operator==(
            const const_static_map_iterator& lhs,
            const const_static_map_iterator& rhs
        ) noexcept {
            return lhs.m_data == rhs.m_data && lhs.m_idx == rhs.m_idx;
        }

        [[nodiscard]] friend bool operator!=(
            const const_static_map_iterator& lhs,
            const const_static_map_iterator& rhs
        ) noexcept {
            return !(lhs == rhs);
        }

        [[nodiscard]] friend bool operator>=(
            const const_static_map_iterator& lhs,
            const const_static_map_iterator& rhs
        ) noexcept {
            return lhs.m_data == rhs.m_data && lhs.m_idx >= rhs.m_idx;
        }

        [[nodiscard]] friend bool operator>(
            const const_static_map_iterator& lhs,
            const const_static_map_iterator& rhs
        ) noexcept {
            return lhs.m_data == rhs.m_data && lhs.m_idx > rhs.m_idx;
        }
    };

}


namespace meta {

    template <bertrand::static_str... Keys>
    concept perfectly_hashable =
        strings_are_unique<Keys...> &&
        impl::minimal_perfect_hash<Keys...>::exists;

}


/* A compile-time perfect hash table with a finite set of static strings as keys.  The
data structure will compute a perfect hash function for the given strings at compile
time, and will store the values in a fixed-size array that can be baked into the final
binary.

Searching the map is extremely fast even in the worst case, consisting only of a
perfect FNV-1a hash, a single array lookup, and a string comparison to validate.  No
collision resolution is necessary, due to the perfect hash function.  If the search
string is also known at compile time, then even these can be optimized out, skipping
straight to the final value with no intermediate computation. */
template <typename Value, static_str... Keys>
    requires (meta::perfectly_hashable<Keys...>)
struct static_map : impl::minimal_perfect_hash<Keys...> {
    using key_type = const std::string_view;
    using mapped_type = Value;
    using value_type = std::pair<key_type, mapped_type>;
    using reference = value_type&;
    using const_reference = const value_type&;
    using pointer = value_type*;
    using const_pointer = const value_type*;
    using size_type = size_t;
    using difference_type = std::ptrdiff_t;
    using hasher = impl::minimal_perfect_hash<Keys...>;
    using key_equal = std::equal_to<key_type>;
    using iterator = impl::static_map_iterator<value_type>;
    using const_iterator = impl::const_static_map_iterator<value_type>;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    template <typename T>
    static constexpr bool hashable = hasher::template hashable<T>;

    [[nodiscard]] static constexpr size_t size() noexcept { return hasher::table_size; }
    [[nodiscard]] static constexpr bool empty() noexcept { return !size(); }
    [[nodiscard]] constexpr pointer data() noexcept { return m_table.data(); }
    [[nodiscard]] constexpr const pointer data() const noexcept { return m_table.data(); }

private:
    static constexpr std::array<size_t, size()> m_indices {
        (hasher::hash(Keys) % size())...
    };

    using Table = std::array<value_type, size()>;
    Table m_table;

    template <size_t I, size_t...>
    static constexpr size_t get_index = 0;
    template <size_t I, size_t J, size_t... Js>
    static constexpr size_t get_index<I, J, Js...> =
        (I == J) ? 0 : get_index<I, Js...> + 1;

    template <size_t I, size_t... occupied>
    static constexpr value_type populate(auto&&... values) {
        constexpr size_t idx = get_index<I, occupied...>;
        return {
            std::string_view(meta::unpack_string<idx, Keys...>),
            meta::unpack_arg<idx>(std::forward<decltype(values)>(values)...)
        };
    }

public:
    template <typename Self = static_map>
        requires (meta::default_constructible<typename Self::mapped_type>)
    constexpr static_map() {}

    template <typename... Values>
        requires (
            sizeof...(Values) == size() &&
            (meta::convertible_to<Values, Value> && ...)
        )
    constexpr static_map(Values&&... values) :
        m_table([]<size_t... Is>(std::index_sequence<Is...>, auto&&... values) {
            return Table{populate<Is, (hasher::hash(Keys) % size())...>(
                std::forward<decltype(values)>(values)...
            )...};
        }(std::make_index_sequence<size()>{}, std::forward<Values>(values)...))
    {}

    constexpr static_map(const static_map& other) = default;
    constexpr static_map(static_map&& other) = default;

    constexpr static_map& operator=(const static_map& other) {
        if (&other != this) {
            for (size_t i = 0; i < size(); ++i) {
                m_table[i].~pair();
                new (&m_table[i]) value_type(other.m_table[i]);
            }
        }
        return *this;
    }

    constexpr static_map& operator=(static_map&& other) {
        if (&other != this) {
            for (size_t i = 0; i < size(); ++i) {
                m_table[i].~pair();
                new (&m_table[i]) value_type(std::move(other.m_table[i]));
            }
        }
        return *this;
    }

    /* Check whether the given index is in bounds for the table. */
    template <size_t I>
    [[nodiscard]] static constexpr bool contains() noexcept {
        return I < size();
    }

    /* Check whether the map contains an arbitrary key at compile time. */
    template <static_str Key>
    [[nodiscard]] static constexpr bool contains() noexcept {
        return ((Key == Keys) || ...);
    }

    /* Check whether the map contains an arbitrary key. */
    template <size_t N>
    [[nodiscard]] constexpr bool contains(const char(&key)[N]) const noexcept {
        constexpr size_t M = N - 1;
        if constexpr (M < hasher::min_length || M > hasher::max_length) {
            return false;
        } else {
            const_reference result = m_table[hasher::hash(key) % size()];
            if (M != result.first.size()) {
                return false;
            }
            for (size_t i = 0; i < M; ++i) {
                if (key[i] != result.first[i]) {
                    return false;
                }
            }
            return true;
        }
    }

    /* Check whether the map contains an arbitrary key. */
    template <meta::static_str Key>
    [[nodiscard]] constexpr bool contains(const Key& key) const noexcept {
        constexpr size_t len = meta::unqualify<Key>::size();
        if constexpr (len < hasher::min_length || len > hasher::max_length) {
            return false;
        } else {
            return std::string_view(key) == m_table[hasher::hash(key) % size()].first;
        }
    }

    /* Check whether the map contains an arbitrary key. */
    template <meta::convertible_to<const char*> T>
        requires (!meta::string_literal<T> && !meta::static_str<T>)
    [[nodiscard]] constexpr bool contains(const T& key) const noexcept {
        const char* str = key;
        size_t len;
        const_reference result = m_table[hasher::hash(str, len) % size()];
        if (len != result.first.size()) {
            return false;
        }
        for (size_t i = 0; i < len; ++i) {
            if (str[i] != result.first[i]) {
                return false;
            }
        }
        return true;
    }

    /* Check whether the map contains an arbitrary key. */
    template <meta::convertible_to<std::string_view> T>
        requires (
            !meta::string_literal<T> &&
            !meta::static_str<T> &&
            !meta::convertible_to<T, const char*>
        )
    [[nodiscard]] constexpr bool contains(const T& key) const noexcept {
        std::string_view str = key;
        if (str.size() < hasher::min_length || str.size() > hasher::max_length) {
            return false;
        }
        return str == m_table[hasher::hash(str) % size()].first;
    }

    /* Check whether the given index is in bounds for the table. */
    template <meta::convertible_to<size_t> T>
        requires (
            !meta::string_literal<T> &&
            !meta::static_str<T> &&
            !meta::convertible_to<T, const char*> &&
            !meta::convertible_to<T, std::string_view>
        )
    [[nodiscard]] constexpr bool contains(const T& key) const noexcept {
        return size_t(key) < size();
    }

    /* Get the value associate with the key at index I at compile time, asserting that
    the index is within range. */
    template <size_t I> requires (I < size())
    [[nodiscard]] constexpr const_reference get() const noexcept {
        return m_table[m_indices[I]];
    }

    /* Get the value associate with the key at index I at compile time, asserting that
    the index is within range. */
    template <size_t I> requires (I < size())
    [[nodiscard]] constexpr reference get() noexcept {
        return m_table[m_indices[I]];
    }

    /* Get the value associated with a key at compile time, asserting that it is
    present in the map. */
    template <static_str Key> requires (contains<Key>())
    [[nodiscard]] constexpr const mapped_type& get() const noexcept {
        constexpr size_t idx = hasher::hash(Key) % size();
        return m_table[idx].second;
    }

    /* Get the value associated with a key at compile time, asserting that it is
    present in the map. */
    template <static_str Key> requires (contains<Key>())
    [[nodiscard]] constexpr mapped_type& get() noexcept {
        constexpr size_t idx = hasher::hash(Key) % size();
        return m_table[idx].second;
    }

    /* Look up a key, returning a pointer to the corresponding value or nullptr if it
    is not present. */
    template <size_t N>
    [[nodiscard]] constexpr auto operator[](const char(&key)[N]) const noexcept
        -> meta::as_pointer<meta::as_const<mapped_type>>
    {
        constexpr size_t M = N - 1;
        if constexpr (M < hasher::min_length || M > hasher::max_length) {
            return nullptr;
        } else {
            const_reference result = m_table[hasher::hash(key) % size()];
            if (M != result.first.size()) {
                return nullptr;
            }
            for (size_t i = 0; i < M; ++i) {
                if (key[i] != result.first[i]) {
                    return nullptr;
                }
            }
            return &result.second;
        }
    }

    /* Look up a key, returning a pointer to the corresponding value or nullptr if it
    is not present. */
    template <size_t N>
    [[nodiscard]] constexpr auto operator[](const char(&key)[N]) noexcept
        -> meta::as_pointer<mapped_type>
    {
        constexpr size_t M = N - 1;
        if constexpr (M < hasher::min_length || M > hasher::max_length) {
            return nullptr;
        } else {
            reference result = m_table[hasher::hash(key) % size()];
            if (M != result.first.size()) {
                return nullptr;
            }
            for (size_t i = 0; i < M; ++i) {
                if (key[i] != result.first[i]) {
                    return nullptr;
                }
            }
            return &result.second;
        }
    }

    /* Look up a key, returning a pointer to the corresponding value or nullptr if it
    is not present. */
    template <meta::static_str Key>
    [[nodiscard]] constexpr auto operator[](const Key& key) const noexcept
        -> meta::as_pointer<meta::as_const<mapped_type>>
    {
        constexpr size_t len = meta::unqualify<Key>::size();
        if constexpr (len < hasher::min_length || len > hasher::max_length) {
            return nullptr;
        } else {
            const_reference result = m_table[hasher::hash(key) % size()];
            return std::string_view(key) == result.first ? &result.second : nullptr;
        }
    }

    /* Look up a key, returning a pointer to the corresponding value or nullptr if it
    is not present. */
    template <meta::static_str Key>
    [[nodiscard]] constexpr auto operator[](const Key& key) noexcept
        -> meta::as_pointer<mapped_type>
    {
        constexpr size_t len = meta::unqualify<Key>::size();
        if constexpr (len < hasher::min_length || len > hasher::max_length) {
            return nullptr;
        } else {
            reference result = m_table[hasher::hash(key) % size()];
            return std::string_view(key) == result.first ? &result.second : nullptr;
        }
    }

    /* Look up a key, returning a pointer to the corresponding value or nullptr if it
    is not present. */
    template <meta::convertible_to<const char*> T>
        requires (!meta::string_literal<T> && !meta::static_str<T>)
    [[nodiscard]] constexpr auto operator[](const T& key) const noexcept
        -> meta::as_pointer<meta::as_const<mapped_type>>
    {
        const char* str = key;
        size_t len;
        const_reference result = m_table[hasher::hash(str, len) % size()];
        if (len != result.first.size()) {
            return nullptr;
        }
        for (size_t i = 0; i < len; ++i) {
            if (str[i] != result.first[i]) {
                return nullptr;
            }
        }
        return &result.second;
    }

    /* Look up a key, returning a pointer to the corresponding value or nullptr if it
    is not present. */
    template <meta::convertible_to<const char*> T>
        requires (!meta::string_literal<T> && !meta::static_str<T>)
    [[nodiscard]] constexpr auto operator[](const T& key) noexcept
        -> meta::as_pointer<mapped_type>
    {
        const char* str = key;
        size_t len;
        reference result = m_table[hasher::hash(str, len) % size()];
        if (len != result.first.size()) {
            return nullptr;
        }
        for (size_t i = 0; i < len; ++i) {
            if (str[i] != result.first[i]) {
                return nullptr;
            }
        }
        return &result.second;
    }

    /* Look up a key, returning a pointer to the corresponding value or nullptr if it
    is not present. */
    template <meta::convertible_to<std::string_view> T>
        requires (
            !meta::string_literal<T> &&
            !meta::static_str<T> &&
            !meta::convertible_to<T, const char*>
        )
    [[nodiscard]] constexpr auto operator[](const T& key) const noexcept
        -> meta::as_pointer<meta::as_const<mapped_type>>
    {
        std::string_view str = key;
        if (str.size() < hasher::min_length || str.size() > hasher::max_length) {
            return nullptr;
        }
        const_reference result = m_table[hasher::hash(str) % size()];
        return str == result.first ? &result.second : nullptr;
    }

    /* Look up a key, returning a pointer to the corresponding value or nullptr if it
    is not present. */
    template <meta::convertible_to<std::string_view> T>
        requires (
            !meta::string_literal<T> &&
            !meta::static_str<T> &&
            !meta::convertible_to<T, const char*>
        )
    [[nodiscard]] constexpr auto operator[](const T& key) noexcept
        -> meta::as_pointer<mapped_type>
    {
        std::string_view str = key;
        if (str.size() < hasher::min_length || str.size() > hasher::max_length) {
            return nullptr;
        }
        reference result = m_table[hasher::hash(str) % size()];
        return str == result.first ? &result.second : nullptr;
    }

    /* Look up an index in the table, returning the value that corresponds to the
    key at that index or nullptr if the index is out of bounds. */
    template <meta::convertible_to<size_t> T>
        requires (
            !meta::string_literal<T> &&
            !meta::static_str<T> &&
            !meta::convertible_to<T, const char*> &&
            !meta::convertible_to<T, std::string_view>
        )
    [[nodiscard]] constexpr const_pointer operator[](const T& key) const noexcept {
        size_t idx = key;
        return idx < size() ? &m_table[m_indices[idx]] : nullptr;
    }

    /* Look up an index in the table, returning the value that corresponds to the
    key at that index or nullptr if the index is out of bounds. */
    template <meta::convertible_to<size_t> T>
        requires (
            !meta::string_literal<T> &&
            !meta::static_str<T> &&
            !meta::convertible_to<T, const char*> &&
            !meta::convertible_to<T, std::string_view>
        )
    [[nodiscard]] constexpr pointer operator[](const T& key) noexcept {
        size_t idx = key;
        return idx < size() ? &m_table[m_indices[idx]] : nullptr;
    }

    [[nodiscard]] constexpr iterator begin() noexcept {
        return {m_table.data(), m_indices.data(), 0, size()};
    }

    [[nodiscard]] constexpr const_iterator begin() const noexcept {
        return {m_table.data(), m_indices.data(), 0, size()};
    }

    [[nodiscard]] constexpr const_iterator cbegin() const noexcept {
        return {m_table.data(), m_indices.data(), 0, size()};
    }

    [[nodiscard]] constexpr iterator end() noexcept {
        return {m_table.data(), m_indices.data(), size(), size()};
    }

    [[nodiscard]] constexpr const_iterator end() const noexcept {
        return {m_table.data(), m_indices.data(), size(), size()};
    }

    [[nodiscard]] constexpr const_iterator cend() const noexcept {
        return {m_table.data(), m_indices.data(), size(), size()};
    }

    [[nodiscard]] constexpr reverse_iterator rbegin() noexcept {
        return std::make_reverse_iterator(end());
    }

    [[nodiscard]] constexpr const_reverse_iterator rbegin() const noexcept {
        return std::make_reverse_iterator(end());
    }

    [[nodiscard]] constexpr const_reverse_iterator crbegin() const noexcept {
        return std::make_reverse_iterator(cend());
    }

    [[nodiscard]] constexpr reverse_iterator rend() noexcept {
        return std::make_reverse_iterator(begin());
    }

    [[nodiscard]] constexpr const_reverse_iterator rend() const noexcept {
        return std::make_reverse_iterator(begin());
    }

    [[nodiscard]] constexpr const_reverse_iterator crend() const noexcept {
        return std::make_reverse_iterator(cbegin());
    }
};


/* Specialization of `static_map` for void values, which correspond to an instantiation
of `static_set`. */
template <meta::is_void Value, static_str... Keys>
    requires (meta::perfectly_hashable<Keys...>)
struct static_map<Value, Keys...> : impl::minimal_perfect_hash<Keys...> {
    using key_type = const std::string_view;
    using mapped_type = void;
    using value_type = key_type;
    using reference = key_type&;
    using const_reference = reference;
    using pointer = key_type*;
    using const_pointer = pointer;
    using size_type = size_t;
    using difference_type = std::ptrdiff_t;
    using hasher = impl::minimal_perfect_hash<Keys...>;
    using key_equal = std::equal_to<key_type>;
    using iterator = impl::static_map_iterator<value_type>;
    using const_iterator = impl::const_static_map_iterator<value_type>;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    template <typename T>
    static constexpr bool hashable = hasher::template hashable<T>;

    [[nodiscard]] constexpr pointer data() const noexcept { return m_table.data(); }
    [[nodiscard]] static constexpr size_t size() noexcept { return hasher::table_size; }
    [[nodiscard]] static constexpr bool empty() noexcept { return !size(); }

private:
    static constexpr std::array<size_t, size()> m_indices {
        (hasher::hash(Keys) % size())...
    };

    using Table = std::array<value_type, size()>;
    Table m_table;

    template <size_t I, size_t...>
    static constexpr size_t get_index = 0;
    template <size_t I, size_t J, size_t... Js>
    static constexpr size_t get_index<I, J, Js...> =
        (I == J) ? 0 : get_index<I, Js...> + 1;

    template <size_t I, size_t... occupied>
    static constexpr std::string_view populate() {
        constexpr size_t idx = get_index<I, occupied...>;
        return std::string_view(meta::unpack_string<idx, Keys...>);
    }

public:
    constexpr static_map() :
        m_table([]<size_t... Is>(std::index_sequence<Is...>) {
            return Table{populate<Is, (hasher::hash(Keys) % size())...>()...};
        }(std::make_index_sequence<size()>{}))
    {}

    constexpr static_map(const static_map& other) noexcept = default;
    constexpr static_map(static_map&& other) noexcept = default;
    constexpr static_map& operator=(const static_map& other) noexcept { return *this; }
    constexpr static_map& operator=(static_map&& other) noexcept { return *this; }

    /* Check whether the given index is in bounds for the table. */
    template <size_t I>
    [[nodiscard]] static constexpr bool contains() noexcept {
        return I < size();
    }

    /* Check whether the map contains an arbitrary key at compile time. */
    template <static_str Key>
    [[nodiscard]] static constexpr bool contains() noexcept {
        return ((Key == Keys) || ...);
    }

    /* Check whether the map contains an arbitrary key. */
    template <size_t N>
    [[nodiscard]] constexpr bool contains(const char(&key)[N]) const noexcept {
        constexpr size_t M = N - 1;
        if constexpr (M < hasher::min_length || M > hasher::max_length) {
            return false;
        } else {
            const_reference result = m_table[hasher::hash(key) % size()];
            if (M != result.size()) {
                return false;
            }
            for (size_t i = 0; i < M; ++i) {
                if (key[i] != result[i]) {
                    return false;
                }
            }
            return true;
        }
    }

    /* Check whether the map contains an arbitrary key. */
    template <meta::static_str Key>
    [[nodiscard]] constexpr bool contains(const Key& key) const noexcept {
        constexpr size_t len = meta::unqualify<Key>::size();
        if constexpr (len < hasher::min_length || len > hasher::max_length) {
            return false;
        } else {
            return std::string_view(key) == m_table[hasher::hash(key) % size()];
        }
    }

    /* Check whether the map contains an arbitrary key. */
    template <meta::convertible_to<const char*> T>
        requires (!meta::string_literal<T> && !meta::static_str<T>)
    [[nodiscard]] constexpr bool contains(const T& key) const noexcept {
        const char* str = key;
        size_t len;
        const_reference result = m_table[hasher::hash(str, len) % size()];
        if (len != result.size()) {
            return false;
        }
        for (size_t i = 0; i < len; ++i) {
            if (str[i] != result[i]) {
                return false;
            }
        }
        return true;
    }

    /* Check whether the map contains an arbitrary key. */
    template <meta::convertible_to<std::string_view> T>
        requires (
            !meta::string_literal<T> &&
            !meta::static_str<T> &&
            !meta::convertible_to<T, const char*>
        )
    [[nodiscard]] constexpr bool contains(const T& key) const noexcept {
        std::string_view str = key;
        if (str.size() < hasher::min_length || str.size() > hasher::max_length) {
            return false;
        }
        return str == m_table[hasher::hash(str) % size()];
    }

    /* Check whether the given index is in bounds for the table. */
    template <meta::convertible_to<size_t> T>
        requires (
            !meta::string_literal<T> &&
            !meta::static_str<T> &&
            !meta::convertible_to<T, const char*> &&
            !meta::convertible_to<T, std::string_view>
        )
    [[nodiscard]] constexpr bool contains(const T& key) const noexcept {
        return size_t(key) < size();
    }

    /* Get the value associated with a key at compile time, asserting that it is
    present in the map. */
    template <size_t I> requires (I < sizeof...(Keys))
    [[nodiscard]] constexpr reference get() const noexcept {
        return m_table[m_indices[I]];
    }

    /* Get the value associated with a key at compile time, asserting that it is
    present in the map. */
    template <static_str Key> requires (contains<Key>())
    [[nodiscard]] constexpr reference get() const noexcept {
        constexpr size_t idx = hasher::hash(Key) % size();
        return m_table[idx];
    }

    /* Look up a key, returning a pointer to the corresponding key or nullptr if it is
    not present. */
    template <size_t N>
    [[nodiscard]] constexpr pointer operator[](const char(&key)[N]) const noexcept {
        constexpr size_t M = N - 1;
        if constexpr (M < hasher::min_length || M > hasher::max_length) {
            return nullptr;
        } else {
            const_reference result = m_table[hasher::hash(key) % size()];
            if (M != result.size()) {
                return nullptr;
            }
            for (size_t i = 0; i < M; ++i) {
                if (key[i] != result[i]) {
                    return nullptr;
                }
            }
            return &result;
        }
    }

    /* Look up a key, returning a pointer to the corresponding key or nullptr if it is
    not present. */
    template <meta::static_str Key>
    [[nodiscard]] constexpr pointer operator[](const Key& key) const noexcept {
        constexpr size_t len = meta::unqualify<Key>::size();
        if constexpr (len < hasher::min_length || len > hasher::max_length) {
            return nullptr;
        } else {
            const_reference result = m_table[hasher::hash(key) % size()];
            return std::string_view(key) == result ? &result : nullptr;
        }
    }

    /* Look up a key, returning a pointer to the corresponding key or nullptr if it is
    not present. */
    template <meta::convertible_to<const char*> T>
        requires (!meta::string_literal<T> && !meta::static_str<T>)
    [[nodiscard]] constexpr pointer operator[](const T& key) const noexcept {
        const char* str = key;
        size_t len;
        reference result = m_table[hasher::hash(str, len) % size()];
        if (len != result.size()) {
            return nullptr;
        }
        for (size_t i = 0; i < len; ++i) {
            if (str[i] != result[i]) {
                return nullptr;
            }
        }
        return &result;
    }

    /* Look up a key, returning a pointer to the corresponding key or nullptr if it is
    not present. */
    template <meta::convertible_to<std::string_view> T>
        requires (
            !meta::string_literal<T> &&
            !meta::static_str<T> &&
            !meta::convertible_to<T, const char*>
        )
    [[nodiscard]] constexpr pointer operator[](const T& key) const noexcept {
        std::string_view str = key;
        if (str.size() < hasher::min_length || str.size() > hasher::max_length) {
            return nullptr;
        }
        reference result = m_table[hasher::hash(str) % size()];
        return str == result ? &result : nullptr;
    }

    /* Look up an index in the table, returning the value that corresponds to the
    key at that index or nullptr if the index is out of bounds. */
    template <meta::convertible_to<size_t> T>
        requires (
            !meta::string_literal<T> &&
            !meta::static_str<T> &&
            !meta::convertible_to<T, const char*> &&
            !meta::convertible_to<T, std::string_view>
        )
    [[nodiscard]] constexpr pointer operator[](const T& key) const noexcept {
        size_t idx = key;
        return idx < size() ? &m_table[m_indices[idx]] : nullptr;
    }

    [[nodiscard]] constexpr iterator begin() noexcept {
        return {m_table.data(), m_indices.data(), 0, size()};
    }

    [[nodiscard]] constexpr const_iterator begin() const noexcept {
        return {m_table.data(), m_indices.data(), 0, size()};
    }

    [[nodiscard]] constexpr const_iterator cbegin() const noexcept {
        return {m_table.data(), m_indices.data(), 0, size()};
    }

    [[nodiscard]] constexpr iterator end() noexcept {
        return {m_table.data(), m_indices.data(), size(), size()};
    }

    [[nodiscard]] constexpr const_iterator end() const noexcept {
        return {m_table.data(), m_indices.data(), size(), size()};
    }

    [[nodiscard]] constexpr const_iterator cend() const noexcept {
        return {m_table.data(), m_indices.data(), size(), size()};
    }

    [[nodiscard]] constexpr reverse_iterator rbegin() noexcept {
        return std::make_reverse_iterator(end());
    }

    [[nodiscard]] constexpr const_reverse_iterator rbegin() const noexcept {
        return std::make_reverse_iterator(end());
    }

    [[nodiscard]] constexpr const_reverse_iterator crbegin() const noexcept {
        return std::make_reverse_iterator(cend());
    }

    [[nodiscard]] constexpr reverse_iterator rend() noexcept {
        return std::make_reverse_iterator(begin());
    }

    [[nodiscard]] constexpr const_reverse_iterator rend() const noexcept {
        return std::make_reverse_iterator(begin());
    }

    [[nodiscard]] constexpr const_reverse_iterator crend() const noexcept {
        return std::make_reverse_iterator(cbegin());
    }
};


/* A specialization of static_map that does not hold any values.  Such a data structure
is equivalent to a perfectly-hashed set of compile-time strings, which can be
efficiently searched at runtime.  Rather than dereferencing to a value, the buckets
and iterators will dereference to `string_view`s of the template key buffers. */
template <static_str... Keys>
using static_set = static_map<void, Keys...>;


}


namespace std {

    /* Specialize `std::tuple_size` to allow for structured bindings. */
    template <typename Value, bertrand::static_str... Keys>
    struct tuple_size<bertrand::static_map<Value, Keys...>> :
        std::integral_constant<size_t, bertrand::static_map<Value, Keys...>::size()>
    {};

    /* Specialize `std::tuple_element` to allow for structured bindings. */
    template <size_t I, typename Value, bertrand::static_str... Keys>
        requires (I < sizeof...(Keys))
    struct tuple_element<I, bertrand::static_map<Value, Keys...>> {
        using type = bertrand::static_map<Value, Keys...>::value_type;
    };

    /* `std::get<"name">(map)` is a type-safe accessor for `bertrand::static_map`. */
    template <size_t I, typename Value, bertrand::static_str... Keys>
        requires (bertrand::static_map<Value, Keys...>::template contains<I>())
    constexpr const auto& get(const bertrand::static_map<Value, Keys...>& map) noexcept {
        return map.template get<I>();
    }

    /* `std::get<"name">(map)` is a type-safe accessor for `bertrand::static_map`. */
    template <size_t I, typename Value, bertrand::static_str... Keys>
        requires (bertrand::static_map<Value, Keys...>::template contains<I>())
    constexpr auto& get(bertrand::static_map<Value, Keys...>& map) noexcept {
        return map.template get<I>();
    }

    /* `std::get<"name">(map)` is a type-safe accessor for `bertrand::static_map`. */
    template <bertrand::static_str Key, typename Value, bertrand::static_str... Keys>
        requires (bertrand::static_map<Value, Keys...>::template contains<Key>())
    constexpr const auto& get(const bertrand::static_map<Value, Keys...>& map) noexcept {
        return map.template get<Key>();
    }

    /* `std::get<"name">(map)` is a type-safe accessor for `bertrand::static_map`. */
    template <bertrand::static_str Key, typename Value, bertrand::static_str... Keys>
        requires (bertrand::static_map<Value, Keys...>::template contains<Key>())
    constexpr auto& get(bertrand::static_map<Value, Keys...>& map) noexcept {
        return map.template get<Key>();
    }

}


#endif