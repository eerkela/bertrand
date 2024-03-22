#ifndef BERTRAND_COMPILE_TIME_STRING_H
#define BERTRAND_COMPILE_TIME_STRING_H

#include <array>
#include <cstddef>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>




namespace bertrand {


/* C++20 expands support for non-type template parameters, including compile-time
strings.  This helper class allows arbitrary string literals to be encoded directly
as template parameters, and for them to be manipulated entirely at compile-time using
the familiar Python string interface.  Note that for now, this only works for simple
ASCII strings, but it could theoretically be extended to handle Unicode in the
future. */
template <size_t N>
class StaticStr {

    template <size_t M>
    friend class StaticStr;

    struct Cases {

        static constexpr char LOWER_TO_UPPER = 'A' - 'a';
        static constexpr char UPPER_TO_LOWER = 'a' - 'A';

        static constexpr bool islower(char c) {
            return c >= 'a' && c <= 'z';
        }

        static constexpr bool isupper(char c) {
            return c >= 'A' && c <= 'Z';
        }

        static constexpr bool isalpha(char c) {
            return islower(c) || isupper(c);
        }

        static constexpr bool isdigit(char c) {
            return c >= '0' && c <= '9';
        }

        static constexpr bool isalnum(char c) {
            return isalpha(c) || isdigit(c);
        }

        static constexpr bool isascii(char c) {
            return c >= 0 && c <= 127;
        }

        static constexpr bool isspace(char c) {
            return c == ' ' || (c >= '\t' && c <= '\r');
        }

        /* Check if a character delimits a word boundary.  NOTE: this is only stable
        for ASCII-encoded text, which most string literals should be. */
        static constexpr bool is_delimeter(char c) {
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

        static constexpr char tolower(char c) {
            return isupper(c) ? c + UPPER_TO_LOWER : c;
        }

        static constexpr char toupper(char c) {
            return islower(c) ? c + LOWER_TO_UPPER : c;
        }



    };

    struct ReverseIterator {
        const char* ptr;
        ssize_t index;

    public:
        using iterator_category = std::input_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = const char;
        using pointer = value_type*;
        using reference = value_type&;

        ReverseIterator(const char* ptr, ssize_t index) : ptr(ptr), index(index) {}
        ReverseIterator(const ReverseIterator& other) : ptr(other.ptr), index(other.index) {}
        ReverseIterator(ReverseIterator&& other) : ptr(other.ptr), index(other.index) {}
        ReverseIterator& operator=(const ReverseIterator& other) {
            ptr = other.ptr;
            index = other.index;
            return *this;
        }
        ReverseIterator& operator=(ReverseIterator&& other) {
            ptr = other.ptr;
            index = other.index;
            return *this;
        }

        value_type operator*() const {
            if (ptr == nullptr) {
                throw std::out_of_range("Cannot dereference end iterator");
            }
            return *ptr;
        }

        ReverseIterator& operator++() {
            if (--index >= 0) {
                --ptr;
            } else {
                ptr = nullptr;
            }
            return *this;
        }

        ReverseIterator operator++(int) {
            ReverseIterator copy = *this;
            ++(*this);
            return copy;
        }

        bool operator==(const ReverseIterator& other) const {
            return ptr == other.ptr;
        }

        bool operator!=(const ReverseIterator& other) const {
            return ptr != other.ptr;
        }

    };

    template <const char* buffer, size_t M, size_t O, size_t start = 0, size_t stop = M>
    static constexpr size_t count_impl(StaticStr<O> sub) {
        if constexpr ((stop - start) < M) {
            return 0;
        }
        size_t count = 0;
        for (size_t i = start; i < stop - M; ++i) {
            if (std::equal(sub.buffer, sub.buffer + M, buffer + i)) {
                ++count;
            }
        }
        return count;
    }

public:
    char buffer[N + 1];  // + 1 for null terminator

    static constexpr size_t not_found = std::numeric_limits<size_t>::max();

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    constexpr StaticStr() = default;

    constexpr StaticStr(const char(&arr)[N + 1]) {
        std::copy_n(arr, N + 1, buffer);
    }

    constexpr StaticStr(const StaticStr& other) {
        std::copy_n(other.buffer, N + 1, buffer);
    }

    ///////////////////////////
    ////    CONVERSIONS    ////
    ///////////////////////////

    inline operator const char*() const {
        return buffer;
    }

    inline operator std::string() const {
        return {buffer, N};
    }

    inline operator std::string_view() const {
        return {buffer, N};
    }

    ////////////////////////////////
    ////    STRING INTERFACE    ////
    ////////////////////////////////

    /* Equivalent to Python `str.capitalize()`, but computed at compile time. */
    constexpr StaticStr<N> capitalize() const {
        StaticStr<N> result;
        bool capitalized = false;
        for (size_t i = 0; i < N; ++i) {
            char c = buffer[i];
            if (Cases::isalpha(c)) {
                if (capitalized) {
                    result.buffer[i] = Cases::tolower(c);
                } else {
                    result.buffer[i] = Cases::toupper(c);
                    capitalized = true;
                }
            } else {
                result.buffer[i] = c;
            }
        }
        result.buffer[N] = '\0';
        return result;
    }

    /* Equivalent to Python `str.center()`, but computed at compile time. */
    template <size_t width>
    constexpr StaticStr<width <= N ? N : width> center(char fillchar=' ') const {
        if constexpr (width <= N) {
            return *this;
        } else {
            StaticStr<width> result;
            size_t left = (width - N) / 2;
            size_t right = width - N - left;
            std::fill_n(result.buffer, left, fillchar);
            std::copy_n(buffer, N, result.buffer + left);
            std::fill_n(result.buffer + left + N, right, fillchar);
            result.buffer[width] = '\0';
            return result;
        }
    }

    /* Equivalent to Python `str.count()`, but computed at compile time. */
    template <size_t start = 0, size_t stop = N, size_t M>
    constexpr size_t count(const StaticStr<M>& sub) const {
        static_assert(
            (start >= 0 && start < N) && (stop >= start && stop <= N),
            "start must be less than or equal to stop and both must be within the "
            "bounds of the string"
        );
        if constexpr ((stop - start) < M) {
            return 0;
        }
        size_t count = 0;
        for (size_t i = start; i < stop - M; ++i) {
            if (std::equal(sub.buffer, sub.buffer + M, buffer + i)) {
                ++count;
            }
        }
        return count;
    }

    /* Equivalent to Python `str.endswith()`, but computed at compile time. */
    template <size_t M>
    constexpr bool endswith(const StaticStr<M>& suffix) const {
        if (M > N) {
            return false;
        }
        return std::equal(suffix.buffer, suffix.buffer + M, buffer + N - M);
    }

    /* Equivalent to Python `str.find()`, but computed at compile time. */
    template <size_t start = 0, size_t stop = N, size_t M>
    constexpr size_t find(const StaticStr<M>& sub) {
        static_assert(
            (start >= 0 && start < N) && (stop >= start && stop <= N),
            "start must be less than or equal to stop and both must be within the "
            "bounds of the string"
        );
        if constexpr ((stop - start) < M) {
            return 0;
        }
        for (size_t i = start; i < stop - M; ++i) {
            if (std::equal(sub.buffer, sub.buffer + M, buffer + i)) {
                return i;
            }
        }
        return not_found;
    }

    /* Equivalent to Python `str.isalpha()`, but computed at compile time. */
    constexpr bool isalpha() const {
        for (size_t i = 0; i < N; ++i) {
            if (!Cases::isalpha(buffer[i])) {
                return false;
            }
        }
        return N > 0;
    }

    /* Equivalent to Python `str.isalnum()`, but computed at compile time. */
    constexpr bool isalnum() const {
        for (size_t i = 0; i < N; ++i) {
            if (!Cases::isalnum(buffer[i])) {
                return false;
            }
        }
        return N > 0;
    }

    /* Equivalent to Python `str.isascii()`, but computed at compile time. */
    constexpr bool isascii() const {
        for (size_t i = 0; i < N; ++i) {
            if (!Cases::isascii(buffer[i])) {
                return false;
            }
        }
        return N > 0;
    }

    /* Equivalent to Python `str.isdigit()`, but computed at compile time. */
    constexpr bool isdigit() const {
        for (size_t i = 0; i < N; ++i) {
            if (!Cases::isdigit(buffer[i])) {
                return false;
            }
        }
        return N > 0;
    }

    /* Equivalent to Python `str.islower()`, but computed at compile time. */
    constexpr bool islower() const {
        for (size_t i = 0; i < N; ++i) {
            if (!Cases::islower(buffer[i])) {
                return false;
            }
        }
        return N > 0;
    }

    /* Equivalent to Python `str.isspace()`, but computed at compile time. */
    constexpr bool isspace() const {
        for (size_t i = 0; i < N; ++i) {
            if (!Cases::isspace(buffer[i])) {
                return false;
            }
        }
        return N > 0;
    }

    /* Equivalent to Python `str.istitle()`, but computed at compile time. */
    constexpr bool istitle() const {
        bool last_was_delimeter = true;
        for (size_t i = 0; i < N; ++i) {
            char c = buffer[i];
            if (last_was_delimeter && Cases::islower(c)) {
                return false;
            }
            last_was_delimeter = Cases::is_delimeter(c);
        }
        return N > 0;
    }

    /* Equivalent to Python `str.isupper()`, but computed at compile time. */
    constexpr bool isupper() const {
        for (size_t i = 0; i < N; ++i) {
            if (!Cases::isupper(buffer[i])) {
                return false;
            }
        }
        return N > 0;
    }

    /* Equivalent to Python `str.join()`, but computed at compile time. */
    template <size_t M, size_t... Ms>
    constexpr auto join(StaticStr<M> first, StaticStr<Ms>... rest) const {
        StaticStr<first.size() + (... + (N + rest.size()))> result;
        std::copy_n(first.buffer, first.size(), result.buffer);
        size_t offset = first.size();
        (
            (
                std::copy_n(buffer, N, result.buffer + offset),
                offset += N,
                std::copy_n(rest.buffer, rest.size(), result.buffer + offset),
                offset += rest.size()
            ),
            ...
        );
        result.buffer[offset] = '\0';
        return result;
    }

    /* Equivalent to Python `str.join()`, but computed at compile time. */
    template <size_t M, size_t... Ms>
    constexpr auto join(const char(& first)[M], const char(&... rest)[Ms]) const {
        return join(StaticStr(first), StaticStr(rest)...);
    }

    /* Equivalent to Python `str.startswith()`, but computed at compile time. */
    template <size_t M>
    constexpr bool startswith(const StaticStr<M>& prefix) const {
        if (M > N) {
            return false;
        }
        return std::equal(prefix.buffer, prefix.buffer + M, buffer);
    }

    /* Equivalent to Python `str.swapcase()`, but computed at compile time. */
    constexpr StaticStr<N> swapcase() const {
        StaticStr<N> result;
        for (size_t i = 0; i < N; ++i) {
            char c = buffer[i];
            if (Cases::islower(c)) {
                result.buffer[i] = Cases::toupper(c);
            } else if (Cases::isupper(c)) {
                result.buffer[i] = Cases::tolower(c);
            } else {
                result.buffer[i] = c;
            }
        }
        result.buffer[N] = '\0';
        return result;
    }

    /* Equivalent to Python `str.ljust()`, but computed at compile time. */
    template <size_t width>
    constexpr StaticStr<width <= N ? N : width> ljust(char fillchar=' ') const {
        if constexpr (width <= N) {
            return *this;
        } else {
            StaticStr<width> result;
            std::copy_n(buffer, N, result.buffer);
            std::fill_n(result.buffer + N, width - N, fillchar);
            result.buffer[width] = '\0';
            return result;
        }
    }

    /* Equivalent to Python `str.lower()`, but computed at compile time. */
    constexpr StaticStr<N> lower() const {
        StaticStr<N> result;
        for (size_t i = 0; i < N; ++i) {
            result.buffer[i] = Cases::tolower(buffer[i]);
        }
        result.buffer[N] = '\0';
        return result;
    }

    // TODO: replace compiles if and only if self is a string of length 1 for
    // some reason.  This seems to be extremely finnicky, and I can't do the obvious
    // thing since `this` is not a constant expression.  This is really close to
    // working, though.

    /* Equivalent to Python `str.replace()`, but computed at compile time. */
    template <size_t max_count = not_found, size_t M, size_t O>
    static constexpr auto replace(
        StaticStr<N> self,
        StaticStr<M> sub,
        StaticStr<O> repl
    ) {
        constexpr size_t freq = self.count(sub);
        constexpr size_t size = N - (freq * M) + (freq * O);
        StaticStr<size> result;
        size_t offset = 0;
        size_t count = 0;
        for (size_t i = 0; i < N;) {
            if (count < max_count && std::equal(sub.buffer, sub.buffer + M, self.buffer + i)) {
                std::copy_n(repl.buffer, O, result.buffer + offset);
                offset += O;
                i += M;
                ++count;
            } else {
                result.buffer[offset++] = self.buffer[i++];
            }
        }
        result.buffer[size] = '\0';
        return result;
    }

    /* Equivalent to Python `str.replace()`, but computed at compile time. */
    template <size_t max_count = not_found, size_t M, size_t O>
    static constexpr auto replace(
        StaticStr<N> self,
        const char(&sub)[M],
        const char(&repl)[O]
    ) {
        return replace<max_count>(self, StaticStr<M - 1>{sub}, StaticStr<O - 1>{repl});
    }

    /* Equivalent to Python `str.rfind()`, but computed at compile time. */
    template <size_t start = 0, size_t stop = N, size_t M>
    constexpr size_t rfind(const StaticStr<M>& sub) {
        static_assert(
            (start >= 0 && start < N) && (stop >= start && stop <= N),
            "start must be less than or equal to stop and both must be within the "
            "bounds of the string"
        );
        if constexpr ((stop - start) < M) {
            return 0;
        }
        for (size_t i = stop - M; i >= start; --i) {
            if (std::equal(sub.buffer, sub.buffer + M, buffer + i)) {
                return i;
            }
        }
        return not_found;
    }

    /* Equivalent to Python `str.rjust()`, but computed at compile time. */
    template <size_t width>
    constexpr StaticStr<width <= N ? N : width> rjust(char fillchar=' ') const {
        if constexpr (width <= N) {
            return *this;
        } else {
            StaticStr<width> result;
            std::fill_n(result.buffer, width - N, fillchar);
            std::copy_n(buffer, N, result.buffer + width - N);
            result.buffer[width] = '\0';
            return result;
        }
    }

    /* Equivalent to Python `str.title()`, but computed at compile time. */
    constexpr StaticStr<N> title() const {
        StaticStr<N> result;
        bool capitalize_next = true;
        for (size_t i = 0; i < N; ++i) {
            char c = buffer[i];
            if (Cases::isalpha(c)) {
                if (capitalize_next) {
                    result.buffer[i] = Cases::toupper(c);
                    capitalize_next = false;
                } else {
                    result.buffer[i] = Cases::tolower(c);
                }
            } else {
                if (Cases::is_delimeter(c)) {
                    capitalize_next = true;
                }
                result.buffer[i] = c;
            }
        }
        result.buffer[N] = '\0';
        return result;
    }

    /* Equivalent to Python `str.upper()`, but computed at compile time. */
    constexpr StaticStr<N> upper() const {
        StaticStr<N> result;
        for (size_t i = 0; i < N; ++i) {
            result.buffer[i] = Cases::toupper(buffer[i]);
        }
        result.buffer[N] = '\0';
        return result;
    }

    /* Equivalent to Python `str.zfill()`, but computed at compile time. */
    template <size_t width>
    constexpr StaticStr<width <= N ? N : width> zfill() const {
        if constexpr (width <= N) {
            return *this;
        } else {
            StaticStr<width> result;
            size_t start = 0;
            if (buffer[0] == '+' || buffer[0] == '-') {
                result.buffer[0] = buffer[0];
                start = 1;
            }
            std::fill_n(result.buffer + start, width - N, '0');
            std::copy_n(buffer + start, N - start, result.buffer + width - N + start);
            result.buffer[width] = '\0';
            return result;
        }
    }

    // TODO: replace, expandtabs, split, splitlines, strip, lstrip, rstrip, partition,  
    // rpartition, removeprefix, removesuffix.  Maybe format?  Could I maybe do that
    // with std::format?

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    // TODO: comparison operators

    constexpr size_t size() const {
        return N;
    }

    const char* begin() const {
        return buffer;
    }

    const char* end() const {
        return buffer + N;
    }

    ReverseIterator rbegin() const {
        return {buffer + N - 1, N - 1};
    }

    ReverseIterator rend() const {
        return {nullptr, -1};
    }

    // TODO: support slice syntax for compile-time strings

    constexpr char operator[](size_t i) const {
        return buffer[i];
    }

    template <size_t M>
    constexpr StaticStr<N + M> operator+(const StaticStr<M>& other) const {
        StaticStr<N + M> result;
        std::copy_n(buffer, N, result.buffer);
        std::copy_n(other.buffer, M, result.buffer + N);
        result.buffer[N + M] = '\0';
        return result;
    }

    template <size_t M>
    constexpr StaticStr<N + M - 1> operator+(const char(&arr)[M]) const {
        return *this + StaticStr<M - 1>(arr);
    }

    // NOTE: C++20 does not allow the repetition operator to be overloaded at compile
    // time, so we have to use a template method instead.  This might be fixed in
    // C++23, but for now, this is the best we can do.

    template <size_t Reps>
    constexpr StaticStr<N * (Reps > 0 ? Reps : 0)> repeat() const {
        if constexpr (Reps <= 0) {
            return StaticStr<0>();
        } else {
            StaticStr<N * Reps> result;
            for (size_t i = 0; i < Reps; ++i) {
                std::copy_n(buffer, N, result.buffer + i * N);
            }
            result.buffer[N * Reps] = '\0';
            return result;
        }
    }

    template <typename T = void>
    constexpr void operator*(size_t reps) const {
        static_assert(
            !std::is_void_v<T>,
            "Due to limitations in the C++ language, the `*` operator cannot be "
            "supported for compile-time string manipulation.  Use "
            "`str.template repeat<N>()` instead, which has identical semantics."
        );
    }

};


template <size_t N>
StaticStr(const char(&arr)[N]) -> StaticStr<N-1>;


}  // namespace bertrand


namespace std {

    template <size_t N>
    struct hash<bertrand::StaticStr<N>> {
        static constexpr size_t FNV_basis = 0xcbf29ce484222325;  // typical 64-bit basis
        static constexpr size_t FNV_prime = 0x00000100000001B3;  // typical 64-bit prime

        constexpr size_t operator()(const bertrand::StaticStr<N>& str) const {
            size_t result = FNV_basis;
            for (size_t i = 0; i < N; ++i) {
                result = (result * FNV_prime) ^ str.buffer[i];
            }
            return result;
        }
    };

}


#endif  // BERTRAND_COMPILE_TIME_STRING_H
