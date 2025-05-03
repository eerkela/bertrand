#ifndef BERTRAND_BITSET_H
#define BERTRAND_BITSET_H

#include "bertrand/common.h"
#include "bertrand/except.h"
#include "bertrand/math.h"
#include "bertrand/iter.h"
#include "bertrand/static_str.h"


namespace bertrand {


namespace impl {
    struct Bits_tag {};
    struct UInt_tag {};
    struct Int_tag {};
    struct Float_tag {};

}


namespace meta {

    template <typename T>
    concept Bits = inherits<T, impl::Bits_tag>;

    template <typename T>
    concept UInt = inherits<T, impl::UInt_tag>;

    template <typename T>
    concept Int = inherits<T, impl::Int_tag>;

    template <typename T>
    concept Float = inherits<T, impl::Float_tag>;

}


namespace impl {
    template <typename T>
    constexpr size_t _bitcount = 0;
    template <meta::boolean T>
    constexpr size_t _bitcount<T> = 1;
    template <meta::integer T>
    constexpr size_t _bitcount<T> = sizeof(T) * 8;
    template <meta::Bits T>
    constexpr size_t _bitcount<T> = T::size();
    template <meta::UInt T>
    constexpr size_t _bitcount<T> = T::size();
    template <meta::Int T>
    constexpr size_t _bitcount<T> = T::size();

    template <typename... Ts> requires ((_bitcount<Ts> > 0) && ...)
    constexpr size_t bitcount = (_bitcount<Ts> + ... + 0);

    template <size_t M>
    struct word {
        using type = uint64_t;
        static constexpr size_t size = sizeof(type) * 8;
        struct big {
            static constexpr bool composite = true;
            type h;
            type l;
            constexpr big(type v) noexcept : l(v) {}
            constexpr big(type hi, type lo) noexcept : h(hi), l(lo) {}
            constexpr type hi() const noexcept { return h; }
            constexpr type lo() const noexcept { return l; }
            static constexpr big mul(type a, type b) noexcept {
                constexpr size_t chunk = size / 2;
                constexpr type mask = (type(1) << chunk) - 1;

                // 1. split a, b into low and high halves
                big x = {a >> chunk, a & mask};
                big y = {b >> chunk, b & mask};

                // 2. compute partial products
                type lo_lo = x.lo() * y.lo();
                type lo_hi = x.lo() * y.hi();
                type hi_lo = x.hi() * y.lo();
                type hi_hi = x.hi() * y.hi();

                // 3. combine cross terms
                type cross = (lo_lo >> chunk) + (lo_hi & mask) + (hi_lo & mask);
                type carry = cross >> chunk;

                // 4. compute result
                return {
                    hi_hi + (lo_hi >> chunk) + (hi_lo >> chunk) + carry,
                    (lo_lo & mask) | (cross << chunk)
                };
            }
            constexpr big operator/(type v) const noexcept {
                /// NOTE: this implementation is taken from the libdivide reference:
                /// https://github.com/ridiculousfish/libdivide/blob/master/doc/divlu.c
                /// It should be much faster than the naive approach found in Hacker's Delight.
                using Signed = meta::as_signed<type>;
                constexpr size_t chunk = size / 2;
                constexpr type b = type(1) << chunk;
                constexpr type mask = b - 1;
                type h = this->h;
                type l = this->l;

                // 1. If the high bits are empty, then we devolve to a single word divide.
                if (!h) {
                    return {l / v, l % v};
                }

                // 2. Check for overflow and divide by zero.
                if (h >= v) {
                    return {
                        std::numeric_limits<type>::max(),
                        std::numeric_limits<type>::max()
                    };
                }

                // 3. Left shift divisor until the most significant bit is set.  This
                // cannot overflow the numerator because u.hi() < v.  The strange
                // bitwise AND is meant to avoid undefined behavior when shifting by a
                // full word size.  It is taken from
                // https://ridiculousfish.com/blog/posts/labor-of-division-episode-v.html
                size_t shift = std::countl_zero(v);
                v <<= shift;
                h <<= shift;
                h |= ((l >> (-shift & (size - 1))) & (-Signed(shift) >> (size - 1)));
                l <<= shift;

                // 4. Split divisor and low bits of numerator into partial words.
                big n = {l >> chunk, l & mask};
                big d = {v >> chunk, v & mask};

                // 5. Estimate q1 = [n3 n2 n1] / [d1 d0].  Note that while qhat may be 2
                // half-words, q1 is always just the lower half, which translates to the upper
                // half of the final quotient.
                type qhat = n.hi() / d.hi();
                type rhat = n.hi() % d.hi();
                type c1 = qhat * d.lo();
                type c2 = rhat * b + n.lo();
                if (c1 > c2) {
                    qhat -= 1 + ((c1 - c2) > v);
                }
                type q1 = qhat & mask;

                // 6. Compute the true (normalized) partial remainder.
                type r = n.hi() * b + n.lo() - q1 * v;

                // 7. Estimate q0 = [r1 r0 n0] / [d1 d0].  These are the bottom bits of the
                // final quotient.
                qhat = r / d.hi();
                rhat = r % d.hi();
                c1 = qhat * d.lo();
                c2 = rhat * b + n.lo();
                if (c1 > c2) {
                    qhat -= 1 + ((c1 - c2) > v);
                }
                type q0 = qhat & mask;

                // 8. Return the quotient and unnormalized remainder
                return {(q1 << chunk) | q0, ((r * b) + n.lo() - (q0 * v)) >> shift};
            }
            constexpr bool operator>(big other) const noexcept {
                return h > other.h || l > other.l;
            }
        };
    };
    template <size_t M> requires (M <= (sizeof(uint8_t) * 8))
    struct word<M> {
        using type = uint8_t;
        static constexpr size_t size = sizeof(type) * 8;
        struct big {
            using type = uint16_t;
            static constexpr bool composite = false;
            type value;
            constexpr big(type v) noexcept : value(v) {}
            constexpr big(word::type hi, word::type lo) noexcept :
                value((type(hi) << size) | type(lo))
            {}
            constexpr word::type hi() const noexcept {
                return word::type(value >> size);
            }
            constexpr word::type lo() const noexcept {
                return word::type(value & ((type(1) << size) - 1));
            }
            static constexpr big mul(word::type a, word::type b) noexcept {
                return {type(type(a) * type(b))};
            }
            constexpr big operator/(word::type v) const noexcept {
                return {type(value / v)};
            }
            constexpr bool operator>(big other) const noexcept {
                return value > other.value;
            }
        };
    };
    template <size_t M>
        requires (M > (sizeof(uint8_t) * 8) && M <= (sizeof(uint16_t) * 8))
    struct word<M> {
        using type = uint16_t;
        static constexpr size_t size = sizeof(type) * 8;
        struct big {
            using type = uint32_t;
            static constexpr bool composite = false;
            type value;
            constexpr big(type v) noexcept : value(v) {}
            constexpr big(word::type hi, word::type lo) noexcept :
                value((type(hi) << size) | type(lo))
            {}
            constexpr word::type hi() const noexcept {
                return word::type(value >> size);
            }
            constexpr word::type lo() const noexcept {
                return word::type(value & ((type(1) << size) - 1));
            }
            static constexpr big mul(word::type a, word::type b) noexcept {
                return {type(type(a) * type(b))};
            }
            constexpr big operator/(word::type v) const noexcept {
                return {type(value / v)};
            }
            constexpr bool operator>(big other) const noexcept {
                return value > other.value;
            }
        };
    };
    template <size_t M>
        requires (M > (sizeof(uint16_t) * 8) && M <= (sizeof(uint32_t) * 8))
    struct word<M> {
        using type = uint32_t;
        static constexpr size_t size = sizeof(type) * 8;
        struct big {
            using type = uint64_t;
            static constexpr bool composite = false;
            type value;
            constexpr big(type v) noexcept : value(v) {}
            constexpr big(word::type hi, word::type lo) noexcept :
                value((type(hi) << size) | type(lo))
            {}
            constexpr word::type hi() const noexcept {
                return word::type(value >> size);
            }
            constexpr word::type lo() const noexcept {
                return word::type(value & ((type(1) << size) - 1));
            }
            static constexpr big mul(word::type a, word::type b) noexcept {
                return {type(type(a) * type(b))};
            }
            constexpr big operator/(word::type v) const noexcept {
                return {type(value / v)};
            }
            constexpr bool operator>(big other) const noexcept {
                return value > other.value;
            }
        };
    };

    inline constexpr std::array<double, 64> log2_table {
        0,
        0,
        1,
        1.584962500721156,
        2,
        2.321928094887362,
        2.584962500721156,
        2.807354922057604,
        3,
        3.169925001442312,
        3.321928094887362,
        3.4594316186372973,
        3.584962500721156,
        3.700439718141092,
        3.807354922057604,
        3.9068905956085187,
        4,
        4.087462841250339,
        4.169925001442312,
        4.247927513443585,
        4.321928094887363,
        4.392317422778761,
        4.459431618637297,
        4.523561956057013,
        4.584962500721156,
        4.643856189774724,
        4.700439718141092,
        4.754887502163468,
        4.807354922057604,
        4.857980995127572,
        4.906890595608519,
        4.954196310386875,
        5,
        5.044394119358453,
        5.087462841250339,
        5.129283016944966,
        5.169925001442312,
        5.20945336562895,
        5.247927513443585,
        5.285402218862249,
        5.321928094887363,
        5.357552004618084,
        5.392317422778761,
        5.426264754702098,
        5.459431618637297,
        5.491853096329675,
        5.523561956057013,
        5.554588851677638,
        5.584962500721156,
        5.614709844115208,
        5.643856189774724,
        5.672425341971495,
        5.700439718141092,
        5.727920454563199,
        5.754887502163468,
        5.78135971352466,
        5.807354922057604,
        5.832890014164741,
        5.857980995127572,
        5.882643049361842,
        5.906890595608519,
        5.930737337562887,
        5.954196310386875,
        5.977279923499917,
    };

    template <size_t base, std::array<size_t, base> arr>
    struct unique_digit_lengths {
        template <size_t I = 0, size_t... sizes>
        static constexpr decltype(auto) operator()() noexcept {
            if constexpr (I == 0) {
                return (operator()<I + 1, arr[I]>());
            } else {
                if constexpr (
                    arr[I] < meta::unpack_value<sizeof...(sizes) - 1, sizes...>
                ) {
                    return (operator()<I + 1, sizes..., arr[I]>());
                } else {
                    return (operator()<I + 1, sizes...>());
                }
            }
        }
        template <size_t I, size_t... sizes> requires (I == base)
        static constexpr std::array<size_t, sizeof...(sizes)> operator()() noexcept {
            return {sizes...};
        }
    };

    template <typename T>
    concept exact_bits =
        meta::boolean<T> || meta::Bits<T> || meta::UInt<T> || meta::Int<T>;

    template <typename T>
    concept round_bits =
        meta::integer<T> || meta::Bits<T> || meta::UInt<T> || meta::Int<T>;

}


/* A simple bitset type that stores up to `N` boolean flags in a fixed-size array of
machine words.  Allows a wider range of operations than `std::bitset<N>`, including
full, bigint-style unsigned arithmetic, lexicographic comparisons, fast string
encoding/decoding, one-hot decomposition, structured bindings, and more.

`Bits` are generally optimized for small to medium sizes (up to a few thousand bits),
and do not replace arbitrary-precision libraries like GMP or `boost::multiprecision`,
which may be preferable in some situations.  Instead, they are meant as a lightweight,
general-purpose utility for low-level bit manipulation and efficient storage of boolean
flags, and as a building block for further abstractions. */
template <size_t N>
struct Bits : impl::Bits_tag {
    using word = impl::word<N>::type;
    using value_type = bool;
    using size_type = size_t;
    using index_type = ssize_t;

    /* The number of bits that are held in the set. */
    [[nodiscard]] static constexpr size_type size() noexcept { return N; }
    [[nodiscard]] static constexpr index_type ssize() noexcept { return index_type(N); }

    /* The total number of bits needed to represent the array.  This will always be a
    multiple of the machine's word size, or a power of two less than that. */
    [[nodiscard]] static constexpr size_type capacity() noexcept {
        return array_size * word_size;
    }

private:
    using big_word = impl::word<N>::big;
    static constexpr size_type word_size = impl::word<N>::size;
    static constexpr size_type array_size = (N + word_size - 1) / word_size;
    static constexpr word end_mask = word(word(1) << (N % word_size)) - word(1);

    template <size_type I>
    static constexpr void from_bits(std::array<word, array_size>& data) noexcept {}

    template <size_type I, meta::boolean T, typename... Ts>
    static constexpr void from_bits(
        std::array<word, array_size>& data,
        T v,
        const Ts&... rest
    ) noexcept {
        data[I / word_size] |= word(word(v) << (I % word_size));
        from_bits<I + 1>(data, rest...);
    }

    template <size_type I, meta::Bits T, typename... Ts>
    static constexpr void from_bits(
        std::array<word, array_size>& data,
        const T& v,
        const Ts&... rest
    ) noexcept {
        static constexpr size_type J = I + impl::bitcount<T>;
        static constexpr size_type start_bit = I % word_size;
        static constexpr size_type start_word = I / word_size;
        static constexpr size_type stop_word = J / word_size;
        static constexpr size_type stop_bit = J % word_size;

        if constexpr (start_bit == 0) {
            for (size_type i = 0; i < v.buffer.size(); ++i) {
                data[start_word + i] |= v.buffer[i];
            }

        } else {
            T temp = v;
            data[start_word] |= word(temp.buffer[0] << start_bit);
            size_type consumed = word_size - start_bit;
            temp >>= consumed;
            size_type i = start_word;
            size_type j = 0;
            while (consumed < temp.size()) {
                data[++i] |= temp.buffer[j++];
                consumed += word_size;
            }
        }

        from_bits<I + T::size()>(data, rest...);
    }

    template <size_type I, typename T, typename... Ts>
        requires (meta::UInt<T> || meta::Int<T>)
    static constexpr void from_bits(
        std::array<word, array_size>& data,
        const T& v,
        const Ts&... rest
    ) noexcept {
        from_bits<I, T, Ts...>(data, v.value, rest...);
    }

    template <size_type I>
    static constexpr void from_ints(std::array<word, array_size>& data) noexcept {
        if constexpr (end_mask) {
            data[array_size - 1] &= end_mask;
        }
    }

    template <size_type I, meta::integer T, typename... Ts>
    static constexpr void from_ints(
        std::array<word, array_size>& data,
        T v,
        const Ts&... rest
    ) noexcept {
        static constexpr size_type J = I + impl::bitcount<T>;
        static constexpr size_type start_bit = I % word_size;
        static constexpr size_type start_word = I / word_size;
        static constexpr size_type stop_word = J / word_size;
        static constexpr size_type stop_bit = J % word_size;

        if constexpr (stop_bit == 0 || start_word == stop_word) {
            data[start_word] |= word(word(v) << start_bit);
        } else {
            data[start_word] |= word(word(v) << start_bit);
            size_type consumed = word_size - start_bit;
            v >>= consumed;
            size_type i = start_word;
            while (consumed < impl::bitcount<T>) {
                data[++i] |= word(v);
                if constexpr (word_size < impl::bitcount<T>) {
                    v >>= word_size;
                }
                consumed += word_size;
            }
        }

        from_ints<I + impl::bitcount<T>>(data, rest...);
    }

    template <size_type I, meta::Bits T, typename... Ts>
    static constexpr void from_ints(
        std::array<word, array_size>& data,
        const T& v,
        const Ts&... rest
    ) noexcept {
        static constexpr size_type J = I + impl::bitcount<T>;
        static constexpr size_type start_bit = I % word_size;
        static constexpr size_type start_word = I / word_size;
        static constexpr size_type stop_word = J / word_size;
        static constexpr size_type stop_bit = J % word_size;

        if constexpr (start_bit == 0) {
            for (size_type i = 0; i < v.buffer.size(); ++i) {
                data[start_word + i] |= v.buffer[i];
            }

        } else {
            T temp = v;
            data[start_word] |= word(temp.buffer[0] << start_bit);
            size_type consumed = word_size - start_bit;
            temp >>= consumed;
            size_type i = start_word;
            size_type j = 0;
            while (consumed < temp.size()) {
                data[++i] |= temp.buffer[j++];
                consumed += word_size;
            }
        }

        from_ints<I + T::size()>(data, rest...);
    }

    template <size_type I, typename T, typename... Ts>
        requires (meta::UInt<T> || meta::Int<T>)
    static constexpr void from_ints(
        std::array<word, array_size>& data,
        const T& v,
        const Ts&... rest
    ) noexcept {
        from_ints<I, T, Ts...>(data, v.value, rest...);
    }

    template <const auto&... keys, size_type... Is>
    static constexpr auto digit_map(std::index_sequence<Is...>) {
        return string_map<word, keys...>{word(Is)...};
    }

    template <size_type... sizes>
    static constexpr auto unique_digit_lengths() {
        std::array<size_type, sizeof...(sizes)> out {sizes...};
        bertrand::sort<impl::Greater>(out);
        return out;
    }

    template <size_type I, const auto& digits, const auto& widths, size_type per_digit>
    static constexpr Expected<void, ValueError, OverflowError> from_string_helper(
        Bits& result,
        std::string_view str,
        size_type& i
    ) {
        if constexpr (I < widths.size()) {
            // search the digit map for a matching digit
            auto it = digits.find(str.substr(i, widths[I]));

            // if a digit was found, then we can multiply the bitset by the base,
            // join the digit, and then advance the loop index
            if (it != digits.end()) {

                // if the base is a power of 2, then we can use a left shift and
                // bitwise OR to avoid expensive multiplication and addition.
                if constexpr (impl::is_power2(digits.size())) {

                    // if a left shift by `per_digit` would cause the most significant
                    // set bit to overflow, then we have an OverflowError
                    size_type j = array_size - 1;
                    size_type msb = size_type(std::countl_zero(result.buffer[j]));
                    if (msb < word_size) {  // msb is in last word
                        size_type distance = N - (word_size * j + word_size - 1 - msb);
                        if (distance <= per_digit) {  // would overflow
                            return OverflowError();
                        }
                    } else {  // msb is in a previous word
                        // stop early when distance exceeds shift amount
                        size_type distance = N % word_size;
                        while (distance <= per_digit && j-- > 0) {
                            msb = size_type(std::countl_zero(result.buffer[j]));
                            if (msb < word_size) {
                                distance = N - (word_size * j + word_size - 1 - msb);
                                if (distance <= per_digit) {
                                    return OverflowError();
                                }
                            } else {
                                distance += word_size;
                            }
                        }
                    }

                    result <<= per_digit;
                    result |= it->second;

                // otherwise, we use overflow-safe operators to detect errors
                } else {
                    bool overflow;
                    result.mul(word(digits.size()), overflow, result);
                    if (overflow) {
                        return OverflowError();
                    }
                    result.add(it->second, overflow, result);
                    if (overflow) {
                        return OverflowError();
                    }
                }

                // terminate recursion normally and advance by width of digit 
                i += widths[I];
                return {};
            }

            // otherwise, try again with the next largest width
            return from_string_helper<
                I + 1,
                digits,
                widths,
                per_digit
            >(result, str, i);

        // if no digit was found, then the string is invalid
        } else if consteval {
            static constexpr static_str message =
                "string must contain only '" + digits.template join<"', '">();
            return ValueError(message);
        } else {
            return ValueError(
                "string must contain only '" + digits.template join<"', '">() +
                "', not '" + std::string(str.substr(i, widths[0])) +
                "' at index " + std::to_string(i)
            );
        }
    }

    template <size_t J, const Bits& divisor>
    static constexpr std::string_view to_string_helper(
        const auto& digits,
        Bits& quotient,
        Bits& remainder,
        size_type& size
    ) {
        quotient.divmod(divisor, quotient, remainder);
        std::string_view out = digits[remainder.data()[0]];
        size += out.size();
        return out;
    }

    template <size_t J>
    constexpr std::string_view to_string_helper(
        const auto& digits,
        size_type& size
    ) const {
        bool bit = buffer[J / word_size] & (word(1) << (J % word_size));
        std::string_view out = digits[bit];
        size += out.size();
        return out;
    }

public:
    std::array<word, array_size> buffer;

    /* A mutable reference to a single bit in the set. */
    struct reference {
    private:
        friend Bits;
        word* value = nullptr;
        word index = 0;

        constexpr reference() noexcept = default;
        constexpr reference(word* value, word index) noexcept :
            value(value), index(index)
        {}

    public:
        [[nodiscard]] constexpr operator bool() const noexcept {
            return *value & (word(1) << index);
        }

        [[nodiscard]] constexpr bool operator~() const noexcept {
            return !*this;
        }

        constexpr reference& operator=(bool x) noexcept {
            *value = (*value & ~(word(1) << index)) | (x << index);
            return *this;
        }

        constexpr reference& flip() noexcept {
            *value ^= word(1) << index;
            return *this;
        }
    };

    /* An iterator over the individual bits within the set, from least to most
    significant. */
    struct iterator {
        using iterator_category = std::random_access_iterator_tag;
        using difference_type = index_type;
        using value_type = Bits::reference;
        using reference = value_type&;
        using const_reference = const value_type&;
        using pointer = value_type*;
        using const_pointer = const value_type*;

    private:
        friend Bits;
        Bits* self;
        difference_type index;
        mutable value_type cache;

        constexpr iterator(Bits* self, difference_type index) noexcept :
            self(self), index(index)
        {}

    public:
        [[nodiscard]] constexpr reference operator*() noexcept(!DEBUG) {
            if constexpr (DEBUG) {
                if (index < 0 || index >= N) {
                    throw IndexError(std::to_string(index));
                }
            }
            cache = {&self->buffer[index / word_size], word(index % word_size)};
            return cache;
        }

        [[nodiscard]] constexpr const_reference operator*() const noexcept(!DEBUG) {
            if constexpr (DEBUG) {
                if (index < 0 || index >= N) {
                    throw IndexError(std::to_string(index));
                }
            }
            cache = {&self->buffer[index / word_size], word(index % word_size)};
            return cache;
        }

        [[nodiscard]] constexpr pointer operator->() noexcept(!DEBUG) {
            return &**this;
        }

        [[nodiscard]] constexpr const_pointer operator->() const noexcept(!DEBUG) {
            return &**this;
        }

        [[nodiscard]] constexpr value_type operator[](difference_type n) const
            noexcept(!DEBUG)
        {
            size_t idx = static_cast<size_t>(index + n);
            if constexpr (DEBUG) {
                if (idx >= N) {
                    throw IndexError(std::to_string(index + n));
                }
            }
            return {&self->buffer[idx / word_size], word(idx % word_size)};
        }

        constexpr iterator& operator++() noexcept {
            ++index;
            return *this;
        }

        [[nodiscard]] constexpr iterator operator++(int) noexcept {
            iterator copy = *this;
            ++index;
            return copy;
        }

        constexpr iterator& operator+=(difference_type n) noexcept {
            index += n;
            return *this;
        }

        [[nodiscard]] friend constexpr iterator operator+(
            const iterator& lhs,
            difference_type rhs
        ) noexcept {
            return {lhs.self, lhs.index + rhs};
        }

        [[nodiscard]] friend constexpr iterator operator+(
            difference_type lhs,
            const iterator& rhs
        ) noexcept {
            return {rhs.self, rhs.index + lhs};
        }

        constexpr iterator& operator--() noexcept {
            --index;
            return *this;
        }

        [[nodiscard]] constexpr iterator operator--(int) noexcept {
            iterator copy = *this;
            --index;
            return copy;
        }

        constexpr iterator& operator-=(difference_type n) noexcept {
            index -= n;
            return *this;
        }

        [[nodiscard]] friend constexpr iterator operator-(
            const iterator& lhs,
            difference_type rhs
        ) noexcept {
            return {lhs.self, lhs.index - rhs};
        }

        [[nodiscard]] friend constexpr iterator operator-(
            difference_type lhs,
            const iterator& rhs
        ) noexcept {
            return {rhs.self, lhs - rhs.index};
        }

        [[nodiscard]] friend constexpr difference_type operator-(
            const iterator& lhs,
            const iterator& rhs
        ) noexcept {
            return lhs.index - rhs.index;
        }

        [[nodiscard]] friend constexpr auto operator<=>(
            const iterator& lhs,
            const iterator& rhs
        ) noexcept {
            return lhs.index <=> rhs.index;
        }

        [[nodiscard]] friend constexpr bool operator==(
            const iterator& lhs,
            const iterator& rhs
        ) noexcept {
            return lhs.index == rhs.index;
        }
    };

    /* A read-only iterator over the individual bits within the set, from least to
    most significant. */
    struct const_iterator {
        using iterator_category = std::random_access_iterator_tag;
        using difference_type = index_type;
        using value_type = const bool;
        using reference = const value_type&;
        using const_reference = reference;
        using pointer = const value_type*;
        using const_pointer = pointer;

    private:
        friend Bits;
        const Bits* self;
        difference_type index;
        mutable bool cache;

        constexpr const_iterator(const Bits* self, difference_type index) noexcept :
            self(self), index(index)
        {}

    public:
        [[nodiscard]] constexpr const_reference operator*() const noexcept(!DEBUG) {
            if constexpr (DEBUG) {
                if (index < 0 || index >= N) {
                    throw IndexError(std::to_string(index));
                }
            }
            cache = self->get(static_cast<size_type>(index));
            return cache;
        }

        [[nodiscard]] constexpr const_pointer operator->() const noexcept(!DEBUG) {
            return &**this;
        }

        [[nodiscard]] constexpr value_type operator[](difference_type n) const noexcept {
            size_t idx = static_cast<size_t>(index + n);
            if constexpr (DEBUG) {
                if (idx >= N) {
                    throw IndexError(std::to_string(index + n));
                }
            }
            return self->get(static_cast<size_type>(index));
        }

        constexpr const_iterator& operator++() noexcept {
            ++index;
            return *this;
        }

        [[nodiscard]] constexpr const_iterator operator++(int) noexcept {
            const_iterator copy = *this;
            ++index;
            return copy;
        }

        constexpr const_iterator& operator+=(difference_type n) noexcept {
            index += n;
            return *this;
        }

        [[nodiscard]] friend constexpr const_iterator operator+(
            const const_iterator& lhs,
            difference_type rhs
        ) noexcept {
            return {lhs.self, lhs.index + rhs};
        }

        [[nodiscard]] friend constexpr const_iterator operator+(
            difference_type lhs,
            const const_iterator& rhs
        ) noexcept {
            return {rhs.self, rhs.index + lhs};
        }

        constexpr const_iterator& operator--() noexcept {
            --index;
            return *this;
        }

        [[nodiscard]] constexpr const_iterator operator--(int) noexcept {
            const_iterator copy = *this;
            --index;
            return copy;
        }

        constexpr const_iterator& operator-=(difference_type n) noexcept {
            index -= n;
            return *this;
        }

        [[nodiscard]] friend constexpr const_iterator operator-(
            const const_iterator& lhs,
            difference_type rhs
        ) noexcept {
            return {lhs.self, lhs.index - rhs};
        }

        [[nodiscard]] friend constexpr const_iterator operator-(
            difference_type lhs,
            const const_iterator& rhs
        ) noexcept {
            return {rhs.self, lhs - rhs.index};
        }

        [[nodiscard]] friend constexpr difference_type operator-(
            const const_iterator& lhs,
            const const_iterator& rhs
        ) noexcept {
            return lhs.index - rhs.index;
        }

        [[nodiscard]] friend constexpr auto operator<=>(
            const const_iterator& lhs,
            const const_iterator& rhs
        ) noexcept {
            return lhs.index <=> rhs.index;
        }

        [[nodiscard]] friend constexpr bool operator==(
            const const_iterator& lhs,
            const const_iterator& rhs
        ) noexcept {
            return lhs.index == rhs.index;
        }
    };

    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    /* A range that decomposes a bitmask into its one-hot components from least to
    most significant. */
    struct one_hot {
    private:
        friend Bits;
        const Bits* self;
        index_type start;
        index_type stop;

        constexpr one_hot(
            const Bits* self,
            index_type start,
            index_type stop
        ) noexcept :
            self(self), start(start), stop(stop)
        {}

    public:
        struct iterator {
            using iterator_category = std::input_iterator_tag;
            using difference_type = index_type;
            using value_type = Bits;
            using reference = const value_type&;
            using const_reference = reference;
            using pointer = const value_type*;
            using const_pointer = pointer;

        private:
            friend one_hot;
            const Bits* self;
            difference_type index;
            difference_type stop;
            value_type curr;

            static constexpr difference_type next(
                const Bits* self,
                difference_type start,
                difference_type stop
            ) noexcept {
                Bits temp;
                temp.fill(1);
                temp <<= N - size_type(stop);
                temp >>= N - size_type(stop - start);
                temp <<= size_type(start);  
                for (size_type i = 0; i < array_size; ++i) {
                    size_type j = size_type(std::countr_zero(
                        word(self->buffer[i] & temp.buffer[i])
                    ));
                    if (j < word_size) {
                        return difference_type(word_size * i + j);
                    }
                }
                return stop;
            }

            constexpr iterator(
                const Bits* self,
                difference_type start,
                difference_type stop
            ) noexcept :
                self(self),
                index(start < stop ? next(self, start, stop) : stop),
                stop(stop),
                curr(word(1))
            {
                curr <<= size_type(index);
            }

        public:
            [[nodiscard]] constexpr reference operator*() const noexcept {
                return curr;
            }

            [[nodiscard]] constexpr pointer operator->() const noexcept {
                return &curr;
            }

            constexpr iterator& operator++() noexcept {
                if (index >= stop) {
                    return *this;
                }
                difference_type new_index = next(self, index + 1, stop);
                curr <<= size_type(new_index - index);
                index = new_index;
                return *this;
            }

            [[nodiscard]] constexpr iterator operator++(int) noexcept {
                iterator copy = *this;
                ++*this;
                return copy;
            }

            [[nodiscard]] friend constexpr bool operator==(
                const iterator& self,
                impl::sentinel
            ) noexcept {
                return self.index >= self.stop;
            }

            [[nodiscard]] friend constexpr bool operator==(
                impl::sentinel,
                const iterator& self
            ) noexcept {
                return self.index >= self.stop;
            }

            [[nodiscard]] friend constexpr bool operator!=(
                const iterator& self,
                impl::sentinel
            ) noexcept {
                return self.index < self.stop;
            }

            [[nodiscard]] friend constexpr bool operator!=(
                impl::sentinel,
                const iterator& self
            ) noexcept {
                return self.index < self.stop;
            }
        };

        [[nodiscard]] constexpr iterator begin() const noexcept {
            return {self, start, stop};
        }
        [[nodiscard]] constexpr iterator cbegin() const noexcept {
            return {self, start, stop};
        }
        [[nodiscard]] constexpr impl::sentinel end() const noexcept { return {}; }
        [[nodiscard]] constexpr impl::sentinel cend() const noexcept { return {}; }
    };

    /// TODO: maybe the bool and int constructors need to accept values in big-endian
    /// order, in order to conform to the string constructor and mathematical
    /// intuition.  The only problem in that regard is that indexing and iteration are
    /// still little-endian, so the first bit is the least-significant, and that could
    /// cause confusion.  Perhaps the best thing to do is to also swap indexing over
    /// to being big-endian at the same time, so that everything is consistent.

    /* Construct a bitset from a variadic parameter pack of boolean initializers.  The
    flag sequence is parsed left-to-right in little-endian order, meaning the first
    boolean will correspond to the least significant bit of the first word in the
    bitset, which is stored at index 0.  The last boolean will be the most significant
    bit, which is stored at index `sizeof...(bits) - 1`.  The total number of arguments
    cannot exceed `N`, and any remaining bits will be initialized to zero. */
    template <typename... bits>
        requires (impl::exact_bits<bits> && ... && (impl::bitcount<bits...> <= N))
    constexpr Bits(const bits&... vals) noexcept : buffer{} {
        from_bits<0>(buffer, vals...);
    }

    /* Construct a bitset from a sequence of integer values whose bit widths sum to an
    amount less than or equal to the bitset's storage capacity.  If the bitset width is
    not an even multiple of the word size, then any upper bits above `N` will be masked
    off and initialized to zero.

    The values are parsed left-to-right in little-endian order, and are joined together
    using bitwise OR to form the final bitset.  The initializers do not need to have
    the same type, as long as their combined widths do not exceed the bitset's
    capacity.  Any remaining bits will be initialized to zero.  Little-endianness in
    this context means that the first integer will correspond to the `M` least
    significant bits of the bitarray, where `M` is the bit width of the first
    integer.  The last integer will correspond to the most significant bits of the
    bitarray, which will be terminate at index `sum(M_i...)`, where `M_i` is the bit
    width of the `i`th integer. */
    template <typename... words>
        requires (
            sizeof...(words) > 0 &&
            !(impl::exact_bits<words> && ...) &&
            (impl::round_bits<words> && ... && (
                impl::bitcount<words...> <= max(capacity(), impl::bitcount<uint64_t>)
            ))
        )
    constexpr Bits(const words&... vals) noexcept : buffer{} {
        from_ints<0>(buffer, vals...);
    }

    /* Construct a bitset from a string literal containing exactly `N` of the indicated
    true and false characters.  Throws a `ValueError` if the string contains any
    characters other than the indicated ones.  A constructor of this form allows for
    CTAD-based width deduction from string literal initializers. */
    constexpr Bits(
        const char(&str)[N + 1],
        char zero = '0',
        char one = '1'
    ) : buffer{} {
        constexpr size_type M = N - 1;

        for (size_type i = 0; i < N; ++i) {
            if (str[i] == zero) {
                // do nothing - the bit is already zero
            } else if (str[i] == one) {
                buffer[(M - i) / word_size] |= (word(1) << ((M - i) % word_size));
            } else {
                throw ValueError(
                    "string must contain only '" + std::string(1, zero) +
                    "' and '" + std::string(1, one) + "', not: '" + str[i] +
                    "' at index " + std::to_string(i)
                );
            }
        }
    }

    /// TODO: maybe rather than erroring in the case of a leftover substring, I just
    /// return it as a std::pair<Bits, std::string_view>?  Maybe it's better if
    /// I do that as a mutable out parameter, and in that case, I can also break early
    /// if an unparsed substring is found, which means the only error case is if the
    /// bitset is too small to represent the indicated value.  If the out parameter
    /// isn't provided, and a continuation is found, then I return a `ValueError` state.
    /// If the out parameter is provided, then the only error state is the
    /// `OverflowError`.


    /// TODO: what I should do is just extract digit characters until I encounter a
    /// non-digit or the bitset overflows, whichever comes first.  That allows me to
    /// ignore leading zeros and trailing, non-digit characters.



    /// TODO: from_string() and to_string() should potentially also be able to work
    /// with signed integers?  Basically, I add some special handling for the
    /// very first character, which may be a sign bit.  If it is, then I can just
    /// parse the rest of the string as an unsigned integer, and then take the
    /// two's complement at the end.


    /* Decode a bitset from a string representation.  Defaults to base 2 with the given
    zero and one digit strings, which are provided as template parameters.  The total
    number of digits dictates the base for the conversion, which must be at least 2 and
    at most 64.  Returns an `Expected<Bits, ValueError>`, where a `ValueError` state
    indicates that the string contains invalid characters that could not be parsed as
    digits in the given base, or if extra digits exist that do not fit in the bitset.

    Note that the string will be parsed as if it were big-endian, meaning the rightmost
    digit corresponds to the least significant bits, and digits to the left count
    upwards in significance. */
    template <static_str zero = "0", static_str one = "1", static_str... rest>
        requires (
            sizeof...(rest) + 2 <= 64 &&
            !zero.empty() && (!one.empty() && ... && !rest.empty()) &&
            meta::perfectly_hashable<zero, one, rest...>
        )
    [[nodiscard]] static constexpr auto from_string(std::string_view str) noexcept
        -> Expected<Bits, ValueError, OverflowError>
    {
        // if the final bitset is empty, decoding is trivial
        if constexpr (N == 0) {
            if (!str.empty()) {
                if consteval {
                    return ValueError("leftover substring");
                } else {
                    return ValueError(
                        "leftover substring: '" + std::string(str) + "'"
                    );
                }
            }
            return Bits{};

        } else {
            static constexpr size_type base = sizeof...(rest) + 2;
            Bits result;
            size_type idx = 0;

            // special case for base 2, which devolves to a simple bitscan
            if constexpr (base == 2) {
                while (idx < str.size()) {
                    // if the most significant bit is set, then we have an overflow
                    if (
                        result.buffer[array_size - 1] &
                        (word(1) << ((N % word_size) - 1))
                    ) {
                        if consteval {
                            return OverflowError();
                        } else {
                            return OverflowError();
                        }
                    }
                    if (str.substr(idx, zero.size()) == zero) {
                        result <<= word(1);
                        idx += zero.size();
                    } else if (str.substr(idx, one.size()) == one) {
                        result <<= word(1);
                        result |= word(1);
                        idx += one.size();
                    } else {
                        if consteval {
                            static constexpr static_str message =
                                "string must contain only '" + zero + "' and '" +
                                one + "'";
                            return ValueError(message);
                        } else {
                            return ValueError(
                                "string must contain only '" + zero + "' and '" + one +
                                "', not: '" + std::string(str.substr(
                                    idx,
                                    max(zero.size(), one.size())
                                )) + "' at index " + std::to_string(idx)
                            );
                        }
                    }
                }

            // otherwise, we search against a minimal perfect hash table to get the
            // corresponding digit
            } else {
                static constexpr size_type bits_per_digit = std::bit_width(base - 1);
                static constexpr auto digits =
                    digit_map<zero, one, rest...>(std::make_index_sequence<base>{});
                static constexpr auto widths = impl::unique_digit_lengths<
                    base,
                    unique_digit_lengths<zero.size(), one.size(), rest.size()...>()
                >{}();

                // loop until the string is consumed or the bitset overflows
                while (idx < str.size()) {
                    // attempt to extract a digit from the string, testing string
                    // segments beginning at index `i` in order of decreasing width
                    Expected<void, ValueError, OverflowError> status = from_string_helper<
                        0,
                        digits,
                        widths,
                        bits_per_digit
                    >(result, str, idx);
                    if (status.has_error()) {
                        return std::move(status.error());
                    }
                }
            }

            // if there are any leftover characters, return an error state
            if (idx < str.size()) {
                if consteval {
                    return ValueError("leftover substring");
                } else {
                    if (str.size() - idx > 128) {
                        return ValueError(
                            "leftover substring: '" + std::string(
                                str.substr(idx, idx + 128)
                            ) + " (...)'"
                        );
                    } else {
                        return ValueError(
                            "leftover substring: '" + std::string(
                                str.substr(idx)
                            ) + "'"
                        );
                    }
                }
            }
            return result;
        }
    }

    /* A shorthand for `from_string<"0", "1">(str)`, which decodes a string in the
    canonical binary representation. */
    [[nodiscard]] static constexpr auto from_binary(std::string_view str) noexcept
        -> Expected<Bits, ValueError, OverflowError>
    {
        return from_string<"0", "1">(str);
    }

    /* A shorthand for `from_string<"0", "1", "2", "3", "4", "5", "6", "7">(str)`,
    which decodes a string in the canonical octal representation. */
    [[nodiscard]] static constexpr auto from_octal(std::string_view str) noexcept
        -> Expected<Bits, ValueError, OverflowError>
    {
        return from_string<"0", "1", "2", "3", "4", "5", "6", "7">(str);
    }

    /* A shorthand for
    `from_string<"0", "1", "2", "3", "4", "5", "6", "7", "8", "9">(str)`, which decodes
    a string in the canonical decimal representation. */
    [[nodiscard]] static constexpr auto from_decimal(std::string_view str) noexcept
        -> Expected<Bits, ValueError, OverflowError>
    {
        return from_string<"0", "1", "2", "3", "4", "5", "6", "7", "8", "9">(str);
    }

    /* A shorthand for
    `from_string<"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "A", "B", "C", "D", "E", "F">(str)`,
    which decodes a string in the canonical hexadecimal representation. */
    [[nodiscard]] static constexpr auto from_hex(std::string_view str) noexcept
        -> Expected<Bits, ValueError, OverflowError>
    {
        return from_string<
            "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
            "A", "B", "C", "D", "E", "F"
        >(str);
    }

    /// TODO: to_string should be able to produce signed integer representations by
    /// using two's complement?

    /* Encode the bitset into a string representation.  Defaults to base 2 with the
    given zero and one characters, which are given as template parameters.  The total
    number of substrings dictates the base for the conversion, which must be at least
    2 and at most 64.  The result is always padded to the exact width needed to
    represent the bitset in the chosen base, including leading zeroes if needed.  Note
    that the resulting string is always big-endian, meaning the first substring
    corresponds to the most significant digit in the bitset, and the last substring
    corresponds to the least significant digit.  If the base is 2, then the result can
    be passed back to the `Bits` constructor to recover the original state.  Fails to
    compile if any of the substrings are empty, or if there are duplicates. */
    template <static_str zero = "0", static_str one = "1", static_str... rest>
        requires (
            sizeof...(rest) + 2 <= 64 &&
            !zero.empty() && !one.empty() && (... && !rest.empty()) &&
            meta::strings_are_unique<zero, one, rest...>
        )
    [[nodiscard]] constexpr std::string to_string() const noexcept {
        // # of digits needed to represent the value in `base = sizeof...(Strs)` is
        // ceil(N / log2(base))
        static constexpr size_type base = sizeof...(rest) + 2;
        static constexpr double len = N / impl::log2_table[base];
        static constexpr size_type ceil = size_type(len) + (size_type(len) < len);

        // generate a lookup table of substrings to use for each digit
        static constexpr std::array<std::string_view, base> digits {zero, one, rest...};
        static constexpr Bits divisor = word(base);

        // if the base is larger than the representable range of the bitset, then we
        // can avoid division entirely.
        /// TODO: the second condition here may not compile, since the compiler will
        /// attempt to eagerly evaluate it and trip a UB filter.
        if constexpr (N <= word_size && base >= (1ULL << N)) {
            return std::string(digits[word(*this)]);

        // if the base is exactly 2, then we can use a simple bitscan to determine the
        // final return string, rather than doing multi-word division
        } else if (base == 2) {
            size_type size = 0;
            auto contents = [this]<size_t... Js>(
                std::index_sequence<Js...>,
                size_type& size
            ) {
                return std::array<std::string_view, ceil>{
                    to_string_helper<Js>(digits, size)...
                };
            }(std::make_index_sequence<ceil>{}, size);

            // join the substrings in reverse order to create the final result
            std::string result;
            result.reserve(size);
            for (size_type i = ceil; i-- > 0;) {
                result.append(contents[i]);
            }
            return result;

        // otherwise, use modular division to calculate all the substrings needed to
        // represent the value, from the least significant digit to most significant.
        } else {
            Bits quotient = *this;
            Bits remainder;
            size_type size = 0;
            auto contents = []<size_t... Js>(
                std::index_sequence<Js...>,
                Bits& quotient,
                Bits& remainder,
                size_type& size
            ) {
                return std::array<std::string_view, ceil>{
                    to_string_helper<Js, divisor>(digits, quotient, remainder, size)...
                };
            }(std::make_index_sequence<ceil>{}, quotient, remainder, size);

            // join the substrings in reverse order to create the final result
            std::string result;
            result.reserve(size);
            for (size_type i = ceil; i-- > 0;) {
                result.append(contents[i]);
            }
            return result;
        }
    }

    /* A shorthand for `to_string<"0", "1">()`, which yields a string in the canonical
    binary representation. */
    [[nodiscard]] constexpr std::string to_binary() const noexcept {
        return operator std::string();
    }

    /* A shorthand for `to_string<"0", "1", "2", "3", "4", "5", "6", "7">()`, which
    yields a string in the canonical octal representation. */
    [[nodiscard]] constexpr std::string to_octal() const noexcept {
        return to_string<"0", "1", "2", "3", "4", "5", "6", "7">();
    }

    /* A shorthand for `to_string<"0", "1", "2", "3", "4", "5", "6", "7", "8", "9">()`,
    which yields a string in the canonical decimal representation. */
    [[nodiscard]] constexpr std::string to_decimal() const noexcept {
        return to_string<"0", "1", "2", "3", "4", "5", "6", "7", "8", "9">();
    }

    /* A shorthand for
    `to_string<"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "A", "B", "C", "D", "E", "F">()`,
    which yields a string in the canonical hexadecimal representation. */
    [[nodiscard]] constexpr std::string to_hex() const noexcept {
        return to_string<
            "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
            "A", "B", "C", "D", "E", "F"
        >();
    }

    /* Convert the bitset to a string representation with '1' as the true character and
    '0' as the false character.  Note that the string is returned in big-endian order,
    meaning the first character corresponds to the most significant bit in the bitset,
    and the last character corresponds to the least significant bit.  The string will
    be zero-padded to the exact width of the bitset, and can be passed to
    `Bits::from_string()` to recover the original state. */
    [[nodiscard]] explicit constexpr operator std::string() const noexcept {
        static constexpr size_type M = N - 1;
        static constexpr int diff = '1' - '0';
        std::string result;
        result.reserve(N);
        for (size_type i = 0; i < N; ++i) {
            bool bit = buffer[(M - i) / word_size] & (word(1) << ((M - i) % word_size));
            result.push_back('0' + diff * bit);
        }
        return result;
    }

    /* Bitsets evalute true if any of their bits are set. */
    [[nodiscard]] explicit constexpr operator bool() const noexcept {
        return any();
    }

    /* Convert the bitset to an integer representation if it fits within a single
    word. */
    template <typename T>
        requires (
            array_size == 1 &&
            !impl::exact_bits<T> &&
            meta::explicitly_convertible_to<word, T>
        )
    [[nodiscard]] explicit constexpr operator T() const noexcept {
        if constexpr (array_size == 0) {
            return static_cast<T>(word(0));
        } else {
            return static_cast<T>(buffer[0]);
        }
    }

    /* Get the underlying array. */
    [[nodiscard]] constexpr auto& data() noexcept { return buffer; }
    [[nodiscard]] constexpr const auto& data() const noexcept { return buffer; }
    [[nodiscard]] constexpr iterator begin() noexcept { return {this, 0}; }
    [[nodiscard]] constexpr const_iterator begin() const noexcept { return {this, 0}; }
    [[nodiscard]] constexpr const_iterator cbegin() const noexcept { return {this, 0}; }
    [[nodiscard]] constexpr iterator end() noexcept { return {this, ssize()}; }
    [[nodiscard]] constexpr const_iterator end() const noexcept { return {this, ssize()}; }
    [[nodiscard]] constexpr const_iterator cend() const noexcept { return {this, ssize()}; }
    [[nodiscard]] constexpr reverse_iterator rbegin() noexcept { return {end()}; }
    [[nodiscard]] constexpr const_reverse_iterator rbegin() const noexcept { return {end()}; }
    [[nodiscard]] constexpr const_reverse_iterator crbegin() const noexcept { return {cend()}; }
    [[nodiscard]] constexpr reverse_iterator rend() noexcept { return {begin()}; }
    [[nodiscard]] constexpr const_reverse_iterator rend() const noexcept { return {begin()}; }
    [[nodiscard]] constexpr const_reverse_iterator crend() const noexcept { return {cbegin()}; }

    /* Return a range over the one-hot masks that make up the bitset within a given
    interval.  Iterating over the range yields bitsets of the same size as the input
    with only one active bit and zero everywhere else.  Summing the masks yields an
    exact copy of the input bitset within the interval.  The range may be empty if no
    bits are set within the interval. */
    [[nodiscard]] constexpr one_hot components(
        index_type start = 0,
        index_type stop = ssize()
    ) const noexcept {
        index_type norm_start = start + ssize() * (start < 0);
        if (norm_start < 0) {
            norm_start = 0;
        }
        index_type norm_stop = stop + ssize() * (stop < 0);
        if (norm_stop > ssize()) {
            norm_stop = ssize();
        }
        return {this, norm_start, norm_stop};
    }

    /* Return an iterator to a specific bit in the set.  Applies Python-style
    wraparound to the index, and returns an end iterator if the index is out of bounds
    after normalizing. */
    [[nodiscard]] constexpr iterator at(index_type index) noexcept {
        index_type i = index + ssize() * (index < 0);
        if (i < 0 || i >= ssize()) {
            return end();
        }
        return {this, i};
    }
    [[nodiscard]] constexpr const_iterator at(index_type index) const noexcept {
        index_type i = index + ssize() * (index < 0);
        if (i < 0 || i >= ssize()) {
            return end();  // Return end iterator if out of bounds
        }
        return {this, i};
    }

    /* Get the value of a specific bit in the set, where the bit index is known at
    compile time.  Does not apply Python-style wraparound, and fails to compile if the
    index is out of bounds.  This is a lower-level access than the `[]` operator, and
    may be faster in hot loops.  It is also available as `std::get<I>(Bits)`, which
    allows the bitset to be unpacked via structured bindings. */
    template <index_type I> requires (impl::valid_index<ssize(), I>)
    [[nodiscard]] constexpr reference get() noexcept {
        static constexpr index_type J = impl::normalize_index<ssize(), I>();
        return {&buffer[J / word_size], word(J % word_size)};
    }
    template <index_type I> requires (impl::valid_index<ssize(), I>)
    [[nodiscard]] constexpr bool get() const noexcept {
        static constexpr index_type J = impl::normalize_index<ssize(), I>();
        return buffer[J / word_size] & (word(1) << (J % word_size));
    }

    /* Get the value of a specific bit in the set.  Applies Python-style wraparound to
    the index, and throws an `IndexError` if the program is compiled in debug mode and
    the index is out of bounds after normalizing. */
    [[nodiscard]] constexpr reference operator[](index_type i) noexcept(!DEBUG) {
        index_type j = impl::normalize_index(ssize(), i);
        return reference{&buffer[j / word_size], word(j % word_size)};
    }
    [[nodiscard]] constexpr bool operator[](index_type i) const noexcept(!DEBUG) {
        index_type j = impl::normalize_index(ssize(), i);
        return buffer[j / word_size] & (word(1) << (j % word_size));
    }

    /// TODO: operator[slice{}] returning a range of the bitset.
    /// -> Is it possible to generalize the slice classes to account for the category
    /// of the underlying iterator?  So `Bits` could use a random access
    /// specialization that gets an iterator to the start using `begin() + slice.first`
    /// and an iterator to the end using `begin() + slice.last`, and then jump by
    /// `step` at every iteration.

    /* Check if any of the bits are set. */
    [[nodiscard]] constexpr bool any() const noexcept {
        for (size_type i = 0; i < array_size; ++i) {
            if (buffer[i]) {
                return true;
            }
        }
        return false;
    }

    /* Check if any of the bits are set within a particular interval. */
    [[nodiscard]] constexpr bool any(
        index_type start,
        index_type stop = ssize()
    ) const noexcept {
        index_type norm_start = start + ssize() * (start < 0);
        if (norm_start < 0) {
            norm_start = 0;
        }
        if (norm_start >= ssize()) {
            return false;
        }
        index_type norm_stop = stop + ssize() * (stop < 0);
        if (norm_stop < 0) {
            return false;
        }
        if (norm_stop >= ssize()) {
            norm_stop = ssize();
        }
        Bits temp;
        temp.fill(1);
        temp <<= N - size_type(norm_stop);
        temp >>= N - size_type(norm_stop - norm_start);
        temp <<= size_type(norm_start);  // [start, stop).
        return bool(*this & temp);
    }

    /* Check if all of the bits are set. */
    [[nodiscard]] constexpr bool all() const noexcept {
        for (size_type i = 0; i < array_size - (end_mask > 0); ++i) {
            if (buffer[i] != std::numeric_limits<word>::max()) {
                return false;
            }
        }
        if constexpr (end_mask) {
            return buffer[array_size - 1] == end_mask;
        } else {
            return true;
        }
    }

    /* Check if all of the bits are set within a particular interval. */
    [[nodiscard]] constexpr bool all(
        index_type start,
        index_type stop = ssize()
    ) const noexcept {
        index_type norm_start = start + ssize() * (start < 0);
        if (norm_start < 0) {
            norm_start = 0;
        }
        if (norm_start >= ssize()) {
            return false;
        }
        index_type norm_stop = stop + ssize() * (stop < 0);
        if (norm_stop < 0) {
            return false;
        }
        if (norm_stop >= ssize()) {
            norm_stop = ssize();
        }
        Bits temp;
        temp.fill(1);
        temp <<= N - size_type(norm_stop);
        temp >>= N - size_type(norm_stop - norm_start);
        temp <<= size_type(norm_start);  // [start, stop).
        return (*this & temp) == temp;
    }

    /* Get the number of bits that are currently set. */
    [[nodiscard]] constexpr size_type count() const noexcept {
        size_type count = 0;
        for (size_type i = 0; i < array_size; ++i) {
            count += std::popcount(buffer[i]);
        }
        return count;
    }

    /* Get the number of bits that are currently set within a particular interval. */
    [[nodiscard]] constexpr size_type count(
        index_type start,
        index_type stop = ssize()
    ) const noexcept {
        index_type norm_start = start + ssize() * (start < 0);
        if (norm_start < 0) {
            norm_start = 0;
        }
        if (norm_start >= ssize()) {
            return 0;
        }
        index_type norm_stop = stop + ssize() * (stop < 0);
        if (norm_stop < 0) {
            return 0;
        }
        if (norm_stop >= ssize()) {
            norm_stop = ssize();
        }
        Bits temp;
        temp.fill(1);
        temp <<= N - size_type(norm_stop);
        temp >>= N - size_type(norm_stop - norm_start);
        temp <<= size_type(norm_start);  // [start, stop).
        size_type count = 0;
        for (size_type i = 0; i < array_size; ++i) {
            count += size_type(std::popcount(word(buffer[i] & temp.buffer[i])));
        }
        return count;
    }

    /* Set all of the bits to the given value. */
    constexpr Bits& fill(bool value) noexcept {
        word filled = std::numeric_limits<word>::max() * value;
        for (size_type i = 0; i < array_size - (end_mask > 0); ++i) {
            buffer[i] = filled;
        }
        if constexpr (end_mask) {
            buffer[array_size - 1] = filled & end_mask;
        }
        return *this;
    }

    /* Set all of the bits within a certain interval to the given value. */
    constexpr Bits& fill(
        bool value,
        index_type start,
        index_type stop = ssize()
    ) noexcept {
        index_type norm_start = start + ssize() * (start < 0);
        if (norm_start < 0) {
            norm_start = 0;
        }
        if (norm_start >= ssize()) {
            return *this;
        }
        index_type norm_stop = stop + ssize() * (stop < 0);
        if (norm_stop < 0) {
            return *this;
        }
        if (norm_stop >= ssize()) {
            norm_stop = ssize();
        }
        Bits temp;
        temp.fill(1);
        temp <<= N - size_type(norm_stop);
        temp >>= N - size_type(norm_stop - norm_start);
        temp <<= size_type(norm_start);  // [start, stop).
        if (value) {
            *this |= temp;
        } else {
            *this &= ~temp;
        }
        return *this;
    }

    /* Toggle all of the bits in the set. */
    constexpr Bits& flip() noexcept {
        for (size_type i = 0; i < array_size - (end_mask > 0); ++i) {
            buffer[i] ^= std::numeric_limits<word>::max();
        }
        if constexpr (end_mask) {
            buffer[array_size - 1] ^= end_mask;
        }
        return *this;
    }

    /* Toggle all of the bits within a certain interval. */
    constexpr Bits& flip(index_type start, index_type stop = ssize()) noexcept {
        index_type norm_start = start + ssize() * (start < 0);
        if (norm_start < 0) {
            norm_start = 0;
        }
        if (norm_start >= ssize()) {
            return *this;
        }
        index_type norm_stop = stop + ssize() * (stop < 0);
        if (norm_stop < 0) {
            return *this;
        }
        if (norm_stop >= ssize()) {
            norm_stop = ssize();
        }
        Bits temp;
        temp.fill(1);
        temp <<= N - size_type(norm_stop);
        temp >>= N - size_type(norm_stop - norm_start);
        temp <<= size_type(norm_start);  // [start, stop).
        *this ^= temp;
        return *this;
    }

    /* Reverse the order of all bits in the set.  This effectively converts from
    big-endian to little-endian or vice versa. */
    constexpr Bits& reverse() noexcept {
        // swap the words in the array, and then reverse the bits within each word
        for (size_type i = 0; i < array_size / 2; ++i) {
            word temp = buffer[i];
            buffer[i] = impl::bit_reverse(buffer[array_size - 1 - i]);
            buffer[array_size - 1 - i] = impl::bit_reverse(temp);
        }

        // account for the middle word in an odd-sized array
        if constexpr (array_size % 2) {
            buffer[array_size / 2] = impl::bit_reverse(buffer[array_size / 2]);
        }

        // if there are any upper bits that should be masked off, shift down to align
        // the bits in the view window
        if constexpr (N % word_size) {
            *this >>= (word_size - N % word_size);
        }
        return *this;
    }

    /* Reverse the order of the bits within a certain interval. */
    constexpr Bits& reverse(index_type start, index_type stop = ssize()) noexcept {
        index_type norm_start = start + ssize() * (start < 0);
        if (norm_start < 0) {
            norm_start = 0;
        }
        if (norm_start >= ssize()) {
            return *this;
        }
        index_type norm_stop = stop + ssize() * (stop < 0);
        if (norm_stop < 0) {
            return *this;
        }
        if (norm_stop >= ssize()) {
            norm_stop = ssize();
        }
        Bits temp;
        temp.fill(1);
        temp <<= N - size_type(norm_stop);
        temp >>= N - size_type(norm_stop - norm_start);
        temp <<= size_type(norm_start);  // [start, stop).
        Bits reversed = *this & temp;
        reversed.reverse();
        *this &= ~temp;  // clear the bits in the original bitset
        *this |= reversed;  // set the reversed bits back into the original bitset
        return *this;
    }

    /// TODO: Although two's complement ensures the actual arithmetic is correct, all
    /// the overflow detection works differently for signed vs unsigned integers, since
    /// the overflow point is at `0111111... + 1` or `100000... - 1`, rather than the
    /// inverse, so that gets somewhat complicated  I would basically need some way of
    /// knowing whether the operation is being done in signed vs unsigned mode, and
    /// disregard the upper bit in signed mode.  The rest of the overflow detection
    /// works correctly with respect to the lower N - 1 bits, but it's that extra bit
    /// that causes problems.  If I could address that, then maybe the unified
    /// signed/unsigned representation could work.  Alternatively, I could just not
    /// allow bitsets to be used as signed integers, although that would be very nice
    /// as a core abstraction for `N`-bit arithmetic.

    /* Convert the value to its two's complement equivalent.  This is equivalent to
    flipping the sign for a signed integral value. */
    constexpr Bits& complement() noexcept {
        flip();
        ++*this;
        return *this;
    }

    /// TODO: maybe `first_one()` and `last_one()` can be reduced to `msb()` and
    /// `lsb()`, and I can delete the `first_zero()` and `last_zero()` methods?

    /* Return the index of the first bit that is set, or an empty optional if no bits
    are set. */
    [[nodiscard]] constexpr Optional<index_type> first_one() const noexcept {
        for (size_type i = 0; i < array_size; ++i) {
            size_type j = size_type(std::countr_zero(buffer[i]));
            if (j < word_size) {
                return index_type(word_size * i  + j);
            }
        }
        return std::nullopt;
    }

    /* Return the index of the first bit that is set within a given interval, or an
    empty optional if no bits are set. */
    [[nodiscard]] constexpr Optional<index_type> first_one(
        index_type start,
        index_type stop = ssize()
    ) const noexcept {
        index_type norm_start = start + ssize() * (start < 0);
        if (norm_start < 0) {
            norm_start = 0;
        }
        if (norm_start >= ssize()) {
            return std::nullopt;
        }
        index_type norm_stop = stop + ssize() * (stop < 0);
        if (norm_stop < 0) {
            return std::nullopt;
        }
        if (norm_stop >= ssize()) {
            norm_stop = ssize();
        }
        Bits temp;
        temp.fill(1);
        temp <<= N - size_type(norm_stop);
        temp >>= N - size_type(norm_stop - norm_start);
        temp <<= size_type(norm_start);  // [start, stop).
        for (size_type i = 0; i < array_size; ++i) {
            size_type j = size_type(std::countr_zero(
                word(buffer[i] & temp.buffer[i])
            ));
            if (j < word_size) {
                return index_type(word_size * i + j);
            }
        }
        return std::nullopt;
    }

    /* Return the index of the last bit that is set, or an empty optional if no bits
    are set. */
    [[nodiscard]] constexpr Optional<index_type> last_one() const noexcept {
        for (size_type i = array_size; i-- > 0;) {
            size_type j = size_type(std::countl_zero(buffer[i]));
            if (j < word_size) {
                return index_type(word_size * i + word_size - 1 - j);
            }
        }
        return std::nullopt;
    }

    /* Return the index of the last bit that is set within a given interval, or an
    empty optional if no bits are set. */
    [[nodiscard]] constexpr Optional<index_type> last_one(
        index_type start,
        index_type stop = ssize()
    ) const noexcept {
        index_type norm_start = start + ssize() * (start < 0);
        if (norm_start < 0) {
            norm_start = 0;
        }
        if (norm_start >= ssize()) {
            return std::nullopt;
        }
        index_type norm_stop = stop + ssize() * (stop < 0);
        if (norm_stop < 0) {
            return std::nullopt;
        }
        if (norm_stop >= ssize()) {
            norm_stop = ssize();
        }
        Bits temp;
        temp.fill(1);
        temp <<= N - size_type(norm_stop);
        temp >>= N - size_type(norm_stop - norm_start);
        temp <<= size_type(norm_start);  // [start, stop).
        for (size_type i = array_size; i-- > 0;) {
            size_type j = size_type(std::countl_zero(
                word(buffer[i] & temp.buffer[i])
            ));
            if (j < word_size) {
                return index_type(word_size * i + word_size - 1 - j);
            }
        }
        return std::nullopt;
    }

    /* Return the index of the first bit that is not set, or an empty optional if all
    bits are set. */
    [[nodiscard]] constexpr Optional<index_type> first_zero() const noexcept {
        for (size_type i = 0; i < array_size; ++i) {
            size_type j = size_type(std::countr_one(buffer[i]));
            if (j < word_size) {
                return index_type(word_size * i + j);
            }
        }
        return std::nullopt;
    }

    /* Return the index of the first bit that is not set within a given interval, or
    an empty optional if all bits are set. */
    [[nodiscard]] constexpr Optional<index_type> first_zero(
        index_type start,
        index_type stop = ssize()
    ) const noexcept {
        index_type norm_start = start + ssize() * (start < 0);
        if (norm_start < 0) {
            norm_start = 0;
        }
        if (norm_start >= ssize()) {
            return std::nullopt;
        }
        index_type norm_stop = stop + ssize() * (stop < 0);
        if (norm_stop < 0) {
            return std::nullopt;
        }
        if (norm_stop >= ssize()) {
            norm_stop = ssize();
        }
        Bits temp;
        temp.fill(1);
        temp <<= N - size_type(norm_stop);
        temp >>= N - size_type(norm_stop - norm_start);
        temp <<= size_type(norm_start);  // [start, stop).
        for (size_type i = 0; i < array_size; ++i) {
            size_type j = size_type(std::countr_one(
                word(buffer[i] & temp.buffer[i])
            ));
            if (j < word_size) {
                return index_type(word_size * i + j);
            }
        }
        return std::nullopt;
    }

    /* Return the index of the last bit that is not set, or an empty optional if all
    bits are set. */
    [[nodiscard]] constexpr Optional<index_type> last_zero() const noexcept {
        if constexpr (end_mask) {
            size_type j = size_type(std::countl_one(
                word(buffer[array_size - 1] | ~end_mask)
            ));
            if (j < word_size) {
                return index_type(word_size * (array_size - 1) + word_size - 1 - j);
            }
        }
        for (size_type i = array_size - (end_mask > 0); i-- > 0;) {
            size_type j = size_type(std::countl_one(buffer[i]));
            if (j < word_size) {
                return index_type(word_size * i + word_size - 1 - j);
            }
        }
        return std::nullopt;
    }

    /* Return the index of the last bit that is not set within a given interval, or an
    empty optional if all bits are set. */
    [[nodiscard]] constexpr Optional<index_type> last_zero(
        index_type start,
        index_type stop = ssize()
    ) const noexcept {
        index_type norm_start = start + ssize() * (start < 0);
        if (norm_start < 0) {
            norm_start = 0;
        }
        if (norm_start >= ssize()) {
            return std::nullopt;
        }
        index_type norm_stop = stop + ssize() * (stop < 0);
        if (norm_stop < 0) {
            return std::nullopt;
        }
        if (norm_stop >= ssize()) {
            norm_stop = ssize();
        }
        Bits temp;
        temp.fill(1);
        temp <<= N - size_type(norm_stop);
        temp >>= N - size_type(norm_stop - norm_start);
        temp <<= size_type(norm_start);  // [start, stop).
        for (size_type i = array_size; i-- > 0;) {
            size_type j = size_type(std::countl_one(
                word(buffer[i] | ~temp.buffer[i])
            ));
            if (j < word_size) {
                return index_type(word_size * i + word_size - 1 - j);
            }
        }
        return std::nullopt;
    }

    /* Add two bitsets of equal size.  If the result overflows, then the `overflow`
    flag will be set to true, and the value will wrap around to the other end of the
    number line (modulo `N`). */
    [[nodiscard]] constexpr Bits add(
        const Bits& other,
        bool& overflow
    ) const noexcept {
        Bits result;
        add(other, overflow, result);
        return result;
    }

    /* Add two bitsets of equal size and store the sum as a mutable out parameter.  If
    the result overflows, then the `overflow` flag will be set to true, and the value
    will wrap around to the other end of the number line (modulo `N`).  The out
    parameter may be a reference to either operand, without affecting the overall
    calculation. */
    constexpr void add(
        const Bits& other,
        bool& overflow,
        Bits& out
    ) const noexcept {
        overflow = false;

        // promote to a larger word size if possible
        if constexpr (!big_word::composite) {
            using big = big_word::type;
            big s = big(buffer[0]) + big(other.buffer[0]);
            if constexpr (end_mask) {
                overflow = (s >> (N % word_size)) != 0;
                out.buffer[0] = word(s & end_mask);
            } else {
                overflow = (s >> word_size) != 0;
                out.buffer[0] = word(s);
            }

        // revert to schoolbook addition
        } else {
            for (size_type i = 0; i < array_size - (end_mask > 0); ++i) {
                word a = buffer[i] + overflow;
                overflow = a < buffer[i];
                word b = a + other.buffer[i];
                overflow |= b < a;
                out.buffer[i] = b;
            }
            if constexpr (end_mask) {
                word s = buffer[array_size - 1] + overflow + other.buffer[array_size - 1];
                overflow = (s >> (N % word_size)) != 0;
                out.buffer[array_size - 1] = s & end_mask;
            }
        }
    }

    /* Subtract two bitsets of equal size.  If the result overflows, then the
    `overflow` flag will be set to true, and the value will wrap around to the other
    end of the number line (modulo `N`). */
    [[nodiscard]] constexpr Bits sub(
        const Bits& other,
        bool& overflow
    ) const noexcept {
        Bits result;
        sub(other, overflow, result);
        return result;
    }

    /* Subtract two bitsets of equal size and store the difference as a mutable out
    parameter.  If the result overflows, then the `overflow` flag will be set to true,
    and the value will wrap around to the other end of the number line (modulo `N`).
    The out parameter may be a reference to either operand, without affecting the
    overall calculation. */
    constexpr void sub(
        const Bits& other,
        bool& overflow,
        Bits& out
    ) const noexcept {
        overflow = false;

        // schoolbook subtraction
        for (size_type i = 0; i < array_size - (end_mask > 0); ++i) {
            word a = buffer[i] - overflow;
            overflow = a > buffer[i];
            word b = a - other.buffer[i];
            overflow |= b > a;
            out.buffer[i] = b;
        }
        if constexpr (end_mask) {
            word d = buffer[array_size - 1] - overflow - other.buffer[array_size - 1];
            overflow = d > buffer[array_size - 1];
            out.buffer[array_size - 1] = d & end_mask;
        }
    }

    /* Multiply two bitsets of equal size.  If the result overflows, then the
    `overflow` flag will be set to true, and the value will wrap around to the other
    end of the number line (modulo `N`).

    Note that this uses simple schoolbook multiplication under the hood, which is
    generally optimized for small bit counts (up to a few thousand bits).  For larger
    bit counts, it may be more efficient to use a specialized library such as GMP or
    `boost::multiprecision`. */
    [[nodiscard]] constexpr Bits mul(
        const Bits& other,
        bool& overflow
    ) const noexcept {
        Bits result;
        mul(other, overflow, result);
        return result;
    }

    /* Multiply two bitsets of equal size and store the product as a mutable out
    parameter.  If the result overflows, then the `overflow` flag will be set to true,
    and the value will wrap around to the other end of the number line (modulo `N`).
    The out parameter may be a reference to either operand, without affecting the
    overall calculation.

    Note that this uses simple schoolbook multiplication under the hood, which is
    generally optimized for small bit counts (up to a few thousand bits).  For larger
    bit counts, it may be more efficient to use a specialized library such as GMP or
    `boost::multiprecision`. */
    constexpr void mul(
        const Bits& other,
        bool& overflow,
        Bits& out
    ) const noexcept {
        overflow = false;

        // promote to a larger word size if possible
        if constexpr (!big_word::composite) {
            using big = big_word::type;
            static constexpr big mask = ~word(0);
            big p = big(buffer[0]) * big(other.buffer[0]);
            if constexpr (end_mask) {
                overflow = (p >> (N % word_size)) != 0;
                out.buffer[0] = word(p & end_mask);
            } else {
                overflow = (p >> word_size) != 0;
                out.buffer[0] = word(p);
            }

        // revert to schoolbook multiplication
        } else {
            // compute 2N-bit full product
            std::array<word, array_size * 2> temp {};
            for (size_type i = 0; i < array_size; ++i) {
                word carry = 0;
                for (size_type j = 0; j < array_size; ++j) {
                    size_type k = j + i;
                    big_word p = big_word::mul(buffer[i], other.buffer[j]);

                    word sum = p.lo() + temp[k];
                    word new_carry = sum < temp[k];
                    sum += carry;
                    new_carry |= sum < carry;
                    carry = p.hi() + new_carry;  // high half -> carry to next word
                    temp[k] = sum;  // low half -> final digit in this column
                }
                temp[i + array_size] += carry;  // last carry of the row
            }

            // low N bits become the final result
            std::copy_n(temp.begin(), array_size, out.buffer.begin());
            if constexpr (end_mask) {
                overflow = (temp[array_size - 1] & ~end_mask) != 0;
                out.buffer[array_size - 1] &= end_mask;
                if (overflow) {
                    return;  // skip following overflow check
                }
            }

            // if any of the high bits are set, then overflow has occurred
            for (size_type i = array_size; i < array_size * 2; ++i) {
                if (temp[i]) {
                    overflow = true;
                    break;
                }
            }
        }
    }

    /* Divide two bitsets of equal size, returning both the quotient and remainder.  If
    the divisor is zero and the program is compiled in debug mode, then a
    `ZeroDivisionError` will be thrown.

    Note that this uses Knuth's Algorithm D under the hood, which is generally
    optimized for small bit counts (up to a few thousand bits).  For larger bit counts,
    it may be more efficient to use a specialized library such as GMP or
    `boost::multiprecision`. */
    [[nodiscard]] constexpr std::pair<Bits, Bits> divmod(
        const Bits& other
    ) const {
        Bits quotient, remainder;
        divmod(other, quotient, remainder);
        return {quotient, remainder};
    }

    /* Divide two bitsets of equal size, returning the quotient and storing the
    remainder as a mutable out parameter.  If the divisor is zero and the program is
    compiled in debug mode, then a `ZeroDivisionError` will be thrown.

    Listing either the dividend or divisor as the out parameter for the remainder is
    allowed (but not required), and will update the referenced bitset in-place, without
    affecting the calculation.

    Note that this uses Knuth's Algorithm D under the hood, which is generally
    optimized for small bit counts (up to a few thousand bits).  For larger bit counts,
    it may be more efficient to use a specialized library such as GMP or
    `boost::multiprecision`. */
    [[nodiscard]] constexpr Bits divmod(
        const Bits& other,
        Bits& remainder
    ) const {
        Bits quotient;
        divmod(other, quotient, remainder);
        return quotient;
    }

    /* Divide two bitsets of equal size, storing both the quotient and remainder as
    mutable out parameters.  If the divisor is zero and the program is compiled in
    debug mode, then a `ZeroDivisionError` will be thrown.

    Listing either the dividend or divisor as the out parameter for the quotient or
    remainder is allowed (but not required), and will update the referenced bitset
    in-place, without affecting the calculation.

    Note that this uses Knuth's Algorithm D under the hood, which is generally
    optimized for small bit counts (up to a few thousand bits).  For larger bit counts,
    it may be more efficient to use a specialized library such as GMP or
    `boost::multiprecision`. */
    constexpr void divmod(
        const Bits& other,
        Bits& quotient,
        Bits& remainder
    ) const {
        if constexpr (array_size == 1) {
            word l = buffer[0];
            word r = other.buffer[0];
            quotient.buffer[0] = l / r;
            remainder.buffer[0] = l % r;

        } else {
            using Signed = meta::as_signed<word>;
            constexpr size_type chunk = word_size / 2;
            constexpr word b = word(1) << chunk;

            if (!other) {
                throw ZeroDivisionError();
            }
            if (*this < other) {
                remainder = other;
                quotient.fill(0);
                return;
            }

            // 1. Compute effective lengths.  Above checks ensure that neither operand
            // is zero.
            size_type lhs_last = size();
            for (size_type i = array_size; i-- > 0;) {
                size_type j = size_type(std::countl_zero(buffer[i]));
                if (j < word_size) {
                    lhs_last = size_type(word_size * i + word_size - 1 - j);
                    break;
                }
            }
            size_type rhs_last = size();
            for (size_type i = array_size; i-- > 0;) {
                size_type j = size_type(std::countl_zero(other.buffer[i]));
                if (j < word_size) {
                    rhs_last = size_type(word_size * i + word_size - 1 - j);
                    break;
                }
            }
            size_type n = (rhs_last + (word_size - 1)) / word_size;
            size_type m = ((lhs_last + (word_size - 1)) / word_size) - n;

            // 2. If the divisor is a single word, then we can avoid multi-word division.
            if (n == 1) {
                word v = other.buffer[0];
                word rem = 0;
                for (size_type i = m + n; i-- > 0;) {
                    auto wide = big_word{rem, buffer[i]} / v;
                    quotient.buffer[i] = wide.hi();
                    rem = wide.lo();
                }
                for (size_type i = m + n; i < array_size; ++i) {
                    quotient.buffer[i] = 0;
                }
                remainder.buffer[0] = rem;
                for (size_type i = 1; i < n; ++i) {
                    remainder.buffer[i] = 0;
                }
                return;
            }

            /// NOTE: this is based on Knuth's Algorithm D, which is among the simplest for
            /// bigint division.  Much of the implementation was taken from:
            /// https://skanthak.hier-im-netz.de/division.html
            /// Which references Hacker's Delight, with a helpful explanation of the
            /// algorithm design.  See that or the Knuth reference for more details.
            std::array<word, array_size> v = other.buffer;
            std::array<word, array_size + 1> u;
            for (size_type i = 0; i < array_size; ++i) {
                u[i] = buffer[i];
            }
            u[array_size] = 0;

            // 3. Left shift until the highest set bit in the divisor is at the top of its
            // respective word.  The strange bitwise AND is meant to avoid undefined
            // behavior when shifting by a full word size (i.e. shift == 0).  It is taken
            // from https://ridiculousfish.com/blog/posts/labor-of-division-episode-v.html
            size_type shift = word_size - 1 - (rhs_last % word_size);
            size_type shift_carry = -shift & (word_size - 1);
            size_type shift_correct = -Signed(shift) >> (word_size - 1);
            for (size_type i = array_size + 1; i-- > 1;) {
                u[i] = (u[i] << shift) | ((u[i - 1] >> shift_carry) & shift_correct);
            }
            u[0] <<= shift;
            for (size_type i = array_size; i-- > 1;) {
                v[i] = (v[i] << shift) | ((v[i - 1] >> shift_carry) & shift_correct);
            }
            v[0] <<= shift;

            // 4. Trial division
            quotient.fill(0);
            for (size_type j = m + 1; j-- > 0;) {
                // take the top two words of the numerator for wide division
                auto hat = big_word{u[j + n], u[j + n - 1]} / v[n - 1];
                word qhat = hat.hi();
                word rhat = hat.lo();

                // refine quotient if guess is too large
                while (qhat >= b || (big_word::mul(
                    qhat,
                    v[n - 2]) > big_word{word(rhat * b), u[j + n - 2]}
                )) {
                    --qhat;
                    rhat += v[n - 1];
                    if (rhat >= b) {
                        break;
                    }
                }

                // 5. Multiply and subtract
                word borrow = 0;
                for (size_type i = 0; i < n; ++i) {
                    big_word prod = big_word::mul(qhat, v[i]);
                    word temp = u[i + j] - borrow;
                    borrow = temp > u[i + j];
                    temp -= prod.lo();
                    borrow += temp > prod.lo();
                    u[i + j] = temp;
                    borrow += prod.hi();
                }

                // 6. Correct for negative remainder
                if (u[j + n] < borrow) {
                    --qhat;
                    borrow = 0;
                    for (size_type i = 0; i < n; ++i) {
                        word temp = u[i + j] + borrow;
                        borrow = temp < u[i + j];
                        temp += v[i];
                        borrow += temp < v[i];
                        u[i + j] = temp;
                    }
                    u[j + n] += borrow;
                }
                quotient.buffer[j] = qhat;
            }

            // 7. Unshift the remainder and quotient to get the final result
            for (size_type i = 0; i < n - 1; ++i) {
                remainder.buffer[i] = (u[i] >> shift) | (u[i + 1] << (word_size - shift));
            }
            remainder.buffer[n - 1] = u[n - 1] >> shift;
            for (size_type i = n; i < array_size; ++i) {
                remainder.buffer[i] = 0;
            }
        }
    }

    /* Lexicographically compare two bitsets of equal size. */
    [[nodiscard]] friend constexpr auto operator<=>(
        const Bits& lhs,
        const Bits& rhs
    ) noexcept {
        return std::lexicographical_compare_three_way(
            lhs.buffer.rbegin(),
            lhs.buffer.rend(),
            rhs.buffer.rbegin(),
            rhs.buffer.rend()
        );
    }

    /* Check whether one bitset is lexicographically equal to another of the same
    size. */
    [[nodiscard]] friend constexpr bool operator==(
        const Bits& lhs,
        const Bits& rhs
    ) noexcept {
        for (size_type i = 0; i < array_size; ++i) {
            if (lhs.buffer[i] != rhs.buffer[i]) {
                return false;
            }
        }
        return true;
    }

    /* Apply a bitwise NOT to the contents of the bitset. */
    [[nodiscard]] friend constexpr Bits operator~(const Bits& set) noexcept {
        Bits result;
        for (size_type i = 0; i < array_size - (end_mask > 0); ++i) {
            result.buffer[i] = ~set.buffer[i];
        }
        if constexpr (end_mask) {
            result.buffer[array_size - 1] = ~set.buffer[array_size - 1] & end_mask;
        }
        return result;
    }

    /* Apply a bitwise AND between the contents of two bitsets of equal size. */
    [[nodiscard]] friend constexpr Bits operator&(
        const Bits& lhs,
        const Bits& rhs
    ) noexcept {
        Bits result;
        for (size_type i = 0; i < array_size; ++i) {
            result.buffer[i] = lhs.buffer[i] & rhs.buffer[i];
        }
        return result;
    }

    /* Apply a bitwise AND between the contents of this bitset and another of equal
    length, updating the former in-place. */
    constexpr Bits& operator&=(
        const Bits& other
    ) noexcept {
        for (size_type i = 0; i < array_size; ++i) {
            buffer[i] &= other.buffer[i];
        }
        return *this;
    }

    /* Apply a bitwise OR between the contents of two bitsets of equal size. */
    [[nodiscard]] friend constexpr Bits operator|(
        const Bits& lhs,
        const Bits& rhs
    ) noexcept {
        Bits result;
        for (size_type i = 0; i < array_size; ++i) {
            result.buffer[i] = lhs.buffer[i] | rhs.buffer[i];
        }
        return result;
    }

    /* Apply a bitwise OR between the contents of this bitset and another of equal
    length, updating the former in-place  */
    constexpr Bits& operator|=(
        const Bits& other
    ) noexcept {
        for (size_type i = 0; i < array_size; ++i) {
            buffer[i] |= other.buffer[i];
        }
        return *this;
    }

    /* Apply a bitwise XOR between the contents of two bitsets of equal size. */
    [[nodiscard]] friend constexpr Bits operator^(
        const Bits& lhs,
        const Bits& rhs
    ) noexcept {
        Bits result;
        for (size_type i = 0; i < array_size; ++i) {
            result.buffer[i] = lhs.buffer[i] ^ rhs.buffer[i];
        }
        return result;
    }

    /* Apply a bitwise XOR between the contents of this bitset and another of equal
    length, updating the former in-place. */
    constexpr Bits& operator^=(
        const Bits& other
    ) noexcept {
        for (size_type i = 0; i < array_size; ++i) {
            buffer[i] ^= other.buffer[i];
        }
        return *this;
    }

    /* Apply a bitwise left shift to the contents of the bitset. */
    [[nodiscard]] constexpr Bits operator<<(size_type rhs) const noexcept {
        Bits result;
        size_type whole = rhs / word_size;  // whole words

        // if the shift is larger than the array size, then we can return empty
        if (whole < array_size) {
            if (size_type partial = rhs % word_size) {
                // starting from the most significant word, look ahead by `whole`
                // words, then shift up the lowest `partial` bits of that word, and
                // join the highest `word_size - partial` bits of the next word
                for (size_type i = array_size, end = whole + 1; i-- > end;) {
                    size_type offset = i - whole;
                    result.buffer[i] = (buffer[offset] << partial) |
                        (buffer[offset - 1] >> (word_size - partial));
                }

                // the last word has no next word to join, so we just get the low bits
                result.buffer[whole] = buffer[0] << partial;
            } else {
                for (size_type i = array_size, end = whole + 1; i-- > end;) {
                    size_type offset = i - whole;
                    result.buffer[i] = buffer[offset];
                }
                result.buffer[whole] = buffer[0];
            }
            if constexpr (end_mask) {
                result.buffer[array_size - 1] &= end_mask;
            }
        }

        return result;
    }

    /* Apply a bitwise left shift to the contents of this bitset, updating it
    in-place. */
    constexpr Bits& operator<<=(size_type rhs) noexcept {
        size_type whole = rhs / word_size;

        // if the shift is smaller than the array size, then we have to shift without
        // invalidating the existing contents
        if (whole < array_size) {
            if (size_type partial = rhs % word_size) {
                // starting from the most significant word, look ahead by `whole`
                // words, then shift up the lowest `partial` bits of that word, and
                // join the highest `word_size - partial` bits of the next word
                for (size_type i = array_size, end = whole + 1; i-- > end;) {
                    size_type offset = i - whole;
                    buffer[i] = (buffer[offset] << partial) |
                        (buffer[offset - 1] >> (word_size - partial));
                }

                // the last word has no next word to join, so we just get the low bits,
                // and then clear the rest of the array
                buffer[whole] = buffer[0] << partial;
                for (size_type i = whole; i-- > 0;) {
                    buffer[i] = 0;
                }
            } else {
                for (size_type i = array_size, end = whole + 1; i-- > end;) {
                    size_type offset = i - whole;
                    buffer[i] = buffer[offset];
                }
                buffer[whole] = buffer[0];
                for (size_type i = whole; i-- > 0;) {
                    buffer[i] = 0;
                }
            }
            if constexpr (end_mask) {
                buffer[array_size - 1] &= end_mask;
            }

        // if the shift is larger than the size of the array, then we can clear it
        } else {
            std::fill_n(buffer.begin(), buffer.size(), word(0));
        }
        return *this;
    }

    /* Apply a bitwise right shift to the contents of the bitset. */
    [[nodiscard]] constexpr Bits operator>>(size_type rhs) const noexcept {
        Bits result;
        size_type whole = rhs / word_size;

        // if the shift is larger than the array size, then we can return empty
        if (whole < array_size) {
            size_type end = array_size - whole - 1;
            if (size_type partial = rhs % word_size) {
                // starting from the least significant word, look ahead by `whole`
                // words, then shift down the highest `word_size - partial` bits of
                // that word, and join the lowest `partial` bits of the next word
                for (size_type i = 0; i < end; ++i) {
                    size_type offset = i + whole;
                    result.buffer[i] = (buffer[offset] >> partial) |
                        (buffer[offset + 1] << (word_size - partial));
                }

                // the last word has no next word to join, so we just get the high bits
                result.buffer[end] = buffer[array_size - 1] >> partial;
            } else {
                for (size_type i = 0; i < end; ++i) {
                    size_type offset = i + whole;
                    result.buffer[i] = buffer[offset];
                }
                result.buffer[end] = buffer[array_size - 1];
            }
        }

        return result;
    }

    /* Apply a bitwise right shift to the contents of this bitset, updating it
    in-place. */
    constexpr Bits& operator>>=(size_type rhs) noexcept {
        size_type whole = rhs / word_size;

        // if the shift is smaller than the array size, then we have to shift without
        // invalidating the existing contents
        if (whole < array_size) {
            size_type end = array_size - whole - 1;
            if (size_type partial = rhs % word_size) {
                // starting from the least significant word, look ahead by `whole`
                // words, then shift down the highest `word_size - partial` bits of
                // that word, and join the lowest `partial` bits of the next word
                for (size_type i = 0; i < end; ++i) {
                    size_type offset = i + whole;
                    buffer[i] = (buffer[offset] >> partial) |
                        (buffer[offset + 1] << (word_size - partial));
                }

                // the last word has no next word to join, so we just get the high
                // bits, and then clear the rest of the array
                buffer[end] = buffer[array_size - 1] >> partial;

            } else {
                for (size_type i = 0; i < end; ++i) {
                    size_type offset = i + whole;
                    buffer[i] = buffer[offset];
                }
                buffer[end] = buffer[array_size - 1];
            }
            for (size_type i = array_size - whole; i < array_size; ++i) {
                buffer[i] = 0;
            }

        // if the shift is larger than the size of the array, then we can clear it
        } else {
            for (size_type i = 0; i < array_size; ++i) {
                buffer[i] = 0;
            }
        }

        return *this;
    }

    /* Increment a bitset by one and return a reference to the new value. */
    constexpr Bits& operator++() noexcept {
        if constexpr (N) {
            word carry = buffer[0] == std::numeric_limits<word>::max();
            ++buffer[0];
            for (size_type i = 1; carry && i < array_size - (end_mask > 0); ++i) {
                carry = buffer[i] == std::numeric_limits<word>::max();
                ++buffer[i];
            }
            if constexpr (end_mask) {
                buffer[array_size - 1] = (buffer[array_size - 1] + carry) & end_mask;
            }
        }
        return *this;
    }

    /* Increment the bitset by one and return a copy of the old value. */
    [[nodiscard]] constexpr Bits operator++(int) noexcept {
        Bits copy = *this;
        ++*this;
        return copy;
    }

    /* Operator version of `Bits.add()` that discards the overflow flag. */
    [[nodiscard]] friend constexpr Bits operator+(
        const Bits& lhs,
        const Bits& rhs
    ) noexcept {
        bool overflow;
        return lhs.add(rhs, overflow);
    }

    /* Operator version of `Bits.add()` that discards the overflow flag and
    writes the sum back to this bitset. */
    constexpr Bits& operator+=(const Bits& other) noexcept {
        bool overflow;
        add(*this, overflow, *this);
        return *this;
    }

    /* Decrement the bitset by one and return a reference to the new value. */
    constexpr Bits& operator--() noexcept {
        if constexpr (N) {
            word borrow = buffer[0] == 0;
            --buffer[0];
            for (size_type i = 1; borrow && i < array_size - (end_mask > 0); ++i) {
                borrow = buffer[i] == 0;
                --buffer[i];
            }
            if constexpr (end_mask) {
                buffer[array_size - 1] = (buffer[array_size - 1] - borrow) & end_mask;
            }
        }
        return *this;
    }

    /* Decrement the bitset by one and return a copy of the old value. */
    [[nodiscard]] constexpr Bits operator--(int) noexcept {
        Bits copy = *this;
        --*this;
        return copy;
    }

    /* Operator version of `Bits.sub()` that discards the overflow flag. */
    [[nodiscard]] friend constexpr Bits operator-(
        const Bits& lhs,
        const Bits& rhs
    ) noexcept {
        bool overflow;
        return lhs.sub(rhs, overflow);
    }

    /* Operator version of `Bits.sub()` that discards the overflow flag and
    writes the difference back to this bitset. */
    constexpr Bits& operator-=(const Bits& other) noexcept {
        bool overflow;
        sub(other, overflow, *this);
        return *this;
    }

    /* Operator version of `Bits.mul()` that discards the overflow flag. */
    [[nodiscard]] friend constexpr Bits operator*(
        const Bits& lhs,
        const Bits& rhs
    ) noexcept {
        bool overflow;
        return lhs.mul(rhs, overflow);
    }

    /* Operator version of `Bits.mul()` that discards the overflow flag and writes
    the product back to this bitset. */
    constexpr Bits& operator*=(const Bits& other) noexcept {
        bool overflow;
        mul(other, overflow, *this);
        return *this;
    }

    /* Operator version of `Bits.divmod()` that discards the remainder. */
    [[nodiscard]] friend constexpr Bits operator/(
        const Bits& lhs,
        const Bits& rhs
    ) {
        Bits quotient, remainder;
        lhs.divmod(rhs, quotient, remainder);
        return quotient;
    }

    /* Operator version of `Bits.divmod()` that discards the remainder and
    writes the quotient back to this bitset. */
    constexpr Bits& operator/=(const Bits& other) {
        Bits remainder;
        divmod(other, *this, remainder);
        return *this;
    }

    /* Operator version of `Bits.divmod()` that discards the quotient. */
    [[nodiscard]] friend constexpr Bits operator%(
        const Bits& lhs,
        const Bits& rhs
    ) {
        Bits quotient, remainder;
        lhs.divmod(rhs, quotient, remainder);
        return remainder;
    }

    /* Operator version of `Bits.divmod()` that discards the quotient and
    writes the remainder back to this bitset. */
    constexpr Bits& operator%=(const Bits& other) {
        Bits quotient;
        divmod(other, quotient, *this);
        return *this;
    }







    /* Print the bitset to an output stream. */
    constexpr friend std::ostream& operator<<(std::ostream& os, const Bits& set)
        noexcept(noexcept(os << std::string(set)))
    {
        /// TODO: maybe print directly, rather than requiring an allocation and
        /// extra layer of indirection?
        os << std::string(set);
        return os;
    }

    /// TODO: input streams should read the bitset in big-endian order.  Maybe it
    /// can detect any of the recognized prefixes, such as `0b`, `0x`, or `0o`, and
    /// then read the rest of the string in that base, defaulting to base 10 if no
    /// prefix is found.

    /* Read the bitset from an input stream. */
    constexpr friend std::istream& operator>>(std::istream& is, Bits& set) {
        char c;
        is.get(c);
        while (std::isspace(c)) {
            is.get(c);
        }
        size_type i = 0;
        if (c == '0') {
            is.get(c);
            if (c == 'b') {
                is.get(c);
            } else {
                ++i;
            }
        }
        bool one;
        while (is.good() && i < N && (c == '0' || (one = (c == '1')))) {
            set[i++] = one;
            is.get(c);
        }
        return is;
    }
};


template <meta::integer... Ts>
Bits(Ts...) -> Bits<impl::bitcount<Ts...>>;
template <size_t N>
Bits(const char(&)[N]) -> Bits<N - 1>;
template <size_t N>
Bits(const char(&)[N], char) -> Bits<N - 1>;
template <size_t N>
Bits(const char(&)[N], char, char) -> Bits<N - 1>;


template <size_t N>
struct UInt : impl::UInt_tag {
    Bits<N> value;

    /// TODO: very thin wrapper around `Bits<N>` that mostly just forwards the
    /// same interface, possibly without iteration/indexing support, etc.

};


template <meta::integer... Ts>
UInt(Ts...) -> UInt<impl::bitcount<Ts...>>;


template <size_t N>
struct Int : impl::Int_tag {
    Bits<N> value;

    /// TODO: a very thin wrapper around `Bits<N>` that applies two's complement
    /// and modifies the arithmetic operators very slightly, so that I can use it as
    /// a generalized integer type.

};


template <meta::integer... Ts>
Int(Ts...) -> Int<impl::bitcount<Ts...>>;



/// TODO: CTAD guides for Int<N> and UInt<N> that allow the bit width to be inferred
/// from the initializer.


/// TODO: maybe in the future, I can add a `Float<E, M>` type that generalizes a float
/// with `E` exponent bits and `M` mantissa bits, which would be useful especially for
/// ML applications.


}  // namespace bertrand


namespace std {

    /* Specializing `std::hash` allows bitsets to be stored in hash tables. */
    template <bertrand::meta::Bits T>
    struct hash<T> {
        [[nodiscard]] static size_t operator()(const T& bits) noexcept {
            size_t result = 0;
            for (size_t i = 0; i < bits.data().size(); ++i) {
                result = bertrand::impl::hash_combine(
                    result,
                    bits.data()[i]
                );
            }
            return result;
        }
    };

    /* Specializing `std::tuple_size` allows bitsets to be decomposed using
    structured bindings. */
    template <bertrand::meta::Bits T>
    struct tuple_size<T> :
        std::integral_constant<size_t, std::remove_cvref_t<T>::size()>
    {};

    /* Specializing `std::tuple_element` allows bitsets to be decomposed using
    structured bindings. */
    template <size_t I, bertrand::meta::Bits T>
        requires (I < std::remove_cvref_t<T>::size())
    struct tuple_element<I, T> { using type = bool; };

    /* `std::get<I>(chain)` extracts the I-th flag from the bitset. */
    template <size_t I, bertrand::meta::Bits T>
        requires (I < std::remove_cvref_t<T>::size())
    [[nodiscard]] constexpr bool get(T&& bits) noexcept {
        return std::forward<T>(bits).template get<I>();
    }

    /// TODO: specialize `std::numeric_limits` for `Bits<N>`, `Int<N>`, `UInt<N>`,
    /// and `Float<E, M>`.

}


namespace bertrand {


    template <Bits b>
    struct Foo {
        static constexpr const auto& value = b;
    };

    template <typename W>
    concept int_like =
        meta::integer<W> ||
        meta::Bits<W> ||
        meta::UInt<W> ||
        meta::Int<W>;

    inline void test() {
        {
            static constexpr Bits<2> a{0b1010};
            static constexpr Bits a2{false, true};
            static constexpr Bits a3{1, 2};
            static constexpr Bits a4{0, true};
            static_assert(a4.count() == 1);
            static_assert(a3.size() == 64);
            static_assert(a == a2);
            auto [f1, f2] = a;
            static constexpr Bits b = {"abab", 'b', 'a'};
            static constexpr std::string c = b.to_binary();
            static constexpr std::string d = b.to_decimal();
            static constexpr std::string d2 = b.to_string();
            static constexpr std::string d3 = b.to_hex();
            static_assert(any(b.components()));
            static_assert(b.first_one(2).value() == 3);
            static_assert(a == 0b10);
            static_assert(b == 0b1010);
            static_assert(b[1] == true);
            static_assert(c == "1010");
            static_assert(d == "10");
            static_assert(d2 == "1010");
            static_assert(d3 == "A");

            static_assert(std::same_as<typename Bits<2>::word, uint8_t>);
            static_assert(sizeof(Bits<2>) == 1);

            constexpr auto x = []() {
                Bits<4> out;
                out[-1] = true;
                return out;
            }();
            static_assert(x == uint8_t(0b1000));

            for (auto&& x : a.components()) {

            }
        }

        {
            static constexpr Bits b = "100";
            static constexpr auto b2 = Bits<3>::from_string(
                std::string_view("100")
            );
            static_assert(b.data()[0] == 4);
            static_assert(b2.result().data()[0] == 4);

            static constexpr auto b3 = Bits{"0100"}.reverse();
            static_assert(b3.data()[0] == 2);

            static constexpr auto b4 = Bits<3>::from_string<"ab", "c">("cabab");
            static_assert(b4.result().data()[0] == 4);

            static constexpr auto b5 = Bits<3>::from_decimal("5");
            static_assert(b5.result().data()[0] == 5);

            static constexpr auto b6 = Bits<8>::from_hex("FF");
            static_assert(b6.result().data()[0] == 255);

            static constexpr auto b7 = Bits<8>::from_hex("ZZ");
            static_assert(b7.has_error());

            static constexpr auto b8 = Bits<8>::from_hex("FFC");
            static_assert(b8.has_error());
        }

        {
            static constexpr Foo<{true, false, true}> foo;
            static constexpr Foo<{"bab", 'a', 'b'}> bar;
            static_assert(foo.value == "101");
            static_assert(bar.value == 5);
            static_assert(bar.value == 5);
        }

        {
            static_assert(Bits<5>::from_binary("10101").result().to_hex() == "15");
            static_assert(Bits<72>::from_hex("FFFFFFFFFFFFFFFFFF").result().count() == 72);
            static_assert(Bits<4>::from_octal("20").has_error());
            static_assert(Bits<4>::from_decimal("16").has_error<OverflowError>());
            static_assert(Bits<4>::from_decimal("15").result().data()[0] == 15);

            // static_assert((Bits<4>{uint8_t(1)} * uint8_t(10) + uint8_t(2)).data()[0] == 10);
        }

        {
            static constexpr Bits<5> a = -1;
            static constexpr Bits<5> b = a + 2;
            static_assert(a == 31);
            static_assert(b.data()[0] == 1);

            static constexpr Bits<5> c;
            static constexpr Bits<5> d = c - 1;
            static_assert(c == 0);
            static_assert(d.data()[0] == 31);

            static constexpr Bits<5> e = 12;
            static constexpr Bits<5> f = e * 3;
            static_assert(f.data()[0] == 4);
        }

        {
            static constexpr Bits<4> b = {Bits{"110"}, true};
            static_assert(b.last_zero().value() == 0);
            static_assert(b == "1110");
        }
    }

}


#endif  // BERTRAND_BITSET_H
