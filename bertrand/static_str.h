#ifndef BERTRAND_STATIC_STRING_H
#define BERTRAND_STATIC_STRING_H

#include "bertrand/common.h"
#include "bertrand/math.h"
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


// avoid conflict with deprecated isascii() from <ctype.h>
#undef isascii


namespace bertrand {


/* A compile-time string literal type that can be used as a deduced non-type template
parameter.

This class allows string literals to be encoded directly into C++'s type system,
consistent with other consteval language constructs.  That means they can be used to
specialize templates and trigger arbitrarily complex metafunctions at compile time,
without any impact on the final binary.  Such metafunctions are used internally to
implement zero-cost keyword arguments for C++ functions, full static type safety for
Python attributes, and minimal perfect hash tables for arbitrary data.

Users can leverage these strings for their own metaprogramming needs as well; the class
implements the full Python string interface as consteval member methods, and even
supports regular expressions through the CTRE library.  This can serve as a powerful
base for user-defined metaprogramming facilities, up to and including full
Domain-Specific Languages (DSLs) that can be parsed at compile time and subjected to
exhaustive static analysis. */
template <size_t N>
struct static_str;


/* A wrapper class for a template string that exposes a Python-style string interface,
allowing for straightforward compile-time manipulation.

Due to limitations around the use of `this` in compile-time contexts, C++ does not
allow the string interface to be defined directly on the `static_str` class itself.
Instead, a separate helper class is needed to inform the compiler that the `self`
parameter is strictly known at compile time, and that methods can be evaluated against
its contents accordingly.  Otherwise, ISO C++ dictates that the buffer in `self` is
not a core constant expression, and disallows any compile-time operations on it,
despite it possibly being a template parameter or marked as constexpr/constinit.
Future standards may relax this restriction, in which case this class may be
deprecated, and the methods moved directly to `static_str`.  For the foreseeable
future, however, a helper of this form is the only way to provide a straightforward
Python interface at compile time. */
template <static_str self>
struct string_wrapper;


/* A list-like container for a sequence of template strings.

Containers of this form act like standard tuples, except that they can only contain
strings, and encode those strings directly as template parameters.  This allows the
strings to be easily recovered and deduced through template specializations, making it
easier to write metaprogramming code that operates on them.  Lists of this form are
always returned by methods that yield sequences of strings, (such as `split()` or
`partition()`), and expose many of the same methods as scalar strings, following the
composite pattern. */
template <static_str... Strings>
struct string_list;


/* CTAD guide allows string literals of any bound to be used as initializer to
`static_str`, and therefore also as template parameters. */
template <size_t N>
static_str(const char(&)[N]) -> static_str<N - 1>;


/* CTAD guide allows string wrappers to be implicitly converted to `static_str` without
user intervention. */
template <auto S>
static_str(string_wrapper<S>) -> static_str<S.size()>;


/* A user-defined literal operator that converts a string literal directly to a
`string_wrapper` object, which naturally decays to `static_str` and has a full Python
string interface. */
template <static_str self>
consteval string_wrapper<self> operator""_str() noexcept { return {}; }


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


template <size_t N>
struct static_str : impl::static_str_tag {
    using value_type = const char;
    using reference = value_type&;
    using const_reference = reference;
    using pointer = value_type*;
    using const_pointer = pointer;
    using size_type = size_t;
    using index_type = ssize_t;
    using difference_type = std::ptrdiff_t;
    using iterator = impl::contiguous_iterator<value_type>;
    using const_iterator = iterator;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;
    using slice = impl::contiguous_slice<value_type>;
    using const_slice = slice;

    char buffer[N + 1];  // +1 for null terminator

    consteval static_str(const char* arr) noexcept {
        std::copy_n(arr, N, buffer);
        buffer[N] = '\0';
    }

private:
    consteval static_str() = default;

    template <size_t M>
    friend struct bertrand::static_str;
    template <bertrand::static_str self>
    friend struct bertrand::string_wrapper;

    template <long long num, size_type base>
    static constexpr size_type _int_length = [] {
        if constexpr (num == 0) {
            return 0;
        } else {
            return _int_length<num / base, base> + 1;
        }
    }();

    template <long long num, size_type base>
    static constexpr size_type int_length = [] {
        // length is always at least 1 to correct for num == 0
        if constexpr (num < 0) {
            return std::max(_int_length<-num, base>, 1UL) + 1;  // include negative sign
        } else {
            return std::max(_int_length<num, base>, 1UL);
        }
    }();

    template <double num, size_type precision>
    static constexpr size_type float_length = [] {
        if constexpr (std::isnan(num)) {
            return 3;  // "nan"
        } else if constexpr (std::isinf(num)) {
            return 3 + impl::sign_bit(num);  // "inf" or "-inf"
        } else {
            // negative zero integral part needs a leading minus sign
            return
                int_length<static_cast<long long>(num), 10> +
                (static_cast<long long>(num) == 0 && impl::sign_bit(num)) +
                (precision > 0) +
                precision;
        }
    }();

    template <typename T>
    static constexpr bool is_format_string = false;
    template <typename... Args>
    static constexpr bool is_format_string<std::basic_format_string<char, Args...>> = true;

public:

    /* Convert an integer into a string at compile time using the specified base. */
    template <long long num, size_type base = 10> requires (base >= 2 && base <= 36)
    static constexpr auto from_int = [] {
        constexpr const char chars[] = "0123456789abcdefghijklmnopqrstuvwxyz";
        constexpr size_type len = int_length<num, base>;
        static_str<len> result;

        long long temp = num;
        size_type idx = len - 1;
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
    template <double num, size_type precision = 6>
    static constexpr auto from_float = [] {
        constexpr size_type len = float_length<num, precision>;
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
                for (size_type i = 0; i < precision; ++i) {
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
            if constexpr (integral == 0 && impl::sign_bit(num)) {
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

    /* Implicitly convert a static string to any other standard string type. */
    template <typename T>
        requires (!is_format_string<T> && (
            meta::convertible_to<std::string_view, T> ||
            meta::convertible_to<std::string, T> ||
            meta::convertible_to<const char(&)[N + 1], T>
        ))
    [[nodiscard]] constexpr operator T() const noexcept {
        if constexpr (meta::convertible_to<std::string_view, T>) {
            return std::string_view{buffer, size()};
        } else if constexpr (meta::convertible_to<std::string, T>) {
            return std::string{buffer, size()};
        } else {
            return buffer;
        }
    }

    [[nodiscard]] consteval const char* data() const noexcept { return buffer; }
    [[nodiscard]] static consteval size_type size() noexcept { return N; }
    [[nodiscard]] static consteval index_type ssize() noexcept { return index_type(size()); }
    [[nodiscard]] static consteval bool empty() noexcept { return !size(); }
    [[nodiscard]] explicit consteval operator bool() const noexcept { return size(); }
    [[nodiscard]] constexpr iterator begin() const noexcept { return {buffer}; }
    [[nodiscard]] constexpr iterator cbegin() const noexcept { return {buffer}; }
    [[nodiscard]] constexpr iterator end() const noexcept { return {buffer + size()}; }
    [[nodiscard]] constexpr iterator cend() const noexcept { return {buffer + size()}; }
    [[nodiscard]] constexpr reverse_iterator rbegin() const noexcept { return {end()}; }
    [[nodiscard]] constexpr reverse_iterator crbegin() const noexcept { return {cend()}; }
    [[nodiscard]] constexpr reverse_iterator rend() const noexcept { return {begin()}; }
    [[nodiscard]] constexpr reverse_iterator crend() const noexcept { return {cbegin()}; }

    /* Get the character at index `I`, where `I` is known at compile time.  Applies
    Python-style wraparound for negative indices, and fails to compile if the index is
    out of bounds after normalization. */
    template <index_type I> requires (impl::valid_index<size(), I>())
    [[nodiscard]] consteval char get() const noexcept {
        return buffer[impl::normalize_index<size(), I>()];
    }

    /* Get a slice from the string at compile time.  Takes an explicitly-initialized
    `bertrand::slice` pack describing the start, stop, and step indices.  Each index
    can be omitted by initializing it to std::nullopt, which is equivalent to an empty
    slice index in Python.  Applies Python-style wraparound to both `start` and
    `stop`. */
    template <bertrand::slice s>
    [[nodiscard]] consteval auto get() const noexcept {
        constexpr auto indices = s.normalize(ssize());
        static_str<indices.length> result;
        for (index_type i = 0; i < indices.length; ++i) {
            result.buffer[i] = buffer[indices.start + i * indices.step];
        }
        result.buffer[indices.length] = '\0';
        return result;
    }

    /* Access a character within the underlying buffer.  Applies Python-style
    wraparound for negative indices, and throws an `IndexError` if the index is out of
    bounds after normalization. */
    [[nodiscard]] constexpr iterator operator[](index_type i) const noexcept {
        i += ssize() * (i < 0);
        if (i < 0 || i >= ssize()) {
            return end();
        }
        return {buffer + i};
    }

    /* Slice operator.  Takes an explicitly-initialized `bertrand::slice` pack
    describing the start, stop, and step indices.  Each index can be omitted by
    initializing it to `std::nullopt`, which is equivalent to an empty slice index in
    Python.  Applies Python-style wraparound to both `start` and `stop`. */
    [[nodiscard]] constexpr slice operator[](bertrand::slice s) const noexcept {
        return {buffer, s.normalize(ssize())};
    }

    /* Concatenate two static strings at compile time. */
    template <auto M>
    [[nodiscard]] consteval auto operator+(const static_str<M>& other) const noexcept {
        static_str<N + M> result;
        std::copy_n(buffer, N, result.buffer);
        std::copy_n(other.buffer, other.size(), result.buffer + N);
        result.buffer[N + M] = '\0';
        return result;
    }

    /* Concatenate two static strings at compile time. */
    template <auto S>
    [[nodiscard]] consteval auto operator+(string_wrapper<S>) const noexcept {
        return operator+(S);
    }

    /* Concatenate a static string with a string literal at compile time. */
    template <auto M>
    [[nodiscard]] friend constexpr auto operator+(
        const static_str<N>& self,
        const char(&other)[M]
    ) noexcept {
        static_str<N + M - 1> result;
        std::copy_n(self.buffer, self.size(), result.buffer);
        std::copy_n(other, M - 1, result.buffer + self.size());
        result.buffer[N + M - 1] = '\0';
        return result;
    }

    /* Concatenate a static string with a string literal at compile time. */
    template <auto M>
    [[nodiscard]] friend constexpr auto operator+(
        const char(&other)[M],
        const static_str<N>& self
    ) noexcept {
        static_str<N + M - 1> result;
        std::copy_n(other, M - 1, result.buffer);
        std::copy_n(self.buffer, self.size(), result.buffer + M - 1);
        result.buffer[N + M - 1] = '\0';
        return result;
    }

    /* Concatenate a static string with a runtime string. */
    template <meta::convertible_to<std::string> T>
        requires (!meta::string_literal<T> && !meta::static_str<T>)
    [[nodiscard]] friend constexpr std::string operator+(
        const static_str& self,
        T&& other
    ) {
        return std::string(self) + std::string(std::forward<T>(other));
    }

    /* Concatenate a static string with a runtime string. */
    template <meta::convertible_to<std::string> T>
        requires (!meta::string_literal<T> && !meta::static_str<T>)
    [[nodiscard]] friend constexpr std::string operator+(
        T&& other,
        const static_str& self
    ) {
        return std::string(std::forward<T>(other)) + std::string(self);
    }

    /// NOTE: due to language limitations, the * operator cannot return another
    /// static_str instance, so it returns a std::string instead.  This is not ideal,
    /// but there is currently no way to inform the compiler that the other operand
    /// must be a compile-time constant, and can therefore be used to determine the
    /// size of the resulting string.  The `repeat<self, reps>()` method is a
    /// workaround that encodes the repetitions as a template parameter.

    /* Repeat a string a given number of times at runtime. */
    [[nodiscard]] friend constexpr std::string operator*(
        const static_str& self,
        size_type reps
    ) noexcept {
        if (reps <= 0) {
            return {};
        } else {
            std::string result(self.size() * reps, '\0');
            for (size_type i = 0; i < reps; ++i) {
                std::copy_n(self.buffer, self.size(), result.data() + (self.size() * i));
            }
            return result;
        }
    }

    /* Repeat a string a given number of times at runtime. */
    [[nodiscard]] friend constexpr std::string operator*(
        size_type reps,
        const static_str& self
    ) noexcept {
        if (reps <= 0) {
            return {};
        } else {
            std::string result(self.size() * reps, '\0');
            for (size_type i = 0; i < reps; ++i) {
                std::copy_n(self.buffer, self.size(), result.data() + (self.size() * i));
            }
            return result;
        }
    }

    /* Lexicographically compare two static strings at compile time. */
    template <auto M>
    [[nodiscard]] consteval auto operator<=>(const static_str<M>& other) const noexcept {
        return std::lexicographical_compare_three_way(
            buffer,
            buffer + N,
            other.buffer,
            other.buffer + M
        );
    }

    /* Lexicographically compare a static string to a string methods wrapper. */
    template <auto S>
    [[nodiscard]] consteval auto operator<=>(string_wrapper<S>) const noexcept {
        return operator<=>(S);
    }

    /* Lexicographically compare a static string against a string literal. */
    template <auto M>
    [[nodiscard]] friend constexpr auto operator<=>(
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

    /* Lexicographically compare a static string against a string literal. */
    template <auto M>
    [[nodiscard]] friend constexpr auto operator<=>(
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

    /* Lexicographically compare a static string against a string view. */
    [[nodiscard]] friend constexpr auto operator<=>(
        const static_str& self,
        std::string_view other
    ) noexcept(noexcept(std::string_view(self) <=> other)) {
        return std::string_view(self) <=> other;
    }

    /* Lexicographically compare a static string against a string view. */
    [[nodiscard]] friend constexpr auto operator<=>(
        std::string_view other,
        const static_str& self
    ) noexcept(noexcept(other <=> std::string_view(self))) {
        return other <=> std::string_view(self);
    }

    /* Check for lexicographic equality between two static strings. */
    template <auto M>
    [[nodiscard]] consteval bool operator==(const static_str<M>& other) const noexcept {
        if constexpr (N == M) {
            return std::lexicographical_compare_three_way(
                buffer,
                buffer + N,
                other.buffer,
                other.buffer + M
            ) == 0;
        } else {
            return false;
        }
    }

    /* Check for lexicographic equality between two static strings. */
    template <auto S>
    [[nodiscard]] consteval bool operator==(string_wrapper<S>) const noexcept {
        return operator==(S);
    }

    /* Check for lexicographic equality between a static string and a string literal. */
    template <auto M>
    [[nodiscard]] friend constexpr bool operator==(
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

    /* Check for lexicographic equality between a static string and a string literal. */
    template <auto M>
    [[nodiscard]] friend constexpr bool operator==(
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

    /* Check for lexicographic equality between a static string and a string view. */
    [[nodiscard]] friend constexpr bool operator==(
        const static_str& self,
        std::string_view other
    ) noexcept(noexcept(std::string_view(self) == other)) {
        return std::string_view(self) == other;
    }

    /* Check for lexicographic equality between a static string and a string view. */
    [[nodiscard]] friend constexpr bool operator==(
        std::string_view other,
        const static_str& self
    ) noexcept(noexcept(other == std::string_view(self))) {
        return other == std::string_view(self);
    }

private:

    /* Repeat a string a given number of times at compile time. */
    template <bertrand::static_str self, size_type reps>
    [[nodiscard]] static consteval auto repeat() noexcept {
        static_str<self.size() * reps> result;
        for (size_type i = 0; i < reps; ++i) {
            std::copy_n(self.buffer, self.size(), result.buffer + (self.size() * i));
        }
        result.buffer[self.size() * reps] = '\0';
        return result;
    }

    template <bertrand::static_str self, bertrand::static_str chars>
    static consteval index_type first_non_stripped() noexcept {
        for (index_type i = 0; i < self.ssize(); ++i) {
            char c = self.buffer[i];
            index_type j = 0;
            while (j < chars.ssize()) {
                if (chars.buffer[j++] == c) {
                    break;
                } else if (j == chars.ssize()) {
                    return i;
                }
            }
        }
        return -1;
    }

    template <bertrand::static_str self, bertrand::static_str chars>
    static consteval index_type last_non_stripped() noexcept {
        for (index_type i = 0; i < self.ssize(); ++i) {
            index_type idx = self.ssize() - i - 1;
            char c = self.buffer[idx];
            index_type j = 0;
            while (j < chars.ssize()) {
                if (chars.buffer[j++] == c) {
                    break;
                } else if (j == chars.ssize()) {
                    return idx;
                }
            }
        }
        return -1;
    }

    /// NOTE: Because it is impossible to reference data from `this` within a
    /// consteval context, all of these algorithms are implemented by taking the
    /// `self` string as a template parameter.  They are therefore private, so as not
    /// to confuse users who might expect them to be callable like Python methods.
    /// The `string_wrapper` class is used to abstract this distinction, yielding a
    /// naturally Pythonic interface.  If this restriction is lifted in a future
    /// version of the language, then these can be converted into normal instance
    /// methods, and the `string_wrapper` class can be removed.

    /* Equivalent to Python `str.capitalize()`. */
    template <bertrand::static_str self>
    [[nodiscard]] static consteval auto capitalize() noexcept {
        static_str<self.size()> result;
        bool capitalized = false;
        for (size_type i = 0; i < self.size(); ++i) {
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
    template <bertrand::static_str self, size_type width, char fillchar = ' '>
    [[nodiscard]] static consteval auto center() noexcept {
        if constexpr (width <= self.size()) {
            return self;
        } else {
            static_str<width> result;
            size_type left = (width - self.size()) / 2;
            size_type right = width - self.size() - left;
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
        index_type start = 0,
        index_type stop = self.size()
    >
    [[nodiscard]] static consteval size_type count() noexcept {
        index_type nstart = impl::truncate_index(self.size(), start);
        index_type nstop = impl::truncate_index(self.size(), stop);
        size_type count = 0;
        for (index_type i = nstart; i < nstop; ++i) {
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
    template <bertrand::static_str self, size_type tabsize = 8>
    [[nodiscard]] static consteval auto expandtabs() noexcept {
        constexpr size_type n = count<self, "\t">();
        static_str<self.size() - n + n * tabsize> result;
        size_type offset = 0;
        for (size_type i = 0; i < self.size(); ++i) {
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

    /* Equivalent to Python `str.find(sub[, start[, stop]])`.  Returns -1 if the
    substring is not found. */
    template <
        bertrand::static_str self,
        bertrand::static_str sub,
        index_type start = 0,
        index_type stop = self.size()
    >
    [[nodiscard]] static consteval index_type find() noexcept {
        constexpr index_type nstart = impl::truncate_index(self.size(), start);
        constexpr index_type nstop = impl::truncate_index(self.size(), stop);
        for (index_type i = nstart; i < nstop; ++i) {
            if (std::equal(sub.buffer, sub.buffer + sub.size(), self.buffer + i)) {
                return i;
            }
        }
        return -1;
    }

    /* Equivalent to Python `str.isalpha()`. */
    template <bertrand::static_str self>
    [[nodiscard]] static consteval bool isalpha() noexcept {
        for (size_type i = 0; i < self.size(); ++i) {
            if (!impl::char_isalpha(self.buffer[i])) {
                return false;
            }
        }
        return self.size() > 0;
    }

    /* Equivalent to Python `str.isalnum()`. */
    template <bertrand::static_str self>
    [[nodiscard]] static consteval bool isalnum() noexcept {
        for (size_type i = 0; i < self.size(); ++i) {
            if (!impl::char_isalnum(self.buffer[i])) {
                return false;
            }
        }
        return self.size() > 0;
    }

    /* Equivalent to Python `str.isascii()`. */
    template <bertrand::static_str self>
    [[nodiscard]] static consteval bool isascii() noexcept {
        for (size_type i = 0; i < self.size(); ++i) {
            if (!impl::char_isascii(self.buffer[i])) {
                return false;
            }
        }
        return self.size() > 0;
    }

    /* Equivalent to Python `str.isdigit()`. */
    template <bertrand::static_str self>
    [[nodiscard]] static consteval bool isdigit() noexcept {
        for (size_type i = 0; i < self.size(); ++i) {
            if (!impl::char_isdigit(self.buffer[i])) {
                return false;
            }
        }
        return self.size() > 0;
    }

    /* Equivalent to Python `str.islower()`. */
    template <bertrand::static_str self>
    [[nodiscard]] static consteval bool islower() noexcept {
        for (size_type i = 0; i < self.size(); ++i) {
            if (!impl::char_islower(self.buffer[i])) {
                return false;
            }
        }
        return self.size() > 0;
    }

    /* Equivalent to Python `str.isspace()`. */
    template <bertrand::static_str self>
    [[nodiscard]] static consteval bool isspace() noexcept {
        for (size_type i = 0; i < self.size(); ++i) {
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
        for (size_type i = 0; i < self.size(); ++i) {
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
        for (size_type i = 0; i < self.size(); ++i) {
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
        size_type offset = first.size();
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
    template <bertrand::static_str self, size_type width, char fillchar = ' '>
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
        for (size_type i = 0; i < self.size(); ++i) {
            result.buffer[i] = impl::char_tolower(self.buffer[i]);
        }
        result.buffer[self.size()] = '\0';
        return result;
    }

    /* Equivalent to Python `str.lstrip([chars])`. */
    template <bertrand::static_str self, bertrand::static_str chars = " \t\n\r\f\v">
    [[nodiscard]] static consteval auto lstrip() noexcept {
        constexpr index_type start = first_non_stripped<self, chars>();
        if constexpr (start < 0) {
            return bertrand::static_str{""};
        } else {
            constexpr size_type delta = self.size() - size_type(start);
            static_str<delta> result;
            std::copy_n(self.buffer + start, delta, result.buffer);
            result.buffer[delta] = '\0';
            return result;
        }
    }

    /* Equivalent to Python `str.partition(sep)`. */
    template <bertrand::static_str self, bertrand::static_str sep>
    [[nodiscard]] static consteval auto partition() noexcept {
        constexpr size_type index = find<self, sep>();
        if constexpr (index < 0) {
            return string_list<self, "", "">{};
        } else {
            constexpr size_type offset = index + sep.size();
            constexpr size_type remaining = self.size() - offset;
            return string_list<
                static_str<index>{self.buffer},
                sep,
                static_str<remaining>{self.buffer + offset}
            >{};
        }
    }

    /* Equivalent to Python `str.replace()`. */
    template <
        bertrand::static_str self,
        bertrand::static_str sub,
        bertrand::static_str repl,
        size_type max_count = std::numeric_limits<size_type>::max()
    >
    [[nodiscard]] static consteval auto replace() noexcept {
        constexpr size_type freq = count<self, sub>();
        constexpr size_type n = freq < max_count ? freq : max_count;
        static_str<self.size() - (n * sub.size()) + (n * repl.size())> result;
        size_type offset = 0;
        size_type count = 0;
        for (size_type i = 0; i < self.size();) {
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

    /* Equivalent to Python `str.rfind(sub[, start[, stop]])`.  Returns -1 if the
    substring is not found. */
    template <
        bertrand::static_str self,
        bertrand::static_str sub,
        index_type start = 0,
        index_type stop = self.size()
    >
    [[nodiscard]] static consteval index_type rfind() noexcept {
        constexpr index_type nstart = impl::truncate_index(self.size(), stop) - 1;
        constexpr index_type nstop = impl::truncate_index(self.size(), start) - 1;
        for (index_type i = nstart; i > nstop; --i) {
            if (std::equal(sub.buffer, sub.buffer + sub.size(), self.buffer + i)) {
                return i;
            }
        }
        return -1;
    }

    /* Equivalent to Python `str.rjust(width[, fillchar])`. */
    template <bertrand::static_str self, size_type width, char fillchar = ' '>
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
        constexpr size_type index = rfind<self, sep>();
        if constexpr (index < 0) {
            return string_list<self, "", "">{};
        } else {
            constexpr size_type offset = index + sep.size();
            constexpr size_type remaining = self.size() - offset;
            return string_list<
                static_str<index>{self.buffer},
                sep,
                static_str<remaining>{self.buffer + offset}
            >{};
        }
    }

    /* Equivalent to Python `str.rsplit(sep[, maxsplit])`. */
    template <
        bertrand::static_str self,
        bertrand::static_str sep,
        size_type maxsplit = std::numeric_limits<size_type>::max()
    > requires (sep.size() > 0)
    [[nodiscard]] static consteval auto rsplit() noexcept {
        constexpr size_type freq = count<self, sep>();
        if constexpr (freq == 0) {
            return string_list<self>{};
        } else {
            constexpr size_type n = (freq < maxsplit ? freq : maxsplit) + 1;
            return []<size_type... Is>(std::index_sequence<Is...>) {
                constexpr std::array<size_type, n> strides = [] {
                    std::array<size_type, n> result;
                    size_type prev = self.size();
                    for (size_type i = prev - 1, j = 0; j < n - 1; --i) {
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

                /// TODO: this is basically the opposite logic from split()
                std::tuple<static_str<std::get<Is>(strides)>...> result;
                size_type offset = self.size();
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
        constexpr index_type stop = last_non_stripped<self, chars>();
        if constexpr (stop < 0) {
            return bertrand::static_str{""};
        } else {
            constexpr size_type delta = size_type(stop) + 1;
            static_str<delta> result;
            std::copy_n(self.buffer, delta, result.buffer);
            result.buffer[delta] = '\0';
            return result;
        }
    }

    /* Equivalent to Python `str.split(sep[, maxsplit])`.  The result is returned as a
    `string_list<...>` specialized to hold each substring. */
    template <
        bertrand::static_str self,
        bertrand::static_str sep,
        size_type maxsplit = std::numeric_limits<size_type>::max()
    > requires (sep.size() > 0)
    [[nodiscard]] static consteval auto split() noexcept {
        constexpr size_type freq = count<self, sep>();
        if constexpr (freq == 0) {
            return string_list<self>{};
        } else {
            constexpr size_type n = (freq < maxsplit ? freq : maxsplit) + 1;
            return []<size_type... Is>(std::index_sequence<Is...>) {
                static constexpr std::array<size_type, sizeof...(Is)> strides = [] {
                    std::array<size_type, n> result;
                    size_type prev = 0;
                    for (size_type i = prev, j = 0; j < n - 1;) {
                        if (std::equal(
                            sep.buffer,
                            sep.buffer + sep.size(),
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
                constexpr auto substr = []<size_type... Js>(std::index_sequence<Js...>) {
                    return self.buffer + (0 + ... + (strides[Js] + sep.size()));
                };
                return string_list<
                    static_str<strides[Is]>{substr(std::make_index_sequence<Is>{})}...
                >{};
            }(std::make_index_sequence<n>{});
        }
    }

    /* Equivalent to Python `str.splitlines([keepends])`. */
    template <bertrand::static_str self, bool keepends = false>
    [[nodiscard]] static consteval auto splitlines() noexcept {
        constexpr size_type n = [] {
            if constexpr (self.size() == 0) {
                return 0;
            } else {
                size_type total = 1;
                for (size_type i = 0; i < self.size(); ++i) {
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
            constexpr auto result = []<size_type... Is>(std::index_sequence<Is...>) {
                constexpr std::array<size_type, n> strides = [] {
                    std::array<size_type, n> result;
                    size_type prev = 0;
                    for (size_type i = prev, j = 0; j < n - 1;) {
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
                size_type offset = 0;
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
                return []<size_type... Is>(std::index_sequence<Is...>, auto&& result) {
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
        constexpr index_type start = first_non_stripped<self, chars>();
        if constexpr (start < 0) {
            return bertrand::static_str{""};
        } else {
            constexpr index_type stop = last_non_stripped<self, chars>();
            constexpr size_type delta = size_type(stop - start + 1);  // +1 for half-open interval
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
        for (size_type i = 0; i < self.size(); ++i) {
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
        for (size_type i = 0; i < self.size(); ++i) {
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
        for (size_type i = 0; i < self.size(); ++i) {
            result.buffer[i] = impl::char_toupper(self.buffer[i]);
        }
        result.buffer[self.size()] = '\0';
        return result;
    }

    /* Equivalent to Python `str.zfill(width)`, but evaluated statically at compile
    time. */
    template <bertrand::static_str self, size_type width>
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
            size_type start = 0;
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
};


template <static_str self>
struct string_wrapper : impl::static_str_tag {
private:
    using string_type = static_str<self.size()>;

public:
    using value_type = string_type::value_type;
    using reference = string_type::reference;
    using const_reference = string_type::const_reference;
    using pointer = string_type::pointer;
    using const_pointer = string_type::const_pointer;
    using size_type = string_type::size_type;
    using index_type = string_type::index_type;
    using difference_type = string_type::difference_type;
    using iterator = string_type::iterator;
    using const_iterator = string_type::const_iterator;
    using reverse_iterator = string_type::reverse_iterator;
    using const_reverse_iterator = string_type::const_reverse_iterator;
    using slice = string_type::slice;
    using const_slice = string_type::const_slice;

    template <typename V> requires (meta::convertible_to<static_str<self.size()>, V>)
    [[nodiscard]] constexpr operator V() const noexcept { return self; }
    [[nodiscard]] static consteval auto data() noexcept { return self.data(); }
    [[nodiscard]] static consteval auto size() noexcept { return self.size(); }
    [[nodiscard]] static consteval auto ssize() noexcept { return self.ssize(); }
    [[nodiscard]] static consteval auto empty() noexcept { return self.empty(); }
    [[nodiscard]] explicit consteval operator bool() noexcept { return self.operator bool(); }
    [[nodiscard]] static constexpr auto begin() noexcept { return self.begin(); }
    [[nodiscard]] static constexpr auto cbegin() noexcept { return self.cbegin(); }
    [[nodiscard]] static constexpr auto end() noexcept { return self.end(); }
    [[nodiscard]] static constexpr auto cend() noexcept { return self.cend(); }
    [[nodiscard]] static constexpr auto rbegin() noexcept { return self.rbegin(); }
    [[nodiscard]] static constexpr auto crbegin() noexcept { return self.crbegin(); }
    [[nodiscard]] static constexpr auto rend() noexcept { return self.rend(); }
    [[nodiscard]] static constexpr auto crend() noexcept { return self.crend(); }

    /* Check whether a given substring is present within the string. */
    template <static_str sub, index_type start = 0, index_type stop = self.size()>
    [[nodiscard]] static consteval bool contains() noexcept {
        return find<sub, start, stop>() >= 0;
    }

    /* Get the character at index `I`, where `I` is known at compile time.  Applies
    Python-style wraparound for negative indices, and fails to compile if the index is
    out of bounds after normalization. */
    template <index_type I> requires (impl::valid_index<size(), I>())
    [[nodiscard]] static consteval auto get() noexcept {
        return self.template get<I>();
    }

    /* Get a slice from the string at compile time.  Takes an explicitly-initialized
    `bertrand::slice` pack describing the start, stop, and step indices.  Each index
    can be omitted by initializing it to `std::nullopt`, which is equivalent to an
    empty slice index in Python.  Applies Python-style wraparound to both `start` and
    `stop`. */
    template <bertrand::slice s>
    [[nodiscard]] static consteval auto get() noexcept {
        return string_wrapper<self.template get<s>()>{};
    }

    /* Access a character within the underlying buffer.  Applies Python-style
    wraparound for negative indices, and throws an `IndexError` if the index is out of
    bounds after normalization. */
    [[nodiscard]] static constexpr auto operator[](index_type i) {
        return self[i];
    }

    /* Slice operator utilizing an initializer list.  Up to 3 (possibly negative)
    indices may be supplied according to Python semantics, with `std::nullopt` equating
    to `None`. */
    [[nodiscard]] static constexpr auto operator[](bertrand::slice s) {
        return self[s];
    }

    /* Concatenate two static strings at compile time. */
    template <auto S>
    [[nodiscard]] consteval auto operator+(string_wrapper<S>) const noexcept {
        return string_wrapper<self + S>{};
    }

    /* Concatenate two static strings at compile time. */
    template <auto M>
    [[nodiscard]] consteval auto operator+(const static_str<M>& other) const noexcept {
        return self + other;
    }

    /* Concatenate a static string with a string literal at compile time. */
    template <auto M>
    [[nodiscard]] friend constexpr auto operator+(
        string_wrapper,
        const char(&other)[M]
    ) noexcept {
        return self + other;
    }

    /* Concatenate a static string with a string literal at compile time. */
    template <auto M>
    [[nodiscard]] friend constexpr auto operator+(
        const char(&other)[M],
        string_wrapper
    ) noexcept {
        return other + self;
    }

    /* Concatenate a static string with a runtime string. */
    template <meta::convertible_to<std::string> V>
        requires (!meta::string_literal<V> && !meta::static_str<V>)
    [[nodiscard]] friend constexpr auto operator+(string_wrapper, V&& other) {
        return self + std::forward<V>(other);
    }

    /* Concatenate a static string with a runtime string. */
    template <meta::convertible_to<std::string> V>
        requires (!meta::string_literal<V> && !meta::static_str<V>)
    [[nodiscard]] friend constexpr auto operator+(V&& other, string_wrapper) {
        return std::forward<V>(other) + self;
    }

    /* Repeat a string a given number of times at compile time. */
    template <size_type reps>
    [[nodiscard]] static consteval auto repeat() noexcept {
        return string_wrapper<self.template repeat<self, reps>()>{};
    }

    /* Repeat a string a given number of times at runtime. */
    [[nodiscard]] friend constexpr auto operator*(string_wrapper, size_type reps) noexcept {
        return self * reps;
    }

    /* Repeat a string a given number of times at runtime. */
    [[nodiscard]] friend constexpr auto operator*(size_type reps, string_wrapper) noexcept {
        return reps * self;
    }

    /* Lexicographically compare two static strings at compile time. */
    template <auto S>
    [[nodiscard]] consteval auto operator<=>(string_wrapper<S>) const noexcept {
        return self <=> S;
    }

    /* Lexicographically compare two static strings at compile time. */
    template <auto M>
    [[nodiscard]] consteval auto operator<=>(const static_str<M>& other) const noexcept {
        return self <=> other;
    }

    /* Lexicographically compare a static string against a string literal. */
    template <auto M>
    [[nodiscard]] friend constexpr auto operator<=>(
        string_wrapper,
        const char(&other)[M]
    ) noexcept {
        return self <=> other;
    }

    /* Lexicographically compare a static string against a string literal. */
    template <auto M>
    [[nodiscard]] friend constexpr auto operator<=>(
        const char(&other)[M],
        string_wrapper
    ) noexcept {
        return other <=> self;
    }

    /* Lexicographically compare a static string against a string view. */
    template <auto M>
    [[nodiscard]] friend constexpr auto operator<=>(
        string_wrapper,
        std::string_view other
    ) noexcept(noexcept(self <=> other)) {
        return self <=> other;
    }

    /* Lexicographically compare a static string against a string view. */
    template <auto M>
    [[nodiscard]] friend constexpr auto operator<=>(
        std::string_view other,
        string_wrapper
    ) noexcept(noexcept(other <=> self)) {
        return other <=> self;
    }

    /* Check for lexicographic equality between two static strings. */
    template <auto S>
    [[nodiscard]] consteval auto operator==(string_wrapper<S>) const noexcept {
        return self == S;
    }

    /* Check for lexicographic equality between two static strings. */
    template <auto M>
    [[nodiscard]] consteval auto operator==(const static_str<M>& other) const noexcept {
        return self == other;
    }

    /* Check for lexicographic equality between a static string and a string literal. */
    template <auto M>
    [[nodiscard]] friend constexpr auto operator==(
        string_wrapper,
        const char(&other)[M]
    ) noexcept {
        return self == other;
    }

    /* Check for lexicographic equality between a static string and a string literal. */
    template <auto M>
    [[nodiscard]] friend constexpr auto operator==(
        const char(&other)[M],
        string_wrapper
    ) noexcept {
        return other == self;
    }

    /* Check for lexicographic equality between a static string and a string view. */
    template <auto M>
    [[nodiscard]] friend constexpr auto operator==(
        string_wrapper,
        std::string_view other
    ) noexcept(noexcept(self == other)) {
        return self == other;
    }

    /* Check for lexicographic equality between a static string and a string view. */
    template <auto M>
    [[nodiscard]] friend constexpr auto operator==(
        std::string_view other,
        string_wrapper
    ) noexcept(noexcept(other == self)) {
        return other == self;
    }

    /* Equivalent to Python `str.capitalize()`. */
    [[nodiscard]] static consteval auto capitalize() noexcept {
        return string_wrapper<self.template capitalize<self>()>{};
    }

    /* Equivalent to Python `str.center(width[, fillchar])`. */
    template <size_type width, char fillchar = ' '>
    [[nodiscard]] static consteval auto center() noexcept {
        return string_wrapper<self.template center<self, width, fillchar>()>{};
    }

    /* Equivalent to Python `str.count(sub[, start[, stop]])`. */
    template <static_str sub, index_type start = 0, index_type stop = self.size()>
    [[nodiscard]] static consteval auto count() noexcept {
        return self.template count<self, sub, start, stop>();
    }

    /* Equivalent to Python `str.endswith(suffix)`. */
    template <static_str suffix>
    [[nodiscard]] static consteval auto endswith() noexcept {
        return self.template endswith<self, suffix>();
    }

    /* Equivalent to Python `str.expandtabs([tabsize])`. */
    template <size_type tabsize = 8>
    [[nodiscard]] static consteval auto expandtabs() noexcept {
        return string_wrapper<self.template expandtabs<self, tabsize>()>{};
    }

    /* Equivalent to Python `str.find(sub[, start[, stop]])`.  Returns -1 if the
    substring is not found. */
    template <static_str sub, index_type start = 0, index_type stop = self.size()>
    [[nodiscard]] static consteval auto find() noexcept {
        return self.template find<self, sub, start, stop>();
    }

    /* Equivalent to Python `str.index(sub[, start[, stop]])`.  Fails to compile if
    the substring is not present. */
    template <static_str sub, index_type start = 0, index_type stop = self.size()>
        requires (contains<sub, start, stop>())
    [[nodiscard]] static consteval auto index() noexcept {
        return find<sub, start, stop>();
    }

    /* Equivalent to Python `str.isalpha()`. */
    [[nodiscard]] static consteval auto isalpha() noexcept {
        return self.template isalpha<self>();
    }

    /* Equivalent to Python `str.isalnum()`. */
    [[nodiscard]] static consteval auto isalnum() noexcept {
        return self.template isalnum<self>();
    }

    /* Equivalent to Python `str.isascii()`. */
    [[nodiscard]] static consteval auto isascii() noexcept {
        return self.template isascii<self>();
    }

    /* Equivalent to Python `str.isdigit()`. */
    [[nodiscard]] static consteval auto isdigit() noexcept {
        return self.template isdigit<self>();
    }

    /* Equivalent to Python `str.islower()`. */
    [[nodiscard]] static consteval auto islower() noexcept {
        return self.template islower<self>();
    }

    /* Equivalent to Python `str.isspace()`. */
    [[nodiscard]] static consteval auto isspace() noexcept {
        return self.template isspace<self>();
    }

    /* Equivalent to Python `str.istitle()`. */
    [[nodiscard]] static consteval auto istitle() noexcept {
        return self.template istitle<self>();
    }

    /* Equivalent to Python `str.isupper()`. */
    [[nodiscard]] static consteval auto isupper() noexcept {
        return self.template isupper<self>();
    }

    /* Equivalent to Python `str.join(strings...)`. */
    template <static_str first, static_str... rest>
    [[nodiscard]] static consteval auto join() noexcept {
        return string_wrapper<self.template join<self, first, rest...>()>{};
    }

    /* Equivalent to Python `str.ljust(width[, fillchar])`. */
    template <size_type width, char fillchar = ' '>
    [[nodiscard]] static consteval auto ljust() noexcept {
        return string_wrapper<self.template ljust<self, width, fillchar>()>{};
    }

    /* Equivalent to Python `str.lower()`. */
    [[nodiscard]] static consteval auto lower() noexcept {
        return string_wrapper<self.template lower<self>()>{};
    }

    /* Equivalent to Python `str.lstrip([chars])`. */
    template <static_str chars = " \t\n\r\f\v">
    [[nodiscard]] static consteval auto lstrip() noexcept {
        return string_wrapper<self.template lstrip<self, chars>()>{};
    }

    /* Equivalent to Python `str.partition(sep)`. */
    template <static_str sep>
    [[nodiscard]] static consteval auto partition() noexcept {
        return self.template partition<self, sep>();
    }

    /* Equivalent to Python `str.removeprefix()`, but evaluated statically at compile
    time. */
    template <static_str prefix>
    [[nodiscard]] static consteval auto removeprefix() noexcept {
        return string_wrapper<self.template removeprefix<self, prefix>()>{};
    }

    /* Equivalent to Python `str.removesuffix()`, but evaluated statically at compile
    time. */
    template <static_str suffix>
    [[nodiscard]] static consteval auto removesuffix() noexcept {
        return string_wrapper<self.template removesuffix<self, suffix>()>{};
    }

    /* Equivalent to Python `str.replace()`. */
    template <
        static_str sub,
        static_str repl,
        size_type max_count = std::numeric_limits<size_type>::max()
    >
    [[nodiscard]] static consteval auto replace() noexcept {
        return string_wrapper<self.template replace<self, sub, repl, max_count>()>{};
    }

    /* Equivalent to Python `str.rfind(sub[, start[, stop]])`.  Returns -1 if the
    substring is not found. */
    template <static_str sub, index_type start = 0, index_type stop = self.size()>
    [[nodiscard]] static consteval auto rfind() noexcept {
        return self.template rfind<self, sub, start, stop>();
    }

    /* Equivalent to Python `str.rindex(sub[, start[, stop]])`.  Fails to compile if
    the substring is not present. */
    template <static_str sub, index_type start = 0, index_type stop = self.size()>
        requires (contains<sub, start, stop>())
    [[nodiscard]] static consteval auto rindex() noexcept {
        return rfind<sub, start, stop>();
    }

    /* Equivalent to Python `str.rjust(width[, fillchar])`. */
    template <size_type width, char fillchar = ' '>
    [[nodiscard]] static consteval auto rjust() noexcept {
        return string_wrapper<self.template rjust<self, width, fillchar>()>{};
    }

    /* Equivalent to Python `str.rpartition(sep)`. */
    template <static_str sep>
    [[nodiscard]] static consteval auto rpartition() noexcept {
        return self.template rpartition<self, sep>();
    }

    /* Equivalent to Python `str.rsplit(sep[, maxsplit])`. */
    template <
        static_str sep,
        size_type maxsplit = std::numeric_limits<size_type>::max()
    > requires (sep.size() > 0)
    [[nodiscard]] static consteval auto rsplit() noexcept {
        return self.template rsplit<self, sep, maxsplit>();
    }

    /* Equivalent to Python `str.rstrip([chars])`. */
    template <static_str chars = " \t\n\r\f\v">
    [[nodiscard]] static consteval auto rstrip() noexcept {
        return string_wrapper<self.template rstrip<self, chars>()>{};
    }

    /* Equivalent to Python `str.split(sep[, maxsplit])`.  The result is returned as a
    `string_list<...>` specialized to hold each substring. */
    template <
        static_str sep,
        size_type maxsplit = std::numeric_limits<size_type>::max()
    > requires (sep.size() > 0)
    [[nodiscard]] static consteval auto split() noexcept {
        return self.template split<self, sep, maxsplit>();
    }

    /* Equivalent to Python `str.splitlines([keepends])`. */
    template <bool keepends = false>
    [[nodiscard]] static consteval auto splitlines() noexcept {
        return self.template splitlines<self, keepends>();
    }

    /* Equivalent to Python `str.startswith(prefix)`, but evaluated statically at
    compile time. */
    template <static_str prefix>
    [[nodiscard]] static consteval auto startswith() noexcept {
        return self.template startswith<self, prefix>();
    }

    /* Equivalent to Python `str.strip([chars])`, but evaluated statically at compile
    time. */
    template <static_str chars = " \t\n\r\f\v">
    [[nodiscard]] static consteval auto strip() noexcept {
        return string_wrapper<self.template strip<self, chars>()>{};
    }

    /* Equivalent to Python `str.swapcase()`, but evaluated statically at compile
    time. */
    [[nodiscard]] static consteval auto swapcase() noexcept {
        return string_wrapper<self.template swapcase<self>()>{};
    }

    /* Equivalent to Python `str.title()`, but evaluated statically at compile time. */
    [[nodiscard]] static consteval auto title() noexcept {
        return string_wrapper<self.template title<self>()>{};
    }

    /* Equivalent to Python `str.upper()`, but evaluated statically at compile time. */
    [[nodiscard]] static consteval auto upper() noexcept {
        return string_wrapper<self.template upper<self>()>{};
    }

    /* Equivalent to Python `str.zfill(width)`, but evaluated statically at compile
    time. */
    template <size_type width>
    [[nodiscard]] static consteval auto zfill() noexcept {
        return string_wrapper<self.template zfill<self, width>()>{};
    }
};


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
                return
                    _counts<Rest...>{}(c, pos) +
                    (pos < First.size() && First.data()[pos] == c);
            }
        };
        static constexpr size_t counts(unsigned char c, size_t pos) {
            return _counts<Keys...>{}(c, pos);
        }

        template <size_t I, unsigned char C, static_str... Strings>
        static constexpr size_t first_occurrence = 0;
        template <size_t I, unsigned char C, static_str First, static_str... Rest>
        static constexpr size_t first_occurrence<I, C, First, Rest...> =
            (I < First.size() && First.data()[I] == C) ?
                0 : first_occurrence<I, C, Rest...> + 1;

        template <size_t I, size_t J, static_str... Strings>
        static constexpr size_t _variation = 0;
        template <size_t I, size_t J, static_str First, static_str... Rest>
        static constexpr size_t _variation<I, J, First, Rest...> =
            (I < First.size() && J == first_occurrence<I, First.data()[I], Keys...>) +
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
                        unsigned char c = pos < F.size() ? *F[pos] : 0;
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
                    unsigned char c = pos < First.size() ? First.data()[pos] : 0;
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
            constexpr size_t len = Key::size();
            size_t out = 0;
            for (size_t pos : positions) {
                out += weights[pos < len ? str.data()[pos] : 0];
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


/* A specialization of string_map that does not hold any values.  Such a data structure
is equivalent to a perfectly-hashed set of compile-time strings, which can be
efficiently searched at runtime.  Rather than dereferencing to a value, the buckets
and iterators will dereference to `string_view`s of the template key buffers. */
template <static_str... Keys> requires (meta::perfectly_hashable<Keys...>)
struct string_set;


/* A compile-time perfect hash table with a finite set of static strings as keys.  The
data structure will compute a perfect hash function for the given strings at compile
time, and will store the values in a fixed-size array that can be baked into the final
binary.

Searching the map is extremely fast even in the worst case, consisting only of a
perfect FNV-1a hash, a single array lookup, and a string comparison to validate.  No
collision resolution is necessary, due to the perfect hash function.  If the search
string is also known at compile time, then even these can be optimized out, skipping
straight to the final value with no intermediate computation. */
template <meta::not_void T, static_str... Keys> requires (meta::perfectly_hashable<Keys...>)
struct string_map;


/// TODO: allow heterogenous values?
/// -> Maybe you can write string_wrapper<"key", T>?  So:
///    static_map<string_wrapper<"foo", int>, string_wrapper<"bar", double>> map;

/// TODO: constexpr string_map map = make_string_map<"foo", "bar", "baz">(1, "a", 2.5);

/// constexpr string_map map = string_list<"foo", "bar", "baz">::map(1, "a", 2.5);

/// -> static_map[] returns the common_type of the initializers, whereas get<>()
/// returns the actual type at that index.  contains() takes any type that is
/// comparable against all value types.  Iterators also return the common type.


/// TODO: the only way to do this might be to store an array of string_view and values
/// converted to the common type, and then separately store a tuple of the original
/// values.  That way, if you look up by `get<"key">()`, you get the actual original
/// type, and if you iterate or search by `operator[]`, you get the common type.


template <static_str... Strings>
struct string_list {
    using value_type = const std::string_view;
    using reference = value_type&;
    using const_reference = reference;
    using pointer = value_type*;
    using const_pointer = pointer;
    using size_type = size_t;
    using index_type = ssize_t;
    using difference_type = std::ptrdiff_t;
    using iterator = impl::contiguous_iterator<const std::string_view>;
    using const_iterator = iterator;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = reverse_iterator;
    using slice = impl::contiguous_slice<const std::string_view>;
    using const_slice = slice;

    [[nodiscard]] static consteval pointer data() noexcept { return m_data.data(); }
    [[nodiscard]] static consteval size_type size() noexcept { return sizeof...(Strings); }
    [[nodiscard]] static consteval index_type ssize() noexcept { return ssize_t(size()); }
    [[nodiscard]] static consteval bool empty() noexcept { return size() == 0; }
    [[nodiscard]] explicit consteval operator bool() noexcept { return !empty(); }
    [[nodiscard]] static constexpr iterator begin() noexcept { return {m_data.data()}; }
    [[nodiscard]] static constexpr iterator cbegin() noexcept { return begin(); }
    [[nodiscard]] static constexpr iterator end() noexcept { return {m_data.data() + size()}; }
    [[nodiscard]] static constexpr iterator cend() noexcept { return end(); }
    [[nodiscard]] static constexpr reverse_iterator rbegin() noexcept {
        return std::make_reverse_iterator(end());
    }
    [[nodiscard]] static constexpr reverse_iterator crbegin() noexcept {
        return std::make_reverse_iterator(cend());
    }
    [[nodiscard]] static constexpr reverse_iterator rend() noexcept {
        return std::make_reverse_iterator(begin());
    }
    [[nodiscard]] static constexpr reverse_iterator crend() noexcept {
        return std::make_reverse_iterator(cbegin());
    }

    /* Check whether a compile-time index is in bounds for the list. */
    template <index_type I>
    [[nodiscard]] static consteval bool contains() noexcept {
        return impl::valid_index<size(), I>();
    }

    /* Check whether the list contains an arbitrary string. */
    template <static_str Key>
    [[nodiscard]] static consteval bool contains() noexcept {
        return ((Key == Strings) || ...);
    }

    /* Check whether the list contains an arbitrary string. */
    template <size_t N>
    [[nodiscard]] static constexpr bool contains(const char(&str)[N])
        noexcept(noexcept(((str == Strings) || ...)))
    {
        return ((str == Strings) || ...);
    }

    /* Check whether the list contains an arbitrary string. */
    template <meta::static_str Key>
    [[nodiscard]] static constexpr bool contains(const Key& key)
        noexcept(noexcept(((key == Strings) || ...)))
    {
        return ((key == Strings) || ...);
    }

    /* Check whether the list contains an arbitrary string. */
    template <meta::convertible_to<const char*> T>
        requires (!meta::string_literal<T> && !meta::static_str<T>)
    [[nodiscard]] static constexpr bool contains(T&& key)
        noexcept(noexcept(
            ((std::string_view(static_cast<const char*>(std::forward<T>(key))) == Strings) || ...)
        ))
    {
        std::string_view view = static_cast<const char*>(std::forward<T>(key));
        return ((view == Strings) || ...);
    }

    /* Check whether the list contains an arbitrary string. */
    template <meta::convertible_to<std::string_view> T>
        requires (
            !meta::string_literal<T> &&
            !meta::static_str<T> &&
            !meta::convertible_to<T, const char*>
        )
    [[nodiscard]] static constexpr bool contains(T&& key)
        noexcept(noexcept(((std::string_view(std::forward<T>(key)) == Strings) || ...)))
    {
        std::string_view view = std::forward<T>(key);
        return ((view == Strings) || ...);
    }

    /* Check whether the list contains an arbitrary string. */
    template <meta::convertible_to<std::string> T>
        requires (
            !meta::string_literal<T> &&
            !meta::static_str<T> &&
            !meta::convertible_to<T, const char*> &&
            !meta::convertible_to<T, std::string_view>
        )
    [[nodiscard]] static constexpr bool contains(T&& key)
        noexcept(noexcept(((std::string(std::forward<T>(key)) == Strings) || ...)))
    {
        std::string str = std::forward<T>(key);
        std::string_view view = str;
        return ((view == Strings) || ...);
    }

    /* Check whether a runtime index is in bounds for the list. */
    template <meta::convertible_to<index_type> T>
        requires (
            !meta::string_literal<T> &&
            !meta::static_str<T> &&
            !meta::convertible_to<T, const char*> &&
            !meta::convertible_to<T, std::string_view> &&
            !meta::convertible_to<T, std::string>
        )
    [[nodiscard]] static constexpr bool contains(T&& idx)
        noexcept(noexcept(impl::valid_index(size(), std::forward<T>(idx))))
    {
        return impl::valid_index(size(), std::forward<T>(idx));
    }

    /* Get the string at index I, where `I` is known at compile time.  Applies
    Python-style wraparound for negative indices, and fails to compile if the index
    is out of bounds after normalization. */
    template <index_type I> requires (contains<I>())
    [[nodiscard]] static consteval const auto& get() noexcept {
        return meta::unpack_string<size_type(I), Strings...>;
    }

    /* Get a slice from the list at compile time.  Takes an explicitly-initialized
    `bertrand::slice` pack describing the start, stop, and step indices.  Each index
    can be omitted by initializing it to `std::nullopt`, which is equivalent to an
    empty slice index in Python.  Applies Python-style wraparound to both `start` and
    `stop`. */
    template <bertrand::slice s>
    [[nodiscard]] static consteval auto get() noexcept {
        static constexpr auto indices = s.normalize(ssize());
        return []<size_t... Is>(std::index_sequence<Is...>) {
            return string_list<meta::unpack_string<
                indices.start + Is * indices.step,
                Strings...
            >...>{};
        }(std::make_index_sequence<indices.length>{});
    }

    /* Get the string at index I, where `I` is known at runtime.  Applies Python-style
    wraparound for negative indices, and returns an `end()` iterator if the index is
    out of bounds. */
    [[nodiscard]] static constexpr iterator operator[](index_type i) noexcept {
        i += ssize() * (i < 0);
        if (i < 0 || i >= ssize()) {
            return end();
        }
        return {data() + i};
    }

    /* Slice operator.  Takes an explicitly-initialized `bertrand::slice` pack
    describing the start, stop, and step indices, and returns a slice object containing
    the strings within the slice.  Each index can be omitted by initializing it to
    `std::nullopt`, which is equivalent to an empty slice index in Python.  Applies
    Python-style wraparound to both `start` and `stop`. */
    [[nodiscard]] static constexpr slice operator[](bertrand::slice s) noexcept {
        return {data(), s.normalize(ssize())};
    }

    /* Equivalent to Python `sep.join(strings...)`. */
    template <static_str sep>
    [[nodiscard]] static consteval auto join() noexcept {
        return string_wrapper<sep>::template join<Strings...>();
    }

    /* Convert this string list into a string set, assuming the strings it contains are
    perfectly hashable. */
    [[nodiscard]] static consteval string_set<Strings...> set()
        noexcept(noexcept(string_set<Strings...>{}))
        requires(meta::perfectly_hashable<Strings...>)
    {
        return {};
    }

    /// TODO: support for heterogenous types is an interesting idea.  Might be
    /// challenging in practice, however.

    // /* Convert this string list into a string map with the given values, assuming the
    // strings it contains are perfectly hashable. */
    // template <typename... Args>
    // [[nodiscard]] static consteval string_map<...> map(Args&&... args)
    //     noexcept(noexcept(string_map<...>{std::forward<Args>(args)...}))
    //     requires(meta::perfectly_hashable<Strings...>)
    // {
    //     return {std::forward<Args>(args)...};
    // }

    /* Concatenate two string lists via the + operator. */
    template <static_str... OtherStrings>
    [[nodiscard]] consteval auto operator+(string_list<OtherStrings...>) const noexcept {
        return string_list<Strings..., OtherStrings...>{};
    }

    /* Lexicographically compare two string lists. */
    template <static_str... OtherStrings>
    [[nodiscard]] consteval std::strong_ordering operator<=>(
        string_list<OtherStrings...>
    ) const noexcept {
        return []<size_t I = 0>(this auto&& self) {
            if constexpr (I < sizeof...(Strings) && I < sizeof...(OtherStrings)) {
                auto comp = get<I>() <=> meta::unpack_string<I, OtherStrings...>;
                if (comp != 0) {
                    return comp;
                }
                return std::forward<decltype(self)>(self).template operator()<I + 1>();
            } else if constexpr (I < sizeof...(Strings)) {
                return std::strong_ordering::greater;
            } else if constexpr (I < sizeof...(OtherStrings)) {
                return std::strong_ordering::less;
            } else {
                return std::strong_ordering::equal;
            }
        }();
    }

    /* Check for lexicographic equality between two string lists. */
    template <static_str... OtherStrings>
    [[nodiscard]] consteval bool operator==(string_list<OtherStrings...>) const noexcept {
        return
            sizeof...(Strings) == sizeof...(OtherStrings) &&
            ((Strings == OtherStrings) && ...);
    }

private:
    static constexpr std::array<value_type, size()> m_data {Strings...};
};



template <static_str... Keys> requires (meta::perfectly_hashable<Keys...>)
struct string_set {

};


template <meta::not_void T, static_str... Keys> requires (meta::perfectly_hashable<Keys...>)
struct string_map {

};







template <auto... values>
struct Foo {};

constexpr Foo<1, 2, "abc"_str> foo;



inline void test() {
    constexpr string_list<"foo", "bar", "baz"> list;
    static_assert(list[5] == list.end());




    static constexpr static_str s = "hello";
    constexpr std::string s2 = s;
    static_assert(s == s2);


    static_assert(string_wrapper<s>::get<slice{-1, std::nullopt, -1}>().upper() == "OLLEH");
    static_assert("hello world"_str.split<" ">().join<".">() == "hello.world");
}








namespace impl {

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


/* Format a string similar to `std::format()`.  Unformattable objects will be passed
through `repr()` to obtain a universal representation. */
template <typename... Args>
constexpr std::string format(impl::format_repr<Args...> fmt, Args&&... args) {
    return std::format(fmt, impl::to_format_repr(std::forward<Args>(args))...);
}


/* Print a format string to an output buffer.  Does not append a newline character.
Unformattable objects will be passed through `repr()` to obtain a universal
representation. */
template <typename... Args>
constexpr void print(impl::format_repr<Args...> fmt, Args&&... args) {
    std::print(fmt, impl::to_format_repr(std::forward<Args>(args))...);
}


/* Print an arbitrary value to an output buffer by calling `repr()` on it. Does not
append a newline character.  Unformattable objects will be passed through `repr()` to
obtain a universal representation. */
template <typename T>
constexpr void print(T&& obj) {
    std::cout << repr(std::forward<T>(obj));
}


/* Print a format string to an output buffer.  Appends a newline character to the
output.  Unformattable objects will be passed through `repr()` to obtain a universal
representation. */
template <typename... Args>
constexpr void println(impl::format_repr<Args...> fmt, Args&&... args) {
    std::println(fmt, impl::to_format_repr(std::forward<Args>(args))...);
}


/* Print an arbitrary value to an output buffer by calling `repr()` on it.  Appends a
newline character to the output.  Unformattable objects will be passed through `repr()`
to obtain a universal representation. */
template <typename T>
constexpr void println(T&& obj) {
    std::cout << repr(std::forward<T>(obj)) << "\n";
}


/* A python-style `assert` statement in C++, which is optimized away if built with
`bertrand::DEBUG == false` (release mode).  This differs from the built-in C++
`assert()` macro in that this is implemented as a normal inline function that accepts
a format string and arguments (which are automatically passed through `repr()`), and
results in a `bertrand::AssertionError` with a coherent traceback, which can be
seamlessly passed up to Python.  It is thus possible to implement pytest-style unit
tests using this function just as in native Python. */
template <typename... Args> requires (DEBUG)
[[gnu::always_inline]] void assert_(
    bool cnd,
    impl::format_repr<Args...> msg = "",
    Args&&... args
) {
    if (!cnd) {
        throw AssertionError(std::format(
            msg,
            impl::to_format_repr(std::forward<Args>(args))...
        ));
    }
}


/* A python-style `assert` statement in C++, which is optimized away if built with
`bertrand::DEBUG == false` (release mode).  This differs from the built-in C++
`assert()` macro in that this is implemented as a normal inline function that accepts
an arbitrary value (which is automatically passed through `repr()`), and results in a
`bertrand::AssertionError` with a coherent traceback, which can be seamlessly passed up
to Python.  It is thus possible to implement pytest-style unit tests using this
function just as in native Python. */
template <typename T> requires (DEBUG)
[[gnu::always_inline]] void assert_(bool cnd, T&& obj) {
    if (!cnd) {
        throw AssertionError(repr(std::forward<T>(obj)));
    }
}


/* A python-style `assert` statement in C++, which is optimized away if built with
`bertrand::DEBUG == false` (release mode).  This differs from the built-in C++
`assert()` macro in that this is implemented as a normal inline function that accepts
a format string and arguments (which are automatically passed through `repr()`), and
results in a `bertrand::AssertionError` with a coherent traceback, which can be
seamlessly passed up to Python.  It is thus possible to implement pytest-style unit
tests using this function just as in native Python. */
template <typename... Args> requires (!DEBUG)
[[gnu::always_inline]] void assert_(Args&&... args) noexcept {}


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
