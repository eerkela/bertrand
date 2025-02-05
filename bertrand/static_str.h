#ifndef BERTRAND_STATIC_STRING_H
#define BERTRAND_STATIC_STRING_H

#include <algorithm>
#include <array>
#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include "bertrand/common.h"


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


/* C++20 expands support for non-type template parameters, including compile-time
strings.  This helper class allows ASCII string literals to be encoded directly as
template parameters, and for them to be manipulated entirely at compile-time using
the familiar Python string interface.  Furthermore, templates can be specialized based
on these strings, allowing for full compile-time flexibility based on their values. */
template <size_t N>
struct static_str;


namespace impl {
    struct static_str_tag {};

    static constexpr bool char_islower(char c) {
        return c >= 'a' && c <= 'z';
    }

    static constexpr bool char_isupper(char c) {
        return c >= 'A' && c <= 'Z';
    }

    static constexpr bool char_isalpha(char c) {
        return char_islower(c) || char_isupper(c);
    }

    static constexpr bool char_isdigit(char c) {
        return c >= '0' && c <= '9';
    }

    static constexpr bool char_isalnum(char c) {
        return char_isalpha(c) || char_isdigit(c);
    }

    static constexpr bool char_isascii(char c) {
        return c >= 0 && c <= 127;
    }

    static constexpr bool char_isspace(char c) {
        return c == ' ' || (c >= '\t' && c <= '\r');
    }

    static constexpr bool char_isdelimeter(char c) {
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

    static constexpr bool char_islinebreak(char c) {
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

    static constexpr char char_tolower(char c) {
        return char_isupper(c) ? c + ('a' - 'A') : c;
    }

    static constexpr char char_toupper(char c) {
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

    struct Iterator {
        const char* ptr;
        ssize_t index;

    public:
        using iterator_category             = std::random_access_iterator_tag;
        using difference_type               = std::ptrdiff_t;
        using value_type                    = const char;
        using pointer                       = value_type*;
        using reference                     = value_type&;

        Iterator(const char* ptr, ssize_t index) : ptr(ptr), index(index) {}
        Iterator(const Iterator& other) : ptr(other.ptr), index(other.index) {}
        Iterator(Iterator&& other) : ptr(other.ptr), index(other.index) {}

        Iterator& operator=(const Iterator& other) {
            ptr = other.ptr;
            index = other.index;
            return *this;
        }

        Iterator& operator=(Iterator&& other) {
            ptr = other.ptr;
            index = other.index;
            return *this;
        }

        value_type operator*() const {
            if (ptr == nullptr) {
                throw std::out_of_range("attempt to dereference a null iterator");
            }
            return *ptr;
        }

        pointer operator->() const {
            return &(**this);
        }

        Iterator& operator++() {
            ++index;
            if (index >= 0 && index < N) {
                ++ptr;
            } else {
                ptr = nullptr;
            }
            return *this;
        }

        Iterator operator++(int) {
            Iterator copy = *this;
            ++(*this);
            return copy;
        }

        Iterator& operator--() {
            --index;
            if (index >= 0 && index < N) {
                --ptr;
            } else {
                ptr = nullptr;
            }
            return *this;
        }

        Iterator operator--(int) {
            Iterator copy = *this;
            --(*this);
            return copy;
        }

        Iterator& operator+=(difference_type n) {
            index += n;
            if (index >= 0 && index < N) {
                ptr += n;
            } else {
                ptr = nullptr;
            }
            return *this;
        }

        Iterator operator+(difference_type n) const {
            Iterator copy = *this;
            copy += n;
            return copy;
        }

        Iterator& operator-=(difference_type n) {
            index -= n;
            if (index >= 0 && index < N) {
                ptr -= n;
            } else {
                ptr = nullptr;
            }
            return *this;
        }

        Iterator operator-(difference_type n) const {
            Iterator copy = *this;
            copy -= n;
            return copy;
        }

        difference_type operator-(const Iterator& other) const {
            return index - other.index;
        }

        value_type operator[](difference_type n) const {
            return *(*this + n);
        }

        bool operator<(const Iterator& other) const {
            return ptr != nullptr && (*this - other) < 0;
        }

        bool operator<=(const Iterator& other) const {
            return ptr != nullptr && (*this - other) <= 0;
        }

        bool operator==(const Iterator& other) const {
            return ptr == other.ptr;
        }

        bool operator!=(const Iterator& other) const {
            return ptr != other.ptr;
        }

        bool operator>=(const Iterator& other) const {
            return ptr == nullptr || (*this - other) >= 0;
        }

        bool operator>(const Iterator& other) const {
            return ptr == nullptr || (*this - other) > 0;
        }

    };

    struct ReverseIterator : public Iterator {
        using Iterator::Iterator;

        ReverseIterator& operator++() {
            Iterator::operator--();
            return *this;
        }

        ReverseIterator operator++(int) {
            ReverseIterator copy = *this;
            Iterator::operator--();
            return copy;
        }

        ReverseIterator& operator--() {
            Iterator::operator++();
            return *this;
        }

        ReverseIterator operator--(int) {
            ReverseIterator copy = *this;
            Iterator::operator++();
            return copy;
        }

        ReverseIterator& operator+=(typename Iterator::difference_type n) {
            Iterator::operator-=(n);
            return *this;
        }

        ReverseIterator operator+(typename Iterator::difference_type n) const {
            ReverseIterator copy = *this;
            copy -= n;
            return copy;
        }

        ReverseIterator& operator-=(typename Iterator::difference_type n) {
            Iterator::operator+=(n);
            return *this;
        }

        ReverseIterator operator-(typename Iterator::difference_type n) const {
            ReverseIterator copy = *this;
            Iterator::operator+=(n);
            return copy;
        }

    };

    static constexpr ssize_t normalize_index(ssize_t i) { return i + N * (i < 0); };

    static constexpr char char_tolower(char c) {
        return impl::char_isupper(c) ? c + ('a' - 'A') : c;
    }

    static constexpr char char_toupper(char c) {
        return impl::char_islower(c) ? c + ('A' - 'a') : c;
    }

    template <bertrand::static_str self, bertrand::static_str chars>
    static consteval size_t first_non_stripped() {
        for (size_t i = 0; i < self.size(); ++i) {
            char c = self[i];
            size_t j = 0;
            while (j < chars.size()) {
                if (chars[j++] == c) {
                    break;
                } else if (j == chars.size()) {
                    return i;
                }
            }
        }
        return missing;
    }

    template <bertrand::static_str self, bertrand::static_str chars>
    static consteval size_t last_non_stripped() {
        for (size_t i = 0; i < self.size(); ++i) {
            size_t idx = self.size() - i - 1;
            char c = self[idx];
            size_t j = 0;
            while (j < chars.size()) {
                if (chars[j++] == c) {
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

    static constexpr bool sign_bit(double num) {
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
    /* A placeholder index returned when a substring is not present. */
    static constexpr size_t missing = std::numeric_limits<size_t>::max();

    char buffer[N + 1];  // +1 for null terminator

    consteval static_str(const char* arr) : buffer{} {
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

    constexpr operator const char*() const { return buffer; }
    explicit constexpr operator bool() const { return !empty(); }
    explicit constexpr operator std::string() const { return {buffer, N}; }
    explicit constexpr operator std::string_view() const { return {buffer, N}; }

    constexpr const char* data() const { return buffer; }
    static constexpr size_t size() { return N; }
    static constexpr bool empty() { return !N; }
    Iterator begin() const { return {buffer, 0}; }
    Iterator cbegin() const { return begin(); }
    Iterator end() const { return {nullptr, N}; }
    Iterator cend() const { return end(); }
    ReverseIterator rbegin() const { return {buffer + N - 1, N - 1}; }
    ReverseIterator crbegin() const { return rbegin(); }
    ReverseIterator rend() const { return {nullptr, -1}; }
    ReverseIterator crend() const { return rend(); }

    /* Access a character within the underlying buffer. */
    constexpr const char operator[](ssize_t i) const {
        ssize_t norm = normalize_index(i);
        return (norm >= 0 && norm < N) ? buffer[norm] : '\0';
    }

    /* Slice operator utilizing `std::initializer_list`.  Up to 3 indices may be
    supplied according to Python semantics, with `std::nullopt` equating to `None`. */
    constexpr std::string operator[](
        std::initializer_list<std::optional<ssize_t>> slice
    ) const {
        if (slice.size() > 3) {
            throw std::runtime_error(
                "Slices must be of the form {start[, stop[, step]]}"
            );
        }

        // fill in missing indices
        auto indices = []<size_t... Is>(std::index_sequence<Is...>, const auto& slice) {
            return std::array<std::optional<ssize_t>, 3>{
                Is < slice.size() ? std::data(slice)[Is] : std::nullopt...
            };
        }(std::make_index_sequence<3>{}, slice);

        // normalize step
        ssize_t step = indices[2].value_or(1);
        if (step == 0) {
            throw std::runtime_error("slice step cannot be zero");
        }

        // normalize start/stop based on sign of step and populate result
        std::optional<ssize_t> istart = indices[0];
        std::optional<ssize_t> istop = indices[1];
        if (step > 0) {
            ssize_t start = !istart.has_value() ? 0 : normalize_index(istart.value());
            ssize_t stop = !istop.has_value() ? N : normalize_index(istop.value());
            std::string result((stop - start) * (stop > start) / step, '\0');
            for (ssize_t i = start, j = 0; i < stop; i += step) {
                result[j++] = buffer[i];
            }
            return result;
        } else {
            ssize_t start = !istart.has_value() ? N - 1 : normalize_index(istart.value());
            ssize_t stop = !istop.has_value() ? -1 : normalize_index(istop.value());
            ssize_t delta = (stop - start) * (stop < start);  // needed for floor
            std::string result(delta / step + (delta % step != 0), '\0');
            for (ssize_t i = start, j = 0; i > stop; i += step) {
                result[j++] = buffer[i];
            }
            return result;
        }
    }

    /* Equivalent to Python `str.capitalize()`. */
    template <bertrand::static_str self>
    static consteval auto capitalize() {
        static_str<self.size()> result;
        bool capitalized = false;
        for (size_t i = 0; i < self.size(); ++i) {
            char c = self[i];
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
    static consteval auto center() {
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
    static consteval size_t count() {
        ssize_t nstart = std::max(
            normalize_index(start),
            static_cast<ssize_t>(0)
        );
        ssize_t nstop = std::min(
            normalize_index(stop),
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
    static consteval bool endswith() {
        return suffix.size() <= self.size() && std::equal(
            suffix.buffer,
            suffix.buffer + suffix.size(),
            self.buffer + self.size() - suffix.size()
        );
    }

    /* Equivalent to Python `str.expandtabs([tabsize])`. */
    template <bertrand::static_str self, size_t tabsize = 8>
    static consteval auto expandtabs() {
        constexpr size_t n = count<self, "\t">();
        static_str<self.size() - n + n * tabsize> result;
        size_t offset = 0;
        for (size_t i = 0; i < self.size(); ++i) {
            char c = self[i];
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
    static consteval size_t find() {
        constexpr ssize_t nstart =
            std::max(normalize_index(start), static_cast<ssize_t>(0));
        constexpr ssize_t nstop =
            std::min(normalize_index(stop), static_cast<ssize_t>(self.size()));
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
    static consteval size_t index() {
        return find<self, sub, start, stop>();
    }

    /* Equivalent to Python `str.isalpha()`. */
    template <bertrand::static_str self>
    static consteval bool isalpha() {
        for (size_t i = 0; i < self.size(); ++i) {
            if (!impl::char_isalpha(self[i])) {
                return false;
            }
        }
        return self.size() > 0;
    }

    /* Equivalent to Python `str.isalnum()`. */
    template <bertrand::static_str self>
    static consteval bool isalnum() {
        for (size_t i = 0; i < self.size(); ++i) {
            if (!impl::char_isalnum(self[i])) {
                return false;
            }
        }
        return self.size() > 0;
    }

    /* Equivalent to Python `str.isascii_()`. */
    template <bertrand::static_str self>
    static consteval bool isascii_() {
        for (size_t i = 0; i < self.size(); ++i) {
            if (!impl::char_isascii(self[i])) {
                return false;
            }
        }
        return self.size() > 0;
    }

    /* Equivalent to Python `str.isdigit()`. */
    template <bertrand::static_str self>
    static consteval bool isdigit() {
        for (size_t i = 0; i < self.size(); ++i) {
            if (!impl::char_isdigit(self[i])) {
                return false;
            }
        }
        return self.size() > 0;
    }

    /* Equivalent to Python `str.islower()`. */
    template <bertrand::static_str self>
    static consteval bool islower() {
        for (size_t i = 0; i < self.size(); ++i) {
            if (!impl::char_islower(self[i])) {
                return false;
            }
        }
        return self.size() > 0;
    }

    /* Equivalent to Python `str.isspace()`. */
    template <bertrand::static_str self>
    static consteval bool isspace() {
        for (size_t i = 0; i < self.size(); ++i) {
            if (!impl::char_isspace(self[i])) {
                return false;
            }
        }
        return self.size() > 0;
    }

    /* Equivalent to Python `str.istitle()`. */
    template <bertrand::static_str self>
    static consteval bool istitle() {
        bool last_was_delimeter = true;
        for (size_t i = 0; i < self.size(); ++i) {
            char c = self[i];
            if (last_was_delimeter && impl::char_islower(c)) {
                return false;
            }
            last_was_delimeter = impl::char_isdelimeter(c);
        }
        return self.size() > 0;
    }

    /* Equivalent to Python `str.isupper()`. */
    template <bertrand::static_str self>
    static consteval bool isupper() {
        for (size_t i = 0; i < self.size(); ++i) {
            if (!impl::char_isupper(self[i])) {
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
    static consteval auto join() {
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
    static consteval auto ljust() {
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
    static consteval auto lower() {
        static_str<self.size()> result;
        for (size_t i = 0; i < self.size(); ++i) {
            result.buffer[i] = impl::char_tolower(self[i]);
        }
        result.buffer[self.size()] = '\0';
        return result;
    }

    /* Equivalent to Python `str.lstrip([chars])`. */
    template <bertrand::static_str self, bertrand::static_str chars = " \t\n\r\f\v">
    static consteval auto lstrip() {
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
    static consteval auto partition() {
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
    static consteval auto replace() {
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
    static consteval size_t rfind() {
        constexpr ssize_t nstart =
            std::min(normalize_index(stop), static_cast<ssize_t>(self.size())) - 1;
        constexpr ssize_t nstop =
            std::max(normalize_index(start), static_cast<ssize_t>(0)) - 1;
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
    static consteval size_t rindex() {
        return rfind<self, sub, start, stop>();
    }

    /* Equivalent to Python `str.rjust(width[, fillchar])`. */
    template <bertrand::static_str self, size_t width, char fillchar = ' '>
    static consteval auto rjust() {
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
    static consteval auto rpartition() {
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
    static consteval auto rsplit() {
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
    static consteval auto rstrip() {
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
    static consteval auto split() {
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
    static consteval auto splitlines() {
        constexpr size_t n = [] {
            if constexpr (self.size() == 0) {
                return 0;
            } else {
                size_t total = 1;
                for (size_t i = 0; i < self.size(); ++i) {
                    char c = self[i];
                    if (c == '\r') {
                        ++total;
                        if (self[i + 1] == '\n') {
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
                        char c = self[i];
                        if (c == '\r' && self[i + 1] == '\n') {
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
    static consteval bool startswith() {
        return (
            prefix.size() <= self.size() &&
            std::equal(prefix.buffer, prefix.buffer + prefix.size(), self.buffer)
        );
    }

    /* Equivalent to Python `str.strip([chars])`, but evaluated statically at compile
    time. */
    template <bertrand::static_str self, bertrand::static_str chars = " \t\n\r\f\v">
    static consteval auto strip() {
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
    static consteval auto swapcase() {
        static_str<self.size()> result;
        for (size_t i = 0; i < self.size(); ++i) {
            char c = self[i];
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
    static consteval auto title() {
        static_str<self.size()> result;
        bool capitalize_next = true;
        for (size_t i = 0; i < self.size(); ++i) {
            char c = self[i];
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
    static consteval auto upper() {
        static_str<self.size()> result;
        for (size_t i = 0; i < self.size(); ++i) {
            result.buffer[i] = impl::char_toupper(self[i]);
        }
        result.buffer[self.size()] = '\0';
        return result;
    }

    /* Equivalent to Python `str.zfill(width)`, but evaluated statically at compile
    time. */
    template <bertrand::static_str self, size_t width>
    static consteval auto zfill() {
        if constexpr (width <= self.size()) {
            return self;
        } else {
            static_str<width> result;
            size_t start = 0;
            if (self[0] == '+' || self[0] == '-') {
                result.buffer[0] = self[0];
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
    static consteval auto removeprefix() {
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
    static consteval auto removesuffix() {
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
    friend consteval bool operator<(
        const static_str& self,
        const static_str<M>& other
    ) {
        size_t i = 0;
        while (i < self.size() && i < other.size()) {
            const char x = self[i];
            const char y = other[i];
            if (x < y) {
                return true;
            } else if (y < x) {
                return false;
            }
            ++i;
        }
        return i == self.size() && i != other.size();
    }
    template <size_t M>
    friend consteval bool operator<(
        const static_str& self,
        const char(&other)[M]
    ) {
        size_t i = 0;
        while (i < self.size() && i < (M - 1)) {
            const char x = self[i];
            const char y = other[i];
            if (x < y) {
                return true;
            } else if (y < x) {
                return false;
            }
            ++i;
        }
        return i == self.size() && i != (M - 1);
    }
    template <size_t M>
    friend consteval bool operator<(
        const char(&other)[M],
        const static_str& self
    ) {
        size_t i = 0;
        while (i < self.size() && i < (M - 1)) {
            const char x = other[i];
            const char y = self[i];
            if (x < y) {
                return true;
            } else if (y < x) {
                return false;
            }
            ++i;
        }
        return i == (M - 1) && i != self.size();
    }

    template <size_t M>
    friend consteval bool operator<=(
        const static_str& self,
        const static_str<M>& other
    ) {
        size_t i = 0;
        while (i < self.size() && i < other.size()) {
            const char x = self[i];
            const char y = other[i];
            if (x < y) {
                return true;
            } else if (y < x) {
                return false;
            }
            ++i;
        }
        return i == self.size();
    }
    template <size_t M>
    friend consteval bool operator<=(
        const static_str& self,
        const char(&other)[M]
    ) {
        size_t i = 0;
        while (i < self.size() && i < (M - 1)) {
            const char x = self[i];
            const char y = other[i];
            if (x < y) {
                return true;
            } else if (y < x) {
                return false;
            }
            ++i;
        }
        return i == self.size();
    }
    template <size_t M>
    friend consteval bool operator<=(
        const char(&other)[M],
        const static_str& self
    ) {
        size_t i = 0;
        while (i < self.size() && i < (M - 1)) {
            const char x = other[i];
            const char y = self[i];
            if (x < y) {
                return true;
            } else if (y < x) {
                return false;
            }
            ++i;
        }
        return i == (M - 1);
    }

    template <size_t M>
    friend consteval bool operator==(
        const static_str& self,
        const static_str<M>& other
    ) {
        if constexpr (N == M) {
            for (size_t i = 0; i < N; ++i) {
                if (self[i] != other[i]) {
                    return false;
                }
            }
            return true;
        }
        return false;
    }
    template <size_t M>
    friend consteval bool operator==(
        const static_str& self,
        const char(&other)[M]
    ) {
        if constexpr (N == (M - 1)) {
            for (size_t i = 0; i < N; ++i) {
                if (self[i] != other[i]) {
                    return false;
                }
            }
            return true;
        }
        return false;
    }
    template <size_t M>
    friend consteval bool operator==(
        const char(&other)[M],
        const static_str& self
    ) {
        if constexpr (N == (M - 1)) {
            for (size_t i = 0; i < N; ++i) {
                if (self[i] != other[i]) {
                    return false;
                }
            }
            return true;
        }
    }

    template <size_t M>
    friend consteval bool operator!=(
        const static_str& self,
        const static_str<M>& other
    ) {
        if constexpr (N == M) {
            for (size_t i = 0; i < N; ++i) {
                if (self[i] != other[i]) {
                    return true;
                }
            }
            return false;
        }
        return true;
    }
    template <size_t M>
    friend consteval bool operator!=(
        const static_str& self,
        const char(&other)[M]
    ) {
        if constexpr (N == (M - 1)) {
            for (size_t i = 0; i < N; ++i) {
                if (self[i] != other[i]) {
                    return true;
                }
            }
            return false;
        }
        return true;
    }
    template <size_t M>
    friend consteval bool operator!=(
        const char(&other)[M],
        const static_str& self
    ) {
        if constexpr (N == (M - 1)) {
            for (size_t i = 0; i < N; ++i) {
                if (self[i] != other[i]) {
                    return true;
                }
            }
            return false;
        }
        return true;
    }

    template <size_t M>
    friend consteval bool operator>=(
        const static_str& self,
        const static_str<M>& other
    ) {
        size_t i = 0;
        while (i < self.size() && i < other.size()) {
            const char x = self[i];
            const char y = other[i];
            if (x > y) {
                return true;
            } else if (y > x) {
                return false;
            }
            ++i;
        }
        return i == self.size();
    }
    template <size_t M>
    friend consteval bool operator>=(
        const static_str& self,
        const char(&other)[M]
    ) {
        size_t i = 0;
        while (i < self.size() && i < (M - 1)) {
            const char x = self[i];
            const char y = other[i];
            if (x > y) {
                return true;
            } else if (y > x) {
                return false;
            }
            ++i;
        }
        return i == self.size();
    }
    template <size_t M>
    friend consteval bool operator>=(
        const char(&other)[M],
        const static_str& self
    ) {
        size_t i = 0;
        while (i < self.size() && i < (M - 1)) {
            const char x = other[i];
            const char y = self[i];
            if (x > y) {
                return true;
            } else if (y > x) {
                return false;
            }
            ++i;
        }
        return i == (M - 1);
    }

    template <size_t M>
    friend consteval bool operator>(
        const static_str& self,
        const static_str<M>& other
    ) {
        size_t i = 0;
        while (i < self.size() && i < other.size()) {
            const char x = self[i];
            const char y = other[i];
            if (x > y) {
                return true;
            } else if (y > x) {
                return false;
            }
            ++i;
        }
        return i != self.size() && i == other.size();
    }
    template <size_t M>
    friend consteval bool operator>(
        const static_str& self,
        const char(&other)[M]
    ) {
        size_t i = 0;
        while (i < self.size() && i < (M - 1)) {
            const char x = self[i];
            const char y = other[i];
            if (x > y) {
                return true;
            } else if (y > x) {
                return false;
            }
            ++i;
        }
        return i != self.size() && i == (M - 1);
    }
    template <size_t M>
    friend consteval bool operator>(
        const char(&other)[M],
        const static_str& self
    ) {
        size_t i = 0;
        while (i < self.size() && i < (M - 1)) {
            const char x = other[i];
            const char y = self[i];
            if (x > y) {
                return true;
            } else if (y > x) {
                return false;
            }
            ++i;
        }
        return i != (M - 1) && i == self.size();
    }

    template <size_t M>
    friend consteval static_str<N + M> operator+(
        const static_str<N>& self,
        const static_str<M>& other
    ) {
        static_str<N + M> result;
        std::copy_n(self.buffer, self.size(), result.buffer);
        std::copy_n(other.buffer, other.size(), result.buffer + self.size());
        result.buffer[N + M] = '\0';
        return result;
    }
    template <size_t M>
    friend consteval static_str<N + M - 1> operator+(
        const static_str<N>& self,
        const char(&other)[M]
    ) {
        static_str<N + M - 1> result;
        std::copy_n(self.buffer, self.size(), result.buffer);
        std::copy_n(other, M - 1, result.buffer + self.size());
        result.buffer[N + M - 1] = '\0';
        return result;
    }
    template <size_t M>
    friend consteval static_str<N + M - 1> operator+(
        const char(&other)[M],
        const static_str<N>& self
    ) {
        static_str<N + M - 1> result;
        std::copy_n(other, M - 1, result.buffer);
        std::copy_n(self.buffer, self.size(), result.buffer + M - 1);
        result.buffer[N + M - 1] = '\0';
        return result;
    }

    /// NOTE: due to language limitations, the * operator cannot return another
    /// static_str instance, so it returns a std::string instead.  This is not ideal,
    /// but there is currently no way to inform the compiler that the other operand
    /// must be a compile-time constant, and can therefore be used to determine the
    /// size of the resulting string.

    friend constexpr std::string operator*(const static_str& self, size_t reps) {
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
    friend constexpr std::string operator*(size_t reps, const static_str& self) {
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


/* Customizes the `bertrand::repr()` output for an arbitrary type.  Note that
`bertrand::repr()` is always enabled by default; specializing this struct merely
changes the output.  The default behavior will look for a valid `to_string()` function
either as a member method, an ADL function, or `std::to_string()` as a fallback.  If
none of these are found, `repr()` will attempt to perform stream insertion via the `<<`
operator, and if that fails, will return a string containing the demangled type name
and memory address, similar to Python. */
template <typename Self>
struct __repr__ {
    static constexpr bool enable = true;
    using type = std::string;

    static constexpr std::string operator()(Self obj) {
        if constexpr (meta::has_member_to_string<Self>) {
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
};


/* Get a simple string representation of an arbitrary object.  This function is
functionally equivalent to Python's `repr()` function, but extended to work for
arbitrary C++ types, with possible customization via the `__repr__` control struct. */
template <typename Self>
    requires (__repr__<Self>::enable && (
        std::convertible_to<typename __repr__<Self>::type, std::string> &&
        std::is_invocable_r_v<std::string, __repr__<Self>, Self>
    ))
[[nodiscard]] constexpr std::string repr(Self&& obj) {
    return __repr__<Self>{}(std::forward<Self>(obj));
}


}  // namespace bertrand


namespace std {

    template <bertrand::meta::static_str T>
    struct hash<T> {
        consteval static size_t operator()(const T& str) {
            return bertrand::impl::fnv1a(
                str,
                bertrand::impl::fnv1a_seed,
                bertrand::impl::fnv1a_prime
            );
        }
    };

}


#endif  // BERTRAND_STATIC_STRING_H
