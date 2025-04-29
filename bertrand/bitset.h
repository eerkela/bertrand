#ifndef BERTRAND_BITSET_H
#define BERTRAND_BITSET_H

#include "bertrand/common.h"
#include "bertrand/except.h"
#include "bertrand/math.h"
#include "bertrand/iter.h"


namespace bertrand {


namespace impl {
    struct bitset_tag {};

    template <size_t M>
    struct Word {
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
    struct Word<M> {
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
    struct Word<M> {
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
    struct Word<M> {
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

}


namespace meta {
    template <typename T>
    concept bitset = inherits<T, impl::bitset_tag>;
}


/* A simple bitset type that stores flags in a fixed-size array of machine words.
Allows a wider range of operations than `std::bitset<N>`, including full, bigint-style
arithmetic, lexicographic comparisons, one-hot decomposition, and more, which allow
bitsets to pull double duty as portable, unsigned integers of arbitrary width. */
template <size_t N>
struct bitset : impl::bitset_tag {
    using Word = impl::Word<N>::type;
    using size_type = size_t;
    using index_type = ssize_t;
    struct Ref;

private:
    using BigWord = impl::Word<N>::big;
    static constexpr size_type word_size = impl::Word<N>::size;
    static constexpr size_type array_size = (N + word_size - 1) / word_size;

    std::array<Word, array_size> m_data;

    static constexpr void _divmod(
        const bitset& lhs,
        const bitset& rhs,
        bitset& quotient,
        bitset& remainder
    ) {
        if constexpr (N <= word_size) {
            Word l = lhs.m_data[0];
            Word r = rhs.m_data[0];
            quotient.m_data[0] = l / r;
            remainder.m_data[0] = l % r;
            return;
        }

        using Signed = meta::as_signed<Word>;
        constexpr size_type chunk = word_size / 2;
        constexpr Word b = Word(1) << chunk;

        if (!rhs) {
            throw ZeroDivisionError();
        }
        if (lhs < rhs || !lhs) {
            remainder = lhs;
            quotient.fill(0);
            return;
        }

        // 1. Compute effective lengths.
        size_type lhs_last = lhs.last_one().value_or(N);
        size_type rhs_last = rhs.last_one().value_or(N);
        size_type n = (rhs_last + (word_size - 1)) / word_size;
        size_type m = ((lhs_last + (word_size - 1)) / word_size) - n;

        // 2. If the divisor is a single word, then we can avoid multi-word division.
        if (n == 1) {
            Word v = rhs.m_data[0];
            Word rem = 0;
            for (size_type i = m + n; i-- > 0;) {
                auto wide = BigWord{rem, lhs.m_data[i]} / v;
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
        std::array<Word, array_size> v = rhs.m_data;
        std::array<Word, array_size + 1> u;
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
            auto hat = BigWord{u[j + n], u[j + n - 1]} / v[n - 1];
            Word qhat = hat.hi();
            Word rhat = hat.lo();

            // refine quotient if guess is too large
            while (qhat >= b || (
                BigWord::mul(qhat, v[n - 2]) > BigWord{Word(rhat * b), u[j + n - 2]}
            )) {
                --qhat;
                rhat += v[n - 1];
                if (rhat >= b) {
                    break;
                }
            }

            // 5. Multiply and subtract
            Word borrow = 0;
            for (size_type i = 0; i < n; ++i) {
                BigWord prod = BigWord::mul(qhat, v[i]);
                Word temp = u[i + j] - borrow;
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
                    Word temp = u[i + j] + borrow;
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

    constexpr bool subscript(size_type i) const noexcept {
        return m_data[i / word_size] & (Word(1) << (i % word_size));
    }

public:
    /* Construct an empty bitset initialized to zero. */
    constexpr bitset() noexcept : m_data{} {}

    /* Construct a bitset from an integer value.  Note that the value will be parsed
    as if it were little-endian, meaning least significant bit first (e.g. `bitset[0]`
    corresponds to the ones place, `bitset[1]` to the twos place, `bitset[2]` to the
    fours place, and so on).  If the initializer has any bits set above index `N`,
    they will be masked off and initialized to zero. */
    constexpr bitset(Word value) noexcept : m_data{} {
        if constexpr (N < word_size) {
            constexpr Word mask = (Word(1) << N) - Word(1);
            m_data[0] = value & mask;
        } else {
            m_data[0] = value;
        }
    }

    /* Construct a bitset from a string of true and false substrings.  Throws a
    `ValueError` if the string contains any substrings other than the indicated ones,
    or if either of the substrings are empty.  If the string contains fewer than `N`
    substrings, the remaining bits will be initialized to zero.  If it contains more
    than `N`, the extra substrings will be ignored.  Note that the value will be parsed
    as if it were little-endian, meaning the first character of the string corresponds
    to the least significant bit, which equates to `bitset[0]`, and so forth. */
    constexpr bitset(
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
        for (size_type i = 0, j = 0; i < str.size() && j < N;) {
            if (str.substr(i, zero.size()) == zero) {
                i += zero.size();
                ++j;
            } else if (str.substr(i, one.size()) == one) {
                m_data[j / word_size] |= (Word(1) << (j % word_size));
                i += one.size();
                ++j;
            } else {
                throw ValueError(
                    "bitset string must contain only '" + std::string(zero) +
                    "' and '" + std::string(one) + "', not: '" +
                    std::string(str.substr(i, max(zero.size(), one.size()))) +
                    "'"
                );
            }
        }
    }

    /* Construct a bitset from a string literal of true and false substrings, allowing
    for CTAD.  Throws a `ValueError` if the string contains any substrings other than
    the indicated ones, or if either of the substrings are empty. */
    constexpr bitset(
        const char(&str)[N + 1],
        std::string_view zero = "0",
        std::string_view one = "1"
    ) : bitset(std::string_view{str, N}, zero, one) {}

    /* Bitsets evalute true if any of their bits are set. */
    [[nodiscard]] explicit constexpr operator bool() const noexcept {
        return any();
    }

    /* Convert the bitset to an integer representation if it fits within the platform's
    word size. */
    template <typename T>
        requires (N <= word_size && meta::explicitly_convertible_to<Word, T>)
    [[nodiscard]] explicit constexpr operator T() const noexcept {
        if constexpr (array_size == 0) {
            return static_cast<T>(Word(0));
        } else {
            return static_cast<T>(m_data[0]);
        }
    }

    /* Convert the bitset to a string representation with '1' as the true character and
    '0' as the false character.  Note that the characters in the output string are
    index-aligned to the values in this bitset, and can be passed to the bitset
    constructor to recover the original state. */
    [[nodiscard]] explicit constexpr operator std::string() const noexcept {
        constexpr int diff = '1' - '0';
        std::string result;
        result.reserve(N);
        for (size_type i = 0; i < N; ++i) {
            result.push_back('0' + diff * subscript(i));
        }
        return result;
    }

    /* Convert the bitset into a string representation.  Defaults to base 2 with the
    given zero and one characters, but also allows bases up to 36, in which case the
    zero and one characters will be ignored.  The result is always padded to the exact
    width needed to represent the bitset in the chosen base, including leading zeroes
    if needed.  Note that base 2 is reported in little-endian order, meaning the
    leftmost substring represents the least significant bit, and all substrings are
    index-aligned to the values in this bitset.  Passing the resulting binary string
    to the bitset constructor will recover the original state.  Throws a `ValueError`
    if either substring is empty. */
    [[nodiscard]] constexpr std::string to_string(
        size_type base = 2,
        std::string_view zero = "0",
        std::string_view one = "1"
    ) const {
        if (base < 2) {
            throw ValueError("bitset base must be at least 2");
        } else if (base > 36) {
            throw ValueError("bitset base must be at most 36");
        }

        size_type min_len = min(zero.size(), one.size());
        if (min_len == 0) {
            if (one.empty()) {
                throw ValueError("`one` substring must not be empty");
            } else {
                throw ValueError("`zero` substring must not be empty");
            }
        }
        if (base == 2) {
            std::string result;
            result.reserve(N * min_len);
            for (size_type i = 0; i < N; ++i) {
                if (subscript(i)) {
                    result.append(one);
                } else {
                    result.append(zero);
                }
            }
            return result;
        }

        constexpr char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
        constexpr double log2[] = {
            0.0, 0.0, 1.0, 1.5849625007211563, 2.0,
            2.321928094887362, 2.584962500721156, 2.807354922057604,
            3.0, 3.169925001442312, 3.321928094887362,
            3.4594316186372973, 3.584962500721156, 3.700439718141092,
            3.807354922057604, 3.9068905956085187, 4.0,
            4.087462841250339, 4.169925001442312, 4.247927513443585,
            4.321928094887363, 4.392317422778761, 4.459431618637297,
            4.523561956057013, 4.584962500721156, 4.643856189774724,
            4.700439718141092, 4.754887502163469, 4.807354922057604,
            4.857980995127572, 4.906890595608518, 4.954196310386875,
            5.0, 5.044394119358453, 5.087462841250339,
            5.129283016944966, 5.169925001442312
        };
        double len = N / log2[base];
        size_type ceil = len;
        ceil += ceil < len;
        std::string result(ceil, '0');
        size_type i = 0;
        bitset quotient = *this;
        bitset divisor = base;
        bitset remainder;
        for (size_type i = 0; quotient; ++i) {
            _divmod(quotient, divisor, quotient, remainder);
            result[ceil - i - 1] = digits[remainder.m_data[0]];
        }
        return result;
    }

    /* The number of bits that are held in the set. */
    [[nodiscard]] static constexpr size_type size() noexcept { return N; }
    [[nodiscard]] static constexpr index_type ssize() noexcept { return index_type(N); }

    /* The total number of bits that were allocated.  This will always be a multiple of
    the machine's word size, or a power of two less than that. */
    [[nodiscard]] static constexpr size_type capacity() noexcept {
        return array_size * word_size;
    }

    /* Get the underlying array that backs the bitset. */
    [[nodiscard]] constexpr auto& data() noexcept { return m_data; }
    [[nodiscard]] constexpr const auto& data() const noexcept { return m_data; }

    /* Check if any of the bits are set. */
    [[nodiscard]] constexpr bool any() const noexcept {
        for (size_type i = 0; i < array_size; ++i) {
            if (m_data[i]) {
                return true;
            }
        }
        return false;
    }

    /* Check if any of the bits are set within a particular range. */
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
        bitset temp;
        temp.fill(1);
        temp <<= N - size_type(norm_stop);
        temp >>= N - size_type(norm_stop - norm_start);
        temp <<= size_type(norm_start);  // [start, stop).
        return *this & temp;
    }

    /* Check if all of the bits are set. */
    [[nodiscard]] constexpr bool all() const noexcept {
        constexpr bool odd = N % word_size;
        for (size_type i = 0; i < array_size - odd; ++i) {
            if (m_data[i] != std::numeric_limits<Word>::max()) {
                return false;
            }
        }
        if constexpr (odd) {
            constexpr Word mask = (Word(1) << (N % word_size)) - 1;
            return m_data[array_size - 1] == mask;
        } else {
            return true;
        }
    }

    /* Check if all of the bits are set within a particular range. */
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
        bitset temp;
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

    /* Get the number of bits that are currently set within a particular range. */
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
        bitset temp;
        temp.fill(1);
        temp <<= N - size_type(norm_stop);
        temp >>= N - size_type(norm_stop - norm_start);
        temp <<= size_type(norm_start);  // [start, stop).
        size_type count = 0;
        for (size_type i = 0; i < array_size; ++i) {
            count += std::popcount(m_data[i] & temp.m_data[i]);
        }
        return count;
    }

    /* Return the index of the first bit that is set, or the size of the array if no
    bits are set. */
    [[nodiscard]] constexpr Optional<index_type> first_one() const noexcept {
        for (size_type i = 0; i < array_size; ++i) {
            Word curr = m_data[i];
            if (curr) {
                return index_type(word_size * i  + std::countr_zero(curr));
            }
        }
        return std::nullopt;
    }

    /* Return the index of the first bit that is set within a given range, or the size
    of the array if no bits are set. */
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
        bitset temp;
        temp.fill(1);
        temp <<= N - size_type(norm_stop);
        temp >>= N - size_type(norm_stop - norm_start);
        temp <<= size_type(norm_start);  // [start, stop).
        for (size_type i = 0; i < array_size; ++i) {
            Word curr = m_data[i] & temp.m_data[i];
            if (curr) {
                return index_type(word_size * i + std::countr_zero(curr));
            }
        }
        return std::nullopt;
    }

    /* Return the index of the last bit that is set, or the size of the array if no
    bits are set. */
    [[nodiscard]] constexpr Optional<index_type> last_one() const noexcept {
        for (size_type i = array_size; i-- > 0;) {
            Word curr = m_data[i];
            if (curr) {
                return index_type(
                    word_size * i + word_size - 1 - std::countl_zero(curr)
                );
            }
        }
        return std::nullopt;
    }

    /* Return the index of the last bit that is set within a given range, or the size
    of the array if no bits are set. */
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
        bitset temp;
        temp.fill(1);
        temp <<= N - size_type(norm_stop);
        temp >>= N - size_type(norm_stop - norm_start);
        temp <<= size_type(norm_start);  // [start, stop).
        for (size_type i = array_size; i-- > 0;) {
            Word curr = m_data[i] & temp.m_data[i];
            if (curr) {
                return index_type(
                    word_size * i + word_size - 1 - std::countl_zero(curr)
                );
            }
        }
        return std::nullopt;
    }

    /* Return the index of the first bit that is not set, or the size of the array if
    all bits are set. */
    [[nodiscard]] constexpr Optional<index_type> first_zero() const noexcept {
        for (size_type i = 0; i < array_size; ++i) {
            Word curr = m_data[i];
            if (curr != std::numeric_limits<Word>::max()) {
                return index_type(word_size * i + std::countr_one(curr));
            }
        }
        return std::nullopt;
    }

    /* Return the index of the first bit that is not set within a given range, or the
    size of the array if all bits are set. */
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
        bitset temp;
        temp.fill(1);
        temp <<= N - size_type(norm_stop);
        temp >>= N - size_type(norm_stop - norm_start);
        temp <<= size_type(norm_start);  // [start, stop).
        for (size_type i = 0; i < array_size; ++i) {
            Word curr = m_data[i] & temp.m_data[i];
            if (curr != temp.m_data[i]) {
                return index_type(word_size * i + std::countr_one(curr));
            }
        }
        return std::nullopt;
    }

    /* Return the index of the last bit that is not set, or the size of the array if
    all bits are set. */
    [[nodiscard]] constexpr Optional<index_type> last_zero() const noexcept {
        constexpr bool odd = N % word_size;
        if constexpr (odd) {
            constexpr Word mask = (Word(1) << (N % word_size)) - 1;
            Word curr = m_data[array_size - 1];
            if (curr != mask) {
                return index_type(
                    word_size * (array_size - 1) +
                    word_size - 1 -
                    std::countl_one(curr | ~mask)
                );
            }
        }
        for (size_type i = array_size - odd; i-- > 0;) {
            Word curr = m_data[i];
            if (curr != std::numeric_limits<Word>::max()) {
                return index_type(
                    word_size * i + word_size - 1 - std::countl_one(curr)
                );
            }
        }
        return std::nullopt;
    }

    /* Return the index of the last bit that is not set within a given range, or the
    size of the array if all bits are set. */
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
        bitset temp;
        temp.fill(1);
        temp <<= N - size_type(norm_stop);
        temp >>= N - size_type(norm_stop - norm_start);
        temp <<= size_type(norm_start);  // [start, stop).
        for (size_type i = array_size; i-- > 0;) {
            Word curr = m_data[i] & temp.m_data[i];
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
    constexpr bitset& fill(bool value) noexcept {
        constexpr bool odd = N % word_size;
        Word filled = std::numeric_limits<Word>::max() * value;
        for (size_type i = 0; i < array_size - odd; ++i) {
            m_data[i] = filled;
        }
        if constexpr (odd) {
            constexpr Word mask = (Word(1) << (N % word_size)) - 1;
            m_data[array_size - 1] = filled & mask;
        }
        return *this;
    }

    /* Set all of the bits within a certain range to the given value. */
    constexpr bitset& fill(
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
        bitset temp;
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
    constexpr bitset& flip() noexcept {
        constexpr bool odd = N % word_size;
        for (size_type i = 0; i < array_size - odd; ++i) {
            m_data[i] ^= std::numeric_limits<Word>::max();
        }
        if constexpr (odd) {
            constexpr Word mask = (Word(1) << (N % word_size)) - 1;
            m_data[array_size - 1] ^= mask;
        }
        return *this;
    }

    /* Toggle all of the bits within a certain range. */
    constexpr bitset& flip(index_type start, index_type stop = ssize()) noexcept {
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
        bitset temp;
        temp.fill(1);
        temp <<= N - size_type(norm_stop);
        temp >>= N - size_type(norm_stop - norm_start);
        temp <<= size_type(norm_start);  // [start, stop).
        *this ^= temp;
        return *this;
    }

    /* A mutable reference to a single bit in the set. */
    struct Ref {
    private:
        friend bitset;

        Word& value;
        Word index;

        constexpr Ref(Word& value, Word index) noexcept :
            value(value),
            index(index)
        {}

    public:
        [[nodiscard]] constexpr operator bool() const noexcept {
            return value & (Word(1) << index);
        }

        [[nodiscard]] constexpr bool operator~() const noexcept {
            return !*this;
        }

        constexpr Ref& operator=(bool x) noexcept {
            value = (value & ~(Word(1) << index)) | (x << index);
            return *this;
        }

        constexpr Ref& flip() noexcept {
            value ^= Word(1) << index;
            return *this;
        }
    };

    /* Get the value of a specific bit in the set, without bounds checking. */
    [[nodiscard]] constexpr Expected<bool, IndexError> operator[](
        index_type index
    ) const noexcept {
        index_type i = index + ssize() * (index < 0);
        if (i < 0 || i >= ssize()) {
            return IndexError(std::to_string(index));
        }
        return m_data[i / word_size] & (Word(1) << (i % word_size));
    }
    [[nodiscard]] constexpr Expected<Ref, IndexError> operator[](
        index_type index
    ) noexcept {
        index_type i = index + ssize() * (index < 0);
        if (i < 0 || i >= ssize()) {
            return IndexError(std::to_string(index));
        }
        return Ref{m_data[i / word_size], Word(i % word_size)};
    }

    /* Get the value of a specific bit in the set, performing a bounds check on the
    way.  Also available as `std::get<I>(bitset)`, which allows for structured
    bindings. */
    template <size_type I> requires (I < N)
    [[nodiscard]] constexpr bool get() const noexcept {
        return subscript(I);
    }
    template <size_type I> requires (I < N)
    [[nodiscard]] constexpr Ref get() noexcept {
        return {m_data[I / word_size], Word(I % word_size)};
    }

    /// TODO: get(i) acts like subscript, but throws an IndexError as a debug
    /// assertion.

    [[nodiscard]] constexpr bool get(size_type index) const {
        if (index >= N) {
            throw IndexError(std::to_string(index));
        }
        return subscript(index);
    }
    [[nodiscard]] constexpr Ref get(size_type index) {
        if (index >= N) {
            throw IndexError(std::to_string(index));
        }
        return {m_data[index / word_size], Word(index % word_size)};
    }

    /// TODO: at(i) returns an iterator


    /// TODO: the Components range should take index_type instead of size_type

    /* A range that decomposes a bitmask into its one-hot components. */
    struct Components {
    private:
        friend bitset;
        const bitset* self;
        size_type first;
        size_type last;

        Components(const bitset* self, size_type first, size_type last) noexcept :
            self(self), first(first), last(last)
        {}

    public:
        struct Iterator {
        private:
            friend Components;

            /// TODO: index should be stored as index_type I think

            const bitset* self;
            size_type index;
            size_type last;
            bitset curr;

            Iterator(const bitset* self, size_type first, size_type last) noexcept :
                self(self),
                index(self->first_one(first, last).value_or(N)),
                last(last),
                curr{1}
            {
                curr <<= index;
            }

        public:
            using iterator_category = std::forward_iterator_tag;
            using difference_type = std::ptrdiff_t;
            using value_type = bitset;
            using pointer = const bitset*;
            using reference = const bitset&;

            constexpr reference operator*() const noexcept {
                return curr;
            }

            constexpr pointer operator->() const noexcept {
                return &curr;
            }

            constexpr Iterator& operator++() noexcept {
                size_type new_index = self->first_one(index + 1).value_or(N);
                curr <<= new_index - index;
                index = new_index;
                return *this;
            }

            constexpr Iterator operator++(int) noexcept {
                Iterator copy = *this;
                ++*this;
                return copy;
            }

            friend constexpr bool operator==(const Iterator& self, impl::sentinel) noexcept {
                return self.index >= self.last;
            }

            friend constexpr bool operator==(impl::sentinel, const Iterator& self) noexcept {
                return self.index >= self.last;
            }

            friend constexpr bool operator!=(const Iterator& self, impl::sentinel) noexcept {
                return self.index < self.last;
            }

            friend constexpr bool operator!=(impl::sentinel, const Iterator& self) noexcept {
                return self.index < self.last;
            }
        };

        Iterator begin() const noexcept {
            return {self, first, last};
        }

        impl::sentinel end() const noexcept {
            return {};
        }
    };

    /* Return a view over the one-hot masks that make up the bitset within a given
    range. */
    [[nodiscard]] constexpr Components components(
        size_type first = 0,
        size_type last = N
    ) const noexcept {
        return {this, first, std::min(last, N)};
    }

    /* An iterator over the individual bits within the set. */
    struct Iterator {
    private:
        friend bitset;

        index_type index;
        bitset* self;
        mutable Ref cache;

        Iterator(bitset* self, index_type index) noexcept : self(self), index(index) {}

    public:
        using iterator_category = std::random_access_iterator_tag;
        using difference_type = index_type;
        using value_type = Ref;
        using pointer = Ref*;
        using reference = Ref;

        /// TODO: these dereference operators are incorrect.  The base subscript
        /// operator now returns an Expected

        [[nodiscard]] constexpr reference operator*() const noexcept {
            return (*self)[static_cast<size_t>(index)];
        }

        [[nodiscard]] constexpr const pointer operator->() const noexcept {
            cache = **this;
            return &cache;
        }

        [[nodiscard]] constexpr pointer operator->() noexcept {
            cache = **this;
            return &cache;
        }

        [[nodiscard]] constexpr reference operator[](difference_type n) const noexcept {
            return (*self)[static_cast<size_t>(index + n)];
        }

        constexpr Iterator& operator++() noexcept {
            ++index;
            return *this;
        }

        constexpr Iterator operator++(int) noexcept {
            Iterator copy = *this;
            ++index;
            return copy;
        }

        constexpr Iterator& operator+=(difference_type n) noexcept {
            index += n;
            return *this;
        }

        [[nodiscard]] friend constexpr Iterator operator+(
            const Iterator& lhs,
            difference_type rhs
        ) noexcept {
            return {lhs.self, lhs.index + rhs};
        }

        [[nodiscard]] friend constexpr Iterator operator+(
            difference_type lhs,
            const Iterator& rhs
        ) noexcept {
            return {rhs.self, rhs.index + lhs};
        }

        constexpr Iterator& operator--() noexcept {
            --index;
            return *this;
        }

        constexpr Iterator operator--(int) noexcept {
            Iterator copy = *this;
            --index;
            return copy;
        }

        constexpr Iterator& operator-=(difference_type n) noexcept {
            index -= n;
            return *this;
        }

        [[nodiscard]] friend constexpr Iterator operator-(
            const Iterator& lhs,
            difference_type rhs
        ) noexcept {
            return {lhs.self, lhs.index - rhs};
        }

        [[nodiscard]] friend constexpr Iterator operator-(
            difference_type lhs,
            const Iterator& rhs
        ) noexcept {
            return {rhs.self, lhs - rhs.index};
        }

        [[nodiscard]] friend constexpr difference_type operator-(
            const Iterator& lhs,
            const Iterator& rhs
        ) noexcept {
            return lhs.index - rhs.index;
        }

        [[nodiscard]] friend constexpr auto operator<=>(
            const Iterator& lhs,
            const Iterator& rhs
        ) noexcept {
            return lhs.index <=> rhs.index;
        }

        [[nodiscard]] friend constexpr bool operator==(
            const Iterator& lhs,
            const Iterator& rhs
        ) noexcept {
            return lhs.index == rhs.index;
        }
    };

    /* A read-only iterator over the individual bits within the set. */
    struct ConstIterator {
    private:
        friend bitset;

        index_type index;
        const bitset* self;
        mutable bool cache;

        ConstIterator(const bitset* self, index_type index) noexcept :
            self(self),
            index(index)
        {}

    public:
        using iterator_category = std::random_access_iterator_tag;
        using difference_type = index_type;
        using value_type = bool;
        using pointer = const bool*;
        using reference = bool;

        [[nodiscard]] constexpr bool operator*() const noexcept {
            return (*self)[index];
        }

        [[nodiscard]] constexpr pointer operator->() const noexcept {
            cache = **this;
            return &cache;
        }

        [[nodiscard]] constexpr reference operator[](difference_type n) const noexcept {
            return (*self)[index + n];
        }

        constexpr ConstIterator& operator++() noexcept {
            ++index;
            return *this;
        }

        constexpr ConstIterator operator++(int) noexcept {
            ConstIterator copy = *this;
            ++index;
            return copy;
        }

        constexpr ConstIterator& operator+=(difference_type n) noexcept {
            index += n;
            return *this;
        }

        [[nodiscard]] friend constexpr ConstIterator operator+(
            const ConstIterator& lhs,
            difference_type rhs
        ) noexcept {
            return {lhs.self, lhs.index + rhs};
        }

        [[nodiscard]] friend constexpr ConstIterator operator+(
            difference_type lhs,
            const ConstIterator& rhs
        ) noexcept {
            return {rhs.self, rhs.index + lhs};
        }

        constexpr ConstIterator& operator--() noexcept {
            --index;
            return *this;
        }

        constexpr ConstIterator operator--(int) noexcept {
            ConstIterator copy = *this;
            --index;
            return copy;
        }

        constexpr ConstIterator& operator-=(difference_type n) noexcept {
            index -= n;
            return *this;
        }

        [[nodiscard]] friend constexpr ConstIterator operator-(
            const ConstIterator& lhs,
            difference_type rhs
        ) noexcept {
            return {lhs.self, lhs.index - rhs};
        }

        [[nodiscard]] friend constexpr ConstIterator operator-(
            difference_type lhs,
            const ConstIterator& rhs
        ) noexcept {
            return {rhs.self, lhs - rhs.index};
        }

        [[nodiscard]] friend constexpr difference_type operator-(
            const ConstIterator& lhs,
            const ConstIterator& rhs
        ) noexcept {
            return lhs.index - rhs.index;
        }

        [[nodiscard]] friend constexpr bool operator<(
            const ConstIterator& lhs,
            const ConstIterator& rhs
        ) noexcept {
            return lhs.index < rhs.index;
        }

        [[nodiscard]] friend constexpr bool operator<=(
            const ConstIterator& lhs,
            const ConstIterator& rhs
        ) noexcept {
            return lhs.index <= rhs.index;
        }

        [[nodiscard]] friend constexpr bool operator==(
            const ConstIterator& lhs,
            const ConstIterator& rhs
        ) noexcept {
            return lhs.index == rhs.index;
        }

        [[nodiscard]] friend constexpr bool operator!=(
            const ConstIterator& lhs,
            const ConstIterator& rhs
        ) noexcept {
            return lhs.index != rhs.index;
        }

        [[nodiscard]] friend constexpr bool operator>=(
            const ConstIterator& lhs,
            const ConstIterator& rhs
        ) noexcept {
            return lhs.index >= rhs.index;
        }

        [[nodiscard]] friend constexpr bool operator>(
            const ConstIterator& lhs,
            const ConstIterator& rhs
        ) noexcept {
            return lhs.index > rhs.index;
        }
    };

    using ReverseIterator = std::reverse_iterator<Iterator>;
    using ConstReverseIterator = std::reverse_iterator<ConstIterator>;

    [[nodiscard]] constexpr Iterator begin() noexcept { return {this, 0}; }
    [[nodiscard]] constexpr ConstIterator begin() const noexcept { return {this, 0}; }
    [[nodiscard]] constexpr ConstIterator cbegin() const noexcept { return {this, 0}; }
    [[nodiscard]] constexpr Iterator end() noexcept { return {this, N}; }
    [[nodiscard]] constexpr ConstIterator end() const noexcept { return {this, N}; }
    [[nodiscard]] constexpr ConstIterator cend() const noexcept { return {this, N}; }
    [[nodiscard]] constexpr ReverseIterator rbegin() noexcept { return {end()}; }
    [[nodiscard]] constexpr ConstReverseIterator rbegin() const noexcept { return {end()}; }
    [[nodiscard]] constexpr ConstReverseIterator crbegin() const noexcept { return {cend()}; }
    [[nodiscard]] constexpr ReverseIterator rend() noexcept { return {begin()}; }
    [[nodiscard]] constexpr ConstReverseIterator rend() const noexcept { return {begin()}; }
    [[nodiscard]] constexpr ConstReverseIterator crend() const noexcept { return {cbegin()}; }

    /* Lexicographically compare two bitsets of equal size. */
    [[nodiscard]] friend constexpr auto operator<=>(
        const bitset& lhs,
        const bitset& rhs
    ) noexcept {
        return std::lexicographical_compare_three_way(
            lhs.m_data.begin(),
            lhs.m_data.end(),
            rhs.m_data.begin(),
            rhs.m_data.end()
        );
    }

    /* Check whether one bitset is lexicographically equal to another of the same
    size. */
    [[nodiscard]] friend constexpr bool operator==(
        const bitset& lhs,
        const bitset& rhs
    ) noexcept {
        for (size_type i = 0; i < array_size; ++i) {
            if (lhs.m_data[i] != rhs.m_data[i]) {
                return false;
            }
        }
        return true;
    }

    /* Add two bitsets of equal size. */
    [[nodiscard]] friend constexpr bitset operator+(
        const bitset& lhs,
        const bitset& rhs
    ) noexcept {
        constexpr bool odd = N % word_size;
        bitset result;
        Word carry = 0;
        for (size_type i = 0; i < array_size - odd; ++i) {
            Word a = lhs.m_data[i] + carry;
            carry = a < lhs.m_data[i];
            Word b = a + rhs.m_data[i];
            carry |= b < a;
            result.m_data[i] = b;
        }
        if constexpr (odd) {
            constexpr Word mask = (Word(1) << (N % word_size)) - 1;
            Word sum = lhs.m_data[array_size - 1] + carry + rhs.m_data[array_size - 1];
            result.m_data[array_size - 1] = sum & mask;
        }
        return result;
    }
    constexpr bitset& operator+=(const bitset& other) noexcept {
        constexpr bool odd = N % word_size;
        Word carry = 0;
        for (size_type i = 0; i < array_size - odd; ++i) {
            Word a = m_data[i] + carry;
            carry = a < m_data[i];
            Word b = a + other.m_data[i];
            carry |= b < a;
            m_data[i] = b;
        }
        if constexpr (odd) {
            constexpr Word mask = (Word(1) << (N % word_size)) - 1;
            Word sum = m_data[array_size - 1] + carry + other.m_data[array_size - 1];
            m_data[array_size - 1] = sum & mask;
        }
        return *this;
    }

    /* Subtract two bitsets of equal size. */
    [[nodiscard]] friend constexpr bitset operator-(
        const bitset& lhs,
        const bitset& rhs
    ) noexcept {
        constexpr bool odd = N % word_size;
        bitset result;
        Word borrow = 0;
        for (size_type i = 0; i < array_size - odd; ++i) {
            Word a = lhs.m_data[i] - borrow;
            borrow = a > lhs.m_data[i];
            Word b = a - rhs.m_data[i];
            borrow |= b > a;
            result.m_data[i] = b;
        }
        if constexpr (odd) {
            constexpr Word mask = (Word(1) << (N % word_size)) - 1;
            Word diff = lhs.m_data[array_size - 1] - borrow - rhs.m_data[array_size - 1];
            result.m_data[array_size - 1] = diff & mask;
        }
        return result;
    }
    constexpr bitset& operator-=(const bitset& other) noexcept {
        constexpr bool odd = N % word_size;
        Word borrow = 0;
        for (size_type i = 0; i < array_size - odd; ++i) {
            Word a = m_data[i] - borrow;
            borrow = a > m_data[i];
            Word b = a - other.m_data[i];
            borrow |= b > a;
            m_data[i] = b;
        }
        if constexpr (odd) {
            constexpr Word mask = (Word(1) << (N % word_size)) - 1;
            Word diff = m_data[array_size - 1] - borrow - other.m_data[array_size - 1];
            m_data[array_size - 1] = diff & mask;
        }
        return *this;
    }

    /* Multiply two bitsets of equal size. */
    [[nodiscard]] friend constexpr bitset operator*(
        const bitset& lhs,
        const bitset& rhs
    ) noexcept {
        /// NOTE: this uses schoolbook multiplication, which is generally fastest for
        /// small set sizes, up to a couple thousand bits.
        constexpr size_type chunk = word_size / 2;
        bitset result;
        Word carry = 0;
        for (size_type i = 0; i < array_size; ++i) {
            Word carry = 0;
            for (size_type j = 0; j + i < array_size; ++j) {
                size_type k = j + i;
                BigWord prod = BigWord::mul(lhs.m_data[i], rhs.m_data[j]);
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
            constexpr Word mask = (Word(1) << (N % word_size)) - 1;
            result.m_data[array_size - 1] &= mask;
        }
        return result;
    }
    constexpr bitset& operator*=(const bitset& other) noexcept {
        *this = *this * other;
        return *this;
    }

    /// TODO: return an Expected<T, ZeroDivisionError>

    /* Divide this bitset by another and return both the quotient and remainder.  This
    is slightly more efficient than doing separate `/` and `%` operations if both are
    needed. */
    [[nodiscard]] constexpr std::pair<bitset, bitset> divmod(const bitset& d) const {
        bitset quotient, remainder;
        _divmod(*this, d, quotient, remainder);
        return {quotient, remainder};
    }
    [[nodiscard]] constexpr bitset divmod(const bitset& d, bitset& remainder) const {
        bitset quotient;
        _divmod(*this, d, quotient, remainder);
        return quotient;
    }
    constexpr void divmod(const bitset& d, bitset& quotient, bitset& remainder) const {
        _divmod(*this, d, quotient, remainder);
    }

    /* Divide this bitset by another in-place, returning the remainder.  This is
    slightly more efficient than doing separate `/=` and `%` operations */
    [[nodiscard]] constexpr bitset idivmod(const bitset& d) {
        bitset remainder;
        _divmod(*this, d, *this, remainder);
        return remainder;
    }
    constexpr void idivmod(const bitset& d, bitset& remainder) {
        _divmod(*this, d, *this, remainder);
    }

    /* Divide two bitsets of equal size. */
    [[nodiscard]] friend constexpr bitset operator/(
        const bitset& lhs,
        const bitset& rhs
    ) {
        bitset quotient, remainder;
        _divmod(lhs, rhs, quotient, remainder);
        return quotient;
    }
    constexpr bitset& operator/=(const bitset& other) {
        bitset remainder;
        _divmod(*this, other, *this, remainder);
        return *this;
    }

    /* Get the remainder after dividing two bitsets of equal size. */
    [[nodiscard]] friend constexpr bitset operator%(
        const bitset& lhs,
        const bitset& rhs
    ) {
        bitset quotient, remainder;
        _divmod(lhs, rhs, quotient, remainder);
        return remainder;
    }
    constexpr bitset& operator%=(const bitset& other) {
        bitset quotient;
        _divmod(*this, other, quotient, *this);
        return *this;
    }

    /* Increment the bitset by one. */
    constexpr bitset& operator++() noexcept {
        if constexpr (N) {
            constexpr bool odd = N % word_size;
            Word carry = m_data[0] == std::numeric_limits<Word>::max();
            ++m_data[0];
            for (size_type i = 1; carry && i < array_size - odd; ++i) {
                carry = m_data[i] == std::numeric_limits<Word>::max();
                ++m_data[i];
            }
            if constexpr (odd) {
                constexpr Word mask = (Word(1) << (N % word_size)) - 1;
                m_data[array_size - 1] = (m_data[array_size - 1] + carry) & mask;
            }
        }
        return *this;
    }
    constexpr bitset operator++(int) noexcept {
        bitset copy = *this;
        ++*this;
        return copy;
    }

    /* Decrement the bitset by one. */
    constexpr bitset& operator--() noexcept {
        if constexpr (N) {
            constexpr bool odd = N % word_size;
            Word borrow = m_data[0] == 0;
            --m_data[0];
            for (size_type i = 1; borrow && i < array_size - odd; ++i) {
                borrow = m_data[i] == 0;
                --m_data[i];
            }
            if constexpr (odd) {
                constexpr Word mask = (Word(1) << (N % word_size)) - 1;
                m_data[array_size - 1] = (m_data[array_size - 1] - borrow) & mask;
            }
        }
        return *this;
    }
    constexpr bitset operator--(int) noexcept {
        bitset copy = *this;
        --*this;
        return copy;
    }

    /* Apply a binary NOT to the contents of the bitset. */
    [[nodiscard]] friend constexpr bitset operator~(const bitset& set) noexcept {
        constexpr bool odd = N % word_size;
        bitset result;
        for (size_type i = 0; i < array_size - odd; ++i) {
            result.m_data[i] = ~set.m_data[i];
        }
        if constexpr (odd) {
            constexpr Word mask = (Word(1) << (N % word_size)) - 1;
            result.m_data[array_size - 1] = ~set.m_data[array_size - 1] & mask;
        }
        return result;
    }

    /* Apply a binary AND between the contents of two bitsets of equal size. */
    [[nodiscard]] friend constexpr bitset operator&(
        const bitset& lhs,
        const bitset& rhs
    ) noexcept {
        bitset result;
        for (size_type i = 0; i < array_size; ++i) {
            result.m_data[i] = lhs.m_data[i] & rhs.m_data[i];
        }
        return result;
    }
    constexpr bitset& operator&=(
        const bitset& other
    ) noexcept {
        for (size_type i = 0; i < array_size; ++i) {
            m_data[i] &= other.m_data[i];
        }
        return *this;
    }

    /* Apply a binary OR between the contents of two bitsets of equal size. */
    [[nodiscard]] friend constexpr bitset operator|(
        const bitset& lhs,
        const bitset& rhs
    ) noexcept {
        bitset result;
        for (size_type i = 0; i < array_size; ++i) {
            result.m_data[i] = lhs.m_data[i] | rhs.m_data[i];
        }
        return result;
    }
    constexpr bitset& operator|=(
        const bitset& other
    ) noexcept {
        for (size_type i = 0; i < array_size; ++i) {
            m_data[i] |= other.m_data[i];
        }
        return *this;
    }

    /* Apply a binary XOR between the contents of two bitsets of equal size. */
    [[nodiscard]] friend constexpr bitset operator^(
        const bitset& lhs,
        const bitset& rhs
    ) noexcept {
        bitset result;
        for (size_type i = 0; i < array_size; ++i) {
            result.m_data[i] = lhs.m_data[i] ^ rhs.m_data[i];
        }
        return result;
    }
    constexpr bitset& operator^=(
        const bitset& other
    ) noexcept {
        for (size_type i = 0; i < array_size; ++i) {
            m_data[i] ^= other.m_data[i];
        }
        return *this;
    }

    /* Apply a binary left shift to the contents of the bitset. */
    [[nodiscard]] constexpr bitset operator<<(size_type rhs) const noexcept {
        bitset result;
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
                constexpr Word mask = (Word(1) << (N % word_size)) - 1;
                result.m_data[array_size - 1] &= mask;
            }
        }
        return result;
    }
    constexpr bitset& operator<<=(size_type rhs) noexcept {
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
                constexpr Word mask = (Word(1) << (N % word_size)) - 1;
                m_data[array_size - 1] &= mask;
            }
        } else {
            for (size_type i = 0; i < array_size; ++i) {
                m_data[i] = 0;
            }
        }
        return *this;
    }

    /* Apply a binary right shift to the contents of the bitset. */
    [[nodiscard]] constexpr bitset operator>>(size_type rhs) const noexcept {
        bitset result;
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
    constexpr bitset& operator>>=(size_type rhs) noexcept {
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

    /* Print the bitset to an output stream. */
    constexpr friend std::ostream& operator<<(std::ostream& os, const bitset& set)
        noexcept(noexcept(os << std::string(set)))
    {
        os << std::string(set);
        return os;
    }

    /* Read the bitset from an input stream. */
    constexpr friend std::istream& operator>>(std::istream& is, bitset& set) {
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


template <meta::integer T>
bitset(T) -> bitset<sizeof(T) * 8>;
template <size_t N>
bitset(const char(&)[N]) -> bitset<N - 1>;


}  // namespace bertrand


namespace std {

    /* Specializing `std::hash` allows bitsets to be stored in hash tables. */
    template <bertrand::meta::bitset T>
    struct hash<T> {
        [[nodiscard]] static size_t operator()(const T& bitset) noexcept {
            size_t result = 0;
            for (size_t i = 0; i < std::remove_cvref_t<T>::size(); ++i) {
                result ^= bertrand::impl::hash_combine(
                    result,
                    bitset.data()[i]
                );
            }
            return result;
        }
    };

    /* Specializing `std::tuple_size` allows bitsets to be decomposed using
    structured bindings. */
    template <bertrand::meta::bitset T>
    struct tuple_size<T> :
        std::integral_constant<size_t, std::remove_cvref_t<T>::size()>
    {};

    /* Specializing `std::tuple_element` allows bitsets to be decomposed using
    structured bindings. */
    template <size_t I, bertrand::meta::bitset T>
        requires (I < std::remove_cvref_t<T>::size())
    struct tuple_element<I, T> { using type = bool; };

    /* `std::get<I>(chain)` extracts the I-th flag from the bitset. */
    template <size_t I, bertrand::meta::bitset T>
        requires (I < std::remove_cvref_t<T>::size())
    [[nodiscard]] constexpr bool get(T&& bitset) noexcept {
        return std::forward<T>(bitset).template get<I>();
    }

}


namespace bertrand {

    inline void test() {
        static constexpr bitset<2> a{0b1010};
        auto [f1, f2] = a;
        static constexpr bitset<4> b = "1010";
        static constexpr std::string c = std::string(b);
        static constexpr std::string d = b.to_string(10);
        static_assert(a == 0b10);
        static_assert(b == 0b0101);
        static_assert(b[0].result() == true);
        static_assert(c == "1010");
        static_assert(d == "05");
        static_assert(d[2] == '\0');

        // static_assert(uint8_t)
        static_assert(std::same_as<typename bitset<2>::Word, uint8_t>);
        static_assert(sizeof(bitset<2>) == 1);

        constexpr auto x = []() {
            bitset<4> out;
            out[-1].result() = true;
            return out;
        }();
        static_assert(x == 0b1000);
    }

}


#endif  // BERTRAND_BITSET_H
