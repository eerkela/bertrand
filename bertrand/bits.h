#ifndef BERTRAND_BITSET_H
#define BERTRAND_BITSET_H

#include "bertrand/common.h"
#include "bertrand/except.h"
#include "bertrand/math.h"
#include "bertrand/iter.h"
#include "bertrand/static_str.h"
#include <format>


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


namespace impl {
    struct Bits_tag {};
    struct UInt_tag {};
    struct Int_tag {};
}


namespace meta {

    template <typename T>
    concept Bits = inherits<T, impl::Bits_tag>;
    template <typename T>
    concept UInt = inherits<T, impl::UInt_tag>;
    template <typename T>
    concept Int = inherits<T, impl::Int_tag>;

    namespace detail {
        template <meta::Bits T>
        constexpr bool integer<T> = true;
        template <meta::Bits T>
        constexpr bool unsigned_integer<T> = true;
        template <meta::integer T> requires (meta::Bits<T>)
        constexpr size_t integer_width<T> = T::size();
        template <meta::Bits T>
        struct as_signed<T> { using type = bertrand::Int<T::size()>; };

        template <meta::UInt T>
        constexpr bool integer<T> = true;
        template <meta::UInt T>
        constexpr bool unsigned_integer<T> = true;
        template <meta::integer T> requires (meta::UInt<T>)
        constexpr size_t integer_width<T> = meta::unqualify<T>::width;
        template <meta::UInt T>
        struct as_signed<T> { using type = bertrand::Int<T::width>; };

        template <meta::Int T>
        constexpr bool integer<T> = true;
        template <meta::Int T>
        constexpr bool signed_integer<T> = true;
        template <meta::integer T> requires (meta::Int<T>)
        constexpr size_t integer_width<T> = meta::unqualify<T>::width;
        template <meta::Int T>
        struct as_unsigned<T> { using type = bertrand::UInt<T::width>; };
    }

}


namespace impl {

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

    template <size_t M>
    struct word {
        using type = uint64_t;
        static constexpr size_t size = meta::integer_width<type>;
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
    template <size_t M> requires (M <= meta::integer_width<uint8_t>)
    struct word<M> {
        using type = uint8_t;
        static constexpr size_t size = meta::integer_width<type>;
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
        requires (M > meta::integer_width<uint8_t> && M <= meta::integer_width<uint16_t>)
    struct word<M> {
        using type = uint16_t;
        static constexpr size_t size = meta::integer_width<type>;
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
        requires (M > meta::integer_width<uint16_t> && M <= meta::integer_width<uint32_t>)
    struct word<M> {
        using type = uint32_t;
        static constexpr size_t size = meta::integer_width<type>;
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


template <size_t N>
struct Bits : impl::Bits_tag {
    using word = impl::word<N>::type;
    using value_type = bool;
    using size_type = size_t;
    using index_type = ssize_t;

    /* The number of bits in each machine word. */
    static constexpr size_type word_size = impl::word<N>::size;

    /* The total number of words that back the bitset. */
    static constexpr size_type array_size = (N + word_size - 1) / word_size;

    /* A bitmask that identifies the valid bits for the last word, assuming
    `N % word_size` is nonzero.  Otherwise, if `N` is a clean multiple of `word_size`,
    then this mask will be set to zero. */
    static constexpr word end_mask = word(word(1) << (N % word_size)) - word(1);

    /* The number of bits that are held in the set. */
    [[nodiscard]] static constexpr size_type size() noexcept { return N; }
    [[nodiscard]] static constexpr index_type ssize() noexcept { return index_type(N); }

    /* The total number of bits needed to represent the array.  This will always be a
    equal to the product of `word_size` and `array_size`. */
    [[nodiscard]] static constexpr size_type capacity() noexcept {
        return array_size * word_size;
    }

private:
    using big_word = impl::word<N>::big;

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
        static constexpr size_type J = I + meta::integer_width<T>;
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
        static constexpr size_type J = I + meta::integer_width<T>;
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
            while (consumed < meta::integer_width<T>) {
                data[++i] |= word(v);
                if constexpr (word_size < meta::integer_width<T>) {
                    v >>= word_size;
                }
                consumed += word_size;
            }
        }

        from_ints<I + meta::integer_width<T>>(data, rest...);
    }

    template <size_type I, meta::Bits T, typename... Ts>
    static constexpr void from_ints(
        std::array<word, array_size>& data,
        const T& v,
        const Ts&... rest
    ) noexcept {
        static constexpr size_type J = I + meta::integer_width<T>;
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
        size_type& size
    ) {
        quotient.divmod(divisor, quotient, remainder);
        std::string_view out = digits[remainder.data()[0]];
        size += out.size();
        return out;
    }

    template <size_t J, word mask, size_type nbits>
    constexpr std::string_view extract_digit_power2(
        const auto& digits,
        size_type& size
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
        size += out.size();
        return out;
    }

    constexpr bool msb_is_set() const noexcept {
        if constexpr (end_mask) {
            return buffer[array_size - 1] >= word(word(1) << ((N % word_size) - 1));
        } else {
            return buffer[array_size - 1] >= word(word(1) << (word_size - 1));
        }
    }

    static constexpr Bits interval_mask(index_type start, index_type stop) noexcept {
        Bits result;
        result.fill(1);
        result <<= N - size_type(stop);
        result >>= N - size_type(stop - start);
        result <<= size_type(start);  // [start, stop).
        return result;
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

    /* Construct a bitset from a variadic parameter pack of bitset and/or boolean
    initializers.  The sequence is parsed left-to-right in little-endian order, meaning
    the first argument will correspond to the least significant bits in the bitset, and
    each argument counts upward in significance by its respective bit width.  Any
    remaining bits will be set to zero.  The total bit width of the arguments cannot
    exceed `N`, otherwise the constructor will fail to compile. */
    template <typename... bits>
        requires (
            (impl::strict_bits<bits> && ...) &&
            ((meta::integer_width<bits> + ... + 0) <= N)
        )
    [[nodiscard]] constexpr Bits(const bits&... vals) noexcept : buffer{} {
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
            !(impl::strict_bits<words> && ...) &&
            (meta::integer<words> && ... && (
                (meta::integer_width<words> + ... + 0) <= max(capacity(), 64)
            ))
        )
    [[nodiscard]] constexpr Bits(const words&... vals) noexcept : buffer{} {
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

    /* Trivially swap the values of two bitsets. */
    constexpr void swap(Bits& other) noexcept {
        if (this != &other) {
            buffer.swap(other.buffer);
        }
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
                                max(zero.size(), one.size())
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
        if constexpr (base >= (1ULL << min(N, word_size - 1))) {
            return std::string(digits[buffer[0]]);

        // if the base is a power of 2, then we can use a simple bitscan to determine
        // the final string, rather than doing multi-word division
        } else if constexpr (impl::is_power2(base)) {
            static constexpr word mask = word(base - 1);
            static constexpr size_type nbits = std::bit_width(mask);

            size_type size = 0;
            auto parts = [this]<size_t... Js>(
                std::index_sequence<Js...>,
                size_type& size
            ) {
                return std::array<std::string_view, ceil>{
                    extract_digit_power2<Js, mask, nbits>(digits, size)...
                };
            }(std::make_index_sequence<ceil>{}, size);

            // join the substrings in reverse order to create the final result
            std::string result;
            result.reserve(size);
            for (size_type i = ceil; i-- > 0;) {
                result.append(parts[i]);
            }
            return result;

        // otherwise, use modular division to calculate all the substrings needed to
        // represent the value, from the least significant digit to most significant.
        } else {
            Bits quotient = *this;
            Bits remainder;
            size_type size = 0;
            auto parts = []<size_t... Js>(
                std::index_sequence<Js...>,
                Bits& quotient,
                Bits& remainder,
                size_type& size
            ) {
                return std::array<std::string_view, ceil>{
                    extract_digit<Js, divisor>(digits, quotient, remainder, size)...
                };
            }(std::make_index_sequence<ceil>{}, quotient, remainder, size);

            // join the substrings in reverse order to create the final result
            std::string result;
            result.reserve(size);
            for (size_type i = ceil; i-- > 0;) {
                result.append(parts[i]);
            }
            return result;
        }
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

    /* Implicitly convert a single-word bitset to its underlying integer
    representation. */
    [[nodiscard]] constexpr operator word() const noexcept requires(array_size == 1) {
        return buffer[0];
    }

    /* Explicitly convert a multi-word bitset into an integer type, possibly truncating
    any upper bits. */
    template <std::integral T>
    [[nodiscard]] explicit constexpr operator T() const noexcept
        requires(array_size > 1 && meta::explicitly_convertible_to<word, T>)
    {
        return static_cast<T>(buffer[0]);
    }

    /* Explicitly convert a multi-word bitset into a floating point type, retaining as
    much precision as possible. */
    template <meta::floating T>
    [[nodiscard]] explicit constexpr operator T() const noexcept
        requires(array_size > 1 && meta::explicitly_convertible_to<word, T>)
    {
        T value = 0;
        for (size_type i = array_size; i-- > 0;) {
            value = std::ldexp(value, word_size) + static_cast<T>(buffer[i]);
        }
        return value;
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
        index_type norm_start = max(start + ssize() * (start < 0), index_type(0));
        index_type norm_stop = min(stop + ssize() * (stop < 0), ssize());
        if (norm_stop <= norm_start) {
            return false;
        }
        return bool(*this & interval_mask(norm_start, norm_stop));
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
        index_type norm_start = max(start + ssize() * (start < 0), index_type(0));
        index_type norm_stop = min(stop + ssize() * (stop < 0), ssize());
        if (norm_stop <= norm_start) {
            return false;
        }
        Bits temp = interval_mask(norm_start, norm_stop);
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
        index_type norm_start = max(start + ssize() * (start < 0), index_type(0));
        index_type norm_stop = min(stop + ssize() * (stop < 0), ssize());
        if (norm_stop <= norm_start) {
            return 0;
        }
        Bits temp = interval_mask(norm_start, norm_stop);
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
        index_type norm_start = max(start + ssize() * (start < 0), index_type(0));
        index_type norm_stop = min(stop + ssize() * (stop < 0), ssize());
        if (norm_stop <= norm_start) {
            return *this;
        }
        if (value) {
            *this |= interval_mask(norm_start, norm_stop);
        } else {
            *this &= ~interval_mask(norm_start, norm_stop);
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
        index_type norm_start = max(start + ssize() * (start < 0), index_type(0));
        index_type norm_stop = min(stop + ssize() * (stop < 0), ssize());
        if (norm_stop <= norm_start) {
            return *this;
        }
        *this ^= interval_mask(norm_start, norm_stop);
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
        index_type norm_start = max(start + ssize() * (start < 0), index_type(0));
        index_type norm_stop = min(stop + ssize() * (stop < 0), ssize());
        if (norm_stop <= norm_start) {
            return *this;
        }
        Bits temp = interval_mask(norm_start, norm_stop);
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
    /// -> ChatGPT seems to think I can derive the correct sign bit after the operation,
    /// so that I don't have to do anything special in these cases.  The only odd ball
    /// is division, where I have to strip the signs by taking the complement, and
    /// then implementing the division algorithm, and then taking the complement again
    /// if the result is negative.

    /* Convert the value to its two's complement equivalent.  This is equivalent to
    flipping the sign for a signed integral value. */
    constexpr Bits& complement() noexcept {
        flip();
        ++*this;
        return *this;
    }

    /* Return +1 if the value is positive or -1 if it is negative.  For unsigned
    bitsets, this will always return +1. */
    constexpr int sign() const noexcept {
        return 1;
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

    /* Return the index of the first bit that is set within a given interval, or an
    empty optional if no bits are set. */
    [[nodiscard]] constexpr Optional<index_type> first_one(
        index_type start,
        index_type stop = ssize()
    ) const noexcept {
        index_type norm_start = max(start + ssize() * (start < 0), index_type(0));
        index_type norm_stop = min(stop + ssize() * (stop < 0), ssize());
        if (norm_stop <= norm_start) {
            return std::nullopt;
        }
        Bits temp = interval_mask(norm_start, norm_stop);
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
        index_type norm_start = max(start + ssize() * (start < 0), index_type(0));
        index_type norm_stop = min(stop + ssize() * (stop < 0), ssize());
        if (norm_stop <= norm_start) {
            return std::nullopt;
        }
        Bits temp = interval_mask(norm_start, norm_stop);
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
        index_type norm_start = max(start + ssize() * (start < 0), index_type(0));
        index_type norm_stop = min(stop + ssize() * (stop < 0), ssize());
        if (norm_stop <= norm_start) {
            return std::nullopt;
        }
        Bits temp = interval_mask(norm_start, norm_stop);
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
        index_type norm_start = max(start + ssize() * (start < 0), index_type(0));
        index_type norm_stop = min(stop + ssize() * (stop < 0), ssize());
        if (norm_stop <= norm_start) {
            return std::nullopt;
        }
        Bits temp = interval_mask(norm_start, norm_stop);
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

    /// TODO: root<N>(), log<N>(), pow<N>()?

    /* Lexicographically compare two bitsets of equal size. */
    [[nodiscard]] friend constexpr auto operator<=>(
        const Bits& lhs,
        const Bits& rhs
    ) noexcept requires(array_size > 1) {
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
    ) noexcept requires(array_size > 1) {
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
    ) noexcept requires(array_size > 1) {
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
    ) noexcept requires(array_size > 1) {
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
    ) noexcept requires(array_size > 1) {
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
    [[nodiscard]] constexpr Bits operator<<(size_type rhs) const noexcept
        requires(array_size > 1)
    {
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
    [[nodiscard]] constexpr Bits operator>>(size_type rhs) const noexcept
        requires(array_size > 1)
    {
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

    /* Return a positive copy of the bitset.  This devolves to a simple copy for
    unsigned bitsets. */
    [[nodiscard]] constexpr Bits operator+() const noexcept { return *this; }

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
    ) noexcept requires(array_size > 1) {
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

    /* Return a negative copy of the bitset.  This equates to a copy followed by a
    `complement()` modifier. */
    [[nodiscard]] constexpr Bits operator-() const noexcept {
        Bits result = *this;
        result.complement();
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
    ) noexcept requires(array_size > 1) {
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
    ) noexcept requires(array_size > 1) {
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
    ) requires(array_size > 1) {
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
    ) requires(array_size > 1) {
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
        requires(array_size > 1)
    {
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
        std::string str = ctx.template digits_with_grouping<char>(set);

        // 3) adjust for sign + base prefix, and fill to width
        ctx.template pad_and_align<char>(str);

        // 4) write to output stream
        os.write(str.data(), str.size());
        os.width(0);  // reset width for next output (mandatory)
        return os;
    }

    /* Read the bitset from an input stream. */
    constexpr friend std::istream& operator>>(std::istream& is, Bits& set) {
        std::istream::sentry s(is);  // automatically skips leading whitespace
        if (!s) {
            return is;  // stream is not ready for input
        }
        impl::scan_bits ctx(
            is,
            std::use_facet<std::numpunct<char>>(is.getloc())
        );
        ctx.scan(is, set);
        return is;
    }
};


template <meta::integer... Ts>
Bits(Ts...) -> Bits<(meta::integer_width<Ts> + ... + 0)>;
template <size_t N>
Bits(const char(&)[N]) -> Bits<N - 1>;
template <size_t N>
Bits(const char(&)[N], char) -> Bits<N - 1>;
template <size_t N>
Bits(const char(&)[N], char, char) -> Bits<N - 1>;


/* ADL `swap()` method for `bertrand::Bits` instances.  */
template <size_t N>
constexpr void swap(Bits<N>& lhs, Bits<N>& rhs) noexcept {
    lhs.swap(rhs);
}


template <size_t N>
struct UInt : impl::UInt_tag {
    /* The underlying word type for the integer. */
    using word = Bits<N>::word;

    /* The number of bits needed to represent the integer.  Equivalent to `N`. */
    static constexpr size_t width = N;

    /* A bitset holding the bitwise representation of the integer. */
    Bits<N> bits;

    /* Construct an integer from a variadic parameter pack of component words of exact
    width.  See `Bits<N>` for more details. */
    template <typename... bits>
        requires (
            (impl::strict_bits<bits> && ...) &&
            ((meta::integer_width<bits> + ... + 0) <= N)
        )
    [[nodiscard]] constexpr UInt(const bits&... vals) noexcept : bits(vals...) {}

    /* Construct an integer from a sequence of integer values whose bit widths sum to
    an amount less than or equal to the integer's storage capacity.  See `Bits<N>` for
    more details. */
    template <typename... words>
        requires (
            sizeof...(words) > 0 &&
            !(impl::strict_bits<words> && ...) &&
            (meta::integer<words> && ... && (
                (meta::integer_width<words> + ... + 0) <= max(Bits<N>::capacity(), 64)
            ))
        )
    [[nodiscard]] constexpr UInt(const words&... vals) noexcept : bits(vals...) {}


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
        return Bits<N>::template from_string<zero, one, rest...>(str);
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
        return Bits<N>::template from_string<zero, one, rest...>(str, continuation);
    }

    /* A shorthand for `from_string<"0", "1">(str)`, which decodes a string in the
    canonical binary representation. */
    [[nodiscard]] static constexpr auto from_binary(std::string_view str) noexcept
        -> Expected<UInt, ValueError, OverflowError>
    {
        return Bits<N>::from_binary(str);
    }

    /* A shorthand for `from_string<"0", "1">(str, continuation)`, which decodes a
    string in the canonical binary representation. */
    [[nodiscard]] static constexpr Expected<UInt, OverflowError> from_binary(
        std::string_view str,
        std::string_view& continuation
    ) noexcept {
        return Bits<N>::from_binary(str, continuation);
    }

    /* A shorthand for `from_string<"0", "1", "2", "3", "4", "5", "6", "7">(str)`,
    which decodes a string in the canonical octal representation. */
    [[nodiscard]] static constexpr auto from_octal(std::string_view str) noexcept
        -> Expected<UInt, ValueError, OverflowError>
    {
        return Bits<N>::from_octal(str);
    }

    /* A shorthand for `from_string<"0", "1", "2", "3", "4", "5", "6", "7">(str, continuation)`,
    which decodes a string in the canonical octal representation. */
    [[nodiscard]] static constexpr Expected<UInt, OverflowError> from_octal(
        std::string_view str,
        std::string_view& continuation
    ) noexcept {
        return Bits<N>::from_octal(str, continuation);
    }

    /* A shorthand for
    `from_string<"0", "1", "2", "3", "4", "5", "6", "7", "8", "9">(str)`, which decodes
    a string in the canonical decimal representation. */
    [[nodiscard]] static constexpr auto from_decimal(std::string_view str) noexcept
        -> Expected<UInt, ValueError, OverflowError>
    {
        return Bits<N>::from_decimal(str);
    }

    /* A shorthand for
    `from_string<"0", "1", "2", "3", "4", "5", "6", "7", "8", "9">(str, continuation)`,
    which decodes a string in the canonical decimal representation. */
    [[nodiscard]] static constexpr Expected<UInt, OverflowError> from_decimal(
        std::string_view str,
        std::string_view& continuation
    ) noexcept {
        return Bits<N>::from_decimal(str, continuation);
    }

    /* A shorthand for
    `from_string<"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "A", "B", "C", "D", "E", "F">(str)`,
    which decodes a string in the canonical hexadecimal representation. */
    [[nodiscard]] static constexpr auto from_hex(std::string_view str) noexcept
        -> Expected<UInt, ValueError, OverflowError>
    {
        return Bits<N>::from_hex(str);
    }

    /* A shorthand for
    `from_string<"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "A", "B", "C", "D", "E", "F">(str, continuation)`,
    which decodes a string in the canonical hexadecimal representation. */
    [[nodiscard]] static constexpr Expected<UInt, OverflowError> from_hex(
        std::string_view str,
        std::string_view& continuation
    ) noexcept {
        return Bits<N>::from_hex(str, continuation);
    }

    /// TODO: the result from to_string() is zero padded to the exact width of the
    /// underlying bitset, which should not be the case for integers, or for
    /// std::format or std::ostream formatting.  I can either eliminate it for the
    /// bitset itself, or add a special case.

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
        return bits.to_binary();
    }

    /* A shorthand for `to_string<"0", "1", "2", "3", "4", "5", "6", "7">()`, which
    yields a string in the canonical octal representation. */
    [[nodiscard]] constexpr std::string to_octal() const noexcept {
        return bits.to_octal();
    }

    /* A shorthand for `to_string<"0", "1", "2", "3", "4", "5", "6", "7", "8", "9">()`,
    which yields a string in the canonical decimal representation. */
    [[nodiscard]] constexpr std::string to_decimal() const noexcept {
        return bits.to_decimal();
    }

    /* A shorthand for
    `to_string<"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "A", "B", "C", "D", "E", "F">()`,
    which yields a string in the canonical hexadecimal representation. */
    [[nodiscard]] constexpr std::string to_hex() const noexcept {
        return bits.to_hex();
    }

    /* Convert the bitset to a string representation with '1' as the true character and
    '0' as the false character.  Note that the string is returned in big-endian order,
    meaning the first character corresponds to the most significant bit in the bitset,
    and the last character corresponds to the least significant bit.  The string will
    be zero-padded to the exact width of the bitset, and can be passed to
    `Bits::from_binary()` to recover the original state. */
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
        requires(Bits<N>::array_size == 1)
    {
        return word(bits);
    }

    /* Explicitly convert a multi-word integer into a hardware integer type, possibly
    truncating any upper bits. */
    template <meta::integer T>
    [[nodiscard]] explicit constexpr operator T() const noexcept
        requires(Bits<N>::array_size > 1 && meta::explicitly_convertible_to<Bits<N>, T>)
    {
        return static_cast<T>(bits);
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
        UInt& result
    ) const noexcept {
        bits.add(other.bits, overflow, result.bits);
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
        UInt& result
    ) const noexcept {
        bits.sub(other.bits, overflow, result.bits);
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
        UInt& result
    ) const noexcept {
        bits.mul(other.bits, overflow, result.bits);
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
    ) const noexcept {
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
    ) const noexcept {
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
    ) const noexcept {
        bits.divmod(other.bits, quotient.bits, remainder.bits);
    }

    /* Compare two integers of equal size. */
    [[nodiscard]] friend constexpr auto operator<=>(
        const UInt& lhs,
        const UInt& rhs
    ) noexcept requires(Bits<N>::array_size > 1) {
        return lhs.bits <=> rhs.bits;
    }

    /* Compare two integers of equal size. */
    [[nodiscard]] friend constexpr bool operator==(
        const UInt& lhs,
        const UInt& rhs
    ) noexcept requires(Bits<N>::array_size > 1) {
        return lhs.bits == rhs.bits;
    }

    /* Apply a bitwise NOT to the integer. */
    [[nodiscard]] friend constexpr UInt operator~(const UInt& set) noexcept {
        return ~set.bits;
    }

    /* Apply a bitwise AND between the contents of two integers of equal size. */
    [[nodiscard]] friend constexpr UInt operator&(
        const UInt& lhs,
        const UInt& rhs
    ) noexcept requires(Bits<N>::array_size > 1) {
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
    ) noexcept requires(Bits<N>::array_size > 1) {
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
    ) noexcept requires(Bits<N>::array_size > 1) {
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
        requires(Bits<N>::array_size > 1)
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
        requires(Bits<N>::array_size > 1)
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
    ) noexcept requires(Bits<N>::array_size > 1) {
        return lhs.bits + rhs.bits;
    }

    /* Operator version of `UInt.add()` that discards the overflow flag and
    writes the sum back to this integer. */
    constexpr UInt& operator+=(const UInt& other) noexcept {
        bits += other.bits;
        return *this;
    }

    /* Return a negative copy of the integer.  This equates to a copy followed by a
    `complement()` modifier. */
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
    ) noexcept requires(Bits<N>::array_size > 1) {
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
    ) noexcept requires(Bits<N>::array_size > 1) {
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
    ) requires(Bits<N>::array_size > 1) {
        return lhs.bits / rhs.bits;
    }

    /* Operator version of `UInt.divmod()` that discards the remainder and
    writes the quotient back to this integer. */
    constexpr UInt& operator/=(const UInt& other) {
        bits /= other.bits;
        return *this;
    }

    /* Operator version of `UInt.divmod()` that discards the quotient. */
    [[nodiscard]] friend constexpr UInt operator%(
        const UInt& lhs,
        const UInt& rhs
    ) requires(Bits<N>::array_size > 1) {
        return lhs.bits % rhs.bits;
    }

    /* Operator version of `UInt.divmod()` that discards the quotient and
    writes the remainder back to this integer. */
    constexpr UInt& operator%=(const UInt& other) {
        bits %= other.bits;
        return *this;
    }

    /* Print the integer to an output stream. */
    constexpr friend std::ostream& operator<<(
        std::ostream& os,
        const UInt& set
    ) {
        return os << set.bits;  // defaults to decimal
    }

    /* Read the integer from an input stream. */
    constexpr friend std::istream& operator>>(
        std::istream& is,
        UInt& set
    ) {
        return is >> set.bits;  // defaults to decimal
    }

};


template <meta::integer... Ts>
UInt(Ts...) -> UInt<(meta::integer_width<Ts> + ... + 0)>;


/* ADL `swap()` method for `bertrand::UInt` instances. */
template <size_t N>
constexpr void swap(UInt<N>& lhs, UInt<N>& rhs) noexcept {
    lhs.value.swap(rhs.value);
}


template <size_t N>
struct Int : impl::Int_tag {
    Bits<N> value;

    /// TODO: a very thin wrapper around `Bits<N>` that applies two's complement
    /// and modifies the arithmetic operators very slightly, so that I can use it as
    /// a generalized integer type.

};


template <meta::integer... Ts>
Int(Ts...) -> Int<(meta::integer_width<Ts> + ... + 0)>;


/* ADL `swap()` method for `bertrand::Int<N>` instances. */
template <size_t N>
constexpr void swap(Int<N>& lhs, Int<N>& rhs) noexcept {
    lhs.value.swap(rhs.value);
}


}  // namespace bertrand


namespace std {

    /* Specializing `std::formatter` allows bitsets to be formatted using `std::format`,
    `repr()`, `print`, and other formatting functions. */
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

    /* Specializing `std::formatter` allows unsigned integers to be formatted using
    `std::format`, `repr()`, `print`, and other formatting functions.  This reuses the
    same logic as `Bits<N>`. */
    template <bertrand::meta::UInt T>
    struct formatter<T> {
        using const_reference = bertrand::meta::as_const<bertrand::meta::as_lvalue<T>>;
        formatter<bertrand::Bits<bertrand::meta::integer_width<T>>> config;
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
    `std::format`, `repr()`, `print`, and other formatting functions. */
    template <bertrand::meta::Int T>
    struct formatter<T> {
        using const_reference = bertrand::meta::as_const<bertrand::meta::as_lvalue<T>>;
        formatter<bertrand::Bits<bertrand::meta::integer_width<T>>> config;
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
        static constexpr bool has_denorm                    = false;
        static constexpr bool has_denorm_loss               = false;
        static constexpr std::float_round_style round_style =
            std::float_round_style::round_toward_zero;
        static constexpr bool is_iec559                     = false;
        static constexpr bool is_bounded                    = true;
        static constexpr bool is_modulo                     = true;
        static constexpr int digits                         = bertrand::meta::integer_width<T>;
        static constexpr int digits10                       = digits * 0.3010299956639812;  // log10(2)
        static constexpr int max_digits10                   = 0;
        static constexpr int radix                          = 2;
        static constexpr int min_exponent                   = 0;
        static constexpr int min_exponent10                 = 0;
        static constexpr int max_exponent                   = 0;
        static constexpr int max_exponent10                 = 0;
        static constexpr bool traps                         = true;  // division by zero should trap
        static constexpr bool tinyness_before               = false;
        static constexpr type min() noexcept { return {}; }
        static constexpr type lowest() noexcept { return {}; }
        static constexpr type max() noexcept {
            type result;
            for (size_t i = 0; i < type::array_size - (type::end_mask > 0); ++i) {
                result.buffer[i] = std::numeric_limits<word>::max();
            }
            if constexpr (type::end_mask) {
                result.buffer[type::array_size - 1] = type::end_mask;
            }
            return result;
        }
        static constexpr type epsilon() noexcept { return {}; }
        static constexpr type round_error() noexcept { return {}; }
        static constexpr type infinity() noexcept { return {}; }
        static constexpr type quiet_NaN() noexcept { return {}; }
        static constexpr type signaling_NaN() noexcept { return {}; }
        static constexpr type denorm_min() noexcept { return {}; }
    };

    /* Specializing `std::numeric_limits` allows bitsets to be introspected just like
    other integer types. */
    template <bertrand::meta::UInt T>
    struct numeric_limits<T> {
    private:
        using type = std::remove_cvref_t<T>;
        using bits = bertrand::Bits<bertrand::meta::integer_width<T>>;
        using traits = std::numeric_limits<bits>;

    public:
        static constexpr bool is_specialized                = traits::is_specialized;
        static constexpr bool is_signed                     = traits::is_signed;
        static constexpr bool is_integer                    = traits::is_integer;
        static constexpr bool is_exact                      = traits::is_exact;
        static constexpr bool has_infinity                  = traits::has_infinity;
        static constexpr bool has_quiet_NaN                 = traits::has_quiet_NaN;
        static constexpr bool has_signaling_NaN             = traits::has_signaling_NaN;
        static constexpr bool has_denorm                    = traits::has_denorm;
        static constexpr bool has_denorm_loss               = traits::has_denorm_loss;
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
        static constexpr type min() noexcept { return {}; }
        static constexpr type lowest() noexcept { return {}; }
        static constexpr type max() noexcept { return {traits::max()}; }
        static constexpr type epsilon() noexcept { return {}; }
        static constexpr type round_error() noexcept { return {}; }
        static constexpr type infinity() noexcept { return {}; }
        static constexpr type quiet_NaN() noexcept { return {}; }
        static constexpr type signaling_NaN() noexcept { return {}; }
        static constexpr type denorm_min() noexcept { return {}; }
    };

    /* Specializing `std::numeric_limits` allows bitsets to be introspected just like
    other integer types. */
    template <bertrand::meta::Int T>
    struct numeric_limits<T> {
    private:
        using type = std::remove_cvref_t<T>;
        using bits = bertrand::Bits<bertrand::meta::integer_width<T>>;
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
        static constexpr bool has_denorm                    = traits::has_denorm;
        static constexpr bool has_denorm_loss               = traits::has_denorm_loss;
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
        static constexpr type min() noexcept {
            // only most significant bit is set -> two's complement
            type result;
            if constexpr (bits::end_mask) {
                result.bits.data()[bits::array_size - 1] =
                    word(word(1) << ((digits % bits::word_size) - 1));
            } else {
                result.bits.data()[bits::array_size - 1] =
                    word(word(1) << (bits::word_size - 1));
            }
            return result;
        }
        static constexpr type lowest() noexcept { return min(); }
        static constexpr type max() noexcept {
            // all but the most significant bit are set -> two's complement
            type result;
            for (size_t i = 0; i < bits::array_size - 1; ++i) {
                result.bits.data()[i] = std::numeric_limits<word>::max();
            }
            if constexpr (bits::end_mask) {
                result.bits.data()[bits::array_size - 1] = word(bits::end_mask >> 1);
            } else {
                result.bits.data()[bits::array_size - 1] =
                    word(std::numeric_limits<word>::max() >> 1);
            }
            return result;
        }
        static constexpr type epsilon() noexcept { return {}; }
        static constexpr type round_error() noexcept { return {}; }
        static constexpr type infinity() noexcept { return {}; }
        static constexpr type quiet_NaN() noexcept { return {}; }
        static constexpr type signaling_NaN() noexcept { return {}; }
        static constexpr type denorm_min() noexcept { return {}; }
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

    /* Specializing `std::hash` allows bitsets to be used as keys in hash tables. */
    template <bertrand::meta::UInt T>
    struct hash<T> {
        using const_reference = bertrand::meta::as_const<bertrand::meta::as_lvalue<T>>;
        [[nodiscard]] static constexpr size_t operator()(const_reference x) noexcept {
            return hash<bertrand::Bits<bertrand::meta::integer_width<T>>>{}(x.bits);
        }
    };

    /* Specializing `std::hash` allows bitsets to be used as keys in hash tables. */
    template <bertrand::meta::Int T>
    struct hash<T> {
        using const_reference = bertrand::meta::as_const<bertrand::meta::as_lvalue<T>>;
        [[nodiscard]] static constexpr size_t operator()(const_reference x) noexcept {
            return hash<bertrand::Bits<bertrand::meta::integer_width<T>>>{}(x.bits);
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
//             static_assert(b.first_one(2).value() == 3);
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

//             for (auto&& x : a.components()) {

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

//             static constexpr UInt y = -1;
//             static_assert(y.bits.count() == 32);
//         }
//     }

// }


#endif  // BERTRAND_BITSET_H
