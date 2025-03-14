#ifndef BERTRAND_STATIC_STRING_H
#define BERTRAND_STATIC_STRING_H

#include "bertrand/common.h"
#include "bertrand/iter.h"


// required for demangling
#if defined(__GNUC__) || defined(__clang__)
    #include <cxxabi.h>
    #include <cstdlib>
#elif defined(_MSC_VER)
    #include <windows.h>
    #include <dbghelp.h>
    #pragma comment(lib, "dbghelp.lib")
#endif


namespace bertrand {


/// TODO: noexcept specifiers may need to be conditional for operations like
/// concatenation/slicing, etc.  Anything that can't be done strictly at compile time


/* C++20 expands support for non-type template parameters, including compile-time
strings.  This helper class allows ASCII string literals to be encoded directly as
template parameters, and for them to be manipulated entirely at compile-time using
the familiar Python string interface.  Furthermore, templates can be specialized based
on these strings, allowing for full compile-time flexibility based on their values. */
template <size_t N>
struct static_str;


namespace impl {
    struct static_str_tag {};

    constexpr bool char_islower(char c) noexcept {
        return c >= 'a' && c <= 'z';
    }

    constexpr bool char_isupper(char c) noexcept {
        return c >= 'A' && c <= 'Z';
    }

    constexpr bool char_isalpha(char c) noexcept {
        return char_islower(c) || char_isupper(c);
    }

    constexpr bool char_isdigit(char c) noexcept {
        return c >= '0' && c <= '9';
    }

    constexpr bool char_isalnum(char c) noexcept {
        return char_isalpha(c) || char_isdigit(c);
    }

    constexpr bool char_isascii(char c) noexcept {
        return c >= 0 && c <= 127;
    }

    constexpr bool char_isspace(char c) noexcept {
        return c == ' ' || (c >= '\t' && c <= '\r');
    }

    constexpr bool char_isdelimeter(char c) noexcept {
        switch (c) {
            case ' ':
            case '\t':
            case '\n':
            case '\r':
            case '\f':
            case '\v':
            case '.':
            case '!':
            case '?':
            case ',':
            case ';':
            case ':':
            case '#':
            case '&':
            case '+':
            case '-':
            case '*':
            case '/':
            case '|':
            case '\\':
            case '(':
            case ')':
            case '[':
            case ']':
            case '{':
            case '}':
            case '<':
            case '>':
                return true;
            default:
                return false;
        }
    }

    constexpr bool char_islinebreak(char c) noexcept {
        switch (c) {
            case '\n':
            case '\r':
            case '\v':
            case '\f':
            case '\x1c':
            case '\x1d':
            case '\x1e':
            case '\x85':
                return true;
            default:
                return false;
        }
    }

    constexpr char char_tolower(char c) noexcept {
        return char_isupper(c) ? c + ('a' - 'A') : c;
    }

    constexpr char char_toupper(char c) noexcept {
        return char_islower(c) ? c + ('A' - 'a') : c;
    }

    template <typename T>
    constexpr auto type_name_impl() {
        #if defined(__clang__)
            constexpr std::string_view prefix {"[T = "};
            constexpr std::string_view suffix {"]"};
            constexpr std::string_view function {__PRETTY_FUNCTION__};
        #elif defined(__GNUC__)
            constexpr std::string_view prefix {"with T = "};
            constexpr std::string_view suffix {"]"};
            constexpr std::string_view function {__PRETTY_FUNCTION__};
        #elif defined(_MSC_VER)
            constexpr std::string_view prefix {"type_name_impl<"};
            constexpr std::string_view suffix {">(void)"};
            constexpr std::string_view function {__FUNCSIG__};
        #else
            #error Unsupported compiler
        #endif

        constexpr size_t start = function.find(prefix) + prefix.size();
        constexpr size_t end = function.rfind(suffix);
        static_assert(start < end);

        constexpr std::string_view name = function.substr(start, (end - start));
        constexpr size_t N = name.size();
        return static_str<N>{name.data()};
    }

}


namespace meta {

    namespace detail {

        template <size_t I, static_str... Strs>
        struct _unpack_string;
        template <size_t I, static_str Str, static_str... Strs>
        struct _unpack_string<I, Str, Strs...> {
            static constexpr static_str value = _unpack_string<I - 1, Strs...>::value;
        };
        template <static_str Str, static_str... Strs>
        struct _unpack_string<0, Str, Strs...> {
            static constexpr static_str value = Str;
        };

        template <static_str...>
        constexpr bool strings_are_unique = true;
        template <static_str First, static_str... Rest>
        constexpr bool strings_are_unique<First, Rest...> =
            ((First != Rest) && ...) && strings_are_unique<Rest...>;

    }

    template <size_t I, static_str... Strs>
    constexpr static_str unpack_string = detail::_unpack_string<I, Strs...>::value;

    template <static_str... Strings>
    concept strings_are_unique = detail::strings_are_unique<Strings...>;

    template <typename T>
    concept static_str = inherits<T, impl::static_str_tag>;

}


/* CTAD guide allows static_str to be used as a template parameter accepting string
literals with arbitrary length. */
template <size_t N>
static_str(const char(&)[N]) -> static_str<N - 1>;


template <static_str... strings>
struct static_strings {
    static constexpr bool unique = meta::strings_are_unique<strings...>;

    template <size_t I> requires (I < sizeof...(strings))
    static constexpr static_str at = meta::unpack_string<I, strings...>;

    /// TODO: some kind of helper that serves as a list-like container for a bunch of
    /// strings.  More methods could be added to this class to make it more useful
    /// over time.  Eventually, I should rework the static_str methods that currently
    /// return tuples of strings (e.g. `split()`) to return instantiations of this
    /// template instead, and the methods that take in lists of strings (e.g. `join()`)
    /// could accept them as template parameters as well.  That way you could do
    /// something like:

    /// static_str<>::join<" ", static_str<>::split<".", "a.b.c.d.e">>()
    /// -> "a b c d e"
};


template <size_t N = 0>
struct static_str : impl::static_str_tag {
private:
    template <size_t M>
    friend class static_str;

    template <bertrand::static_str self, bertrand::static_str chars>
    static consteval size_t first_non_stripped() noexcept {
        for (size_t i = 0; i < self.size(); ++i) {
            char c = self.buffer[i];
            size_t j = 0;
            while (j < chars.size()) {
                if (chars.buffer[j++] == c) {
                    break;
                } else if (j == chars.size()) {
                    return i;
                }
            }
        }
        return missing;
    }

    template <bertrand::static_str self, bertrand::static_str chars>
    static consteval size_t last_non_stripped() noexcept {
        for (size_t i = 0; i < self.size(); ++i) {
            size_t idx = self.size() - i - 1;
            char c = self.buffer[idx];
            size_t j = 0;
            while (j < chars.size()) {
                if (chars.buffer[j++] == c) {
                    break;
                } else if (j == chars.size()) {
                    return idx;
                }
            }
        }
        return missing;
    }

    template <long long num, size_t base>
    static constexpr size_t _int_length = [] {
        if constexpr (num == 0) {
            return 0;
        } else {
            return _int_length<num / base, base> + 1;
        }
    }();

    template <long long num, size_t base>
    static constexpr size_t int_length = [] {
        // length is always at least 1 to correct for num == 0
        if constexpr (num < 0) {
            return std::max(_int_length<-num, base>, 1UL) + 1;  // include negative sign
        } else {
            return std::max(_int_length<num, base>, 1UL);
        }
    }();

    template <typename T>
    struct bit_view;
    template <typename T> requires (sizeof(T) == 1)
    struct bit_view<T> { using type = uint8_t; };
    template <typename T> requires (sizeof(T) == 2)
    struct bit_view<T> { using type = uint16_t; };
    template <typename T> requires (sizeof(T) == 4)
    struct bit_view<T> { using type = uint32_t; };
    template <typename T> requires (sizeof(T) == 8)
    struct bit_view<T> { using type = uint64_t; };

    static constexpr bool sign_bit(double num) noexcept {
        using Int = bit_view<double>::type;
        return std::bit_cast<Int>(num) >> (8 * sizeof(double) - 1);
    };

    template <double num, size_t precision>
    static constexpr size_t float_length = [] {
        if constexpr (std::isnan(num)) {
            return 3;  // "nan"
        } else if constexpr (std::isinf(num)) {
            return 3 + sign_bit(num);  // "inf" or "-inf"
        } else {
            // negative zero integral part needs a leading minus sign
            return
                int_length<static_cast<long long>(num), 10> +
                (static_cast<long long>(num) == 0 && sign_bit(num)) +
                (precision > 0) +
                precision;
        }
    }();

    constexpr static_str() = default;

public:
    using value_type = const char;
    using reference = value_type&;
    using const_reference = reference;
    using pointer = value_type*;
    using const_pointer = pointer;
    using size_type = size_t;
    using difference_type = std::ptrdiff_t;
    using iterator = impl::contiguous_iterator<value_type>;
    using const_iterator = iterator;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;
    using slice = impl::contiguous_slice<value_type>;
    using const_slice = slice;

    /* A placeholder index returned when a substring is not present. */
    static constexpr size_t missing = std::numeric_limits<size_t>::max();

    char buffer[N + 1];  // +1 for null terminator

    consteval static_str(const char* arr) noexcept : buffer{} {
        for (size_t i = 0; i < N; ++i) {
            buffer[i] = arr[i];
        }
        buffer[N] = '\0';
    }

    /* Convert an integer into a string at compile time using the specified base. */
    template <long long num, size_t base = 10> requires (base >= 2 && base <= 36)
    static constexpr auto from_int = [] {
        constexpr const char chars[] = "0123456789abcdefghijklmnopqrstuvwxyz";
        constexpr size_t len = int_length<num, base>;
        static_str<len> result;

        long long temp = num;
        size_t idx = len - 1;
        if constexpr (num < 0) {
            result.buffer[0] = '-';
            temp = -temp;
        } else if constexpr (num == 0) {
            result.buffer[idx--] = '0';
        }

        while (temp > 0) {
            result.buffer[idx--] = chars[temp % base];
            temp /= base;
        }

        result.buffer[len] = '\0';
        return result;
    }();

    /* Convert a floating point number into a string at compile time with the
    specified precision. */
    template <double num, size_t precision = 6>
    static constexpr auto from_float = [] {
        constexpr size_t len = float_length<num, precision>;
        static_str<len> result;

        if constexpr (std::isnan(num)) {
            result.buffer[0] = 'n';
            result.buffer[1] = 'a';
            result.buffer[2] = 'n';

        } else if constexpr (std::isinf(num)) {
            if constexpr (num < 0) {
                result.buffer[0] = '-';
                result.buffer[1] = 'i';
                result.buffer[2] = 'n';
                result.buffer[3] = 'f';
            } else {
                result.buffer[0] = 'i';
                result.buffer[1] = 'n';
                result.buffer[2] = 'f';
            }

        } else {
            // decompose into integral and (rounded) fractional parts
            constexpr long long integral = static_cast<long long>(num);
            constexpr long long fractional = [] {
                double exp = 1;
                for (size_t i = 0; i < precision; ++i) {
                    exp *= 10;
                }
                if constexpr (num > integral) {
                    return static_cast<long long>((num - integral) * exp + 0.5);
                } else {
                    return static_cast<long long>((integral - num) * exp + 0.5);
                }
            }();

            // convert to string (base 10)
            constexpr auto integral_str = from_int<integral, 10>;
            constexpr auto fractional_str = from_int<fractional, 10>;

            char* pos = result.buffer;
            if constexpr (integral == 0 && sign_bit(num)) {
                result.buffer[0] = '-';
                ++pos;
            }

            // concatenate integral and fractional parts (zero padded to precision)
            std::copy_n(
                integral_str.buffer,
                integral_str.size(),
                pos
            );
            if constexpr (precision > 0) {
                std::copy_n(".", 1, pos + integral_str.size());
                pos += integral_str.size() + 1;
                std::copy_n(
                    fractional_str.buffer,
                    fractional_str.size(),
                    pos
                );
                std::fill_n(
                    pos + fractional_str.size(),
                    precision - fractional_str.size(),
                    '0'
                );
            }
        }

        result.buffer[len] = '\0';
        return result;
    }();

    [[nodiscard]] constexpr operator const char*() const noexcept { return buffer; }
    [[nodiscard]] explicit constexpr operator bool() const noexcept { return N; }
    [[nodiscard]] explicit constexpr operator std::string() const { return {buffer, N}; }
    [[nodiscard]] explicit constexpr operator std::string_view() const noexcept {
        return {buffer, N};
    }

    [[nodiscard]] static constexpr size_t size() noexcept { return N; }
    [[nodiscard]] static constexpr ssize_t ssize() noexcept { return ssize_t(N); }
    [[nodiscard]] static constexpr bool empty() noexcept { return !N; }

    [[nodiscard]] constexpr const char* data() const noexcept { return buffer; }
    [[nodiscard]] constexpr iterator begin() const noexcept { return {buffer, 0}; }
    [[nodiscard]] constexpr iterator cbegin() const noexcept { return {buffer, 0}; }
    [[nodiscard]] constexpr iterator end() const noexcept { return {buffer, N}; }
    [[nodiscard]] constexpr iterator cend() const noexcept { return {buffer, N}; }
    [[nodiscard]] constexpr reverse_iterator rbegin() const noexcept {
        return std::make_reverse_iterator(end());
    }
    [[nodiscard]] constexpr reverse_iterator crbegin() const noexcept {
        return std::make_reverse_iterator(cend());
    }
    [[nodiscard]] constexpr reverse_iterator rend() const noexcept {
        return std::make_reverse_iterator(begin());
    }
    [[nodiscard]] constexpr reverse_iterator crend() const noexcept {
        return std::make_reverse_iterator(cbegin());
    }

    /* Access a character within the underlying buffer.  Applies Python-style
    wraparound for negative indices. */
    [[nodiscard]] constexpr const char operator[](ssize_t i) const {
        return buffer[impl::normalize_index(size(), i)];
    }

    /* Slice operator utilizing an initializer list.  Up to 3 (possibly negative)
    indices may be supplied according to Python semantics, with `std::nullopt` equating
    to `None`. */
    [[nodiscard]] constexpr slice operator[](bertrand::slice s) const {
        return {buffer, s.normalize(ssize())};
    }

    /* Equivalent to Python `str.capitalize()`. */
    template <bertrand::static_str self>
    [[nodiscard]] static consteval auto capitalize() noexcept {
        static_str<self.size()> result;
        bool capitalized = false;
        for (size_t i = 0; i < self.size(); ++i) {
            char c = self.buffer[i];
            if (impl::char_isalpha(c)) {
                if (capitalized) {
                    result.buffer[i] = impl::char_tolower(c);
                } else {
                    result.buffer[i] = impl::char_toupper(c);
                    capitalized = true;
                }
            } else {
                result.buffer[i] = c;
            }
        }
        result.buffer[self.size()] = '\0';
        return result;
    }

    /* Equivalent to Python `str.center(width[, fillchar])`. */
    template <bertrand::static_str self, size_t width, char fillchar = ' '>
    [[nodiscard]] static consteval auto center() noexcept {
        if constexpr (width <= self.size()) {
            return self;
        } else {
            static_str<width> result;
            size_t left = (width - self.size()) / 2;
            size_t right = width - self.size() - left;
            std::fill_n(result.buffer, left, fillchar);
            std::copy_n(self.buffer, self.size(), result.buffer + left);
            std::fill_n(
                result.buffer + left + self.size(),
                right,
                fillchar
            );
            result.buffer[width] = '\0';
            return result;
        }
    }

    /* Equivalent to Python `str.count(sub[, start[, stop]])`. */
    template <
        bertrand::static_str self,
        bertrand::static_str sub,
        ssize_t start = 0,
        ssize_t stop = self.size()
    >
    [[nodiscard]] static consteval size_t count() noexcept {
        ssize_t nstart = std::max(
            impl::truncate_index(size(), start),
            static_cast<ssize_t>(0)
        );
        ssize_t nstop = std::min(
            impl::truncate_index(size(), stop),
            static_cast<ssize_t>(self.size())
        );
        size_t count = 0;
        for (size_t i = nstart; i < nstop; ++i) {
            if (std::equal(sub.buffer, sub.buffer + sub.size(), self.buffer + i)) {
                ++count;
            }
        }
        return count;
    }

    /* Equivalent to Python `str.endswith(suffix)`. */
    template <bertrand::static_str self, bertrand::static_str suffix>
    [[nodiscard]] static consteval bool endswith() noexcept {
        return suffix.size() <= self.size() && std::equal(
            suffix.buffer,
            suffix.buffer + suffix.size(),
            self.buffer + self.size() - suffix.size()
        );
    }

    /* Equivalent to Python `str.expandtabs([tabsize])`. */
    template <bertrand::static_str self, size_t tabsize = 8>
    [[nodiscard]] static consteval auto expandtabs() noexcept {
        constexpr size_t n = count<self, "\t">();
        static_str<self.size() - n + n * tabsize> result;
        size_t offset = 0;
        for (size_t i = 0; i < self.size(); ++i) {
            char c = self.buffer[i];
            if (c == '\t') {
                std::fill_n(result.buffer + offset, tabsize, ' ');
                offset += tabsize;
            } else {
                result.buffer[offset++] = c;
            }
        }
        result.buffer[offset] = '\0';
        return result;
    }

    /* Equivalent to Python `str.find(sub[, start[, stop]])`.  Returns
    static_str<>::missing if the substring is not found. */
    template <
        bertrand::static_str self,
        bertrand::static_str sub,
        ssize_t start = 0,
        ssize_t stop = self.size()
    >
    [[nodiscard]] static consteval size_t find() noexcept {
        constexpr ssize_t nstart =
            std::max(
                impl::truncate_index(size(), start),
                static_cast<ssize_t>(0)
            );
        constexpr ssize_t nstop =
            std::min(
                impl::truncate_index(size(), stop),
                static_cast<ssize_t>(self.size())
            );
        for (ssize_t i = nstart; i < nstop; ++i) {
            if (std::equal(sub.buffer, sub.buffer + sub.size(), self.buffer + i)) {
                return i;
            }
        }
        return missing;
    }

    /* Equivalent to Python `str.index(sub[, start[, stop]])`.  Uses a template
    constraint to ensure that the substring is present. */
    template <
        bertrand::static_str self,
        bertrand::static_str sub,
        ssize_t start = 0,
        ssize_t stop = self.size()
    > requires (find<self, sub, start, stop>() != missing)
    [[nodiscard]] static consteval size_t index() noexcept {
        return find<self, sub, start, stop>();
    }

    /* Equivalent to Python `str.isalpha()`. */
    template <bertrand::static_str self>
    [[nodiscard]] static consteval bool isalpha() noexcept {
        for (size_t i = 0; i < self.size(); ++i) {
            if (!impl::char_isalpha(self.buffer[i])) {
                return false;
            }
        }
        return self.size() > 0;
    }

    /* Equivalent to Python `str.isalnum()`. */
    template <bertrand::static_str self>
    [[nodiscard]] static consteval bool isalnum() noexcept {
        for (size_t i = 0; i < self.size(); ++i) {
            if (!impl::char_isalnum(self.buffer[i])) {
                return false;
            }
        }
        return self.size() > 0;
    }

    /* Equivalent to Python `str.isascii_()`. */
    template <bertrand::static_str self>
    [[nodiscard]] static consteval bool isascii_() noexcept {
        for (size_t i = 0; i < self.size(); ++i) {
            if (!impl::char_isascii(self.buffer[i])) {
                return false;
            }
        }
        return self.size() > 0;
    }

    /* Equivalent to Python `str.isdigit()`. */
    template <bertrand::static_str self>
    [[nodiscard]] static consteval bool isdigit() noexcept {
        for (size_t i = 0; i < self.size(); ++i) {
            if (!impl::char_isdigit(self.buffer[i])) {
                return false;
            }
        }
        return self.size() > 0;
    }

    /* Equivalent to Python `str.islower()`. */
    template <bertrand::static_str self>
    [[nodiscard]] static consteval bool islower() noexcept {
        for (size_t i = 0; i < self.size(); ++i) {
            if (!impl::char_islower(self.buffer[i])) {
                return false;
            }
        }
        return self.size() > 0;
    }

    /* Equivalent to Python `str.isspace()`. */
    template <bertrand::static_str self>
    [[nodiscard]] static consteval bool isspace() noexcept {
        for (size_t i = 0; i < self.size(); ++i) {
            if (!impl::char_isspace(self.buffer[i])) {
                return false;
            }
        }
        return self.size() > 0;
    }

    /* Equivalent to Python `str.istitle()`. */
    template <bertrand::static_str self>
    [[nodiscard]] static consteval bool istitle() noexcept {
        bool last_was_delimeter = true;
        for (size_t i = 0; i < self.size(); ++i) {
            char c = self.buffer[i];
            if (last_was_delimeter && impl::char_islower(c)) {
                return false;
            }
            last_was_delimeter = impl::char_isdelimeter(c);
        }
        return self.size() > 0;
    }

    /* Equivalent to Python `str.isupper()`. */
    template <bertrand::static_str self>
    [[nodiscard]] static consteval bool isupper() noexcept {
        for (size_t i = 0; i < self.size(); ++i) {
            if (!impl::char_isupper(self.buffer[i])) {
                return false;
            }
        }
        return self.size() > 0;
    }

    /* Equivalent to Python `str.join(strings...)`. */
    template <
        bertrand::static_str self,
        bertrand::static_str first,
        bertrand::static_str... rest
    >
    [[nodiscard]] static consteval auto join() noexcept {
        static_str<first.size() + (0 + ... + (self.size() + rest.size()))> result;
        std::copy_n(first.buffer, first.size(), result.buffer);
        size_t offset = first.size();
        (
            (
                std::copy_n(
                    self.buffer,
                    self.size(),
                    result.buffer + offset
                ),
                offset += self.size(),
                std::copy_n(
                    rest.buffer,
                    rest.size(),
                    result.buffer + offset
                ),
                offset += rest.size()
            ),
            ...
        );
        result.buffer[result.size()] = '\0';
        return result;
    }

    /* Equivalent to Python `str.ljust(width[, fillchar])`. */
    template <bertrand::static_str self, size_t width, char fillchar = ' '>
    [[nodiscard]] static consteval auto ljust() noexcept {
        if constexpr (width <= self.size()) {
            return self;
        } else {
            static_str<width> result;
            std::copy_n(self.buffer, self.size(), result.buffer);
            std::fill_n(
                result.buffer + self.size(),
                width - self.size(),
                fillchar
            );
            result.buffer[width] = '\0';
            return result;
        }
    }

    /* Equivalent to Python `str.lower()`. */
    template <bertrand::static_str self>
    [[nodiscard]] static consteval auto lower() noexcept {
        static_str<self.size()> result;
        for (size_t i = 0; i < self.size(); ++i) {
            result.buffer[i] = impl::char_tolower(self.buffer[i]);
        }
        result.buffer[self.size()] = '\0';
        return result;
    }

    /* Equivalent to Python `str.lstrip([chars])`. */
    template <bertrand::static_str self, bertrand::static_str chars = " \t\n\r\f\v">
    [[nodiscard]] static consteval auto lstrip() noexcept {
        constexpr size_t start = first_non_stripped<self, chars>();
        if constexpr (start == missing) {
            return bertrand::static_str{""};
        } else {
            constexpr size_t delta = self.size() - start;
            static_str<delta> result;
            std::copy_n(self.buffer + start, delta, result.buffer);
            result.buffer[delta] = '\0';
            return result;
        }
    }

    /* Equivalent to Python `str.partition(sep)`. */
    template <bertrand::static_str self, bertrand::static_str sep>
    [[nodiscard]] static consteval auto partition() noexcept {
        constexpr size_t index = find<self, sep>();
        if constexpr (index == missing) {
            return std::make_tuple(
                self,
                bertrand::static_str{""},
                bertrand::static_str{""}
            );
        } else {
            constexpr size_t remaining = self.size() - index - sep.size();
            static_str<index> first;
            static_str<remaining> third;
            std::copy_n(self.buffer, index, first.buffer);
            std::copy_n(
                self.buffer + index + sep.size(),
                remaining,
                third.buffer
            );
            first.buffer[index] = '\0';
            third.buffer[remaining] = '\0';
            return std::make_tuple(first, sep, third);
        }
    }

    /* Equivalent to Python `str.replace()`. */
    template <
        bertrand::static_str self,
        bertrand::static_str sub,
        bertrand::static_str repl,
        size_t max_count = missing
    >
    [[nodiscard]] static consteval auto replace() noexcept {
        constexpr size_t freq = count<self, sub>();
        constexpr size_t n = freq < max_count ? freq : max_count;
        static_str<self.size() - (n * sub.size()) + (n * repl.size())> result;
        size_t offset = 0;
        size_t count = 0;
        for (size_t i = 0; i < self.size();) {
            if (
                count < max_count &&
                std::equal(sub.buffer, sub.buffer + sub.size(), self.buffer + i)
            ) {
                std::copy_n(repl.buffer, repl.size(), result.buffer + offset);
                offset += repl.size();
                i += sub.size();
                ++count;
            } else {
                result.buffer[offset++] = self.buffer[i++];
            }
        }
        result.buffer[result.size()] = '\0';
        return result;
    }

    /* Equivalent to Python `str.rfind(sub[, start[, stop]])`.  Returns
    static_str<>::missing if the substring is not found. */
    template <
        bertrand::static_str self,
        bertrand::static_str sub,
        ssize_t start = 0,
        ssize_t stop = self.size()
    >
    [[nodiscard]] static consteval size_t rfind() noexcept {
        constexpr ssize_t nstart =
            std::min(
                impl::truncate_index(size(), stop),
                static_cast<ssize_t>(self.size())
            ) - 1;
        constexpr ssize_t nstop =
            std::max(
                impl::truncate_index(size(), start),
                static_cast<ssize_t>(0)
            ) - 1;
        for (ssize_t i = nstart; i > nstop; --i) {
            if (std::equal(sub.buffer, sub.buffer + sub.size(), self.buffer + i)) {
                return i;
            }
        }
        return missing;
    }

    /* Equivalent to Python `str.rindex(sub[, start[, stop]])`.  Uses a template
    constraint to ensure that the substring is present. */
    template <
        bertrand::static_str self,
        bertrand::static_str sub,
        ssize_t start = 0,
        ssize_t stop = self.size()
    > requires (rfind<self, sub, start, stop>() != missing)
    [[nodiscard]] static consteval size_t rindex() noexcept {
        return rfind<self, sub, start, stop>();
    }

    /* Equivalent to Python `str.rjust(width[, fillchar])`. */
    template <bertrand::static_str self, size_t width, char fillchar = ' '>
    [[nodiscard]] static consteval auto rjust() noexcept {
        if constexpr (width <= self.size()) {
            return self;
        } else {
            static_str<width> result;
            std::fill_n(
                result.buffer,
                width - self.size(),
                fillchar
            );
            std::copy_n(
                self.buffer,
                self.size(),
                result.buffer + width - self.size()
            );
            result.buffer[width] = '\0';
            return result;
        }
    }

    /* Equivalent to Python `str.rpartition(sep)`. */
    template <bertrand::static_str self, bertrand::static_str sep>
    [[nodiscard]] static consteval auto rpartition() noexcept {
        constexpr size_t index = rfind<self, sep>();
        if constexpr (index == missing) {
            return std::make_tuple(
                self,
                bertrand::static_str{""},
                bertrand::static_str{""}
            );
        } else {
            constexpr size_t remaining = self.size() - index - sep.size();
            static_str<index> first;
            static_str<remaining> third;
            std::copy_n(self.buffer, index, first.buffer);
            std::copy_n(
                self.buffer + index + sep.size(),
                remaining,
                third.buffer
            );
            first.buffer[index] = '\0';
            third.buffer[remaining] = '\0';
            return std::make_tuple(first, sep, third);
        }
    }

    /* Equivalent to Python `str.rsplit(sep[, maxsplit])`. */
    template <
        bertrand::static_str self,
        bertrand::static_str sep,
        size_t maxsplit = missing
    > requires (sep.size() > 0)
    [[nodiscard]] static consteval auto rsplit() noexcept {
        constexpr size_t freq = count<self, sep>();
        if constexpr (freq == 0) {
            return std::make_tuple(self);
        } else {
            constexpr size_t n = (freq < maxsplit ? freq : maxsplit) + 1;
            return []<size_t... Is>(std::index_sequence<Is...>) {
                constexpr std::array<size_t, n> strides = [] {
                    std::array<size_t, n> result;
                    size_t prev = self.size();
                    for (size_t i = prev - 1, j = 0; j < n - 1; --i) {
                        if (std::equal(
                            sep.buffer,
                            sep.buffer + sep.size(),
                            self.buffer + i
                        )) {
                            result[j++] = prev - (i + sep.size());
                            prev = i;
                        }
                    }
                    result[n - 1] = prev;
                    return result;
                }();
                std::tuple<static_str<std::get<Is>(strides)>...> result;
                size_t offset = self.size();
                (
                    (
                        offset -= strides[Is],
                        std::copy_n(
                            self.buffer + offset,
                            strides[Is],
                            std::get<Is>(result).buffer
                        ),
                        std::get<Is>(result).buffer[strides[Is]] = '\0',
                        offset -= sep.size()
                    ),
                    ...
                );
                return result;
            }(std::make_index_sequence<n>{});
        }
    }

    /* Equivalent to Python `str.rstrip([chars])`. */
    template <bertrand::static_str self, bertrand::static_str chars = " \t\n\r\f\v">
    [[nodiscard]] static consteval auto rstrip() noexcept {
        constexpr size_t stop = last_non_stripped<self, chars>();
        if constexpr (stop == missing) {
            return bertrand::static_str{""};
        } else {
            constexpr size_t delta = stop + 1;
            static_str<delta> result;
            std::copy_n(self.buffer, delta, result.buffer);
            result.buffer[delta] = '\0';
            return result;
        }
    }

    /* Equivalent to Python `str.split(sep[, maxsplit])`. */
    template <
        bertrand::static_str self,
        bertrand::static_str sep,
        size_t maxsplit = missing
    > requires (sep.size() > 0)
    [[nodiscard]] static consteval auto split() noexcept {
        constexpr size_t freq = count<self, sep>();
        if constexpr (freq == 0) {
            return std::make_tuple(self);
        } else {
            constexpr size_t n = (freq < maxsplit ? freq : maxsplit) + 1;
            return []<size_t... Is>(std::index_sequence<Is...>) {
                constexpr std::array<size_t, sizeof...(Is)> strides = [] {
                    std::array<size_t, n> result;
                    size_t prev = 0;
                    for (size_t i = prev, j = 0; j < n - 1;) {
                        if (std::equal(
                            sep.buffer, sep.buffer + sep.size(),
                            self.buffer + i
                        )) {
                            result[j++] = i - prev;
                            i += sep.size();
                            prev = i;
                        } else {
                            ++i;
                        }
                    }
                    result[n - 1] = self.size() - prev;
                    return result;
                }();
                std::tuple<static_str<std::get<Is>(strides)>...> result;
                size_t offset = 0;
                (
                    (
                        std::copy_n(
                            self.buffer + offset,
                            strides[Is],
                            std::get<Is>(result).buffer
                        ),
                        std::get<Is>(result).buffer[strides[Is]] = '\0',
                        offset += strides[Is] + sep.size()
                    ),
                    ...
                );
                return result;
            }(std::make_index_sequence<n>{});
        }
    }

    /* Equivalent to Python `str.splitlines([keepends])`. */
    template <bertrand::static_str self, bool keepends = false>
    [[nodiscard]] static consteval auto splitlines() noexcept {
        constexpr size_t n = [] {
            if constexpr (self.size() == 0) {
                return 0;
            } else {
                size_t total = 1;
                for (size_t i = 0; i < self.size(); ++i) {
                    char c = self.buffer[i];
                    if (c == '\r') {
                        ++total;
                        if (self.buffer[i + 1] == '\n') {
                            ++i;  // skip newline character
                        }
                    } else if (impl::char_islinebreak(c)) {
                        ++total;
                    }
                }
                return total;
            }
        }();
        if constexpr (n == 0) {
            return std::make_tuple();
        } else {
            constexpr auto result = []<size_t... Is>(std::index_sequence<Is...>) {
                constexpr std::array<size_t, n> strides = [] {
                    std::array<size_t, n> result;
                    size_t prev = 0;
                    for (size_t i = prev, j = 0; j < n - 1;) {
                        char c = self.buffer[i];
                        if (c == '\r' && self.buffer[i + 1] == '\n') {
                            if constexpr (keepends) {
                                i += 2;
                                result[j++] = i - prev;
                            } else {
                                result[j++] = i - prev;
                                i += 2;
                            }
                            prev = i;
                        } else if (impl::char_islinebreak(c)) {
                            if constexpr (keepends) {
                                ++i;
                                result[j++] = i - prev;
                            } else {
                                result[j++] = i - prev;
                                ++i;
                            }
                            prev = i;
                        } else {
                            ++i;
                        }
                    }
                    result[n - 1] = self.size() - prev;
                    return result;
                }();
                std::tuple<static_str<std::get<Is>(strides)>...> result;
                size_t offset = 0;
                (
                    (
                        std::copy_n(
                            self.buffer + offset,
                            strides[Is],
                            std::get<Is>(result).buffer
                        ),
                        std::get<Is>(result).buffer[strides[Is]] = '\0',
                        offset += strides[Is],
                        offset += (
                            self.buffer[strides[Is]] == '\r' &&
                            self.buffer[strides[Is] + 1] == '\n'
                        ) ? 2 : 1
                    ),
                    ...
                );
                return result;
            }(std::make_index_sequence<n>{});
            if constexpr (std::get<n - 1>(result).size() == 0) {  // strip empty line
                return []<size_t... Is>(std::index_sequence<Is...>, auto&& result) {
                    return std::make_tuple(std::get<Is>(result)...);
                }(std::make_index_sequence<n - 1>{}, result);
            } else {
                return result;
            }
        }
    }

    /* Equivalent to Python `str.startswith(prefix)`, but evaluated statically at
    compile time. */
    template <bertrand::static_str self, bertrand::static_str prefix>
    [[nodiscard]] static consteval bool startswith() noexcept {
        return (
            prefix.size() <= self.size() &&
            std::equal(prefix.buffer, prefix.buffer + prefix.size(), self.buffer)
        );
    }

    /* Equivalent to Python `str.strip([chars])`, but evaluated statically at compile
    time. */
    template <bertrand::static_str self, bertrand::static_str chars = " \t\n\r\f\v">
    [[nodiscard]] static consteval auto strip() noexcept {
        constexpr size_t start = first_non_stripped<self, chars>();
        if constexpr (start == missing) {
            return bertrand::static_str{""};
        } else {
            constexpr size_t stop = last_non_stripped<self, chars>();
            constexpr size_t delta = stop - start + 1;  // +1 for half-open interval
            static_str<delta> result;
            std::copy_n(self.buffer + start, delta, result.buffer);
            result.buffer[delta] = '\0';
            return result;
        }
    }

    /* Equivalent to Python `str.swapcase()`, but evaluated statically at compile
    time. */
    template <bertrand::static_str self>
    [[nodiscard]] static consteval auto swapcase() noexcept {
        static_str<self.size()> result;
        for (size_t i = 0; i < self.size(); ++i) {
            char c = self.buffer[i];
            if (impl::char_islower(c)) {
                result.buffer[i] = impl::char_toupper(c);
            } else if (impl::char_isupper(c)) {
                result.buffer[i] = impl::char_tolower(c);
            } else {
                result.buffer[i] = c;
            }
        }
        result.buffer[self.size()] = '\0';
        return result;
    }

    /* Equivalent to Python `str.title()`, but evaluated statically at compile time. */
    template <bertrand::static_str self>
    [[nodiscard]] static consteval auto title() noexcept {
        static_str<self.size()> result;
        bool capitalize_next = true;
        for (size_t i = 0; i < self.size(); ++i) {
            char c = self.buffer[i];
            if (impl::char_isalpha(c)) {
                if (capitalize_next) {
                    result.buffer[i] = impl::char_toupper(c);
                    capitalize_next = false;
                } else {
                    result.buffer[i] = impl::char_tolower(c);
                }
            } else {
                if (impl::char_isdelimeter(c)) {
                    capitalize_next = true;
                }
                result.buffer[i] = c;
            }
        }
        result.buffer[self.size()] = '\0';
        return result;
    }

    /* Equivalent to Python `str.upper()`, but evaluated statically at compile time. */
    template <bertrand::static_str self>
    [[nodiscard]] static consteval auto upper() noexcept {
        static_str<self.size()> result;
        for (size_t i = 0; i < self.size(); ++i) {
            result.buffer[i] = impl::char_toupper(self.buffer[i]);
        }
        result.buffer[self.size()] = '\0';
        return result;
    }

    /* Equivalent to Python `str.zfill(width)`, but evaluated statically at compile
    time. */
    template <bertrand::static_str self, size_t width>
    [[nodiscard]] static consteval auto zfill() noexcept {
        if constexpr (width <= self.size()) {
            return self;
        } else if constexpr (self.empty()) {
            static_str<width> result;
            std::fill_n(result.buffer, width, '0');
            result.buffer[width] = '\0';
            return result;
        } else {
            static_str<width> result;
            size_t start = 0;
            if (self.buffer[0] == '+' || self.buffer[0] == '-') {
                result.buffer[0] = self.buffer[0];
                start = 1;
            }
            std::fill_n(
                result.buffer + start,
                width - self.size(),
                '0'
            );
            std::copy_n(
                self.buffer + start,
                self.buffer - start,
                result.buffer + width - self.size() + start
            );
            result.buffer[width] = '\0';
            return result;
        }
    }

    /* Equivalent to Python `str.removeprefix()`, but evaluated statically at compile
    time. */
    template <bertrand::static_str self, bertrand::static_str prefix>
    [[nodiscard]] static consteval auto removeprefix() noexcept {
        if constexpr (startswith<self, prefix>()) {
            static_str<self.size() - prefix.size()> result;
            std::copy_n(
                self.buffer + prefix.size(),
                self.size() - prefix.size(),
                result.buffer
            );
            result.buffer[self.size() - prefix.size()] = '\0';
            return result;
        } else {
            return self;
        }
    }

    /* Equivalent to Python `str.removesuffix()`, but evaluated statically at compile
    time. */
    template <bertrand::static_str self, bertrand::static_str suffix>
    [[nodiscard]] static consteval auto removesuffix() noexcept {
        if constexpr (endswith<self, suffix>()) {
            static_str<self.size() - suffix.size()> result;
            std::copy_n(
                self.buffer,
                self.size() - suffix.size(),
                result.buffer
            );
            result.buffer[self.size() - suffix.size()] = '\0';
            return result;
        } else {
            return self;
        }
    }

    template <size_t M>
    [[nodiscard]] friend consteval std::strong_ordering operator<=>(
        const static_str& self,
        const static_str<M>& other
    ) noexcept {
        return std::lexicographical_compare_three_way(
            self.buffer,
            self.buffer + N,
            other.buffer,
            other.buffer + M
        );
    }

    template <size_t M>
    [[nodiscard]] friend consteval std::strong_ordering operator<=>(
        const static_str& self,
        const char(&other)[M]
    ) noexcept {
        return std::lexicographical_compare_three_way(
            self.buffer,
            self.buffer + N,
            other,
            other + M - 1
        );
    }

    template <size_t M>
    [[nodiscard]] friend consteval std::strong_ordering operator<=>(
        const char(&other)[M],
        const static_str& self
    ) noexcept {
        return std::lexicographical_compare_three_way(
            other,
            other + M - 1,
            self.buffer,
            self.buffer + N
        );
    }

    template <size_t M>
    [[nodiscard]] friend consteval bool operator==(
        const static_str& self,
        const static_str<M>& other
    ) noexcept {
        if constexpr (N == M) {
            return std::lexicographical_compare_three_way(
                self.buffer,
                self.buffer + N,
                other.buffer,
                other.buffer + M
            ) == 0;
        } else {
            return false;
        }
    }

    template <size_t M>
    [[nodiscard]] friend consteval bool operator==(
        const static_str& self,
        const char(&other)[M]
    ) noexcept {
        if constexpr (N == (M - 1)) {
            return std::lexicographical_compare_three_way(
                self.buffer,
                self.buffer + N,
                other,
                other + N
            ) == 0;
        } else {
            return false;
        }
    }

    template <size_t M>
    [[nodiscard]] friend consteval bool operator==(
        const char(&other)[M],
        const static_str& self
    ) noexcept {
        if constexpr (N == (M - 1)) {
            return std::lexicographical_compare_three_way(
                other,
                other + N,
                self.buffer,
                self.buffer + N
            ) == 0;
        } else {
            return false;
        }
    }

    template <size_t M>
    [[nodiscard]] friend consteval static_str<N + M> operator+(
        const static_str<N>& self,
        const static_str<M>& other
    ) noexcept {
        static_str<N + M> result;
        std::copy_n(self.buffer, self.size(), result.buffer);
        std::copy_n(other.buffer, other.size(), result.buffer + self.size());
        result.buffer[N + M] = '\0';
        return result;
    }

    template <size_t M>
    [[nodiscard]] friend consteval static_str<N + M - 1> operator+(
        const static_str<N>& self,
        const char(&other)[M]
    ) noexcept {
        static_str<N + M - 1> result;
        std::copy_n(self.buffer, self.size(), result.buffer);
        std::copy_n(other, M - 1, result.buffer + self.size());
        result.buffer[N + M - 1] = '\0';
        return result;
    }

    template <size_t M>
    [[nodiscard]] friend consteval static_str<N + M - 1> operator+(
        const char(&other)[M],
        const static_str<N>& self
    ) noexcept {
        static_str<N + M - 1> result;
        std::copy_n(other, M - 1, result.buffer);
        std::copy_n(self.buffer, self.size(), result.buffer + M - 1);
        result.buffer[N + M - 1] = '\0';
        return result;
    }

    template <meta::convertible_to<std::string> T>
        requires (!meta::string_literal<T> && !meta::static_str<T>)
    [[nodiscard]] friend constexpr std::string operator+(
        const static_str& self,
        T&& other
    ) {
        return std::string(self) + std::forward<T>(other);
    }

    template <meta::convertible_to<std::string> T>
        requires (!meta::string_literal<T> && !meta::static_str<T>)
    [[nodiscard]] friend constexpr std::string operator+(
        T&& other,
        const static_str& self
    ) {
        return std::forward<T>(other) + std::string(self);
    }

    /// NOTE: due to language limitations, the * operator cannot return another
    /// static_str instance, so it returns a std::string instead.  This is not ideal,
    /// but there is currently no way to inform the compiler that the other operand
    /// must be a compile-time constant, and can therefore be used to determine the
    /// size of the resulting string.

    [[nodiscard]] friend constexpr std::string operator*(
        const static_str& self,
        size_t reps
    ) noexcept {
        if (reps <= 0) {
            return {};
        } else {
            std::string result(self.size() * reps, '\0');
            for (size_t i = 0; i < reps; ++i) {
                std::copy_n(self.buffer, self.size(), result.data() + (self.size() * i));
            }
            return result;
        }
    }

    [[nodiscard]] friend constexpr std::string operator*(
        size_t reps,
        const static_str& self
    ) noexcept {
        if (reps <= 0) {
            return {};
        } else {
            std::string result(self.size() * reps, '\0');
            for (size_t i = 0; i < reps; ++i) {
                std::copy_n(self.buffer, self.size(), result.data() + (self.size() * i));
            }
            return result;
        }
    }
};


/* Gets a C++ type name as a fully-qualified, demangled string computed entirely
at compile time.  The underlying buffer is baked directly into the final binary. */
template <typename T>
constexpr auto type_name = impl::type_name_impl<T>();


/* Demangle a runtime string using the compiler's intrinsics. */
constexpr std::string demangle(const char* name) {
    #if defined(__GNUC__) || defined(__clang__)
        int status = 0;
        std::unique_ptr<char, void(*)(void*)> res {
            abi::__cxa_demangle(
                name,
                nullptr,
                nullptr,
                &status
            ),
            std::free
        };
        return (status == 0) ? res.get() : name;
    #elif defined(_MSC_VER)
        char undecorated_name[1024];
        if (UnDecorateSymbolName(
            name,
            undecorated_name,
            sizeof(undecorated_name),
            UNDNAME_COMPLETE
        )) {
            return std::string(undecorated_name);
        } else {
            return name;
        }
    #else
        return name; // fallback: no demangling
    #endif
}


/* Get a simple string representation of an arbitrary object.  This function is
functionally equivalent to Python's `repr()` function, but extended to work for
arbitrary C++ types.  It is guaranteed not to fail, and will attempt the following,
in order of precedence:

    1.  A `std::format()` call using a registered `std::formatter<>` specialization.
    2.  An explicit conversion to `std::string`.
    3.  A member `.to_string()` function on the object's type.
    4.  An ADL `to_string()` function in the same namespace as the object.
    5.  A `std::to_string()` call.
    6.  A stream insertion operator (`<<`).
    7.  A generic identifier based on the demangled type name and memory address.
*/
template <typename Self>
[[nodiscard]] constexpr std::string repr(Self&& obj) {
    if constexpr (std::formattable<Self, char>) {
        return std::format("{}", std::forward<Self>(obj));

    } else if constexpr (meta::explicitly_convertible_to<Self, std::string>) {
        return static_cast<std::string>(obj);

    } else if constexpr (meta::has_member_to_string<Self>) {
        return std::forward<Self>(obj).to_string();

    } else if constexpr (meta::has_adl_to_string<Self>) {
        return to_string(std::forward<Self>(obj));

    } else if constexpr (meta::has_std_to_string<Self>) {
        return std::to_string(std::forward<Self>(obj));

    } else if constexpr (meta::has_stream_insertion<Self>) {
        std::ostringstream stream;
        stream << std::forward<Self>(obj);
        return stream.str();

    } else {
        return "<" + type_name<Self> + " at " + std::to_string(
            reinterpret_cast<size_t>(&obj)
        ) + ">";
    }
}


namespace impl {

    template <typename out, typename... Args>
    struct _format_repr;
    template <typename... out, typename... Args>
    struct _format_repr<std::tuple<out...>, Args...> {
        using type = std::format_string<out...>;
    };
    template <typename... out, typename A, typename... Args>
    struct _format_repr<std::tuple<out...>, A, Args...> {
        template <typename T>
        struct to_repr { using type = std::string; };
        template <std::formattable<char> T>
        struct to_repr<T> { using type = T; };
        using type = _format_repr<
            std::tuple<out..., typename to_repr<A>::type>,
            Args...
        >::type;
    };

    template <typename... Args>
    using format_repr = _format_repr<std::tuple<>, Args...>::type;

    template <typename A>
    constexpr decltype(auto) to_format_repr(A&& arg) {
        if constexpr (std::formattable<decltype(arg), char>) {
            return std::forward<decltype(arg)>(arg);
        } else {
            return bertrand::repr(std::forward<decltype(arg)>(arg));
        }
    }

}


/* Print a format string to an output buffer.  Does not append a newline character. */
template <typename... Args>
constexpr void print(impl::format_repr<Args...> fmt, Args&&... args) {
    std::print(fmt, impl::to_format_repr(std::forward<Args>(args))...);
}


/* Print an arbitrary value to an output buffer by calling `repr()` on it. Does not
append a newline character. */
template <typename T>
constexpr void print(T&& obj) {
    std::cout << repr(std::forward<T>(obj));
}


/* Print a format string to an output buffer.  Appends a newline character to the
output. */
template <typename... Args>
constexpr void println(impl::format_repr<Args...> fmt, Args&&... args) {
    std::println(fmt, impl::to_format_repr(std::forward<Args>(args))...);
}


/* Print an arbitrary value to an output buffer by calling `repr()` on it.  Appends a
newline character to the output. */
template <typename T>
constexpr void println(T&& obj) {
    std::cout << repr(std::forward<T>(obj)) << "\n";
}


}  // namespace bertrand


namespace std {

    template <bertrand::meta::static_str T>
    struct hash<T> {
        consteval static size_t operator()(const T& str) {
            return bertrand::impl::fnv1a{}(
                str,
                bertrand::impl::fnv1a::seed,
                bertrand::impl::fnv1a::prime
            );
        }
    };

}


#endif  // BERTRAND_STATIC_STRING_H
