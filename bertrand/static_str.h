#ifndef BERTRAND_STATIC_STRING_H
#define BERTRAND_STATIC_STRING_H

#include <algorithm>
#include <array>
#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <optional>
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
struct StaticStr;


/* CTAD guide allows StaticStr to be used as a template parameter accepting string
literals with arbitrary length. */
template <size_t N>
StaticStr(const char(&)[N]) -> StaticStr<N - 1>;


template <size_t N = 0>
struct StaticStr {
private:

    template <size_t M>
    friend class StaticStr;

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

    template <bertrand::StaticStr self, bertrand::StaticStr chars>
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

    template <bertrand::StaticStr self, bertrand::StaticStr chars>
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

    constexpr StaticStr() = default;

public:
    /* A placeholder index returned when a substring is not present. */
    static constexpr size_t missing = std::numeric_limits<size_t>::max();

    char buffer[N + 1];  // +1 for null terminator

    // consteval StaticStr(const char* arr) : buffer(build(std::make_index_sequence<N>{}, arr)) {}
    consteval StaticStr(const char* arr) {
        []<size_t... Is>(std::index_sequence<Is...>, char* buffer, const char* arr){
            ((buffer[Is] = arr[Is]), ..., (buffer[N] = '\0'));
        }(std::make_index_sequence<N>{}, buffer, arr);
    }

    /* Convert an integer into a string at compile time using the specified base. */
    template <long long num, size_t base = 10> requires (base >= 2 && base <= 36)
    static constexpr auto from_int = [] {
        constexpr const char chars[] = "0123456789abcdefghijklmnopqrstuvwxyz";
        constexpr size_t len = int_length<num, base>;
        StaticStr<len> result;

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
        StaticStr<len> result;

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
    constexpr explicit operator bool() const { return !empty(); }
    constexpr explicit operator std::string() const { return {buffer}; }
    constexpr explicit operator std::string_view() const { return {buffer, N}; }

    constexpr const char* data() const { return buffer; }
    constexpr size_t size() const { return N; }
    constexpr bool empty() const { return !N; }
    Iterator begin() const { return {buffer, 0}; }
    Iterator cbegin() const { return begin(); }
    Iterator end() const { return {nullptr, N}; }
    Iterator cend() const { return end(); }
    ReverseIterator rbegin() const { return {buffer + N - 1, N - 1}; }
    ReverseIterator crbegin() const { return rbegin(); }
    ReverseIterator rend() const { return {nullptr, -1}; }
    ReverseIterator crend() const { return rend(); }

    /* Demangle a string using the compiler's intrinsics. */
    constexpr std::string demangle() const {
        #if defined(__GNUC__) || defined(__clang__)
            int status = 0;
            std::unique_ptr<char, void(*)(void*)> res {
                abi::__cxa_demangle(
                    buffer,
                    nullptr,
                    nullptr,
                    &status
                ),
                std::free
            };
            return (status == 0) ? res.get() : std::string(buffer);
        #elif defined(_MSC_VER)
            char undecorated_name[1024];
            if (UnDecorateSymbolName(
                buffer,
                undecorated_name,
                sizeof(undecorated_name),
                UNDNAME_COMPLETE
            )) {
                return std::string(undecorated_name);
            } else {
                return std::string{buffer, N};
            }
        #else
            return std::string{buffer, N}; // fallback: no demangling
        #endif
    }

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
    template <bertrand::StaticStr self>
    static consteval auto capitalize() {
        StaticStr<self.size()> result;
        bool capitalized = false;
        for (size_t i = 0; i < self.size(); ++i) {
            char c = self[i];
            if (char_isalpha(c)) {
                if (capitalized) {
                    result.buffer[i] = char_tolower(c);
                } else {
                    result.buffer[i] = char_toupper(c);
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
    template <bertrand::StaticStr self, size_t width, char fillchar = ' '>
    static consteval auto center() {
        if constexpr (width <= self.size()) {
            return self;
        } else {
            StaticStr<width> result;
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
        bertrand::StaticStr self,
        bertrand::StaticStr sub,
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
    template <bertrand::StaticStr self, bertrand::StaticStr suffix>
    static consteval bool endswith() {
        return suffix.size() <= self.size() && std::equal(
            suffix.buffer,
            suffix.buffer + suffix.size(),
            self.buffer + self.size() - suffix.size()
        );
    }

    /* Equivalent to Python `str.expandtabs([tabsize])`. */
    template <bertrand::StaticStr self, size_t tabsize = 8>
    static consteval auto expandtabs() {
        constexpr size_t n = count<self, "\t">();
        StaticStr<self.size() - n + n * tabsize> result;
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
    StaticStr<>::missing if the substring is not found. */
    template <
        bertrand::StaticStr self,
        bertrand::StaticStr sub,
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
        bertrand::StaticStr self,
        bertrand::StaticStr sub,
        ssize_t start = 0,
        ssize_t stop = self.size()
    > requires (find<self, sub, start, stop>() != missing)
    static consteval size_t index() {
        return find<self, sub, start, stop>();
    }

    /* Equivalent to Python `str.isalpha()`. */
    template <bertrand::StaticStr self>
    static consteval bool isalpha() {
        for (size_t i = 0; i < self.size(); ++i) {
            if (!char_isalpha(self[i])) {
                return false;
            }
        }
        return self.size() > 0;
    }

    /* Equivalent to Python `str.isalnum()`. */
    template <bertrand::StaticStr self>
    static consteval bool isalnum() {
        for (size_t i = 0; i < self.size(); ++i) {
            if (!char_isalnum(self[i])) {
                return false;
            }
        }
        return self.size() > 0;
    }

    /* Equivalent to Python `str.isascii_()`. */
    template <bertrand::StaticStr self>
    static consteval bool isascii_() {
        for (size_t i = 0; i < self.size(); ++i) {
            if (!char_isascii(self[i])) {
                return false;
            }
        }
        return self.size() > 0;
    }

    /* Equivalent to Python `str.isdigit()`. */
    template <bertrand::StaticStr self>
    static consteval bool isdigit() {
        for (size_t i = 0; i < self.size(); ++i) {
            if (!char_isdigit(self[i])) {
                return false;
            }
        }
        return self.size() > 0;
    }

    /* Equivalent to Python `str.islower()`. */
    template <bertrand::StaticStr self>
    static consteval bool islower() {
        for (size_t i = 0; i < self.size(); ++i) {
            if (!char_islower(self[i])) {
                return false;
            }
        }
        return self.size() > 0;
    }

    /* Equivalent to Python `str.isspace()`. */
    template <bertrand::StaticStr self>
    static consteval bool isspace() {
        for (size_t i = 0; i < self.size(); ++i) {
            if (!char_isspace(self[i])) {
                return false;
            }
        }
        return self.size() > 0;
    }

    /* Equivalent to Python `str.istitle()`. */
    template <bertrand::StaticStr self>
    static consteval bool istitle() {
        bool last_was_delimeter = true;
        for (size_t i = 0; i < self.size(); ++i) {
            char c = self[i];
            if (last_was_delimeter && char_islower(c)) {
                return false;
            }
            last_was_delimeter = char_isdelimeter(c);
        }
        return self.size() > 0;
    }

    /* Equivalent to Python `str.isupper()`. */
    template <bertrand::StaticStr self>
    static consteval bool isupper() {
        for (size_t i = 0; i < self.size(); ++i) {
            if (!char_isupper(self[i])) {
                return false;
            }
        }
        return self.size() > 0;
    }

    /* Equivalent to Python `str.join(strings...)`. */
    template <
        bertrand::StaticStr self,
        bertrand::StaticStr first,
        bertrand::StaticStr... rest
    >
    static consteval auto join() {
        StaticStr<first.size() + (0 + ... + (self.size() + rest.size()))> result;
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
    template <bertrand::StaticStr self, size_t width, char fillchar = ' '>
    static consteval auto ljust() {
        if constexpr (width <= self.size()) {
            return self;
        } else {
            StaticStr<width> result;
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
    template <bertrand::StaticStr self>
    static consteval auto lower() {
        StaticStr<self.size()> result;
        for (size_t i = 0; i < self.size(); ++i) {
            result.buffer[i] = char_tolower(self[i]);
        }
        result.buffer[self.size()] = '\0';
        return result;
    }

    /* Equivalent to Python `str.lstrip([chars])`. */
    template <bertrand::StaticStr self, bertrand::StaticStr chars = " \t\n\r\f\v">
    static consteval auto lstrip() {
        constexpr size_t start = first_non_stripped<self, chars>();
        if constexpr (start == missing) {
            return bertrand::StaticStr{""};
        } else {
            constexpr size_t delta = self.size() - start;
            StaticStr<delta> result;
            std::copy_n(self.buffer + start, delta, result.buffer);
            result.buffer[delta] = '\0';
            return result;
        }
    }

    /* Equivalent to Python `str.partition(sep)`. */
    template <bertrand::StaticStr self, bertrand::StaticStr sep>
    static consteval auto partition() {
        constexpr size_t index = find<self, sep>();
        if constexpr (index == missing) {
            return std::make_tuple(
                self,
                bertrand::StaticStr{""},
                bertrand::StaticStr{""}
            );
        } else {
            constexpr size_t remaining = self.size() - index - sep.size();
            StaticStr<index> first;
            StaticStr<remaining> third;
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
        bertrand::StaticStr self,
        bertrand::StaticStr sub,
        bertrand::StaticStr repl,
        size_t max_count = missing
    >
    static consteval auto replace() {
        constexpr size_t freq = count<self, sub>();
        constexpr size_t n = freq < max_count ? freq : max_count;
        StaticStr<self.size() - (n * sub.size()) + (n * repl.size())> result;
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
    StaticStr<>::missing if the substring is not found. */
    template <
        bertrand::StaticStr self,
        bertrand::StaticStr sub,
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
        bertrand::StaticStr self,
        bertrand::StaticStr sub,
        ssize_t start = 0,
        ssize_t stop = self.size()
    > requires (rfind<self, sub, start, stop>() != missing)
    static consteval size_t rindex() {
        return rfind<self, sub, start, stop>();
    }

    /* Equivalent to Python `str.rjust(width[, fillchar])`. */
    template <bertrand::StaticStr self, size_t width, char fillchar = ' '>
    static consteval auto rjust() {
        if constexpr (width <= self.size()) {
            return self;
        } else {
            StaticStr<width> result;
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
    template <bertrand::StaticStr self, bertrand::StaticStr sep>
    static consteval auto rpartition() {
        constexpr size_t index = rfind<self, sep>();
        if constexpr (index == missing) {
            return std::make_tuple(
                self,
                bertrand::StaticStr{""},
                bertrand::StaticStr{""}
            );
        } else {
            constexpr size_t remaining = self.size() - index - sep.size();
            StaticStr<index> first;
            StaticStr<remaining> third;
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
        bertrand::StaticStr self,
        bertrand::StaticStr sep,
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
                std::tuple<StaticStr<std::get<Is>(strides)>...> result;
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
    template <bertrand::StaticStr self, bertrand::StaticStr chars = " \t\n\r\f\v">
    static consteval auto rstrip() {
        constexpr size_t stop = last_non_stripped<self, chars>();
        if constexpr (stop == missing) {
            return bertrand::StaticStr{""};
        } else {
            constexpr size_t delta = stop + 1;
            StaticStr<delta> result;
            std::copy_n(self.buffer, delta, result.buffer);
            result.buffer[delta] = '\0';
            return result;
        }
    }

    /* Equivalent to Python `str.split(sep[, maxsplit])`. */
    template <
        bertrand::StaticStr self,
        bertrand::StaticStr sep,
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
                std::tuple<StaticStr<std::get<Is>(strides)>...> result;
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
    template <bertrand::StaticStr self, bool keepends = false>
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
                    } else if (char_islinebreak(c)) {
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
                        } else if (char_islinebreak(c)) {
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
                std::tuple<StaticStr<std::get<Is>(strides)>...> result;
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
    template <bertrand::StaticStr self, bertrand::StaticStr prefix>
    static consteval bool startswith() {
        return (
            prefix.size() <= self.size() &&
            std::equal(prefix.buffer, prefix.buffer + prefix.size(), self.buffer)
        );
    }

    /* Equivalent to Python `str.strip([chars])`, but evaluated statically at compile
    time. */
    template <bertrand::StaticStr self, bertrand::StaticStr chars = " \t\n\r\f\v">
    static consteval auto strip() {
        constexpr size_t start = first_non_stripped<self, chars>();
        if constexpr (start == missing) {
            return bertrand::StaticStr{""};
        } else {
            constexpr size_t stop = last_non_stripped<self, chars>();
            constexpr size_t delta = stop - start + 1;  // +1 for half-open interval
            StaticStr<delta> result;
            std::copy_n(self.buffer + start, delta, result.buffer);
            result.buffer[delta] = '\0';
            return result;
        }
    }

    /* Equivalent to Python `str.swapcase()`, but evaluated statically at compile
    time. */
    template <bertrand::StaticStr self>
    static consteval auto swapcase() {
        StaticStr<self.size()> result;
        for (size_t i = 0; i < self.size(); ++i) {
            char c = self[i];
            if (char_islower(c)) {
                result.buffer[i] = char_toupper(c);
            } else if (char_isupper(c)) {
                result.buffer[i] = char_tolower(c);
            } else {
                result.buffer[i] = c;
            }
        }
        result.buffer[self.size()] = '\0';
        return result;
    }

    /* Equivalent to Python `str.title()`, but evaluated statically at compile time. */
    template <bertrand::StaticStr self>
    static consteval auto title() {
        StaticStr<self.size()> result;
        bool capitalize_next = true;
        for (size_t i = 0; i < self.size(); ++i) {
            char c = self[i];
            if (char_isalpha(c)) {
                if (capitalize_next) {
                    result.buffer[i] = char_toupper(c);
                    capitalize_next = false;
                } else {
                    result.buffer[i] = char_tolower(c);
                }
            } else {
                if (char_isdelimeter(c)) {
                    capitalize_next = true;
                }
                result.buffer[i] = c;
            }
        }
        result.buffer[self.size()] = '\0';
        return result;
    }

    /* Equivalent to Python `str.upper()`, but evaluated statically at compile time. */
    template <bertrand::StaticStr self>
    static consteval auto upper() {
        StaticStr<self.size()> result;
        for (size_t i = 0; i < self.size(); ++i) {
            result.buffer[i] = char_toupper(self[i]);
        }
        result.buffer[self.size()] = '\0';
        return result;
    }

    /* Equivalent to Python `str.zfill(width)`, but evaluated statically at compile
    time. */
    template <bertrand::StaticStr self, size_t width>
    static consteval auto zfill() {
        if constexpr (width <= self.size()) {
            return self;
        } else {
            StaticStr<width> result;
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
    template <bertrand::StaticStr self, bertrand::StaticStr prefix>
    static consteval auto removeprefix() {
        if constexpr (startswith<self, prefix>()) {
            StaticStr<self.size() - prefix.size()> result;
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
    template <bertrand::StaticStr self, bertrand::StaticStr suffix>
    static consteval auto removesuffix() {
        if constexpr (endswith<self, suffix>()) {
            StaticStr<self.size() - suffix.size()> result;
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
        const StaticStr& self,
        const StaticStr<M>& other
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
        const StaticStr& self,
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
        const StaticStr& self
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
        const StaticStr& self,
        const StaticStr<M>& other
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
        const StaticStr& self,
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
        const StaticStr& self
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
        const StaticStr& self,
        const StaticStr<M>& other
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
        const StaticStr& self,
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
        const StaticStr& self
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
        const StaticStr& self,
        const StaticStr<M>& other
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
        const StaticStr& self,
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
        const StaticStr& self
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
        const StaticStr& self,
        const StaticStr<M>& other
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
        const StaticStr& self,
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
        const StaticStr& self
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
        const StaticStr& self,
        const StaticStr<M>& other
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
        const StaticStr& self,
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
        const StaticStr& self
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
    friend consteval StaticStr<N + M> operator+(
        const StaticStr<N>& self,
        const StaticStr<M>& other
    ) {
        StaticStr<N + M> result;
        std::copy_n(self.buffer, self.size(), result.buffer);
        std::copy_n(other.buffer, other.size(), result.buffer + self.size());
        result.buffer[N + M] = '\0';
        return result;
    }
    template <size_t M>
    friend consteval StaticStr<N + M - 1> operator+(
        const StaticStr<N>& self,
        const char(&other)[M]
    ) {
        StaticStr<N + M - 1> result;
        std::copy_n(self.buffer, self.size(), result.buffer);
        std::copy_n(other, M - 1, result.buffer + self.size());
        result.buffer[N + M - 1] = '\0';
        return result;
    }
    template <size_t M>
    friend consteval StaticStr<N + M - 1> operator+(
        const char(&other)[M],
        const StaticStr<N>& self
    ) {
        StaticStr<N + M - 1> result;
        std::copy_n(other, M - 1, result.buffer);
        std::copy_n(self.buffer, self.size(), result.buffer + M - 1);
        result.buffer[N + M - 1] = '\0';
        return result;
    }

    /// NOTE: due to language limitations, the * operator cannot return another
    /// StaticStr instance, so it returns a std::string instead.  This is not ideal,
    /// but there is currently no way to inform the compiler that the other operand
    /// must be a compile-time constant, and can therefore be used to determine the
    /// size of the resulting string.

    friend constexpr std::string operator*(const StaticStr& self, size_t reps) {
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
    friend constexpr std::string operator*(size_t reps, const StaticStr& self) {
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
        return StaticStr<N>{name.data()};
    }

    template <typename>
    constexpr bool _static_str = false;
    template <size_t N>
    constexpr bool _static_str<bertrand::StaticStr<N>> = true;
    template <typename T>
    concept static_str = _static_str<std::remove_cvref_t<T>>;

    template <StaticStr...>
    constexpr bool _strings_are_unique = true;
    template <StaticStr First, StaticStr... Rest>
    constexpr bool _strings_are_unique<First, Rest...> =
        ((First != Rest) && ...) && _strings_are_unique<Rest...>;
    template <StaticStr... Strings>
    concept strings_are_unique = _strings_are_unique<Strings...>;

    template <size_t I, StaticStr... Strings>
    struct unpack_string;
    template <StaticStr First, StaticStr... Rest>
    struct unpack_string<0, First, Rest...> {
        static constexpr StaticStr value = First;
    };
    template <size_t I, StaticStr First, StaticStr... Rest>
    struct unpack_string<I, First, Rest...> {
        static constexpr StaticStr value = unpack_string<I - 1, Rest...>::value;
    };

    /* A helper struct that computes a perfect FNV-1a hash function over the given
    strings at compile time. */
    template <StaticStr... Keys>
    struct perfect_hash {
    private:
        static constexpr std::pair<size_t, size_t> minmax =
            std::minmax({Keys.size()...});

    public:
        static constexpr size_t table_size = next_prime(
            sizeof...(Keys) + (sizeof...(Keys) >> 1)
        );
        static constexpr size_t min_length = minmax.first;
        static constexpr size_t max_length = minmax.second;

    private:
        /* Check to see if the candidate seed and prime produce any collisions for the
        target keyword arguments. */
        template <StaticStr...>
        struct collisions {
            static constexpr bool operator()(size_t, size_t) {
                return false;
            }
        };
        template <StaticStr First, StaticStr... Rest>
        struct collisions<First, Rest...> {
            template <StaticStr...>
            struct scan {
                static constexpr bool operator()(size_t, size_t, size_t) {
                    return false;
                }
            };
            template <StaticStr F, StaticStr... Rs>
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
    template <StaticStr... Keys>
    struct minimal_perfect_hash {
    private:
        static constexpr std::pair<size_t, size_t> minmax =
            std::minmax({Keys.size()...});

    public:
        static constexpr size_t table_size = sizeof...(Keys);
        static constexpr size_t min_length = minmax.first;
        static constexpr size_t max_length = minmax.second;

    private:
        using Weights = std::array<unsigned char, 256>;

        template <StaticStr...>
        struct _counts {
            static constexpr size_t operator()(unsigned char, size_t) {
                return 0;
            }
        };
        template <StaticStr First, StaticStr... Rest>
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

        template <size_t I, unsigned char C, StaticStr... Strings>
        static constexpr size_t first_occurrence = 0;
        template <size_t I, unsigned char C, StaticStr First, StaticStr... Rest>
        static constexpr size_t first_occurrence<I, C, First, Rest...> =
            (I < First.size() && First[I] == C) ?
                0 : first_occurrence<I, C, Rest...> + 1;

        template <size_t I, size_t J, StaticStr... Strings>
        static constexpr size_t _variation = 0;
        template <size_t I, size_t J, StaticStr First, StaticStr... Rest>
        static constexpr size_t _variation<I, J, First, Rest...> =
            (I < First.size() && J == first_occurrence<I, First[I], Keys...>) +
            _variation<I, J + 1, Rest...>;
        template <size_t I, StaticStr... Strings>
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
        template <StaticStr...>
        struct collisions {
            static constexpr collision operator()(const Weights&, size_t) {
                return {"", ""};
            }
        };
        template <StaticStr First, StaticStr... Rest>
        struct collisions<First, Rest...> {
            template <StaticStr...>
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
            template <StaticStr F, StaticStr... Rs>
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
            constexpr size_t max_iterations = 1000;
            Weights weights;

            for (size_t i = 0; i <= max_length; ++i) {
                weights.fill(1);
                for (size_t j = 0; j < max_iterations; ++j) {
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

        /* Hash a character buffer according to the computed perfect hash algorithm. */
        static constexpr size_t hash(const char* str) noexcept {
            /// TODO: it might be possible to use an extended bitmask with length
            /// equal to max_length, with 1s at the positions of the significant
            /// characters, and 0s elsewhere.  I would then load up to 8 characters
            /// at a time, and use the bitmask to extract the significant characters
            /// and look up their associative values, rather than doing a linear scan.
            if constexpr (positions.empty()) {
                return 0;
            }
            const char* ptr = str;
            size_t out = 0;
            size_t i = 0;
            size_t next_pos = positions[i];
            while (*ptr != '\0') {
                if ((ptr - str) == next_pos) {
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
        static constexpr size_t hash(const char* str, size_t& len) noexcept {
            const char* ptr = str;
            if constexpr (positions.empty()) {
                while (*ptr != '\0') { ++ptr; }
                len = ptr - str;
                return 0;
            }
            size_t out = 0;
            size_t i = 0;
            size_t next_pos = positions[i];
            while (*ptr != '\0') {
                if ((ptr - str) == next_pos) {
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
            len = ptr - str;
            return out;
        }

        /* Hash a string view according to the computed perfect hash algorithm. */
        static constexpr size_t hash(std::string_view str) noexcept {
            size_t out = 0;
            for (size_t pos : positions) {
                unsigned char c = pos < str.size() ? str[pos] : 0;
                out += weights[c];
            }
            return out;
        }

        /* Hash a compile-time string according to the computed perfect hash algorithm. */
        template <impl::static_str Key>
        static constexpr size_t hash(const Key& str) noexcept {
            size_t out = 0;
            for (size_t pos : positions) {
                unsigned char c = pos < str.size() ? str[pos] : 0;
                out += weights[c];
            }
            return out;
        }
    };

}


/* Gets a C++ type name as a fully-qualified, demangled string computed entirely
at compile time.  The underlying buffer is baked directly into the final binary. */
template <typename T>
constexpr auto type_name = impl::type_name_impl<T>();


/* A compile-time perfect hash table with a finite set of static strings as keys.  The
data structure will compute a perfect hash function for the given strings at compile
time, and will store the values in a fixed-size array that can be baked into the final
binary.

Searching the map is extremely fast even in the worst case, consisting only of a
perfect FNV-1a hash, a single array lookup, and a string comparison to validate.  No
collision resolution is necessary, due to the perfect hash function.  If the search
string is also known at compile time, then even these can be optimized out, skipping
straight to the final value with no intermediate computation. */
template <typename Value, StaticStr... Keys>
    requires (
        impl::strings_are_unique<Keys...> &&
        impl::minimal_perfect_hash<Keys...>::exists
    )
struct StaticMap : impl::minimal_perfect_hash<Keys...> {
private:
    using Hash = impl::minimal_perfect_hash<Keys...>;

    struct Bucket {
        std::string_view key;
        Value value;
    };

    using Table = std::array<Bucket, Hash::table_size>;

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
        using value_type = std::pair<const std::string_view&, const Value&>;
        using pointer = std::pair<const std::string_view&, const Value&>*;
        using reference = std::pair<const std::string_view&, const Value&>&;

        Iterator(const Table& table) : m_table(&table), m_idx(0) {}
        Iterator(const Table& table, Sentinel) :
            m_table(&table), m_idx(Hash::table_size)
        {}

        value_type operator*() const {
            const Bucket& bucket = (*m_table)[m_idx];
            return {bucket.key, bucket.value};
        }

        pointer operator->() const {
            return &operator*();
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

        friend bool operator==(const Iterator& self, Sentinel) {
            return self.m_idx == Hash::table_size;
        }

        friend bool operator==(Sentinel, const Iterator& self) {
            return self.m_idx == Hash::table_size;
        }

        friend bool operator!=(const Iterator& self, Sentinel) {
            return self.m_idx != Hash::table_size;
        }

        friend bool operator!=(Sentinel, const Iterator& self) {
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
    constexpr StaticMap(Values&&... values) :
        table([]<size_t... Is>(std::index_sequence<Is...>, auto&&... values) {
            return Table{populate<Is, (Hash::hash(Keys) % Hash::table_size)...>(
                std::forward<decltype(values)>(values)...
            )...};
        }(
            std::make_index_sequence<Hash::table_size>{},
            std::forward<Values>(values)...
        ))
    {}

    /* Check whether the map contains an arbitrary key at compile time. */
    template <StaticStr Key>
    static constexpr bool contains() {
        return ((Key == Keys) || ...);
    }

    /* Check whether the map contains an arbitrary key. */
    constexpr bool contains(const char* key) const {
        size_t len;
        const Bucket& result = table[Hash::hash(key, len) % Hash::table_size];
        if (len == result.key.size()) {
            for (size_t i = 0; i < len; ++i) {
                if (key[i] != result.key[i]) {
                    return false;
                }
            }
            return true;
        }
        return false;
    }

    /* Check whether the map contains an arbitrary key. */
    constexpr bool contains(std::string_view key) const {
        const Bucket& result = table[Hash::hash(key) % Hash::table_size];
        if (key == result.key) {
            return true;
        }
        return false;
    }

    /* Check whether the map contains an arbitrary key. */
    template <impl::static_str Key>
    constexpr bool contains(const Key& key) const {
        const Bucket& result = table[Hash::hash(key) % Hash::table_size];
        if (key == result.key) {
            return true;
        }
        return false;
    }

    /* Get the value associated with a key at compile time, asserting that it is
    present in the map. */
    template <StaticStr Key> requires (contains<Key>())
    constexpr const Value& get() const {
        constexpr size_t idx = Hash::hash(Key) % Hash::table_size;
        return *table[idx];
    }

    /* Get the value associated with a key at compile time, asserting that it is
    present in the map. */
    template <StaticStr Key> requires (contains<Key>())
    Value& get() {
        constexpr size_t idx = Hash::hash(Key) % Hash::table_size;
        return *table[idx];
    }

    /* Look up a key, returning a pointer to the corresponding value or nullptr if it
    is not present. */
    constexpr const Value* operator[](const char* key) const {
        size_t len;
        const Bucket& result = table[Hash::hash(key, len) % Hash::table_size];
        if (len == result.key.size()) {
            for (size_t i = 0; i < len; ++i) {
                if (key[i] != result.key[i]) {
                    return nullptr;
                }
            }
            return &result.value;
        }
        return nullptr;
    }

    /* Look up a key, returning a pointer to the corresponding value or nullptr if it
    is not present. */
    constexpr const Value* operator[](std::string_view key) const {
        const Bucket& result = table[Hash::hash(key) % Hash::table_size];
        if (key == result.key) {
            return &result.value;
        }
        return nullptr;
    }

    /* Look up a key, returning a pointer to the corresponding value or nullptr if it
    is not present. */
    template <impl::static_str Key>
    constexpr const Value* operator[](const Key& key) const {
        const Bucket& result = table[Hash::hash(key) % Hash::table_size];
        if (key == result.key) {
            return &result.value;
        }
        return nullptr;
    }


    /* Look up a key, returning a pointer to the corresponding value or nullptr if it
    is not present. */
    Value* operator[](const char* key) {
        size_t len;
        Bucket& result = table[Hash::hash(key, len) % Hash::table_size];
        if (len == result.key.size()) {
            for (size_t i = 0; i < result.key.size(); ++i) {
                if (key[i] != result.key[i]) {
                    return nullptr;
                }
            }
            return &result.value;
        }
        return nullptr;
    }

    /* Look up a key, returning a pointer to the corresponding value or nullptr if it
    is not present. */
    Value* operator[](std::string_view key) {
        Bucket& result = table[Hash::hash(key) % Hash::table_size];
        if (key == result.key) {
            return &result.value;
        }
        return nullptr;
    }

    /* Look up a key, returning a pointer to the corresponding value or nullptr if it
    is not present. */
    template <impl::static_str Key>
    Value* operator[](const Key& key) {
        Bucket& result = table[Hash::hash(key) % Hash::table_size];
        if (key == result.key) {
            return &result.value;
        }
        return nullptr;
    }

    constexpr size_t size() const { return sizeof...(Keys); }
    constexpr bool empty() const { return size() == 0; }
    Iterator begin() const { return {table}; }
    Iterator cbegin() const { return {table}; }
    Sentinel end() const { return {}; }
    Sentinel cend() const { return {}; }
};


/* A specialization of StaticMap that does not hold any values.  Such a data structure
is equivalent to a perfectly-hashed set of compile-time strings, which can be
efficiently searched at runtime.  Rather than dereferencing to a value, the buckets
and iterators will dereference to `string_view`s of the templated key buffers. */
template <StaticStr... Keys>
    requires (
        impl::strings_are_unique<Keys...> &&
        impl::minimal_perfect_hash<Keys...>::exists
    )
struct StaticMap<void, Keys...> : impl::minimal_perfect_hash<Keys...> {
private:
    using Hash = impl::minimal_perfect_hash<Keys...>;
    using Table = std::array<std::string_view, Hash::table_size>;

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
        Iterator(const Table& table, Sentinel) :
            m_table(&table), m_idx(Hash::table_size)
        {}

        reference operator*() const {
            return (*m_table)[m_idx];
        }

        pointer operator->() const {
            return &operator*();
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

        friend bool operator==(const Iterator& self, Sentinel) {
            return self.m_idx == Hash::table_size;
        }

        friend bool operator==(Sentinel, const Iterator& self) {
            return self.m_idx == Hash::table_size;
        }

        friend bool operator!=(const Iterator& self, Sentinel) {
            return self.m_idx != Hash::table_size;
        }

        friend bool operator!=(Sentinel, const Iterator& self) {
            return self.m_idx != Hash::table_size;
        }
    };

public:
    Table table;

    constexpr StaticMap() :
        table([]<size_t... Is>(std::index_sequence<Is...>) {
            return Table{populate<Is, (Hash::hash(Keys) % Hash::table_size)...>()...};
        }(std::make_index_sequence<Hash::table_size>{}))
    {}

    /* Check whether the map contains an arbitrary key at compile time. */
    template <StaticStr Key>
    static constexpr bool contains() {
        return ((Key == Keys) || ...);
    }

    /* Check whether the map contains an arbitrary key. */
    constexpr bool contains(const char* key) const {
        size_t len;
        const std::string_view& result = table[Hash::hash(key, len) % Hash::table_size];
        if (len == result.size()) {
            for (size_t i = 0; i < len; ++i) {
                if (key[i] != result[i]) {
                    return false;
                }
            }
            return true;
        }
        return false;
    }

    /* Check whether the map contains an arbitrary key. */
    constexpr bool contains(std::string_view key) const {
        const std::string_view& result = table[Hash::hash(key) % Hash::table_size];
        if (key == result) {
            return true;
        }
        return false;
    }

    /* Check whether the map contains an arbitrary key. */
    template <impl::static_str Key>
    constexpr bool contains(const Key& key) const {
        const std::string_view& result = table[Hash::hash(key) % Hash::table_size];
        if (key == result) {
            return true;
        }
        return false;
    }

    /* Get the value associated with a key at compile time, asserting that it is
    present in the map. */
    template <StaticStr Key> requires (contains<Key>())
    constexpr const std::string_view& get() const {
        constexpr size_t idx = Hash::hash(Key) % Hash::table_size;
        return *table[idx];
    }

    /* Look up a key, returning a pointer to the corresponding key or nullptr if it is
    not present. */
    constexpr const std::string_view* operator[](const char* key) const {
        size_t len;
        const std::string_view& result = table[Hash::hash(key, len) % Hash::table_size];
        if (len == result.size()) {
            for (size_t i = 0; i < len; ++i) {
                if (key[i] != result[i]) {
                    return nullptr;
                }
            }
            return &result;
        }
        return nullptr;
    }

    /* Look up a key, returning a pointer to the corresponding key or nullptr if it is
    not present. */
    constexpr const std::string_view* operator[](std::string_view key) const {
        const std::string_view& result = table[Hash::hash(key) % Hash::table_size];
        if (key == result) {
            return &result;
        }
        return nullptr;
    }

    /* Look up a key, returning a pointer to the corresponding key or nullptr if it is
    not present. */
    template <impl::static_str Key>
    constexpr const std::string_view* operator[](const Key& key) const {
        const std::string_view& result = table[Hash::hash(key) % Hash::table_size];
        if (key == result) {
            return &result;
        }
        return nullptr;
    }

    constexpr size_t size() const { return sizeof...(Keys); }
    constexpr bool empty() const { return size() == 0; }
    Iterator begin() const { return {table}; }
    Iterator cbegin() const { return {table}; }
    Sentinel end() const { return {}; }
    Sentinel cend() const { return {}; }
};


template <StaticStr... Keys>
using StaticSet = StaticMap<void, Keys...>;


}  // namespace bertrand


namespace std {

    template <bertrand::impl::static_str T>
    struct hash<T> {
        consteval static size_t operator()(const T& str) {
            return bertrand::fnv1a(
                str,
                bertrand::fnv1a_seed,
                bertrand::fnv1a_prime
            );
        }
    };

    /* `std::get<"name">(dict)` is a type-safe accessor for `bertrand::StaticMap`. */
    template <bertrand::StaticStr Key, typename Value, bertrand::StaticStr... Keys>
        requires (
            bertrand::StaticMap<Value, Keys...>::template contains<Key>() &&
            !std::is_void_v<Value>
        )
    constexpr const Value& get(const bertrand::StaticMap<Value, Keys...>& dict) {
        return dict.template get<Key>();
    }

    /* `std::get<"name">(dict)` is a type-safe accessor for `bertrand::StaticMap`. */
    template <bertrand::StaticStr Key, typename Value, bertrand::StaticStr... Keys>
        requires (
            bertrand::StaticMap<Value, Keys...>::template contains<Key>() &&
            !std::is_void_v<Value>
        )
    Value& get(bertrand::StaticMap<Value, Keys...>& dict) {
        return dict.template get<Key>();
    }

    /* `std::get<"name">(set)` is a type-safe accessor for `bertrand::StaticSet`. */
    template <bertrand::StaticStr Key, bertrand::StaticStr... Keys>
        requires (bertrand::StaticSet<Keys...>::template contains<Key>())
    constexpr const std::string_view& get(const bertrand::StaticSet<Keys...>& set) {
        return set.template get<Key>();
    }

}


#endif  // BERTRAND_STATIC_STRING_H
