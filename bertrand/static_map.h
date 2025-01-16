#ifndef BERTRAND_STATIC_MAP_H
#define BERTRAND_STATIC_MAP_H


#include "bertrand/static_str.h"


namespace bertrand {


namespace impl {

    template <static_str...>
    constexpr bool _strings_are_unique = true;
    template <static_str First, static_str... Rest>
    constexpr bool _strings_are_unique<First, Rest...> =
        ((First != Rest) && ...) && _strings_are_unique<Rest...>;
    template <static_str... Strings>
    concept strings_are_unique = _strings_are_unique<Strings...>;

    template <size_t I, static_str... Strings>
    struct unpack_string;
    template <static_str First, static_str... Rest>
    struct unpack_string<0, First, Rest...> {
        static constexpr static_str value = First;
    };
    template <size_t I, static_str First, static_str... Rest>
    struct unpack_string<I, First, Rest...> {
        static constexpr static_str value = unpack_string<I - 1, Rest...>::value;
    };

    /* A helper struct that computes a perfect FNV-1a hash function over the given
    strings at compile time. */
    template <static_str... Keys>
    struct perfect_hash {
    private:
        using minmax_type = std::pair<size_t, size_t>;

        template <size_t>
        struct _minmax {
            static constexpr minmax_type value = std::minmax({Keys.size()...});
        };
        template <>
        struct _minmax<0> {
            static constexpr minmax_type value = {0, 0};
        };
        static constexpr minmax_type minmax = _minmax<sizeof...(Keys)>::value;

    public:
        static constexpr size_t table_size = next_prime(
            sizeof...(Keys) + (sizeof...(Keys) >> 1)
        );
        static constexpr size_t min_length = minmax.first;
        static constexpr size_t max_length = minmax.second;

        template <size_t I> requires (I < sizeof...(Keys))
        static constexpr static_str at = unpack_string<I, Keys...>::value;

    private:
        /* Check to see if the candidate seed and prime produce any collisions for the
        target keyword arguments. */
        template <static_str...>
        struct collisions {
            static constexpr bool operator()(size_t, size_t) {
                return false;
            }
        };
        template <static_str First, static_str... Rest>
        struct collisions<First, Rest...> {
            template <static_str...>
            struct scan {
                static constexpr bool operator()(size_t, size_t, size_t) {
                    return false;
                }
            };
            template <static_str F, static_str... Rs>
            struct scan<F, Rs...> {
                static constexpr bool operator()(size_t idx, size_t seed, size_t prime) {
                    size_t hash = fnv1a(F, seed, prime);
                    return ((hash % table_size) == idx) || scan<Rs...>{}(idx, seed, prime);
                }
            };

            static constexpr bool operator()(size_t seed, size_t prime) {
                size_t hash = fnv1a(First, seed, prime);
                return scan<Rest...>{}(
                    hash % table_size,
                    seed,
                    prime
                ) || collisions<Rest...>{}(seed, prime);
            }
        };

        /* Search for an FNV-1a seed and prime that perfectly hashes the argument names
        with respect to the keyword table size. */
        static constexpr auto hash_components = [] -> std::tuple<size_t, size_t, bool> {
            constexpr size_t recursion_limit = 100;
            size_t i = 0;
            size_t j = 0;
            size_t seed = fnv1a_seed;
            size_t prime = fnv1a_prime;
            while (collisions<Keys...>{}(seed, prime)) {
                seed = prime * seed + 31;
                if (++i >= recursion_limit) {
                    if (++j >= recursion_limit) {
                        return {0, 0, false};
                    }
                    seed = fnv1a_seed;
                    prime = next_prime(prime);
                }
            }
            return {seed, prime, true};
        }();

    public:
        /* A template constraint that indicates whether the recursive algorithm could
        find a perfect hash algorithm for the given keys. */
        static constexpr bool exists = std::get<2>(hash_components);

        /* A seed for an FNV-1a hash algorithm that was found to perfectly hash the
        keyword argument names from the enclosing parameter list. */
        static constexpr size_t seed = std::get<0>(hash_components);

        /* A prime for an FNV-1a hash algorithm that was found to perfectly hash the
        keyword argument names from the enclosing parameter list. */
        static constexpr size_t prime = std::get<1>(hash_components);

        template <typename T>
        static constexpr bool hashable =
            std::is_convertible_v<T, const char*> ||
            std::is_convertible_v<T, std::string_view>;

        /* Hash an arbitrary string according to the precomputed FNV-1a algorithm
        that was found to perfectly hash the enclosing keyword arguments. */
        static constexpr size_t hash(const char* str) noexcept {
            return fnv1a(str, seed, prime);
        }
        static constexpr size_t hash(std::string_view str) noexcept {
            return fnv1a(str.data(), seed, prime);
        }
    };

    /* A helper struct that computes a gperf-style minimal perfect hash function over
    the given strings at compile time.  Only the N most significant characters are
    considered, where N is minimized using an associative array containing relative
    weights for each character. */
    template <static_str... Keys>
    struct minimal_perfect_hash {
    private:
        using minmax_type = std::pair<size_t, size_t>;

        template <size_t>
        struct _minmax {
            static constexpr minmax_type value = std::minmax({Keys.size()...});
        };
        template <>
        struct _minmax<0> {
            static constexpr minmax_type value = {0, 0};
        };
        static constexpr minmax_type minmax = _minmax<sizeof...(Keys)>::value;

    public:
        static constexpr size_t table_size = sizeof...(Keys);
        static constexpr size_t min_length = minmax.first;
        static constexpr size_t max_length = minmax.second;

        template <size_t I> requires (I < sizeof...(Keys))
        static constexpr static_str at = unpack_string<I, Keys...>::value;

    private:
        using Weights = std::array<unsigned char, 256>;

        template <static_str...>
        struct _counts {
            static constexpr size_t operator()(unsigned char, size_t) {
                return 0;
            }
        };
        template <static_str First, static_str... Rest>
        struct _counts<First, Rest...> {
            static constexpr size_t operator()(unsigned char c, size_t pos) {
                return
                    _counts<Rest...>{}(c, pos) +
                    (pos < First.size() && First[pos] == c);
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
            std::is_convertible_v<T, const char*> ||
            std::is_convertible_v<T, std::string_view>;

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
        template <is_static_str Key>
        static constexpr size_t hash(const Key& str) noexcept {
            size_t out = 0;
            for (size_t pos : positions) {
                unsigned char c = pos < str.size() ? str[pos] : 0;
                out += weights[c];
            }
            return out;
        }

        /* Hash a string literal according to the computed perfect hash algorithm. */
        template <size_t N>
        static constexpr size_t hash(const char(&str)[N]) noexcept {
            constexpr size_t M = N - 1;
            size_t out = 0;
            for (size_t pos : positions) {
                unsigned char c = pos < M ? str[pos] : 0;
                out += weights[c];
            }
            return out;
        }

        /* Hash a string literal according to the computed perfect hash algorithm. */
        template <size_t N>
        static constexpr size_t hash(const char(&str)[N], size_t& len) noexcept {
            constexpr size_t M = N - 1;
            size_t out = 0;
            for (size_t pos : positions) {
                unsigned char c = pos < M ? str[pos] : 0;
                out += weights[c];
            }
            len = M;
            return out;
        }

        /* Hash a character buffer according to the computed perfect hash algorithm. */
        template <std::convertible_to<const char*> T>
            requires (!is_static_str<T> && !string_literal<T>)
        static constexpr size_t hash(const T& str) noexcept {
            const char* start = str;
            if constexpr (positions.empty()) {
                return 0;
            }
            const char* ptr = start;
            size_t out = 0;
            size_t i = 0;
            size_t next_pos = positions[i];
            while (*ptr != '\0') {
                if ((ptr - start) == next_pos) {
                    out += weights[*ptr];
                    if (++i >= positions.size()) {
                        // early break if no characters left to probe
                        return out;
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

        /* Hash a character buffer according to the computed perfect hash algorithm and
        record its length as an out parameter. */
        template <std::convertible_to<const char*> T>
            requires (!is_static_str<T> && !string_literal<T>)
        static constexpr size_t hash(const T& str, size_t& len) noexcept {
            const char* start = str;
            const char* ptr = start;
            if constexpr (positions.empty()) {
                while (*ptr != '\0') { ++ptr; }
                len = ptr - start;
                return 0;
            }
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

        /* Hash a string view according to the computed perfect hash algorithm. */
        template <std::convertible_to<std::string_view> T>
            requires (
                !is_static_str<T> &&
                !string_literal<T> &&
                !std::convertible_to<T, const char*>
            )
        static constexpr size_t hash(const T& str) noexcept {
            std::string_view s = str;
            size_t out = 0;
            for (size_t pos : positions) {
                unsigned char c = pos < s.size() ? s[pos] : 0;
                out += weights[c];
            }
            return out;
        }
    };

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
    requires (
        impl::strings_are_unique<Keys...> &&
        impl::minimal_perfect_hash<Keys...>::exists
    )
struct static_map : impl::minimal_perfect_hash<Keys...> {
private:
    using Hash = impl::minimal_perfect_hash<Keys...>;
    using Bucket = std::pair<const std::string_view, Value>;
    using Table = std::array<Bucket, Hash::table_size>;
    static constexpr std::array<size_t, Hash::table_size> indices {
        (Hash::hash(Keys) % Hash::table_size)...
    };

    template <size_t I, size_t...>
    static constexpr size_t pack_idx = 0;
    template <size_t I, size_t J, size_t... Js>
    static constexpr size_t pack_idx<I, J, Js...> =
        (I == J) ? 0 : pack_idx<I, Js...> + 1;

    template <size_t I, size_t... occupied, typename... Values>
    static constexpr Bucket populate(Values&&... values) {
        constexpr size_t idx = pack_idx<I, occupied...>;
        return {
            std::string_view(impl::unpack_string<idx, Keys...>::value),
            unpack_arg<idx>(std::forward<Values>(values)...)
        };
    }

    struct Iterator {
    private:
        const Table* m_table;
        size_t m_idx;

    public:
        using iterator_category = std::forward_iterator_tag;
        using difference_type = size_t;
        using value_type = const Bucket;
        using pointer = const Bucket*;
        using reference = const Bucket&;

        Iterator(const Table& table) : m_table(&table), m_idx(0) {}
        Iterator(const Table& table, sentinel) :
            m_table(&table), m_idx(Hash::table_size)
        {}

        reference operator*() const {
            return (*m_table)[indices[m_idx]];
        }

        pointer operator->() const {
            return &**this;
        }

        Iterator& operator++() {
            ++m_idx;
            return *this;
        }
    
        Iterator operator++(int) {
            Iterator copy = *this;
            ++m_idx;
            return copy;
        }

        friend bool operator==(const Iterator& self, const Iterator& other) {
            return self.m_table == other.m_table && self.m_idx == other.m_idx;
        }

        friend bool operator!=(const Iterator& self, const Iterator& other) {
            return self.m_table != other.m_table || self.m_idx != other.m_idx;
        }

        friend bool operator==(const Iterator& self, sentinel) {
            return self.m_idx == Hash::table_size;
        }

        friend bool operator==(sentinel, const Iterator& self) {
            return self.m_idx == Hash::table_size;
        }

        friend bool operator!=(const Iterator& self, sentinel) {
            return self.m_idx != Hash::table_size;
        }

        friend bool operator!=(sentinel, const Iterator& self) {
            return self.m_idx != Hash::table_size;
        }
    };

public:
    using mapped_type = Value;

    Table table;

    template <typename... Values>
        requires (
            sizeof...(Values) == sizeof...(Keys) &&
            (std::convertible_to<Values, Value> && ...)
        )
    constexpr static_map(Values&&... values) :
        table([]<size_t... Is>(std::index_sequence<Is...>, auto&&... values) {
            return Table{populate<Is, (Hash::hash(Keys) % Hash::table_size)...>(
                std::forward<decltype(values)>(values)...
            )...};
        }(
            std::make_index_sequence<Hash::table_size>{},
            std::forward<Values>(values)...
        ))
    {}

    /* Check whether the given index is in bounds for the table. */
    template <size_t I>
    static constexpr bool contains() {
        return I < Hash::table_size;
    }

    /* Check whether the map contains an arbitrary key at compile time. */
    template <static_str Key>
    static constexpr bool contains() {
        return ((Key == Keys) || ...);
    }

    /* Check whether the map contains an arbitrary key. */
    template <size_t N>
    constexpr bool contains(const char(&key)[N]) const {
        constexpr size_t M = N - 1;
        if constexpr ((M < Hash::min_length) | (M > Hash::max_length)) {
            return false;
        }
        const Bucket& result = table[Hash::hash(key) % Hash::table_size];
        for (size_t i = 0; i < M; ++i) {
            if (key[i] != result.first[i]) {
                return false;
            }
        }
        return true;
    }

    /* Check whether the map contains an arbitrary key. */
    template <is_static_str Key>
    constexpr bool contains(const Key& key) const {
        if constexpr ((key.size() < Hash::min_length) | (key.size() > Hash::max_length)) {
            return false;
        }
        const Bucket& result = table[Hash::hash(key) % Hash::table_size];
        if (key != result.first) {
            return false;
        }
        return true;
    }

    /* Check whether the map contains an arbitrary key. */
    template <std::convertible_to<const char*> T>
        requires (!string_literal<T> && !is_static_str<T>)
    constexpr bool contains(const T& key) const {
        const char* str = key;
        size_t len;
        const Bucket& result = table[Hash::hash(str, len) % Hash::table_size];
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
    template <std::convertible_to<std::string_view> T>
        requires (
            !string_literal<T> &&
            !is_static_str<T> &&
            !std::convertible_to<T, const char*>
        )
    constexpr bool contains(const T& key) const {
        std::string_view str = key;
        if ((str.size() < Hash::min_length) | (str.size() > Hash::max_length)) {
            return false;
        }
        const Bucket& result = table[Hash::hash(str) % Hash::table_size];
        if (str != result.first) {
            return false;
        }
        return true;
    }

    /* Check whether the given index is in bounds for the table. */
    template <std::convertible_to<size_t> T>
        requires (
            !string_literal<T> &&
            !is_static_str<T> &&
            !std::convertible_to<T, const char*> &&
            !std::convertible_to<T, std::string_view>
        )
    constexpr bool contains(const T& key) const {
        return size_t(key) < Hash::table_size;
    }

    /* Get the value associate with the key at index I at compile time, asserting that
    the index is within range. */
    template <size_t I> requires (I < sizeof...(Keys))
    constexpr const Bucket& get() const {
        return table[indices[I]];
    }

    /* Get the value associate with the key at index I at compile time, asserting that
    the index is within range. */
    template <size_t I> requires (I < sizeof...(Keys))
    Bucket& get() {
        return table[indices[I]];
    }

    /* Get the value associated with a key at compile time, asserting that it is
    present in the map. */
    template <static_str Key> requires (contains<Key>())
    constexpr const Value& get() const {
        constexpr size_t idx = Hash::hash(Key) % Hash::table_size;
        return table[idx].second;
    }

    /* Get the value associated with a key at compile time, asserting that it is
    present in the map. */
    template <static_str Key> requires (contains<Key>())
    Value& get() {
        constexpr size_t idx = Hash::hash(Key) % Hash::table_size;
        return table[idx].second;
    }

    /* Look up a key, returning a pointer to the corresponding value or nullptr if it
    is not present. */
    template <size_t N>
    constexpr const std::remove_reference_t<Value>* operator[](const char(&key)[N]) const {
        constexpr size_t M = N - 1;
        if constexpr ((M < Hash::min_length) | (M > Hash::max_length)) {
            return nullptr;
        }
        const Bucket& result = table[Hash::hash(key) % Hash::table_size];
        for (size_t i = 0; i < M; ++i) {
            if (key[i] != result.first[i]) {
                return nullptr;
            }
        }
        return &result.second;
    }

    /* Look up a key, returning a pointer to the corresponding value or nullptr if it
    is not present. */
    template <is_static_str Key>
    constexpr const std::remove_reference_t<Value>* operator[](const Key& key) const {
        if constexpr (
            (key.size() < Hash::min_length) | (key.size() > Hash::max_length)
        ) {
            return nullptr;
        }
        const Bucket& result = table[Hash::hash(key) % Hash::table_size];
        if (key != result.first) {
            return nullptr;
        }
        return &result.second;
    }

    /* Look up a key, returning a pointer to the corresponding value or nullptr if it
    is not present. */
    template <std::convertible_to<const char*> T>
        requires (!string_literal<T> && !is_static_str<T>)
    constexpr const std::remove_reference_t<Value>* operator[](const T& key) const {
        const char* str = key;
        size_t len;
        const Bucket& result = table[Hash::hash(str, len) % Hash::table_size];
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
    template <std::convertible_to<std::string_view> T>
        requires (
            !string_literal<T> &&
            !is_static_str<T> &&
            !std::convertible_to<T, const char*>
        )
    constexpr const std::remove_reference_t<Value>* operator[](const T& key) const {
        std::string_view str = key;
        if ((str.size() < Hash::min_length) | (str.size() > Hash::max_length)) {
            return nullptr;
        }
        const Bucket& result = table[Hash::hash(str) % Hash::table_size];
        if (str != result.first) {
            return nullptr;
        }
        return &result.second;
    }

    /* Look up an index in the table, returning the value that corresponds to the
    key at that index or nullptr if the index is out of bounds. */
    template <std::convertible_to<size_t> T>
        requires (
            !string_literal<T> &&
            !is_static_str<T> &&
            !std::convertible_to<T, const char*> &&
            !std::convertible_to<T, std::string_view>
        )
    constexpr const Bucket* operator[](const T& key) const {
        size_t idx = key;
        if (idx > Hash::table_size) {
            return nullptr;
        }
        return &table[indices[idx]];
    }

    /* Look up a key, returning a pointer to the corresponding value or nullptr if it
    is not present. */
    template <size_t N>
    std::remove_reference_t<Value>* operator[](const char(&key)[N]) {
        constexpr size_t M = N - 1;
        if constexpr ((M < Hash::min_length) | (M > Hash::max_length)) {
            return nullptr;
        }
        Bucket& result = table[Hash::hash(key) % Hash::table_size];
        for (size_t i = 0; i < M; ++i) {
            if (key[i] != result.first[i]) {
                return nullptr;
            }
        }
        return &result.second;
    }

    /* Look up a key, returning a pointer to the corresponding value or nullptr if it
    is not present. */
    template <is_static_str Key>
    std::remove_reference_t<Value>* operator[](const Key& key) {
        if constexpr (
            (key.size() < Hash::min_length) | (key.size() > Hash::max_length)
        ) {
            return nullptr;
        }
        Bucket& result = table[Hash::hash(key) % Hash::table_size];
        if (key != result.first) {
            return nullptr;
        }
        return &result.second;
    }

    /* Look up a key, returning a pointer to the corresponding value or nullptr if it
    is not present. */
    template <std::convertible_to<const char*> T>
        requires (!string_literal<T> && !is_static_str<T>)
    std::remove_reference_t<Value>* operator[](const T& key) {
        const char* str = key;
        size_t len;
        Bucket& result = table[Hash::hash(str, len) % Hash::table_size];
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
    template <std::convertible_to<std::string_view> T>
        requires (
            !string_literal<T> &&
            !is_static_str<T> &&
            !std::convertible_to<T, const char*>
        )
    std::remove_reference_t<Value>* operator[](const T& key) {
        std::string_view str = key;
        if ((str.size() < Hash::min_length) | (str.size() > Hash::max_length)) {
            return nullptr;
        }
        Bucket& result = table[Hash::hash(str) % Hash::table_size];
        if (str != result.first) {
            return nullptr;
        }
        return &result.second;
    }

    /* Look up an index in the table, returning the value that corresponds to the
    key at that index or nullptr if the index is out of bounds. */
    template <std::convertible_to<size_t> T>
        requires (
            !string_literal<T> &&
            !is_static_str<T> &&
            !std::convertible_to<T, const char*> &&
            !std::convertible_to<T, std::string_view>
        )
    Bucket* operator[](const T& key) {
        size_t idx = key;
        if (idx > Hash::table_size) {
            return nullptr;
        }
        return &table[indices[idx]];
    }

    constexpr size_t size() const { return sizeof...(Keys); }
    constexpr bool empty() const { return size() == 0; }
    Iterator begin() const { return {table}; }
    Iterator cbegin() const { return {table}; }
    sentinel end() const { return {}; }
    sentinel cend() const { return {}; }
};


/* A specialization of static_map that does not hold any values.  Such a data structure
is equivalent to a perfectly-hashed set of compile-time strings, which can be
efficiently searched at runtime.  Rather than dereferencing to a value, the buckets
and iterators will dereference to `string_view`s of the templated key buffers. */
template <static_str... Keys>
    requires (
        impl::strings_are_unique<Keys...> &&
        impl::minimal_perfect_hash<Keys...>::exists
    )
struct static_map<void, Keys...> : impl::minimal_perfect_hash<Keys...> {
private:
    using Hash = impl::minimal_perfect_hash<Keys...>;
    using Table = std::array<std::string_view, Hash::table_size>;
    static constexpr std::array<size_t, Hash::table_size> indices {
        (Hash::hash(Keys) % Hash::table_size)...
    };

    template <size_t I, size_t...>
    static constexpr size_t pack_idx = 0;
    template <size_t I, size_t J, size_t... Js>
    static constexpr size_t pack_idx<I, J, Js...> =
        (I == J) ? 0 : pack_idx<I, Js...> + 1;

    template <size_t I, size_t... occupied>
    static constexpr std::string_view populate() {
        constexpr size_t idx = pack_idx<I, occupied...>;
        return std::string_view(impl::unpack_string<idx, Keys...>::value);
    }

    struct Iterator {
    private:
        const Table* m_table;
        size_t m_idx;

    public:
        using iterator_category = std::forward_iterator_tag;
        using difference_type = size_t;
        using value_type = const std::string_view;
        using pointer = const std::string_view*;
        using reference = const std::string_view&;

        Iterator(const Table& table) : m_table(&table), m_idx(0) {}
        Iterator(const Table& table, sentinel) :
            m_table(&table), m_idx(Hash::table_size)
        {}

        reference operator*() const {
            return (*m_table)[indices[m_idx]];
        }

        pointer operator->() const {
            return &**this;
        }

        Iterator& operator++() {
            ++m_idx;
            return *this;
        }
    
        Iterator operator++(int) {
            Iterator copy = *this;
            ++m_idx;
            return copy;
        }

        friend bool operator==(const Iterator& self, const Iterator& other) {
            return self.m_table == other.m_table && self.m_idx == other.m_idx;
        }

        friend bool operator!=(const Iterator& self, const Iterator& other) {
            return self.m_table != other.m_table || self.m_idx != other.m_idx;
        }

        friend bool operator==(const Iterator& self, sentinel) {
            return self.m_idx == Hash::table_size;
        }

        friend bool operator==(sentinel, const Iterator& self) {
            return self.m_idx == Hash::table_size;
        }

        friend bool operator!=(const Iterator& self, sentinel) {
            return self.m_idx != Hash::table_size;
        }

        friend bool operator!=(sentinel, const Iterator& self) {
            return self.m_idx != Hash::table_size;
        }
    };

public:
    Table table;

    constexpr static_map() :
        table([]<size_t... Is>(std::index_sequence<Is...>) {
            return Table{populate<Is, (Hash::hash(Keys) % Hash::table_size)...>()...};
        }(std::make_index_sequence<Hash::table_size>{}))
    {}

    /* Check whether the given index is in bounds for the table. */
    template <size_t I>
    static constexpr bool contains() {
        return I < Hash::table_size;
    }

    /* Check whether the map contains an arbitrary key at compile time. */
    template <static_str Key>
    static constexpr bool contains() {
        return ((Key == Keys) || ...);
    }

    /* Check whether the map contains an arbitrary key. */
    template <size_t N>
    constexpr bool contains(const char(&key)[N]) const {
        constexpr size_t M = N - 1;
        if constexpr ((M < Hash::min_length) | (M > Hash::max_length)) {
            return false;
        }
        const std::string_view& result = table[Hash::hash(key) % Hash::table_size];
        for (size_t i = 0; i < M; ++i) {
            if (key[i] != result[i]) {
                return false;
            }
        }
        return true;
    }

    /* Check whether the map contains an arbitrary key. */
    template <is_static_str Key>
    constexpr bool contains(const Key& key) const {
        if constexpr (
            (key.size() < Hash::min_length) | (key.size() > Hash::max_length)
        ) {
            return false;
        }
        const std::string_view& result = table[Hash::hash(key) % Hash::table_size];
        if (key != result) {
            return false;
        }
        return true;
    }

    /* Check whether the map contains an arbitrary key. */
    template <std::convertible_to<const char*> T>
        requires (!string_literal<T> && !is_static_str<T>)
    constexpr bool contains(const T& key) const {
        const char* str = key;
        size_t len;
        const std::string_view& result = table[Hash::hash(str, len) % Hash::table_size];
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
    template <std::convertible_to<std::string_view> T>
        requires (
            !string_literal<T> &&
            !is_static_str<T> &&
            !std::convertible_to<T, const char*>
        )
    constexpr bool contains(const T& key) const {
        std::string_view str = key;
        if ((str.size() < Hash::min_length) | (str.size() > Hash::max_length)) {
            return false;
        }
        const std::string_view& result = table[Hash::hash(str) % Hash::table_size];
        if (str != result) {
            return false;
        }
        return true;
    }

    /* Check whether the given index is in bounds for the table. */
    template <std::convertible_to<size_t> T>
        requires (
            !string_literal<T> &&
            !is_static_str<T> &&
            !std::convertible_to<T, const char*> &&
            !std::convertible_to<T, std::string_view>
        )
    constexpr bool contains(const T& key) const {
        return size_t(key) < Hash::table_size;
    }

    /* Get the value associated with a key at compile time, asserting that it is
    present in the map. */
    template <size_t I> requires (I < sizeof...(Keys))
    constexpr const std::string_view& get() const {
        return table[indices[I]];
    }

    /* Get the value associated with a key at compile time, asserting that it is
    present in the map. */
    template <static_str Key> requires (contains<Key>())
    constexpr const std::string_view& get() const {
        constexpr size_t idx = Hash::hash(Key) % Hash::table_size;
        return table[idx];
    }

    /* Look up a key, returning a pointer to the corresponding key or nullptr if it is
    not present. */
    template <size_t N>
    constexpr const std::string_view* operator[](const char(&key)[N]) const {
        constexpr size_t M = N - 1;
        if constexpr ((M < Hash::min_length) | (M > Hash::max_length)) {
            return nullptr;
        }
        const std::string_view& result = table[Hash::hash(key) % Hash::table_size];
        for (size_t i = 0; i < M; ++i) {
            if (key[i] != result[i]) {
                return nullptr;
            }
        }
        return &result;
    }

    /* Look up a key, returning a pointer to the corresponding key or nullptr if it is
    not present. */
    template <is_static_str Key>
    constexpr const std::string_view* operator[](const Key& key) const {
        if constexpr (
            (key.size() < Hash::min_length) | (key.size() > Hash::max_length)
        ) {
            return nullptr;
        }
        const std::string_view& result = table[Hash::hash(key) % Hash::table_size];
        if (key != result) {
            return nullptr;
        }
        return &result;
    }

    /* Look up a key, returning a pointer to the corresponding key or nullptr if it is
    not present. */
    template <std::convertible_to<const char*> T>
        requires (!string_literal<T> && !is_static_str<T>)
    constexpr const std::string_view* operator[](const T& key) const {
        const char* str = key;
        size_t len;
        const std::string_view& result = table[Hash::hash(str, len) % Hash::table_size];
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
    template <std::convertible_to<std::string_view> T>
        requires (
            !string_literal<T> &&
            !is_static_str<T> &&
            !std::convertible_to<T, const char*>
        )
    constexpr const std::string_view* operator[](const T& key) const {
        std::string_view str = key;
        if ((str.size() < Hash::min_length) | (str.size() > Hash::max_length)) {
            return nullptr;
        }
        const std::string_view& result = table[Hash::hash(str) % Hash::table_size];
        if (str != result) {
            return nullptr;
        }
        return &result;
    }

    /* Look up an index in the table, returning the value that corresponds to the
    key at that index or nullptr if the index is out of bounds. */
    template <std::convertible_to<size_t> T>
        requires (
            !string_literal<T> &&
            !is_static_str<T> &&
            !std::convertible_to<T, const char*> &&
            !std::convertible_to<T, std::string_view>
        )
    constexpr const std::string_view* operator[](const T& key) const {
        size_t idx = key;
        if (idx > Hash::table_size) {
            return nullptr;
        }
        return &table[indices[idx]];
    }

    constexpr size_t size() const { return sizeof...(Keys); }
    constexpr bool empty() const { return size() == 0; }
    Iterator begin() const { return {table}; }
    Iterator cbegin() const { return {table}; }
    sentinel end() const { return {}; }
    sentinel cend() const { return {}; }
};


template <static_str... Keys>
using static_set = static_map<void, Keys...>;


}


namespace std {

    /* Specialize `std::tuple_size` to allow for structured bindings. */
    template <typename Value, bertrand::static_str... Keys>
    struct tuple_size<bertrand::static_map<Value, Keys...>> :
        std::integral_constant<size_t, sizeof...(Keys)>
    {};

    /* Specialize `std::tuple_element` to allow for structured bindings. */
    template <size_t I, typename Value, bertrand::static_str... Keys>
        requires (I < sizeof...(Keys))
    struct tuple_element<I, bertrand::static_map<Value, Keys...>> {
        using type = std::conditional_t<
            std::is_void_v<Value>,
            std::string_view,
            std::pair<const std::string_view, Value>
        >;
    };

    /* `std::get<"name">(dict)` is a type-safe accessor for `bertrand::static_map`. */
    template <size_t I, typename Value, bertrand::static_str... Keys>
        requires (I < sizeof...(Keys))
    constexpr const auto& get(const bertrand::static_map<Value, Keys...>& map) {
        return map.template get<I>();
    }

    /* `std::get<"name">(dict)` is a type-safe accessor for `bertrand::static_map`. */
    template <size_t I, typename Value, bertrand::static_str... Keys>
        requires (I < sizeof...(Keys) && !std::is_void_v<Value>)
    auto& get(bertrand::static_map<Value, Keys...>& map) {
        return map.template get<I>();
    }

    /* `std::get<"name">(dict)` is a type-safe accessor for `bertrand::static_map`. */
    template <bertrand::static_str Key, typename Value, bertrand::static_str... Keys>
        requires (
            bertrand::static_map<Value, Keys...>::template contains<Key>()
        )
    constexpr const auto& get(const bertrand::static_map<Value, Keys...>& map) {
        return map.template get<Key>();
    }

    /* `std::get<"name">(dict)` is a type-safe accessor for `bertrand::static_map`. */
    template <bertrand::static_str Key, typename Value, bertrand::static_str... Keys>
        requires (
            bertrand::static_map<Value, Keys...>::template contains<Key>() &&
            !std::is_void_v<Value>
        )
    auto& get(bertrand::static_map<Value, Keys...>& map) {
        return map.template get<Key>();
    }

}


#endif  // BERTRAND_STATIC_MAP_H