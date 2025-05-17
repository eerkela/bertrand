#ifndef BERTRAND_BITSET_H
#define BERTRAND_BITSET_H

#include "bertrand/common.h"
#include "bertrand/except.h"
#include "bertrand/math.h"
#include "bertrand/iter.h"
#include "bertrand/static_str.h"


namespace bertrand {


/* A simple bitset that stores up to `N` boolean flags as a compressed array of machine
words.  Allows a wider range of operations than `std::bitset<N>`, including full,
bigint-style unsigned arithmetic, lexicographic comparisons, one-hot decomposition,
structured bindings, fast string encoding/decoding, and more.

`Bits` are generally optimized for small to medium sizes (up to a few thousand bits),
and do not replace arbitrary-precision libraries like GMP or `boost::multiprecision`,
which may be preferable in some situations.  Instead, they are meant as a lightweight,
general-purpose utility for low-level bit manipulation and efficient storage of boolean
flags, and as a building block for further abstractions. */
template <size_t N>
struct Bits;


/* A generalized, arbitrary-width, unsigned integer type backed by a bitset of `N`
bits.

If `N` is less than or equal to 64, then the integer will be represented as a single
machine word whose size is the nearest power of 2 greater than or equal to `N`, with
`uint8_t` being the smallest possible type.  If `N` is not an exact power of 2, then
the upper bits will be masked out to give true `N`-bit behavior.  Such single-word
integers are implicitly convertible both to and from their underlying hardware types,
and can be used interchangeably with them in most contexts without degrading
performance or consuming any excess memory.  They can be freely mixed in arithmetic
operations, used as array indices or keys in associative containers, and so on.  They
also exhibit the same overflow semantics as their underlying types, modulo `2^N`.

If `N` is greater than 64, then the integer will instead be represented as an array of
64-bit words, with the least significant word first.  As with single-word integers, any
upper bits of the last word will be masked out to give true `N`-bit behavior.  Such
integers support all of the same arithmetic operations as their single-word
counterparts, but may involve extra overhead due to the need for multi-word arithmetic.
They also cannot be implicitly converted to any underlying hardware type, as their
values may fall outside the representable range of any such type.  Explicit conversions
are available, but risk possible truncation unless the integer is known to be within
range of the target type.  In all other respects, they behave like normal integers,
and can be used wherever large integers above the 64-bit limit may be needed.

This class is mostly meant to bridge the gap between bigint-style Python integers
(which can dynamically grow to accommodate any size) and C++ integers, which are
fixed-width and usually capped at 64 bits.  By generalizing to arbitrary `N`,
Python-style integer pipelines can be lowered directly into C++ without needing a
full multiprecision library like GMP or `boost::multiprecision`, which may be overkill
for many applications.  This class is thus not a full replacement for those libraries,
but rather a lightweight alternative that is optimized for small to medium sizes (up to
a few thousand bits), as a convenience for users accustomed to higher-level
languages.

An equivalent wrapper type is exposed in Python itself as `bertrand.UInt[N]`, which
works in the opposite direction, allowing Python users to work with true `N`-bit
hardware integers in their native format, without requiring multiple distinct types or
complex conversions.  This also erases the semantic differences between C++ and Python
integers, particularly around integer division (defined as floor division in Python vs
truncated division in C++) and possible overflow, which brings Python code into
alignment with extensions written in C-family languages. */
template <size_t N>
struct UInt;


/* A generalized, arbitrary-width, signed integer type backed by a bitset of `N` bits.

If `N` is less than or equal to 64, then the integer will be represented as a single
machine word whose size is the nearest power of 2 greater than or equal to `N`, with
`int8_t` being the smallest possible type.  Negative values are represented using
two's complement, and if `N` is not an exact power of 2, then the upper bits will be
masked out to give true `N`-bit behavior.  Such single-word integers are implicitly
convertible both to and from their underlying hardware types, and can be used
interchangeably with them in most contexts without degrading performance or consuming
any excess memory.  They can be freely mixed in arithmetic operations, used as array
indices or keys in associative containers, and so on.  They also exhibit the same
overflow semantics as their underlying types, modulo `2^N`, with `-2^(N - 1)` as the
minimum value and `2^(N - 1) - 1` as the maximum value.

If `N` is greater than 64, then the integer will instead be represented as an array of
64-bit words, with the least significant word first.  As with single-word integers, any
upper bits of the last word will be masked out to give true `N`-bit behavior.  Such
integers support all of the same arithmetic operations as their single-word
counterparts, but may involve extra overhead due to the need for multi-word arithmetic.
They also cannot be implicitly converted to any underlying hardware type, as their
values may fall outside the representable range of any such type.  Explicit conversions
are available, but risk possible truncation unless the integer is known to be within
range of the target type.  In all other respects, they behave like normal integers,
and can be used wherever large integers above the 64-bit limit may be needed.

This class is mostly meant to bridge the gap between bigint-style Python integers
(which can dynamically grow to accommodate any size) and C++ integers, which are
fixed-width and usually capped at 64 bits.  By generalizing to arbitrary `N`,
Python-style integer pipelines can be lowered directly into C++ without needing a
full multiprecision library like GMP or `boost::multiprecision`, which may be overkill
for many applications.  This class is thus not a full replacement for those libraries,
but rather a lightweight alternative that is optimized for small to medium sizes (up to
a few thousand bits), as a convenience for users accustomed to higher-level
languages.

An equivalent wrapper type is exposed in Python itself as `bertrand.Int[N]`, which
works in the opposite direction, allowing Python users to work with true `N`-bit
hardware integers in their native format, without requiring multiple distinct types or
complex conversions.  This also erases the semantic differences between C++ and Python
integers, particularly around integer division (defined as floor division in Python vs
truncated division in C++) and possible overflow, which brings Python code into
alignment with extensions written in C-family languages. */
template <size_t N>
struct Int;


/// TODO: when writing docs, note that the exponent and mantissa must be at least 2
/// in order to support all IEEE 754 principles.


template <size_t E, size_t M> requires (E > 1 && M > 1)
struct Float;


namespace impl {
    struct Bits_tag {};
    struct Bits_slice_tag {};
    struct Bits_one_hot_tag {};
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

    namespace detail {
        template <meta::Bits T>
        constexpr bool integer<T> = true;
        template <meta::Bits T>
        constexpr bool unsigned_integer<T> = true;
        template <meta::integer T> requires (meta::Bits<T>)
        constexpr size_t integer_size<T> = T::size();
        template <meta::Bits T>
        struct as_signed<T> { using type = bertrand::Int<T::size()>; };
        template <meta::Bits T>
        constexpr bool prefer_constructor<T> = true;

        template <meta::UInt T>
        constexpr bool integer<T> = true;
        template <meta::UInt T>
        constexpr bool unsigned_integer<T> = true;
        template <meta::integer T> requires (meta::UInt<T>)
        constexpr size_t integer_size<T> = meta::unqualify<T>::Bits::size();
        template <meta::UInt T>
        struct as_signed<T> { using type = bertrand::Int<integer_size<T>>; };
        template <meta::UInt T>
        constexpr bool prefer_constructor<T> = true;

        template <meta::Int T>
        constexpr bool integer<T> = true;
        template <meta::Int T>
        constexpr bool signed_integer<T> = true;
        template <meta::integer T> requires (meta::Int<T>)
        constexpr size_t integer_size<T> = meta::unqualify<T>::Bits::size();
        template <meta::Int T>
        struct as_unsigned<T> { using type = bertrand::UInt<integer_size<T>>; };
        template <meta::Int T>
        constexpr bool prefer_constructor<T> = true;

        template <meta::Float T>
        constexpr bool floating<T> = true;
        template <meta::floating T> requires (meta::Float<T>)
        constexpr size_t float_exponent_size<T> =
            meta::unqualify<T>::exponent_size;
        template <meta::floating T> requires (meta::Float<T>)
        constexpr size_t float_mantissa_size<T> =
            meta::unqualify<T>::mantissa_size;
        template <meta::floating T> requires (meta::Float<T>)
        constexpr size_t float_size<T> = float_exponent_size<T> + float_mantissa_size<T>;
        template <meta::Float T>
        constexpr bool prefer_constructor<T> = true;
    }

}


namespace impl {

    /// TODO: extend log2_table to 256 bits, so that any bitset can be encoded as an
    /// ASCII string, and vice versa.  Maybe `to_ascii()` and `from_ascii()` can even
    /// be standard methods

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

    template <typename T>
    concept strict_bits =
        meta::boolean<T> || meta::Bits<T> || meta::UInt<T> || meta::Int<T>;

    template <size_t N, typename... words>
    concept strict_bitwise_constructor =
        (impl::strict_bits<words> && ...) &&
        ((meta::integer_size<words> + ... + 0) <= N);

    template <size_t N, typename... words>
    concept loose_bitwise_constructor =
        sizeof...(words) > 0 &&
        !(impl::strict_bits<words> && ...) &&
        (meta::integer<words> && ... && (
            (meta::integer_size<words> + ... + 0) <= bertrand::max(N, 64)
        ));

    /// TODO: bit_cast_constructor should possibly assert that not ALL Ts... are
    /// integers, not fail if only one is.

    template <size_t N, typename... Ts>
    concept bit_cast_constructor =
        sizeof...(Ts) > 0 &&
        (meta::trivially_copyable<Ts> && ...) &&
        ((!meta::integer<Ts> && !meta::inherits<Ts, impl::Bits_slice_tag>) && ...) &&
        ((sizeof(Ts) * 8) + ... + 0) <= N;

    /* Hardware words scale from 8 to 64 bits, based on the overall number needed to
    represent a bitset.  Bitsets greater than 64 bits are stored as arrays of 64-bit
    words.

    Each word must expose the following operations at a minimum:
        - `type` - the underlying hardware type
        - `big` - a word type of the next size class, which can represent two words
                  in the high and low halves, respectively.  64-bit words will attempt
                  to use 128-bit representations if available (GCC + LLVM usually do),
                  or a composite representation of two 64-bit words otherwise (which
                  may be slower in some cases).

    The `big` type must expose the following operations at a minimum:
        - `composite` - a compile-time boolean that is true if the word is a composite
                        representation of two smaller words, and false if it is a
                        single type of a larger size class.  If false, then `big` must
                        also expose a `type` alias that indicates the larger word
                        type directly, which will be used in place of the `big` type
                        where possible for single-word arithmetic, as an optimization.
        - {hi, lo} constructor taking individual words
        - hi() and lo() accessors returning the initializers
        - operator> for comparison
        - a widening, static mul() method that takes two words and returns their full
          product as a big word.
        - a narrowing, member divmod() method that takes a single word and returns a
          pair of words containing the quotient and remainder
    */
    template <size_t M>
    struct word {
        using type = uint64_t;
        static constexpr size_t size = meta::integer_size<type>;

        // if 128-bit integers are available, use them.  Otherwise, use a composite
        // word with 2 64-bit limbs.
        #ifdef __SIZEOF_INT128__
            struct big {
                using type = __uint128_t;
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
                    return word::type(value & type(type(type(1) << size) - 1));
                }
                constexpr bool operator>(big other) const noexcept {
                    return value > other.value;
                }
                static constexpr big mul(word::type a, word::type b) noexcept {
                    return {type(type(a) * type(b))};
                }
                constexpr std::pair<word::type, word::type> divmod(word::type v) const
                    noexcept
                {
                    return {word::type(value / v), word::type(value % v)};
                }
            };
        #else
            struct big {
                static constexpr bool composite = true;
                type h;
                type l;

                constexpr big(type hi, type lo) noexcept : h(hi), l(lo) {}
                constexpr type hi() const noexcept { return h; }
                constexpr type lo() const noexcept { return l; }
                constexpr bool operator>(big other) const noexcept {
                    return h > other.h || (h == other.h && l > other.l);
                }
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
                /* This implementation is taken from the libdivide reference:
                    https://github.com/ridiculousfish/libdivide/blob/master/doc/divlu.c

                It should be slightly faster than the naive approach found in Hacker's
                Delight. */
                constexpr std::pair<uint64_t, uint64_t> divmod(uint64_t v) const
                    noexcept
                {
                    // 1. Check for overflow and divide by zero.
                    if (hi() >= v) {
                        return {
                            std::numeric_limits<uint64_t>::max(),
                            std::numeric_limits<uint64_t>::max()
                        };
                    }

                    // 2. If the high bits of the dividend are empty, then we devolve
                    // to a single word divide as an optimization.
                    if (!hi()) {
                        return {lo() / v, lo() % v};
                    }

                    constexpr size_t chunk = size / 2;
                    constexpr uint64_t b = uint64_t(1) << chunk;
                    constexpr uint64_t mask = b - 1;
                    uint64_t h = hi();
                    uint64_t l = lo();

                    // 3. Normalize by left shifting divisor until the most significant
                    // bit is set.  This cannot overflow the numerator because h < v.
                    // The strange bitwise AND is meant to avoid undefined behavior
                    // when shifting by a full word size.  It is taken from
                    // https://ridiculousfish.com/blog/posts/labor-of-division-episode-v.html
                    int shift = std::countl_zero(v);
                    v <<= shift;
                    h <<= shift;
                    h |= ((l >> (-shift & (size - 1))) & (-int64_t(shift) >> (size - 1)));
                    l <<= shift;

                    // 4. Split divisor and low bits of numerator into partial
                    // half-words.
                    uint32_t n1 = l >> chunk;
                    uint32_t n0 = l & mask;
                    uint32_t d1 = v >> chunk;
                    uint32_t d0 = v & mask;

                    // 5. Estimate q1 = [n3 n2 n1] / [d1 d0].  Note that while qhat may
                    // be 2 half-words, q1 is always just the lower half, which
                    // translates to the upper half of the final quotient.
                    uint64_t qhat = h / d1;
                    uint64_t rhat = h % d1;
                    uint64_t c1 = qhat * d0;
                    uint64_t c2 = rhat * b + n1;
                    if (c1 > c2) {
                        qhat -= 1 + ((c1 - c2) > v);
                    }
                    uint32_t q1 = uint32_t(qhat & mask);

                    // 6. Compute the true (normalized) partial remainder.
                    uint64_t r = h * b + n1 - q1 * v;

                    // 7. Estimate q0 = [r1 r0 n0] / [d1 d0].  Estimate q0 as
                    // [r1 r0] / [d1] and correct it.  These become the bottom bits of
                    // the final quotient.
                    qhat = r / d1;
                    rhat = r % d1;
                    c1 = qhat * d0;
                    c2 = rhat * b + n0;
                    if (c1 > c2) {
                        qhat -= 1 + ((c1 - c2) > v);
                    }
                    uint32_t q0 = uint32_t(qhat & mask);

                    // 8. Return the quotient and unnormalized remainder
                    return {
                        (uint64_t(q1) << chunk) | q0,
                        ((r * b) + n0 - (q0 * v)) >> shift
                    };
                }
            };
        #endif
    };
    template <size_t M> requires (M <= meta::integer_size<uint8_t>)
    struct word<M> {
        using type = uint8_t;
        static constexpr size_t size = meta::integer_size<type>;
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
            constexpr bool operator>(big other) const noexcept {
                return value > other.value;
            }
            static constexpr big mul(word::type a, word::type b) noexcept {
                return {type(type(a) * type(b))};
            }
            constexpr std::pair<word::type, word::type> divmod(word::type v) const
                noexcept
            {
                return {word::type(value / v), word::type(value % v)};
            }
        };
    };
    template <size_t M>
        requires (M > meta::integer_size<uint8_t> && M <= meta::integer_size<uint16_t>)
    struct word<M> {
        using type = uint16_t;
        static constexpr size_t size = meta::integer_size<type>;
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
            constexpr bool operator>(big other) const noexcept {
                return value > other.value;
            }
            static constexpr big mul(word::type a, word::type b) noexcept {
                return {type(type(a) * type(b))};
            }
            constexpr std::pair<word::type, word::type> divmod(word::type v) const
                noexcept
            {
                return {word::type(value / v), word::type(value % v)};
            }
        };
    };
    template <size_t M>
        requires (M > meta::integer_size<uint16_t> && M <= meta::integer_size<uint32_t>)
    struct word<M> {
        using type = uint32_t;
        static constexpr size_t size = meta::integer_size<type>;
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
            constexpr bool operator>(big other) const noexcept {
                return value > other.value;
            }
            static constexpr big mul(word::type a, word::type b) noexcept {
                return {type(type(a) * type(b))};
            }
            constexpr std::pair<word::type, word::type> divmod(word::type v) const
                noexcept
            {
                return {word::type(value / v), word::type(value % v)};
            }
        };
    };

    /* A mutable reference to a single bit in a bitset. */
    template <typename word>
    struct bit_reference {
    private:
        template <size_t N>
        friend struct bertrand::Bits;
        word* value = nullptr;
        word index = 0;

        constexpr bit_reference() noexcept = default;
        constexpr bit_reference(word* value, word index) noexcept :
            value(value), index(index)
        {}

    public:
        [[nodiscard]] constexpr operator bool() const noexcept {
            return *value & (word(1) << index);
        }

        constexpr bit_reference& operator=(bool x) noexcept {
            *value = word(*value & ~word(word(1) << index)) | word(word(x) << index);
            return *this;
        }

        constexpr bit_reference& operator|=(bool x) noexcept {
            *value |= word(word(x) << index);
            return *this;
        }

        constexpr bit_reference& operator&=(bool x) noexcept {
            *value &= word(word(x) << index);
            return *this;
        }

        constexpr bit_reference& operator^=(bool x) noexcept {
            *value ^= word(word(x) << index);
            return *this;
        }
    };

    /* A compile-time context object backing the `Bits<N>::from_string()` factory
    method.  Centralizing the context here reduces template instantiation depth, and
    therefore compile time and binary size. */
    template <typename word, const auto&... keys>
    struct bits_from_string {
        static constexpr size_t base = sizeof...(keys);
        static constexpr size_t bits_per_digit = std::bit_width(base - 1);

        template <size_t... Is>
        static constexpr auto _digits(std::index_sequence<Is...>) {
            return string_map<word, keys...>{word(Is)...};
        }

        static constexpr auto digits = _digits(std::make_index_sequence<base>{});

        template <size_t... sizes>
        static constexpr auto _digit_lengths() {
            std::array<size_t, sizeof...(sizes)> out {sizes...};
            bertrand::sort<impl::Greater>(out);
            return out;
        }

        static constexpr auto digit_lengths = _digit_lengths<keys.size()...>();

        template <size_t I = 0, size_t... sizes>
        static constexpr decltype(auto) _widths() noexcept {
            if constexpr (I == 0) {
                return (_widths<I + 1, digit_lengths[I]>());
            } else {
                if constexpr (
                    digit_lengths[I] < meta::unpack_value<sizeof...(sizes) - 1, sizes...>
                ) {
                    return (_widths<I + 1, sizes..., digit_lengths[I]>());
                } else {
                    return (_widths<I + 1, sizes...>());
                }
            }
        }
        template <size_t I, size_t... sizes> requires (I == base)
        static constexpr std::array<size_t, sizeof...(sizes)> _widths() noexcept {
            return {sizes...};
        }

        static constexpr auto widths = _widths();
    };

    /* Print Bits, UInt, and Int types using output streams or `std::format()`.
    Reference: https://en.cppreference.com/w/cpp/utility/format/spec */
    struct format_bits {
    private:
        static constexpr char lower_alpha[] = "0123456789abcdef";
        static constexpr char upper_alpha[] = "0123456789ABCDEF";

        constexpr char get_align(std::ostream& os) const noexcept {
            char align = '>';
            switch (os.flags() & std::ios_base::adjustfield) {
                case std::ios_base::left: align = '<'; break;
                case std::ios_base::internal: align = '='; break;
                default: break;
            }
            return align;
        }

        constexpr char get_base(std::ostream& os) const noexcept {
            char base = 'd';
            switch (os.flags() & std::ios_base::basefield) {
                case std::ios_base::oct: base = 'o'; break;
                case std::ios_base::hex:
                    base = os.flags() & std::ios_base::uppercase ? 'X' : 'x';
                    break;
                default: break;
            }
            return base;
        }

        static constexpr char advance(auto& it, auto& end) {
            char c = *it++;
            if (it == end) {
                throw std::format_error("Unbalanced braces in format string");
            }
            return c;
        }

    public:
        enum class Width : uint8_t {
            fixed,
            arg_explicit,
            arg_implicit
        };

        // [fill]['<'/'>'/'^']['+'/'-'/' ']['#']['0'][width]['L']
        // ['b'/'B'/'c'/'d'/'o'/'x'/'X']
        char fill = ' ';            // any character other than '{' or '}'
        char align = '=';           // '<' (left), '>' (right - default), '^' (center)
        char sign = '-';            // '+' (+ for positive, - for negative)
                                    // '-' (- for negative only),
                                    // ' ' (space for positive, - for negative)
        bool negative = false;      // true if the actual value is negative
        bool show_base = false;     // prefix "0b" (binary), "0" (octal), or "0x" (hex)
        Width width_type;           // width may be a nested format specifier
        bool locale = false;        // true if locale-based grouping is used
        char base = 'd';            // 'b' (binary w/ prefix 0b)
                                    // 'B' (binary w/ prefix 0B),
                                    // 'c' (static cast to char or error if overflow)
                                    // 'd' (decimal - default),
                                    // 'o' (octal, prefix 0 unless value is zero),
                                    // 'x' (hexadecimal w/ prefix 0x and lowercase letters)
                                    // 'X' (hexadecimal w/ prefix 0X and uppercase letters)
        char sep;                   // separator character for grouping, if any
        size_t width = 0;           // minimum field width OR argument index
        std::string grouping;       // locale-based grouping string

        constexpr format_bits() noexcept = default;

        constexpr format_bits(
            std::ostream& os,
            bool negative,
            const auto& facet
        ) noexcept :
            fill(os.fill()),
            align(get_align(os)),
            sign(os.flags() & std::ios_base::showpos ? '+' : '-'),
            negative(negative),
            show_base(os.flags() & std::ios_base::showbase),
            width_type(Width::fixed),
            locale(true),
            base(get_base(os)),
            sep(facet.thousands_sep()),
            width(os.width()),
            grouping(facet.grouping())
        {}

        constexpr format_bits(
            std::format_parse_context& ctx,
            auto& it
        ) {
            auto end = ctx.end();
            if (it == end) {
                throw std::format_error("Unbalanced braces in format string");
            }
            if (*it == '}') {
                return;
            }

            // check for fill and align
            if (*it == '<' || *it == '>' || *it == '^') {
                align = advance(it, end);
            } else if ((it + 1) != end && (
                it[1] == '>' || it[1] == '<' || it[1] == '^'
            )) {
                if (*it == '{' || *it == '}') {
                    throw std::format_error("Invalid fill character");
                }
                fill = advance(it, end);
                align = advance(it, end);
            }

            // check for sign
            if (*it == '+' || *it == '-' || *it == ' ') {
                sign = advance(it, end);
            }

            // check for show_base
            if (*it == '#') {
                show_base = true;
                advance(it, end);
            }

            // check for zero padding
            if (*it == '0' && align == '=') {
                fill = advance(it, end);
            }

            // check for width
            if (*it == '{') {  // nested specifier
                advance(it, end);
                if (*it == '}') {  // implicit argument index
                    advance(it, end);
                    width_type = Width::arg_implicit;
                    width = ctx.next_arg_id();
                } else {  // explicit argument index
                    while (std::isdigit(*it)) {
                        width = width * 10 + (advance(it, end) - '0');
                    }
                    if (*it != '}') {
                        throw std::format_error(
                            "Bad index to nested width specifier"
                        );
                    }
                    ctx.check_arg_id(width);
                    advance(it, end);
                    width_type = Width::arg_explicit;
                }
            } else if (std::isdigit(*it)) {
                do {
                    width = width * 10 + (advance(it, end) - '0');
                } while (std::isdigit(*it));
                width_type = Width::fixed;
            }

            // check for locale
            if (*it == 'L') {
                locale = true;
                advance(it, end);
            }

            // check for base
            if (
                *it == 'b' || *it == 'B' || *it == 'c' || *it == 'd' ||
                *it == 'o' || *it == 'x' || *it == 'X'
            ) {
                if (*it == 'c') {
                    if (show_base) {
                        throw std::format_error(
                            "base 'c' is mutually exclusive with '#'"
                        );
                    } else if (sign != '-') {
                        throw std::format_error(
                            "base 'c' is mutually exclusive with '+' or ' ' sign"
                        );
                    }
                }

                base = advance(it, end);
            }

            // if the current character is not a closing brace, then the argument
            // specifier is invalid somehow
            if (*it != '}') {
                throw std::format_error("Invalid format specifier");
            }
        }

        template <typename out, typename char_type>
        constexpr void complete(
            std::basic_format_context<out, char_type>& ctx,
            bool negative
        ) {
            this->negative = negative;

            // get proper locale from context
            if (locale) {
                const auto& facet = std::use_facet<std::numpunct<char_type>>(
                    ctx.locale()
                );
                sep = facet.thousands_sep();
                grouping = facet.grouping();
            }

            // resolve nested width specifier
            if (width_type != Width::fixed) {
                auto arg = ctx.arg(width);
                if (!arg) {
                    throw std::format_error(
                        "Invalid argument index for nested width specifier"
                    );
                }
                std::visit_format_arg([&](auto&& x) {
                    using A = std::decay_t<decltype(x)>;
                    if constexpr (meta::integer<decltype(x)>) {
                        if (x < 0) {
                            throw std::format_error(
                                "Nested width specifier must be non-negative"
                            );
                        }
                        if (x > std::numeric_limits<size_t>::max()) {
                            throw std::format_error(
                                "Nested width specifier must be less than "
                                "std::numeric_limits<size_t>::max()"
                            );
                        }
                        width = static_cast<size_t>(x);
                    } else {
                        throw std::format_error(
                            "Nested width specifier must be an integer"
                        );
                    }
                }, arg);
            }
        }

        template <typename char_type, size_t N>
        constexpr std::basic_string<char_type> digits_with_grouping(
            const Bits<N>& bits
        ) const {
            using word = Bits<N>::word;

            // if `base` is set to 'c', then we only output a single character, which
            // is the value of the bitset converted to the character type of the format
            // string, assuming it is within range
            if (base == 'c') {
                if (bits <= std::numeric_limits<char_type>::max()) {
                    return {1, static_cast<char_type>(bits)};
                } else {
                    throw std::format_error(
                        "Cannot convert Bits to char: value is too large"
                    );
                }
            }

            auto last = bits.last_one();
            if (!last.has_value()) {
                return {1, '0'};
            }

            // decode base specifier and choose the appropriate alphabet
            size_t actual_base;
            bool upper = false;
            switch (base) {
                case 'b':
                    actual_base = 2;
                    break;
                case 'B':
                    actual_base = 2;
                    upper = true;
                    break;
                case 'd':
                    actual_base = 10;
                    break;
                case 'o':
                    actual_base = 8;
                    break;
                case 'x':
                    actual_base = 16;
                    break;
                case 'X':
                    actual_base = 16;
                    upper = true;
                    break;
                default:
                    throw std::format_error(
                        "Invalid base for Bits::format_bits: '" +
                        std::to_string(base) + "'"
                    );
            }
            const char* alpha = upper ? upper_alpha : lower_alpha;

            // compute # of digits in the given base needed to represent the most
            // significant nonzero bit: ceil(N / log2(base))
            size_t size = size_t(std::ceil(
                double(last.value() + 1) / std::log2(actual_base)
            ));
            std::basic_string<char_type> result;
            Bits<N> quotient = bits;

            // if the base is a power of 2, prefer simple shifts and masks
            if (impl::is_power2(actual_base)) {
                result.reserve(size);
                word mask = word(actual_base - 1);
                word width = word(std::bit_width(mask));
                for (size_t i = 0; i < size; ++i) {
                    result.push_back(alpha[word(quotient) & mask]);
                    quotient >>= width;
                }

            // otherwise, fall back to division-based extraction
            } else {
                Bits<N> divisor = actual_base;
                Bits<N> remainder;
                if (actual_base == 10 && !grouping.empty()) {
                    // estimate final string length with separators inserted by getting
                    // last positive group width less than CHAR_MAX and adding to size
                    signed char group_len = 0;
                    for (signed char s : reversed(grouping)) {
                        if (s > 0 && s < CHAR_MAX) {
                            group_len = s;
                            break;
                        }
                    }
                    if (group_len == 0) {
                        result.reserve(size);
                    } else {
                        result.reserve(size + (size / size_t(group_len)));
                    }
                    group_len = grouping[0];
                    for (size_t i = 0, group_idx = 0; i < size; ++i) {
                        quotient.divmod(divisor, quotient, remainder);
                        result.push_back(alpha[word(remainder)]);

                        // if `group_len` is a positive number less than CHAR_MAX, then
                        // decrement it and insert a separator if it reaches zero
                        if (
                            (i + 1) < size &&  // skip last sep if no subsequent digit
                            group_len > 0 &&  // non-positive lengths are infinite size
                            group_len < CHAR_MAX &&  // CHAR_MAX is infinite size
                            --group_len == 0  // reached end of group
                        ) {
                            result.push_back(sep);
                            if ((group_idx + 1) < grouping.size()) {
                                group_len = grouping[++group_idx];  // advance group
                            } else {
                                group_len = grouping[group_idx];  // repeat last group
                            }
                        }
                    }

                } else {
                    result.reserve(size);
                    for (size_t i = 0; i < size; ++i) {
                        quotient.divmod(divisor, quotient, remainder);
                        result.push_back(alpha[word(remainder)]);
                    }
                }
            }

            std::reverse(result.begin(), result.end());
            return result;
        }

        template <typename char_type>
        constexpr void pad_and_align(
            std::basic_string<char_type>& str
        ) const {
            size_t prefix_len = 0;

            // prepend base prefix if requested
            if (show_base) {
                switch (base) {
                    case 'b':
                        str.insert(0, std::string_view{"0b", 2});
                        prefix_len += 2;
                        break;
                    case 'B':
                        str.insert(0, std::string_view{"0B", 2});
                        prefix_len += 2;
                        break;
                    case 'c':
                        break;
                    case 'd':
                        break;
                    case 'o':
                        if (str.size() > 1 || str[0] != '0') {
                            str.insert(0, std::string_view{"0", 1});
                            ++prefix_len;
                        }
                        break;
                    case 'x':
                        str.insert(0, std::string_view{"0x", 2});
                        prefix_len += 2;
                        break;
                    case 'X':
                        str.insert(0, std::string_view{"0X", 2});
                        prefix_len += 2;
                        break;
                    default:
                        throw std::format_error(
                            "Invalid base for show_base: '" +
                            std::to_string(base) + "'"
                        );
                }
            }

            // prepend sign if requested
            switch (sign) {
                case '+':  // always
                    if (negative) str.insert(str.begin(), '-');
                    else str.insert(str.begin(), '+');
                    ++prefix_len;
                    break;
                case '-':  // only if negative
                    if (negative) {
                        str.insert(str.begin(), '-');
                        ++prefix_len;
                    }
                    break;
                case ' ':  // align using space if positive
                    if (negative) str.insert(str.begin(), '-');
                    else str.insert(str.begin(), ' ');
                    ++prefix_len;
                    break;
                default:
                    throw std::format_error(
                        "Invalid sign: '" + std::to_string(sign) + "'"
                    );
            }

            // fill to at least `width` characters
            if (str.size() < width) {
                size_t n = width - str.size();
                switch (align) {
                    case '<':  // left align
                        str.append(n, fill);
                        break;
                    case '>':  // right align
                        str.insert(str.begin(), n, fill);
                        break;
                    case '^':  // center align
                        str.insert(0, n / 2, fill);
                        str.append(n - (n / 2), fill);
                        break;
                    case '=':  // internal alignment - pad between sign/base and value
                        str.insert(prefix_len, n, fill);
                        break;
                    default:
                        throw std::format_error(
                            "Invalid alignment for show_base: '" +
                            std::to_string(align) + "'"
                        );
                }
            }
        }
    };

    /* Parse Bits, UInt, and Int types from strings using input streams or
    `std::scan()` when that becomes available. */
    struct scan_bits {
    private:
        static constexpr char lower_alpha[] = "0123456789abcdef";
        static constexpr char upper_alpha[] = "0123456789ABCDEF";

        constexpr uint8_t get_base(std::istream& is) const noexcept {
            uint8_t base = 0;
            switch (is.flags() & std::ios_base::basefield) {
                case std::ios_base::oct: base = 8; break;
                case std::ios_base::dec: base = 10; break;
                case std::ios_base::hex: base = 16; break;
                default: break;
            }
            return base;
        }

        template <typename word>
        static constexpr word decode(char c) noexcept {
            if ('0' <= c && c <= '9') return c - '0';
            if ('a' <= c && c <= 'z') return 10 + c - 'a';
            if ('A' <= c && c <= 'Z') return 10 + c - 'A';
            return std::numeric_limits<word>::max();  // invalid character
        }

        constexpr bool ok(std::istream& is) noexcept {
            return --width && is.good();
        }

        constexpr void fail(std::istream& is) noexcept {
            is.setstate(std::ios_base::failbit);
        }

    public:
        char sep;                   // separator character for grouping, if any
        uint8_t base = 0;           // numeric base (0, 2, 8, 10, 16)
        size_t width = 0;           // maximum number of characters to read

        constexpr scan_bits() noexcept = default;

        constexpr scan_bits(
            std::istream& is,
            const auto& facet
        ) noexcept :
            sep(facet.thousands_sep()),
            base(get_base(is)),
            width(is.width())
        {
            if (width == 0) {
                width = std::numeric_limits<size_t>::max();  // no limit
            }
            is.width(0);  // reset now, per standard
        }

        template <typename out>
        constexpr void scan(std::istream& is, out& result) {
            using word = out::word;
            bool negative = false;
            bool overflow;
            char c;

            // 1) check for optional sign
            if (is.peek() == '+') {
                is.get(c);  // consume the '+' sign
                if (!ok(is)) return fail(is);  // no digits after sign
            } else if (is.peek() == '-') {
                if constexpr (meta::signed_integer<out>) {
                    negative = true;
                    is.get(c);  // consume the '-' sign
                    if (!ok(is)) return fail(is);  // no digits after sign
                } else {
                    return fail(is);  // negative sign not allowed for unsigned types
                }
            }

            // 2) detect and consume base prefix
            is.get(c);  // extract first character
            if (!ok(is)) {  // no digits after first character
                word value = decode<word>(c);
                if (value >= base) return fail(is);
                result = value;
                return;
            }
            switch (base) {
                case 0:
                    if (c == '0') {
                        if (is.peek() == 'b' || is.peek() == 'B') {
                            is.get(c);  // consume the 'b' or 'B'
                            if (!ok(is)) return fail(is);  // no digits after prefix
                            is.get(c);  // c now points to first real character of number
                            base = 2;
                        } else if (is.peek() == 'o' || is.peek() == 'O') {
                            is.get(c);
                            if (!ok(is)) return fail(is);
                            is.get(c);
                            base = 8;
                        } else if (is.peek() == 'x' || is.peek() == 'X') {
                            is.get(c);
                            if (!ok(is)) return fail(is);
                            is.get(c);
                            base = 16;
                        } else {
                            base = 10;
                        }
                    } else {
                        base = 10;
                    }
                    break;
                case 8:
                    if (c == '0' && (is.peek() == 'o' || is.peek() == 'O')) {
                        is.get(c);
                        if (!ok(is)) return fail(is);
                        is.get(c);
                    }
                    break;
                case 16:
                    if (c == '0' && (is.peek() == 'x' || is.peek() == 'X')) {
                        is.get(c);
                        if (!ok(is)) return fail(is);
                        is.get(c);
                    }
                    break;
                default: break;
            }
            out multiplier = base;
            out temp;

            /// 3) decode each character, respecting the locale's grouping
            while (base == 10 && c == sep) {
                is.get(c);  // skip grouping separator
                if (!ok(is)) return fail(is);  // no digits after grouping separator
            }
            word value = decode<word>(c);  // decode first real digit
            if (value >= base) return fail(is);  // invalid digit
            temp.add(value, overflow, temp);  // add first digit in-place
            if (overflow) {
                while (ok(is)) {  
                    is.get(c);
                    if (base == 10 && c == sep) continue;  // skip grouping separator
                    value = decode<word>(c);
                    if (value >= base) break;  // digit sequence finished
                }
                is.setstate(std::ios_base::failbit | std::ios_base::badbit);
                return;  // overflow on first digit
            }
            while (ok(is)) {
                is.get(c);  // read the next character
                if (base == 10 && c == sep) continue;  // skip grouping separator
                value = decode<word>(c);
                if (value >= base) break;  // digit sequence finished
                temp.mul(multiplier, overflow, temp);  // shift left by base
                temp.add(value, overflow, temp);  // add digit
                if (overflow) {
                    while (ok(is)) {  // consume any remaining characters in this base/limit
                        is.get(c);
                        if (base == 10 && c == sep) continue;  // skip grouping separator
                        value = decode<word>(c);
                        if (value >= base) break;  // digit sequence finished
                    }
                    is.setstate(std::ios_base::failbit | std::ios_base::badbit);
                    return;
                }
            }

            // 4) only assign if we have a valid result
            result = std::move(result);
        }
    };

}


template <meta::integer... Ts>
Bits(Ts...) -> Bits<(meta::integer_size<Ts> + ... + 0)>;
template <meta::inherits<impl::Bits_slice_tag> T>
Bits(T) -> Bits<T::self::size()>;
template <size_t N>
Bits(const char(&)[N]) -> Bits<N - 1>;
template <size_t N>
Bits(const char(&)[N], char) -> Bits<N - 1>;
template <size_t N>
Bits(const char(&)[N], char, char) -> Bits<N - 1>;
template <typename... Ts>
    requires (impl::bit_cast_constructor<((sizeof(Ts) * 8) + ... + 0), Ts...>)
Bits(const Ts&...) -> Bits<((sizeof(Ts) * 8) + ... + 0)>;


template <size_t N>
struct Bits : impl::Bits_tag {
    using word = impl::word<N>::type;
    using value_type = bool;
    using size_type = size_t;
    using index_type = ssize_t;
    using reference = impl::bit_reference<word>;

    /* The number of bits in each machine word. */
    static constexpr size_type word_size = impl::word<N>::size;

    /* The total number of words that back the bitset. */
    static constexpr size_type array_size = (N + word_size - 1) / word_size;

    /* A bitmask that identifies the valid bits for the last word, assuming
    `N % word_size` is nonzero.  Otherwise, if `N` is a clean multiple of `word_size`,
    then this mask will be set to zero. */
    static constexpr word end_mask =
        N % word_size ?
            word(word(1) << (N % word_size)) - word(1) :
            word(0);

    using array = std::array<word, array_size>;
    array buffer;

    /* The minimum value that the bitset can hold. */
    [[nodiscard]] static constexpr const Bits& min() noexcept {
        static constexpr Bits result;
        return result;
    }

    /* The maximum value that the bitset can hold. */
    [[nodiscard]] static constexpr const Bits& max() noexcept {
        static constexpr Bits result = ~Bits{};
        return result;
    }

    /* The number of bits that are held in the set. */
    [[nodiscard]] static constexpr size_type size() noexcept { return N; }
    [[nodiscard]] static constexpr index_type ssize() noexcept { return index_type(N); }

    /* The total number of bits needed to represent the array.  This will always be a
    equal to the product of `word_size` and `array_size`. */
    [[nodiscard]] static constexpr size_type capacity() noexcept {
        return array_size * word_size;
    }

private:
    template <size_type>
    friend struct bertrand::UInt;
    template <size_type>
    friend struct bertrand::Int;
    template <size_type E, size_type M> requires (E > 1 && M > 1)
    friend struct bertrand::Float;
    using big_word = impl::word<N>::big;

    template <size_type I>
    static constexpr void from_bits(array& data) noexcept {}

    template <size_type I, meta::boolean T, typename... Ts>
    static constexpr void from_bits(array& data, T v, const Ts&... rest) noexcept {
        data[I / word_size] |= word(word(v) << (I % word_size));
        from_bits<I + 1>(data, rest...);
    }

    template <size_type I, meta::Bits T, typename... Ts>
    static constexpr void from_bits(array& data, const T& v, const Ts&... rest) noexcept {
        static constexpr size_type J = I + meta::integer_size<T>;
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
    static constexpr void from_bits(array& data, const T& v, const Ts&... rest) noexcept {
        from_bits<I>(data, v.bits, rest...);
    }

    template <size_type I>
    static constexpr void from_ints(array& data) noexcept {
        if constexpr (end_mask) {
            data[array_size - 1] &= end_mask;
        }
    }

    template <size_type I, meta::integer T, typename... Ts>
    static constexpr void from_ints(array& data, T v, const Ts&... rest) noexcept {
        static constexpr size_type J = I + meta::integer_size<T>;
        static constexpr size_type start_bit = I % word_size;
        static constexpr size_type start_word = I / word_size;
        static constexpr size_type stop_word = J / word_size;
        static constexpr size_type stop_bit = J % word_size;

        if constexpr (meta::Bits<T>) {
            if constexpr (start_bit == 0) {
                size_type i = 0;
                while (i < v.array_size && (start_word + i) < array_size) {
                    data[start_word + i] |= v.buffer[i];
                    ++i;
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
            from_ints<I + meta::integer_size<T>>(data, rest...);

        } else if constexpr (meta::UInt<T> || meta::Int<T>) {
            from_ints<I>(data, v.bits, rest...);

        } else {
            if constexpr (stop_bit == 0 || start_word == stop_word) {
                data[start_word] |= word(word(v) << start_bit);
            } else {
                data[start_word] |= word(word(v) << start_bit);
                size_type consumed = word_size - start_bit;
                v >>= consumed;
                size_type i = start_word;
                while (consumed < meta::integer_size<T>) {
                    data[++i] |= word(v);
                    if constexpr (word_size < meta::integer_size<T>) {
                        v >>= word_size;
                    }
                    consumed += word_size;
                }
            }
            from_ints<I + meta::integer_size<T>>(data, rest...);
        }
    }

    static constexpr void _bitwise_repr(
        array& out,
        size_type idx,
        const auto& curr,
        const auto&... rest
    ) noexcept {
        for (size_type i = 0; i < curr.size(); ++i, idx += 8) {
            out[idx / word_size] |= word(word(curr[i]) << (idx % word_size));
        }
        if constexpr (sizeof...(rest)) {
            _bitwise_repr(out, idx, rest...);
        }
    }

    template <typename T>
    static constexpr array bitwise_repr(const T& val) noexcept {
        // if the type's size exactly matches the underlying array, we can directly
        // cast as an optimization
        if constexpr (sizeof(T) * 8 == capacity()) {
            return std::bit_cast<array>(val);

        // otherwise, we initialize a zero array and copy each byte in one at a time
        } else {
            array out {};
            _bitwise_repr(
                out,
                0,
                std::bit_cast<std::array<uint8_t, sizeof(T)>>(val)
            );
            return out;
        }
    }

    template <typename... Ts> requires (sizeof...(Ts) > 0)
    static constexpr array bitwise_repr(const Ts&... vals) noexcept {
        array out{};
        _bitwise_repr(
            out,
            0,
            std::bit_cast<std::array<uint8_t, sizeof(Ts)>>>(vals)...
        );
        return out;
    }

    template <size_type I, typename ctx>
    static constexpr Expected<void, OverflowError> from_string_helper(
        Bits& result,
        std::string_view str,
        size_type& i,
        std::string_view& continuation
    ) {
        if constexpr (I < ctx::widths.size()) {
            // search the digit map for a matching digit
            auto it = ctx::digits.find(str.substr(i, ctx::widths[I]));

            // if a digit was found, then we can multiply the bitset by the base,
            // join the digit, and then advance the loop index
            if (it != ctx::digits.end()) {

                // if the base is a power of 2, then we can use a left shift and
                // bitwise OR to avoid expensive multiplication and addition.
                if constexpr (impl::is_power2(ctx::digits.size())) {

                    // if a left shift by `per_digit` would cause the most significant
                    // set bit to overflow, then we have an OverflowError
                    size_type j = array_size - 1;
                    size_type msb = size_type(std::countl_zero(result.buffer[j]));
                    if (msb < word_size) {  // msb is in last word
                        size_type distance = N - (word_size * j + word_size - 1 - msb);
                        if (distance <= ctx::bits_per_digit) {  // would overflow
                            return OverflowError();
                        }
                    } else {  // msb is in a previous word
                        // stop early when distance exceeds shift amount
                        size_type distance = N % word_size;
                        while (distance <= ctx::bits_per_digit && j-- > 0) {
                            msb = size_type(std::countl_zero(result.buffer[j]));
                            if (msb < word_size) {
                                distance = N - (word_size * j + word_size - 1 - msb);
                                if (distance <= ctx::bits_per_digit) {
                                    return OverflowError();
                                }
                            } else {
                                distance += word_size;
                            }
                        }
                    }

                    result <<= ctx::bits_per_digit;
                    result |= it->second;

                // otherwise, we use overflow-safe operators to detect errors
                } else {
                    bool overflow;
                    result.mul(word(ctx::digits.size()), overflow, result);
                    if (overflow) {
                        return OverflowError();
                    }
                    result.add(it->second, overflow, result);
                    if (overflow) {
                        return OverflowError();
                    }
                }

                // terminate recursion normally and advance by width of digit 
                i += ctx::widths[I];
                return {};
            }

            // otherwise, try again with the next largest width
            return from_string_helper<I + 1, ctx>(result, str, i, continuation);

        // if no digit was found, then we stop parsing and set the continuation
        // parameter
        } else {
            continuation = str.substr(i);
            return {};
        }
    }

    template <size_t J, const Bits& divisor>
    static constexpr std::string_view extract_digit(
        const auto& digits,
        Bits& quotient,
        Bits& remainder,
        size_type& size,
        size_type count
    ) {
        quotient.divmod(divisor, quotient, remainder);
        std::string_view out = digits[remainder.data()[0]];
        size += out.size() * (J < count);
        return out;
    }

    template <size_t J, word mask, size_type nbits>
    constexpr std::string_view extract_digit_power2(
        const auto& digits,
        size_type& size,
        size_type count
    ) const {
        constexpr size_type idx = (J * nbits) / word_size;
        constexpr size_type bit = (J * nbits) % word_size;

        // right shift starting word and get lower bits of value
        word val = word(word(buffer[idx] >> bit) & mask);

        // if the mask straddles a word boundary, then we need to get the high bits
        // from the next word, assuming there is one.
        if constexpr (((bit + nbits) > word_size) && idx + 1 <= array_size) {
            constexpr size_type consumed = word_size - bit;
            word next = word(buffer[idx + 1] & word(mask >> consumed));
            val += word(next << consumed);
        }

        // look up completed value in digit map
        std::string_view out = digits[val];
        size += out.size() * (J < count);
        return out;
    }

    template <static_str zero, static_str one, static_str... rest>
        requires (
            sizeof...(rest) + 2 <= 64 &&
            !zero.empty() && !one.empty() && (... && !rest.empty()) &&
            meta::strings_are_unique<zero, one, rest...>
        )
    [[nodiscard]] constexpr auto _to_string(size_type& size, size_type& count) const
        noexcept
    {
        // # of digits needed to represent the value in `base = sizeof...(Strs)` is
        // ceil(N / log2(base))
        static constexpr size_type base = sizeof...(rest) + 2;
        static constexpr double _ceil = N / impl::log2_table[base];
        static constexpr size_type ceil = size_type(_ceil) + (size_type(_ceil) < _ceil);

        // generate a lookup table of substrings to use for each digit
        static constexpr std::array<std::string_view, base> digits {zero, one, rest...};
        static constexpr Bits divisor = base;

        // digit count is equal to the number of digits needed to represent the most
        // significant active bit in the set
        index_type msb = last_one().value_or(0);
        double _count = (msb + 1) / impl::log2_table[base];
        count = size_type(_count) + (size_type(_count) < _count);

        // if the base is a power of 2, then we can use a simple bitscan to determine
        // the final string, rather than doing multi-word division
        if constexpr (impl::is_power2(base)) {
            static constexpr word mask = word(base - 1);
            static constexpr size_type nbits = std::bit_width(mask);
            return [this]<size_t... Js>(
                std::index_sequence<Js...>,
                size_type& size,
                size_type count
            ) {
                return std::array<std::string_view, ceil>{
                    extract_digit_power2<Js, mask, nbits>(
                        digits,
                        size,
                        count
                    )...
                };
            }(std::make_index_sequence<ceil>{}, size, count);

        // otherwise, use modular division to calculate all the substrings needed to
        // represent the value, from the least significant digit to most significant.
        } else {
            Bits quotient = *this;
            Bits remainder;
            return []<size_t... Js>(
                std::index_sequence<Js...>,
                Bits& quotient,
                Bits& remainder,
                size_type& size,
                size_type count
            ) {
                return std::array<std::string_view, ceil>{
                    extract_digit<Js, divisor>(
                        digits,
                        quotient,
                        remainder,
                        size,
                        count
                    )...
                };
            }(std::make_index_sequence<ceil>{}, quotient, remainder, size, count);
        }
    }

    constexpr bool msb_is_set() const noexcept {
        if constexpr (end_mask) {
            return buffer[array_size - 1] >= word(word(1) << ((N % word_size) - 1));
        } else {
            return buffer[array_size - 1] >= word(word(1) << (word_size - 1));
        }
    }

    static constexpr Bits interval_mask(size_type start, size_type stop) noexcept {
        Bits result = max();
        result <<= N - stop;
        result >>= N - (stop - start);
        result <<= start;  // [start, stop).
        return result;
    }

    static constexpr Bits alternating_mask(
        size_type start,
        size_type stop,
        size_type step
    ) noexcept {
        constexpr size_type wide = N + word_size;
        size_type k = (stop - start + step - 1) / step;
        Bits<wide> num = Bits<wide>::max() >> (wide - (step * k));
        Bits<wide> denom = Bits<wide>::max() >> (wide - step);
        Bits result {num / denom};
        result <<= start;
        return result;
    }

    static constexpr Bits _mask(const bertrand::slice::normalized& indices) noexcept {
        if (indices.length == 0) {
            return {};
        }

        // if step size is positive, then no adjustment is needed
        if (indices.step > 0) {
            if (indices.step == 1) {
                return interval_mask(
                    size_type(indices.start),
                    size_type(indices.stop)
                );
            }
            return alternating_mask(
                size_type(indices.start),
                size_type(indices.stop),
                size_type(indices.step)
            );
        }

        // otherwise, we have to invert stop and start so that we can use the same
        // optimized mask generation code
        size_type norm_start = size_type(
            indices.start + (indices.step * (indices.length - 1))
        );
        size_type norm_stop = size_type(indices.start + 1);
        if (indices.step == -1) {
            return interval_mask(
                norm_start,
                norm_stop
            );
        }
        return alternating_mask(
            norm_start,
            norm_stop,
            size_type(-indices.step)
        );
    }

    constexpr bool slice_any(const Bits& mask) const noexcept {
        return static_cast<bool>(*this & mask);
    }

    constexpr bool slice_all(const Bits& mask) const noexcept {
        return (*this & mask) == mask;
    }

    constexpr size_type slice_count(const Bits& mask) const noexcept {
        size_type count = 0;
        for (size_type i = 0; i < array_size; ++i) {
            count += size_type(std::popcount(word(buffer[i] & mask.buffer[i])));
        }
        return count;
    }

    constexpr Optional<index_type> slice_first_one(const Bits& mask) const noexcept {
        for (size_type i = 0; i < array_size; ++i) {
            size_type j = size_type(std::countr_zero(
                word(buffer[i] & mask.buffer[i])
            ));
            if (j < word_size) {
                return index_type(word_size * i + j);
            }
        }
        return std::nullopt;
    }

    constexpr Optional<index_type> slice_last_one(const Bits& mask) const noexcept {
        for (size_type i = array_size; i-- > 0;) {
            size_type j = size_type(std::countl_zero(
                word(buffer[i] & mask.buffer[i])
            ));
            if (j < word_size) {
                return index_type(word_size * i + word_size - 1 - j);
            }
        }
        return std::nullopt;
    }

    constexpr Optional<index_type> slice_first_zero(const Bits& mask) const noexcept {
        for (size_type i = 0; i < array_size; ++i) {
            size_type j = size_type(std::countr_one(
                word(buffer[i] & mask.buffer[i])
            ));
            if (j < word_size) {
                return index_type(word_size * i + j);
            }
        }
        return std::nullopt;
    }

    constexpr Optional<index_type> slice_last_zero(const Bits& mask) const noexcept {
        for (size_type i = array_size; i-- > 0;) {
            size_type j = size_type(std::countl_one(
                word(buffer[i] | ~mask.buffer[i])
            ));
            if (j < word_size) {
                return index_type(word_size * i + word_size - 1 - j);
            }
        }
        return std::nullopt;
    }

    constexpr auto _mul(const Bits& other) const noexcept {
        // promote to a larger word size if possible
        if constexpr (array_size == 1 && !big_word::composite) {
            return big_word::mul(buffer[0], other.buffer[0]).value;

        // revert to schoolbook multiplication
        } else {
            // compute 2N-bit full product
            std::array<word, array_size * 2> result {};
            for (size_type i = 0; i < array_size; ++i) {
                word carry = 0;
                for (size_type j = 0; j < array_size; ++j) {
                    size_type k = j + i;
                    big_word p = big_word::mul(buffer[i], other.buffer[j]);
                    word sum = p.lo() + result[k];
                    word new_carry = sum < result[k];
                    sum += carry;
                    new_carry |= sum < carry;
                    carry = p.hi() + new_carry;  // high half -> carry to next word
                    result[k] = sum;  // low half -> final digit in this column
                }
                result[i + array_size] += carry;  // last carry of the row
            }
            return result;
        }
    }

    struct one_hot : impl::Bits_one_hot_tag {
    private:
        using normalized = bertrand::slice::normalized;

        [[no_unique_address]] union storage {
            [[no_unique_address]] std::nullopt_t borrow;
            [[no_unique_address]] Bits own;
            constexpr storage() noexcept : borrow(std::nullopt) {}
            constexpr storage(Bits&& val) noexcept : own(std::move(val)) {}
            constexpr ~storage() noexcept {
                /// NOTE: since `Bits` are trivially-destructible, we don't actually
                /// need to store a discriminator or provide any destructor logic.
            }
        } store;
        const Bits& bits;
        normalized m_indices;

    public:
        constexpr one_hot(const Bits& bits, const normalized& indices)
            noexcept
        :
            store(),
            bits(bits),
            m_indices(indices)
        {}

        constexpr one_hot(Bits&& bits, const normalized& indices)
            noexcept
        :
            store(std::move(bits)),
            bits(store.own),
            m_indices(indices)
        {}

        constexpr one_hot(const one_hot&) = delete;
        constexpr one_hot(one_hot&&) = delete;
        constexpr one_hot& operator=(const one_hot&) = delete;
        constexpr one_hot& operator=(one_hot&&) = delete;

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
            const Bits* bits = nullptr;
            normalized indices;
            value_type curr;

            constexpr iterator(const Bits& bits, const normalized& indices) noexcept :
                bits(&bits),
                indices(indices),
                curr(word(1))
            {
                curr <<= size_type(indices.start);
                if (indices.step > 0) {
                    size_type shift = size_type(indices.step);
                    while (this->indices.length) {
                        if (bits & curr) break;
                        curr <<= shift;
                        --this->indices.length;
                    }
                } else {
                    size_type shift = size_type(-indices.step);
                    while (this->indices.length) {
                        if (bits & curr) break;
                        curr >>= shift;
                        --this->indices.length;
                    }
                }
            }

        public:
            constexpr iterator() noexcept = default;

            [[nodiscard]] constexpr reference operator*() const noexcept {
                return curr;
            }

            [[nodiscard]] constexpr pointer operator->() const noexcept {
                return &curr;
            }

            constexpr iterator& operator++() noexcept {
                if (indices.step > 0) {
                    size_type shift = size_type(indices.step);
                    while (indices.length) {
                        curr <<= shift;
                        --indices.length;
                        if (*bits & curr) break;
                    }
                } else {
                    size_type shift = size_type(-indices.step);
                    while (indices.length) {
                        curr >>= shift;
                        --indices.length;
                        if (*bits & curr) break;
                    }
                }
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
                return self.indices.length == 0;
            }

            [[nodiscard]] friend constexpr bool operator==(
                impl::sentinel,
                const iterator& self
            ) noexcept {
                return self.indices.length == 0;
            }

            [[nodiscard]] friend constexpr bool operator!=(
                const iterator& self,
                impl::sentinel
            ) noexcept {
                return self.indices.length > 0;
            }

            [[nodiscard]] friend constexpr bool operator!=(
                impl::sentinel,
                const iterator& self
            ) noexcept {
                return self.indices.length > 0;
            }
        };

        /* Return a reference to the underlying bitset. */
        [[nodiscard]] constexpr const Bits& data() const noexcept { return bits; }

        /* Produce a bitmask with a one in all the indices that are contained within
        this slice, and zero everywhere else. */
        [[nodiscard]] constexpr Bits mask() const noexcept {
            return Bits::_mask(m_indices);
        }

        [[nodiscard]] constexpr const normalized& indices() const noexcept { return m_indices; }
        [[nodiscard]] constexpr ssize_t start() const noexcept { return m_indices.start; }
        [[nodiscard]] constexpr ssize_t stop() const noexcept { return m_indices.stop; }
        [[nodiscard]] constexpr ssize_t step() const noexcept { return m_indices.step; }
        [[nodiscard]] constexpr ssize_t ssize() const noexcept { return m_indices.length; }
        [[nodiscard]] constexpr size_t size() const noexcept { return size_t(ssize()); }
        [[nodiscard]] constexpr bool empty() const noexcept { return !ssize(); }
        [[nodiscard]] constexpr iterator begin() const noexcept { return {bits, m_indices}; }
        [[nodiscard]] constexpr iterator cbegin() const noexcept { return {bits, m_indices}; }
        [[nodiscard]] constexpr impl::sentinel end() const noexcept { return {}; }
        [[nodiscard]] constexpr impl::sentinel cend() const noexcept { return {}; }
    };

public:

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
        Bits* self = nullptr;
        difference_type index = 0;
        mutable value_type cache;

        constexpr iterator(Bits* self, difference_type index) noexcept :
            self(self), index(index)
        {}

    public:
        constexpr iterator() = default;

        [[nodiscard]] constexpr reference operator*() const noexcept(!DEBUG) {
            if constexpr (DEBUG) {
                if (index < 0 || index >= N) {
                    throw IndexError(std::to_string(index));
                }
            }
            cache = {&self->buffer[index / word_size], word(index % word_size)};
            return cache;
        }

        [[nodiscard]] constexpr pointer operator->() const noexcept(!DEBUG) {
            return &**this;
        }

        [[nodiscard]] constexpr value_type operator[](difference_type n) const
            noexcept(!DEBUG)
        {
            size_type idx = static_cast<size_type>(index + n);
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
        const Bits* self = nullptr;
        difference_type index = 0;
        mutable bool cache = false;

        constexpr const_iterator(const Bits* self, difference_type index) noexcept :
            self(self), index(index)
        {}

    public:
        constexpr const_iterator() = default;
        constexpr const_iterator(const iterator& other) noexcept :
            self(other.self),
            index(other.index),
            cache(other.cache)
        {}

        [[nodiscard]] constexpr const_reference operator*() const noexcept(!DEBUG) {
            if constexpr (DEBUG) {
                if (index < 0 || index >= N) {
                    throw IndexError(std::to_string(index));
                }
            }
            cache = self->buffer[index / word_size] & word(word(1) << (index % word_size));
            return cache;
        }

        [[nodiscard]] constexpr const_pointer operator->() const noexcept(!DEBUG) {
            return &**this;
        }

        [[nodiscard]] constexpr value_type operator[](difference_type n) const noexcept {
            size_type idx = static_cast<size_type>(index + n);
            if constexpr (DEBUG) {
                if (idx >= N) {
                    throw IndexError(std::to_string(index + n));
                }
            }
            return self->buffer[idx / word_size] & word(idx % word_size);
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

    /* A view over a section of the bitset with Python-style start and stop indices, as
    well as a nonzero step size.  The slice view acts just like a bitset of a smaller
    size, and does not need to be contiguous in memory.  It can be iterated over and
    implicitly converted to any other container that is constructible from a boolean
    range, including other bitsets.  Mutable slices can also be assigned to from such
    containers or from raw integers, updating the original bitset's contents
    in-place. */
    struct slice : impl::slice<iterator>, impl::Bits_slice_tag {
    private:
        friend Bits;
        using base = impl::slice<iterator>;
        using normalized = bertrand::slice::normalized;

        [[no_unique_address]] union storage {
            [[no_unique_address]] std::nullopt_t borrow;
            [[no_unique_address]] Bits own;
            constexpr storage() noexcept : borrow(std::nullopt) {}
            constexpr storage(Bits&& val) noexcept : own(std::move(val)) {}
            constexpr ~storage() noexcept {
                /// NOTE: since `Bits` are trivially-destructible, we don't actually
                /// need to store a discriminator or provide any destructor logic.
            }
        } store;
        Bits& bits;

        constexpr slice(Bits& bits, const normalized& indices)
            noexcept(noexcept(base(bits, indices)))
        :
            base(bits, indices),
            store(),
            bits(bits)
        {}

        constexpr slice(Bits&& bits, const normalized& indices)
            noexcept(noexcept(base(bits, indices)))
        :
            base(bits, indices),
            store(std::move(bits)),
            bits(store.own)
        {}

    public:
        using self = Bits;
        using base::operator=;

        /* Overwrite the slice contents with a numeric value in bitwise
        representation. */
        constexpr slice& operator=(const Bits& value) noexcept {
            Bits temp = mask();
            bits &= ~temp;  // clear the bits in the original
            bits |= value.scatter(temp);  // set the bits in the original
            return *this;
        }

        /* Return a range over the one-hot masks that make up the slice.  Iterating
        over the range yields bitsets of the same size as the underlying bitset with
        only one active bit and zero everywhere else.  Summing the masks yields an
        exact copy of the input slice.  The range may be empty if no bits are set
        within the slice. */
        [[nodiscard]] constexpr one_hot components() const noexcept {
            return {bits, base::indices()};
        }

        /* Return a reference to the underlying bitset. */
        [[nodiscard]] constexpr Bits& data() noexcept { return bits; }
        [[nodiscard]] constexpr const Bits& data() const noexcept { return bits; }

        /* Produce a bitmask with a one in all the indices that are contained within
        this slice, and zero everywhere else. */
        [[nodiscard]] constexpr Bits mask() const noexcept {
            return Bits::_mask(base::indices());
        }

        /* Check if any of the bits within the slice are set. */
        [[nodiscard]] constexpr bool any() const noexcept {
            return bits.slice_any(mask());
        }

        /* Check if all of the bits within the slice are set. */
        [[nodiscard]] constexpr bool all() const noexcept {
            return bits.slice_all(mask());
        }

        /* Get the number of bits within the slice that are currently set. */
        [[nodiscard]] constexpr size_type count() const noexcept {
            return bits.slice_count(mask());
        }

        /* Return the proper index of the first active bit within the slice, relative
        to the start of the bitset.  Returns an empty optional monad if no bits are
        set. */
        [[nodiscard]] constexpr Optional<index_type> first_one() const noexcept {
            return bits.slice_first_one(mask());
        }

        /* Return the proper index of the last active bit within the slice, relative
        to the start of the bitset.  Returns an empty optional monad if no bits are
        set. */
        [[nodiscard]] constexpr Optional<index_type> last_one() const noexcept {
            return bits.slice_last_one(mask());
        }

        /* Return the proper index of the first inactive bit within the slice, relative
        to the start of the bitset.  Returns an empty optional monad if all bits are
        set. */
        [[nodiscard]] constexpr Optional<index_type> first_zero() const noexcept {
            return bits.slice_first_zero(mask());
        }

        /* Return the proper index of the last inactive bit within the slice, relative
        to the start of the bitset.  Returns an empty optional monad if all bits are
        set. */
        [[nodiscard]] constexpr Optional<index_type> last_zero() const noexcept {
            return bits.slice_last_zero(mask());
        }

        /* Set all bits within the slice to the given value. */
        constexpr Bits& fill(bool value) noexcept {
            if (value) {
                bits |= mask();
            } else {
                bits &= ~mask();
            }
            return bits;
        }

        /* Toggle all bits within the slice. */
        constexpr Bits& flip() noexcept {
            bits ^= mask();
            return bits;
        }

        /* Reverse the order of bits within the slice. */
        constexpr Bits& reverse() noexcept {
            Bits temp = mask();
            Bits reversed = bits & temp;
            reversed.reverse();
            bits &= ~temp;  // clear the bits in the original
            bits |= reversed;  // set the reversed bits back into the original
            return bits;
        }

        /// TODO: rotate().  The only problem is that the bits would have to be rotated
        /// only in the fixed interval, meaning extras shouldn't wrap around to the
        /// other side of the full bitset, but only just the actual slice indices.
        /// -> It's possible that this can be accounted for by shifting by n times the
        /// step size, and then calculating the corrective shift amount by the
        /// start index, but this will require some thought.

        /* Convert the contents of the slice to their two's complement representation.
        This equates to flipping the sign of a signed integer. */
        constexpr Bits& negate() noexcept {
            Bits temp {*this};
            temp.negate();
            *this = temp;
            return bits;
        }

        /* See `Bits<N>::add()`. */
        [[nodiscard]] constexpr Bits add(
            const Bits& other,
            bool& overflow
        ) const noexcept {
            return Bits{*this}.add(other, overflow);
        }

        /* See `Bits<N>::add()`. */
        constexpr void add(
            const Bits& other,
            bool& overflow,
            Bits& out
        ) const noexcept {
            Bits{*this}.add(other, overflow, out);
        }

        /* See `Bits<N>::sub()`. */
        [[nodiscard]] constexpr Bits sub(
            const Bits& other,
            bool& overflow
        ) const noexcept {
            return Bits{*this}.sub(other, overflow);
        }

        /* See `Bits<N>::sub()`. */
        constexpr void sub(
            const Bits& other,
            bool& overflow,
            Bits& out
        ) const noexcept {
            Bits{*this}.sub(other, overflow, out);
        }

        /* See `Bits<N>::mul()`. */
        [[nodiscard]] constexpr Bits mul(
            const Bits& other,
            bool& overflow
        ) const noexcept {
            return Bits{*this}.mul(other, overflow);
        }

        /* See `Bits<N>::mul()`. */
        constexpr void mul(
            const Bits& other,
            bool& overflow,
            Bits& out
        ) const noexcept {
            Bits{*this}.mul(other, overflow, out);
        }

        /* See `Bits<N>::divmod()`. */
        [[nodiscard]] constexpr std::pair<Bits, Bits> divmod(
            const Bits& other
        ) const noexcept(!DEBUG) {
            return Bits{*this}.divmod(other);
        }

        /* See `Bits<N>::divmod()`. */
        [[nodiscard]] constexpr Bits divmod(
            const Bits& other,
            Bits& remainder
        ) const noexcept(!DEBUG) {
            return Bits{*this}.divmod(other, remainder);
        }

        /* See `Bits<N>::divmod()`. */
        constexpr void divmod(
            const Bits& other,
            Bits& quotient,
            Bits& remainder
        ) const noexcept(!DEBUG) {
            Bits{*this}.divmod(other, quotient, remainder);
        }

        [[nodiscard]] friend constexpr auto operator<=>(
            const slice& lhs,
            const Bits& rhs
        ) noexcept {
            return Bits{lhs} <=> rhs;
        }

        [[nodiscard]] friend constexpr auto operator<=>(
            const Bits& lhs,
            const slice& rhs
        ) noexcept {
            return lhs <=> Bits{rhs};
        }

        [[nodiscard]] friend constexpr bool operator==(
            const slice& lhs,
            const Bits& rhs
        ) noexcept {
            return Bits{lhs} == rhs;
        }

        [[nodiscard]] friend constexpr bool operator==(
            const Bits& lhs,
            const slice& rhs
        ) noexcept {
            return lhs == Bits{rhs};
        }

        [[nodiscard]] constexpr Bits operator~() const noexcept {
            return ~Bits{*this};
        }

        [[nodiscard]] friend constexpr Bits operator&(
            const slice& lhs,
            const Bits& rhs
        ) noexcept {
            return Bits{lhs} & rhs;
        }

        [[nodiscard]] friend constexpr Bits operator&(
            const Bits& lhs,
            const slice& rhs
        ) noexcept {
            return lhs & Bits{rhs};
        }

        constexpr Bits& operator&=(const Bits& other) noexcept {
            bits &= other.scatter(mask());
            return bits;
        }

        [[nodiscard]] friend constexpr Bits operator|(
            const slice& lhs,
            const Bits& rhs
        ) noexcept {
            return Bits{lhs} | rhs;
        }

        [[nodiscard]] friend constexpr Bits operator|(
            const Bits& lhs,
            const slice& rhs
        ) noexcept {
            return lhs | Bits{rhs};
        }

        constexpr Bits& operator|=(const Bits& other) noexcept {
            bits |= other.scatter(mask());
            return bits;
        }

        [[nodiscard]] friend constexpr Bits operator^(
            const slice& lhs,
            const Bits& rhs
        ) noexcept {
            return Bits{lhs} ^ rhs;
        }

        [[nodiscard]] friend constexpr Bits operator^(
            const Bits& lhs,
            const slice& rhs
        ) noexcept {
            return lhs ^ Bits{rhs};
        }

        constexpr Bits& operator^=(const Bits& other) noexcept {
            bits ^= other.scatter(mask());
            return bits;
        }

        [[nodiscard]] constexpr Bits operator<<(size_type rhs) const noexcept {
            return Bits{*this} << rhs;
        }

        constexpr Bits& operator<<=(size_type other) noexcept {
            *this = Bits{*this} << other;
            return bits;
        }

        [[nodiscard]] constexpr Bits operator>>(size_type rhs) const noexcept {
            return Bits{*this} >> rhs;
        }

        constexpr Bits& operator>>=(size_type other) noexcept {
            *this = Bits{*this} >> other;
            return bits;
        }

        [[nodiscard]] constexpr Bits operator+() const noexcept {
            return +Bits{*this};
        }

        constexpr Bits& operator++() noexcept {
            Bits temp = Bits{*this};
            ++temp;
            *this = temp;
            return bits;
        }

        [[nodiscard]] constexpr Bits operator++(int) noexcept {
            Bits copy = Bits{*this};
            ++*this;
            return copy;
        }

        [[nodiscard]] friend constexpr Bits operator+(
            const slice& lhs,
            const Bits& rhs
        ) noexcept {
            return Bits{lhs} + rhs;
        }

        [[nodiscard]] friend constexpr Bits operator+(
            const Bits& lhs,
            const slice& rhs
        ) noexcept {
            return lhs + Bits{rhs};
        }

        constexpr Bits& operator+=(const Bits& other) noexcept {
            *this = Bits{*this} + other;
            return bits;
        }

        [[nodiscard]] constexpr Bits operator-() const noexcept {
            return -Bits{*this};
        }

        constexpr Bits& operator--() noexcept {
            Bits temp = Bits{*this};
            --temp;
            *this = temp;
            return bits;
        }

        [[nodiscard]] constexpr Bits operator--(int) noexcept {
            Bits copy = Bits{*this};
            --*this;
            return copy;
        }

        [[nodiscard]] friend constexpr Bits operator-(
            const slice& lhs,
            const Bits& rhs
        ) noexcept {
            return Bits{lhs} - rhs;
        }

        [[nodiscard]] friend constexpr Bits operator-(
            const Bits& lhs,
            const slice& rhs
        ) noexcept {
            return lhs - Bits{rhs};
        }

        constexpr Bits& operator-=(const Bits& other) noexcept {
            *this = Bits{*this} - other;
            return bits;
        }

        [[nodiscard]] friend constexpr Bits operator*(
            const slice& lhs,
            const Bits& rhs
        ) noexcept {
            return Bits{lhs} * rhs;
        }

        [[nodiscard]] friend constexpr Bits operator*(
            const Bits& lhs,
            const slice& rhs
        ) noexcept {
            return lhs * Bits{rhs};
        }

        constexpr Bits& operator*=(const Bits& other) noexcept {
            *this = Bits{*this} * other;
            return bits;
        }

        [[nodiscard]] friend constexpr Bits operator/(
            const slice& lhs,
            const Bits& rhs
        ) noexcept {
            return Bits{lhs} / rhs;
        }

        [[nodiscard]] friend constexpr Bits operator/(
            const Bits& lhs,
            const slice& rhs
        ) noexcept {
            return lhs / Bits{rhs};
        }

        constexpr Bits& operator/=(const Bits& other) noexcept {
            *this = Bits{*this} / other;
            return bits;
        }

        [[nodiscard]] friend constexpr Bits operator%(
            const slice& lhs,
            const Bits& rhs
        ) noexcept {
            return Bits{lhs} % rhs;
        }

        [[nodiscard]] friend constexpr Bits operator%(
            const Bits& lhs,
            const slice& rhs
        ) noexcept {
            return lhs % Bits{rhs};
        }
    
        constexpr Bits& operator%=(const Bits& other) noexcept {
            *this = Bits{*this} % other;
            return bits;
        }

        friend constexpr std::ostream& operator<<(
            std::ostream& os,
            const slice& bits
        ) noexcept {
            return os << Bits{bits};
        }
    };

    /* A view over a section of the bitset with Python-style start and stop indices, as
    well as a nonzero step size.  The slice view acts just like an immutable bitset of
    a smaller size, and does not need to be contiguous in memory.  It can be iterated
    over and implicitly converted to any other container that is constructible from a
    boolean range, including other bitsets. */
    struct const_slice : impl::slice<const_iterator>, impl::Bits_slice_tag {
    private:
        friend Bits;
        using base = impl::slice<const_iterator>;
        using normalized = bertrand::slice::normalized;

        [[no_unique_address]] union storage {
            [[no_unique_address]] std::nullopt_t borrow;
            [[no_unique_address]] Bits own;
            constexpr storage() noexcept : borrow(std::nullopt) {}
            constexpr storage(Bits&& val) noexcept : own(std::move(val)) {}
            constexpr ~storage() noexcept {
                /// NOTE: since `Bits` are trivially-destructible, we don't actually
                /// need to store a discriminator or provide any destructor logic.
            }
        } store;
        const Bits& bits;

        constexpr const_slice(const Bits& bits, const normalized& indices)
            noexcept(noexcept(base(bits, indices)))
        :
            base(bits, indices),
            store(),
            bits(bits)
        {}

        constexpr const_slice(Bits&& bits, const normalized& indices)
            noexcept(noexcept(base(bits, indices)))
        :
            base(bits, indices),
            store(std::move(bits)),
            bits(store.own)
        {}

    public:
        using self = const Bits;

        /* Return a range over the one-hot masks that make up the slice.  Iterating
        over the range yields bitsets of the same size as the underlying bitset with
        only one active bit and zero everywhere else.  Summing the masks yields an
        exact copy of the input slice.  The range may be empty if no bits are set
        within the slice. */
        [[nodiscard]] constexpr one_hot components() const noexcept {
            return {bits, base::indices()};
        }

        /* Return a reference to the underlying bitset. */
        [[nodiscard]] constexpr const Bits& data() const noexcept {
            return bits;
        }

        /* Produce a bitmask with a one in all the indices that are contained within
        this slice, and zero everywhere else. */
        [[nodiscard]] constexpr Bits mask() const noexcept {
            return Bits::_mask(base::indices());
        }

        /* Check if any of the bits within the slice are set. */
        [[nodiscard]] constexpr bool any() const noexcept {
            return bits.slice_any(mask());
        }

        /* Check if all of the bits within the slice are set. */
        [[nodiscard]] constexpr bool all() const noexcept {
            return bits.slice_all(mask());
        }

        /* Get the number of bits within the slice that are currently set. */
        [[nodiscard]] constexpr size_type count() const noexcept {
            return bits.slice_count(mask());
        }

        /* Return the proper index of the first active bit within the slice, relative
        to the start of the bitset.  Returns an empty optional monad if no bits are
        set. */
        [[nodiscard]] constexpr Optional<index_type> first_one() const noexcept {
            return bits.slice_first_one(mask());
        }

        /* Return the proper index of the last active bit within the slice, relative
        to the start of the bitset.  Returns an empty optional monad if no bits are
        set. */
        [[nodiscard]] constexpr Optional<index_type> last_one() const noexcept {
            return bits.slice_last_one(mask());
        }

        /* Return the proper index of the first inactive bit within the slice, relative
        to the start of the bitset.  Returns an empty optional monad if all bits are
        set. */
        [[nodiscard]] constexpr Optional<index_type> first_zero() const noexcept {
            return bits.slice_first_zero(mask());
        }

        /* Return the proper index of the last inactive bit within the slice, relative
        to the start of the bitset.  Returns an empty optional monad if all bits are
        set. */
        [[nodiscard]] constexpr Optional<index_type> last_zero() const noexcept {
            return bits.slice_last_zero(mask());
        }

        /* See `Bits<N>::add()`. */
        [[nodiscard]] constexpr Bits add(
            const Bits& other,
            bool& overflow
        ) const noexcept {
            return Bits{*this}.add(other, overflow);
        }

        /* See `Bits<N>::add()`. */
        constexpr void add(
            const Bits& other,
            bool& overflow,
            Bits& out
        ) const noexcept {
            Bits{*this}.add(other, overflow, out);
        }

        /* See `Bits<N>::sub()`. */
        [[nodiscard]] constexpr Bits sub(
            const Bits& other,
            bool& overflow
        ) const noexcept {
            return Bits{*this}.sub(other, overflow);
        }

        /* See `Bits<N>::sub()`. */
        constexpr void sub(
            const Bits& other,
            bool& overflow,
            Bits& out
        ) const noexcept {
            Bits{*this}.sub(other, overflow, out);
        }

        /* See `Bits<N>::mul()`. */
        [[nodiscard]] constexpr Bits mul(
            const Bits& other,
            bool& overflow
        ) const noexcept {
            return Bits{*this}.mul(other, overflow);
        }

        /* See `Bits<N>::mul()`. */
        constexpr void mul(
            const Bits& other,
            bool& overflow,
            Bits& out
        ) const noexcept {
            Bits{*this}.mul(other, overflow, out);
        }

        /* See `Bits<N>::divmod()`. */
        [[nodiscard]] constexpr std::pair<Bits, Bits> divmod(
            const Bits& other
        ) const noexcept(!DEBUG) {
            return Bits{*this}.divmod(other);
        }

        /* See `Bits<N>::divmod()`. */
        [[nodiscard]] constexpr Bits divmod(
            const Bits& other,
            Bits& remainder
        ) const noexcept(!DEBUG) {
            return Bits{*this}.divmod(other, remainder);
        }

        /* See `Bits<N>::divmod()`. */
        constexpr void divmod(
            const Bits& other,
            Bits& quotient,
            Bits& remainder
        ) const noexcept(!DEBUG) {
            Bits{*this}.divmod(other, quotient, remainder);
        }

        [[nodiscard]] friend constexpr auto operator<=>(
            const const_slice& lhs,
            const Bits& rhs
        ) noexcept {
            return Bits{lhs} <=> rhs;
        }

        [[nodiscard]] friend constexpr auto operator<=>(
            const Bits& lhs,
            const const_slice& rhs
        ) noexcept {
            return lhs <=> Bits{rhs};
        }

        [[nodiscard]] friend constexpr bool operator==(
            const const_slice& lhs,
            const Bits& rhs
        ) noexcept {
            return Bits{lhs} == rhs;
        }

        [[nodiscard]] friend constexpr bool operator==(
            const Bits& lhs,
            const const_slice& rhs
        ) noexcept {
            return lhs == Bits{rhs};
        }

        [[nodiscard]] constexpr Bits operator~() const noexcept {
            return ~Bits{*this};
        }

        [[nodiscard]] friend constexpr Bits operator&(
            const const_slice& lhs,
            const Bits& rhs
        ) noexcept {
            return Bits{lhs} & rhs;
        }

        [[nodiscard]] friend constexpr Bits operator&(
            const Bits& lhs,
            const const_slice& rhs
        ) noexcept {
            return lhs & Bits{rhs};
        }

        [[nodiscard]] friend constexpr Bits operator|(
            const const_slice& lhs,
            const Bits& rhs
        ) noexcept {
            return Bits{lhs} | rhs;
        }

        [[nodiscard]] friend constexpr Bits operator|(
            const Bits& lhs,
            const const_slice& rhs
        ) noexcept {
            return lhs | Bits{rhs};
        }

        [[nodiscard]] friend constexpr Bits operator^(
            const const_slice& lhs,
            const Bits& rhs
        ) noexcept {
            return Bits{lhs} ^ rhs;
        }

        [[nodiscard]] friend constexpr Bits operator^(
            const Bits& lhs,
            const const_slice& rhs
        ) noexcept {
            return lhs ^ Bits{rhs};
        }

        [[nodiscard]] constexpr Bits operator<<(size_type rhs) const noexcept {
            return Bits{*this} << rhs;
        }

        [[nodiscard]] constexpr Bits operator>>(size_type rhs) const noexcept {
            return Bits{*this} >> rhs;
        }

        [[nodiscard]] constexpr Bits operator+(
            const Bits& rhs
        ) const noexcept {
            return Bits{*this} + rhs;
        }

        [[nodiscard]] friend constexpr Bits operator+(
            const const_slice& lhs,
            const Bits& rhs
        ) noexcept {
            return Bits{lhs} + rhs;
        }

        [[nodiscard]] friend constexpr Bits operator+(
            const Bits& lhs,
            const const_slice& rhs
        ) noexcept {
            return lhs + Bits{rhs};
        }

        [[nodiscard]] constexpr Bits operator-(
            const Bits& rhs
        ) const noexcept {
            return Bits{*this} - rhs;
        }

        [[nodiscard]] friend constexpr Bits operator-(
            const const_slice& lhs,
            const Bits& rhs
        ) noexcept {
            return Bits{lhs} - rhs;
        }

        [[nodiscard]] friend constexpr Bits operator-(
            const Bits& lhs,
            const const_slice& rhs
        ) noexcept {
            return lhs - Bits{rhs};
        }

        [[nodiscard]] friend constexpr Bits operator*(
            const const_slice& lhs,
            const Bits& rhs
        ) noexcept {
            return Bits{lhs} * rhs;
        }

        [[nodiscard]] friend constexpr Bits operator*(
            const Bits& lhs,
            const const_slice& rhs
        ) noexcept {
            return lhs * Bits{rhs};
        }

        [[nodiscard]] friend constexpr Bits operator/(
            const const_slice& lhs,
            const Bits& rhs
        ) noexcept {
            return Bits{lhs} / rhs;
        }

        [[nodiscard]] friend constexpr Bits operator/(
            const Bits& lhs,
            const const_slice& rhs
        ) noexcept {
            return lhs / Bits{rhs};
        }

        [[nodiscard]] friend constexpr Bits operator%(
            const const_slice& lhs,
            const Bits& rhs
        ) noexcept {
            return Bits{lhs} % rhs;
        }

        [[nodiscard]] friend constexpr Bits operator%(
            const Bits& lhs,
            const const_slice& rhs
        ) noexcept {
            return lhs % Bits{rhs};
        }
    
        friend constexpr std::ostream& operator<<(
            std::ostream& os,
            const const_slice& bits
        ) noexcept {
            return os << Bits{bits};
        }
    };

    /* Implicitly construct a bitset from a variadic parameter pack of bitset and/or
    boolean initializers of exact width.  The sequence is parsed left-to-right from
    least to most significant, meaning the first argument will correspond to the least
    significant bits, and each subsequent argument counts upward by its respective bit
    width.  Any remaining bits will be set to zero.  The total bit width of the
    arguments cannot exceed `N`, otherwise the constructor will fail to compile. */
    template <typename... words>
        requires (impl::strict_bitwise_constructor<N, words...>)
    [[nodiscard]] constexpr Bits(const words&... vals) noexcept : buffer{} {
        from_bits<0>(buffer, vals...);
    }

    /* Implicitly construct a bitset from a sequence of integer values whose bit widths
    sum to an amount less than or equal to the bitset's storage capacity.  If the
    bitset width is not an even multiple of the word size, then any upper bits above
    `N` will be masked off and initialized to zero.

    The values are parsed left-to-right from least to most significant, and are joined
    together using bitwise OR to form the final bitset.  The initializers do not need
    to have the same type, as long as their combined widths do not exceed the bitset's
    capacity.  Any remaining bits will be initialized to zero. */
    template <typename... words>
        requires (impl::loose_bitwise_constructor<capacity(), words...>)
    [[nodiscard]] constexpr Bits(const words&... vals) noexcept : buffer{} {
        from_ints<0>(buffer, vals...);
    }

    /* Explicitly construct a bitset from a sequence of integer values regardless of
    their width.  This is identical to the implicit constructors, but ignores the extra
    safety checks to allow for explicit truncation. */
    template <meta::integer... words>
        requires (
            sizeof...(words) > 0 &&
            !impl::strict_bitwise_constructor<N, words...> &&
            !impl::loose_bitwise_constructor<capacity(), words...>
        )
    [[nodiscard]] explicit constexpr Bits(const words&... vals) noexcept : buffer{} {
        from_ints<0>(buffer, vals...);
    }

    /* Construct a bitset from a string literal containing exactly `N` of the indicated
    true and false characters.  Throws a `ValueError` if the string contains any
    characters other than the indicated ones.  A constructor of this form allows for
    CTAD-based width deduction from string literal initializers. */
    [[nodiscard]] explicit constexpr Bits(
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

    /* Obtain a bitwise representation of an arbitrary type or sequence of types by
    applying a `std::bit_cast()` to each value and concatenating the results.  Trailing
    zeroes will be inserted to account for uninitialized bits, if present.  Each of the
    input types must be trivially copyable, in accordance with `std::bit_cast()`.  A
    constructor of this form allows safe, CTAD-based type punning usable in constant
    expressions, together with an equivalent explicit conversion operator. */
    template <typename... Ts> requires (impl::bit_cast_constructor<N, Ts...>)
    [[nodiscard]] explicit constexpr Bits(const Ts&... vals) noexcept :
        buffer(bitwise_repr(vals...))
    {}

    /* Explicitly construct a bitset from an iterable range yielding integers or values
    that are contextually convertible to bool, stopping at either the end of the range
    or the maximum width of the bitset, whichever comes first.  If the range yields
    integers, then they will populate a number of consecutive bits equal to their
    detected width, with any extra bits above `N` masked out.  Values are given from
    least to most significant.  A constructor of this form enables implicit conversion
    from slices, as well as the `std::ranges::to()` universal constructor. */
    template <meta::iterable T>
        requires (meta::explicitly_convertible_to<meta::yield_type<T>, bool>)
    [[nodiscard]] explicit constexpr Bits(std::from_range_t, const T& range) noexcept :
        buffer{}
    {
        if constexpr (meta::inherits<T, impl::Bits_one_hot_tag>)  {
            *this |= range.data() & range.mask();
            return;
        } else if constexpr (meta::inherits<T, impl::Bits_slice_tag>) {
            if (range.step() == 1) {
                *this |= range.data() & range.mask();
                *this >>= size_type(range.start());
                return;
            } else if (range.step() == -1) {
                *this |= range.data() & range.mask();
                size_type idx = size_type(range.start() + range.ssize() * range.step());
                *this >>= size_type(idx);
                return;
            }
        }
        size_type i = 0;
        auto it = range.begin();
        auto end = range.end();
        while (it != end && i < size()) {
            if constexpr (meta::boolean<meta::yield_type<T>>) {
                buffer[i / word_size] |=
                    (word(static_cast<bool>(*it)) << (i % word_size));
                ++i;
            } else if constexpr (meta::integer<meta::yield_type<T>>) {
                Bits temp = *it;
                temp <<= i;
                *this |= temp;
                i += meta::integer_size<meta::yield_type<T>>;
            } else {
                buffer[i / word_size] |=
                    (word(static_cast<bool>(*it)) << (i % word_size));
                ++i;
            }
            ++it;
        }
    }

    /* Trivially swap the values of two bitsets. */
    constexpr void swap(Bits& other) noexcept {
        if (this != &other) {
            buffer.swap(other.buffer);
        }
    }

    /* Efficiently generate a bit pattern with a one in all the positions that would be
    included in a hypothetical slice with the given indices.  Step magnitudes greater
    than 1 can be used to generate alternating bit patterns, starting with a one in the
    first index followed by `step - 1` zeros, repeating until the end of the slice.

    Step sizes equal to zero are invalid, and will result in a `ValueError`. */
    [[nodiscard]] static constexpr Bits mask(
        const Optional<index_type>& start = std::nullopt,
        const Optional<index_type>& stop = std::nullopt,
        const Optional<index_type>& step = std::nullopt
    )
        noexcept(noexcept(
            _mask(bertrand::slice{start, stop, step}.normalize(ssize()))
        ))
    {
        return _mask(bertrand::slice{start, stop, step}.normalize(ssize()));
    }

    /* Decode a bitset from a string representation.  Defaults to base 2 with the given
    zero and one digit strings, which are provided as template parameters.  The total
    number of digits dictates the base for the conversion, which must be at least 2 and
    at most 64.  Returns an `Expected<Bits, ValueError, OverflowError>`, where a
    `ValueError` indicates that the string contains substrings that could not be parsed
    as digits in the given base, and an `OverflowError` indicates that the resulting
    value would not fit in the bitset's storage capacity.  If the string is empty, then
    the bitset will be initialized to zero.

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
        Bits result;
        size_type idx = 0;

        // special case for base 2, which devolves to a simple bitscan
        if constexpr (sizeof...(rest) == 0) {
            while (idx < str.size()) {
                if (str.substr(idx, zero.size()) == zero) {
                    // if the most significant bit is set, then we have an overflow
                    if (result.msb_is_set()) {
                        return OverflowError();
                    }
                    result <<= word(1);
                    idx += zero.size();
                } else if (str.substr(idx, one.size()) == one) {
                    // if the most significant bit is set, then we have an overflow
                    if (result.msb_is_set()) {
                        return OverflowError();
                    }
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
                                bertrand::max(zero.size(), one.size())
                            )) + "' at index " + std::to_string(idx)
                        );
                    }
                }
            }

        // otherwise, we search against a minimal perfect hash table to get the
        // corresponding digit
        } else {
            using ctx = impl::bits_from_string<word, zero, one, rest...>;

            // loop until the string is consumed or the bitset overflows
            std::string_view continuation;
            while (idx < str.size()) {
                // attempt to extract a digit from the string, testing string
                // segments beginning at index `i` in order of decreasing width
                Expected<void, OverflowError> status = from_string_helper<0, ctx>(
                    result,
                    str,
                    idx,
                    continuation
                );
                if (status.has_error()) {
                    return std::move(status.error());
                }
                if (!continuation.empty()) {
                    if consteval {
                        static constexpr static_str message =
                            "string must contain only '" +
                            ctx::digits.template join<"', '">() + "'";
                        return ValueError();
                    } else {
                        return ValueError(
                            "string must contain only '" +
                            ctx::digits.template join<"', '">() + "', not '" +
                            std::string(str.substr(idx, ctx::widths[0])) +
                            "' at index " + std::to_string(idx)
                        );
                    }
                }
            }
        }

        return result;
    }

    /* Decode a bitset from a string representation, stopping at the first non-digit
    character.  The remaining substring will be returned via the `continuation`
    parameter, which will be empty if the entire string was consumed.  Otherwise
    behaves like `from_string<"0", "1">()`, except that the `ValueError` state will
    never occur. */
    template <static_str zero = "0", static_str one = "1", static_str... rest>
        requires (
            sizeof...(rest) + 2 <= 64 &&
            !zero.empty() && (!one.empty() && ... && !rest.empty()) &&
            meta::perfectly_hashable<zero, one, rest...>
        )
    [[nodiscard]] static constexpr Expected<Bits, OverflowError> from_string(
        std::string_view str,
        std::string_view& continuation
    ) noexcept {
        continuation = {};
        Bits result;
        size_type idx = 0;

        // special case for base 2, which devolves to a simple bitscan
        if constexpr (sizeof...(rest) == 0) {
            while (idx < str.size()) {
                if (str.substr(idx, zero.size()) == zero) {
                    // if the most significant bit is set, then we have an overflow
                    if (result.msb_is_set()) {
                        return OverflowError();
                    }
                    result <<= word(1);
                    idx += zero.size();
                } else if (str.substr(idx, one.size()) == one) {
                    // if the most significant bit is set, then we have an overflow
                    if (result.msb_is_set()) {
                        return OverflowError();
                    }
                    result <<= word(1);
                    result |= word(1);
                    idx += one.size();
                } else {
                    continuation = str.substr(idx);
                }
            }

        // otherwise, we search against a minimal perfect hash table to get the
        // corresponding digit
        } else {
            using ctx = impl::bits_from_string<word, zero, one, rest...>;

            // loop until the string is consumed or the bitset overflows
            while (idx < str.size()) {
                // attempt to extract a digit from the string, testing string
                // segments beginning at index `i` in order of decreasing width
                Expected<void, OverflowError> status = from_string_helper<0, ctx>(
                    result,
                    str,
                    idx,
                    continuation
                );
                if (status.has_error()) {
                    return std::move(status.error());
                }
            }
        }

        return result;
    }

    /* A shorthand for `from_string<"0", "1">(str)`, which decodes a string in the
    canonical binary representation. */
    [[nodiscard]] static constexpr auto from_binary(std::string_view str) noexcept
        -> Expected<Bits, ValueError, OverflowError>
    {
        return from_string<"0", "1">(str);
    }

    /* A shorthand for `from_string<"0", "1">(str, continuation)`, which decodes a
    string in the canonical binary representation. */
    [[nodiscard]] static constexpr Expected<Bits, OverflowError> from_binary(
        std::string_view str,
        std::string_view& continuation
    ) noexcept {
        return from_string<"0", "1">(str, continuation);
    }

    /* A shorthand for `from_string<"0", "1", "2", "3", "4", "5", "6", "7">(str)`,
    which decodes a string in the canonical octal representation. */
    [[nodiscard]] static constexpr auto from_octal(std::string_view str) noexcept
        -> Expected<Bits, ValueError, OverflowError>
    {
        return from_string<"0", "1", "2", "3", "4", "5", "6", "7">(str);
    }

    /* A shorthand for `from_string<"0", "1", "2", "3", "4", "5", "6", "7">(str, continuation)`,
    which decodes a string in the canonical octal representation. */
    [[nodiscard]] static constexpr Expected<Bits, OverflowError> from_octal(
        std::string_view str,
        std::string_view& continuation
    ) noexcept {
        return from_string<"0", "1", "2", "3", "4", "5", "6", "7">(str, continuation);
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
    `from_string<"0", "1", "2", "3", "4", "5", "6", "7", "8", "9">(str, continuation)`,
    which decodes a string in the canonical decimal representation. */
    [[nodiscard]] static constexpr Expected<Bits, OverflowError> from_decimal(
        std::string_view str,
        std::string_view& continuation
    ) noexcept {
        return from_string<"0", "1", "2", "3", "4", "5", "6", "7", "8", "9">(
            str,
            continuation
        );
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

    /* A shorthand for
    `from_string<"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "A", "B", "C", "D", "E", "F">(str, continuation)`,
    which decodes a string in the canonical hexadecimal representation. */
    [[nodiscard]] static constexpr Expected<Bits, OverflowError> from_hex(
        std::string_view str,
        std::string_view& continuation
    ) noexcept {
        return from_string<
            "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
            "A", "B", "C", "D", "E", "F"
        >(str, continuation);
    }

    /* Encode the bitset into a string representation.  Defaults to base 2 with the
    given zero and one digit strings, which are given as template parameters.  The
    total number of digits dictates the base for the conversion, which must be at least
    2 and at most 64.  No leading zeroes will be included in the string.

    Note that the resulting string is always big-endian, meaning the first substring
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
        size_type size = 0;
        size_type count = 0;
        auto parts = _to_string<zero, one, rest...>(size, count);

        // join the substrings in reverse order to create the final result
        std::string result;
        result.reserve(size);
        for (size_type i = count; i-- > 0;) {
            result.append(parts[i]);
        }
        return result;
    }

    /* A shorthand for `to_string<"0", "1">()`, which yields a string in the canonical
    binary representation. */
    [[nodiscard]] constexpr std::string to_binary() const noexcept {
        return to_string<"0", "1">();
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
    `Bits::from_binary()` to recover the original state. */
    [[nodiscard]] explicit constexpr operator std::string() const noexcept {
        return to_binary();
    }

    /* Bitsets evalute to true if any of their bits are set. */
    [[nodiscard]] explicit constexpr operator bool() const noexcept {
        return any();
    }

    /* Explicitly convert the bit pattern to an integer regardless of width.  If the
    width is less than that of the bitset, then only the `M` least significant bits
    will be included, where `M` is bit width of the target integer.  */
    template <meta::integer T> requires (!meta::prefer_constructor<T>)
    [[nodiscard]] explicit constexpr operator T() const noexcept {
        if constexpr (meta::integer_size<T> <= word_size) {
            return static_cast<T>(buffer[0]);
        } else {
            T out(buffer[array_size - 1]);
            for (size_type i = array_size - 1; i-- > 0;) {
                out <<= word_size;
                out |= buffer[i];
            }
            return out;
        }
    }

    /* Reinterpret the bit pattern stored in the bitset as another type by applying a
    `std::bit_cast()` on either the full array or a subset of it starting from the
    least significant bit.  Common uses for this conversion include interpreting the
    bit pattern as a floating-point value or any other POD (Plain Old Data, possibly
    aggregate) type.  Note that the result will store a copy of the underlying bit
    pattern, and does not depend on the lifetime of the bitset. */
    template <meta::trivially_copyable T> requires (!meta::integer<T>)
    [[nodiscard]] explicit constexpr operator T() const
        noexcept(meta::nothrow::copyable<T>)
        requires(sizeof(T) * 8 == N)
    {
        // if the buffer is exactly the right size, then we can just bit_cast it
        // directly
        if constexpr (sizeof(T) * 8 == capacity()) {
            return std::bit_cast<T>(buffer);

        // otherwise, we need to scale down to individual bytes and then bit_cast the
        // aligned result.
        } else {
            std::array<uint8_t, sizeof(T)> out {};
            for (size_type i = 0, j = 0; i < out.size() && j < N; ++i, j += 8) {
                out[i] = uint8_t(
                    word(buffer[j / word_size] >> (j % word_size)) & word(0xFF)
                );
            }
            return std::bit_cast<T>(out);
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

    /* Return a range over the one-hot masks that make up the bitset within a given
    interval.  Iterating over the range yields bitsets of the same size as the input
    with only one active bit and zero everywhere else.  Summing the masks yields an
    exact copy of the input bitset within the interval.  The range may be empty if no
    bits are set within the interval. */
    [[nodiscard]] constexpr one_hot components() const noexcept {
        return {*this, bertrand::slice::normalized{
            .start = 0,
            .stop = ssize(),
            .step = 1,
            .length = ssize()
        }};
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
    the index is out of bounds after normalizing.  If called on a mutable bitset, a
    smart reference is returned, which is implicitly convertible to bool, and can be
    assigned to in order to set the state of the referenced bit. */
    [[nodiscard]] constexpr reference operator[](index_type i) noexcept(!DEBUG) {
        index_type j = impl::normalize_index(ssize(), i);
        return reference{&buffer[j / word_size], word(j % word_size)};
    }
    [[nodiscard]] constexpr bool operator[](index_type i) const noexcept(!DEBUG) {
        index_type j = impl::normalize_index(ssize(), i);
        return buffer[j / word_size] & word(word(1) << (j % word_size));
    }

    /* Get a view over a range of the set as a Python-style slice.  Applies wraparound
    to the indices, and normalizes according to the step size.  The result is a range
    adaptor that yields the contents of the slice when iterated over.  The adaptor
    itself can also be implicitly converted to any other container type that  has a
    suitable range constructor or is constructible from a pair of input iterators.
    Assigning a range to an rvalue adaptor will write the new values into the slice's
    existing contents, assuming the underlying iterator is an output iterator.  If the
    start and stop indices do not denote a valid range according to the step size, then
    the resulting view will be empty. */
    [[nodiscard]] constexpr slice operator[](bertrand::slice s) &
        noexcept(noexcept(slice{*this, s.normalize(ssize())}))
    {
        return {*this, s.normalize(ssize())};
    }
    [[nodiscard]] constexpr slice operator[](bertrand::slice s) &&
        noexcept(noexcept(slice{std::move(*this), s.normalize(ssize())}))
    {
        return {std::move(*this), s.normalize(ssize())};
    }
    [[nodiscard]] constexpr const_slice operator[](bertrand::slice s) const &
        noexcept(noexcept(const_slice{*this, s.normalize(ssize())}))
    {
        return {*this, s.normalize(ssize())};
    }
    [[nodiscard]] constexpr const_slice operator[](bertrand::slice s) const &&
        noexcept(noexcept(const_slice{std::move(*this), s.normalize(ssize())}))
    {
        return {std::move(*this), s.normalize(ssize())};
    }

    /* Check if any of the bits are set. */
    [[nodiscard]] constexpr bool any() const noexcept {
        for (size_type i = 0; i < array_size; ++i) {
            if (buffer[i]) return true;
        }
        return false;
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

    /* Get the number of bits that are currently set. */
    [[nodiscard]] constexpr size_type count() const noexcept {
        size_type count = 0;
        for (size_type i = 0; i < array_size; ++i) {
            count += std::popcount(buffer[i]);
        }
        return count;
    }

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

    /* Distribute the bits within this bitset over an arbitrary mask, such that the
    least significant bit is shifted to the index of the first active bit in the mask,
    and the next bit to the second active bit, and so on.  Note that any bits that do
    not have a corresponding active bit in the mask will be discarded.  This is also
    sometimes called a "bit-deposit" operation. */
    [[nodiscard]] constexpr Bits scatter(const Bits& mask) const noexcept {
        Bits out;
        Bits src = *this;
        Bits slots = mask;
        for (Bits slot; slots; slots ^= slot) {
            slot = slots & -slots;
            if (src & 1) {
                out |= slot;
            }
            src >>= 1;
            if (!src) {
                break;
            }
        }
        return out;
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

    /* Reverse the order of all bits in the set. */
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

    /* Reverse the order of bytes in the set.  This effectively converts from
    big-endian to little-endian or vice versa. */
    constexpr Bits& reverse_bytes() noexcept {
        // reverse bytes and swap the words in the array (middle word unchanged)
        for (size_type i = 0; i < array_size / 2; ++i) {
            word temp = std::byteswap(buffer[i]);
            buffer[i] = std::byteswap(buffer[array_size - 1 - i]);
            buffer[array_size - 1 - i] = temp;
        }

        // reverse the middle word in-place
        if constexpr (array_size % 2) {
            buffer[array_size / 2] = std::byteswap(buffer[array_size / 2]);
        }

        // if there are any upper bits that should be masked off, shift down to align
        // the bits in the view window
        if constexpr (end_mask) {
            *this >>= (word_size - N % word_size);
        }
        return *this;
    }

    /* Shift all bits to the right `n` indices, wrapping any overflowing bits around to
    the left (most significant) side of the bitset.  Negative values of `n` count to
    the left instead. */
    constexpr Bits& rotate(index_type n) noexcept {
        index_type m = n % ssize();  // actual shift amount
        if (m == 0) {
            return *this;
        }
        if (n > 0) {  // m is positive
            Bits temp = *this >> m;
            *this <<= ssize() - m;
            *this |= temp;
        } else {  // m is negative
            Bits temp = *this << -m;
            *this >>= ssize() + m;
            *this |= temp;
        }
        return *this;
    }

    /* Convert the value to its two's complement equivalent, updating it in-place.
    This is equivalent to flipping the sign for a signed integral value.  For an
    out-of-place equivalent, see `operator-()`. */
    constexpr Bits& negate() noexcept {
        flip();
        ++*this;
        return *this;
    }

    /* Return +1 if the value is positive or zero, or -1 if it is negative.  For
    unsigned bitsets, this will always return +1. */
    constexpr int sign() const noexcept {
        return 1;
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
        if constexpr (array_size == 1 && !big_word::composite) {
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
        auto result = _mul(other);

        // promote to a larger word size if possible
        if constexpr (array_size == 1 && !big_word::composite) {
            if constexpr (end_mask) {
                out.buffer[0] = word(result & end_mask);
                overflow = (result >> (N % word_size)) != 0;
            } else {
                out.buffer[0] = word(result);
                overflow = (result >> word_size) != 0;
            }

        // revert to schoolbook multiplication
        } else {
            // low N bits become the final result
            std::copy_n(result.begin(), array_size, out.buffer.begin());
            if constexpr (end_mask) {
                out.buffer[array_size - 1] &= end_mask;
                overflow = (result[array_size - 1] & ~end_mask) != 0;
                if (overflow) {
                    return;  // skip following overflow check
                }
            }

            // if any of the high bits are set, then overflow has occurred
            for (size_type i = array_size; i < array_size * 2; ++i) {
                if (result[i] != 0) {
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
    ) const noexcept(!DEBUG) {
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
    ) const noexcept(!DEBUG) {
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
    ) const noexcept(!DEBUG) {
        // Check for zero divisor
        Optional<index_type> power_of_two = other.first_one();
        if constexpr (DEBUG) {
            if (!power_of_two.has_value()) {
                throw ZeroDivisionError();
            }
        }

        // If the dividend is less than the divisor, then the quotient is always zero,
        // and the remainder is trivial
        if (*this < other) {
            remainder = *this;
            quotient.fill(0);
            return;
        }

        // If the divisor is a power of two, then we can use a simple right shift and
        // bitwise AND instead of entering the full division kernel, which is
        // substantially faster
        Bits power_mask = 1;
        power_mask <<= power_of_two.value();
        if (other == power_mask) {
            Bits temp = *this;
            power_mask -= 1;
            remainder = temp & power_mask;
            temp >>= power_of_two.value();
            quotient = temp;
            return;
        }

        // If both operands are single-word, then we can default to hardware division
        if constexpr (array_size == 1) {
            word l = buffer[0];
            word r = other.buffer[0];
            quotient.buffer[0] = l / r;
            remainder.buffer[0] = l % r;

        // Otherwise, we need to use the full division algorithm.  This is based on
        // Knuth's Algorithm D, which is among the simplest for bigint division.  Much
        // of the implementation was taken from:
        //
        //      https://skanthak.hier-im-netz.de/division.html
        //      https://ridiculousfish.com/blog/posts/labor-of-division-episode-v.html
        //
        // Both of which reference Hacker's Delight, with a helpful explanation of the
        // algorithm design.  See that or the Knuth reference for more details.
        } else {
            constexpr size_type chunk = word_size / 2;
            constexpr word b = word(1) << chunk;

            // 1. Compute effective lengths as index of most significant active bit.
            // Previous checks ensure that neither operand is zero.
            size_type lhs_last;
            for (size_type i = array_size; i-- > 0;) {
                size_type j = size_type(std::countl_zero(buffer[i]));
                if (j < word_size) {
                    lhs_last = size_type(word_size * i + word_size - 1 - j);
                    break;
                }
            }
            size_type rhs_last;
            for (size_type i = array_size; i-- > 0;) {
                size_type j = size_type(std::countl_zero(other.buffer[i]));
                if (j < word_size) {
                    rhs_last = size_type(word_size * i + word_size - 1 - j);
                    break;
                }
            }
            size_type n = (rhs_last + (word_size - 1)) / word_size;
            size_type m = ((lhs_last + (word_size - 1)) / word_size) - n;

            // 2. If the divisor is a single word, then we can avoid multi-word
            // division.  This also allows us to avoid bounds checking
            if (n == 1) {
                word v = other.buffer[0];
                word rem = 0;
                for (size_type i = m + n; i-- > 0;) {
                    auto [qhat, rhat] = big_word{rem, buffer[i]}.divmod(v);
                    quotient.buffer[i] = qhat;
                    rem = rhat;
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

            // 3. Normalize by left shifting until the highest set bit in the divisor
            // is at the top of its respective word.
            std::array<word, array_size + 1> u;
            std::copy_n(buffer.begin(), array_size, u.begin());
            u[array_size] = 0;
            array v = other.buffer;
            size_type shift = word_size - 1 - (rhs_last % word_size);
            if (shift) {
                word carry = 0;
                for (size_type i = 0; i <= array_size; ++i) {
                    word new_carry = u[i] >> (word_size - shift);
                    u[i] = (u[i] << shift) | carry;
                    carry = new_carry;
                }
                carry = 0;
                for (size_type i = 0; i < array_size; ++i) {
                    word new_carry = v[i] >> (word_size - shift);
                    v[i] = (v[i] << shift) | carry;
                    carry = new_carry;
                }
            }

            // 4. Trial division
            quotient.fill(0);
            for (size_type j = m + 1; j-- > 0;) {
                // take the top two words of the numerator for wide division
                auto [qhat, rhat] = big_word{u[j + n], u[j + n - 1]}.divmod(v[n - 1]);

                // refine quotient if guess is too large
                while (qhat >= b || (
                    big_word::mul(qhat, v[n - 2]) >
                    big_word{word(rhat * b), u[j + n - 2]}
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
    [[nodiscard]] friend constexpr Bits operator~(const Bits& self) noexcept {
        Bits result;
        for (size_type i = 0; i < array_size - (end_mask > 0); ++i) {
            result.buffer[i] = ~self.buffer[i];
        }
        if constexpr (end_mask) {
            result.buffer[array_size - 1] = ~self.buffer[array_size - 1] & end_mask;
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
    constexpr Bits& operator&=(const Bits& other) noexcept {
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
    constexpr Bits& operator|=(const Bits& other) noexcept {
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
    constexpr Bits& operator^=(const Bits& other) noexcept {
        for (size_type i = 0; i < array_size; ++i) {
            buffer[i] ^= other.buffer[i];
        }
        return *this;
    }

    /* Apply a bitwise left shift to the contents of the bitset.  Shifting by more than
    the bitset width is always defined as zeroing its contents. */
    [[nodiscard]] constexpr Bits operator<<(size_type rhs) const noexcept {
        Bits result;
        size_type whole = rhs / word_size;  // whole words

        // if the shift is larger than the overall size, then we can return empty
        if (whole < N) {
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
                    result.buffer[i] = buffer[i - whole];
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
        if (whole < N) {
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
                    result.buffer[i] = buffer[i + whole];
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

    /* Return a copy of the bitset. */
    [[nodiscard]] constexpr Bits operator+() const noexcept {
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
        add(other, overflow, *this);
        return *this;
    }

    /* Return a negative copy of the bitset.  This equates to a copy followed by a
    `negate()` modifier. */
    [[nodiscard]] constexpr Bits operator-() const noexcept {
        Bits result = *this;
        result.negate();
        return result;
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
    ) noexcept(!DEBUG) {
        Bits quotient, remainder;
        lhs.divmod(rhs, quotient, remainder);
        return quotient;
    }

    /* Operator version of `Bits.divmod()` that discards the remainder and
    writes the quotient back to this bitset. */
    constexpr Bits& operator/=(const Bits& other) noexcept(!DEBUG) {
        Bits remainder;
        divmod(other, *this, remainder);
        return *this;
    }

    /* Operator version of `Bits.divmod()` that discards the quotient. */
    [[nodiscard]] friend constexpr Bits operator%(
        const Bits& lhs,
        const Bits& rhs
    ) noexcept(!DEBUG) {
        Bits quotient, remainder;
        lhs.divmod(rhs, quotient, remainder);
        return remainder;
    }

    /* Operator version of `Bits.divmod()` that discards the quotient and
    writes the remainder back to this bitset. */
    constexpr Bits& operator%=(const Bits& other) noexcept(!DEBUG) {
        Bits quotient;
        divmod(other, quotient, *this);
        return *this;
    }

    /* Print the bitset to an output stream. */
    friend constexpr std::ostream& operator<<(std::ostream& os, const Bits& self) {
        std::ostream::sentry s(os);
        if (!s) {
            return os;  // stream is not ready for output
        }

        // 1) extract configuration from stream
        impl::format_bits ctx(
            os,
            false,
            std::use_facet<std::numpunct<char>>(os.getloc())
        );

        // 2) convert to string in proper base with locale-based grouping (if any)
        std::string str = ctx.template digits_with_grouping<char>(self);

        // 3) adjust for sign + base prefix, and fill to width
        ctx.template pad_and_align<char>(str);

        // 4) write to output stream
        os.write(str.data(), str.size());
        os.width(0);  // reset width for next output (mandatory)
        return os;
    }

    /* Read the bitset from an input stream. */
    friend constexpr std::istream& operator>>(std::istream& is, Bits& self) {
        std::istream::sentry s(is);  // automatically skips leading whitespace
        if (!s) {
            return is;  // stream is not ready for input
        }
        impl::scan_bits ctx(
            is,
            std::use_facet<std::numpunct<char>>(is.getloc())
        );
        ctx.scan(is, self);
        return is;
    }
};


/* ADL `swap()` method for `bertrand::Bits` instances.  */
template <size_t N>
constexpr void swap(Bits<N>& lhs, Bits<N>& rhs) noexcept { lhs.swap(rhs); }


template <meta::integer... Ts>
UInt(Ts...) -> UInt<(meta::integer_size<Ts> + ... + 0)>;
template <meta::inherits<impl::Bits_slice_tag> T>
UInt(T) -> UInt<T::self::size()>;


template <size_t N>
struct UInt : impl::UInt_tag {
    /* The underlying bitset type for the contents of this integer. */
    using Bits = bertrand::Bits<N>;

private:
    using word = Bits::word;

public:
    /* The minimum value that the bitset can hold. */
    [[nodiscard]] static constexpr const UInt& min() noexcept {
        static constexpr UInt result = Bits::min();
        return result;
    }

    /* The maximum value that the bitset can hold. */
    [[nodiscard]] static constexpr const UInt& max() noexcept {
        static constexpr UInt result = Bits::max();
        return result;
    }

    /* A bitset holding the bitwise representation of the integer. */
    Bits bits;

    /* Construct an integer from a variadic parameter pack of component words of exact
    width.  See `Bits<N>` for more details. */
    template <typename... words>
        requires (impl::strict_bitwise_constructor<N, words...>)
    [[nodiscard]] constexpr UInt(const words&... vals) noexcept : bits(vals...) {}

    /* Construct an integer from a sequence of integer values whose bit widths sum to
    an amount less than or equal to the integer's storage capacity.  See `Bits<N>` for
    more details. */
    template <typename... words>
        requires (impl::loose_bitwise_constructor<Bits::capacity(), words...>)
    [[nodiscard]] constexpr UInt(const words&... vals) noexcept : bits(vals...) {}

    /* Explicitly construct an integer from a sequence of integer values regardless of
    their width.  This is identical to the implicit constructors, but ignores the extra
    safety checks to allow for explicit truncation. */
    template <meta::integer... words>
        requires (
            !impl::strict_bitwise_constructor<N, words...> &&
            !impl::loose_bitwise_constructor<Bits::capacity(), words...>
        )
    [[nodiscard]] explicit constexpr UInt(const words&... vals) noexcept :
        bits(vals...)
    {}

    /* Construct an integer from a range yielding values that are contextually
    convertible to bool, stopping at either the end of the range or the maximum width
    of the integer, whichever comes first.  If the range yields integer types, then they
    will populate a number of consecutive bits equal to their detected width, with any
    extra bits above the integer width masked out.  Values are given from least to most
    significant.  A constructor of this form enables implicit conversion from slices,
    as well as the `std::ranges::to()` universal constructor. */
    template <meta::iterable T>
        requires (meta::explicitly_convertible_to<meta::yield_type<T>, bool>)
    [[nodiscard]] explicit constexpr UInt(std::from_range_t, const T& range) noexcept :
        bits(std::from_range, range)
    {}

    /* Trivially swap the values of two integers. */
    constexpr void swap(UInt& other) noexcept {
        bits.swap(other.bits);
    }

    /* Decode an integer from a string representation.  Defaults to base 2 with the
    given zero and one digit strings, which are provided as template parameters, and
    whose number dictates the base for the conversion.  See `Bits<N>::from_string()`
    for more details. */
    template <static_str zero = "0", static_str one = "1", static_str... rest>
        requires (
            sizeof...(rest) + 2 <= 64 &&
            !zero.empty() && (!one.empty() && ... && !rest.empty()) &&
            meta::perfectly_hashable<zero, one, rest...>
        )
    [[nodiscard]] static constexpr auto from_string(std::string_view str) noexcept
        -> Expected<UInt, ValueError, OverflowError>
    {
        return Bits::template from_string<zero, one, rest...>(str);
    }

    /* Decode an integer from a string representation.  Defaults to base 2 with the
    given zero and one digit strings, which are provided as template parameters, and
    whose number dictates the base for the conversion.  See `Bits<N>::from_string()`
    for more details. */
    template <static_str zero = "0", static_str one = "1", static_str... rest>
        requires (
            sizeof...(rest) + 2 <= 64 &&
            !zero.empty() && (!one.empty() && ... && !rest.empty()) &&
            meta::perfectly_hashable<zero, one, rest...>
        )
    [[nodiscard]] static constexpr Expected<UInt, OverflowError> from_string(
        std::string_view str,
        std::string_view& continuation
    ) noexcept {
        return Bits::template from_string<zero, one, rest...>(str, continuation);
    }

    /* A shorthand for `from_string<"0", "1">(str)`, which decodes a string in the
    canonical binary representation. */
    [[nodiscard]] static constexpr auto from_binary(std::string_view str) noexcept
        -> Expected<UInt, ValueError, OverflowError>
    {
        return from_string<"0", "1">(str);
    }

    /* A shorthand for `from_string<"0", "1">(str, continuation)`, which decodes a
    string in the canonical binary representation. */
    [[nodiscard]] static constexpr Expected<UInt, OverflowError> from_binary(
        std::string_view str,
        std::string_view& continuation
    ) noexcept {
        return from_string<"0", "1">(str, continuation);
    }

    /* A shorthand for `from_string<"0", "1", "2", "3", "4", "5", "6", "7">(str)`,
    which decodes a string in the canonical octal representation. */
    [[nodiscard]] static constexpr auto from_octal(std::string_view str) noexcept
        -> Expected<UInt, ValueError, OverflowError>
    {
        return from_string<
            "0", "1", "2", "3", "4", "5", "6", "7"
        >(str);
    }

    /* A shorthand for `from_string<"0", "1", "2", "3", "4", "5", "6", "7">(str, continuation)`,
    which decodes a string in the canonical octal representation. */
    [[nodiscard]] static constexpr Expected<UInt, OverflowError> from_octal(
        std::string_view str,
        std::string_view& continuation
    ) noexcept {
        return from_string<
            "0", "1", "2", "3", "4", "5", "6", "7"
        >(str, continuation);
    }

    /* A shorthand for
    `from_string<"0", "1", "2", "3", "4", "5", "6", "7", "8", "9">(str)`, which decodes
    a string in the canonical decimal representation. */
    [[nodiscard]] static constexpr auto from_decimal(std::string_view str) noexcept
        -> Expected<UInt, ValueError, OverflowError>
    {
        return from_string<
            "0", "1", "2", "3", "4", "5", "6", "7", "8", "9"
        >(str);
    }

    /* A shorthand for
    `from_string<"0", "1", "2", "3", "4", "5", "6", "7", "8", "9">(str, continuation)`,
    which decodes a string in the canonical decimal representation. */
    [[nodiscard]] static constexpr Expected<UInt, OverflowError> from_decimal(
        std::string_view str,
        std::string_view& continuation
    ) noexcept {
        return from_string<
            "0", "1", "2", "3", "4", "5", "6", "7", "8", "9"
        >(str, continuation);
    }

    /* A shorthand for
    `from_string<"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "A", "B", "C", "D", "E", "F">(str)`,
    which decodes a string in the canonical hexadecimal representation. */
    [[nodiscard]] static constexpr auto from_hex(std::string_view str) noexcept
        -> Expected<UInt, ValueError, OverflowError>
    {
        return from_string<
            "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
            "A", "B", "C", "D", "E", "F"
        >(str);
    }

    /* A shorthand for
    `from_string<"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "A", "B", "C", "D", "E", "F">(str, continuation)`,
    which decodes a string in the canonical hexadecimal representation. */
    [[nodiscard]] static constexpr Expected<UInt, OverflowError> from_hex(
        std::string_view str,
        std::string_view& continuation
    ) noexcept {
        return from_string<
            "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
            "A", "B", "C", "D", "E", "F"
        >(str, continuation);
    }

    /* Encode an integer into a string representation.  Defaults to base 2 with the
    given zero and one digit strings, which are provided as template parameters, and
    whose number dictates the base for the conversion.  See `Bits<N>::to_string()` for
    more details. */
    template <static_str zero = "0", static_str one = "1", static_str... rest>
        requires (
            sizeof...(rest) + 2 <= 64 &&
            !zero.empty() && !one.empty() && (... && !rest.empty()) &&
            meta::strings_are_unique<zero, one, rest...>
        )
    [[nodiscard]] constexpr std::string to_string() const noexcept {
        return bits.template to_string<zero, one, rest...>();
    }

    /* A shorthand for `to_string<"0", "1">()`, which yields a string in the canonical
    binary representation. */
    [[nodiscard]] constexpr std::string to_binary() const noexcept {
        return to_string<"0", "1">();
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

    /* Convert the integer to a decimal string representation. */
    [[nodiscard]] explicit constexpr operator std::string() const noexcept {
        return to_decimal();
    }

    /* Non-zero integers evaluate to true under boolean logic. */
    [[nodiscard]] explicit constexpr operator bool() const noexcept {
        return bool(bits);
    }

    /* Implicitly convert a single-word integer to its underlying integer
    representation. */
    [[nodiscard]] constexpr operator word() const noexcept
        requires(Bits::array_size == 1)
    {
        return word(bits);
    }

    /* Explicitly convert a multi-word integer into a different integer type, possibly
    truncating any upper bits. */
    template <meta::integer T> requires (!meta::prefer_constructor<T>)
    [[nodiscard]] explicit constexpr operator T() const noexcept
        requires(Bits::array_size > 1)
    {
        return static_cast<T>(bits);
    }

    /* Explicitly convert a multi-word integer into a floating point type, retaining as
    much precision as possible. */
    template <meta::floating T> requires (!meta::prefer_constructor<T>)
    [[nodiscard]] explicit constexpr operator T() const noexcept
        requires(Bits::array_size > 1)
    {
        T value = 0;
        for (size_t i = Bits::array_size; i-- > 0;) {
            value = std::ldexp(value, Bits::word_size) + static_cast<T>(bits.data()[i]);
        }
        return value;
    }

    /* Return +1 if the integer is positive or zero, or -1 if it is negative.  For
    unsigned integers, this will always return +1. */
    constexpr int sign() const noexcept {
        return bits.sign();
    }

    /* Add two integers of equal size.  If the result overflows, then the `overflow`
    flag will be set to true, and the value will wrap around to the other end of the
    number line (module `N`). */
    [[nodiscard]] constexpr UInt add(
        const UInt& other,
        bool& overflow
    ) const noexcept {
        return bits.add(other.bits, overflow);
    }

    /* Add two integers of equal size and store the sum as a mutable out parameter.  If
    the result overflows, then the `overflow` flag will be set to true, and the value
    will wrap around to the other end of the number line (modulo `N`).  The out
    parameter may be a reference to either operand, without affecting the overall
    calculation. */
    constexpr void add(
        const UInt& other,
        bool& overflow,
        UInt& out
    ) const noexcept {
        bits.add(other.bits, overflow, out.bits);
    }

    /* Subtract two integers of equal size.  If the result overflows, then the
    `overflow` flag will be set to true, and the value will wrap around to the other
    end of the number line (modulo `N`). */
    [[nodiscard]] constexpr UInt sub(
        const UInt& other,
        bool& overflow
    ) const noexcept {
        return bits.sub(other.bits, overflow);
    }

    /* Subtract two bitsets of equal size and store the difference as a mutable out
    parameter.  If the result overflows, then the `overflow` flag will be set to true,
    and the value will wrap around to the other end of the number line (modulo `N`).
    The out parameter may be a reference to either operand, without affecting the
    overall calculation. */
    constexpr void sub(
        const UInt& other,
        bool& overflow,
        UInt& out
    ) const noexcept {
        bits.sub(other.bits, overflow, out.bits);
    }

    /* Multiply two integers of equal size.  If the result overflows, then the
    `overflow` flag will be set to true, and the value will wrap around to the other
    end of the number line (modulo `N`).

    Note that this uses simple schoolbook multiplication under the hood, which is
    generally optimized for small bit counts (up to a few thousand bits).  For larger
    bit counts, it may be more efficient to use a specialized library such as GMP or
    `boost::multiprecision`. */
    [[nodiscard]] constexpr UInt mul(
        const UInt& other,
        bool& overflow
    ) const noexcept {
        return bits.mul(other.bits, overflow);
    }

    /* Multiply two integers of equal size and store the product as a mutable out
    parameter.  If the result overflows, then the `overflow` flag will be set to true,
    and the value will wrap around to the other end of the number line (modulo `N`).
    The out parameter may be a reference to either operand, without affecting the
    overall calculation.

    Note that this uses simple schoolbook multiplication under the hood, which is
    generally optimized for small bit counts (up to a few thousand bits).  For larger
    bit counts, it may be more efficient to use a specialized library such as GMP or
    `boost::multiprecision`. */
    constexpr void mul(
        const UInt& other,
        bool& overflow,
        UInt& out
    ) const noexcept {
        bits.mul(other.bits, overflow, out.bits);
    }

    /* Divide two integers of equal size, returning both the quotient and remainder.
    If the divisor is zero and the program is compiled in debug mode, then a
    `ZeroDivisionError` will be thrown.

    Note that this uses Knuth's Algorithm D under the hood, which is generally
    optimized for small bit counts (up to a few thousand bits).  For larger bit counts,
    it may be more efficient to use a specialized library such as GMP or
    `boost::multiprecision`. */
    [[nodiscard]] constexpr std::pair<UInt, UInt> divmod(
        const UInt& other
    ) const noexcept(!DEBUG) {
        auto [quotient, remainder] = bits.divmod(other.bits);
        return {quotient, remainder};
    }

    /* Divide two integers of equal size, returning the quotient and storing the
    remainder as a mutable out parameter.  If the divisor is zero and the program is
    compiled in debug mode, then a `ZeroDivisionError` will be thrown.

    Listing either the dividend or divisor as the out parameter for the remainder is
    allowed (but not required), and will update the referenced integer in-place, without
    affecting the calculation.

    Note that this uses Knuth's Algorithm D under the hood, which is generally
    optimized for small bit counts (up to a few thousand bits).  For larger bit counts,
    it may be more efficient to use a specialized library such as GMP or
    `boost::multiprecision`. */
    [[nodiscard]] constexpr UInt divmod(
        const UInt& other,
        UInt& remainder
    ) const noexcept(!DEBUG) {
        return bits.divmod(other.bits, remainder.bits);
    }

    /* Divide two integers of equal size, storing both the quotient and remainder as
    mutable out parameters.  If the divisor is zero and the program is compiled in
    debug mode, then a `ZeroDivisionError` will be thrown.

    Listing either the dividend or divisor as the out parameter for the quotient or
    remainder is allowed (but not required), and will update the referenced integer
    in-place, without affecting the calculation.

    Note that this uses Knuth's Algorithm D under the hood, which is generally
    optimized for small bit counts (up to a few thousand bits).  For larger bit counts,
    it may be more efficient to use a specialized library such as GMP or
    `boost::multiprecision`. */
    constexpr void divmod(
        const UInt& other,
        UInt& quotient,
        UInt& remainder
    ) const noexcept(!DEBUG) {
        bits.divmod(other.bits, quotient.bits, remainder.bits);
    }

    /* Compare two integers of equal size. */
    [[nodiscard]] friend constexpr auto operator<=>(
        const UInt& lhs,
        const UInt& rhs
    ) noexcept requires(Bits::array_size > 1) {
        return lhs.bits <=> rhs.bits;
    }

    /* Compare two integers of equal size. */
    [[nodiscard]] friend constexpr bool operator==(
        const UInt& lhs,
        const UInt& rhs
    ) noexcept requires(Bits::array_size > 1) {
        return lhs.bits == rhs.bits;
    }

    /* Apply a bitwise NOT to the integer. */
    [[nodiscard]] constexpr UInt operator~() const noexcept {
        return ~bits;
    }

    /* Apply a bitwise AND between the contents of two integers of equal size. */
    [[nodiscard]] friend constexpr UInt operator&(
        const UInt& lhs,
        const UInt& rhs
    ) noexcept requires(Bits::array_size > 1) {
        return lhs.bits & rhs.bits;
    }

    /* Apply a bitwise AND between the contents of this integer and another of equal
    length, updating the former in-place. */
    constexpr UInt& operator&=(
        const UInt& other
    ) noexcept {
        bits &= other.bits;
        return *this;
    }

    /* Apply a bitwise OR between the contents of two integers of equal size. */
    [[nodiscard]] friend constexpr UInt operator|(
        const UInt& lhs,
        const UInt& rhs
    ) noexcept requires(Bits::array_size > 1) {
        return lhs.bits | rhs.bits;
    }

    /* Apply a bitwise OR between the contents of this integer and another of equal
    length, updating the former in-place  */
    constexpr UInt& operator|=(
        const UInt& other
    ) noexcept {
        bits |= other.bits;
        return *this;
    }

    /* Apply a bitwise XOR between the contents of two integers of equal size. */
    [[nodiscard]] friend constexpr UInt operator^(
        const UInt& lhs,
        const UInt& rhs
    ) noexcept requires(Bits::array_size > 1) {
        return lhs.bits ^ rhs.bits;
    }

    /* Apply a bitwise XOR between the contents of this integer and another of equal
    length, updating the former in-place. */
    constexpr UInt& operator^=(
        const UInt& other
    ) noexcept {
        bits ^= other.bits;
        return *this;
    }

    /* Apply a bitwise left shift to the contents of the integer. */
    [[nodiscard]] constexpr UInt operator<<(size_t rhs) const noexcept
        requires(Bits::array_size > 1)
    {
        return bits << rhs;
    }

    /* Apply a bitwise left shift to the contents of this integer, updating it
    in-place. */
    constexpr UInt& operator<<=(size_t rhs) noexcept {
        bits <<= rhs;
        return *this;
    }

    /* Apply a bitwise right shift to the contents of the integer. */
    [[nodiscard]] constexpr UInt operator>>(size_t rhs) const noexcept
        requires(Bits::array_size > 1)
    {
        return bits >> rhs;
    }

    /* Apply a bitwise right shift to the contents of this integer, updating it
    in-place. */
    constexpr UInt& operator>>=(size_t rhs) noexcept {
        bits >>= rhs;
        return *this;
    }

    /* Return a positive copy of the integer.  This devolves to a simple copy for
    unsigned integers. */
    [[nodiscard]] constexpr UInt operator+() const noexcept { return *this; }

    /* Increment a integer by one and return a reference to the new value. */
    constexpr UInt& operator++() noexcept {
        ++bits;
        return *this;
    }

    /* Increment the integer by one and return a copy of the old value. */
    [[nodiscard]] constexpr UInt operator++(int) noexcept {
        UInt copy = *this;
        ++bits;
        return copy;
    }

    /* Operator version of `UInt.add()` that discards the overflow flag. */
    [[nodiscard]] friend constexpr UInt operator+(
        const UInt& lhs,
        const UInt& rhs
    ) noexcept requires(Bits::array_size > 1) {
        return lhs.bits + rhs.bits;
    }

    /* Operator version of `UInt.add()` that discards the overflow flag and
    writes the sum back to this integer. */
    constexpr UInt& operator+=(const UInt& other) noexcept {
        bits += other.bits;
        return *this;
    }

    /* Return a negative copy of the integer.  This equates to a copy followed by a
    `negate()` modifier. */
    [[nodiscard]] constexpr UInt operator-() const noexcept {
        return -bits;
    }

    /* Decrement the integer by one and return a reference to the new value. */
    constexpr UInt& operator--() noexcept {
        --bits;
        return *this;
    }

    /* Decrement the integer by one and return a copy of the old value. */
    [[nodiscard]] constexpr UInt operator--(int) noexcept {
        UInt copy = *this;
        --bits;
        return copy;
    }

    /* Operator version of `UInt.sub()` that discards the overflow flag. */
    [[nodiscard]] friend constexpr UInt operator-(
        const UInt& lhs,
        const UInt& rhs
    ) noexcept requires(Bits::array_size > 1) {
        return lhs.bits - rhs.bits;
    }

    /* Operator version of `UInt.sub()` that discards the overflow flag and
    writes the difference back to this integer. */
    constexpr UInt& operator-=(const UInt& other) noexcept {
        bits -= other.bits;
        return *this;
    }

    /* Operator version of `UInt.mul()` that discards the overflow flag. */
    [[nodiscard]] friend constexpr UInt operator*(
        const UInt& lhs,
        const UInt& rhs
    ) noexcept requires(Bits::array_size > 1) {
        return lhs.bits * rhs.bits;
    }

    /* Operator version of `UInt.mul()` that discards the overflow flag and writes
    the product back to this integer. */
    constexpr UInt& operator*=(const UInt& other) noexcept {
        bits *= other.bits;
        return *this;
    }

    /* Operator version of `UInt.divmod()` that discards the remainder. */
    [[nodiscard]] friend constexpr UInt operator/(
        const UInt& lhs,
        const UInt& rhs
    ) noexcept(!DEBUG) requires(Bits::array_size > 1) {
        return lhs.bits / rhs.bits;
    }

    /* Operator version of `UInt.divmod()` that discards the remainder and
    writes the quotient back to this integer. */
    constexpr UInt& operator/=(const UInt& other) noexcept(!DEBUG) {
        bits /= other.bits;
        return *this;
    }

    /* Operator version of `UInt.divmod()` that discards the quotient. */
    [[nodiscard]] friend constexpr UInt operator%(
        const UInt& lhs,
        const UInt& rhs
    ) noexcept(!DEBUG) requires(Bits::array_size > 1) {
        return lhs.bits % rhs.bits;
    }

    /* Operator version of `UInt.divmod()` that discards the quotient and
    writes the remainder back to this integer. */
    constexpr UInt& operator%=(const UInt& other) noexcept(!DEBUG) {
        bits %= other.bits;
        return *this;
    }

    /* Print the integer to an output stream. */
    constexpr friend std::ostream& operator<<(
        std::ostream& os,
        const UInt& self
    ) {
        return os << self.bits;  // defaults to decimal
    }

    /* Read the integer from an input stream. */
    constexpr friend std::istream& operator>>(
        std::istream& is,
        UInt& self
    ) {
        return is >> self.bits;  // defaults to decimal
    }
};


/* ADL `swap()` method for `bertrand::UInt` instances. */
template <size_t N>
constexpr void swap(UInt<N>& lhs, UInt<N>& rhs) noexcept { lhs.swap(rhs); }


template <meta::integer... Ts>
Int(Ts...) -> Int<(meta::integer_size<Ts> + ... + 0)>;
template <meta::inherits<impl::Bits_slice_tag> T>
Int(T) -> Int<T::self::size()>;


template <size_t N>
struct Int : impl::Int_tag {
    /* The underlying bitset type for the contents of this integer. */
    using Bits = bertrand::Bits<N>;

private:
    using word = Bits::word;

public:
    /* The minimum value that the integer can store. */
    [[nodiscard]] static constexpr const Int& min() noexcept {
        static constexpr Int result = [] {
            // only most significant bit is set -> two's complement
            Int result;
            if constexpr (Bits::end_mask) {
                result.bits.data()[Bits::array_size - 1] =
                    word(word(1) << ((N % Bits::word_size) - 1));
            } else {
                result.bits.data()[Bits::array_size - 1] =
                    word(word(1) << (Bits::word_size - 1));
            }
            return result;
        }();
        return result;
    }

    /* The maximum value that the integer can store. */
    [[nodiscard]] static constexpr const Int& max() noexcept {
        static constexpr Int result = [] {
            // all but the most significant bit are set -> two's complement
            Int result;
            for (size_t i = 0; i < Bits::array_size - 1; ++i) {
                result.bits.data()[i] = std::numeric_limits<word>::max();
            }
            if constexpr (Bits::end_mask) {
                result.bits.data()[Bits::array_size - 1] = word(Bits::end_mask >> 1);
            } else {
                result.bits.data()[Bits::array_size - 1] =
                    word(std::numeric_limits<word>::max() >> 1);
            }
            return result;
        }();
        return result;
    }

    /* A bitset holding the bitwise representation of the integer. */
    Bits bits;

    /* Construct an integer from a variadic parameter pack of component words of exact
    width.  See `Bits<N>` for more details. */
    template <typename... words>
        requires (impl::strict_bitwise_constructor<N, words...>)
    [[nodiscard]] constexpr Int(const words&... vals) noexcept : bits(vals...) {}

    /* Construct an integer from a sequence of integer values whose bit widths sum to
    an amount less than or equal to the integer's storage capacity.  See `Bits<N>` for
    more details. */
    template <typename... words>
        requires (impl::loose_bitwise_constructor<Bits::capacity(), words...>)
    [[nodiscard]] constexpr Int(const words&... vals) noexcept : bits(vals...) {}

    /* Explicitly construct an integer from a sequence of integer values regardless of
    their width.  This is identical to the implicit constructors, but ignores the extra
    safety checks to allow for explicit truncation. */
    template <meta::integer... words>
        requires (
            !impl::strict_bitwise_constructor<N, words...> &&
            !impl::loose_bitwise_constructor<Bits::capacity(), words...>
        )
    [[nodiscard]] explicit constexpr Int(const words&... vals) noexcept :
        bits(vals...)
    {}

    /* Explicitly convert a floating point value into an integer, possibly truncating
    towards zero. */
    template <meta::floating T>
    [[nodiscard]] explicit constexpr Int(const T& val) noexcept :
        bits()
    {
        /// TODO: convert a floating point type to an integer safely, and retaining
        /// as much precision as possible
    }

    /* Construct an integer from a range yielding values that are contextually
    convertible to bool, stopping at either the end of the range or the maximum width
    of the integer, whichever comes first.  If the range yields integer types, then they
    will populate a number of consecutive bits equal to their detected width, with any
    extra bits above the integer width masked out.  Values are given from least to most
    significant.  A constructor of this form enables implicit conversion from slices,
    as well as the `std::ranges::to()` universal constructor. */
    template <meta::iterable T>
        requires (meta::explicitly_convertible_to<meta::yield_type<T>, bool>)
    [[nodiscard]] explicit constexpr Int(std::from_range_t, const T& range) noexcept :
        bits(std::from_range, range)
    {}

    /* Trivially swap the values of two integers. */
    constexpr void swap(Int& other) noexcept {
        bits.swap(other.bits);
    }

    /* Decode an integer from a string representation.  Defaults to base 2 with the
    given zero and one digit strings, which are provided as template parameters, and
    whose number dictates the base for the conversion.  See `Bits<N>::from_string()`
    for more details. */
    template <
        static_str negative = "-",
        static_str zero = "0",
        static_str one = "1",
        static_str... rest
    >
        requires (
            sizeof...(rest) + 2 <= 64 &&
            !zero.empty() && (!one.empty() && ... && !rest.empty()) &&
            meta::perfectly_hashable<zero, one, rest...>
        )
    [[nodiscard]] static constexpr auto from_string(std::string_view str) noexcept
        -> Expected<Int, ValueError, OverflowError>
    {
        if (str.starts_with(std::string_view(negative))) {
            return Bits::template from_string<zero, one, rest...>(
                str.substr(negative.size())
            ).visit([](const Bits& bits) -> Int {
                Int result = bits;
                result.bits.negate();
                return result;
            });
        } else {
            return Bits::template from_string<zero, one, rest...>(str);
        }
    }

    /* Decode an integer from a string representation.  Defaults to base 2 with the
    given negative sign, zero, and one digit strings, which are provided as template
    parameters, and whose number dictates the base for the conversion.  See
    `Bits<N>::from_string()` for more details. */
    template <
        static_str negative = "-",
        static_str zero = "0",
        static_str one = "1",
        static_str... rest
    >
        requires (
            sizeof...(rest) + 2 <= 64 &&
            !zero.empty() && (!one.empty() && ... && !rest.empty()) &&
            meta::perfectly_hashable<zero, one, rest...>
        )
    [[nodiscard]] static constexpr Expected<Int, OverflowError> from_string(
        std::string_view str,
        std::string_view& continuation
    ) noexcept {
        if (str.starts_with(std::string_view(negative))) {
            return Bits::template from_string<zero, one, rest...>(
                str.substr(negative.size()),
                continuation
            ).visit([](const Bits& bits) -> Int {
                Int result = bits;
                result.bits.negate();
                return result;
            });
        } else {
            return Bits::template from_string<zero, one, rest...>(str, continuation);
        }
    }

    /* A shorthand for `from_string<"-", "0", "1">(str)`, which decodes a string in the
    canonical binary representation. */
    [[nodiscard]] static constexpr auto from_binary(std::string_view str) noexcept
        -> Expected<Int, ValueError, OverflowError>
    {
        return from_string<"-", "0", "1">(str);
    }

    /* A shorthand for `from_string<"-", "0", "1">(str, continuation)`, which decodes a
    string in the canonical binary representation. */
    [[nodiscard]] static constexpr Expected<Int, OverflowError> from_binary(
        std::string_view str,
        std::string_view& continuation
    ) noexcept {
        return from_string<"-", "0", "1">(str, continuation);
    }

    /* A shorthand for `from_string<"-", "0", "1", "2", "3", "4", "5", "6", "7">(str)`,
    which decodes a string in the canonical octal representation. */
    [[nodiscard]] static constexpr auto from_octal(std::string_view str) noexcept
        -> Expected<Int, ValueError, OverflowError>
    {
        return from_string<
            "-", "0", "1", "2", "3", "4", "5", "6", "7"
        >(str);
    }

    /* A shorthand for
    `from_string<"-", "0", "1", "2", "3", "4", "5", "6", "7">(str, continuation)`,
    which decodes a string in the canonical octal representation. */
    [[nodiscard]] static constexpr Expected<Int, OverflowError> from_octal(
        std::string_view str,
        std::string_view& continuation
    ) noexcept {
        return from_string<
            "-", "0", "1", "2", "3", "4", "5", "6", "7"
        >(str, continuation);
    }

    /* A shorthand for
    `from_string<"-", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9">(str)`, which
    decodes a string in the canonical decimal representation. */
    [[nodiscard]] static constexpr auto from_decimal(std::string_view str) noexcept
        -> Expected<Int, ValueError, OverflowError>
    {
        return from_string<
            "-", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9"
        >(str);
    }

    /* A shorthand for
    `from_string<"-", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9">(str, continuation)`,
    which decodes a string in the canonical decimal representation. */
    [[nodiscard]] static constexpr Expected<Int, OverflowError> from_decimal(
        std::string_view str,
        std::string_view& continuation
    ) noexcept {
        return from_string<
            "-", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9"
        >(str, continuation);
    }

    /* A shorthand for
    `from_string<"-", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "A", "B", "C", "D", "E", "F">(str)`,
    which decodes a string in the canonical hexadecimal representation. */
    [[nodiscard]] static constexpr auto from_hex(std::string_view str) noexcept
        -> Expected<Int, ValueError, OverflowError>
    {
        return from_string<
            "-", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
            "A", "B", "C", "D", "E", "F"
        >(str);
    }

    /* A shorthand for
    `from_string<"-", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "A", "B", "C", "D", "E", "F">(str, continuation)`,
    which decodes a string in the canonical hexadecimal representation. */
    [[nodiscard]] static constexpr Expected<Int, OverflowError> from_hex(
        std::string_view str,
        std::string_view& continuation
    ) noexcept {
        return from_string<
            "-", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
            "A", "B", "C", "D", "E", "F"
        >(str, continuation);
    }

    /* Encode an integer into a string representation.  Defaults to base 2 with the
    given negative sign, zero, and one digit strings, which are provided as template
    parameters, and whose number dictates the base for the conversion.  See
    `Bits<N>::to_string()` for more details. */
    template <
        static_str negative = "-",
        static_str zero = "0",
        static_str one = "1",
        static_str... rest
    >
        requires (
            sizeof...(rest) + 2 <= 64 &&
            !zero.empty() && !one.empty() && (... && !rest.empty()) &&
            meta::strings_are_unique<zero, one, rest...>
        )
    [[nodiscard]] constexpr std::string to_string() const noexcept {
        using size_type = Bits::size_type;
        size_type size = 0;
        size_type count = 0;
        auto parts = bits.template _to_string<zero, one, rest...>(size, count);

        // join the substrings in reverse order to create the final result
        std::string result;
        if (bits.msb_is_set()) {
            result.reserve(size + 1);
            result.append(std::string_view(negative));
        } else {
            result.reserve(size);
        }
        for (size_type i = count; i-- > 0;) {
            result.append(parts[i]);
        }
        return result;
    }

    /* A shorthand for `to_string<"-", "0", "1">()`, which yields a string in the canonical
    binary representation. */
    [[nodiscard]] constexpr std::string to_binary() const noexcept {
        return to_string<"-", "0", "1">();
    }

    /* A shorthand for `to_string<"-", "0", "1", "2", "3", "4", "5", "6", "7">()`, which
    yields a string in the canonical octal representation. */
    [[nodiscard]] constexpr std::string to_octal() const noexcept {
        return to_string<"-", "0", "1", "2", "3", "4", "5", "6", "7">();
    }

    /* A shorthand for `to_string<"-", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9">()`,
    which yields a string in the canonical decimal representation. */
    [[nodiscard]] constexpr std::string to_decimal() const noexcept {
        return to_string<"-", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9">();
    }

    /* A shorthand for
    `to_string<"-", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "A", "B", "C", "D", "E", "F">()`,
    which yields a string in the canonical hexadecimal representation. */
    [[nodiscard]] constexpr std::string to_hex() const noexcept {
        return to_string<
            "-", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
            "A", "B", "C", "D", "E", "F"
        >();
    }

    /* Convert the integer to a decimal string representation. */
    [[nodiscard]] explicit constexpr operator std::string() const noexcept {
        return to_decimal();
    }

    /* Non-zero integers evaluate to true under boolean logic. */
    [[nodiscard]] explicit constexpr operator bool() const noexcept {
        return bool(bits);
    }

    /* Implicitly convert a single-word integer to its underlying integer
    representation. */
    [[nodiscard]] constexpr operator meta::as_signed<word>() const noexcept
        requires(Bits::array_size == 1)
    {
        if (bits.msb_is_set()) {
            return -meta::as_signed<word>(-bits);
        } else {
            return meta::as_signed<word>(bits);
        }
    }

    /* Explicitly convert a multi-word integer into another integer type, possibly
    truncating any upper bits. */
    template <meta::integer T> requires (!meta::prefer_constructor<T>)
    [[nodiscard]] explicit constexpr operator T() const noexcept
        requires(Bits::array_size > 1)
    {
        return static_cast<T>(bits);
    }

    /* Explicitly convert a multi-word integer into a floating point type, retaining as
    much precision as possible. */
    template <meta::floating T> requires (!meta::prefer_constructor<T>)
    [[nodiscard]] explicit constexpr operator T() const noexcept
        requires(Bits::array_size > 1)
    {
        T value = 0;
        for (size_t i = Bits::array_size; i-- > 0;) {
            value = std::ldexp(value, Bits::word_size) + static_cast<T>(bits.data()[i]);
        }
        return bits.msb_is_set() ? -value : value;
    }

    /* Return +1 if the integer is positive or zero, or -1 if it is negative. */
    [[nodiscard]] constexpr int sign() const noexcept {
        return 1 - (2 * bits.msb_is_set());
    }

    /* Add two integers of equal size.  If the result overflows, then the `overflow`
    flag will be set to true, and the value will wrap around to the other end of the
    number line (module `N`). */
    [[nodiscard]] constexpr Int add(
        const Int& other,
        bool& overflow
    ) const noexcept {
        bool this_neg = bits.msb_is_set();
        bool other_neg = other.bits.msb_is_set();
        Int result = bits.add(other.bits, overflow);
        overflow = this_neg == other_neg && result.bits.msb_is_set() != this_neg;
        return result;
    }

    /* Add two integers of equal size and store the sum as a mutable out parameter.  If
    the result overflows, then the `overflow` flag will be set to true, and the value
    will wrap around to the other end of the number line (modulo `N`).  The out
    parameter may be a reference to either operand, without affecting the overall
    calculation. */
    constexpr void add(
        const Int& other,
        bool& overflow,
        Int& out
    ) const noexcept {
        bool this_neg = bits.msb_is_set();
        bool other_neg = other.bits.msb_is_set();
        bits.add(other.bits, overflow, out.bits);
        overflow = this_neg == other_neg && out.bits.msb_is_set() != this_neg;
    }

    /* Subtract two integers of equal size.  If the result overflows, then the
    `overflow` flag will be set to true, and the value will wrap around to the other
    end of the number line (modulo `N`). */
    [[nodiscard]] constexpr Int sub(
        const Int& other,
        bool& overflow
    ) const noexcept {
        bool this_neg = bits.msb_is_set();
        bool other_neg = other.bits.msb_is_set();
        Int result = bits.sub(other.bits, overflow);
        overflow = this_neg != other_neg && result.bits.msb_is_set() != this_neg;
        return result;
    }

    /* Subtract two bitsets of equal size and store the difference as a mutable out
    parameter.  If the result overflows, then the `overflow` flag will be set to true,
    and the value will wrap around to the other end of the number line (modulo `N`).
    The out parameter may be a reference to either operand, without affecting the
    overall calculation. */
    constexpr void sub(
        const Int& other,
        bool& overflow,
        Int& out
    ) const noexcept {
        bool this_neg = bits.msb_is_set();
        bool other_neg = other.bits.msb_is_set();
        bits.sub(other.bits, overflow, out.bits);
        overflow = this_neg != other_neg && out.bits.msb_is_set() != this_neg;
    }

    /* Multiply two integers of equal size.  If the result overflows, then the
    `overflow` flag will be set to true, and the value will wrap around to the other
    end of the number line (modulo `N`).

    Note that this uses simple schoolbook multiplication under the hood, which is
    generally optimized for small bit counts (up to a few thousand bits).  For larger
    bit counts, it may be more efficient to use a specialized library such as GMP or
    `boost::multiprecision`. */
    [[nodiscard]] constexpr Int mul(
        const Int& other,
        bool& overflow
    ) const noexcept {
        Int result;
        mul(other, overflow, result);
        return result;
    }

    /* Multiply two integers of equal size and store the product as a mutable out
    parameter.  If the result overflows, then the `overflow` flag will be set to true,
    and the value will wrap around to the other end of the number line (modulo `N`).
    The out parameter may be a reference to either operand, without affecting the
    overall calculation.

    Note that this uses simple schoolbook multiplication under the hood, which is
    generally optimized for small bit counts (up to a few thousand bits).  For larger
    bit counts, it may be more efficient to use a specialized library such as GMP or
    `boost::multiprecision`. */
    constexpr void mul(
        const Int& other,
        bool& overflow,
        Int& out
    ) const noexcept {
        // get full 2N-bit product
        overflow = false;
        auto result = bits._mul(other.bits);

        // low N bits become the final result, while upper N bits indicate overflow via
        // a sign extension test
        if constexpr (Bits::array_size == 1 && !Bits::big_word::composite) {
            using big_word = Bits::big_word;
            using big = big_word::type;

            // if the sign bit is set, then all upper bits must also be set to avoid
            // overflow.  Otherwise, all upper bits must be zero
            if constexpr (Bits::end_mask) {
                constexpr big sign_mask = big(big(1) << ((N % Bits::word_size) - 1));
                constexpr big mask = big(Bits::end_mask);
                constexpr big inv_mask = ~mask;
                out.bits.data()[0] = word(result & mask);
                bool sign = result & sign_mask;
                overflow = (result & inv_mask) != (inv_mask * sign);

            } else {
                constexpr big sign_mask = big(big(1) << (Bits::word_size - 1));
                constexpr big mask = big(std::numeric_limits<word>::max());
                constexpr big inv_mask = ~mask;
                out.bits.data()[0] = word(result);
                bool sign = result & sign_mask;
                overflow = (result & inv_mask) != (inv_mask * sign);
            }

        } else {
            using size_type = Bits::size_type;
            std::copy_n(
                result.begin(),
                Bits::array_size,
                out.bits.data().begin()
            );

            // if the sign bit is set, then all upper bits must also be set to avoid
            // overflow.  Otherwise, all upper bits must be zero
            if constexpr (Bits::end_mask) {
                constexpr word sign_mask = word(word(1) << ((N % Bits::word_size) - 1));
                constexpr word mask = word(Bits::end_mask);
                constexpr word inv_mask = ~mask;
                out.bits.data()[Bits::array_size - 1] &= Bits::end_mask;
                bool sign = result[Bits::array_size - 1] & sign_mask;
                overflow = (result[Bits::array_size - 1] & inv_mask) != (inv_mask * sign);
                if (!overflow) {
                    word expected = std::numeric_limits<word>::max() * sign;
                    for (size_type i = Bits::array_size; i < Bits::array_size * 2; ++i) {
                        if (result[i] != expected) {
                            overflow = true;
                            return;
                        }
                    }
                }
            } else {
                constexpr word sign_mask = word(word(1) << (Bits::word_size - 1));
                bool sign = result[Bits::array_size - 1] & sign_mask;
                word expected = std::numeric_limits<word>::max() * sign;
                for (size_type i = Bits::array_size; i < Bits::array_size * 2; ++i) {
                    if (result[i] != expected) {
                        overflow = true;
                        return;
                    }
                }
            }
        }
    }

    /* Divide two integers of equal size, returning both the quotient and remainder.
    If the divisor is zero and the program is compiled in debug mode, then a
    `ZeroDivisionError` will be thrown.  Otherwise, there is precisely one possible
    overflow case at `Int::min() / -1`, which results in an `OverflowError` when the
    program is compiled in debug mode.  Overflow cannot occur in any other case.

    Note that this uses Knuth's Algorithm D under the hood, which is generally
    optimized for small bit counts (up to a few thousand bits).  For larger bit counts,
    it may be more efficient to use a specialized library such as GMP or
    `boost::multiprecision`. */
    [[nodiscard]] constexpr std::pair<Int, Int> divmod(
        const Int& other
    ) const noexcept(!DEBUG) {
        Int quotient;
        Int remainder;
        divmod(other, quotient, remainder);
        return {quotient, remainder};
    }

    /* Divide two integers of equal size, returning the quotient and storing the
    remainder as a mutable out parameter.  If the divisor is zero and the program is
    compiled in debug mode, then a `ZeroDivisionError` will be thrown.  Otherwise,
    there is precisely one possible overflow case at `Int::min() / -1`, which results
    in an `OverflowError` when the program is compiled in debug mode.  Overflow cannot
    occur in any other case.

    Listing either the dividend or divisor as the out parameter for the remainder is
    allowed (but not required), and will update the referenced integer in-place, without
    affecting the calculation.

    Note that this uses Knuth's Algorithm D under the hood, which is generally
    optimized for small bit counts (up to a few thousand bits).  For larger bit counts,
    it may be more efficient to use a specialized library such as GMP or
    `boost::multiprecision`. */
    [[nodiscard]] constexpr Int divmod(
        const Int& other,
        Int& remainder
    ) const noexcept(!DEBUG) {
        Int quotient;
        divmod(other, quotient, remainder);
        return quotient;
    }

    /* Divide two integers of equal size, storing both the quotient and remainder as
    mutable out parameters.  If the divisor is zero and the program is compiled in
    debug mode, then a `ZeroDivisionError` will be thrown.  Otherwise, there is
    precisely one possible overflow case at `Int::min() / -1`, which results in an
    `OverflowError` when the program is compiled in debug mode.  Overflow cannot occur
    in any other case.

    Listing either the dividend or divisor as the out parameter for the quotient or
    remainder is allowed (but not required), and will update the referenced integer
    in-place, without affecting the calculation.

    Note that this uses Knuth's Algorithm D under the hood, which is generally
    optimized for small bit counts (up to a few thousand bits).  For larger bit counts,
    it may be more efficient to use a specialized library such as GMP or
    `boost::multiprecision`. */
    constexpr void divmod(
        const Int& other,
        Int& quotient,
        Int& remainder
    ) const noexcept(!DEBUG) {
        bool this_neg = bits.msb_is_set();
        bool other_neg = other.bits.msb_is_set();
        quotient = *this;
        if (this_neg) {
            if constexpr (DEBUG) {
                if (*this == min() && other == -1) {
                    throw OverflowError(
                        "Integer overflow: cannot divide min() by -1"
                    );
                }
            }
            quotient.bits.negate();
        }
        if (other_neg) {
            quotient.bits.divmod(-other.bits, quotient.bits, remainder.bits);
        } else {
            quotient.bits.divmod(other.bits, quotient.bits, remainder.bits);
        }
        if (this_neg != other_neg) {
            quotient.bits.negate();
        }
        if (this_neg) {
            remainder.bits.negate();
        }
    }

    /* Compare two integers of equal size. */
    [[nodiscard]] friend constexpr auto operator<=>(
        const Int& lhs,
        const Int& rhs
    ) noexcept requires(Bits::array_size > 1) {
        int a = -lhs.bits.msb_is_set();
        int b = -rhs.bits.msb_is_set();
        return a != b ? a <=> b : lhs.bits <=> rhs.bits;
    }

    /* Compare two integers of equal size. */
    [[nodiscard]] friend constexpr bool operator==(
        const Int& lhs,
        const Int& rhs
    ) noexcept requires(Bits::array_size > 1) {
        return lhs.bits == rhs.bits;
    }

    /* Apply a bitwise NOT to the integer. */
    [[nodiscard]] constexpr Int operator~() const noexcept {
        return ~bits;
    }

    /* Apply a bitwise AND between the contents of two integers of equal size. */
    [[nodiscard]] friend constexpr Int operator&(
        const Int& lhs,
        const Int& rhs
    ) noexcept requires(Bits::array_size > 1) {
        return lhs.bits & rhs.bits;
    }

    /* Apply a bitwise AND between the contents of this integer and another of equal
    length, updating the former in-place. */
    constexpr Int& operator&=(
        const Int& other
    ) noexcept {
        bits &= other.bits;
        return *this;
    }

    /* Apply a bitwise OR between the contents of two integers of equal size. */
    [[nodiscard]] friend constexpr Int operator|(
        const Int& lhs,
        const Int& rhs
    ) noexcept requires(Bits::array_size > 1) {
        return lhs.bits | rhs.bits;
    }

    /* Apply a bitwise OR between the contents of this integer and another of equal
    length, updating the former in-place  */
    constexpr Int& operator|=(
        const Int& other
    ) noexcept {
        bits |= other.bits;
        return *this;
    }

    /* Apply a bitwise XOR between the contents of two integers of equal size. */
    [[nodiscard]] friend constexpr Int operator^(
        const Int& lhs,
        const Int& rhs
    ) noexcept requires(Bits::array_size > 1) {
        return lhs.bits ^ rhs.bits;
    }

    /* Apply a bitwise XOR between the contents of this integer and another of equal
    length, updating the former in-place. */
    constexpr Int& operator^=(
        const Int& other
    ) noexcept {
        bits ^= other.bits;
        return *this;
    }

    /* Apply a bitwise left shift to the contents of the integer. */
    [[nodiscard]] constexpr Int operator<<(size_t rhs) const noexcept
        requires(Bits::array_size > 1)
    {
        if (bits.msb_is_set()) {
            Bits result = -bits << rhs;
            result.negate();
            return result;
        } else {
            return bits << rhs;
        }
    }

    /* Apply a bitwise left shift to the contents of this integer, updating it
    in-place. */
    constexpr Int& operator<<=(size_t rhs) noexcept {
        if (bits.msb_is_set()) {
            bits.negate();
            bits <<= rhs;
            bits.negate();
        } else {
            bits <<= rhs;
        }
        return *this;
    }

    /* Apply a bitwise right shift to the contents of the integer. */
    [[nodiscard]] constexpr Int operator>>(size_t rhs) const noexcept
        requires(Bits::array_size > 1)
    {
        if (bits.msb_is_set()) {
            Bits result = -bits >> rhs;
            result.negate();
            return result;
        } else {
            return bits >> rhs;
        }
    }

    /* Apply a bitwise right shift to the contents of this integer, updating it
    in-place. */
    constexpr Int& operator>>=(size_t rhs) noexcept {
        if (bits.msb_is_set()) {
            bits.negate();
            bits >>= rhs;
            bits.negate();
        } else {
            bits >>= rhs;
        }
        return *this;
    }

    /* Return a copy of the integer. */
    [[nodiscard]] constexpr Int operator+() const noexcept {
        return bits;
    }

    /* Increment the integer by one and return a reference to the new value. */
    constexpr Int& operator++() noexcept {
        ++bits;
        return *this;
    }

    /* Increment the integer by one and return a copy of the old value. */
    [[nodiscard]] constexpr Int operator++(int) noexcept {
        Int copy = *this;
        ++bits;
        return copy;
    }

    /* Operator version of `Int.add()` that discards the overflow flag. */
    [[nodiscard]] friend constexpr Int operator+(
        const Int& lhs,
        const Int& rhs
    ) noexcept requires(Bits::array_size > 1) {
        return lhs.bits + rhs.bits;
    }

    /* Operator version of `Int.add()` that discards the overflow flag and
    writes the sum back to this integer. */
    constexpr Int& operator+=(const Int& other) noexcept {
        bits += other.bits;
        return *this;
    }

    /* Return a negative copy of the integer.  This equates to a copy followed by a
    `negate()` modifier. */
    [[nodiscard]] constexpr Int operator-() const noexcept {
        return -bits;
    }

    /* Decrement the integer by one and return a reference to the new value. */
    constexpr Int& operator--() noexcept {
        --bits;
        return *this;
    }

    /* Decrement the integer by one and return a copy of the old value. */
    [[nodiscard]] constexpr Int operator--(int) noexcept {
        Int copy = *this;
        --bits;
        return copy;
    }

    /* Operator version of `Int.sub()` that discards the overflow flag. */
    [[nodiscard]] friend constexpr Int operator-(
        const Int& lhs,
        const Int& rhs
    ) noexcept requires(Bits::array_size > 1) {
        return lhs.bits - rhs.bits;
    }

    /* Operator version of `Int.sub()` that discards the overflow flag and
    writes the difference back to this integer. */
    constexpr Int& operator-=(const Int& other) noexcept {
        bits -= other.bits;
        return *this;
    }

    /* Operator version of `Int.mul()` that discards the overflow flag. */
    [[nodiscard]] friend constexpr Int operator*(
        const Int& lhs,
        const Int& rhs
    ) noexcept requires(Bits::array_size > 1) {
        bool overflow;
        return lhs.mul(rhs, overflow);
    }

    /* Operator version of `Int.mul()` that discards the overflow flag and writes
    the product back to this integer. */
    constexpr Int& operator*=(const Int& other) noexcept {
        bool overflow;
        mul(other, overflow, *this);
        return *this;
    }

    /* Operator version of `Int.divmod()` that discards the remainder. */
    [[nodiscard]] friend constexpr Int operator/(
        const Int& lhs,
        const Int& rhs
    ) noexcept(!DEBUG) requires(Bits::array_size > 1) {
        auto [quotient, remainder] = lhs.divmod(rhs);
        return quotient;
    }

    /* Operator version of `Int.divmod()` that discards the remainder and
    writes the quotient back to this integer. */
    constexpr Int& operator/=(const Int& other) noexcept(!DEBUG) {
        Int remainder;
        divmod(other, *this, remainder);
        return *this;
    }

    /* Operator version of `Int.divmod()` that discards the quotient. */
    [[nodiscard]] friend constexpr Int operator%(
        const Int& lhs,
        const Int& rhs
    ) noexcept(!DEBUG) requires(Bits::array_size > 1) {
        auto [quotient, remainder] = lhs.divmod(rhs);
        return remainder;
    }

    /* Operator version of `Int.divmod()` that discards the quotient and
    writes the remainder back to this integer. */
    constexpr Int& operator%=(const Int& other) noexcept(!DEBUG) {
        Int quotient;
        divmod(other, quotient, *this);
        return *this;
    }

    /* Print the integer to an output stream. */
    constexpr friend std::ostream& operator<<(
        std::ostream& os,
        const Int& self
    ) {
        std::ostream::sentry s(os);
        if (!s) {
            return os;  // stream is not ready for output
        }

        // 1) extract configuration from stream
        impl::format_bits ctx(
            os,
            self.bits.msb_is_set(),
            std::use_facet<std::numpunct<char>>(os.getloc())
        );

        // 2) convert to string in proper base with locale-based grouping (if any)
        std::string str = ctx.template digits_with_grouping<char>(self);

        // 3) adjust for sign + base prefix, and fill to width
        ctx.template pad_and_align<char>(str);

        // 4) write to output stream
        os.write(str.data(), str.size());
        os.width(0);  // reset width for next output (mandatory)
        return os;
    }

    /* Read the integer from an input stream. */
    constexpr friend std::istream& operator>>(
        std::istream& is,
        Int& self
    ) {
        std::istream::sentry s(is);  // automatically skips leading whitespace
        if (!s) {
            return is;  // stream is not ready for input
        }
        impl::scan_bits ctx(
            is,
            std::use_facet<std::numpunct<char>>(is.getloc())
        );
        ctx.scan(is, self);
        return is;
    }
};


/* ADL `swap()` method for `bertrand::Int<N>` instances. */
template <size_t N>
constexpr void swap(Int<N>& lhs, Int<N>& rhs) noexcept { lhs.swap(rhs); }


// template struct Bits<8>;
// template struct Bits<16>;
// template struct Bits<32>;
// template struct Bits<64>;
// template struct Bits<128>;


// template struct Int<8>;
// template struct Int<16>;
// template struct Int<32>;
// template struct Int<64>;
// template struct Int<128>;
using int8 = Int<8>;
using int16 = Int<16>;
using int32 = Int<32>;
using int64 = Int<64>;
using int128 = Int<128>;


// template struct UInt<8>;
// template struct UInt<16>;
// template struct UInt<32>;
// template struct UInt<64>;
// template struct UInt<128>;
using uint8 = UInt<8>;
using uint16 = UInt<16>;
using uint32 = UInt<32>;
using uint64 = UInt<64>;
using uint128 = UInt<128>;


/// TODO: endianness presents a pretty massive problem for floating point numbers, and
/// for arithmetic types in general.  It might interfere with the rather delicate
/// masking logic that I need to provide the correct results, and I'll need to keep it
/// in mind.


/// Reference: https://github.com/oprecomp/FloatX
/// https://en.wikipedia.org/wiki/Minifloat


namespace impl {

    template <size_t E, size_t M>
    struct float_word {  // fall back to softfloat emulation with E + M bits
        using type = void;
        static constexpr size_t size = E + M;
    };

    template <typename T, size_t E, size_t M>
    concept fits_within_float =
        E <= meta::float_exponent_size<T> &&
        M <= meta::float_mantissa_size<T>;

    template <size_t E, size_t M>
        requires (fits_within_float<float, E, M>)
    struct float_word<E, M> {
        using type = float;
        static constexpr size_t size = meta::float_size<type>;
    };

    template <size_t E, size_t M>
        requires (
            !fits_within_float<float, E, M> &&
            fits_within_float<double, E, M>
        )
    struct float_word<E, M> {
        using type = double;
        static constexpr size_t size = meta::float_size<type>;
    };

    #ifdef __STDCPP_FLOAT16_T__
        inline constexpr bool HAS_FLOAT16 = true;
        template <size_t E, size_t M>
            requires (
                (HAS_FLOAT16 && fits_within_float<std::float16_t, E, M>)
            )
        struct float_word<E, M> {
            using type = std::float16_t;
            static constexpr size_t size = meta::float_size<type>;
        };
    #else
        inline constexpr bool HAS_FLOAT16 = false;
    #endif

    #ifdef __STDCPP_BFLOAT16_T__
        inline constexpr bool HAS_BFLOAT16 = true;
        template <size_t E, size_t M>
            requires (
                !(HAS_FLOAT16 && fits_within_float<std::float16_t, E, M>) &&
                (HAS_BFLOAT16 && fits_within_float<std::bfloat16_t, E, M>)
            )
        struct float_word<E, M> {
            using type = std::bfloat16_t;
            static constexpr size_t size = meta::float_size<type>;
        };
    #else
        inline constexpr bool HAS_BFLOAT16 = false;
    #endif

    #ifdef __STDCPP_FLOAT32_T__
        inline constexpr bool HAS_FLOAT32 = true;
        template <size_t E, size_t M>
            requires (
                !(HAS_FLOAT16 && fits_within_float<std::float16_t, E, M>) &&
                !(HAS_BFLOAT16 && fits_within_float<std::bfloat16_t, E, M>) &&
                (HAS_FLOAT32 && fits_within_float<std::float32_t, E, M>)
            )
        struct float_word<E, M> {
            using type = std::float32_t;
            static constexpr size_t size = meta::float_size<type>;
        };
    #else
        inline constexpr bool HAS_FLOAT32 = false;
    #endif

    #ifdef __STDCPP_FLOAT64_T__
        inline constexpr bool HAS_FLOAT64 = true;
        template <size_t E, size_t M>
            requires (
                !(HAS_FLOAT16 && fits_within_float<std::float16_t, E, M>) &&
                !(HAS_BFLOAT16 && fits_within_float<std::bfloat16_t, E, M>) &&
                !(HAS_FLOAT32 && fits_within_float<std::float32_t, E, M>) &&
                (HAS_FLOAT64 && fits_within_float<std::float64_t, E, M>)
            )
        struct float_word<E, M> {
            using type = std::float64_t;
            static constexpr size_t size = meta::float_size<type>;
        };
    #else
        inline constexpr bool HAS_FLOAT64 = false;
    #endif

    #ifdef __STDCPP_FLOAT128_T__
        inline constexpr bool HAS_FLOAT128 = true;
        template <size_t E, size_t M>
            requires (
                !(HAS_FLOAT16 && fits_within_float<std::float16_t, E, M>) &&
                !(HAS_BFLOAT16 && fits_within_float<std::bfloat16_t, E, M>) &&
                !(HAS_FLOAT32 && fits_within_float<std::float32_t, E, M>) &&
                !(HAS_FLOAT64 && fits_within_float<std::float64_t, E, M>) &&
                (HAS_FLOAT128 && fits_within_float<std::float128_t, E, M>)
            )
        struct float_word<E, M> {
            using type = std::float128_t;
            static constexpr size_t size = meta::float_size<type>;
        };
    #else
        inline constexpr bool HAS_FLOAT128 = false;
    #endif

}


template <meta::floating T>
    requires (meta::float_exponent_size<T> > 1 && meta::float_mantissa_size<T> > 1)
Float(T) -> Float<meta::float_exponent_size<T>, meta::float_mantissa_size<T>>;


template <size_t E, size_t M> requires (E > 1 && M > 1)
struct Float : impl::Float_tag {
private:
    static constexpr size_t N = impl::float_word<E, M>::size;

public:
    /* An equivalent (or slightly larger) hardware-supported floating-point type, which
    can represent all possible values of this type, or void if no such type exists.  If
    the type is non-void, then the float will use the same FPU hardware as the
    indicated type and mask out any unused bits, retaining as much performance as
    possible.  Otherwise, all operations will be emulated in software using only
    integer arithmetic.  Such "soft" floats can represent arbitrary exponent and
    mantissa sizes, and may be useful when deploying to platforms that lack a proper
    FPU, but will always be slower than their hardware equivalents.  Emulation as a
    fallback ensures that the same logic can be used on all platforms regardless of
    configuration, while still prioritizing hardware acceleration if available. */
    using hard_type = impl::float_word<E, M>::type;

    /* The underlying bitset representation for the contents of this float. */
    using Bits = bertrand::Bits<N>;

private:

    template <typename T>
    struct _traits {
        static constexpr bool hard = false;
        static constexpr size_t man_size = M;
        static constexpr size_t exp_size = E;
        static constexpr Bits exp_bias =
            Bits::mask(0, E - 1);

        static constexpr Bits sign_mask = Bits{1} << (N - 1);
        static constexpr Bits payload_mask = ~sign_mask;
        static constexpr Bits exp_mask =
            Bits::mask(man_size - 1, man_size - 1 + E);
        static constexpr Bits man_mask =
            Bits::mask(man_size - M, man_size - 1);

        static constexpr Bits man_to_even = 1;

        static constexpr void narrow(Bits& bits) noexcept {}
        static constexpr const Float& widen(const Float& self) noexcept { return self; }

        template <meta::floating U>
        static constexpr Bits from_float(const U& value) noexcept {
            /// TODO: this is where I would need to convert from a hardware float to a
            /// software float, or from a softfloat of a different size to this one.
            return {};
        }

        template <meta::integer U>
        static constexpr Bits from_int(const U& value) noexcept {
            /// TODO: convert an integer into a float with the correct exponent and
            /// mantissa.
            return {};
        }
    };
    template <meta::not_void T>
    struct _traits<T> {
        static constexpr bool hard = true;
        static constexpr size_t man_size = meta::float_mantissa_size<T>;
        static constexpr size_t exp_size = meta::float_exponent_size<T>;
        static constexpr Bits exp_bias =
            Bits::mask(0, E - 1);

        static constexpr Bits sign_mask = Bits{1} << (N - 1);
        static constexpr Bits payload_mask = ~sign_mask;
        static constexpr Bits exp_mask =
            Bits::mask(man_size - 1, man_size - 1 + E);
        static constexpr Bits man_mask =
            Bits::mask(man_size - M, man_size - 1);

        static constexpr Bits man_half =
            M == man_size ? Bits{} : Bits{Bits{1} << (man_size - M - 1)};
        static constexpr Bits man_to_even = man_half << 1;
        static constexpr Bits man_discard = man_to_even ? Bits{man_to_even - 1} : Bits{};

        static constexpr Bits norm_bias =
            (Bits{meta::float_exponent_bias<T>} - exp_bias) << (man_size - 1);
        static constexpr Bits input_exp_mask =
            Bits::mask(man_size - 1, man_size - 1 + exp_size);
        static constexpr Bits input_man_mask =
            Bits::mask(0, man_size - 1);

        static constexpr void narrow(Bits& bits) noexcept {
            /// NOTE: we keep the high M bits of the mantissa, low E bits of the
            /// exponent, and the sign bit.  This preserves +/- 0, inf, and NaN
            /// special cases, while truncating to the true templated sizes.
            if constexpr (M < man_size && E < exp_size) {
                // check for +/- inf input
                if ((bits & input_exp_mask) == input_exp_mask) {
                    bits &= sign_mask | exp_mask | man_mask;
                    return;
                }

                // extract exponent and adjust bias
                Bits exp = bits & input_exp_mask;
                exp -= norm_bias;

                // extract mantissa and round to nearest even value
                Bits man = bits & input_man_mask;
                if ((bits & man_discard) == man_half) {
                    man += man & man_to_even;
                } else {
                    man += man_half;
                }
                exp += man & input_exp_mask;  // add carry from mantissa
                man &= man_mask;

                // check for overflow/underflow and commit changes
                bits &= sign_mask;
                if (exp & ~exp_mask) {
                    if (exp > exp_mask) {
                        bits |= exp_mask;  // +/- inf
                    } else {
                        bits |= man;  // subnormal zero (mantissa unchanged)
                    }
                } else {
                    bits |= exp;
                    bits |= man;
                }

            } else if constexpr (M < man_size) {
                static constexpr Bits keep = input_exp_mask | sign_mask;

                // record state of sign bit to detect exponent overflow
                Bits sign = bits & sign_mask;

                // extract mantissa and round to nearest even value
                Bits man = bits & input_man_mask;
                if ((bits & man_discard) == man_half) {
                    man += man & man_to_even;
                } else {
                    man += man_half;
                }
                bits += man & input_exp_mask;  // add carry from mantissa

                // check for overflow
                if ((bits & sign_mask) != sign) {
                    bits = sign | input_exp_mask;
                } else {
                    bits &= keep;
                    bits |= man;
                }

            } else if constexpr (E < exp_size) {
                static constexpr Bits keep = input_man_mask | sign_mask;

                // check for +/- inf input
                if ((bits & input_exp_mask) == input_exp_mask) {
                    bits &= keep | exp_mask;
                    return;
                }

                // extract exponent and adjust bias
                Bits exp = bits & input_exp_mask;
                exp -= norm_bias;

                // check for overflow/underflow
                if (exp & ~exp_mask) {
                    if (exp > exp_mask) {
                        bits &= sign_mask;
                        bits |= exp_mask;  // +/- inf
                    } else {
                        bits &= keep;  // subnormal zero (mantissa unchanged)
                    }
                } else {
                    bits &= keep;
                    bits |= exp;
                }
            }
        }

        static constexpr T widen(const Bits& bits) noexcept {
            if constexpr (E < exp_size) {
                return static_cast<T>(bits + norm_bias);
            } else {
                return static_cast<T>(bits);
            }
        }

        template <meta::floating U>
        static constexpr Bits from_float(const U& value) noexcept {
            Bits result = Bits{static_cast<T>(value)};
            narrow(result);
            return result;
        }

        template <meta::integer U>
        static constexpr Bits from_int(const U& value) noexcept {
            Bits result = Bits{static_cast<T>(value)};
            narrow(result);
            return result;
        }
    };
    using traits = _traits<hard_type>;

public:

    /* The number of bits devoted to the exponent.  Equivalent to `E`. */
    static constexpr size_t exponent_size = E;

    /* The number of bits devoted to the mantissa, including the implicit ones bit.
    Equivalent to `M`. */
    static constexpr size_t mantissa_size = M;

    /* The minimum exponent that the base can be raised to before producing a subnormal
    value. */
    static constexpr Int<32> min_exponent = Int<32>{1} - traits::exp_bias;

    /* The maximum exponent that the base can be raised to before producing an
    infinity. */
    static constexpr Int<32> max_exponent = Int<32>{1} + traits::exp_bias;
    
    /* The minimum (most negative) finite value that the float can store. */
    [[nodiscard]] static constexpr const Float& min() noexcept {
        static constexpr Float result = [] -> Float {
            // min occurs at exponent == max - 1 and mantissa == all ones
            Float result;
            result.bits |= traits::sign_mask;
            result.bits |= traits::exp_bias << traits::man_size;
            result.bits |= traits::man_mask;
            return result;
        }();
        return result;
    }

    /* The maximum (most positive) finite value that the float can store. */
    [[nodiscard]] static constexpr const Float& max() noexcept {
        static constexpr Float result = [] -> Float {
            // max occurs at exponent == max - 1 and mantissa == all ones
            Float result;
            result.bits |= traits::exp_bias << traits::man_size;
            result.bits |= traits::man_mask;
            return result;
        }();
        return result;
    }

    /* The smallest positive, normalized value that the float can store. */
    [[nodiscard]] static constexpr const Float& smallest() noexcept {
        static constexpr Float result = [] -> Float {
            // smallest occurs at exponent == 1 and mantissa == 0
            Float result;
            result.bits |= 1;
            result.bits <<= traits::man_size - 1;
            return result;
        }();
        return result;
    }

    /* The smallest positive, subnormal value that the float can store. */
    [[nodiscard]] static constexpr const Float& denorm_min() noexcept {
        static constexpr Float result = [] -> Float {
            // denorm min occurs at exponent == 0 and mantissa == 1
            Float result;
            result.bits |= 1;
            return result;
        }();
        return result;
    }

    /* The difference in magnitude between 1.0 and the next representable value for
    this floating point type.  Smaller results indicate a higher floating point
    resolution, which is correlated with `M` (the number of bits devoted to the
    mantissa).  Epsilon is used as the default floating point tolerance for approximate
    comparisons. */
    [[nodiscard]] static constexpr const Float& epsilon() noexcept {
        static constexpr Float result = [] -> Float {
            // epsilon occurs at exponent == bias and mantissa == 1, then subtract 1
            Float result;
            result.bits |= 1;
            result.bits <<= traits::man_size - M;
            result.bits |= traits::exp_bias << (traits::man_size - 1);
            return result - 1;
        }();
        return result;
    }

    /* Negative infinity sentinel. */
    [[nodiscard]] static constexpr const Float& neg_inf() noexcept {
        static constexpr Float result = [] -> Float {
            // infinity occurs at max exponent and zero mantissa
            Float result;
            result.bits |= traits::sign_mask;
            result.bits |= traits::exp_mask;
            return result;
        }();
        return result;
    }

    /* Positive infinity sentinel. */
    [[nodiscard]] static constexpr const Float& inf() noexcept {
        static constexpr Float result = [] -> Float {
            // infinity occurs at max exponent and zero mantissa
            Float result;
            result.bits |= traits::exp_mask;
            return result;
        }();
        return result;
    }

    /* NaN sentinel. */
    [[nodiscard]] static constexpr const Float& nan() noexcept {
        static constexpr Float result = [] -> Float {
            // NaN layout may be implementation-defined
            if constexpr (traits::hard) {
                return {std::numeric_limits<hard_type>::quiet_NaN()};

            // in softfloat mode, quiet NaN corresponds to positive sign, maximum
            // exponent, and non-zero mantissa MSB
            } else {
                Float result;
                result.bits |= traits::exp_mask;
                result.bits |= Bits{1} << (traits::man_size - 2);
                return result;
            }
        }();
        return result;
    }

    /* A bitset holding the bitwise representation of the float. */
    Bits bits;

    /* Default constructor.  Initializes to positive zero. */
    [[nodiscard]] constexpr Float() noexcept = default;

    /* Implicitly convert from a float of any width, rounding the result to the nearest
    representable value. */
    template <meta::floating T>
    [[nodiscard]] constexpr Float(const T& value) noexcept :
        bits(traits::from_float(value))
    {}

    /* Explicitly convert an integer into a float, rounding the result to the nearest
    representable value. */
    template <meta::integer T> requires (!meta::Bits<T>)
    [[nodiscard]] explicit constexpr Float(const T& value) noexcept :
        bits(traits::from_int(value))
    {}

    /* Trivially swap the values of two floats. */
    constexpr void swap(Float& other) noexcept {
        bits.swap(other.bits);
    }

    /// TODO: string conversions can possibly reuse the continuation constructors from
    /// Bits, so that I don't need to do the actual parsing here.

    /// TODO: possibly use the same base conversion logic as the integer types, so
    /// that you can produce a true binary representation of the float with correct
    /// bit representation, etc.

    template <
        static_str negative = "-",
        static_str decimal = ".",
        static_str exponent = "e",
        static_str inf = "inf",
        static_str nan = "nan",
        static_str zero = "0",
        static_str one = "1",
        static_str... rest
    >
        requires (
            !negative.empty() && !decimal.empty() && !exponent.empty() &&
            !inf.empty() && !nan.empty() && !zero.empty() && !one.empty() &&
            (!rest.empty() && ...) && meta::perfectly_hashable<
                negative, decimal, exponent, inf, nan, zero, one, rest...
            >
        )
    [[nodiscard]] static constexpr auto from_string(std::string_view str) noexcept
        -> Expected<Float, ValueError>  // TODO: no overflow error possible?
    {
        /// TODO: quite a bit more complicated than the integer version, but still
        /// doable.
        return {};
    }

    template <
        static_str negative = "-",
        static_str decimal = ".",
        static_str exponent = "e",
        static_str inf = "inf",
        static_str nan = "nan",
        static_str zero = "0",
        static_str one = "1",
        static_str... rest
    >
        requires (
            !negative.empty() && !decimal.empty() && !exponent.empty() &&
            !inf.empty() && !nan.empty() && !zero.empty() && !one.empty() &&
            (!rest.empty() && ...) && meta::perfectly_hashable<
                negative, decimal, exponent, inf, nan, zero, one, rest...
            >
        )
    [[nodiscard]] static constexpr Float from_string(
        std::string_view str,
        std::string_view& continuation
    ) noexcept {
        /// TODO: quite a bit more complicated than the integer version, but still
        /// doable.
        return {};
    }

    /* A shorthand for `from_string<"-", ".", "e", "inf", "nan", "0", "1">(str)`, which
    decodes a string in the canonical binary representation. */
    [[nodiscard]] static constexpr auto from_binary(std::string_view str) noexcept
        -> Expected<Float, ValueError>
    {
        return from_string<"-", ".", "e", "inf", "nan", "0", "1">(str);
    }

    /* A shorthand for
    `from_string<"-", ".", "e", "inf", "nan", "0", "1">(str, continuation)`, which
    decodes a string in the canonical binary representation. */
    [[nodiscard]] static constexpr Float from_binary(
        std::string_view str,
        std::string_view& continuation
    ) noexcept {
        return from_string<"-", ".", "e", "inf", "nan", "0", "1">(str, continuation);
    }

    /* A shorthand for
    `from_string<"-", ".", "e", "inf", "nan", "0", "1", "2", "3", "4", "5", "6", "7">(str)`,
    which decodes a string in the canonical octal representation. */
    [[nodiscard]] static constexpr auto from_octal(std::string_view str) noexcept
        -> Expected<Float, ValueError>
    {
        return from_string<
            "-", ".", "e", "inf", "nan", "0", "1", "2", "3", "4", "5", "6", "7"
        >(str);
    }

    /* A shorthand for
    `from_string<"-", ".", "e", "inf", "nan", "0", "1", "2", "3", "4", "5", "6", "7">(str, continuation)`,
    which decodes a string in the canonical octal representation. */
    [[nodiscard]] static constexpr Float from_octal(
        std::string_view str,
        std::string_view& continuation
    ) noexcept {
        return from_string<
            "-", ".", "e", "inf", "nan", "0", "1", "2", "3", "4", "5", "6", "7"
        >(str, continuation);
    }

    /* A shorthand for
    `from_string<"-", ".", "e", "inf", "nan", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9">(str)`, which
    decodes a string in the canonical decimal representation. */
    [[nodiscard]] static constexpr auto from_decimal(std::string_view str) noexcept
        -> Expected<Float, ValueError>
    {
        return from_string<
            "-", ".", "e", "inf", "nan", "0", "1", "2", "3", "4", "5", "6", "7",
            "8", "9"
        >(str);
    }

    /* A shorthand for
    `from_string<"-", ".", "e", "inf", "nan", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9">(str, continuation)`,
    which decodes a string in the canonical decimal representation. */
    [[nodiscard]] static constexpr Float from_decimal(
        std::string_view str,
        std::string_view& continuation
    ) noexcept {
        return from_string<
            "-", ".", "e", "inf", "nan", "0", "1", "2", "3", "4", "5", "6", "7",
            "8", "9"
        >(str, continuation);
    }

    /* A shorthand for
    `from_string<"-", ".", "e", "inf", "nan", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "A", "B", "C", "D", "E", "F">(str)`,
    which decodes a string in the canonical hexadecimal representation. */
    [[nodiscard]] static constexpr auto from_hex(std::string_view str) noexcept
        -> Expected<Float, ValueError>
    {
        return from_string<
            "-", ".", "e", "inf", "nan", "0", "1", "2", "3", "4", "5", "6", "7",
            "8", "9", "A", "B", "C", "D", "E", "F"
        >(str);
    }

    /* A shorthand for
    `from_string<"-", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "A", "B", "C", "D", "E", "F">(str, continuation)`,
    which decodes a string in the canonical hexadecimal representation. */
    [[nodiscard]] static constexpr Float from_hex(
        std::string_view str,
        std::string_view& continuation
    ) noexcept {
        return from_string<
            "-", ".", "e", "inf", "nan", "0", "1", "2", "3", "4", "5", "6", "7",
            "8", "9", "A", "B", "C", "D", "E", "F"
        >(str, continuation);
    }

    template <
        static_str negative = "-",
        static_str decimal = ".",
        static_str exponent = "e",
        static_str inf = "inf",
        static_str nan = "nan",
        static_str zero = "0",
        static_str one = "1",
        static_str... rest
    >
        requires (
            !negative.empty() && !decimal.empty() && !exponent.empty() &&
            !inf.empty() && !nan.empty() && !zero.empty() && !one.empty() &&
            (!rest.empty() && ...) && meta::strings_are_unique<
                negative, decimal, exponent, inf, nan, zero, one, rest...
            >
        )
    [[nodiscard]] constexpr std::string to_string() const noexcept {
        /// TODO: also some complications here, but not impossible.
        return {};
    }

    /* A shorthand for `to_string<"-", ".", "e", "inf", "nan", "0", "1">()`, which
    yields a string in the canonical binary representation. */
    [[nodiscard]] constexpr std::string to_binary() const noexcept {
        return to_string<"-", ".", "e", "inf", "nan", "0", "1">();
    }

    /* A shorthand for
    `to_string<"-", ".", "e", "inf", "nan", "0", "1", "2", "3", "4", "5", "6", "7">()`,
    which yields a string in the canonical octal representation. */
    [[nodiscard]] constexpr std::string to_octal() const noexcept {
        return to_string<
            "-", ".", "e", "inf", "nan", "0", "1", "2", "3", "4", "5", "6", "7"
        >();
    }

    /* A shorthand for
    `to_string<"-", ".", "e", "inf", "nan", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9">()`,
    which yields a string in the canonical decimal representation. */
    [[nodiscard]] constexpr std::string to_decimal() const noexcept {
        return to_string<
            "-", ".", "e", "inf", "nan", "0", "1", "2", "3", "4", "5", "6", "7",
            "8", "9"
        >();
    }

    /* A shorthand for
    `to_string<"-", ".", "e", "inf", "nan", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "A", "B", "C", "D", "E", "F">()`,
    which yields a string in the canonical hexadecimal representation. */
    [[nodiscard]] constexpr std::string to_hex() const noexcept {
        return to_string<
            "-", ".", "e", "inf", "nan", "0", "1", "2", "3", "4", "5", "6", "7",
            "8", "9", "A", "B", "C", "D", "E", "F"
        >();
    }

    /* Explicitly Convert the float to a decimal string representation. */
    [[nodiscard]] explicit constexpr operator std::string() const noexcept {
        return to_decimal();
    }

    /* Non-zero floats (including subnormals) evaluate to true under boolean logic. */
    [[nodiscard]] explicit constexpr operator bool() const noexcept {
        return bool(bits & traits::payload_mask);
    }

    /* Implicitly convert the float to an equivalent hardware type if one is
    available. */
    [[nodiscard]] constexpr operator hard_type() const noexcept requires(traits::hard) {
        return traits::widen(bits);
    }

    /* Explicitly convert the float to an integer type, truncating towards zero. */
    template <meta::integer T> requires (!meta::prefer_constructor<T>)
    [[nodiscard]] explicit constexpr operator T() const noexcept {
        if constexpr (traits::hard) {
            return static_cast<T>(static_cast<hard_type>(*this));
        } else {
            /// TODO: softfloat emulation
        }
    }

    /* Explicitly convert the float to another floating point type, rounding to the
    nearest representable value. */
    template <meta::floating T> requires (!meta::prefer_constructor<T>)
    [[nodiscard]] explicit constexpr operator T() const noexcept {
        if constexpr (traits::hard) {
            return static_cast<T>(static_cast<hard_type>(*this));
        } else {
            /// TODO: softfloat emulation
        }
    }

    /* Returns true if the float represents zero, regardless of sign. */
    [[nodiscard]] constexpr bool is_zero() const noexcept {
        return !*this;
    }

    /* Returns true if the float represents a subnormal number, regardless of sign. */
    [[nodiscard]] constexpr bool is_subnormal() const noexcept {
        Bits payload = bits & traits::payload_mask;
        return (payload <= traits::man_mask) && payload;  // exp == 0, man != 0
    }

    /* Returns true if the float represents positive or negative infinity. */
    [[nodiscard]] constexpr bool is_inf() const noexcept {
        return (bits & traits::payload_mask) == traits::exp_mask;  // exp == max, man == 0
    }

    /* Returns true if the float represents NaN. */
    [[nodiscard]] constexpr bool is_nan() const noexcept {
        return (bits & traits::payload_mask) > traits::exp_mask;  // exp == max, man != 0
    }

    /* Return +1 if the float is positive or -1 if it is negative. */
    [[nodiscard]] constexpr int sign() const noexcept {
        return 1 - (2 * bits.msb_is_set());
    }

    /* Return the radix base being used by the float.  The result is given as an
    unsigned integer greater than or equal to 2, and always obeys the identity:

        ```
        assert(f == f.sign() * pow(f.base(), f.exponent()) * f.mantissa());
        ```

    ... Except in the case of subnormal numbers, which get rounded down to zero.  For
    each of the IEEE 754 special cases:

        1.  +/- zero or subnormal -> -inf, which causes `pow(...)` to return 0.0
        2.  +/- inf -> +inf, which causes `f.sign() * pow(...)` to obey the identity
        3.  NaN -> returns NaN, which causes `pow(...)` to also return NaN
    */
    [[nodiscard]] static constexpr UInt<32> base() noexcept {
        return 2;
    }

    /* Return the normalized exponent being used by the float.  The result is given as
    another float with a signed integral value centered on zero.  It always obeys the
    identity:

        ```
        assert(f == f.sign() * pow(f.base(), f.exponent()) * f.mantissa());
        ```

    ... Except in the case of subnormal numbers, which get rounded down to zero.  For
    each of the IEEE 754 special cases:

        1.  +/- zero or subnormal -> -inf, which causes `pow(...)` to return 0.0
        2.  +/- inf -> +inf, which causes `f.sign() * pow(...)` to obey the identity
        3.  NaN -> returns NaN, which causes `pow(...)` to also return NaN
    */
    [[nodiscard]] constexpr Float exponent() const noexcept {
        Bits payload = bits & traits::payload_mask;

        // inf/NaN get passed through with positive sign
        if (payload >= traits::exp_mask) {
            Float result;
            result.bits |= payload;
            return result;
        }

        // zero and subnormals get converted to negative infinity
        if (payload <= traits::man_mask) {
            return neg_inf();
        }

        // all other values get converted to a signed integer and subtracted by the
        // bias before being converted back to a float.
        Int<E> result = {
            Int<E>{(bits & traits::exp_mask) >> (traits::man_size - 1)} -
            Int<E>(traits::exp_bias)
        };
        return Float{result};
    }

    /* Return the fractional mantissa being used by the float.  This is always another
    float with the exact same mantissa bits, but with the exponent set to 1.  The
    result is never negative, and always obeys the identity:

        ```
        assert(f == f.sign() * pow(f.base(), f.exponent()) * f.mantissa());
        ```

    ... Except in the case of subnormal numbers, which get rounded down to zero.  For
    each of the IEEE 754 special cases:

        1.  +/- zero or subnormal -> -inf, which causes `pow(...)` to return 0.0
        2.  +/- inf -> +inf, which causes `f.sign() * pow(...)` to obey the identity
        3.  NaN -> returns NaN, which causes `pow(...)` to also return NaN
    */
    [[nodiscard]] constexpr Float mantissa() const noexcept {
        return Float(
            (bits & traits::man_mask) | (traits::exp_bias << (traits::man_size - 1))
        );
    }

    /* Get the absolute value of this float.  This equates to clearing the sign bit. */
    [[nodiscard]] constexpr Float abs() const noexcept {
        Float result = *this;
        result.bits &= traits::payload_mask;
        return result;
    }

    /// TODO: nextafter, nexttoward, copysign, etc.

    /// TODO: on ULPs: https://docs.oracle.com/cd/E19957-01/806-3568/ncg_goldberg.html



    /// TODO: documentation for arithmetic methods, and possible softfloat emulation

    [[nodiscard]] constexpr Float add(const Float& other) const noexcept {
        if constexpr (traits::hard) {
            return hard_type(*this) + hard_type(other);
        } else {
            Float result;
            add(other, result);
            return result;
        }
    }

    constexpr void add(const Float& other, Float& out) const noexcept {
        if constexpr (traits::hard) {
            out = hard_type(*this) + hard_type(other);
        } else {
            /// TODO: softfloat emulation (NYI)
        }
    }

    [[nodiscard]] constexpr Float sub(const Float& other) const noexcept {
        if constexpr (traits::hard) {
            return hard_type(*this) - hard_type(other);
        } else {
            Float result;
            sub(other, result);
            return result;
        }
    }

    constexpr void sub(const Float& other, Float& out) const noexcept {
        if constexpr (traits::hard) {
            out = hard_type(*this) - hard_type(other);
        } else {
            /// TODO: softfloat emulation (NYI)
        }
    }

    [[nodiscard]] constexpr Float mul(const Float& other) const noexcept {
        if constexpr (traits::hard) {
            return hard_type(*this) * hard_type(other);
        } else {
            Float result;
            mul(other, result);
            return result;
        }
    }

    constexpr void mul(const Float& other, Float& out) const noexcept {
        if constexpr (traits::hard) {
            out = hard_type(*this) * hard_type(other);
        } else {
            /// TODO: softfloat emulation (NYI)
        }
    }

    [[nodiscard]] constexpr Float div(const Float& other) const noexcept {
        if constexpr (traits::hard) {
            return hard_type(*this) / hard_type(other);
        } else {
            Float result;
            div(other, result);
            return result;
        }
    }

    constexpr void div(const Float& other, Float& out) const noexcept {
        if constexpr (traits::hard) {
            out = hard_type(*this) / hard_type(other);
        } else {
            /// TODO: softfloat emulation (NYI)
        }
    }

    /// TODO: perhaps if the tolerance is given as an integer, it signifies a ULP
    /// tolerance, which is the number of bits in the mantissa that are allowed to
    /// differ before two floats are considered unequal.  If given as a float, it is an
    /// absolute tolerance, applied as below.  Alternatively, and perhaps more safely,
    /// a ULP-based approximate comparison could be exposed as a .bit_compare(val, 5)
    /// method, which would be more explicit.

    /* Compare two floats of equal size.  The operands will be considered equal if they
    differ by no more than `tolerance.abs()`, which defaults to `epsilon()`.  The
    result is a `std::partial_ordering` value that indicates the relationship between
    the operands, the value of which can be obtained by comparing it with the literal
    0, like so:

        ```
        auto result = lhs.compare(rhs, tolerance);
        if (result < 0) {
            // lhs < rhs
        } else if (result > 0) {
            // lhs > rhs
        } else {
            // lhs == rhs
        }
        ```
    */
    [[nodiscard]] constexpr std::partial_ordering compare(
        const Float& other,
        const Float& tolerance = epsilon()
    ) const noexcept {
        std::partial_ordering result = *this <=> other;
        if (result < 0) {
            Float diff = other - *this;
            diff.bits &= traits::payload_mask;
            if (diff <= tolerance.abs()) {
                return std::partial_ordering::equivalent;
            }
        } else if (result > 0) {
            Float diff = *this - other;
            diff.bits &= traits::payload_mask;
            if (diff <= tolerance.abs()) {
                return std::partial_ordering::equivalent;
            }
        }
        return result;
    }

    /* Compare two floats of equal size.  Note that this only compares exact
    (bit-level) equality.  To check for approximate equality, use `Float::compare()`
    instead. */
    [[nodiscard]] friend constexpr std::partial_ordering operator<=>(
        const Float& lhs,
        const Float& rhs
    ) noexcept requires(!traits::hard) {
        Bits lhs_payload = lhs.bits & traits::payload_mask;
        Bits rhs_payload = rhs.bits & traits::payload_mask;

        // NaNs compare unordered to everything, including themselves
        if (lhs_payload > traits::exp_mask || rhs_payload > traits::exp_mask) {
            return std::partial_ordering::unordered;
        }

        // +/- zero compare equal regardless of sign
        if (lhs_payload == 0 && rhs_payload == 0) {
            return std::partial_ordering::equivalent;
        }

        // all other values compare normally, taking sign into account
        if (lhs.bits.msb_is_set()) {
            if (rhs.bits.msb_is_set()) {
                return rhs_payload <=> lhs_payload;
            } else {
                return std::partial_ordering::less;
            }
        } else {
            if (rhs.bits.msb_is_set()) {
                return std::partial_ordering::greater;
            } else {
                return lhs_payload <=> rhs_payload;
            }
        }
    }

    /* Compare two floats of equal size for equality.  Note that this only compares
    exact (bit-level) equality.  To check for approximate equality, use
    `Float::compare()` instead. */
    [[nodiscard]] friend constexpr bool operator==(
        const Float& lhs,
        const Float& rhs
    ) noexcept requires(!traits::hard) {
        return (lhs <=> rhs) == 0;
    }

    /* Return a copy of the float. */
    [[nodiscard]] constexpr Float operator+() const noexcept {
        return *this;
    }

    /* Increment the float by one and return a reference to the new value. */
    constexpr Float& operator++() noexcept {
        static constexpr Float one = 1.0;
        add(one, *this);
        return *this;
    }

    /* Increment the float by one and return a copy of the old value. */
    [[nodiscard]] constexpr Float operator++(int) noexcept {
        Float copy = *this;
        ++*this;
        return copy;
    }

    /* Operator version of `Float.add()`. */
    [[nodiscard]] friend constexpr Float operator+(
        const Float& lhs,
        const Float& rhs
    ) noexcept requires(!traits::hard) {
        return lhs.add(rhs);
    }

    /* Operator version of `Float.add()` that writes the sum back to this float. */
    constexpr Float& operator+=(const Float& other) noexcept {
        add(other, *this);
        return *this;
    }

    /* Return a negative copy of the float.  This equates to flipping the sign bit. */
    [[nodiscard]] constexpr Float operator-() const noexcept {
        return Float{bits ^ traits::sign_mask};
    }

    /* Decrement the float by one and return a reference to the new value. */
    constexpr Float& operator--() noexcept {
        static constexpr Float one = 1.0;
        sub(one, *this);
        return *this;
    }

    /* Decrement the float by one and return a copy of the old value. */
    [[nodiscard]] constexpr Float operator--(int) noexcept {
        Float copy = *this;
        --*this;
        return copy;
    }

    /* Operator version of `Float.sub()`. */
    [[nodiscard]] friend constexpr Float operator-(
        const Float& lhs,
        const Float& rhs
    ) noexcept requires(!traits::hard) {
        return lhs.sub(rhs);
    }

    /* Operator version of `Float.sub()` that writes the difference back to this
    float. */
    constexpr Float& operator-=(const Float& other) noexcept {
        sub(other, *this);
        return *this;
    }

    /* Operator version of `Float.mul()`. */
    [[nodiscard]] friend constexpr Float operator*(
        const Float& lhs,
        const Float& rhs
    ) noexcept requires(!traits::hard) {
        return lhs.mul(rhs);
    }

    /* Operator version of `Float.mul()` that writes the product back to this
    float. */
    constexpr Float& operator*=(const Float& other) noexcept {
        mul(other, *this);
        return *this;
    }

    /* Operator version of `Float.div()`. */
    [[nodiscard]] friend constexpr Float operator/(
        const Float& lhs,
        const Float& rhs
    ) noexcept requires(!traits::hard) {
        return lhs.div(rhs);
    }

    /* Operator version of `Float.div()` that writes the quotient back to this
    float. */
    constexpr Float& operator/=(const Float& other) noexcept {
        div(other, *this);
        return *this;
    }

    /// TODO: stream operators

};


/* ADL `swap()` method for `bertrand::Float<E, M>` instances. */
template <size_t E, size_t M>
constexpr void swap(Float<E, M>& lhs, Float<E, M>& rhs) noexcept { lhs.swap(rhs); }


/// TODO: eventually, use the exponent and mantissa sizes from the extended floating
/// point types, which are not yet available in libc++.


// template struct Float<5, 11>;
// template struct Float<8, 8>;
// template struct Float<8, 24>;
// template struct Float<11, 53>;
// template struct Float<15, 113>;
using float16 = Float<5, 11>;
using bfloat16 = Float<8, 8>;
using float32 = Float<8, 24>;
using float64 = Float<11, 53>;
// using float128 = Float<15, 113>;


// static_assert(int16(float32(10)) == 10);


static_assert(Bits{std::numeric_limits<float>::denorm_min()}.data()[0] == 0b0'00000000'00000000'00000000'0000001);



inline constexpr float16 test_float = 1.0;
inline constexpr float16 test_float2 = test_float;
static_assert(float(test_float) == 1.0);
static_assert(std::bit_cast<uint32_t>(float(test_float)) == 0b0'01111111'00000000'00000000'0000000);
static_assert(test_float.bits == 0b0'00001111'00000000'00000000'0000000);

static_assert(std::bit_cast<uint32_t>(float(1.0)) == 0b0'01111111'00000000'00000000'0000000);
static_assert(
    // float16(1.0).bits == 0b0'01111111'00000000'00000000'0000000
    float16(1.0).bits == 0b0'00001111'00000000'00000000'0000000
);

static_assert(
    std::bit_cast<uint16_t>(_Float16(1.0)) == 0b0'01111'0000000000
);


inline constexpr auto bitwise = Bits{float(1.0)};
static_assert(float32(1.0).bits == 0b0'01111111'00000000'00000000'0000000);
static_assert(bitwise == 0b0'01111111'00000000'00000000'0000000);
static_assert(float32(bitwise) == 1.0);
static_assert(float32(1.0) + 0.5 == 1.5);


inline constexpr Int<8> x = 1;
static_assert(float32(x) == 1.0);


inline constexpr float32 xyz = 3.5;
// inline constexpr Int<32> xyz2 {xyz};
static_assert(float32(Int<32>(0)) == 0.0);
static_assert(xyz.mantissa() == 1.75);
static_assert(xyz.exponent() == 1);
static_assert(xyz.base() == 2);
// static_assert(xyz == xyz.sign() * pow(xyz.base(), xyz.exponent()) * xyz.mantissa());

// log<2>(x)
// root<2>(x)


inline constexpr auto bitwise_nan = Bits{std::numeric_limits<float>::signaling_NaN()};
static_assert(bitwise_nan.data()[0] == 0b0'11111111'01000000'00000000'0000000);

static_assert(float32::max() == std::numeric_limits<float>::max());
static_assert(float32::min() == std::numeric_limits<float>::lowest());
static_assert(Float{1.0}.bits.size() == 64);
static_assert(float32::nan().is_nan());
static_assert(float32(1.5) <= float32(1.5));


static_assert(float32::epsilon() == std::numeric_limits<float>::epsilon());
// static_assert(
//     Bits{Bits{std::numeric_limits<float>::epsilon()}[slice{0, 24}]}.data()[0] ==
//     0b0'01111111'00000000'00000000'0000000
// );
static_assert(
    Bits{std::numeric_limits<float>::epsilon()}.data()[0] ==
    0b0'01101000'00000000'00000000'0000000
);

static_assert(float32::nan().compare(float32::nan(), 0.5) != 0);

static_assert(float32::smallest() == std::numeric_limits<float>::min());


}  // namespace bertrand


namespace std {

    /* Specializing `std::formatter` allows bitsets to be formatted using
    `std::format()`, `repr()`, `print()`, and other formatting functions. */
    template <bertrand::meta::Bits T>
    struct formatter<T> {
        using const_reference = bertrand::meta::as_const<bertrand::meta::as_lvalue<T>>;
        mutable bertrand::impl::format_bits config;

        /* Parse the format specification mini-language as if the bitset were an
        ordinary integral type. */
        constexpr auto parse(format_parse_context& ctx) {
            auto it = ctx.begin();
            config = bertrand::impl::format_bits(ctx, it);
            return it;
        }

        /* Encode the bitset according to the parsed flags, reusing logic from the
        iostream operators. */
        template <typename out, typename char_type>
        constexpr auto format(
            const_reference v,
            basic_format_context<out, char_type>& ctx
        ) const {
            // 1) get the correct locale from context and resolve nested width specifier
            config.complete(ctx, false);

            // 2) convert to string in proper base with locale-based grouping (if any)
            std::basic_string<char_type> str =
                config.template digits_with_grouping<char_type>(v);

            // 3) adjust for sign + base prefix, and fill to width
            config.template pad_and_align<char_type>(str);

            // 4) write to format context
            out it = ctx.out();
            it = std::copy(str.begin(), str.end(), it);
            return it;
        }
    };

    /* Specializing `std::formatter` allows bitset slices to be formatted using
    `std::format()`, `repr()`, `print()`, and other formatting functions. */
    template <bertrand::meta::inherits<bertrand::impl::Bits_slice_tag> T>
    struct formatter<T> {
        using const_reference = bertrand::meta::as_const<bertrand::meta::as_lvalue<T>>;
        formatter<bertrand::Bits<bertrand::meta::unqualify<T>::self::size()>> config;
        constexpr auto parse(format_parse_context& ctx) {
            return config.parse(ctx);
        }
        template <typename out, typename char_type>
        constexpr auto format(
            const_reference v,
            basic_format_context<out, char_type>& ctx
        ) const {
            return config.format(bertrand::Bits{v}, ctx);
        }
    };

    /* Specializing `std::formatter` allows unsigned integers to be formatted using
    `std::format()`, `repr()`, `print()`, and other formatting functions.  This reuses
    the same logic as `Bits<N>`. */
    template <bertrand::meta::UInt T>
    struct formatter<T> {
        using const_reference = bertrand::meta::as_const<bertrand::meta::as_lvalue<T>>;
        formatter<bertrand::Bits<bertrand::meta::integer_size<T>>> config;
        constexpr auto parse(format_parse_context& ctx) {
            return config.parse(ctx);
        }
        template <typename out, typename char_type>
        constexpr auto format(
            const_reference v,
            basic_format_context<out, char_type>& ctx
        ) const {
            return config.format(v.bits, ctx);
        }
    };

    /* Specializing `std::formatter` allows signed integers to be formatted using
    `std::format()`, `repr()`, `print()`, and other formatting functions. */
    template <bertrand::meta::Int T>
    struct formatter<T> {
        using const_reference = bertrand::meta::as_const<bertrand::meta::as_lvalue<T>>;
        formatter<bertrand::Bits<bertrand::meta::integer_size<T>>> config;
        constexpr auto parse(format_parse_context& ctx) {
            return config.parse(ctx);
        }
        template <typename out, typename char_type>
        constexpr auto format(
            const_reference v,
            basic_format_context<out, char_type>& ctx
        ) const {
            // 1) get the correct locale from context and resolve nested width specifier
            config.complete(ctx, v.sign() < 0);

            // 2) convert to string in proper base with locale-based grouping (if any)
            std::basic_string<char_type> str =
                config.template digits_with_grouping<char_type>(v);

            // 3) adjust for sign + base prefix, and fill to width
            config.template pad_and_align<char_type>(str);

            // 4) write to format context
            out it = ctx.out();
            it = std::copy(str.begin(), str.end(), it);
            return it;
        }
    };

    /* Specializing `std::formatter` allows floats to be formatted using
    `std::format()`, `repr()`, `print()`, and other formatting functions. */
    template <bertrand::meta::Float T>
    struct formatter<T> {
        /// TODO: this will get really complicated really fast.  All format specifiers
        /// that apply to built-in floating point types must also apply to generalized
        /// floats.
    };

    /* Specializing `std::numeric_limits` allows bitsets to be introspected just like
    other integer types. */
    template <bertrand::meta::Bits T>
    struct numeric_limits<T> {
    private:
        using type = std::remove_cvref_t<T>;
        using word = type::word;

    public:
        static constexpr bool is_specialized                = true;
        static constexpr bool is_signed                     = false;
        static constexpr bool is_integer                    = true;
        static constexpr bool is_exact                      = true;
        static constexpr bool has_infinity                  = false;
        static constexpr bool has_quiet_NaN                 = false;
        static constexpr bool has_signaling_NaN             = false;
        static constexpr std::float_round_style round_style =
            std::float_round_style::round_toward_zero;
        static constexpr bool is_iec559                     = false;
        static constexpr bool is_bounded                    = true;
        static constexpr bool is_modulo                     = true;
        static constexpr int digits                         = bertrand::meta::integer_size<T>;
        static constexpr int digits10                       = digits * 0.3010299956639812;  // log10(2)
        static constexpr int max_digits10                   = 0;
        static constexpr int radix                          = 2;
        static constexpr int min_exponent                   = 0;
        static constexpr int min_exponent10                 = 0;
        static constexpr int max_exponent                   = 0;
        static constexpr int max_exponent10                 = 0;
        static constexpr bool traps                         = true;  // division by zero should trap
        static constexpr bool tinyness_before               = false;
        static constexpr type min() noexcept { return type::min(); }
        static constexpr type lowest() noexcept { return min(); }
        static constexpr type max() noexcept { return type::max(); }
        static constexpr type epsilon() noexcept { return {}; }
        static constexpr type round_error() noexcept { return {}; }
        static constexpr type infinity() noexcept { return {}; }
        static constexpr type quiet_NaN() noexcept { return {}; }
        static constexpr type signaling_NaN() noexcept { return {}; }
        static constexpr type denorm_min() noexcept { return {}; }
    };

    /* Specializing `std::numeric_limits` allows unsigned integers to be introspected
    just like other integer types. */
    template <bertrand::meta::UInt T>
    struct numeric_limits<T> {
    private:
        using type = std::remove_cvref_t<T>;
        using bits = bertrand::Bits<bertrand::meta::integer_size<T>>;
        using traits = std::numeric_limits<bits>;

    public:
        static constexpr bool is_specialized                = traits::is_specialized;
        static constexpr bool is_signed                     = traits::is_signed;
        static constexpr bool is_integer                    = traits::is_integer;
        static constexpr bool is_exact                      = traits::is_exact;
        static constexpr bool has_infinity                  = traits::has_infinity;
        static constexpr bool has_quiet_NaN                 = traits::has_quiet_NaN;
        static constexpr bool has_signaling_NaN             = traits::has_signaling_NaN;
        static constexpr std::float_round_style round_style = traits::round_style;
        static constexpr bool is_iec559                     = traits::is_iec559;
        static constexpr bool is_bounded                    = traits::is_bounded;
        static constexpr bool is_modulo                     = traits::is_modulo;
        static constexpr int digits                         = traits::digits;
        static constexpr int digits10                       = traits::digits10;
        static constexpr int max_digits10                   = traits::max_digits10;
        static constexpr int radix                          = traits::radix;
        static constexpr int min_exponent                   = traits::min_exponent;
        static constexpr int min_exponent10                 = traits::min_exponent10;
        static constexpr int max_exponent                   = traits::max_exponent;
        static constexpr int max_exponent10                 = traits::max_exponent10;
        static constexpr bool traps                         = traits::traps;  // division by zero should trap
        static constexpr bool tinyness_before               = traits::tinyness_before;
        static constexpr type min() noexcept { return type::min(); }
        static constexpr type lowest() noexcept { return min(); }
        static constexpr type max() noexcept { return type::max(); }
        static constexpr type epsilon() noexcept { return {}; }
        static constexpr type round_error() noexcept { return {}; }
        static constexpr type infinity() noexcept { return {}; }
        static constexpr type quiet_NaN() noexcept { return {}; }
        static constexpr type signaling_NaN() noexcept { return {}; }
        static constexpr type denorm_min() noexcept { return {}; }
    };

    /* Specializing `std::numeric_limits` allows signed integers to be introspected
    just like other integer types. */
    template <bertrand::meta::Int T>
    struct numeric_limits<T> {
    private:
        using type = std::remove_cvref_t<T>;
        using bits = bertrand::Bits<bertrand::meta::integer_size<T>>;
        using word = type::word;
        using traits = std::numeric_limits<bits>;

    public:
        static constexpr bool is_specialized                = traits::is_specialized;
        static constexpr bool is_signed                     = true;
        static constexpr bool is_integer                    = traits::is_integer;
        static constexpr bool is_exact                      = traits::is_exact;
        static constexpr bool has_infinity                  = traits::has_infinity;
        static constexpr bool has_quiet_NaN                 = traits::has_quiet_NaN;
        static constexpr bool has_signaling_NaN             = traits::has_signaling_NaN;
        static constexpr std::float_round_style round_style = traits::round_style;
        static constexpr bool is_iec559                     = traits::is_iec559;
        static constexpr bool is_bounded                    = traits::is_bounded;
        static constexpr bool is_modulo                     = traits::is_modulo;
        static constexpr int digits                         = traits::digits;
        static constexpr int digits10                       = traits::digits;  // log10(2)
        static constexpr int max_digits10                   = traits::max_digits10;
        static constexpr int radix                          = traits::radix;
        static constexpr int min_exponent                   = traits::min_exponent;
        static constexpr int min_exponent10                 = traits::min_exponent10;
        static constexpr int max_exponent                   = traits::max_exponent;
        static constexpr int max_exponent10                 = traits::max_exponent10;
        static constexpr bool traps                         = traits::traps;  // division by zero should trap
        static constexpr bool tinyness_before               = traits::tinyness_before;
        static constexpr type min() noexcept { return type::min(); }
        static constexpr type lowest() noexcept { return min(); }
        static constexpr type max() noexcept { return type::max(); }
        static constexpr type epsilon() noexcept { return {}; }
        static constexpr type round_error() noexcept { return {}; }
        static constexpr type infinity() noexcept { return {}; }
        static constexpr type quiet_NaN() noexcept { return {}; }
        static constexpr type signaling_NaN() noexcept { return {}; }
        static constexpr type denorm_min() noexcept { return {}; }
    };

    /* Specializing `std::numeric_limits` allows floats to be introspected just like
    other float types. */
    template <bertrand::meta::Float T>
    struct numeric_limits<T> {
    private:
        using type = std::remove_cvref_t<T>;
        static constexpr double log10_2 = 0.3010299956639812;  // log2(10)

    public:
        static constexpr bool is_specialized                = true;
        static constexpr bool is_signed                     = true;
        static constexpr bool is_integer                    = false;
        static constexpr bool is_exact                      = false;
        static constexpr bool has_infinity                  = true;
        static constexpr bool has_quiet_NaN                 = true;
        static constexpr bool has_signaling_NaN             = false;
        static constexpr std::float_round_style round_style = std::float_round_style::round_to_nearest;
        static constexpr bool is_iec559                     = false;  // no signaling NaN
        static constexpr bool is_bounded                    = true;
        static constexpr bool is_modulo                     = false;
        static constexpr int digits                         = type::mantissa_size;
        static constexpr int digits10                       = (digits - 1) * log10_2;  // log10(2)
        static constexpr int max_digits10                   = [] {
            double temp = digits * log10_2 + 1;
            int result = static_cast<int>(temp);
            if (result < temp) {
                ++result;
            }
            return result;
        }();
        static constexpr int radix                          = 2;
        static constexpr int min_exponent                   = type::min_exponent + 1;
        static constexpr int min_exponent10                 = type::min_exponent * log10_2;
        static constexpr int max_exponent                   = type::max_exponent + 1;
        static constexpr int max_exponent10                 = type::max_exponent * log10_2;
        static constexpr bool traps                         = false;
        static constexpr bool tinyness_before               = false;
        static constexpr type min() noexcept { return type::smallest(); }
        static constexpr type lowest() noexcept { return type::min(); }
        static constexpr type max() noexcept { return type::max(); }
        static constexpr type epsilon() noexcept { return type::epsilon(); }
        static constexpr type round_error() noexcept { return 0.5; }
        static constexpr type infinity() noexcept { return type::inf(); }
        static constexpr type quiet_NaN() noexcept { return type::nan(); }
        static constexpr type signaling_NaN() noexcept { return type::nan(); }
        static constexpr type denorm_min() noexcept { return type::denorm_min(); }
    };

    /* Specializing `std::hash` allows bitsets to be used as keys in hash tables. */
    template <bertrand::meta::Bits T>
    struct hash<T> {
        using const_reference = bertrand::meta::as_const<bertrand::meta::as_lvalue<T>>;
        [[nodiscard]] static constexpr size_t operator()(const_reference x) noexcept {
            size_t result = 0;
            for (size_t i = 0; i < x.data().size(); ++i) {
                result = bertrand::impl::hash_combine(result, x.data()[i]);
            }
            return result;
        }
    };

    /* Specializing `std::hash` allows bitset slices to be used as keys to hash
    tables. */
    template <bertrand::meta::inherits<bertrand::impl::Bits_slice_tag> T>
    struct hash<T> {
        using const_reference = bertrand::meta::as_const<bertrand::meta::as_lvalue<T>>;
        [[nodiscard]] static constexpr size_t operator()(const_reference x) noexcept {
            return hash<
                bertrand::Bits<bertrand::meta::unqualify<T>::self::size()>
            >{}(bertrand::Bits{x});
        }
    };

    /* Specializing `std::hash` allows integers to be used as keys in hash tables. */
    template <bertrand::meta::UInt T>
    struct hash<T> {
        using const_reference = bertrand::meta::as_const<bertrand::meta::as_lvalue<T>>;
        [[nodiscard]] static constexpr size_t operator()(const_reference x) noexcept {
            return hash<bertrand::Bits<bertrand::meta::integer_size<T>>>{}(x.bits);
        }
    };

    /* Specializing `std::hash` allows integers to be used as keys in hash tables. */
    template <bertrand::meta::Int T>
    struct hash<T> {
        using const_reference = bertrand::meta::as_const<bertrand::meta::as_lvalue<T>>;
        [[nodiscard]] static constexpr size_t operator()(const_reference x) noexcept {
            return hash<bertrand::Bits<bertrand::meta::integer_size<T>>>{}(x.bits);
        }
    };

    /* Specializing `std::hash` allows floats to be used as keys in hash tables. */
    template <bertrand::meta::Float T>
    struct hash<T> {
        using const_reference = bertrand::meta::as_const<bertrand::meta::as_lvalue<T>>;
        [[nodiscard]] static constexpr size_t operator()(const_reference x) noexcept {
            /// TODO: any special treatment for +/- zero, NaN, infinity, or subnormals?
            return hash<bertrand::Bits<bertrand::meta::integer_size<T>>>{}(x.bits);
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

}


// namespace bertrand {

//     template <Bits b>
//     struct Foo {
//         static constexpr const auto& value = b;
//     };

//     inline void test() {
//         {
//             static constexpr Bits<2> a{0b1010};
//             static constexpr Bits a2{false, true};
//             static constexpr Bits a3{1, 2};
//             static constexpr Bits a4{0, true};
//             static_assert(a4.count() == 1);
//             static_assert(a3.size() == 64);
//             static_assert(a == a2);
//             auto [f1, f2] = a;
//             static constexpr Bits b {"abab", 'b', 'a'};
//             static constexpr std::string c = b.to_binary();
//             static constexpr std::string d = b.to_decimal();
//             static constexpr std::string d2 = b.to_string();
//             static constexpr std::string d3 = b.to_hex();
//             static_assert(any(b.components()));
//             static_assert(b[slice{2}].first_one().value() == 3);
//             static_assert(a == 0b10);
//             static_assert(b == 0b1010);
//             static_assert(b[1] == true);
//             static_assert(c == "1010");
//             static_assert(d == "10");
//             static_assert(d2 == "1010");
//             static_assert(d3 == "A");

//             static_assert(std::same_as<typename Bits<2>::word, uint8_t>);
//             static_assert(sizeof(Bits<2>) == 1);

//             constexpr auto x = []() {
//                 Bits<4> out;
//                 out[-1] = true;
//                 return out;
//             }();
//             static_assert(x == uint8_t(0b1000));

//             for (auto&& x : a) {

//             }
//             for (auto&& x : a.components()) {

//             }
//             for (auto&& x : a[slice{1, -1}]) {

//             }
//         }

//         {
//             static constexpr Bits b {"100"};
//             static constexpr auto b2 = Bits<3>::from_string(
//                 std::string_view("100")
//             );
//             static_assert(b.data()[0] == 4);
//             static_assert(b2.result().data()[0] == 4);

//             static constexpr auto b3 = Bits{"0100"}.reverse();
//             static_assert(b3.data()[0] == 2);

//             static constexpr auto b4 = Bits<3>::from_string<"ab", "c">("cabab");
//             static_assert(b4.result().data()[0] == 4);

//             static constexpr auto b5 = Bits<3>::from_decimal("5");
//             static_assert(b5.result().data()[0] == 5);

//             static constexpr auto b6 = Bits<8>::from_hex("FF");
//             static_assert(b6.result().data()[0] == 255);

//             static constexpr auto b7 = Bits<8>::from_hex("ZZ");
//             static_assert(b7.has_error());

//             static constexpr auto b8 = Bits<8>::from_hex("FFC");
//             static_assert(b8.has_error());
//         }

//         {
//             static constexpr Foo<{true, false, true}> foo;
//             static constexpr Foo<Bits{"bab", 'a', 'b'}> bar;
//             static_assert(foo.value == Bits{"101"});
//             static_assert(bar.value == 5);
//             static_assert(bar.value == 5);
//         }

//         {
//             static_assert(Bits<5>::from_binary("10101").result().to_hex() == "15");
//             static_assert(Bits<72>::from_hex("FFFFFFFFFFFFFFFFFF").result().count() == 72);
//             static_assert(Bits<4>::from_octal("20").has_error());
//             static_assert(Bits<4>::from_decimal("16").has_error<OverflowError>());
//             static_assert(Bits<4>::from_decimal("15").result().data()[0] == 15);

//             // static_assert((Bits<4>{uint8_t(1)} * uint8_t(10) + uint8_t(2)).data()[0] == 10);
//         }

//         {
//             static constexpr Bits<5> a = -1;
//             static constexpr Bits<5> b = a + 2;
//             static_assert(a == 31);
//             static_assert(b.data()[0] == 1);

//             static constexpr Bits<5> c;
//             static constexpr Bits<5> d = c - 1;
//             static_assert(c == 0);
//             static_assert(d.data()[0] == 31);

//             static constexpr Bits<5> e = 12;
//             static constexpr Bits<5> f = e * 3;
//             static_assert(f.data()[0] == 4);
//         }

//         {
//             static constexpr Bits<4> b = {Bits{"110"}, true};
//             static_assert(b.last_zero().value() == 0);
//             static_assert(b == Bits{"1110"});
//             static_assert(-b == Bits{"0010"});
//             static_assert(Bits<5>::from_string("10100").result() == 20);

//             static constexpr Bits<72> b2;
//             static_assert(size_t(b2) == 0);
//             // int x = b2;

//             auto y = b + Bits{3};
//             if (b) {

//             }
//             std::cout << b;

//             static constexpr static_str s = "1";
//             static constexpr static_str s2 {s[0]};
//             static_assert(s2 == "1");
//         }

//         {
//             static constexpr UInt x = 42;
//             static_assert(x == 42);
//             static constexpr auto x2 = UInt<32>::from_decimal("42");
//             static_assert(x2.result() == 42);
//             static_assert(divide::ceil(x, 4) == 11);

//             static constexpr UInt y = -1;
//             static_assert(y.bits.count() == 32);

//             static_assert(UInt<8>::max() == 255);
//             static_assert(Int<8>::max() == 127);
//             static_assert(Int<8>(Int<8>::max() + 1) == -128);
//             static_assert(Int<8>(Int<8>::min() - 1) == 127);
//             static_assert(Int<8>::min() < Int<8>::max());

//             static_assert(Bits<8>{5}.divmod(2).first == (5 / 2));
//             static_assert(Bits<8>{5}.divmod(2).second == (5 % 2));
//             static_assert(UInt<8>{5}.divmod(2).second == (5 % 2));
//             static_assert(Int<8>{5}.divmod(2).second == (5 % 2));


//             static_assert(Bits<72>::max().divmod(2).first == Bits<71>::max());
//             static_assert(Bits<72>::max().divmod(2).first.data()[0] == std::numeric_limits<uint64_t>::max());
//             static_assert(Bits<72>::max().divmod(2).second == 1);
//             static_assert(Bits<72>::max().divmod(2).second.data()[0] == 1);

//             static_assert(Bits<72>::max().divmod(Bits<71>::max()).first.data()[0] == 2);

//             static constexpr Bits extracted = Bits{"0101"}[slice{0, 2}];
//             static constexpr UInt extracted2 = Bits{"0101"}[slice{0, 2}];
//             static constexpr Int extracted3 = Bits{"0101"}[slice{0, 2}];
//             static_assert(Bits{"0101"}[slice{-2, std::nullopt, -2}].any());
//             static_assert(extracted == Bits{"01"});

//             // for (auto x : Bits{"0101"}[slice{0, 2}]) {

//             // }
//             // static constexpr Bits temp = Bits{"0101"};
//             // static constexpr auto it = std::ranges::end(temp[slice{0, 2}]);
//             // static constexpr auto temp2 = temp[slice{0, 2}];
//             // static constexpr auto it2 = std::ranges::end(temp2);
//             // static_assert(meta::iterable<decltype(temp2)>);
//         }

//         {
//             static constexpr Bits alt = Bits<8>::mask(
//                 std::nullopt,
//                 std::nullopt,
//                 2
//             );
//             static_assert(alt == Bits{"01010101"});
//             static constexpr Bits alt2 = Bits<8>::mask(1, -1, 2);
//             static_assert(alt2 == Bits{"00101010"});
//             static constexpr Bits alt3 = Bits<8>::mask(
//                 std::nullopt,
//                 std::nullopt,
//                 3
//             );
//             static_assert(alt3 == Bits{"01001001"});
//             static constexpr Bits alt4 = Bits<8>::mask(
//                 std::nullopt,
//                 std::nullopt,
//                 4
//             );
//             static_assert(alt4 == Bits{"00010001"});
//             static constexpr Bits alt5 = Bits<8>::mask(
//                 std::nullopt,
//                 std::nullopt,
//                 5
//             );
//             static_assert(alt5 == Bits{"00100001"});

//             static constexpr Bits x = [] {
//                 Bits<4> x {"0100"};
//                 x.rotate(-2);
//                 return x;
//             }();
//             static_assert(x == Bits{"0001"});
//             auto y = double(Int<72>::max());
//             // double z = alt;

//             static constexpr Bits alt6 = Bits<72>::mask(
//                 std::nullopt,
//                 std::nullopt,
//                 3
//             );
//             static_assert(alt6 == Bits{"001001001001001001001001001001001001001001001001001001001001001001001001"});

//             static constexpr auto alt7 = [] {
//                 Bits x {"0100"};
//                 x[slice{0, 2}] = 3;
//                 return x;
//             }();
//             static_assert(alt7 == Bits{"0111"});

//             static constexpr auto alt8 = [] {
//                 Bits x {"0100"};
//                 x[slice{0, 2}] = std::vector<bool>{true, true};
//                 return x;
//             }();
//             static_assert(alt8 == Bits{"0111"});

//             static constexpr auto alt9 = [] {
//                 Bits x {"0100"};
//                 x[slice{0, 2}] = {true, true};
//                 return x;
//             }();
//             static_assert(alt9 == Bits{"0111"});

//             static_assert(alt9[slice{0, 2}] < 4);
//         }

//         {
//             static constexpr auto b = Bits{float(-1.0)};
//             static_assert(b == 0b1'01111111'00000000'00000000'0000000);
//             static_assert(float(b) == -1.0);
//             static_assert(uint32_t(b) == 0b1'01111111'00000000'00000000'0000000);
//         }
//     }

// }


#endif  // BERTRAND_BITSET_H
