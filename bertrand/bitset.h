#ifndef BERTRAND_BITSET_H
#define BERTRAND_BITSET_H

#include "bertrand/common.h"
#include "bertrand/except.h"
#include "bertrand/math.h"
#include "bertrand/iter.h"


namespace bertrand {


namespace impl {
    struct BitArray_tag {};

    template <meta::integer T>
    constexpr size_t _bitcount = sizeof(T) * 8;
    template <meta::boolean T>
    constexpr size_t _bitcount<T> = 1;
    template <meta::integer... Ts>
    constexpr size_t bitcount = (_bitcount<Ts> + ... + 0);

    static_assert(std::numeric_limits<int>::digits == 31);

    template <size_t M>
    struct word {
        using type = size_t;
        static constexpr size_t size = sizeof(type) * 8;
        struct big {
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
                type lo_lo = x.lo * y.lo;
                type lo_hi = x.lo * y.hi;
                type hi_lo = x.hi * y.lo;
                type hi_hi = x.hi * y.hi;

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
                    return {~type(0), ~type(0)};
                }

                // 3. Left shift divisor until the most significant bit is set.  This cannot
                // overflow the numerator because u.hi < v.  The strange bitwise AND is meant
                // to avoid undefined behavior when shifting by a full word size.  It is taken
                // from https://ridiculousfish.com/blog/posts/labor-of-division-episode-v.html
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
                type qhat = n.hi / d.hi;
                type rhat = n.hi % d.hi;
                type c1 = qhat * d.lo;
                type c2 = rhat * b + n.lo;
                if (c1 > c2) {
                    qhat -= 1 + ((c1 - c2) > v);
                }
                type q1 = qhat & mask;

                // 6. Compute the true (normalized) partial remainder.
                type r = n.hi * b + n.lo - q1 * v;

                // 7. Estimate q0 = [r1 r0 n0] / [d1 d0].  These are the bottom bits of the
                // final quotient.
                qhat = r / d.hi;
                rhat = r % d.hi;
                c1 = qhat * d.lo;
                c2 = rhat * b + n.lo;
                if (c1 > c2) {
                    qhat -= 1 + ((c1 - c2) > v);
                }
                type q0 = qhat & mask;

                // 8. Return the quotient and unnormalized remainder
                return {(q1 << chunk) | q0, ((r * b) + n.lo - (q0 * v)) >> shift};
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
            uint16_t value;
            constexpr big(uint16_t v) noexcept : value(v) {}
            constexpr big(uint8_t hi, uint8_t lo) noexcept :
                value((uint16_t(hi) << size) | uint16_t(lo))
            {}
            constexpr uint8_t hi() const noexcept {
                return uint8_t(value >> size);
            }
            constexpr uint8_t lo() const noexcept {
                return uint8_t(value & ((uint16_t(1) << size) - 1));
            }
            static constexpr big mul(uint8_t a, uint8_t b) noexcept {
                return {uint16_t(uint16_t(a) * uint16_t(b))};
            }
            constexpr big operator/(uint8_t v) const noexcept {
                return {uint16_t(value / v)};
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
            uint32_t value;
            constexpr big(uint32_t v) noexcept : value(v) {}
            constexpr big(uint16_t hi, uint16_t lo) noexcept :
                value((uint32_t(hi) << size) | uint32_t(lo))
            {}
            constexpr uint16_t hi() const noexcept {
                return uint16_t(value >> size);
            }
            constexpr uint16_t lo() const noexcept {
                return uint16_t(value & ((uint32_t(1) << size) - 1));
            }
            static constexpr big mul(uint16_t a, uint16_t b) noexcept {
                return {uint32_t(uint32_t(a) * uint32_t(b))};
            }
            constexpr big operator/(uint16_t v) const noexcept {
                return {uint32_t(value / v)};
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
            uint64_t value;
            constexpr big(uint64_t v) noexcept : value(v) {}
            constexpr big(uint32_t hi, uint32_t lo) noexcept :
                value((uint64_t(hi) << size) | uint64_t(lo))
            {}
            constexpr uint32_t hi() const noexcept {
                return uint32_t(value >> size);
            }
            constexpr uint32_t lo() const noexcept {
                return uint32_t(value & ((uint64_t(1) << size) - 1));
            }
            static constexpr big mul(uint32_t a, uint32_t b) noexcept {
                return {uint64_t(uint64_t(a) * uint64_t(b))};
            }
            constexpr big operator/(uint32_t v) const noexcept {
                return {uint64_t(value / v)};
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

}


namespace meta {
    template <typename T>
    concept BitArray = inherits<T, impl::BitArray_tag>;
}


/* A simple bitset type that stores flags in a fixed-size array of machine words.
Allows a wider range of operations than `std::bitset<N>`, including full, bigint-style
arithmetic, lexicographic comparisons, one-hot decomposition, and more, which allow
`BitArray`s to pull double duty as portable, unsigned integers of arbitrary width. */
template <size_t N>
struct BitArray : impl::BitArray_tag {
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

    std::array<word, array_size> m_data;

    static constexpr void _divmod(
        const BitArray& lhs,
        const BitArray& rhs,
        BitArray& quotient,
        BitArray& remainder
    ) {
        if constexpr (N <= word_size) {
            word l = lhs.m_data[0];
            word r = rhs.m_data[0];
            quotient.m_data[0] = l / r;
            remainder.m_data[0] = l % r;
            return;

        } else {
            using Signed = meta::as_signed<word>;
            constexpr size_type chunk = word_size / 2;
            constexpr word b = word(1) << chunk;

            if (!rhs) {
                throw ZeroDivisionError();
            }
            if (lhs < rhs) {
                remainder = lhs;
                quotient.fill(0);
                return;
            }

            // 1. Compute effective lengths.  Above checks ensure that neither operand
            // is zero.
            size_type lhs_last = size();
            for (size_type i = array_size; i-- > 0;) {
                size_type j = size_type(std::countl_zero(lhs.m_data[i]));
                if (j < word_size) {
                    lhs_last = size_type(word_size * i + word_size - 1 - j);
                    break;
                }
            }
            size_type rhs_last = size();
            for (size_type i = array_size; i-- > 0;) {
                size_type j = size_type(std::countl_zero(rhs.m_data[i]));
                if (j < word_size) {
                    rhs_last = size_type(word_size * i + word_size - 1 - j);
                    break;
                }
            }
            size_type n = (rhs_last + (word_size - 1)) / word_size;
            size_type m = ((lhs_last + (word_size - 1)) / word_size) - n;

            // 2. If the divisor is a single word, then we can avoid multi-word division.
            if (n == 1) {
                word v = rhs.m_data[0];
                word rem = 0;
                for (size_type i = m + n; i-- > 0;) {
                    auto wide = big_word{rem, lhs.m_data[i]} / v;
                    quotient.m_data[i] = wide.hi();
                    rem = wide.lo();
                }
                for (size_type i = m + n; i < array_size; ++i) {
                    quotient.m_data[i] = 0;
                }
                remainder.m_data[0] = rem;
                for (size_type i = 1; i < n; ++i) {
                    remainder.m_data[i] = 0;
                }
                return;
            }

            /// NOTE: this is based on Knuth's Algorithm D, which is among the simplest for
            /// bigint division.  Much of the implementation was taken from:
            /// https://skanthak.hier-im-netz.de/division.html
            /// Which references Hacker's Delight, with a helpful explanation of the
            /// algorithm design.  See that or the Knuth reference for more details.
            std::array<word, array_size> v = rhs.m_data;
            std::array<word, array_size + 1> u;
            for (size_type i = 0; i < array_size; ++i) {
                u[i] = lhs.m_data[i];
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
                while (qhat >= b || (
                    big_word::mul(qhat, v[n - 2]) > big_word{word(rhat * b), u[j + n - 2]}
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
                quotient.m_data[j] = qhat;
            }

            // 7. Unshift the remainder and quotient to get the final result
            for (size_type i = 0; i < n - 1; ++i) {
                remainder.m_data[i] = (u[i] >> shift) | (u[i + 1] << (word_size - shift));
            }
            remainder.m_data[n - 1] = u[n - 1] >> shift;
            for (size_type i = n; i < array_size; ++i) {
                remainder.m_data[i] = 0;
            }
        }
    }

    template <size_t J, const BitArray& divisor>
    static constexpr std::string_view _to_string_helper(
        const auto& digits,
        BitArray& quotient,
        BitArray& remainder,
        size_type& size
    ) {
        _divmod(quotient, divisor, quotient, remainder);
        std::string_view out = digits[remainder.data()[0]];
        size += out.size();
        return out;
    }

    template <size_t J>
    constexpr std::string_view _to_string_helper(
        const auto& digits,
        size_type& size
    ) const {
        bool bit = m_data[J / word_size] & (word(1) << (J % word_size));
        std::string_view out = digits[bit];
        size += out.size();
        return out;
    }

    template <size_t... Is, meta::convertible_to<std::string_view>... Strs>
    constexpr std::string _to_string(
        std::index_sequence<Is...>,
        Strs... strs
    ) const noexcept(!DEBUG) {
        // # of digits needed to represent the value in `base = sizeof...(Strs)` is
        // ceil(N / log2(base))
        static constexpr double len = N / impl::log2_table[sizeof...(Strs)];
        static constexpr size_type ceil = size_type(len) + (size_type(len) < len);
        static constexpr BitArray divisor = word(sizeof...(Strs));

        // generate a lookup table of substrings to use for each digit
        std::array<std::string_view, sizeof...(Strs)> digits {
            std::forward<decltype(strs)>(strs)...
        };
        if constexpr (DEBUG) {
            size_type min_len = min(digits[Is].size()...);
            if (min_len == 0) {
                throw ValueError("substrings must not be empty");
            }
        }

        // if the base is larger than the representable range of the bitset, then we
        // can avoid division entirely.
        if constexpr (N <= word_size && sizeof...(Strs) >= (1ULL << N)) {
            return std::string(digits[word(*this)]);

        // if the base is exactly 2, then we can use a simple bitscan to determine the
        // final return string, rather than doing multi-word division
        } else if (sizeof...(Strs) == 2) {
            size_type size = 0;
            auto contents = [this]<size_t... Js>(
                std::index_sequence<Js...>,
                auto& digits,
                size_type& size
            ) {
                return std::array<std::string_view, ceil>{
                    _to_string_helper<Js>(digits, size)...
                };
            }(std::make_index_sequence<ceil>{}, digits, size);

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
            BitArray quotient = *this;
            BitArray remainder;
            size_type size = 0;
            auto contents = []<size_t... Js>(
                std::index_sequence<Js...>,
                auto& digits,
                BitArray& quotient,
                BitArray& remainder,
                size_type& size
            ) {
                return std::array<std::string_view, ceil>{
                    _to_string_helper<Js, divisor>(digits, quotient, remainder, size)...
                };
            }(std::make_index_sequence<ceil>{}, digits, quotient, remainder, size);

            // join the substrings in reverse order to create the final result
            std::string result;
            result.reserve(size);
            for (size_type i = ceil; i-- > 0;) {
                result.append(contents[i]);
            }
            return result;
        }
    }

    template <size_type I>
    constexpr void from_booleans(std::array<word, array_size>& data) noexcept {}
    template <size_type I, meta::boolean T, meta::boolean... Ts>
    constexpr void from_booleans(
        std::array<word, array_size>& data,
        T first,
        Ts... rest
    ) noexcept {
        data[I / word_size] |= word(word(first) << (I % word_size));
        from_booleans<I + 1>(data, rest...);
    }

    template <size_type I>
    constexpr void from_integers(std::array<word, array_size>& data) noexcept {
        if constexpr (end_mask) {
            data[array_size - 1] &= end_mask;
        }
    }
    template <size_type I, meta::integer T, meta::integer... Ts>
    constexpr void from_integers(
        std::array<word, array_size>& data,
        T first,
        Ts... rest
    ) noexcept {
        static constexpr size_type start_bit = I % word_size;
        static constexpr size_type start_word = I / word_size;
        static constexpr size_type stop_word = (I + impl::bitcount<T>) / word_size;
        static constexpr size_type stop_bit = (I + impl::bitcount<T>) % word_size;

        if constexpr (stop_bit == 0 || start_word == stop_word) {
            data[start_word] |= word(word(first) << start_bit);
        } else {
            data[start_word] |= word(word(first) << start_bit);
            size_type consumed = word_size - start_bit;
            first >>= consumed;
            size_type i = start_word;
            while (consumed < impl::bitcount<T>) {
                data[++i] |= word(first);
                if constexpr (word_size < impl::bitcount<T>) {
                    first >>= word_size;
                }
                consumed += word_size;
            }
        }

        from_integers<I + impl::bitcount<T>>(data, rest...);
    }

public:
    /* A mutable reference to a single bit in the set. */
    struct reference {
    private:
        friend BitArray;
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
        using value_type = BitArray::reference;
        using reference = value_type&;
        using const_reference = const value_type&;
        using pointer = value_type*;
        using const_pointer = const value_type*;

    private:
        friend BitArray;
        BitArray* self;
        difference_type index;
        mutable value_type cache;

        constexpr iterator(BitArray* self, difference_type index) noexcept :
            self(self), index(index)
        {}

    public:
        [[nodiscard]] constexpr reference operator*() noexcept(!DEBUG) {
            if constexpr (DEBUG) {
                if (index < 0 || index >= N) {
                    throw IndexError(std::to_string(index));
                }
            }
            cache = {&self->m_data[index / word_size], word(index % word_size)};
            return cache;
        }

        [[nodiscard]] constexpr const_reference operator*() const noexcept(!DEBUG) {
            if constexpr (DEBUG) {
                if (index < 0 || index >= N) {
                    throw IndexError(std::to_string(index));
                }
            }
            cache = {&self->m_data[index / word_size], word(index % word_size)};
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
            return {&self->m_data[idx / word_size], word(idx % word_size)};
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
        friend BitArray;
        const BitArray* self;
        difference_type index;
        mutable bool cache;

        constexpr const_iterator(const BitArray* self, difference_type index) noexcept :
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
        friend BitArray;
        const BitArray* self;
        index_type start;
        index_type stop;

        constexpr one_hot(
            const BitArray* self,
            index_type start,
            index_type stop
        ) noexcept :
            self(self), start(start), stop(stop)
        {}

    public:
        struct iterator {
            using iterator_category = std::input_iterator_tag;
            using difference_type = index_type;
            using value_type = BitArray;
            using reference = const value_type&;
            using const_reference = reference;
            using pointer = const value_type*;
            using const_pointer = pointer;

        private:
            friend one_hot;
            const BitArray* self;
            difference_type index;
            difference_type stop;
            value_type curr;

            static constexpr difference_type next(
                const BitArray* self,
                difference_type start,
                difference_type stop
            ) noexcept {
                BitArray temp;
                temp.fill(1);
                temp <<= N - size_type(stop);
                temp >>= N - size_type(stop - start);
                temp <<= size_type(start);  
                for (size_type i = 0; i < array_size; ++i) {
                    size_type j = size_type(std::countr_zero(
                        word(self->m_data[i] & temp.m_data[i])
                    ));
                    if (j < word_size) {
                        return difference_type(word_size * i + j);
                    }
                }
                return stop;
            }

            constexpr iterator(
                const BitArray* self,
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

    /* Construct a BitArray from a variadic parameter pack of boolean initializers.
    The flag sequence is parsed left-to-right in little-endian order, meaning the
    first boolean will correspond to the least significant bit of the first word in
    the BitArray, which is stored at index 0.  The last boolean will be the most
    significant bit, which is stored at index `sizeof...(bits) - 1`.  The total
    number of arguments cannot exceed `N`, and any remaining bits will be initialized
    to zero. */
    template <meta::boolean... bits> requires (sizeof...(bits) <= N)
    constexpr BitArray(bits... vals) noexcept : m_data{} {
        from_booleans<0>(m_data, vals...);
    }

    /* Construct a BitArray from a sequence of integer values whose bit widths sum
    to an amount less than or equal to the BitArray's storage capacity.  If the
    BitArray width is not an even multiple of the word size, then any upper bits above
    `N` will be masked off and initialized to zero.

    The values are parsed left-to-right in little-endian order, and are joined together
    using bitwise OR to form the final BitArray.  The initializers do not need to have
    the same type, as long as their combined widths do not exceed the BitArray's
    capacity.  Any remaining bits will be initialized to zero.  Little-endianness in
    this context means that the first integer will correspond to the `M` least
    significant bits of the bitarray, where `M` is the bit width of the first
    integer.  The last integer will correspond to the most significant bits of the
    bitarray, which will be terminate at index `sum(M_i...)`, where `M_i` is the bit
    width of the `i`th integer. */
    template <meta::integer... words>
        requires (
            sizeof...(words) > 0 &&
            (!meta::boolean<words> || ...) &&
            impl::bitcount<words...> <= capacity()
        )
    constexpr BitArray(words... vals) noexcept : m_data{} {
        from_integers<0>(m_data, vals...);
    }

    /* Construct a BitArray from a string of true and false substrings.  Throws a
    `ValueError` if the string contains any substrings other than the indicated ones,
    or if either of the substrings are empty.  If the string contains fewer than `N`
    substrings, the remaining bits will be initialized to zero.  If it contains more
    than `N`, the extra substrings will be ignored.  Note that the value will be parsed
    as if it were big-endian, meaning the first character of the string corresponds
    to the most significant bit, which is stored at index `str.size() - 1`.  The
    last character corresponds to the least significant bit, which can be found at
    index 0. */
    constexpr BitArray(
        std::string_view str,
        std::string_view zero = "0",
        std::string_view one = "1"
    ) : m_data{} {
        size_type min_len = min(zero.size(), one.size());
        if (min_len == 0) {
            if (one.empty()) {
                throw ValueError("`one` substring must not be empty");
            } else {
                throw ValueError("`zero` substring must not be empty");
            }
        }
        constexpr size_type M = N - 1;
        for (size_type i = 0, j = 0; i < str.size() && j < N;) {
            if (str.substr(i, zero.size()) == zero) {
                i += zero.size();
                ++j;
            } else if (str.substr(i, one.size()) == one) {
                m_data[(M - j) / word_size] |= (word(1) << ((M - j) % word_size));
                i += one.size();
                ++j;
            } else {
                throw ValueError(
                    "string must contain only '" + std::string(zero) + "' and '" +
                    std::string(one) + "', not: '" +
                    std::string(str.substr(i, max(zero.size(), one.size()))) +
                    "' at index " + std::to_string(i)
                );
            }
        }
    }

    /* Construct a BitArray from a string literal of length `N`, consisting of the
    indicated true and false characters.  Throws a `ValueError` if the string contains
    any characters other than the indicated ones.  A constructor of this form allows
    for CTAD-based width deduction from string literal initializers. */
    constexpr BitArray(
        const char(&str)[N + 1],
        char zero = '0',
        char one = '1'
    ) : m_data{} {
        constexpr size_type M = N - 1;

        for (size_type i = 0; i < N; ++i) {
            if (str[i] == zero) {
                // do nothing - the bit is already zero
            } else if (str[i] == one) {
                m_data[(M - i) / word_size] |= (word(1) << ((M - i) % word_size));
            } else {
                throw ValueError(
                    "string must contain only '" + std::string(1, zero) +
                    "' and '" + std::string(1, one) + "', not: '" + str[i] +
                    "' at index " + std::to_string(i)
                );
            }
        }
    }

    /* Bitsets evalute true if any of their bits are set. */
    [[nodiscard]] explicit constexpr operator bool() const noexcept {
        return any();
    }

    /* Convert the BitArray to an integer representation if it fits within a single
    word. */
    template <typename T>
        requires (N <= word_size && meta::explicitly_convertible_to<word, T>)
    [[nodiscard]] explicit constexpr operator T() const noexcept {
        if constexpr (array_size == 0) {
            return static_cast<T>(word(0));
        } else {
            return static_cast<T>(m_data[0]);
        }
    }

    /// TODO: add an explicit conversion to anything that is constructible from either
    /// an array of words or an array of booleans?

    /* Convert the BitArray to a string representation with '1' as the true character
    and '0' as the false character.  Note that the string is returned in big-endian
    order, meaning the first character corresponds to the most significant bit in the
    BitArray, and the last character corresponds to the least significant bit.  The
    string will be zero-padded to the exact width of the BitArray, and can be passed
    back to the BitArray constructor to recover the original state. */
    [[nodiscard]] explicit constexpr operator std::string() const noexcept {
        constexpr size_type M = N - 1;
        constexpr int diff = '1' - '0';
        std::string result;
        result.reserve(N);
        for (size_type i = 0; i < N; ++i) {
            bool bit = m_data[(M - i) / word_size] & (word(1) << ((M - i) % word_size));
            result.push_back('0' + diff * bit);
        }
        return result;
    }

    /* Convert the BitArray into a string representation.  Defaults to base 2 with the
    given zero and one characters, but also allows bases up to 64 by enumerating a
    custom sequence of substrings for each digit.  The overall length of the sequence
    dictates the base for the conversion, which must be at least 2.  The result is
    always padded to the exact width needed to represent the BitArray in the chosen
    base, including leading zeroes if needed.  Note that the resulting string is
    always big-endian, meaning the first substring corresponds to the most
    significant digit in the BitArray, and the last substring corresponds to the least
    significant digit.  If the base is 2, then the result can be passed back to the
    `BitArray` constructor to recover the original state.  Throws a `ValueError` if any
    of the substrings are empty. */
    template <
        meta::convertible_to<std::string_view> Zero = std::string_view,
        meta::convertible_to<std::string_view> One = std::string_view,
        meta::convertible_to<std::string_view>... Rest
    > requires (sizeof...(Rest) + 2 <= 64)
    [[nodiscard]] constexpr std::string to_string(
        Zero&& zero = "0",
        One&& one = "1",
        Rest&&... rest
    ) const {
        return _to_string(
            std::index_sequence_for<Zero, One, Rest...>{},
            std::forward<Zero>(zero),
            std::forward<One>(one),
            std::forward<Rest>(rest)...
        );
    }

    /* A shorthand for `to_string("0", "1")`, which yields a string in the canonical
    binary representation. */
    [[nodiscard]] constexpr std::string to_binary() const noexcept {
        return operator std::string();
    }

    /* A shorthand for `to_string("0", "1", "2", "3", "4", "5", "6", "7")`, which
    yields a string in the canonical octal representation. */
    [[nodiscard]] constexpr std::string to_octal() const noexcept {
        return to_string("0", "1", "2", "3", "4", "5", "6", "7");
    }

    /* A shorthand for `to_string("0", "1", "2", "3", "4", "5", "6", "7", "8", "9")`,
    which yields a string in the canonical decimal representation. */
    [[nodiscard]] constexpr std::string to_decimal() const noexcept {
        return to_string(
            "0", "1", "2", "3", "4", "5", "6", "7", "8", "9"
        );
    }

    /* A shorthand for
    `to_string("0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "A", "B", "C", "D", "E", "F")`,
    which yields a string in the canonical hexadecimal representation. */
    [[nodiscard]] constexpr std::string to_hex() const noexcept {
        return to_string(
            "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
            "A", "B", "C", "D", "E", "F"
        );
    }

    /* Get the underlying array. */
    [[nodiscard]] constexpr auto& data() noexcept { return m_data; }
    [[nodiscard]] constexpr const auto& data() const noexcept { return m_data; }
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

    /* Return a range over the one-hot masks that make up the BitArray within a given
    interval.  Iterating over the range yields BitArrays of the same size as the input
    with only one active bit and zero everywhere else.  Summing the masks yields an
    exact copy of the input BitArray within the interval.  The range may be empty if
    no bits are set within the interval. */
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
            return {this, ssize()};  // Return end iterator if out of bounds
        }
        return {this, i};
    }
    [[nodiscard]] constexpr const_iterator at(index_type index) const noexcept {
        index_type i = index + ssize() * (index < 0);
        if (i < 0 || i >= ssize()) {
            return {this, ssize()};  // Return end iterator if out of bounds
        }
        return {this, i};
    }

    /* Get the value of a specific bit in the set, where the bit index is known at
    compile time.  Does not apply Python-style wraparound, and fails to compile if the
    index is out of bounds.  This is a lower-level access than the `[]` operator, and
    may be faster in hot loops.  It is also available as `std::get<I>(BitArray)`, which
    allows the BitArray to be unpacked via structured bindings. */
    template <size_type I> requires (I < size())
    [[nodiscard]] constexpr reference get() noexcept {
        return {&m_data[I / word_size], word(I % word_size)};
    }
    template <size_type I> requires (I < size())
    [[nodiscard]] constexpr bool get() const noexcept {
        return m_data[I / word_size] & (word(1) << (I % word_size));
    }

    /* Get the value of a specific bit in the set, where the bit index is known at
    run time.  Does not apply Python-style wraparound, and throws an `IndexError` if
    the index is out of bounds and the program is compiled in debug mode.  This is a
    lower-level access than the `[]` operator, and may be faster in hot loops where the
    error case should never occur. */
    [[nodiscard]] constexpr reference get(size_type index) noexcept(!DEBUG) {
        if constexpr (DEBUG) {
            if (index >= size()) {
                throw IndexError(std::to_string(index));
            }
        }
        return {&m_data[index / word_size], word(index % word_size)};
    }
    [[nodiscard]] constexpr bool get(size_type index) const noexcept(!DEBUG) {
        if constexpr (DEBUG) {
            if (index >= size()) {
                throw IndexError(std::to_string(index));
            }
        }
        return m_data[index / word_size] & (word(1) << (index % word_size));
    }

    /* Get the value of a specific bit in the set.  Applies Python-style wraparound to
    the index, and returns an `Expected` monad with a value of `IndexError` if the
    index is out of bounds after normalizing. */
    [[nodiscard]] constexpr Expected<reference, IndexError> operator[](
        index_type index
    ) noexcept {
        index_type i = index + ssize() * (index < 0);
        if (i < 0 || i >= ssize()) {
            return IndexError(std::to_string(index));
        }
        return reference{&m_data[i / word_size], word(i % word_size)};
    }
    [[nodiscard]] constexpr Expected<bool, IndexError> operator[](
        index_type index
    ) const noexcept {
        index_type i = index + ssize() * (index < 0);
        if (i < 0 || i >= ssize()) {
            return IndexError(std::to_string(index));
        }
        return m_data[i / word_size] & (word(1) << (i % word_size));
    }

    /// TODO: operator[slice{}] returning a range of the BitArray.

    /* Check if any of the bits are set. */
    [[nodiscard]] constexpr bool any() const noexcept {
        for (size_type i = 0; i < array_size; ++i) {
            if (m_data[i]) {
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
        BitArray temp;
        temp.fill(1);
        temp <<= N - size_type(norm_stop);
        temp >>= N - size_type(norm_stop - norm_start);
        temp <<= size_type(norm_start);  // [start, stop).
        return bool(*this & temp);
    }

    /* Check if all of the bits are set. */
    [[nodiscard]] constexpr bool all() const noexcept {
        constexpr bool odd = N % word_size;
        for (size_type i = 0; i < array_size - odd; ++i) {
            if (m_data[i] != std::numeric_limits<word>::max()) {
                return false;
            }
        }
        if constexpr (odd) {
            constexpr word mask = (word(1) << (N % word_size)) - 1;
            return m_data[array_size - 1] == mask;
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
        BitArray temp;
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
            count += std::popcount(m_data[i]);
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
        BitArray temp;
        temp.fill(1);
        temp <<= N - size_type(norm_stop);
        temp >>= N - size_type(norm_stop - norm_start);
        temp <<= size_type(norm_start);  // [start, stop).
        size_type count = 0;
        for (size_type i = 0; i < array_size; ++i) {
            count += size_type(std::popcount(word(m_data[i] & temp.m_data[i])));
        }
        return count;
    }

    /* Return the index of the first bit that is set, or an empty optional if no bits
    are set. */
    [[nodiscard]] constexpr Optional<index_type> first_one() const noexcept {
        for (size_type i = 0; i < array_size; ++i) {
            size_type j = size_type(std::countr_zero(m_data[i]));
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
        BitArray temp;
        temp.fill(1);
        temp <<= N - size_type(norm_stop);
        temp >>= N - size_type(norm_stop - norm_start);
        temp <<= size_type(norm_start);  // [start, stop).
        for (size_type i = 0; i < array_size; ++i) {
            size_type j = size_type(std::countr_zero(
                word(m_data[i] & temp.m_data[i])
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
            size_type j = size_type(std::countl_zero(m_data[i]));
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
        BitArray temp;
        temp.fill(1);
        temp <<= N - size_type(norm_stop);
        temp >>= N - size_type(norm_stop - norm_start);
        temp <<= size_type(norm_start);  // [start, stop).
        for (size_type i = array_size; i-- > 0;) {
            size_type j = size_type(std::countl_zero(
                word(m_data[i] & temp.m_data[i])
            ));
            if (j < word_size) {
                return index_type(word_size * i + word_size - 1 - j);
            }
        }
        return std::nullopt;
    }


    /// TODO: first/last zero is a lot harder than first/last one, since the extra
    /// bits at the top of the final word are masked off and set to zero.


    /* Return the index of the first bit that is not set, or an empty optional if all
    bits are set. */
    [[nodiscard]] constexpr Optional<index_type> first_zero() const noexcept {
        for (size_type i = 0; i < array_size; ++i) {
            word curr = m_data[i];
            if (curr != std::numeric_limits<word>::max()) {
                return index_type(word_size * i + std::countr_one(curr));
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
        BitArray temp;
        temp.fill(1);
        temp <<= N - size_type(norm_stop);
        temp >>= N - size_type(norm_stop - norm_start);
        temp <<= size_type(norm_start);  // [start, stop).
        for (size_type i = 0; i < array_size; ++i) {
            word curr = m_data[i] & temp.m_data[i];
            if (curr != temp.m_data[i]) {
                return index_type(word_size * i + std::countr_one(curr));
            }
        }
        return std::nullopt;
    }

    /* Return the index of the last bit that is not set, or an empty optional if all
    bits are set. */
    [[nodiscard]] constexpr Optional<index_type> last_zero() const noexcept {
        constexpr bool odd = N % word_size;
        if constexpr (odd) {
            constexpr word mask = (word(1) << (N % word_size)) - 1;
            word curr = m_data[array_size - 1];
            if (curr != mask) {
                return index_type(
                    word_size * (array_size - 1) +
                    word_size - 1 -
                    std::countl_one(curr | ~mask)
                );
            }
        }
        for (size_type i = array_size - odd; i-- > 0;) {
            word curr = m_data[i];
            if (curr != std::numeric_limits<word>::max()) {
                return index_type(
                    word_size * i + word_size - 1 - std::countl_one(curr)
                );
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
        BitArray temp;
        temp.fill(1);
        temp <<= N - size_type(norm_stop);
        temp >>= N - size_type(norm_stop - norm_start);
        temp <<= size_type(norm_start);  // [start, stop).
        for (size_type i = array_size; i-- > 0;) {
            word curr = m_data[i] & temp.m_data[i];
            if (curr != temp.m_data[i]) {
                return index_type(
                    word_size * i +
                    word_size - 1 -
                    std::countl_one(curr | ~temp.m_data[i])
                );
            }
        }
        return std::nullopt;
    }

    /* Set all of the bits to the given value. */
    constexpr BitArray& fill(bool value) noexcept {
        constexpr bool odd = N % word_size;
        word filled = std::numeric_limits<word>::max() * value;
        for (size_type i = 0; i < array_size - odd; ++i) {
            m_data[i] = filled;
        }
        if constexpr (odd) {
            constexpr word mask = (word(1) << (N % word_size)) - 1;
            m_data[array_size - 1] = filled & mask;
        }
        return *this;
    }

    /* Set all of the bits within a certain interval to the given value. */
    constexpr BitArray& fill(
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
        BitArray temp;
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
    constexpr BitArray& flip() noexcept {
        constexpr bool odd = N % word_size;
        for (size_type i = 0; i < array_size - odd; ++i) {
            m_data[i] ^= std::numeric_limits<word>::max();
        }
        if constexpr (odd) {
            constexpr word mask = (word(1) << (N % word_size)) - 1;
            m_data[array_size - 1] ^= mask;
        }
        return *this;
    }

    /* Toggle all of the bits within a certain interval. */
    constexpr BitArray& flip(index_type start, index_type stop = ssize()) noexcept {
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
        BitArray temp;
        temp.fill(1);
        temp <<= N - size_type(norm_stop);
        temp >>= N - size_type(norm_stop - norm_start);
        temp <<= size_type(norm_start);  // [start, stop).
        *this ^= temp;
        return *this;
    }

    /* Lexicographically compare two BitArrays of equal size. */
    [[nodiscard]] friend constexpr auto operator<=>(
        const BitArray& lhs,
        const BitArray& rhs
    ) noexcept {
        return std::lexicographical_compare_three_way(
            lhs.m_data.begin(),
            lhs.m_data.end(),
            rhs.m_data.begin(),
            rhs.m_data.end()
        );
    }

    /* Check whether one BitArray is lexicographically equal to another of the same
    size. */
    [[nodiscard]] friend constexpr bool operator==(
        const BitArray& lhs,
        const BitArray& rhs
    ) noexcept {
        for (size_type i = 0; i < array_size; ++i) {
            if (lhs.m_data[i] != rhs.m_data[i]) {
                return false;
            }
        }
        return true;
    }

    /* Add two BitArrays of equal size. */
    [[nodiscard]] friend constexpr BitArray operator+(
        const BitArray& lhs,
        const BitArray& rhs
    ) noexcept {
        constexpr bool odd = N % word_size;
        BitArray result;
        word carry = 0;
        for (size_type i = 0; i < array_size - odd; ++i) {
            word a = lhs.m_data[i] + carry;
            carry = a < lhs.m_data[i];
            word b = a + rhs.m_data[i];
            carry |= b < a;
            result.m_data[i] = b;
        }
        if constexpr (odd) {
            constexpr word mask = (word(1) << (N % word_size)) - 1;
            word sum = lhs.m_data[array_size - 1] + carry + rhs.m_data[array_size - 1];
            result.m_data[array_size - 1] = sum & mask;
        }
        return result;
    }
    constexpr BitArray& operator+=(const BitArray& other) noexcept {
        constexpr bool odd = N % word_size;
        word carry = 0;
        for (size_type i = 0; i < array_size - odd; ++i) {
            word a = m_data[i] + carry;
            carry = a < m_data[i];
            word b = a + other.m_data[i];
            carry |= b < a;
            m_data[i] = b;
        }
        if constexpr (odd) {
            constexpr word mask = (word(1) << (N % word_size)) - 1;
            word sum = m_data[array_size - 1] + carry + other.m_data[array_size - 1];
            m_data[array_size - 1] = sum & mask;
        }
        return *this;
    }

    /* Subtract two BitArrays of equal size. */
    [[nodiscard]] friend constexpr BitArray operator-(
        const BitArray& lhs,
        const BitArray& rhs
    ) noexcept {
        constexpr bool odd = N % word_size;
        BitArray result;
        word borrow = 0;
        for (size_type i = 0; i < array_size - odd; ++i) {
            word a = lhs.m_data[i] - borrow;
            borrow = a > lhs.m_data[i];
            word b = a - rhs.m_data[i];
            borrow |= b > a;
            result.m_data[i] = b;
        }
        if constexpr (odd) {
            constexpr word mask = (word(1) << (N % word_size)) - 1;
            word diff = lhs.m_data[array_size - 1] - borrow - rhs.m_data[array_size - 1];
            result.m_data[array_size - 1] = diff & mask;
        }
        return result;
    }
    constexpr BitArray& operator-=(const BitArray& other) noexcept {
        constexpr bool odd = N % word_size;
        word borrow = 0;
        for (size_type i = 0; i < array_size - odd; ++i) {
            word a = m_data[i] - borrow;
            borrow = a > m_data[i];
            word b = a - other.m_data[i];
            borrow |= b > a;
            m_data[i] = b;
        }
        if constexpr (odd) {
            constexpr word mask = (word(1) << (N % word_size)) - 1;
            word diff = m_data[array_size - 1] - borrow - other.m_data[array_size - 1];
            m_data[array_size - 1] = diff & mask;
        }
        return *this;
    }

    /* Multiply two BitArrays of equal size. */
    [[nodiscard]] friend constexpr BitArray operator*(
        const BitArray& lhs,
        const BitArray& rhs
    ) noexcept {
        /// NOTE: this uses schoolbook multiplication, which is generally fastest for
        /// small set sizes, up to a couple thousand bits.
        constexpr size_type chunk = word_size / 2;
        BitArray result;
        word carry = 0;
        for (size_type i = 0; i < array_size; ++i) {
            word carry = 0;
            for (size_type j = 0; j + i < array_size; ++j) {
                size_type k = j + i;
                big_word prod = big_word::mul(lhs.m_data[i], rhs.m_data[j]);
                prod.lo += result.m_data[k];
                if (prod.lo < result.m_data[k]) {
                    ++prod.hi;
                }
                prod.lo += carry;
                if (prod.lo < carry) {
                    ++prod.hi;
                }
                result.m_data[k] = prod.lo;
                carry = prod.hi;
            }
        }
        if constexpr (N % word_size) {
            constexpr word mask = (word(1) << (N % word_size)) - 1;
            result.m_data[array_size - 1] &= mask;
        }
        return result;
    }
    constexpr BitArray& operator*=(const BitArray& other) noexcept {
        *this = *this * other;
        return *this;
    }





    /// TODO: return an Expected<T, ZeroDivisionError>

    /* Divide this BitArray by another and return both the quotient and remainder.  This
    is slightly more efficient than doing separate `/` and `%` operations if both are
    needed. */
    [[nodiscard]] constexpr std::pair<BitArray, BitArray> divmod(const BitArray& d) const {
        BitArray quotient, remainder;
        _divmod(*this, d, quotient, remainder);
        return {quotient, remainder};
    }
    [[nodiscard]] constexpr BitArray divmod(const BitArray& d, BitArray& remainder) const {
        BitArray quotient;
        _divmod(*this, d, quotient, remainder);
        return quotient;
    }
    constexpr void divmod(const BitArray& d, BitArray& quotient, BitArray& remainder) const {
        _divmod(*this, d, quotient, remainder);
    }

    /* Divide this BitArray by another in-place, returning the remainder.  This is
    slightly more efficient than doing separate `/=` and `%` operations */
    [[nodiscard]] constexpr BitArray idivmod(const BitArray& d) {
        BitArray remainder;
        _divmod(*this, d, *this, remainder);
        return remainder;
    }
    constexpr void idivmod(const BitArray& d, BitArray& remainder) {
        _divmod(*this, d, *this, remainder);
    }

    /* Divide two BitArrays of equal size. */
    [[nodiscard]] friend constexpr BitArray operator/(
        const BitArray& lhs,
        const BitArray& rhs
    ) {
        BitArray quotient, remainder;
        _divmod(lhs, rhs, quotient, remainder);
        return quotient;
    }
    constexpr BitArray& operator/=(const BitArray& other) {
        BitArray remainder;
        _divmod(*this, other, *this, remainder);
        return *this;
    }

    /* Get the remainder after dividing two BitArrays of equal size. */
    [[nodiscard]] friend constexpr BitArray operator%(
        const BitArray& lhs,
        const BitArray& rhs
    ) {
        BitArray quotient, remainder;
        _divmod(lhs, rhs, quotient, remainder);
        return remainder;
    }
    constexpr BitArray& operator%=(const BitArray& other) {
        BitArray quotient;
        _divmod(*this, other, quotient, *this);
        return *this;
    }

    /* Increment the BitArray by one. */
    constexpr BitArray& operator++() noexcept {
        if constexpr (N) {
            constexpr bool odd = N % word_size;
            word carry = m_data[0] == std::numeric_limits<word>::max();
            ++m_data[0];
            for (size_type i = 1; carry && i < array_size - odd; ++i) {
                carry = m_data[i] == std::numeric_limits<word>::max();
                ++m_data[i];
            }
            if constexpr (odd) {
                constexpr word mask = (word(1) << (N % word_size)) - 1;
                m_data[array_size - 1] = (m_data[array_size - 1] + carry) & mask;
            }
        }
        return *this;
    }
    constexpr BitArray operator++(int) noexcept {
        BitArray copy = *this;
        ++*this;
        return copy;
    }

    /* Decrement the BitArray by one. */
    constexpr BitArray& operator--() noexcept {
        if constexpr (N) {
            constexpr bool odd = N % word_size;
            word borrow = m_data[0] == 0;
            --m_data[0];
            for (size_type i = 1; borrow && i < array_size - odd; ++i) {
                borrow = m_data[i] == 0;
                --m_data[i];
            }
            if constexpr (odd) {
                constexpr word mask = (word(1) << (N % word_size)) - 1;
                m_data[array_size - 1] = (m_data[array_size - 1] - borrow) & mask;
            }
        }
        return *this;
    }
    constexpr BitArray operator--(int) noexcept {
        BitArray copy = *this;
        --*this;
        return copy;
    }

    /* Apply a binary NOT to the contents of the BitArray. */
    [[nodiscard]] friend constexpr BitArray operator~(const BitArray& set) noexcept {
        constexpr bool odd = N % word_size;
        BitArray result;
        for (size_type i = 0; i < array_size - odd; ++i) {
            result.m_data[i] = ~set.m_data[i];
        }
        if constexpr (odd) {
            constexpr word mask = (word(1) << (N % word_size)) - 1;
            result.m_data[array_size - 1] = ~set.m_data[array_size - 1] & mask;
        }
        return result;
    }

    /* Apply a binary AND between the contents of two BitArrays of equal size. */
    [[nodiscard]] friend constexpr BitArray operator&(
        const BitArray& lhs,
        const BitArray& rhs
    ) noexcept {
        BitArray result;
        for (size_type i = 0; i < array_size; ++i) {
            result.m_data[i] = lhs.m_data[i] & rhs.m_data[i];
        }
        return result;
    }
    constexpr BitArray& operator&=(
        const BitArray& other
    ) noexcept {
        for (size_type i = 0; i < array_size; ++i) {
            m_data[i] &= other.m_data[i];
        }
        return *this;
    }

    /* Apply a binary OR between the contents of two BitArrays of equal size. */
    [[nodiscard]] friend constexpr BitArray operator|(
        const BitArray& lhs,
        const BitArray& rhs
    ) noexcept {
        BitArray result;
        for (size_type i = 0; i < array_size; ++i) {
            result.m_data[i] = lhs.m_data[i] | rhs.m_data[i];
        }
        return result;
    }
    constexpr BitArray& operator|=(
        const BitArray& other
    ) noexcept {
        for (size_type i = 0; i < array_size; ++i) {
            m_data[i] |= other.m_data[i];
        }
        return *this;
    }

    /* Apply a binary XOR between the contents of two BitArrays of equal size. */
    [[nodiscard]] friend constexpr BitArray operator^(
        const BitArray& lhs,
        const BitArray& rhs
    ) noexcept {
        BitArray result;
        for (size_type i = 0; i < array_size; ++i) {
            result.m_data[i] = lhs.m_data[i] ^ rhs.m_data[i];
        }
        return result;
    }
    constexpr BitArray& operator^=(
        const BitArray& other
    ) noexcept {
        for (size_type i = 0; i < array_size; ++i) {
            m_data[i] ^= other.m_data[i];
        }
        return *this;
    }

    /* Apply a binary left shift to the contents of the BitArray. */
    [[nodiscard]] constexpr BitArray operator<<(size_type rhs) const noexcept {
        BitArray result;
        size_type shift = rhs / word_size;
        if (shift < array_size) {
            size_type remainder = rhs % word_size;
            for (size_type i = array_size; i-- > shift + 1;) {
                size_type offset = i - shift;
                result.m_data[i] = (m_data[offset] << remainder) |
                    (m_data[offset - 1] >> (word_size - remainder));
            }
            result.m_data[shift] = m_data[0] << remainder;
            if constexpr (N % word_size) {
                constexpr word mask = (word(1) << (N % word_size)) - 1;
                result.m_data[array_size - 1] &= mask;
            }
        }
        return result;
    }
    constexpr BitArray& operator<<=(size_type rhs) noexcept {
        size_type shift = rhs / word_size;
        if (shift < array_size) {
            size_type remainder = rhs % word_size;
            for (size_type i = array_size; i-- > shift + 1;) {
                size_type offset = i - shift;
                m_data[i] = (m_data[offset] << remainder) |
                    (m_data[offset - 1] >> (word_size - remainder));
            }
            m_data[shift] = m_data[0] << remainder;
            for (size_type i = shift; i-- > 0;) {
                m_data[i] = 0;
            }
            if constexpr (N % word_size) {
                constexpr word mask = (word(1) << (N % word_size)) - 1;
                m_data[array_size - 1] &= mask;
            }
        } else {
            for (size_type i = 0; i < array_size; ++i) {
                m_data[i] = 0;
            }
        }
        return *this;
    }

    /* Apply a binary right shift to the contents of the BitArray. */
    [[nodiscard]] constexpr BitArray operator>>(size_type rhs) const noexcept {
        BitArray result;
        size_type shift = rhs / word_size;
        if (shift < array_size) {
            size_type end = array_size - shift - 1;
            size_type remainder = rhs % word_size;
            for (size_type i = 0; i < end; ++i) {
                size_type offset = i + shift;
                result.m_data[i] = (m_data[offset] >> remainder) |
                    (m_data[offset + 1] << (word_size - remainder));
            }
            result.m_data[end] = m_data[array_size - 1] >> remainder;
        }
        return result;
    }
    constexpr BitArray& operator>>=(size_type rhs) noexcept {
        size_type shift = rhs / word_size;
        if (shift < array_size) {
            size_type end = array_size - shift - 1;
            size_type remainder = rhs % word_size;
            for (size_type i = 0; i < end; ++i) {
                size_type offset = i + shift;
                m_data[i] = (m_data[offset] >> remainder) |
                    (m_data[offset + 1] << (word_size - remainder));
            }
            m_data[end] = m_data[array_size - 1] >> remainder;
            for (size_type i = array_size - shift; i < array_size; ++i) {
                m_data[i] = 0;
            }
        } else {
            for (size_type i = 0; i < array_size; ++i) {
                m_data[i] = 0;
            }
        }
        return *this;
    }

    /* Print the BitArray to an output stream. */
    constexpr friend std::ostream& operator<<(std::ostream& os, const BitArray& set)
        noexcept(noexcept(os << std::string(set)))
    {
        os << std::string(set);
        return os;
    }


    /// TODO: input streams should read the BitArray in big-endian order.


    /* Read the BitArray from an input stream. */
    constexpr friend std::istream& operator>>(std::istream& is, BitArray& set) {
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
BitArray(Ts...) -> BitArray<impl::bitcount<Ts...>>;
template <size_t N>
BitArray(const char(&)[N]) -> BitArray<N - 1>;
template <size_t N>
BitArray(const char(&)[N], char) -> BitArray<N - 1>;
template <size_t N>
BitArray(const char(&)[N], char, char) -> BitArray<N - 1>;


}  // namespace bertrand


namespace std {

    /* Specializing `std::hash` allows BitArrays to be stored in hash tables. */
    template <bertrand::meta::BitArray T>
    struct hash<T> {
        [[nodiscard]] static size_t operator()(const T& BitArray) noexcept {
            size_t result = 0;
            for (size_t i = 0; i < std::remove_cvref_t<T>::size(); ++i) {
                result ^= bertrand::impl::hash_combine(
                    result,
                    BitArray.data()[i]
                );
            }
            return result;
        }
    };

    /* Specializing `std::tuple_size` allows BitArrays to be decomposed using
    structured bindings. */
    template <bertrand::meta::BitArray T>
    struct tuple_size<T> :
        std::integral_constant<size_t, std::remove_cvref_t<T>::size()>
    {};

    /* Specializing `std::tuple_element` allows BitArrays to be decomposed using
    structured bindings. */
    template <size_t I, bertrand::meta::BitArray T>
        requires (I < std::remove_cvref_t<T>::size())
    struct tuple_element<I, T> { using type = bool; };

    /* `std::get<I>(chain)` extracts the I-th flag from the BitArray. */
    template <size_t I, bertrand::meta::BitArray T>
        requires (I < std::remove_cvref_t<T>::size())
    [[nodiscard]] constexpr bool get(T&& BitArray) noexcept {
        return std::forward<T>(BitArray).template get<I>();
    }

}


namespace bertrand {

    inline void test() {
        {
            static constexpr BitArray<2> a{uint8_t(0b1010)};
            static constexpr BitArray a2{false, true};
            static constexpr BitArray a3{1, 2};
            static constexpr BitArray a4{uint8_t(0), true};
            static_assert(a4.count() == 1);
            static_assert(a3.size() == 64);
            static_assert(a == a2);
            auto [f1, f2] = a;
            static constexpr BitArray b = {"abab", 'b', 'a'};
            static constexpr std::string c = b.to_binary();
            static constexpr std::string d = b.to_decimal();
            static constexpr std::string d2 = b.to_string();
            static constexpr std::string d3 = b.to_hex();
            static_assert(any(b.components()));
            static_assert(b.first_one(2).value() == 3);
            static_assert(a == uint8_t(0b10));
            static_assert(b == uint8_t(0b1010));
            static_assert(b[1].result() == true);
            static_assert(c == "1010");
            static_assert(d == "10");
            static_assert(d2 == "1010");  // TODO: incorrect for binary, this is reporting as big endian
            static_assert(d3 == "A");
            auto val = a.get(2);

            static_assert(std::same_as<typename BitArray<2>::word, uint8_t>);
            static_assert(sizeof(BitArray<2>) == 1);

            constexpr auto x = []() {
                BitArray<4> out;
                out[-1].result() = true;
                return out;
            }();
            static_assert(x == uint8_t(0b1000));

            for (auto&& x : a.components()) {

            }
        }

        {
            static constexpr BitArray b = "100";
            static constexpr BitArray<3> b2 {std::string_view("100")};
            static_assert(b.data()[0] == uint8_t(4));
            static_assert(b2.data()[0] == uint8_t(4));
        }
    }

}


#endif  // BERTRAND_BITSET_H
