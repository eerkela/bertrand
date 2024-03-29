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

public:
    char buffer[N + 1] = {};  // +1 for null terminator

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    consteval StaticStr() = default;

    consteval StaticStr(const char(&arr)[N + 1]) {
        std::copy_n(arr, N + 1, buffer);
    }

    ///////////////////////////
    ////    CONVERSIONS    ////
    ///////////////////////////

    constexpr operator const char*() const {
        return buffer;
    }

    operator std::string() const {
        return {buffer, N};
    }

    constexpr operator std::string_view() const {
        return {buffer, N};
    }

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    consteval size_t size() const {
        return N;
    }

    constexpr const char* begin() const {
        return buffer;
    }

    constexpr const char* end() const {
        return buffer + N;
    }

    constexpr ReverseIterator rbegin() const {
        return {buffer + N - 1, N - 1};
    }

    constexpr ReverseIterator rend() const {
        return {nullptr, -1};
    }

    // TODO: support slice syntax for compile-time strings

    constexpr char operator[](size_t i) const {
        return buffer[i];
    }

    template <size_t M>
    consteval bool operator<(const StaticStr<M>& other) const {
        auto it1 = begin();
        auto end1 = end();
        auto it2 = other.begin();
        auto end2 = other.end();

        while (it1 != end1 && it2 != end2) {
            auto x = *it1;
            auto y = *it2;
            if (x < y) {
                return true;
            } else if (y < x) {
                return false;
            }
            ++it1;
            ++it2;
        }

        return (it1 == end1) && (it2 != end2);
    }

    template <size_t M>
    consteval bool operator<=(const StaticStr<M>& other) const {
        auto it1 = begin();
        auto end1 = end();
        auto it2 = other.begin();
        auto end2 = other.end();

        while (it1 != end1 && it2 != end2) {
            auto x = *it1;
            auto y = *it2;
            if (x < y) {
                return true;
            } else if (y < x) {
                return false;
            }
            ++it1;
            ++it2;
        }

        return it1 == end1;
    }

    template <size_t M>
    consteval bool operator==(const StaticStr<M>& other) const {
        if constexpr (N != M) {
            return false;
        }

        auto it1 = begin();
        auto end1 = end();
        auto it2 = other.begin();

        while (it1 != end1) {
            if (*it1 != *it2) {
                return false;
            }
            ++it1;
            ++it2;
        }

        return true;
    }

    template <size_t M>
    consteval bool operator!=(const StaticStr<M>& other) const {
        return !operator==(other);
    }

    template <size_t M>
    consteval bool operator>=(const StaticStr<M>& other) const {
        auto it1 = begin();
        auto end1 = end();
        auto it2 = other.begin();
        auto end2 = other.end();

        while (it1 != end1 && it2 != end2) {
            auto x = *it1;
            auto y = *it2;
            if (x > y) {
                return true;
            } else if (y > x) {
                return false;
            }
            ++it1;
            ++it2;
        }

        return it2 == end2;
    }

    template <size_t M>
    consteval bool operator>(const StaticStr<M>& other) const {
        auto it1 = begin();
        auto end1 = end();
        auto it2 = other.begin();
        auto end2 = other.end();

        while (it1 != end1 && it2 != end2) {
            auto x = *it1;
            auto y = *it2;
            if (x > y) {
                return true;
            } else if (y > x) {
                return false;
            }
            ++it1;
            ++it2;
        }

        return it1 != end1 && it2 == end2;
    }

    template <size_t M>
    consteval StaticStr<N + M> operator+(const StaticStr<M>& other) const {
        StaticStr<N + M> result;
        std::copy_n(buffer, N, result.buffer);
        std::copy_n(other.buffer, M, result.buffer + N);
        result.buffer[N + M] = '\0';
        return result;
    }

    template <size_t M>
    consteval StaticStr<N + M - 1> operator+(const char(&arr)[M]) const {
        return *this + StaticStr<M - 1>(arr);
    }

    template <size_t M>
    consteval friend StaticStr<N + M - 1> operator+(
        const char(&arr)[M],
        const StaticStr<N>& self
    ) {
        return StaticStr<M - 1>(arr) + self;
    }

    // NOTE: C++20 does not allow the repetition operator to be overloaded at compile
    // time, so we have to use a template method instead.  This might be fixed in
    // a future standard, but for now, it's the best we can do.

    template <typename T = void>
    consteval void operator*(size_t reps) const {
        static_assert(
            !std::is_void_v<T>,
            "Due to limitations in the C++ language, the `*` operator cannot be "
            "supported for compile-time string manipulation.  Use "
            "`static_str::repeat<str, N>` instead, which has identical semantics."
        );
    }

    template <typename T = void>
    consteval friend void operator*(size_t reps, const StaticStr<N>& self) {
        static_assert(
            !std::is_void_v<T>,
            "Due to limitations in the C++ language, the `*` operator cannot be "
            "supported for compile-time string manipulation.  Use "
            "`static_str::repeat<str, N>` instead, which has identical semantics."
        );
    }

};


/* CTAD deduction guide that allows StaticStr to be built from string literals using
compile-time aggregate initializatiion. */
template <size_t N>
StaticStr(const char(&arr)[N]) -> StaticStr<N-1>;


/* Compile-time string manipulations must be defined as free functions in order to
avoid issues with template deduction and `'this' is not a constant expression`
errors.  These seem to be running up against some hard limitations in C++. */
namespace static_str {

    constexpr size_t not_found = std::numeric_limits<size_t>::max();

    namespace detail {

        static constexpr char LOWER_TO_UPPER = 'A' - 'a';
        static constexpr char UPPER_TO_LOWER = 'a' - 'A';

        constexpr bool islower(char c) {
            return c >= 'a' && c <= 'z';
        }

        constexpr bool isupper(char c) {
            return c >= 'A' && c <= 'Z';
        }

        constexpr bool isalpha(char c) {
            return islower(c) || isupper(c);
        }

        constexpr bool isdigit(char c) {
            return c >= '0' && c <= '9';
        }

        constexpr bool isalnum(char c) {
            return isalpha(c) || isdigit(c);
        }

        constexpr bool isascii(char c) {
            return c >= 0 && c <= 127;
        }

        constexpr bool isspace(char c) {
            return c == ' ' || (c >= '\t' && c <= '\r');
        }

        /* Check if a character delimits a word boundary.  NOTE: this is only stable
        for ASCII-encoded text, which most string literals should be. */
        constexpr bool isdelimeter(char c) {
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

        constexpr char tolower(char c) {
            return isupper(c) ? c + UPPER_TO_LOWER : c;
        }

        constexpr char toupper(char c) {
            return islower(c) ? c + LOWER_TO_UPPER : c;
        }

        /* Helper for getting the first non-stripped index from the beginning of a
        string. */
        template <StaticStr str, StaticStr chars>
        constexpr size_t first_non_stripped() {
            for (size_t i = 0; i < str.size(); ++i) {
                char c = str.buffer[i];
                size_t j = 0;
                while (j < chars.size()) {
                    if (chars.buffer[j++] == c) {
                        break;
                    } else if (j == chars.size()) {
                        return i;
                    }
                }
            }
            return not_found;
        }

        /* Helper for getting the last non-stripped index from the end of a string. */
        template <StaticStr str, StaticStr chars>
        constexpr size_t last_non_stripped() {
            for (size_t i = 0; i < str.size(); ++i) {
                size_t idx = str.size() - i - 1;
                char c = str.buffer[idx];
                size_t j = 0;
                while (j < chars.size()) {
                    if (chars.buffer[j++] == c) {
                        break;
                    } else if (j == chars.size()) {
                        return idx;
                    }
                }
            }
            return not_found;
        }

        /* Helper for getting the length of each component in a split() operation. */
        template <StaticStr str, StaticStr sep, size_t n>
        constexpr std::array<size_t, n> forward_strides() {
            std::array<size_t, n> result;
            size_t prev = 0;
            for (size_t i = prev, j = 0; j < n - 1;) {
                if (std::equal(sep.buffer, sep.buffer + sep.size(), str.buffer + i)) {
                    result[j++] = i - prev;
                    i += sep.size();
                    prev = i;
                } else {
                    ++i;
                }
            }
            result[n - 1] = str.size() - prev;
            return result;
        }

        /* Helper for getting the length of each component in an rsplit() operation. */
        template <StaticStr str, StaticStr sep, size_t n>
        constexpr std::array<size_t, n> backward_strides() {
            std::array<size_t, n> result;
            size_t prev = str.size() - 1;
            for (size_t i = prev, j = 0; j < n - 1;) {
                if (std::equal(sep.buffer, sep.buffer + sep.size(), str.buffer + i)) {
                    result[j++] = prev - i;
                    i -= sep.size();
                    prev = i;
                } else {
                    --i;
                }
            }
            result[n - 1] = prev;
            return result;
        }

        /* Helper function to extract split components from a string at compile
        time. */
        template <StaticStr str, StaticStr sep, size_t... Ns>
        constexpr auto forward_split(std::index_sequence<Ns...>) {
            constexpr std::array<size_t, sizeof...(Ns)> strides = 
                forward_strides<str, sep, sizeof...(Ns)>();

            std::tuple<StaticStr<std::get<Ns>(strides)>...> result;
            size_t offset = 0;
            (
                (
                    std::copy_n(
                        str.buffer + offset,
                        strides[Ns],
                        std::get<Ns>(result).buffer
                    ),
                    offset += strides[Ns] + sep.size()
                ),
                ...
            );
            return result;
        }

        /* Helper function to extract split components from a string at compile
        time. */
        template <StaticStr str, StaticStr sep, size_t... Ns>
        constexpr auto backward_split(std::index_sequence<Ns...>) {
            constexpr std::array<size_t, sizeof...(Ns)> strides =
                backward_strides<str, sep, sizeof...(Ns)>();

            std::tuple<StaticStr<std::get<Ns>(strides)>...> result;
            size_t offset = str.size();  // TODO: is this correct?
            (
                (
                    offset -= strides[Ns],
                    std::copy_n(
                        str.buffer + offset,
                        strides[Ns],
                        std::get<Ns>(result).buffer
                    ),
                    offset -= sep.size()
                ),
                ...
            );
            return result;
        }

    };

    /* Equivalent to Python `str.capitalize()`, but evaluated statically at compile
    time. */
    template <StaticStr self>
    constexpr auto capitalize = [] {
        StaticStr<self.size()> result;
        bool capitalized = false;
        for (size_t i = 0; i < self.size(); ++i) {
            char c = self.buffer[i];
            if (detail::isalpha(c)) {
                if (capitalized) {
                    result.buffer[i] = detail::tolower(c);
                } else {
                    result.buffer[i] = detail::toupper(c);
                    capitalized = true;
                }
            } else {
                result.buffer[i] = c;
            }
        }
        result.buffer[self.size()] = '\0';
        return result;
    }();

    /* Equivalent to Python `str.center(width[, fillchar])`, but evaluated statically
    at compile time. */
    template <StaticStr self, size_t width, char fillchar = ' '>
    constexpr auto center = [] {
        if constexpr (width <= self.size()) {
            return self;
        } else {
            StaticStr<width> result;
            size_t left = (width - self.size()) / 2;
            size_t right = width - self.size() - left;
            std::fill_n(result.buffer, left, fillchar);
            std::copy_n(self.buffer, self.size(), result.buffer + left);
            std::fill_n(result.buffer + left + self.size(), right, fillchar);
            result.buffer[width] = '\0';
            return result;
        }
    }();

    /* Equivalent to Python `str.count(sub[, start[, stop]])`, but evaluated statically
    at compile time. */
    template <StaticStr self, StaticStr sub, size_t start = 0, size_t stop = self.size()>
    constexpr size_t count = [] {
        static_assert(
            start >= 0 && start <= stop && stop <= self.size(),
            "start must be less than or equal to stop and both must be within the "
            "bounds of the string"
        );
        if constexpr ((stop - start) < sub.size()) {
            return not_found;
        } else {
            size_t count = 0;
            for (size_t i = start; i < stop - sub.size(); ++i) {
                if (std::equal(sub.buffer, sub.buffer + sub.size(), self.buffer + i)) {
                    ++count;
                }
            }
            return count;
        }
    }();

    /* Equivalent to Python `str.endswith(suffix)`, but evaluated statically at
    compile time. */
    template <StaticStr self, StaticStr suffix>
    constexpr bool endswith = [] {
        return (
            suffix.size() <= self.size() &&
            std::equal(
                suffix.buffer,
                suffix.buffer + suffix.size(),
                self.buffer + self.size() - suffix.size()
            )
        );
    }();

    /* Equivalent to Python `str.expandtabs([tabsize])`, but evaluated statically at
    compile time. */
    template <StaticStr self, size_t tabsize = 8>
    constexpr auto expandtabs = [] {
        constexpr size_t n = count<self, "\t">;
        StaticStr<self.size() - n + n * tabsize> result;
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
    }();

    /* Equivalent to Python `str.find(sub[, start[, stop]])`, but evaluated statically
    at compile time.  Returns `static_str::not_found` if the substring is not
    present. */
    template <StaticStr self, StaticStr sub, size_t start = 0, size_t stop = self.size()>
    constexpr size_t find = [] {
        static_assert(
            start >= 0 && start <= stop && stop <= self.size(),
            "start must be less than or equal to stop and both must be within the "
            "bounds of the string"
        );
        if constexpr ((stop - start) < sub.size()) {
            return not_found;
        } else {
            for (size_t i = start; i < stop - sub.size(); ++i) {
                if (std::equal(sub.buffer, sub.buffer + sub.size(), self.buffer + i)) {
                    return i;
                }
            }
            return not_found;
        }
    }();

    /* Equivalent to Python `str.index(sub[, start[, stop]])`, but evaluated statically
    at compile time.  Throws a compile error if the substring is not present. */
    template <StaticStr self, StaticStr sub, size_t start = 0, size_t stop = self.size()>
    constexpr size_t index = [] {
        constexpr size_t result = find<self, sub, start, stop>;
        static_assert(result != not_found, "substring not found");
        return result;
    }();

    /* Equivalent to Python `str.isalpha()`, but evaluated statically at compile
    time. */
    template <StaticStr self>
    constexpr bool isalpha = [] {
        for (size_t i = 0; i < self.size(); ++i) {
            if (!detail::isalpha(self.buffer[i])) {
                return false;
            }
        }
        return self.size() > 0;
    }();

    /* Equivalent to Python `str.isalnum()`, but evaluated statically at compile
    time. */
    template <StaticStr self>
    constexpr bool isalnum = [] {
        for (size_t i = 0; i < self.size(); ++i) {
            if (!detail::isalnum(self.buffer[i])) {
                return false;
            }
        }
        return self.size() > 0;
    }();

    /* Equivalent to Python `str.isascii()`, but evaluated statically at compile
    time. */
    template <StaticStr self>
    constexpr bool isascii = [] {
        for (size_t i = 0; i < self.size(); ++i) {
            if (!detail::isascii(self.buffer[i])) {
                return false;
            }
        }
        return self.size() > 0;
    }();

    /* Equivalent to Python `str.isdigit()`, but evaluated statically at compile
    time. */
    template <StaticStr self>
    constexpr bool isdigit = [] {
        for (size_t i = 0; i < self.size(); ++i) {
            if (!detail::isdigit(self.buffer[i])) {
                return false;
            }
        }
        return self.size() > 0;
    }();

    /* Equivalent to Python `str.islower()`, but evaluated statically at compile
    time. */
    template <StaticStr self>
    constexpr bool islower = [] {
        for (size_t i = 0; i < self.size(); ++i) {
            if (!detail::islower(self.buffer[i])) {
                return false;
            }
        }
        return self.size() > 0;
    }();

    /* Equivalent to Python `str.isspace()`, but evaluated statically at compile
    time. */
    template <StaticStr self>
    constexpr bool isspace = [] {
        for (size_t i = 0; i < self.size(); ++i) {
            if (!detail::isspace(self.buffer[i])) {
                return false;
            }
        }
        return self.size() > 0;
    }();

    /* Equivalent to Python `str.istitle()`, but evaluated statically at compile
    time. */
    template <StaticStr self>
    constexpr bool istitle = [] {
        bool last_was_delimeter = true;
        for (size_t i = 0; i < self.size(); ++i) {
            char c = self.buffer[i];
            if (last_was_delimeter && detail::islower(c)) {
                return false;
            }
            last_was_delimeter = detail::isdelimeter(c);
        }
        return self.size() > 0;
    }();

    /* Equivalent to Python `str.isupper()`, but evaluated statically at compile
    time. */
    template <StaticStr self>
    constexpr bool isupper = [] {
        for (size_t i = 0; i < self.size(); ++i) {
            if (!detail::isupper(self.buffer[i])) {
                return false;
            }
        }
        return self.size() > 0;
    }();

    /* Equivalent to Python `str.join(strings...)`, but evaluated statically at compile
    time. */
    template <StaticStr self, StaticStr first, StaticStr... rest>
    constexpr auto join = [] {
        StaticStr<first.size() + (... + (self.size() + rest.size()))> result;
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
    }();

    /* Equivalent to Python `str.ljust(width[, fillchar])`, but evaluated statically at
    compile time. */
    template <StaticStr self, size_t width, char fillchar = ' '>
    constexpr auto ljust = [] {
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
    }();

    /* Equivalent to Python `str.lower()`, but evaluated statically at compile time. */
    template <StaticStr self>
    constexpr auto lower = [] {
        StaticStr<self.size()> result;
        for (size_t i = 0; i < self.size(); ++i) {
            result.buffer[i] = convert_lower(self.buffer[i]);
        }
        result.buffer[self.size()] = '\0';
        return result;
    }();

    /* Equivalent to Python `str.lstrip([chars])`, but evaluated statically at compile
    time. */
    template <StaticStr self, StaticStr chars = " \t\n\r\f\v">
    constexpr auto lstrip = [] {
        constexpr size_t start = detail::first_non_stripped<self, chars>();
        if constexpr (start == not_found) {
            StaticStr<0> result;
            result.buffer[0] = '\0';
            return result;
        } else {
            constexpr size_t delta = self.size() - start;
            StaticStr<delta> result;
            std::copy_n(self.buffer + start, delta, result.buffer);
            result.buffer[delta] = '\0';
            return result;
        }
    }();

    /* Equivalent to Python `str.partition(sep)`, but evaluated statically at compile
    time. */
    template <StaticStr self, StaticStr sep>
    constexpr auto partition = [] {
        constexpr size_t index = find<self, sep>;
        if constexpr (index == not_found) {
            StaticStr<0> second;
            StaticStr<0> third;
            second.buffer[0] = '\0';
            third.buffer[0] = '\0';
            return std::make_tuple(self, second, third);
        } else {
            constexpr size_t remaining = self.size() - index - sep.size();
            StaticStr<index> first;
            StaticStr<remaining> third;
            std::copy_n(self.buffer, index, first.buffer);
            std::copy_n(self.buffer + index + sep.size(), remaining, third.buffer);
            first.buffer[index] = '\0';
            third.buffer[remaining] = '\0';
            return std::make_tuple(first, sep, third);
        }
    }();

    /* Equivalent to Python `str.replace()`, but evaluated statically at compile
    time. */
    template <StaticStr self, StaticStr sub, StaticStr repl, size_t max_count = not_found>
    constexpr auto replace = [] {
        constexpr size_t freq = count<self, sub>;
        constexpr size_t n = freq < max_count ? freq : max_count;
        StaticStr<self.size() - (n * sub.size()) + (n * repl.size())> result;
        size_t offset = 0;
        size_t count = 0;
        for (size_t i = 0; i < self.size();) {
            if (
                count < max_count &&
                std::equal(sub.buffer, sub.buffer + sub.size(), self.buffer + i)
            ) {
                std::copy_n(
                    repl.buffer,
                    repl.size(),
                    result.buffer + offset
                );
                offset += repl.size();
                i += sub.size();
                ++count;
            } else {
                result.buffer[offset++] = self.buffer[i++];
            }
        }
        result.buffer[result.size()] = '\0';
        return result;
    }();

    /* Equivalent to Python `str.rfind(sub[, start[, stop]])`, but evaluated statically
    at compile time. */
    template <StaticStr self, StaticStr sub, size_t start = 0, size_t stop = self.size()>
    constexpr size_t rfind = [] {
        static_assert(
            start >= 0 && start <= stop && stop <= self.size(),
            "start must be less than or equal to stop and both must be within the "
            "bounds of the string"
        );
        if constexpr ((stop - start) < sub.size()) {
            return not_found;
        }
        for (size_t i = stop - sub.size(); i >= start; --i) {
            if (std::equal(sub.buffer, sub.buffer + sub.size(), self.buffer + i)) {
                return i;
            }
        }
        return not_found;
    }();

    /* Equivalent to Python `str.rindex(sub[, start[, stop]])`, but evaluated statically
    at compile time. */
    template <StaticStr self, StaticStr sub, size_t start = 0, size_t stop = self.size()>
    constexpr size_t rindex = [] {
        constexpr size_t result = rfind<self, sub, start, stop>;
        static_assert(result != not_found, "substring not found");
        return result;
    }();

    /* Equivalent to Python `str.rjust(width[, fillchar])`, but evaluated statically at
    compile time. */
    template <StaticStr self, size_t width, char fillchar = ' '>
    constexpr auto rjust = [] {
        if constexpr (width <= self.size()) {
            return self;
        } else {
            StaticStr<width> result;
            std::fill_n(result.buffer, width - self.size(), fillchar);
            std::copy_n(
                self.buffer,
                self.size(),
                result.buffer + width - self.size()
            );
            result.buffer[width] = '\0';
            return result;
        }
    }();

    /* Equivalent to Python `str.rpartition(sep)`, but evaluated statically at compile
    time. */
    template <StaticStr self, StaticStr sep>
    constexpr auto rpartition = [] {
        constexpr size_t index = find<self, sep>;
        if constexpr (index == not_found) {
            StaticStr<0> second;
            StaticStr<0> third;
            second.buffer[0] = '\0';
            third.buffer[0] = '\0';
            return std::make_tuple(self, second, third);
        } else {
            constexpr size_t remaining = self.size() - index - sep.size();
            StaticStr<index> first;
            StaticStr<remaining> third;
            std::copy_n(self.buffer, index, first.buffer);
            std::copy_n(self.buffer + index + sep.size(), remaining, third.buffer);
            first.buffer[index] = '\0';
            third.buffer[remaining] = '\0';
            return std::make_tuple(first, sep, third);
        }
    }();

    /* Equivalent to Python `str.rsplit(sep[, maxsplit])`, but evaluated statically at
    compile time. */
    template <StaticStr self, StaticStr sep, size_t maxsplit = not_found>
    constexpr auto rsplit = [] {
        constexpr size_t freq = count<self, sep>;
        if constexpr (freq == 0) {
            return std::make_tuple(self);
        } else {
            constexpr size_t n = freq < maxsplit ? freq : maxsplit;
            return detail::backward_split<self, sep>(
                std::make_index_sequence<n + 1>{}
            );
        }
    }();

    /* Equivalent to Python `str.rstrip([chars])`, but evaluated statically at compile
    time. */
    template <StaticStr self, StaticStr chars = " \t\n\r\f\v">
    constexpr auto rstrip = [] {
        constexpr size_t stop = detail::last_non_stripped<self, chars>();
        if constexpr (stop == not_found) {
            StaticStr<0> result;
            result.buffer[0] = '\0';
            return result;
        } else {
            constexpr size_t delta = stop + 1;
            StaticStr<delta> result;
            std::copy_n(self.buffer, delta, result.buffer);
            result.buffer[delta] = '\0';
            return result;
        }
    }();

    /* Equivalent to Python `str.split(sep[, maxsplit])`, but evaluated statically at
    compile time. */
    template <StaticStr self, StaticStr sep, size_t maxsplit = not_found>
    constexpr auto split = [] {
        constexpr size_t freq = count<self, sep>;
        if constexpr (freq == 0) {
            return std::make_tuple(self);
        } else {
            constexpr size_t n = freq < maxsplit ? freq : maxsplit;
            return detail::forward_split<self, sep>(
                std::make_index_sequence<n + 1>{}
            );
        }
    }();

    // TODO: splitlines

    /* Equivalent to Python `str.startswith(prefix)`, but evaluated statically at
    compile time. */
    template <StaticStr self, StaticStr prefix>
    constexpr bool startswith = [] {
        return (
            prefix.size() <= self.size() &&
            std::equal(prefix.buffer, prefix.buffer + prefix.size(), self.buffer)
        );
    }();

    /* Equivalent to Python `str.strip([chars])`, but evaluated statically at compile
    time. */
    template <StaticStr self, StaticStr chars = " \t\n\r\f\v">
    constexpr auto strip = [] {
        constexpr size_t start = detail::first_non_stripped<self, chars>();
        if constexpr (start == not_found) {
            StaticStr<0> result;
            result.buffer[0] = '\0';
            return result;
        } else {
            constexpr size_t stop = detail::last_non_stripped<self, chars>();
            constexpr size_t delta = stop - start + 1;  // +1 for half-open interval
            StaticStr<delta> result;
            std::copy_n(self.buffer + start, delta, result.buffer);
            result.buffer[delta] = '\0';
            return result;
        }
    }();

    /* Equivalent to Python `str.swapcase()`, but evaluated statically at compile
    time. */
    template <StaticStr self>
    constexpr auto swapcase = [] {
        StaticStr<self.size()> result;
        for (size_t i = 0; i < self.size(); ++i) {
            char c = self.buffer[i];
            if (detail::islower(c)) {
                result.buffer[i] = detail::toupper(c);
            } else if (detail::isupper(c)) {
                result.buffer[i] = detail::tolower(c);
            } else {
                result.buffer[i] = c;
            }
        }
        result.buffer[self.size()] = '\0';
        return result;
    }();

    /* Equivalent to Python `str.title()`, but evaluated statically at compile time. */
    template <StaticStr self>
    constexpr auto title = [] {
        StaticStr<self.size()> result;
        bool capitalize_next = true;
        for (size_t i = 0; i < self.size(); ++i) {
            char c = self.buffer[i];
            if (detail::isalpha(c)) {
                if (capitalize_next) {
                    result.buffer[i] = detail::toupper(c);
                    capitalize_next = false;
                } else {
                    result.buffer[i] = detail::tolower(c);
                }
            } else {
                if (detail::isdelimeter(c)) {
                    capitalize_next = true;
                }
                result.buffer[i] = c;
            }
        }
        result.buffer[self.size()] = '\0';
        return result;
    }();

    /* Equivalent to Python `str.upper()`, but evaluated statically at compile time. */
    template <StaticStr self>
    constexpr auto upper = [] {
        StaticStr<self.size()> result;
        for (size_t i = 0; i < self.size(); ++i) {
            result.buffer[i] = convert_upper(self.buffer[i]);
        }
        result.buffer[self.size()] = '\0';
        return result;
    }();

    /* Equivalent to Python `str.zfill(width)`, but evaluated statically at compile
    time. */
    template <StaticStr self, size_t width>
    constexpr auto zfill = [] {
        if constexpr (width <= self.size()) {
            return self;
        } else {
            StaticStr<width> result;
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
                self.size() - start,
                result.buffer + width - self.size() + start
            );
            result.buffer[width] = '\0';
            return result;
        }
    }();

    /* Equivalent to Python `sub in str`, but evaluated statically at compile time. */
    template <StaticStr self, StaticStr sub>
    constexpr bool contains = [] {
        return find<self, sub> != not_found;
    }();

    /* Equivalent to Python `str.removeprefix()`, but evaluated statically at compile
    time. */
    template <StaticStr self, StaticStr prefix>
    constexpr auto removeprefix = [] {
        if constexpr (!startswith<self, prefix>) {
            return self;
        } else {
            StaticStr<self.size() - prefix.size()> result;
            std::copy_n(
                self.buffer + prefix.size(),
                self.size() - prefix.size(),
                result.buffer
            );
            return result;
        }
    }();

    /* Equivalent to Python `str.removesuffix()`, but evaluated statically at compile
    time. */
    template <StaticStr self, StaticStr suffix>
    constexpr auto removesuffix = [] {
        if constexpr (!endswith<self, suffix>) {
            return self;
        } else {
            StaticStr<self.size() - suffix.size()> result;
            std::copy_n(
                self.buffer,
                self.size() - suffix.size(),
                result.buffer
            );
            return result;
        }
    }();


    // TODO: rsplit, splitlines,

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    /* Functional alternative to `StaticStr + StaticStr`. */
    template <StaticStr lhs, StaticStr rhs>
    constexpr auto concat = [] {
        return lhs + rhs;
    }();

    /* Replacement for `StaticStr * reps` that allows it to be evaluated entirely at
    compile time. */
    template <StaticStr self, size_t reps>
    constexpr auto repeat = [] {
        if constexpr (reps <= 0) {
            StaticStr<0> result;
            result.buffer[0] = '\0';
            return result;
        } else {
            StaticStr<self.size() * reps> result;
            for (size_t i = 0; i < reps; ++i) {
                std::copy_n(
                    self.buffer,
                    self.size(),
                    result.buffer + i * self.size()
                );
            }
            result.buffer[self.size() * reps] = '\0';
            return result;
        }
    }();

}  // namespace static_str
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
